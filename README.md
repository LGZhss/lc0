# Lc0 BT4-it332 压缩优化管线 (Go 工具)

本仓库是 [LGZhss/lc0](https://github.com/LGZhss/lc0) 项目的配套 Go 工具集，实现了针对 BT4-it332 网络的两阶段优化压缩管线：

## 方案说明

### Scheme A — RPE 相对位置偏置注入
- 为 BT4-it332 的 15 个 encoder 层注入 `rpe_q` 和 `rpe_k` 权重
- 每层 RPE 权重维度：`[1024, 225]`，对应 8×8 棋盘上所有 15×15 种相对位置偏移
- 使用 sin/cos 位置编码（分 dr/dc 两方向），初始幅度 0.02
- C++ CUDA 后端（来自 PR #2042）会在 Softmax 前融合这些偏置到 Attention logits 中

### Scheme B — A³ 注意力头剪枝
- 对每个 encoder 层的 32 个注意力头按 Q 权重 L2 范数排名
- 剪除最弱 10%（每层 3/32 个头），对应头的 Q/K/V 行块 + Dense 列块置零
- 写回格式与原始 BT4 相同（LINEAR16 with per-layer min/max）

## 工具列表

| 命令 | 路径 | 功能 |
|------|------|------|
| `compress` | `cmd/compress/main.go` | 读取 `weights.pb.gz`，执行 Scheme A+B，输出 `weights_compressed.pb.gz` |
| `telemetry` | `cmd/telemetry/main.go` | 通过 UCI 协议与编译好的 lc0.exe 交互，对比原始和压缩权重的 bestmove 一致率 |
| `diagnose` | `cmd/diagnose/main.go` | 诊断权重编码格式（调试用） |

## 使用方法

```bash
# 1. 下载 BT4-it332 权重（约 365MB）
# 链接：https://storage.lczero.org/files/networks-contrib/BT4-1024x15x32h-swa-6147500-policytune-332.pb.gz
# 保存为 weights.pb.gz

# 2. 运行压缩程序
go run cmd/compress/main.go
# → 生成 weights_compressed.pb.gz

# 3. 编译 lc0（CUDA 版，需要 CUDA Toolkit）
cd ../lc0-src
meson setup build --buildtype release -Dplain_cuda=true -Dblas=false -Donnx=false -Dgtest=false
ninja -C build

# 4. 运行回归测试
go run cmd/telemetry/main.go
```

## 依赖

- Go 1.21+
- `google.golang.org/protobuf`
- `gonum.org/v1/gonum`（已在 go.mod 中声明）
