// ===========================================================================
// ENGINE C of the cathedral: THE GATE-LEVEL VM, YET AGAIN  (language: Go)
// ---------------------------------------------------------------------------
// Third independent gate engine: a 64-bit ALU built from NAND, plus an
// arbitrary-precision integer type built on top of it (base-2^32 limbs).
// Multiplication of limbs goes through the Russian-peasant-flavoured gate
// multiplier. Native +/* never compute the answer; shifts/indexing are plumbing.
// Reads bytecode from os.Args[1] (or stdin), prints top of stack in decimal.
// ===========================================================================
package main

import (
	"bufio"
	"os"
	"strings"
)

// ---- LAYER 1+2: gates and 64-bit ALU ----
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
func fa(a, b, c int) (int, int) {
	axb := xor(a, b)
	return xor(axb, c), or(and(axb, c), and(a, b))
}
func bit64(x uint64, i uint) int { return int((x >> i) & 1) }

func gaddc(a, b uint64, cin int) (uint64, int) {
	var r uint64
	c := cin
	for i := uint(0); i < 64; i++ {
		s, co := fa(bit64(a, i), bit64(b, i), c)
		if s == 1 {
			r |= 1 << i
		}
		c = co
	}
	return r, c
}
func gadd(a, b uint64) uint64 { r, _ := gaddc(a, b, 0); return r }
func gnotw(a uint64) uint64 {
	var r uint64
	for i := uint(0); i < 64; i++ {
		if not(bit64(a, i)) == 1 {
			r |= 1 << i
		}
	}
	return r
}
func gneg(a uint64) uint64    { return gadd(gnotw(a), 1) }
func gsub(a, b uint64) uint64 { return gadd(a, gneg(b)) }
func guge(a, b uint64) bool   { _, c := gaddc(a, gnotw(b), 1); return c == 1 }
func gmul(a, b uint64) uint64 {
	var r uint64
	for b != 0 {
		if b&1 == 1 {
			r = gadd(r, a)
		}
		a <<= 1
		b >>= 1
	}
	return r
}
func gudivmod64(num, den uint64) (uint64, uint64) {
	var q, r uint64
	for i := 63; i >= 0; i-- {
		r = (r << 1) | ((num >> uint(i)) & 1)
		if guge(r, den) {
			r = gsub(r, den)
			q |= 1 << uint(i)
		}
	}
	return q, r
}

// ---- LAYER 3: bignum on the 64-bit gate ALU ----
type Big struct {
	sign int      // -1, 0, 1
	d    []uint32 // little-endian magnitude, no trailing zeros
}

func zero() Big { return Big{0, nil} }

func (x *Big) norm() {
	for len(x.d) > 0 && x.d[len(x.d)-1] == 0 {
		x.d = x.d[:len(x.d)-1]
	}
	if len(x.d) == 0 {
		x.sign = 0
	}
}
func (x Big) clone() Big {
	d := make([]uint32, len(x.d))
	copy(d, x.d)
	return Big{x.sign, d}
}

func cmpAbs(a, b Big) int {
	if len(a.d) != len(b.d) {
		if len(a.d) < len(b.d) {
			return -1
		}
		return 1
	}
	for i := len(a.d) - 1; i >= 0; i-- {
		if a.d[i] != b.d[i] {
			if a.d[i] < b.d[i] {
				return -1
			}
			return 1
		}
	}
	return 0
}

func addAbs(a, b Big) Big {
	n := len(a.d)
	if len(b.d) > n {
		n = len(b.d)
	}
	d := make([]uint32, 0, n+1)
	var carry uint64
	for i := 0; i < n; i++ {
		var av, bv uint64
		if i < len(a.d) {
			av = uint64(a.d[i])
		}
		if i < len(b.d) {
			bv = uint64(b.d[i])
		}
		s := gadd(gadd(av, bv), carry)
		d = append(d, uint32(s&0xffffffff))
		carry = s >> 32
	}
	if carry != 0 {
		d = append(d, uint32(carry))
	}
	r := Big{1, d}
	r.norm()
	return r
}

func subAbs(a, b Big) Big { // requires |a| >= |b|
	d := make([]uint32, 0, len(a.d))
	var borrow uint64
	for i := 0; i < len(a.d); i++ {
		av := uint64(a.d[i])
		var bv uint64
		if i < len(b.d) {
			bv = uint64(b.d[i])
		}
		s := gsub(gsub(av, bv), borrow)
		d = append(d, uint32(s&0xffffffff))
		if (s >> 32) != 0 {
			borrow = 1
		} else {
			borrow = 0
		}
	}
	r := Big{1, d}
	r.norm()
	return r
}

func bigAdd(a, b Big) Big {
	if a.sign == 0 {
		return b.clone()
	}
	if b.sign == 0 {
		return a.clone()
	}
	if a.sign == b.sign {
		r := addAbs(a, b)
		if len(r.d) != 0 {
			r.sign = a.sign
		}
		return r
	}
	c := cmpAbs(a, b)
	if c == 0 {
		return zero()
	}
	var r Big
	if c > 0 {
		r = subAbs(a, b)
		r.sign = a.sign
	} else {
		r = subAbs(b, a)
		r.sign = b.sign
	}
	if len(r.d) == 0 {
		r.sign = 0
	}
	return r
}
func bigNeg(a Big) Big { r := a.clone(); r.sign = -r.sign; return r }
func bigSub(a, b Big) Big { return bigAdd(a, bigNeg(b)) }

func bigMul(a, b Big) Big {
	if a.sign == 0 || b.sign == 0 {
		return zero()
	}
	d := make([]uint32, len(a.d)+len(b.d))
	for i := 0; i < len(a.d); i++ {
		var carry uint64
		for j := 0; j < len(b.d); j++ {
			prod := gmul(uint64(a.d[i]), uint64(b.d[j]))
			cur := uint64(d[i+j])
			s := gadd(gadd(prod, cur), carry)
			d[i+j] = uint32(s & 0xffffffff)
			carry = s >> 32
		}
		k := i + len(b.d)
		for carry != 0 {
			s := gadd(uint64(d[k]), carry)
			d[k] = uint32(s & 0xffffffff)
			carry = s >> 32
			k++
		}
	}
	sign := 1
	if a.sign != b.sign {
		sign = -1
	}
	r := Big{sign, d}
	r.norm()
	return r
}

func shl1(x *Big) {
	var carry uint32
	for i := 0; i < len(x.d); i++ {
		nv := (x.d[i] << 1) | carry
		carry = x.d[i] >> 31
		x.d[i] = nv
	}
	if carry != 0 {
		x.d = append(x.d, carry)
	}
}

func udiv(a, b Big) (Big, Big) {
	q := zero()
	r := zero()
	bits := len(a.d) * 32
	for p := bits - 1; p >= 0; p-- {
		shl1(&r)
		abit := (a.d[p>>5] >> uint(p&31)) & 1
		if abit == 1 {
			if len(r.d) == 0 {
				r.d = append(r.d, 1)
			} else {
				r.d[0] |= 1
			}
		}
		r.norm()
		if cmpAbs(r, b) >= 0 {
			r = subAbs(r, b)
			w := p >> 5
			for len(q.d) <= w {
				q.d = append(q.d, 0)
			}
			q.d[w] |= 1 << uint(p&31)
		}
	}
	q.sign = 1
	r.sign = 1
	q.norm()
	r.norm()
	return q, r
}

func bigDivmod(a, b Big) (Big, Big, bool) {
	if b.sign == 0 {
		return zero(), zero(), false
	}
	aa := a.clone()
	if len(aa.d) != 0 {
		aa.sign = 1
	}
	bb := b.clone()
	bb.sign = 1
	q, r := udiv(aa, bb)
	if len(q.d) == 0 {
		q.sign = 0
	} else if a.sign == b.sign {
		q.sign = 1
	} else {
		q.sign = -1
	}
	if len(r.d) == 0 {
		r.sign = 0
	} else {
		r.sign = a.sign
	}
	return q, r, true
}

func mulSmall(x *Big, m uint32) {
	var carry uint64
	for i := 0; i < len(x.d); i++ {
		s := gadd(gmul(uint64(x.d[i]), uint64(m)), carry)
		x.d[i] = uint32(s & 0xffffffff)
		carry = s >> 32
	}
	for carry != 0 {
		x.d = append(x.d, uint32(carry&0xffffffff))
		carry >>= 32
	}
	if len(x.d) != 0 && x.sign == 0 {
		x.sign = 1
	}
	x.norm()
}
func addSmall(x *Big, a uint32) {
	carry := uint64(a)
	i := 0
	for carry != 0 {
		var cur uint64
		if i < len(x.d) {
			cur = uint64(x.d[i])
		}
		s := gadd(cur, carry)
		if i >= len(x.d) {
			x.d = append(x.d, 0)
		}
		x.d[i] = uint32(s & 0xffffffff)
		carry = s >> 32
		i++
	}
	if len(x.d) != 0 && x.sign == 0 {
		x.sign = 1
	}
	x.norm()
}
func fromDec(s string) Big {
	r := zero()
	neg := false
	if len(s) > 0 && (s[0] == '-' || s[0] == '+') {
		neg = s[0] == '-'
		s = s[1:]
	}
	for i := 0; i < len(s); i++ {
		c := s[i]
		if c < '0' || c > '9' {
			continue
		}
		mulSmall(&r, 10)
		addSmall(&r, uint32(c-'0'))
	}
	if len(r.d) == 0 {
		r.sign = 0
	} else if neg {
		r.sign = -1
	} else {
		r.sign = 1
	}
	return r
}
func toDec(x Big) string {
	if x.sign == 0 {
		return "0"
	}
	t := x.clone()
	t.sign = 1
	var buf []byte
	for len(t.d) > 0 {
		var rem uint64
		for i := len(t.d) - 1; i >= 0; i-- {
			cur := (rem << 32) | uint64(t.d[i])
			q, r := gudivmod64(cur, 1000000000)
			t.d[i] = uint32(q)
			rem = r
		}
		t.norm()
		if len(t.d) > 0 {
			for k := 0; k < 9; k++ {
				q, r := gudivmod64(rem, 10)
				buf = append(buf, byte('0'+r))
				rem = q
			}
		} else {
			for rem > 0 {
				q, r := gudivmod64(rem, 10)
				buf = append(buf, byte('0'+r))
				rem = q
			}
		}
	}
	var sb strings.Builder
	if x.sign < 0 {
		sb.WriteByte('-')
	}
	for i := len(buf) - 1; i >= 0; i-- {
		sb.WriteByte(buf[i])
	}
	return sb.String()
}

// ---- small bignum helpers for the rational layer ----
func bigOne() Big          { return Big{1, []uint32{1}} }
func bigFromSmall(v uint32) Big {
	if v == 0 {
		return zero()
	}
	return Big{1, []uint32{v}}
}
func bigIsOne(x Big) bool { return x.sign == 1 && len(x.d) == 1 && x.d[0] == 1 }

func divmodSmall(x Big, m uint32) (Big, uint32) {
	d := make([]uint32, len(x.d))
	var rem uint64
	for i := len(x.d) - 1; i >= 0; i-- {
		cur := (rem << 32) | uint64(x.d[i])
		q, r := gudivmod64(cur, uint64(m))
		d[i] = uint32(q)
		rem = r
	}
	out := Big{1, d}
	out.norm()
	return out, uint32(rem)
}

func bigGcd(a, b Big) Big {
	a = a.clone()
	if len(a.d) == 0 {
		a.sign = 0
	} else {
		a.sign = 1
	}
	b = b.clone()
	if len(b.d) == 0 {
		b.sign = 0
	} else {
		b.sign = 1
	}
	for b.sign != 0 {
		_, r := udiv(a, b)
		a = b
		b = r
		if len(b.d) == 0 {
			b.sign = 0
		} else {
			b.sign = 1
		}
	}
	if len(a.d) == 0 {
		a.sign = 0
	} else {
		a.sign = 1
	}
	return a
}

func bigPow(base, e Big) Big {
	result := bigOne()
	if e.sign == 0 {
		return result
	}
	b := base.clone()
	top := len(e.d)*32 - 1
	for top >= 0 && (e.d[top>>5]>>uint(top&31))&1 == 0 {
		top--
	}
	for i := 0; i <= top; i++ {
		if (e.d[i>>5]>>uint(i&31))&1 == 1 {
			result = bigMul(result, b)
		}
		if i < top {
			b = bigMul(b, b)
		}
	}
	return result
}

// ---- more bignum helpers + significant-digit rounding ----
func bigSetInt(v int64) Big {
	if v == 0 {
		return zero()
	}
	neg := v < 0
	var uv uint64
	if neg {
		uv = uint64(-v)
	} else {
		uv = uint64(v)
	}
	var d []uint32
	for uv > 0 {
		d = append(d, uint32(uv&0xffffffff))
		uv >>= 32
	}
	s := 1
	if neg {
		s = -1
	}
	return Big{s, d}
}
func bigCmp(a, b Big) int { return bigSub(a, b).sign }
func pow10(k int) Big      { return bigPow(bigFromSmall(10), bigSetInt(int64(k))) }

func bigIsqrt(n Big) Big {
	if n.sign == 0 {
		return zero()
	}
	top := len(n.d)*32 - 1
	for top >= 0 && (n.d[top>>5]>>uint(top&31))&1 == 0 {
		top--
	}
	half := top/2 + 1
	x := Big{1, make([]uint32, half/32+1)}
	x.d[half/32] = 1 << uint(half%32)
	for i := 0; i < 2000; i++ {
		q, _, _ := bigDivmod(n, x)
		s := bigAdd(x, q)
		nx, _ := divmodSmall(s, 2)
		if bigCmp(nx, x) >= 0 {
			break
		}
		x = nx
	}
	for i := 0; i < 4; i++ {
		if bigCmp(bigMul(x, x), n) <= 0 {
			break
		}
		x = bigSub(x, bigOne())
	}
	return x
}

func cmp10eDenNum(e int, den, num Big) int {
	if e >= 0 {
		return bigCmp(bigMul(pow10(e), den), num)
	}
	return bigCmp(den, bigMul(pow10(-e), num))
}

func roundSigRat(numIn, denIn Big, n int) (Big, Big) {
	if numIn.sign == 0 {
		return zero(), bigOne()
	}
	sign := numIn.sign
	num := numIn.clone()
	num.sign = 1
	den := denIn.clone()
	den.sign = 1
	e := len(toDec(num)) - len(toDec(den))
	for {
		if cmp10eDenNum(e, den, num) > 0 {
			e--
			continue
		}
		if cmp10eDenNum(e+1, den, num) <= 0 {
			e++
			continue
		}
		break
	}
	scale := n - 1 - e
	var snum, sden Big
	if scale >= 0 {
		snum = bigMul(num, pow10(scale))
		sden = den
	} else {
		snum = num
		sden = bigMul(den, pow10(-scale))
	}
	q, r, _ := bigDivmod(snum, sden)
	twoR := bigAdd(r, r)
	c := bigCmp(twoR, sden)
	if c > 0 || (c == 0 && len(q.d) > 0 && q.d[0]&1 == 1) {
		q = bigAdd(q, bigOne())
	}
	ex := e - n + 1
	var rnum, rden Big
	if ex >= 0 {
		rnum = bigMul(q, pow10(ex))
		rden = bigOne()
	} else {
		rnum = q
		rden = pow10(-ex)
	}
	if len(rnum.d) == 0 {
		rnum.sign = 0
	} else {
		rnum.sign = sign
	}
	return rnum, rden
}

// ---- fixed-point transcendental functions ----
const WP = 120
const FUNC_SIG = 80
const OUT_SIG = 50
const PI_STR = "3.14159265358979323846264338327950288419716939937510" +
	"5820974944592307816406286208998628034825342117067982148086513282306647093844609550582231725359408128"

type FP struct{ scale, pi, twoPi, ln2, ln10, e Big }

var fpVal FP
var fpReady bool

func fpc() *FP {
	if !fpReady {
		fpVal = buildFP()
		fpReady = true
	}
	return &fpVal
}

func fpFromRat(num, den, s Big) Big { q, _, _ := bigDivmod(bigMul(num, s), den); return q }
func fpMul(a, b, s Big) Big         { q, _, _ := bigDivmod(bigMul(a, b), s); return q }
func fpDiv(a, b, s Big) Big         { q, _, _ := bigDivmod(bigMul(a, s), b); return q }
func fpDivInt(a Big, k uint32) Big {
	aa := a.clone()
	neg := aa.sign < 0
	if len(aa.d) == 0 {
		aa.sign = 0
	} else {
		aa.sign = 1
	}
	q, _ := divmodSmall(aa, k)
	if neg {
		q.sign = -q.sign
	}
	return q
}
func fpParseConst(s string, sc Big) Big {
	ip, fp := s, ""
	if i := strings.IndexByte(s, '.'); i >= 0 {
		ip = s[:i]
		fp = s[i+1:]
	}
	return fpFromRat(fromDec(ip+fp), pow10(len(fp)), sc)
}
func fpAtanh(y, s Big) Big {
	sum, term := y, y
	y2 := fpMul(y, y, s)
	var k uint32 = 1
	for term.sign != 0 && k < 200000 {
		term = fpMul(term, y2, s)
		sum = bigAdd(sum, fpDivInt(term, 2*k+1))
		k++
	}
	return sum
}
func fpExp(x, s Big) Big {
	r := x
	m := 0
	for {
		a := r
		if len(a.d) == 0 {
			a.sign = 0
		} else {
			a.sign = 1
		}
		if cmpAbs(a, s) < 0 || m > 8192 {
			break
		}
		r = fpDivInt(r, 2)
		m++
	}
	sum, term := s, s
	var k uint32 = 1
	for term.sign != 0 && k < 200000 {
		term = fpDivInt(fpMul(term, r, s), k)
		sum = bigAdd(sum, term)
		k++
	}
	for i := 0; i < m; i++ {
		sum = fpMul(sum, sum, s)
	}
	return sum
}
func fpLn(x, s, ln2 Big) Big {
	xx := x
	e2 := 0
	twoS := bigAdd(s, s)
	for cmpAbs(xx, twoS) >= 0 {
		xx = fpDivInt(xx, 2)
		e2++
	}
	for cmpAbs(xx, s) < 0 {
		xx = bigAdd(xx, xx)
		e2--
	}
	y := fpDiv(bigSub(xx, s), bigAdd(xx, s), s)
	at := fpAtanh(y, s)
	return bigAdd(bigAdd(at, at), bigMul(ln2, bigSetInt(int64(e2))))
}
func roundFpToInt(v, s Big) Big {
	q, r, _ := bigDivmod(v, s)
	ar := r
	if len(ar.d) == 0 {
		ar.sign = 0
	} else {
		ar.sign = 1
	}
	if bigCmp(bigAdd(ar, ar), s) >= 0 {
		d := int64(1)
		if v.sign < 0 {
			d = -1
		}
		return bigAdd(q, bigSetInt(d))
	}
	return q
}
func fpReduce(x, s, twoPi Big) Big {
	k := roundFpToInt(fpDiv(x, twoPi, s), s)
	return bigSub(x, bigMul(k, twoPi))
}
func fpSin(x, s, twoPi Big) Big {
	xr := fpReduce(x, s, twoPi)
	sum, term := xr, xr
	x2 := fpMul(xr, xr, s)
	var k uint32 = 1
	for term.sign != 0 && k < 200000 {
		term = fpDivInt(fpMul(term, x2, s), (2*k)*(2*k+1))
		term.sign = -term.sign
		sum = bigAdd(sum, term)
		k++
	}
	return sum
}
func fpCos(x, s, twoPi Big) Big {
	xr := fpReduce(x, s, twoPi)
	sum, term := s, s
	x2 := fpMul(xr, xr, s)
	var k uint32 = 1
	for term.sign != 0 && k < 200000 {
		term = fpDivInt(fpMul(term, x2, s), (2*k-1)*(2*k))
		term.sign = -term.sign
		sum = bigAdd(sum, term)
		k++
	}
	return sum
}
func fpSqrt(x, s Big) Big { return bigIsqrt(bigMul(x, s)) }
func fpAtan(x, s Big) Big {
	xx := x
	neg := xx.sign < 0
	if len(xx.d) == 0 {
		xx.sign = 0
	} else {
		xx.sign = 1
	}
	thresh := fpDivInt(s, 10)
	m := 0
	for cmpAbs(xx, thresh) > 0 && m < 4096 {
		x2 := fpMul(xx, xx, s)
		rt := fpSqrt(bigAdd(s, x2), s)
		xx = fpDiv(xx, bigAdd(s, rt), s)
		m++
	}
	sum := xx
	term := xx
	x2 := fpMul(xx, xx, s)
	var k uint32 = 1
	for term.sign != 0 && k < 200000 {
		term = fpMul(term, x2, s)
		term.sign = -term.sign
		sum = bigAdd(sum, fpDivInt(term, 2*k+1))
		k++
	}
	for i := 0; i < m; i++ {
		sum = bigAdd(sum, sum)
	}
	if neg {
		sum.sign = -sum.sign
	}
	return sum
}
func fpAsin(x, s, pi Big) (Big, bool) {
	ax := x
	if len(ax.d) == 0 {
		ax.sign = 0
	} else {
		ax.sign = 1
	}
	c := cmpAbs(ax, s)
	if c > 0 {
		return Big{}, false
	}
	if c == 0 {
		h := fpDivInt(pi, 2)
		if x.sign < 0 {
			h.sign = -h.sign
		}
		return h, true
	}
	d := bigSub(s, fpMul(x, x, s))
	return fpAtan(fpDiv(x, fpSqrt(d, s), s), s), true
}
func fpAcos(x, s, pi Big) (Big, bool) {
	a, ok := fpAsin(x, s, pi)
	if !ok {
		return Big{}, false
	}
	return bigSub(fpDivInt(pi, 2), a), true
}
func fpSinh(x, s Big) Big { nx := x; nx.sign = -nx.sign; return fpDivInt(bigSub(fpExp(x, s), fpExp(nx, s)), 2) }
func fpCosh(x, s Big) Big { nx := x; nx.sign = -nx.sign; return fpDivInt(bigAdd(fpExp(x, s), fpExp(nx, s)), 2) }
func fpTanh(x, s Big) Big {
	nx := x
	nx.sign = -nx.sign
	e1 := fpExp(x, s)
	e2 := fpExp(nx, s)
	return fpDiv(bigSub(e1, e2), bigAdd(e1, e2), s)
}

func fpAsinh(x, s, ln2 Big) Big { rt := fpSqrt(bigAdd(fpMul(x, x, s), s), s); return fpLn(bigAdd(x, rt), s, ln2) }
func fpAcosh(x, s, ln2 Big) (Big, bool) {
	if bigCmp(x, s) < 0 {
		return Big{}, false
	}
	rt := fpSqrt(bigSub(fpMul(x, x, s), s), s)
	return fpLn(bigAdd(x, rt), s, ln2), true
}
func fpAtanhU(x, s, ln2 Big) (Big, bool) {
	ax := x
	if len(ax.d) == 0 {
		ax.sign = 0
	} else {
		ax.sign = 1
	}
	if cmpAbs(ax, s) >= 0 {
		return Big{}, false
	}
	q := fpDiv(bigAdd(s, x), bigSub(s, x), s)
	return fpDivInt(fpLn(q, s, ln2), 2), true
}
func fpLogb(x, b, s, ln2 Big) (Big, bool) {
	if x.sign <= 0 || b.sign <= 0 {
		return Big{}, false
	}
	lb := fpLn(b, s, ln2)
	if lb.sign == 0 {
		return Big{}, false
	}
	return fpDiv(fpLn(x, s, ln2), lb, s), true
}
func fpHypot(x, y, s Big) Big { return fpSqrt(bigAdd(fpMul(x, x, s), fpMul(y, y, s)), s) }
func fpAtan2(y, x, s, pi Big) Big {
	if x.sign > 0 {
		return fpAtan(fpDiv(y, x, s), s)
	}
	if x.sign < 0 {
		a := fpAtan(fpDiv(y, x, s), s)
		if y.sign >= 0 {
			return bigAdd(a, pi)
		}
		return bigSub(a, pi)
	}
	if y.sign > 0 {
		return fpDivInt(pi, 2)
	}
	if y.sign < 0 {
		h := fpDivInt(pi, 2)
		h.sign = -h.sign
		return h
	}
	return zero()
}

func buildFP() FP {
	scale := pow10(WP)
	pi := fpParseConst(PI_STR, scale)
	twoPi := bigAdd(pi, pi)
	at := fpAtanh(fpDivInt(scale, 3), scale)
	ln2 := bigAdd(at, at)
	tenfp := fpFromRat(bigFromSmall(10), bigOne(), scale)
	ln10 := fpLn(tenfp, scale, ln2)
	e := fpExp(scale, scale)
	return FP{scale, pi, twoPi, ln2, ln10, e}
}

// ---- exact rationals (with an inexact flag for function-tainted values) ----
type Rat struct {
	num, den Big
	inexact  bool
}

func ratNorm(r *Rat) {
	if r.num.sign == 0 {
		r.den = bigOne()
		return
	}
	if r.den.sign < 0 {
		r.num.sign = -r.num.sign
		r.den.sign = 1
	}
	g := bigGcd(r.num, r.den)
	if !bigIsOne(g) {
		q, _, _ := bigDivmod(r.num, g)
		r.num = q
		q, _, _ = bigDivmod(r.den, g)
		r.den = q
	}
}
func ratParse(s string) Rat {
	var r Rat
	if i := strings.IndexByte(s, '/'); i >= 0 {
		r = Rat{fromDec(s[:i]), fromDec(s[i+1:]), false}
	} else {
		r = Rat{fromDec(s), bigOne(), false}
	}
	ratNorm(&r)
	return r
}
func ratAdd(a, b Rat) Rat {
	r := Rat{bigAdd(bigMul(a.num, b.den), bigMul(b.num, a.den)), bigMul(a.den, b.den), a.inexact || b.inexact}
	ratNorm(&r)
	return r
}
func ratSub(a, b Rat) Rat {
	r := Rat{bigSub(bigMul(a.num, b.den), bigMul(b.num, a.den)), bigMul(a.den, b.den), a.inexact || b.inexact}
	ratNorm(&r)
	return r
}
func ratMul(a, b Rat) Rat {
	r := Rat{bigMul(a.num, b.num), bigMul(a.den, b.den), a.inexact || b.inexact}
	ratNorm(&r)
	return r
}
func ratDiv(a, b Rat) (Rat, bool) {
	if b.num.sign == 0 {
		return Rat{}, false
	}
	r := Rat{bigMul(a.num, b.den), bigMul(a.den, b.num), a.inexact || b.inexact}
	ratNorm(&r)
	return r, true
}
func ratIdiv(a, b Rat) (Rat, bool) {
	dd := bigMul(a.den, b.num)
	if dd.sign == 0 {
		return Rat{}, false
	}
	q, _, _ := bigDivmod(bigMul(a.num, b.den), dd)
	return Rat{q, bigOne(), a.inexact || b.inexact}, true
}
func ratMod(a, b Rat) (Rat, bool) {
	t, ok := ratIdiv(a, b)
	if !ok {
		return Rat{}, false
	}
	return ratSub(a, ratMul(b, t)), true
}

// ratPow return code: 1 ok, 0 div-by-zero, -1 non-integer exponent (caller handles)
func ratPow(a, b Rat) (Rat, int) {
	if !bigIsOne(b.den) {
		return Rat{}, -1
	}
	inx := a.inexact || b.inexact
	if b.num.sign == 0 {
		return Rat{bigOne(), bigOne(), inx}, 1
	}
	m := b.num.clone()
	m.sign = 1
	pn := bigPow(a.num, m)
	pd := bigPow(a.den, m)
	var r Rat
	if b.num.sign > 0 {
		r = Rat{pn, pd, inx}
	} else {
		if a.num.sign == 0 {
			return Rat{}, 0
		}
		r = Rat{pd, pn, inx}
	}
	ratNorm(&r)
	return r, 1
}
func ratFromFp(v Big) Rat {
	n, d := roundSigRat(v, fpc().scale, FUNC_SIG)
	r := Rat{n, d, true}
	ratNorm(&r)
	return r
}
func ratToString(r Rat) string {
	if r.num.sign == 0 {
		return "0"
	}
	if bigIsOne(r.den) {
		return toDec(r.num)
	}
	q := r.den.clone()
	q.sign = 1
	var a, b uint32
	for {
		qq, rem := divmodSmall(q, 2)
		if rem == 0 {
			q = qq
			a++
		} else {
			break
		}
	}
	for {
		qq, rem := divmodSmall(q, 5)
		if rem == 0 {
			q = qq
			b++
		} else {
			break
		}
	}
	if bigIsOne(q) {
		k := a
		if b > k {
			k = b
		}
		tenk := bigPow(bigFromSmall(10), bigFromSmall(k))
		scale, _, _ := bigDivmod(tenk, r.den)
		n := bigMul(r.num, scale)
		neg := n.sign < 0
		absn := n.clone()
		if len(absn.d) == 0 {
			absn.sign = 0
		} else {
			absn.sign = 1
		}
		digits := toDec(absn)
		ki := int(k)
		var intp, frac string
		if len(digits) <= ki {
			intp = "0"
			frac = strings.Repeat("0", ki-len(digits)) + digits
		} else {
			il := len(digits) - ki
			intp = digits[:il]
			frac = digits[il:]
		}
		frac = strings.TrimRight(frac, "0")
		var sb strings.Builder
		if neg {
			sb.WriteByte('-')
		}
		sb.WriteString(intp)
		if frac != "" {
			sb.WriteByte('.')
			sb.WriteString(frac)
		}
		return sb.String()
	}
	return toDec(r.num) + "/" + toDec(r.den)
}

func main() {
	var scanner *bufio.Scanner
	if len(os.Args) > 1 {
		f, err := os.Open(os.Args[1])
		if err != nil {
			os.Stderr.WriteString("engine_go: " + err.Error() + "\n")
			os.Exit(2)
		}
		defer f.Close()
		scanner = bufio.NewScanner(f)
	} else {
		scanner = bufio.NewScanner(os.Stdin)
	}
	buf := make([]byte, 0, 1024*1024)
	scanner.Buffer(buf, 16*1024*1024)

	stack := make([]Rat, 0, 64)
	for scanner.Scan() {
		line := strings.TrimSpace(scanner.Text())
		if line == "" {
			continue
		}
		parts := strings.Fields(line)
		op := parts[0]
		switch op {
		case "PUSH":
			stack = append(stack, ratParse(parts[1]))
		case "NEG":
			n := len(stack)
			stack[n-1].num.sign = -stack[n-1].num.sign
		case "CONST":
			c := fpc()
			var v Big
			switch parts[1] {
			case "pi":
				v = c.pi
			case "e":
				v = c.e
			default:
				os.Stdout.WriteString("ERR:UNKNOWN\n")
				return
			}
			stack = append(stack, ratFromFp(v))
		case "FUNC":
			name := parts[1]
			if len(parts) > 2 && parts[2] == "2" {
				c := fpc()
				n := len(stack)
				b := stack[n-1]
				a := stack[n-2]
				stack = stack[:n-2]
				var res Rat
				switch name {
				case "gcd", "lcm":
					if !bigIsOne(a.den) || !bigIsOne(b.den) {
						os.Stdout.WriteString("ERR:DOMAIN\n")
						return
					}
					g := bigGcd(a.num, b.num)
					var num Big
					if name == "gcd" {
						num = g
					} else if a.num.sign == 0 || b.num.sign == 0 {
						num = zero()
					} else {
						p := bigMul(a.num, b.num)
						if len(p.d) == 0 {
							p.sign = 0
						} else {
							p.sign = 1
						}
						q, _, _ := bigDivmod(p, g)
						num = q
					}
					res = Rat{num, bigOne(), a.inexact || b.inexact}
				case "max", "min":
					c2 := bigCmp(bigMul(a.num, b.den), bigMul(b.num, a.den))
					if (name == "max" && c2 >= 0) || (name == "min" && c2 <= 0) {
						res = a
					} else {
						res = b
					}
				case "comb", "perm":
					if !bigIsOne(a.den) || !bigIsOne(b.den) || a.num.sign < 0 || b.num.sign < 0 || bigCmp(a.num, bigSetInt(20000)) > 0 {
						os.Stdout.WriteString("ERR:DOMAIN\n")
						return
					}
					var nn, rr int64
					if len(a.num.d) > 0 {
						nn = int64(a.num.d[0])
					}
					if len(b.num.d) > 0 {
						rr = int64(b.num.d[0])
					}
					var num Big
					if rr > nn {
						num = zero()
					} else {
						p := bigOne()
						for i := int64(0); i < rr; i++ {
							p = bigMul(p, bigSetInt(nn-i))
						}
						if name == "perm" {
							num = p
						} else {
							rf := bigOne()
							for i := int64(2); i <= rr; i++ {
								rf = bigMul(rf, bigSetInt(i))
							}
							q, _, _ := bigDivmod(p, rf)
							num = q
						}
					}
					res = Rat{num, bigOne(), a.inexact || b.inexact}
				case "log":
					v, ok := fpLogb(fpFromRat(a.num, a.den, c.scale), fpFromRat(b.num, b.den, c.scale), c.scale, c.ln2)
					if !ok {
						os.Stdout.WriteString("ERR:DOMAIN\n")
						return
					}
					res = ratFromFp(v)
				case "hypot":
					res = ratFromFp(fpHypot(fpFromRat(a.num, a.den, c.scale), fpFromRat(b.num, b.den, c.scale), c.scale))
				case "atan2":
					res = ratFromFp(fpAtan2(fpFromRat(a.num, a.den, c.scale), fpFromRat(b.num, b.den, c.scale), c.scale, c.pi))
				default:
					os.Stdout.WriteString("ERR:UNKNOWN\n")
					return
				}
				stack = append(stack, res)
				continue
			}
			n := len(stack)
			a := stack[n-1]
			stack = stack[:n-1]
			if name == "abs" {
				a.num.sign = 0
				if len(a.num.d) > 0 {
					a.num.sign = 1
				}
				stack = append(stack, a)
				continue
			}
			if name == "fact" {
				if !bigIsOne(a.den) || a.num.sign < 0 || bigCmp(a.num, bigSetInt(20000)) > 0 {
					os.Stdout.WriteString("ERR:DOMAIN\n")
					return
				}
				var nn int64
				if len(a.num.d) > 0 {
					nn = int64(a.num.d[0])
				}
				f := bigOne()
				for i := int64(2); i <= nn; i++ {
					f = bigMul(f, bigSetInt(i))
				}
				stack = append(stack, Rat{f, bigOne(), a.inexact})
				continue
			}
			c := fpc()
			x := fpFromRat(a.num, a.den, c.scale)
			var y Big
			switch name {
			case "sqrt":
				if x.sign < 0 {
					os.Stdout.WriteString("ERR:DOMAIN\n")
					return
				}
				y = bigIsqrt(bigMul(x, c.scale))
			case "exp":
				y = fpExp(x, c.scale)
			case "ln":
				if x.sign <= 0 {
					os.Stdout.WriteString("ERR:DOMAIN\n")
					return
				}
				y = fpLn(x, c.scale, c.ln2)
			case "log", "log10":
				if x.sign <= 0 {
					os.Stdout.WriteString("ERR:DOMAIN\n")
					return
				}
				y = fpDiv(fpLn(x, c.scale, c.ln2), c.ln10, c.scale)
			case "sin":
				y = fpSin(x, c.scale, c.twoPi)
			case "cos":
				y = fpCos(x, c.scale, c.twoPi)
			case "tan":
				sn := fpSin(x, c.scale, c.twoPi)
				co := fpCos(x, c.scale, c.twoPi)
				if co.sign == 0 {
					os.Stdout.WriteString("ERR:DOMAIN\n")
					return
				}
				y = fpDiv(sn, co, c.scale)
			case "asin":
				v, ok := fpAsin(x, c.scale, c.pi)
				if !ok {
					os.Stdout.WriteString("ERR:DOMAIN\n")
					return
				}
				y = v
			case "acos":
				v, ok := fpAcos(x, c.scale, c.pi)
				if !ok {
					os.Stdout.WriteString("ERR:DOMAIN\n")
					return
				}
				y = v
			case "atan":
				y = fpAtan(x, c.scale)
			case "sinh":
				y = fpSinh(x, c.scale)
			case "cosh":
				y = fpCosh(x, c.scale)
			case "tanh":
				y = fpTanh(x, c.scale)
			case "asinh":
				y = fpAsinh(x, c.scale, c.ln2)
			case "acosh":
				v, ok := fpAcosh(x, c.scale, c.ln2)
				if !ok {
					os.Stdout.WriteString("ERR:DOMAIN\n")
					return
				}
				y = v
			case "atanh":
				v, ok := fpAtanhU(x, c.scale, c.ln2)
				if !ok {
					os.Stdout.WriteString("ERR:DOMAIN\n")
					return
				}
				y = v
			case "rad":
				y = fpMul(x, fpDivInt(c.pi, 180), c.scale)
			case "deg":
				f180 := fpFromRat(bigSetInt(180), bigOne(), c.scale)
				y = fpDiv(fpMul(x, f180, c.scale), c.pi, c.scale)
			case "cbrt":
				if x.sign == 0 {
					y = zero()
				} else {
					ax := x
					neg := ax.sign < 0
					ax.sign = 1
					y = fpExp(fpDivInt(fpLn(ax, c.scale, c.ln2), 3), c.scale)
					if neg {
						y.sign = -y.sign
					}
				}
			default:
				os.Stdout.WriteString("ERR:UNKNOWN\n")
				return
			}
			stack = append(stack, ratFromFp(y))
		default:
			n := len(stack)
			b := stack[n-1]
			a := stack[n-2]
			stack = stack[:n-2]
			var res Rat
			switch op {
			case "ADD":
				res = ratAdd(a, b)
			case "SUB":
				res = ratSub(a, b)
			case "MUL":
				res = ratMul(a, b)
			case "DIV":
				r, ok := ratDiv(a, b)
				if !ok {
					os.Stdout.WriteString("ERR:DIVZERO\n")
					return
				}
				res = r
			case "IDIV":
				r, ok := ratIdiv(a, b)
				if !ok {
					os.Stdout.WriteString("ERR:DIVZERO\n")
					return
				}
				res = r
			case "MOD":
				r, ok := ratMod(a, b)
				if !ok {
					os.Stdout.WriteString("ERR:DIVZERO\n")
					return
				}
				res = r
			case "POW":
				r, code := ratPow(a, b)
				if code == 0 {
					os.Stdout.WriteString("ERR:DIVZERO\n")
					return
				}
				if code < 0 { // non-integer exponent: a^b = exp(b ln a)
					c := fpc()
					if a.num.sign < 0 {
						os.Stdout.WriteString("ERR:DOMAIN\n")
						return
					}
					if a.num.sign == 0 {
						if b.num.sign <= 0 {
							os.Stdout.WriteString("ERR:DOMAIN\n")
							return
						}
						res = Rat{zero(), bigOne(), true}
					} else {
						bb := fpFromRat(a.num, a.den, c.scale)
						eb := fpFromRat(b.num, b.den, c.scale)
						y := fpExp(fpMul(eb, fpLn(bb, c.scale, c.ln2), c.scale), c.scale)
						res = ratFromFp(y)
					}
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
		os.Stderr.WriteString("engine_go: empty stack\n")
		os.Exit(4)
	}
	top := stack[len(stack)-1]
	if top.inexact {
		n, d := roundSigRat(top.num, top.den, OUT_SIG)
		r2 := Rat{n, d, true}
		ratNorm(&r2)
		os.Stdout.WriteString(ratToString(r2) + "\n")
	} else {
		os.Stdout.WriteString(ratToString(top) + "\n")
	}
}
