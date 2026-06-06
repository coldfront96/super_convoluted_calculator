// ===========================================================================
// ENGINE C of the cathedral: THE GATE-LEVEL VM, YET AGAIN  (language: Go)
// ---------------------------------------------------------------------------
// Third independent gate-level engine. Addition/subtraction go through the same
// NAND-built ripple-carry adder, but multiplication here uses the Russian
// peasant method (halving/doubling) for variety. Reads bytecode from os.Args[1],
// prints top of stack as a signed 64-bit integer.
// ===========================================================================
package main

import (
	"bufio"
	"fmt"
	"os"
	"strconv"
	"strings"
)

func nand(a, b int) int {
	if a&b != 0 {
		return 0
	}
	return 1
}
func not(a int) int    { return nand(a, a) }
func and(a, b int) int { return not(nand(a, b)) }
func or(a, b int) int  { return nand(not(a), not(b)) }
func xor(a, b int) int { t := nand(a, b); return nand(nand(a, t), nand(b, t)) }

func fullAdder(a, b, c int) (int, int) {
	axb := xor(a, b)
	return xor(axb, c), or(and(axb, c), and(a, b))
}

func bit(x uint64, i uint) int { return int((x >> i) & 1) }

func addC(a, b uint64, cin int) (uint64, int) {
	var r uint64
	c := cin
	for i := uint(0); i < 64; i++ {
		s, co := fullAdder(bit(a, i), bit(b, i), c)
		if s == 1 {
			r |= 1 << i
		}
		c = co
	}
	return r, c
}
func add(a, b uint64) uint64 { r, _ := addC(a, b, 0); return r }
func notw(a uint64) uint64 {
	var r uint64
	for i := uint(0); i < 64; i++ {
		if not(bit(a, i)) == 1 {
			r |= 1 << i
		}
	}
	return r
}
func neg(a uint64) uint64    { return add(notw(a), 1) }
func sub(a, b uint64) uint64 { return add(a, neg(b)) }
func uge(a, b uint64) bool   { _, c := addC(a, notw(b), 1); return c == 1 }

// Russian peasant multiplication: a different road to the same product.
func mul(a, b uint64) uint64 {
	var r uint64
	for b != 0 {
		if b&1 == 1 {
			r = add(r, a)
		}
		a <<= 1
		b >>= 1
	}
	return r
}

func udivmod(num, den uint64) (uint64, uint64) {
	var q, r uint64
	for i := 63; i >= 0; i-- {
		r = (r << 1) | ((num >> uint(i)) & 1)
		if uge(r, den) {
			r = sub(r, den)
			q |= 1 << uint(i)
		}
	}
	return q, r
}

func isNeg(a uint64) bool { return bit(a, 63) == 1 }

func sdivmod(a, b uint64) (uint64, uint64, bool) {
	if b == 0 {
		return 0, 0, false
	}
	na, nb := isNeg(a), isNeg(b)
	ua, ub := a, b
	if na {
		ua = neg(a)
	}
	if nb {
		ub = neg(b)
	}
	uq, ur := udivmod(ua, ub)
	q, r := uq, ur
	if na != nb {
		q = neg(uq)
	}
	if na {
		r = neg(ur)
	}
	return q, r, true
}

func main() {
	var scanner *bufio.Scanner
	if len(os.Args) > 1 {
		f, err := os.Open(os.Args[1])
		if err != nil {
			fmt.Fprintln(os.Stderr, "engine_go:", err)
			os.Exit(2)
		}
		defer f.Close()
		scanner = bufio.NewScanner(f)
	} else {
		scanner = bufio.NewScanner(os.Stdin)
	}

	stack := make([]uint64, 0, 64)
	for scanner.Scan() {
		line := strings.TrimSpace(scanner.Text())
		if line == "" {
			continue
		}
		parts := strings.Fields(line)
		op := parts[0]
		switch op {
		case "PUSH":
			v, _ := strconv.ParseInt(parts[1], 10, 64)
			stack = append(stack, uint64(v))
		case "NEG":
			n := len(stack)
			stack[n-1] = neg(stack[n-1])
		default:
			n := len(stack)
			b := stack[n-1]
			a := stack[n-2]
			stack = stack[:n-2]
			var res uint64
			switch op {
			case "ADD":
				res = add(a, b)
			case "SUB":
				res = sub(a, b)
			case "MUL":
				res = mul(a, b)
			case "DIV", "MOD":
				q, r, ok := sdivmod(a, b)
				if !ok {
					fmt.Println("ERR:DIVZERO")
					return
				}
				if op == "DIV" {
					res = q
				} else {
					res = r
				}
			default:
				stack = append(stack, a, b)
				continue
			}
			stack = append(stack, res)
		}
	}

	if len(stack) < 1 {
		fmt.Fprintln(os.Stderr, "engine_go: empty stack")
		os.Exit(4)
	}
	fmt.Println(int64(stack[len(stack)-1]))
}
