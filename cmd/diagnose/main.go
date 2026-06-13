package main

import (
	"compress/gzip"
	"encoding/binary"
	"fmt"
	"io"
	"math"
	"os"

	"google.golang.org/protobuf/proto"
	"lc0_compress/pblczero"
)

func main() {
	f, _ := os.Open("weights.pb.gz")
	defer f.Close()
	gr, _ := gzip.NewReader(f)
	defer gr.Close()
	data, _ := io.ReadAll(gr)

	net := &pblczero.Net{}
	opts := proto.UnmarshalOptions{DiscardUnknown: true, AllowPartial: true}
	opts.Unmarshal(data, net)

	// 打印全局格式
	fmt.Printf("Magic: 0x%X (%d)\n", net.GetMagic(), net.GetMagic())
	if net.Format != nil {
		fmt.Printf("Format.WeightsEncoding: %v\n", net.Format.GetWeightsEncoding())
		if net.Format.NetworkFormat != nil {
			fmt.Printf("Format.NetworkFormat.Network: %v\n", net.Format.NetworkFormat.GetNetwork())
		}
	} else {
		fmt.Println("Format: nil")
	}

	if net.Weights == nil {
		fmt.Println("No weights")
		return
	}

	enc0 := net.Weights.Encoder[0]
	qw := enc0.Mha.GetQW()
	fmt.Printf("\nLayer QW:\n")
	fmt.Printf("  Encoding field: %v (nil=%v)\n", qw.GetEncoding(), qw.Encoding == nil)
	fmt.Printf("  Dims: %v\n", qw.Dims)
	fmt.Printf("  MinVal: %v, MaxVal: %v\n", qw.GetMinVal(), qw.GetMaxVal())
	fmt.Printf("  Params len: %d bytes\n", len(qw.Params))

	// 检查前16个字节：原始 uint16 值
	fmt.Printf("\n前16个 uint16 原始值：\n")
	for i := 0; i < 16 && i*2+1 < len(qw.Params); i++ {
		v := binary.LittleEndian.Uint16(qw.Params[i*2 : i*2+2])
		fmt.Printf("  [%d] 0x%04X = %d\n", i, v, v)
	}

	// 测试三种解码方式
	fmt.Println("\n--- 解码测试（前8个值）---")

	// 方式1: FLOAT16 (IEEE-754 half)
	fmt.Println("FLOAT16 解码：")
	for i := 0; i < 8 && i*2+1 < len(qw.Params); i++ {
		h := binary.LittleEndian.Uint16(qw.Params[i*2 : i*2+2])
		fmt.Printf("  [%d] float16(0x%04X) = %v\n", i, h, float16ToFloat32(h))
	}

	// 方式2: LINEAR16 使用 layer 的 min/max
	fmt.Println("LINEAR16 (layer min/max) 解码：")
	minV := qw.GetMinVal()
	maxV := qw.GetMaxVal()
	fmt.Printf("  min=%f, max=%f\n", minV, maxV)
	for i := 0; i < 8 && i*2+1 < len(qw.Params); i++ {
		v := binary.LittleEndian.Uint16(qw.Params[i*2 : i*2+2])
		decoded := float32(float64(minV) + float64(v)/65535.0*float64(maxV-minV))
		fmt.Printf("  [%d] uint16(%d) = %f\n", i, v, decoded)
	}

	// 方式3: BFLOAT16
	fmt.Println("BFLOAT16 解码：")
	for i := 0; i < 8 && i*2+1 < len(qw.Params); i++ {
		hi := binary.LittleEndian.Uint16(qw.Params[i*2 : i*2+2])
		bits := uint32(hi) << 16
		fmt.Printf("  [%d] bfloat16(0x%04X) = %v\n", i, hi, math.Float32frombits(bits))
	}

	// 方式4: FLOAT32 (前4个 float32)
	fmt.Println("FLOAT32 解码（前4组）：")
	for i := 0; i < 4 && i*4+3 < len(qw.Params); i++ {
		bits := binary.LittleEndian.Uint32(qw.Params[i*4 : i*4+4])
		fmt.Printf("  [%d] float32(0x%08X) = %v\n", i, bits, math.Float32frombits(bits))
	}
}

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
