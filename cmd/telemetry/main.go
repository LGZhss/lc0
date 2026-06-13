package main

import (
	"bufio"
	"fmt"
	"io"
	"os"
	"os/exec"
	"strings"
	"sync"
	"time"
)

// UCI 通信结构体
type UCISession struct {
	cmd    *exec.Cmd
	stdin  io.WriteCloser
	stdout *bufio.Scanner
	mu     sync.Mutex
	lines  chan string
}

// 启动 lc0 进程
func newUCISession(lc0Path, weightsPath string) (*UCISession, error) {
	cmd := exec.Command(lc0Path, "--weights="+weightsPath)
	stdin, err := cmd.StdinPipe()
	if err != nil {
		return nil, fmt.Errorf("stdin pipe: %w", err)
	}
	stdoutPipe, err := cmd.StdoutPipe()
	if err != nil {
		return nil, fmt.Errorf("stdout pipe: %w", err)
	}
	cmd.Stderr = os.Stderr // 将 stderr 重定向到我们的 stderr

	if err := cmd.Start(); err != nil {
		return nil, fmt.Errorf("start lc0: %w", err)
	}

	sess := &UCISession{
		cmd:    cmd,
		stdin:  stdin,
		stdout: bufio.NewScanner(stdoutPipe),
		lines:  make(chan string, 256),
	}

	// 后台 goroutine 持续读取 stdout（防止 Windows stdout 缓冲区满死锁）
	go func() {
		for sess.stdout.Scan() {
			line := sess.stdout.Text()
			sess.lines <- line
		}
		close(sess.lines)
	}()

	return sess, nil
}

// 发送命令到引擎
func (s *UCISession) send(cmd string) {
	s.mu.Lock()
	defer s.mu.Unlock()
	fmt.Fprintf(s.stdin, "%s\n", cmd)
}

// 等待指定关键词（超时后返回已收集的行）
func (s *UCISession) waitFor(keyword string, timeout time.Duration) []string {
	deadline := time.After(timeout)
	var collected []string
	for {
		select {
		case line, ok := <-s.lines:
			if !ok {
				return collected
			}
			collected = append(collected, line)
			if strings.Contains(line, keyword) {
				return collected
			}
		case <-deadline:
			fmt.Printf("  [超时] 等待 %q 超过 %v\n", keyword, timeout)
			return collected
		}
	}
}

// 关闭引擎
func (s *UCISession) close() {
	s.send("quit")
	_ = s.cmd.Wait()
}

// ============================================================
// 测试局面（来自官方 Lc0 测试套件）
// ============================================================
var testPositions = []struct {
	name string
	fen  string
}{
	{"初始局面", "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"},
	{"中局复杂", "r1bqkb1r/pppp1ppp/2n2n2/4p3/2B1P3/5N2/PPPP1PPP/RNBQK2R w KQkq - 4 4"},
	{"残局", "8/8/8/4k3/8/8/4K3/8 w - - 0 1"},
	{"战术题", "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1"},
}

// ============================================================
// 对比测试：原始权重 vs 压缩权重
// ============================================================
func testEngine(label, lc0Path, weightsPath string, nodes int) map[string]string {
	fmt.Printf("\n--- [%s] 开始测试 ---\n", label)
	sess, err := newUCISession(lc0Path, weightsPath)
	if err != nil {
		fmt.Printf("  启动引擎失败: %v\n", err)
		return nil
	}
	defer sess.close()

	// UCI 握手
	sess.send("uci")
	lines := sess.waitFor("uciok", 10*time.Second)
	uciOK := false
	for _, l := range lines {
		if strings.Contains(l, "uciok") {
			uciOK = true
		}
	}
	if !uciOK {
		fmt.Println("  UCI 握手失败！")
		return nil
	}
	fmt.Printf("  UCI 握手成功\n")

	sess.send("isready")
	sess.waitFor("readyok", 15*time.Second)
	fmt.Printf("  引擎就绪\n")

	results := make(map[string]string)
	for _, pos := range testPositions {
		fmt.Printf("  测试局面 [%s]: %s\n", pos.name, pos.fen[:min(40, len(pos.fen))]+"...")
		sess.send(fmt.Sprintf("position fen %s", pos.fen))
		sess.send(fmt.Sprintf("go nodes %d", nodes))
		searchLines := sess.waitFor("bestmove", 30*time.Second)

		// 提取最优推荐手和最终 info 行
		var bestmove, lastInfo string
		for _, l := range searchLines {
			if strings.HasPrefix(l, "info") {
				lastInfo = l
			}
			if strings.HasPrefix(l, "bestmove") {
				bestmove = l
			}
		}
		fmt.Printf("    最优手: %s\n", bestmove)
		if lastInfo != "" {
			fmt.Printf("    最终info: %s\n", lastInfo)
		}
		results[pos.name] = bestmove
	}

	return results
}

func min(a, b int) int {
	if a < b {
		return a
	}
	return b
}

func main() {
	// 路径配置
	lc0Path := "lc0.exe"          // lc0 可执行文件路径
	origWeights := "weights.pb.gz" // 原始 BT4-it332 权重
	compWeights := "weights_compressed.pb.gz" // 压缩后权重

	nodes := 400 // 每局面搜索节点数（用于快速回归测试）

	// 检查文件是否存在
	for _, path := range []string{lc0Path, origWeights, compWeights} {
		if _, err := os.Stat(path); os.IsNotExist(err) {
			fmt.Printf("文件不存在: %s\n", path)
			if path == lc0Path {
				fmt.Println("提示: 请先编译 lc0 或从 lczero.org 下载 lc0.exe")
			}
		}
	}

	fmt.Println("=== Lc0 BT4-it332 压缩权重回归测试 ===")
	fmt.Printf("搜索节点数: %d\n", nodes)

	// 测试原始权重
	origResults := testEngine("原始 BT4-it332", lc0Path, origWeights, nodes)

	// 测试压缩权重
	compResults := testEngine("压缩后 BT4-it332", lc0Path, compWeights, nodes)

	// 对比结果
	fmt.Println("\n=== 对比结果 ===")
	fmt.Printf("%-20s  %-25s  %-25s  %s\n", "局面", "原始最优手", "压缩最优手", "一致")
	fmt.Println(strings.Repeat("-", 85))
	agree := 0
	total := 0
	for _, pos := range testPositions {
		orig := origResults[pos.name]
		comp := compResults[pos.name]
		match := orig == comp
		if match {
			agree++
		}
		total++
		symbol := "✓"
		if !match {
			symbol = "✗"
		}
		fmt.Printf("%-20s  %-25s  %-25s  %s\n", pos.name, orig, comp, symbol)
	}
	fmt.Printf("\n最优手一致率: %d/%d (%.1f%%)\n", agree, total, float64(agree)/float64(total)*100)
	if float64(agree)/float64(total) >= 0.75 {
		fmt.Println("✓ 通过回归测试！压缩权重与原始权重高度一致。")
	} else {
		fmt.Println("✗ 回归测试警告：一致率偏低，请检查压缩参数。")
	}
}
