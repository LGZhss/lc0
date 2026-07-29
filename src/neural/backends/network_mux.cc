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

// 【魔改说明】
// 本文件引入 MPMC 无锁环形队列（MpmcLockFreeQueue），替换原有的
// std::mutex + std::condition_variable 同步机制。
//
// 核心改动：
//   1. MuxingNetwork::queue_ 从 std::queue 改为 MpmcLockFreeQueue
//   2. MuxingNetwork::Enqueue() 从 mutex lock 改为无锁 Push
//   3. MuxingNetwork::Worker() 从 cv.wait() 改为无锁自旋 + 批量出队
//   4. 保留 80µs 拼单延迟逻辑（基于 SizeApprox() 判断）
//
// 性能预期：
//   - 消除 mutex 争用：MCTS 搜索线程不再因锁等待而阻塞
//   - 降低入队延迟：从 mutex 的 ~1µs 降至 atomic CAS 的 ~40ns
//   - 提高批量效率：无锁批量出队减少 Worker 循环开销

#include <algorithm>
#include <condition_variable>
#include <limits>
#include <thread>

// MSVC 下 _mm_pause 需要 intrin.h
#if defined(_MSC_VER)
#include <intrin.h>
#endif

#include "neural/backends/network_mux.h"
#include "neural/factory.h"
#include "utils/exception.h"

namespace lczero {
namespace {

class MuxingNetwork;

class MuxingComputation : public NetworkComputation {
 public:
  MuxingComputation(MuxingNetwork* network) : network_(network) {}

  void AddInput(InputPlanes&& input) override { planes_.emplace_back(input); }

  void ComputeBlocking() override;

  int GetBatchSize() const override { return planes_.size(); }

  float GetQVal(int sample) const override {
    return parent_->GetQVal(sample + idx_in_parent_);
  }

  float GetDVal(int sample) const override {
    return parent_->GetDVal(sample + idx_in_parent_);
  }

  float GetMVal(int sample) const override {
    return parent_->GetMVal(sample + idx_in_parent_);
  }

  float GetPVal(int sample, int move_id) const override {
    return parent_->GetPVal(sample + idx_in_parent_, move_id);
  }

  void PopulateToParent(std::shared_ptr<NetworkComputation> parent) {
    // Populate our batch into batch of batches.
    parent_ = parent;
    idx_in_parent_ = parent->GetBatchSize();
    for (auto& x : planes_) parent_->AddInput(std::move(x));
  }

  // 【魔改】NotifyReady 保持不变
  // 生产者线程（MCTS搜索线程）在此等待GPU推理结果
  // 这里的 mutex+cv 是"请求-响应"同步，不是"生产者-消费者"同步
  // 因此保留 mutex+cv 是合理的（等待时间 = GPU推理时间，非争用场景）
  void NotifyReady() {
    std::unique_lock<std::mutex> lock(mutex_);
    dataready_ = true;
    dataready_cv_.notify_one();
  }

 private:
  std::vector<InputPlanes> planes_;
  MuxingNetwork* network_;
  std::shared_ptr<NetworkComputation> parent_;
  int idx_in_parent_ = 0;

  // 【保留】请求-响应同步用的 mutex+cv
  // 这与队列的 mutex+cv 不同：这里用于 MCTS 线程等待 GPU 推理完成
  // 推理时间通常 1-10ms，mutex 开销可忽略
  std::mutex mutex_;
  std::condition_variable dataready_cv_;
  bool dataready_ = false;
};

class MuxingNetwork : public Network {
 public:
  MuxingNetwork(const std::optional<WeightsFile>& weights,
                const OptionsDict& options) {
    const auto parents = options.ListSubdicts();
    if (parents.empty()) {
      auto backends = NetworkFactory::Get()->GetBackendsList();
      AddBackend(backends[0], weights, options);
    }

    for (const auto& name : parents) {
      AddBackend(name, weights, options.GetSubdict(name));
    }
  }

  void AddBackend(const std::string& name,
                  const std::optional<WeightsFile>& weights,
                  const OptionsDict& opts) {
    const int max_batch = opts.GetOrDefault<int>("max_batch", 256);
    const std::string backend = opts.GetOrDefault<std::string>("backend", name);

    networks_.emplace_back(
        NetworkFactory::Get()->Create(backend, weights, opts));
    Network* net = networks_.back().get();

    int nn_threads = opts.GetOrDefault<int>("threads", 0);
    if (nn_threads == 0) {
      nn_threads = net->GetThreads();
    }

    min_batch_size_ = std::min(min_batch_size_, net->GetMiniBatchSize());
    is_cpu_ &= net->IsCpu();

    if (networks_.size() == 1) {
      capabilities_ = net->GetCapabilities();
    } else {
      capabilities_.Merge(net->GetCapabilities());
    }

    for (int i = 0; i < nn_threads; ++i) {
      threads_.emplace_back(
          [this, net, max_batch]() { Worker(net, max_batch); });
    }
  }

  std::unique_ptr<NetworkComputation> NewComputation() override {
    return std::make_unique<MuxingComputation>(this);
  }

  const NetworkCapabilities& GetCapabilities() const override {
    return capabilities_;
  }

  int GetMiniBatchSize() const override { return min_batch_size_; }

  int GetThreads() const override { return threads_.size(); }

  bool IsCpu() const override { return is_cpu_; }

  // 【魔改核心】Enqueue - 从 mutex lock 改为无锁 Push
  // 原实现：lock_guard<mutex> + queue_.push() + cv_.notify_one()
  // 新实现：MpmcLockFreeQueue::Push() - CAS原子入队，无锁无等待
  //
  // 性能提升：
  //   - 消除 mutex 争用：多个 MCTS 线程不再因锁等待而串行化
  //   - 入队延迟：从 mutex 的 ~1µs 降至 atomic CAS 的 ~40ns
  //   - 无需 notify：消费者自旋检测新数据，延迟更低
  void Enqueue(MuxingComputation* computation) {
    lockfree_queue_.Push(computation);
  }

  ~MuxingNetwork() {
    Abort();
    Wait();
    // 清理残留请求：通知所有等待的 MCTS 线程
    MuxingComputation* items[512];
    size_t count = lockfree_queue_.TryPopBatch(items, 512);
    for (size_t i = 0; i < count; ++i) {
      items[i]->NotifyReady();
    }
  }

  // 【魔改核心】Worker - 从 cv.wait() 改为无锁自旋 + 批量出队
  //
  // 原实现流程：
  //   1. cv.wait(lock) 等待队列非空 ← 阻塞，有上下文切换开销
  //   2. lock_guard 下逐个 pop ← 持锁时间长
  //   3. 80µs 拼单延迟 ← 在持锁状态下自旋
  //
  // 新实现流程：
  //   1. 自旋等待队列非空 ← 无锁，无上下文切换
  //   2. TryPopBatch 批量出队 ← 无锁，一次取出多个
  //   3. 【P5】拼单延迟自适应 ← 根据 EMA 历史动态调整
  //
  // 关键改进：
  //   - Worker 线程不再阻塞在 cv.wait()，而是自旋检测
  //   - 自旋中使用 _mm_pause 降低CPU功耗
  //   - 批量出队减少循环次数
  //   - 自适应拼单，避免一刀切的 80µs 等待
  void Worker(Network* network, const int max_batch) {
    // 临时缓冲区：存储批量出队的请求指针
    // 在栈上分配，避免堆分配开销
    // 512 是队列容量上限，max_batch 是单次推理上限
    MuxingComputation* items[512];

    // 【魔改 P5】函数级：最近批大小的 EMA（指数移动平均）
    float avg_batch_ema = 0.0f;
    int last_batch_count = 0;

    while (!abort_.load(std::memory_order_acquire)) {
      // ===== 阶段1：自旋等待队列非空 =====
      // 替代原来的 cv.wait(lock)
      // 自旋检测比 condition_variable 更低延迟：
      //   - cv.wait 需要内核态切换，约 1-5µs
      //   - 自旋检测约 10-40ns（一次 atomic load）
      {
        // 快速路径：队列非空直接跳过自旋
        if (lockfree_queue_.IsEmpty()) {
          // 自旋等待新请求入队
          auto spin_start = std::chrono::high_resolution_clock::now();
          while (true) {
            // 检查 abort 标志
            if (abort_.load(std::memory_order_acquire)) return;

            // 检查队列是否有数据
            if (!lockfree_queue_.IsEmpty()) break;

            // 自旋超时保护：最多自旋 1ms
            // 防止在极端情况下（如所有MCTS线程暂停）空转太久
            auto elapsed = std::chrono::high_resolution_clock::now() - spin_start;
            if (std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count() >= 1000) {
              // 自旋超时，短暂让出CPU
              std::this_thread::yield();
              break;
            }

            // CPU pause：降低功耗，约40ns
#if defined(_MSC_VER)
            _mm_pause();
#else
            __builtin_ia32_pause();
#endif
          }
        }
      }

      // ===== 阶段2：【魔改 P5】拼单延迟自适应（替换硬编码 80µs） =====
      // 根据最近批大小动态调整等待时间：
      //   - 批大小 < 2 → 长等 (250µs)：GPU 远未吃饱，宁可多等凑大批
      //   - 批大小 2-4 → 中等 (120µs)：适度拼单
      //   - 批大小 4-8 → 短等 (40µs)：已有一定并行
      //   - 批大小 ≥ 8 → 不等 (0µs)：GPU 已饱和
      // 用指数移动平均 (EMA) 平滑，避免抖动
      {
        // 【魔改 P5】使用函数级的 avg_batch_ema/last_batch_count
        size_t queue_size = lockfree_queue_.SizeApprox();
        int wait_us = 0;

        // 根据当前队列大小 + EMA 历史，决定等待时间
        float estimated_batch =
            (avg_batch_ema > 0) ? (0.4f * avg_batch_ema + 0.6f * (float)queue_size)
                                 : (float)queue_size;

        if (queue_size > 0 && estimated_batch < 8.0f) {
          if (estimated_batch < 2.0f) {
            wait_us = 250;
          } else if (estimated_batch < 4.0f) {
            wait_us = 120;
          } else {
            wait_us = 40;
          }

          auto start = std::chrono::high_resolution_clock::now();
          while (true) {
            auto elapsed = std::chrono::high_resolution_clock::now() - start;
            if (std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count() >= wait_us) {
              break;
            }
            // 【魔改 P5】用 pause 替代 yield — 更节能，延迟更稳定
#if defined(_MSC_VER)
            _mm_pause();
#else
            __builtin_ia32_pause();
#endif
            // 提前退出：如果队列已经积到足够多样本，立即停止等待
            if (lockfree_queue_.SizeApprox() >= 8) break;
          }
        }
      }

      // ===== 阶段3：批量出队并组装 batch =====
      // 替代原来的 lock_guard + while(!queue_.empty()) + queue_.pop()
      // 无锁批量出队，一次取出多个请求
      //
      // 【设计决策】关于 max_batch 限制的处理：
      // 原实现在持锁状态下逐个pop，遇到max_batch限制就break
      // 无锁批量出队一次取出多个，需要处理超限情况
      // 方案：先批量出队，再逐个检查，超限的请求暂存到 deferred 数组
      // 在当前batch推理完成后，将 deferred 请求重新入队
      // 这比原实现多了一次重新入队的开销，但避免了持锁
      std::vector<MuxingComputation*> children;
      std::vector<MuxingComputation*> deferred;
      std::shared_ptr<NetworkComputation> parent(network->NewComputation());

      size_t popped = lockfree_queue_.TryPopBatch(items, max_batch);
      for (size_t i = 0; i < popped; ++i) {
        // 检查是否超出 max_batch 限制
        // 与原实现一致：如果单个请求就超过max_batch，仍然必须添加
        if (parent->GetBatchSize() != 0 &&
            parent->GetBatchSize() + items[i]->GetBatchSize() > max_batch) {
          // 超出限制，暂存到 deferred，稍后重新入队
          deferred.push_back(items[i]);
          continue;
        }
        children.push_back(items[i]);
        items[i]->PopulateToParent(parent);
      }

      // 如果没有取到任何请求，继续自旋
      if (children.empty()) {
        // 将 deferred 请求重新入队，避免丢失
        for (auto* d : deferred) {
          lockfree_queue_.Push(d);
        }
        continue;
      }

      // 【魔改 P5】更新批大小 EMA：下次循环的拼单延迟基于此历史
      last_batch_count = (int)children.size();
      avg_batch_ema = avg_batch_ema * 0.7f + (float)last_batch_count * 0.3f;

      // ===== 阶段4：执行 GPU 推理 =====
      // ComputeBlocking 保持不变
      parent->ComputeBlocking();

      // ===== 阶段5：通知 MCTS 线程推理完成 =====
      for (auto child : children) child->NotifyReady();

      // ===== 阶段6：将超限的请求重新入队 =====
      // 这些请求因 max_batch 限制未被当前 batch 处理
      // 重新入队后在下一轮 Worker 循环中处理
      for (auto* d : deferred) {
        lockfree_queue_.Push(d);
      }
    }
  }

  // 【魔改】Abort - 使用 atomic 替代 mutex 保护 abort_ 标志
  void Abort() {
    abort_.store(true, std::memory_order_release);
  }

  void Wait() {
    while (!threads_.empty()) {
      threads_.back().join();
      threads_.pop_back();
    }
  }

 private:
  std::vector<std::unique_ptr<Network>> networks_;

  // 【魔改核心】无锁环形队列替代 std::queue
  // 原实现：std::queue<MuxingComputation*> queue_ + mutex + cv
  // 新实现：MpmcLockFreeQueue<MuxingComputation> - 完全无锁
  MpmcLockFreeQueue<MuxingComputation> lockfree_queue_;

  // 【魔改】abort_ 从普通 bool 改为 atomic<bool>
  // 原实现：mutex 保护下的 bool abort_
  // 新实现：atomic<bool>，Worker 线程自旋检测，无需 mutex
  std::atomic<bool> abort_{false};

  NetworkCapabilities capabilities_;
  int min_batch_size_ = std::numeric_limits<int>::max();
  bool is_cpu_ = true;

  // 【移除】不再需要 mutex_ 和 cv_
  // 原实现：std::mutex mutex_; std::condition_variable cv_;
  // 无锁队列通过 atomic 操作协调生产者/消费者，无需传统同步原语

  std::vector<std::thread> threads_;
};

void MuxingComputation::ComputeBlocking() {
  // 【魔改】入队操作从 mutex 保护改为无锁 Push
  // 原实现：network_->Enqueue(this) 内部加锁
  // 新实现：network_->Enqueue(this) 内部无锁 CAS 入队
  network_->Enqueue(this);

  // 【保留】等待 GPU 推理完成的 mutex+cv
  // 这是"请求-响应"同步，与队列的"生产者-消费者"同步不同
  // MCTS 线程在此等待 GPU 推理结果（1-10ms），mutex 开销可忽略
  std::unique_lock<std::mutex> lock(mutex_);
  dataready_cv_.wait(lock, [this]() { return dataready_; });
}

std::unique_ptr<Network> MakeMuxingNetwork(
    const std::optional<WeightsFile>& weights, const OptionsDict& options) {
  return std::make_unique<MuxingNetwork>(weights, options);
}

REGISTER_NETWORK("multiplexing", MakeMuxingNetwork, -1000)

}  // namespace
}  // namespace lczero
