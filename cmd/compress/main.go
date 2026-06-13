package main

import (
	"compress/gzip"
	"encoding/binary"
	"fmt"
	"io"
	"math"
	"os"
	"sort"

	"google.golang.org/protobuf/proto"
	"lc0_compress/pblczero"
)

// ============================================================
// FLOAT16 / LINEAR16 解码 / 编码工具
// ============================================================

// float16ToFloat32 把 IEEE-754 half-precision 转换为 float32
func float16ToFloat32(h uint16) float32 {
	sign := uint32(h&0x8000) << 16
	exp := uint32(h>>10) & 0x1f
	mant := uint32(h & 0x3ff)
	var bits uint32
	switch exp {
	case 0:
		if mant == 0 {
			bits = sign
		} else {
			// 非规格化数
			e := uint32(1)
			for (mant & 0x400) == 0 {
				mant <<= 1
				e--
			}
			mant &= 0x3ff
			bits = sign | ((e + 127 - 15) << 23) | (mant << 13)
		}
	case 31:
		bits = sign | 0x7f800000 | (mant << 13)
	default:
		bits = sign | ((exp + 127 - 15) << 23) | (mant << 13)
	}
	return math.Float32frombits(bits)
}

// float32ToFloat16 把 float32 截断到 half-precision
func float32ToFloat16(f float32) uint16 {
	bits := math.Float32bits(f)
	sign := uint16((bits >> 31) & 0x1)
	exp := int((bits >> 23) & 0xff)
	mant := bits & 0x7fffff

	if exp == 0 && mant == 0 {
		return sign << 15
	}
	if exp == 0xff {
		if mant != 0 {
			return (sign << 15) | 0x7e00 // NaN
		}
		return (sign << 15) | 0x7c00 // inf
	}

	exp -= 127 - 15 // 重新偏置
	if exp <= 0 {
		mant = (mant | 0x800000) >> uint(1-exp)
		return (sign << 15) | uint16(mant>>13)
	}
	if exp >= 31 {
		return (sign << 15) | 0x7c00 // 溢出→inf
	}
	return (sign << 15) | uint16(exp<<10) | uint16(mant>>13)
}

// decodeLayer 从 Layer.Params 解码到 []float32
// globalEnc 是 net.Format.WeightsEncoding 的全局编码（当 layer.Encoding 为 nil 时使用）
func decodeLayer(l *pblczero.Weights_Layer, globalEnc pblczero.Format_Encoding) []float32 {
	if l == nil || len(l.Params) == 0 {
		return nil
	}
	params := l.Params

	// 确定实际编码：层级编码优先，其次全局编码
	enc := l.GetEncoding()
	useLinear16 := false
	if enc == pblczero.Weights_Layer_UNKNOWN_ENCODING {
		// 继承全局编码
		if globalEnc == pblczero.Format_LINEAR16 {
			useLinear16 = true
		} else {
			// 没有全局编码信息时，尝试用层的 min/max 判断：
			// 如果 min_val != 0 或 max_val != 0，说明是 LINEAR16
			if l.GetMinVal() != 0 || l.GetMaxVal() != 0 {
				useLinear16 = true
			} else {
				// 默认回退到 FLOAT16
				enc = pblczero.Weights_Layer_FLOAT16
			}
		}
	}

	if useLinear16 || enc == pblczero.Weights_Layer_LINEAR16 {
		n := len(params) / 2
		result := make([]float32, n)
		minV := l.GetMinVal()
		maxV := l.GetMaxVal()
		scale := float64(maxV-minV) / 65535.0
		for i := 0; i < n; i++ {
			v := binary.LittleEndian.Uint16(params[i*2 : i*2+2])
			result[i] = float32(float64(minV) + float64(v)*scale)
		}
		return result
	}

	if enc == pblczero.Weights_Layer_FLOAT16 {
		n := len(params) / 2
		result := make([]float32, n)
		for i := 0; i < n; i++ {
			h := binary.LittleEndian.Uint16(params[i*2 : i*2+2])
			result[i] = float16ToFloat32(h)
		}
		return result
	}

	if enc == pblczero.Weights_Layer_BFLOAT16 {
		n := len(params) / 2
		result := make([]float32, n)
		for i := 0; i < n; i++ {
			hi := binary.LittleEndian.Uint16(params[i*2 : i*2+2])
			bits := uint32(hi) << 16
			result[i] = math.Float32frombits(bits)
		}
		return result
	}

	if enc == pblczero.Weights_Layer_FLOAT32 {
		n := len(params) / 4
		result := make([]float32, n)
		for i := 0; i < n; i++ {
			bits := binary.LittleEndian.Uint32(params[i*4 : i*4+4])
			result[i] = math.Float32frombits(bits)
		}
		return result
	}

	return nil
}

// encodeFloat16Layer 把 []float32 编码为 FLOAT16 Layer
func encodeFloat16Layer(data []float32) *pblczero.Weights_Layer {
	params := make([]byte, len(data)*2)
	for i, v := range data {
		h := float32ToFloat16(v)
		binary.LittleEndian.PutUint16(params[i*2:], h)
	}
	enc := pblczero.Weights_Layer_FLOAT16
	return &pblczero.Weights_Layer{
		Params:   params,
		Encoding: &enc,
	}
}

// ============================================================
// Scheme A: RPE 相对位置偏置生成
// 棋盘为 8×8，相对偏移 dr∈[-7,7], dc∈[-7,7]，共 15×15=225 种
// RPE-Q 权重维度：[dModel, 225] 展平存储
// ============================================================

func buildRPEBias(dModel, numHeads int) []float32 {
	depth := dModel / numHeads // 每个头的 d_k
	totalSize := dModel * 225  // [dModel, 225]
	result := make([]float32, totalSize)

	for h := 0; h < numHeads; h++ {
		for d := 0; d < depth; d++ {
			rowIdx := h*depth + d
			for dr := -7; dr <= 7; dr++ {
				for dc := -7; dc <= 7; dc++ {
					colIdx := (dr+7)*15 + (dc + 7)
					// sin/cos 位置编码，基于 (dr, dc) 分量
					// d 维度：偶数用 cos(dr), 奇数用 sin(dc)
					freq := math.Pow(10000.0, -2.0*float64(d)/float64(depth))
					var val float64
					switch d % 4 {
					case 0:
						val = math.Cos(float64(dr) * freq)
					case 1:
						val = math.Sin(float64(dr) * freq)
					case 2:
						val = math.Cos(float64(dc) * freq)
					case 3:
						val = math.Sin(float64(dc) * freq)
					}
					// 初始幅度 0.02（可学习的起点，不影响已有权重结构）
					result[rowIdx*225+colIdx] = float32(val * 0.02)
				}
			}
		}
	}
	return result
}

// ============================================================
// Scheme B: A³ 注意力头剪枝
// 按各头 Q 权重行的 L2 范数排名，剪除最弱的 pruneRatio 比例的头
// 对该头在 Q_w, K_w, V_w 中对应的行块，和 Dense_w 中对应的列块置零
// ============================================================

// headNorm 计算第 h 个头的 Q 投影行块 L2 范数
// qw: [dModel × dModel]，行主序；第 h 头 → 行 [h*headDim, (h+1)*headDim)
func headNorm(qw []float32, h, headDim, dModel int) float64 {
	start := h * headDim * dModel // 在展平数组中的起始索引
	end := start + headDim*dModel
	if end > len(qw) {
		end = len(qw)
	}
	sum := 0.0
	for _, v := range qw[start:end] {
		sum += float64(v) * float64(v)
	}
	return math.Sqrt(sum)
}

// zeroHead 把第 h 个头在矩阵中对应的行块（或列块）置零
// isRowBlock=true → 将 [h*headDim, (h+1)*headDim) 行置零（Q/K/V 投影）
// isRowBlock=false → 将 [(h*headDim) ~ ((h+1)*headDim)) 列置零（Dense 投影）
func zeroHead(w []float32, h, headDim, dModel int, isRowBlock bool) {
	if isRowBlock {
		// Q/K/V：矩阵行主序 [dModel, dModel]，头 h 对应行 [h*headDim, (h+1)*headDim)
		for row := h * headDim; row < (h+1)*headDim; row++ {
			for col := 0; col < dModel; col++ {
				w[row*dModel+col] = 0
			}
		}
	} else {
		// Dense：矩阵行主序 [dModel, dModel]，头 h 对应列 [h*headDim, (h+1)*headDim)
		for row := 0; row < dModel; row++ {
			for col := h * headDim; col < (h+1)*headDim; col++ {
				w[row*dModel+col] = 0
			}
		}
	}
}

// encodeLinear16Layer 把 []float32 编码为 LINEAR16 Layer（保持与 BT4 原始格式一致）
func encodeLinear16Layer(data []float32) *pblczero.Weights_Layer {
	if len(data) == 0 {
		return nil
	}
	// 计算 min/max
	minV := data[0]
	maxV := data[0]
	for _, v := range data[1:] {
		if v < minV {
			minV = v
		}
		if v > maxV {
			maxV = v
		}
	}
	params := make([]byte, len(data)*2)
	rng := float64(maxV - minV)
	for i, v := range data {
		var u uint16
		if rng > 0 {
			u = uint16(math.Round(65535.0 * float64(v-minV) / rng))
		}
		binary.LittleEndian.PutUint16(params[i*2:], u)
	}
	enc := pblczero.Weights_Layer_LINEAR16
	return &pblczero.Weights_Layer{
		MinVal:   &minV,
		MaxVal:   &maxV,
		Params:   params,
		Encoding: &enc,
	}
}

// pruneAttentionHeads 对一层的 MHA 做头级别剪枝
func pruneAttentionHeads(mha *pblczero.Weights_MHA, numHeads, dModel int, pruneRatio float64, globalEnc pblczero.Format_Encoding) {
	headDim := dModel / numHeads
	numPrune := int(math.Round(float64(numHeads) * pruneRatio))
	if numPrune == 0 {
		return
	}

	qw := decodeLayer(mha.QW, globalEnc)
	kw := decodeLayer(mha.KW, globalEnc)
	vw := decodeLayer(mha.VW, globalEnc)
	dw := decodeLayer(mha.DenseW, globalEnc)
	if qw == nil {
		return
	}

	// 计算每个头的范数并排序
	type headInfo struct {
		idx  int
		norm float64
	}
	norms := make([]headInfo, numHeads)
	for h := 0; h < numHeads; h++ {
		norms[h] = headInfo{h, headNorm(qw, h, headDim, dModel)}
	}
	sort.Slice(norms, func(i, j int) bool {
		return norms[i].norm < norms[j].norm
	})

	fmt.Printf("    剪枝 %d/%d 个最弱头（最弱3个范数：%.4f, %.4f, %.4f）\n",
		numPrune, numHeads,
		norms[0].norm, norms[1].norm, norms[2].norm)

	// 将最弱的 numPrune 个头置零
	for i := 0; i < numPrune; i++ {
		h := norms[i].idx
		if qw != nil { zeroHead(qw, h, headDim, dModel, true) }
		if kw != nil { zeroHead(kw, h, headDim, dModel, true) }
		if vw != nil { zeroHead(vw, h, headDim, dModel, true) }
		if dw != nil { zeroHead(dw, h, headDim, dModel, false) }
	}

	// 写回 LINEAR16 格式（保持与原始 BT4 一致）
	mha.QW = encodeLinear16Layer(qw)
	mha.KW = encodeLinear16Layer(kw)
	mha.VW = encodeLinear16Layer(vw)
	mha.DenseW = encodeLinear16Layer(dw)
}

// ============================================================
// 主逻辑
// ============================================================

func main() {
	inPath := "weights.pb.gz"
	outPath := "weights_compressed.pb.gz"

	fmt.Printf("读取 %s ...\n", inPath)
	f, err := os.Open(inPath)
	if err != nil {
		fmt.Printf("打开文件失败: %v\n", err)
		os.Exit(1)
	}
	defer f.Close()

	gr, err := gzip.NewReader(f)
	if err != nil {
		fmt.Printf("创建 gzip 读取器失败: %v\n", err)
		os.Exit(1)
	}
	defer gr.Close()

	data, err := io.ReadAll(gr)
	if err != nil {
		fmt.Printf("读取 gzip 内容失败: %v\n", err)
		os.Exit(1)
	}
	fmt.Printf("解压后大小: %d 字节\n", len(data))

	net := &pblczero.Net{}
	opts := proto.UnmarshalOptions{DiscardUnknown: true, AllowPartial: true}
	if err := opts.Unmarshal(data, net); err != nil {
		fmt.Printf("protobuf 解析失败: %v\n", err)
		os.Exit(1)
	}

	w := net.Weights
	if w == nil {
		fmt.Println("权重为空！")
		os.Exit(1)
	}

	// 获取全局权重编码格式
	var globalEnc pblczero.Format_Encoding
	if net.Format != nil {
		globalEnc = net.Format.GetWeightsEncoding()
		fmt.Printf("全局权重编码: %v\n", globalEnc)
	}

	numEncoders := len(w.Encoder)
	fmt.Printf("encoder 层数: %d\n", numEncoders)
	if numEncoders == 0 {
		fmt.Println("无 encoder 层，退出")
		os.Exit(1)
	}

	// 推断网络维度：从 q_w 的 params 长度（2 bytes/param for LINEAR16/FLOAT16）
	firstQW := w.Encoder[0].Mha.GetQW()
	qwParamsLen := len(firstQW.GetParams())
	dModel := int(math.Round(math.Sqrt(float64(qwParamsLen / 2))))
	fmt.Printf("推断 dModel = %d\n", dModel)

	numHeads := 32
	if dModel == 768 {
		numHeads = 24
	} else if dModel == 512 {
		numHeads = 16
	} else if dModel == 256 {
		numHeads = 8
	}
	fmt.Printf("numHeads = %d，每头维度 = %d\n", numHeads, dModel/numHeads)

	// ---- Scheme B: A³ 注意力头剪枝（剪除最弱 10% 的头）----
	pruneRatio := 0.10
	numPrune := int(math.Round(float64(numHeads) * pruneRatio))
	fmt.Printf("\n=== Scheme B: A³ 注意力头剪枝（每层剪除 %d/%d 个最弱头）===\n", numPrune, numHeads)
	for i, encLayer := range w.Encoder {
		fmt.Printf("处理 encoder 层 %d/%d ...\n", i+1, numEncoders)
		if encLayer.Mha != nil {
			pruneAttentionHeads(encLayer.Mha, numHeads, dModel, pruneRatio, globalEnc)
		}
	}

	// ---- Scheme A: RPE 位置偏置注入 ----
	fmt.Printf("\n=== Scheme A: RPE 位置偏置注入 ===\n")
	rpeData := buildRPEBias(dModel, numHeads)
	fmt.Printf("生成 RPE 权重: [%d, 225] = %d 个 float32\n", dModel, len(rpeData))

	for _, encLayer := range w.Encoder {
		if encLayer.Mha != nil {
			// RPE 权重用 FLOAT16 格式存储（小范围浮点，不需要 min/max 量化）
			encLayer.Mha.RpeQ = encodeFloat16Layer(rpeData)
			encLayer.Mha.RpeK = encodeFloat16Layer(rpeData)
			// rpe_v 暂不注入
		}
	}
	fmt.Printf("已为 %d 个 encoder 层注入 rpe_q 和 rpe_k\n", numEncoders)

	// ---- 序列化写出 ----
	fmt.Printf("\n=== 序列化并写出 %s ===\n", outPath)
	marshalOpts := proto.MarshalOptions{AllowPartial: true}
	outData, err := marshalOpts.Marshal(net)
	if err != nil {
		fmt.Printf("序列化失败: %v\n", err)
		os.Exit(1)
	}
	fmt.Printf("序列化后大小: %d 字节\n", len(outData))

	outFile, err := os.Create(outPath)
	if err != nil {
		fmt.Printf("创建输出文件失败: %v\n", err)
		os.Exit(1)
	}
	defer outFile.Close()

	gw, err := gzip.NewWriterLevel(outFile, gzip.BestSpeed)
	if err != nil {
		fmt.Printf("创建 gzip 写入器失败: %v\n", err)
		os.Exit(1)
	}
	if _, err := gw.Write(outData); err != nil {
		fmt.Printf("写入 gzip 失败: %v\n", err)
		os.Exit(1)
	}
	if err := gw.Close(); err != nil {
		fmt.Printf("关闭 gzip 写入器失败: %v\n", err)
		os.Exit(1)
	}

	info, _ := os.Stat(outPath)
	fmt.Printf("输出文件大小: %d 字节 (%.1f MB)\n", info.Size(), float64(info.Size())/1024/1024)
	fmt.Println("完成！")
}
