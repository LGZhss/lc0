/*
  This file is part of Leela Chess Zero.
  Copyright (C) 2018-2020 The LCZero Authors

  Leela Chess is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  Leela Chess is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with Leela Chess.  If not, see <http://www.gnu.org/licenses/>.

  Additional permission under GNU GPL version 3 section 7

  If you modify this Program, or any covered work, by linking or
  combining it with NVIDIA Corporation's libraries from the NVIDIA CUDA
  Toolkit and the NVIDIA CUDA Deep Neural Network library (or a
  modified version of those libraries), containing parts covered by the
  terms of the respective license agreement, the licensors of this
  Program grant you additional permission to convey the resulting work.
*/

#pragma once

#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <new>       // for placement new / operator new[] with alignment
#include <thread>
#include <vector>

#include "neural/network.h"
#include "utils/exception.h"

// MSVC 下 _mm_pause 需要 intrin.h
#if defined(_MSC_VER)
#include <intrin.h>
#endif

namespace lczero {

// ============================================================================
// 【魔改核心】MPMC无锁有界环形队列
// Multi-Producer Multi-Consumer Lock-Free Bounded Ring Buffer
//
// 基于 Dmitry Vyukov 的有界MPMC队列思想：
//   - 每个槽位(cell)包含一个序列号(seq)和数据指针(data)
//   - 生产者通过CAS竞争递增tail，然后写入对应槽位
//   - 消费者通过CAS竞争递增head，然后读取对应槽位
//   - 通过序列号而非独立状态变量来协调生产者/消费者
//
// 关键优势：
//   1. 完全无锁：生产者端和消费者端均使用CAS，无mutex
//   2. 无mutex争用：消除原有mutex+cv的开销（约1-3µs/次）
//   3. Cache友好：槽位紧凑排列，预取效率高
//   4. 背压自旋：队列满时生产者自旋等待，延迟极低
//   5. 多消费者安全：支持多个GPU Worker线程并发出队
//
// 内存序策略：
//   - seq 的 load 使用 acquire，确保看到前序写入
//   - seq 的 store 使用 release，确保前序写入对其他线程可见
//   - tail_/head_ 的 CAS 使用 acq_rel，同时获取和发布
//
// 注意：std::atomic<size_t> 不可拷贝，因此使用 std::unique_ptr<Cell[]>
// 代替 std::vector<Cell> 来管理槽位数组。
// ============================================================================

template <typename T>
class MpmcLockFreeQueue {
 public:
  // 队列容量必须是2的幂次
  // 512个槽位：足以容纳峰值请求，同时保证L1/L2 cache友好
  // 每个Cell约16字节(8字节seq + 8字节data)，512个 = 8KB，可放入L1 cache
  explicit MpmcLockFreeQueue(size_t capacity = 512)
      : capacity_(capacity),
        mask_(capacity - 1),
        cells_(new Cell[capacity]) {
    // 确保 capacity 是2的幂次
    assert((capacity & (capacity - 1)) == 0 && "Capacity must be power of 2");
    // 初始化序列号：槽位i的初始序列号为i
    // 含义：槽位i在经过i轮后可被再次写入
    for (size_t i = 0; i < capacity; ++i) {
      cells_[i].seq.store(i, std::memory_order_relaxed);
      cells_[i].data = nullptr;
    }
  }

  // 【生产者端】尝试入队 - 多线程安全
  // 返回 true 入队成功，false 队列满
  bool TryPush(T* item) {
    size_t pos = tail_.load(std::memory_order_relaxed);
    while (true) {
      Cell& cell = cells_[pos & mask_];
      // 读取槽位的序列号，使用acquire语义确保看到完整的数据状态
      size_t seq = cell.seq.load(std::memory_order_acquire);
      // 序列号与pos的差值决定了槽位状态：
      //   seq == pos      → 槽位为空，可写入
      //   seq < pos       → 槽位仍有数据未消费，队列满
      //   seq > pos       → 不应出现（消费者超前了）
      intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos);
      if (diff == 0) {
        // 槽位为空，CAS竞争占有该槽位
        if (tail_.compare_exchange_weak(pos, pos + 1,
                                        std::memory_order_acq_rel,
                                        std::memory_order_relaxed)) {
          // 成功占有槽位，写入数据
          cell.data = item;
          // 更新序列号为 pos+1，表示数据已就绪
          // 使用release语义确保data写入对消费者可见
          cell.seq.store(pos + 1, std::memory_order_release);
          return true;
        }
        // CAS失败，pos已被更新为最新tail值，继续重试
      } else if (diff < 0) {
        // 队列满：所有槽位都有未消费数据
        return false;
      } else {
        // seq > pos：说明其他生产者已经推进了tail但还没完成写入
        // 重新加载tail
        pos = tail_.load(std::memory_order_relaxed);
      }
    }
  }

  // 【生产者端】阻塞式入队 - 队列满时自旋等待
  void Push(T* item) {
    while (!TryPush(item)) {
      // 自旋等待：使用CPU pause指令降低功耗
      // 在x86上 _mm_pause ≈ 140时钟周期，约40ns @ 3.5GHz
      // 比mutex的1-3µs上下文切换开销小1-2个数量级
#if defined(_MSC_VER)
      _mm_pause();
#else
      __builtin_ia32_pause();
#endif
    }
  }

  // 【消费者端】尝试出队单个元素 - 多消费者线程安全
  // 返回 true 出队成功，false 队列空
  bool TryPop(T** item) {
    size_t pos = head_.load(std::memory_order_relaxed);
    while (true) {
      Cell& cell = cells_[pos & mask_];
      size_t seq = cell.seq.load(std::memory_order_acquire);
      intptr_t diff = static_cast<intptr_t>(seq) -
                      static_cast<intptr_t>(pos + 1);
      if (diff == 0) {
        // 数据就绪，CAS竞争占有该槽位
        if (head_.compare_exchange_weak(pos, pos + 1,
                                        std::memory_order_acq_rel,
                                        std::memory_order_relaxed)) {
          // 成功占有槽位，读取数据
          *item = cell.data;
          // 重置序列号为 pos + 1 + capacity，表示该槽位可被下一轮使用
          cell.seq.store(pos + 1 + capacity_, std::memory_order_release);
          return true;
        }
        // CAS失败，pos已被更新，继续重试
      } else if (diff < 0) {
        // 队列空
        return false;
      } else {
        // 不应出现
        return false;
      }
    }
  }

  // 【消费者端】批量出队 - 多消费者线程安全
  // 一次取出最多 max_count 个请求，写入 out 数组
  // 返回实际取出的数量
  // 内部循环调用 TryPop，每次CAS竞争一个槽位
  size_t TryPopBatch(T** out, size_t max_count) {
    size_t count = 0;
    while (count < max_count) {
      if (!TryPop(&out[count])) break;
      ++count;
    }
    return count;
  }

  // 【消费者端】检查队列是否为空（近似值）
  bool IsEmpty() const {
    size_t pos = head_.load(std::memory_order_acquire);
    const Cell& cell = cells_[pos & mask_];
    size_t seq = cell.seq.load(std::memory_order_acquire);
    return static_cast<intptr_t>(seq) !=
           static_cast<intptr_t>(pos + 1);
  }

  // 【消费者端】获取队列中大致的元素数量
  // 由于并发，返回值仅为近似值，用于拼单延迟判断
  size_t SizeApprox() const {
    size_t tail = tail_.load(std::memory_order_acquire);
    size_t head = head_.load(std::memory_order_acquire);
    // tail - head 是已入队的总数，但部分可能已被消费
    // 这是一个上界估计，用于拼单延迟判断足够
    return tail > head ? tail - head : 0;
  }

 private:
  // 槽位结构：序列号 + 数据指针
  // 序列号是无锁队列的核心协调机制：
  //   - 初始值 = 槽位索引i
  //   - 生产者写入后设为 pos+1（表示数据就绪）
  //   - 消费者读取后设为 pos+capacity（表示槽位可复用）
  struct Cell {
    std::atomic<size_t> seq{0};
    T* data{nullptr};
  };

  // 环形队列容量（2的幂次）
  const size_t capacity_;
  // 位掩码，用于快速取模：idx = pos & mask_
  const size_t mask_;
  // 槽位数组：使用 unique_ptr 管理动态数组
  // 原因：Cell 含 std::atomic 成员，不可拷贝，不能用 std::vector
  std::unique_ptr<Cell[]> cells_;

  // 生产者写入位置（多生产者通过CAS竞争递增）
  // alignas(64) 避免与head_的false sharing
  alignas(64) std::atomic<size_t> tail_{0};
  // 消费者读取位置（多消费者通过CAS竞争递增）
  // alignas(64) 避免与tail_的false sharing
  alignas(64) std::atomic<size_t> head_{0};
};

}  // namespace lczero
