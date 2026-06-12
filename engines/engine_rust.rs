// ===========================================================================
// ENGINE B of the cathedral: THE GATE-LEVEL VM, AGAIN  (language: Rust)
// ---------------------------------------------------------------------------
// Independent reimplementation: a 64-bit ALU built from NAND, and an
// arbitrary-precision integer type built on top of that 64-bit gate ALU
// (base-2^32 limbs). Native +/* never compute the answer; only shifts and
// indexing are used as plumbing.
// Reads bytecode from argv[1] (or STDIN), prints top of stack in decimal.
// ===========================================================================
use std::io::Read;

// ---- LAYER 1+2: gates and the 64-bit ALU ----
fn nand(a: u8, b: u8) -> u8 { if a & b != 0 { 0 } else { 1 } }
fn not(a: u8) -> u8 { nand(a, a) }
fn and(a: u8, b: u8) -> u8 { not(nand(a, b)) }
fn or(a: u8, b: u8) -> u8 { nand(not(a), not(b)) }
fn xor(a: u8, b: u8) -> u8 { let t = nand(a, b); nand(nand(a, t), nand(b, t)) }
fn full_adder(a: u8, b: u8, c: u8) -> (u8, u8) {
    let axb = xor(a, b);
    (xor(axb, c), or(and(axb, c), and(a, b)))
}
fn bit64(x: u64, i: u32) -> u8 { ((x >> i) & 1) as u8 }

fn gadd_c(a: u64, b: u64, cin: u8) -> (u64, u8) {
    let mut r: u64 = 0;
    let mut c = cin;
    for i in 0..64 {
        let (s, co) = full_adder(bit64(a, i), bit64(b, i), c);
        if s == 1 { r |= 1u64 << i; }
        c = co;
    }
    (r, c)
}
fn gadd(a: u64, b: u64) -> u64 { gadd_c(a, b, 0).0 }
fn gnotw(a: u64) -> u64 {
    let mut r: u64 = 0;
    for i in 0..64 { if not(bit64(a, i)) == 1 { r |= 1u64 << i; } }
    r
}
fn gneg(a: u64) -> u64 { gadd(gnotw(a), 1) }
fn gsub(a: u64, b: u64) -> u64 { gadd(a, gneg(b)) }
fn guge(a: u64, b: u64) -> bool { gadd_c(a, gnotw(b), 1).1 == 1 }
fn gmul(a: u64, b: u64) -> u64 {
    let mut r: u64 = 0;
    for i in 0..64 { if bit64(b, i) == 1 { r = gadd(r, a << i); } }
    r
}
fn gudivmod64(num: u64, den: u64) -> (u64, u64) {
    let mut q: u64 = 0;
    let mut r: u64 = 0;
    for i in (0..64).rev() {
        r = (r << 1) | ((num >> i) & 1);
        if guge(r, den) { r = gsub(r, den); q |= 1u64 << i; }
    }
    (q, r)
}

// ---- LAYER 3: bignum on the 64-bit gate ALU ----
#[derive(Clone)]
struct Big { sign: i32, d: Vec<u32> } // little-endian magnitude, no trailing zeros

impl Big {
    fn zero() -> Big { Big { sign: 0, d: vec![] } }
    fn norm(&mut self) {
        while let Some(&0) = self.d.last() { self.d.pop(); }
        if self.d.is_empty() { self.sign = 0; }
    }
}

fn cmp_abs(a: &Big, b: &Big) -> i32 {
    if a.d.len() != b.d.len() { return if a.d.len() < b.d.len() { -1 } else { 1 }; }
    for i in (0..a.d.len()).rev() {
        if a.d[i] != b.d[i] { return if a.d[i] < b.d[i] { -1 } else { 1 }; }
    }
    0
}

fn add_abs(a: &Big, b: &Big) -> Big {
    let n = a.d.len().max(b.d.len());
    let mut d = Vec::with_capacity(n + 1);
    let mut carry: u64 = 0;
    for i in 0..n {
        let av = if i < a.d.len() { a.d[i] as u64 } else { 0 };
        let bv = if i < b.d.len() { b.d[i] as u64 } else { 0 };
        let s = gadd(gadd(av, bv), carry);
        d.push((s & 0xffffffff) as u32);
        carry = s >> 32;
    }
    if carry != 0 { d.push(carry as u32); }
    let mut r = Big { sign: 1, d };
    r.norm();
    r
}

fn sub_abs(a: &Big, b: &Big) -> Big { // requires |a| >= |b|
    let mut d = Vec::with_capacity(a.d.len());
    let mut borrow: u64 = 0;
    for i in 0..a.d.len() {
        let av = a.d[i] as u64;
        let bv = if i < b.d.len() { b.d[i] as u64 } else { 0 };
        let s = gsub(gsub(av, bv), borrow);
        d.push((s & 0xffffffff) as u32);
        borrow = if (s >> 32) != 0 { 1 } else { 0 };
    }
    let mut r = Big { sign: 1, d };
    r.norm();
    r
}

fn big_add(a: &Big, b: &Big) -> Big {
    if a.sign == 0 { return b.clone(); }
    if b.sign == 0 { return a.clone(); }
    if a.sign == b.sign {
        let mut r = add_abs(a, b);
        r.sign = if r.d.is_empty() { 0 } else { a.sign };
        r
    } else {
        let c = cmp_abs(a, b);
        if c == 0 { Big::zero() }
        else if c > 0 { let mut r = sub_abs(a, b); r.sign = if r.d.is_empty() { 0 } else { a.sign }; r }
        else { let mut r = sub_abs(b, a); r.sign = if r.d.is_empty() { 0 } else { b.sign }; r }
    }
}
fn big_neg(a: &Big) -> Big { let mut r = a.clone(); r.sign = -r.sign; r }
fn big_sub(a: &Big, b: &Big) -> Big { big_add(a, &big_neg(b)) }

fn big_mul(a: &Big, b: &Big) -> Big {
    if a.sign == 0 || b.sign == 0 { return Big::zero(); }
    let mut d = vec![0u32; a.d.len() + b.d.len()];
    for i in 0..a.d.len() {
        let mut carry: u64 = 0;
        for j in 0..b.d.len() {
            let prod = gmul(a.d[i] as u64, b.d[j] as u64);
            let cur = d[i + j] as u64;
            let s = gadd(gadd(prod, cur), carry);
            d[i + j] = (s & 0xffffffff) as u32;
            carry = s >> 32;
        }
        let mut k = i + b.d.len();
        while carry != 0 {
            let s = gadd(d[k] as u64, carry);
            d[k] = (s & 0xffffffff) as u32;
            carry = s >> 32;
            k += 1;
        }
    }
    let mut r = Big { sign: if a.sign == b.sign { 1 } else { -1 }, d };
    r.norm();
    r
}

fn shl1(x: &mut Big) {
    let mut carry: u32 = 0;
    for i in 0..x.d.len() {
        let nv = (x.d[i] << 1) | carry;
        carry = x.d[i] >> 31;
        x.d[i] = nv;
    }
    if carry != 0 { x.d.push(carry); }
}

fn udiv(a: &Big, b: &Big) -> (Big, Big) {
    let mut q = Big::zero();
    let mut r = Big::zero();
    let bits = a.d.len() * 32;
    for p in (0..bits).rev() {
        shl1(&mut r);
        let abit = (a.d[p >> 5] >> (p & 31)) & 1;
        if abit == 1 {
            if r.d.is_empty() { r.d.push(1); } else { r.d[0] |= 1; }
        }
        r.norm();
        if cmp_abs(&r, b) >= 0 {
            r = sub_abs(&r, b);
            let w = p >> 5;
            while q.d.len() <= w { q.d.push(0); }
            q.d[w] |= 1u32 << (p & 31);
        }
    }
    q.sign = 1; r.sign = 1;
    q.norm(); r.norm();
    (q, r)
}

fn big_divmod(a: &Big, b: &Big) -> Option<(Big, Big)> {
    if b.sign == 0 { return None; }
    let mut aa = a.clone(); aa.sign = if aa.d.is_empty() { 0 } else { 1 };
    let mut bb = b.clone(); bb.sign = 1;
    let (mut q, mut r) = udiv(&aa, &bb);
    q.sign = if q.d.is_empty() { 0 } else if a.sign == b.sign { 1 } else { -1 };
    r.sign = if r.d.is_empty() { 0 } else { a.sign };
    Some((q, r))
}

fn mul_small(x: &mut Big, m: u32) {
    let mut carry: u64 = 0;
    for i in 0..x.d.len() {
        let s = gadd(gmul(x.d[i] as u64, m as u64), carry);
        x.d[i] = (s & 0xffffffff) as u32;
        carry = s >> 32;
    }
    while carry != 0 { x.d.push((carry & 0xffffffff) as u32); carry >>= 32; }
    if !x.d.is_empty() && x.sign == 0 { x.sign = 1; }
    x.norm();
}
fn add_small(x: &mut Big, a: u32) {
    let mut carry: u64 = a as u64;
    let mut i = 0;
    while carry != 0 {
        let cur = if i < x.d.len() { x.d[i] as u64 } else { 0 };
        let s = gadd(cur, carry);
        if i >= x.d.len() { x.d.push(0); }
        x.d[i] = (s & 0xffffffff) as u32;
        carry = s >> 32;
        i += 1;
    }
    if !x.d.is_empty() && x.sign == 0 { x.sign = 1; }
    x.norm();
}
fn from_dec(s: &str) -> Big {
    let mut r = Big::zero();
    let bytes = s.as_bytes();
    let mut neg = false;
    let mut start = 0;
    if !bytes.is_empty() && (bytes[0] == b'-' || bytes[0] == b'+') {
        neg = bytes[0] == b'-';
        start = 1;
    }
    for &c in &bytes[start..] {
        if c < b'0' || c > b'9' { continue; }
        mul_small(&mut r, 10);
        add_small(&mut r, (c - b'0') as u32);
    }
    if r.d.is_empty() { r.sign = 0; } else { r.sign = if neg { -1 } else { 1 }; }
    r
}
fn to_dec(x: &Big) -> String {
    if x.sign == 0 { return "0".to_string(); }
    let mut t = x.clone(); t.sign = 1;
    let mut buf: Vec<u8> = Vec::new();
    while !t.d.is_empty() {
        let mut rem: u64 = 0;
        for i in (0..t.d.len()).rev() {
            let cur = (rem << 32) | (t.d[i] as u64);
            let (q, r) = gudivmod64(cur, 1_000_000_000);
            t.d[i] = q as u32;
            rem = r;
        }
        t.norm();
        if !t.d.is_empty() {
            for _ in 0..9 { let (q, r) = gudivmod64(rem, 10); buf.push(b'0' + r as u8); rem = q; }
        } else {
            while rem > 0 { let (q, r) = gudivmod64(rem, 10); buf.push(b'0' + r as u8); rem = q; }
        }
    }
    let mut out = String::new();
    if x.sign < 0 { out.push('-'); }
    for &c in buf.iter().rev() { out.push(c as char); }
    out
}

// ---- small bignum helpers for the rational layer ----
fn big_one() -> Big { Big { sign: 1, d: vec![1] } }
fn big_from_small(v: u32) -> Big { if v == 0 { Big::zero() } else { Big { sign: 1, d: vec![v] } } }
fn big_is_one(x: &Big) -> bool { x.sign == 1 && x.d.len() == 1 && x.d[0] == 1 }

fn divmod_small(x: &Big, m: u32) -> (Big, u32) {
    let mut d = vec![0u32; x.d.len()];
    let mut rem: u64 = 0;
    for i in (0..x.d.len()).rev() {
        let cur = (rem << 32) | (x.d[i] as u64);
        let (q, r) = gudivmod64(cur, m as u64);
        d[i] = q as u32;
        rem = r;
    }
    let mut out = Big { sign: 1, d };
    out.norm();
    (out, rem as u32)
}

fn big_gcd(a: &Big, b: &Big) -> Big {
    let mut a = a.clone(); a.sign = if a.d.is_empty() { 0 } else { 1 };
    let mut b = b.clone(); b.sign = if b.d.is_empty() { 0 } else { 1 };
    while b.sign != 0 {
        let (_q, r) = udiv(&a, &b);
        a = b;
        b = r;
        b.sign = if b.d.is_empty() { 0 } else { 1 };
    }
    a.sign = if a.d.is_empty() { 0 } else { 1 };
    a
}

fn big_pow(base: &Big, e: &Big) -> Big {
    let mut result = big_one();
    if e.sign == 0 { return result; }
    let mut b = base.clone();
    let bits = e.d.len() * 32;
    let mut top: i64 = bits as i64 - 1;
    while top >= 0 && ((e.d[(top as usize) >> 5] >> ((top as usize) & 31)) & 1) == 0 { top -= 1; }
    let top = top as usize;
    for i in 0..=top {
        if (e.d[i >> 5] >> (i & 31)) & 1 == 1 { result = big_mul(&result, &b); }
        if i < top { b = big_mul(&b, &b); }
    }
    result
}

// ---- more bignum helpers + significant-digit rounding ----
use std::sync::OnceLock;

fn big_set_int(v: i64) -> Big {
    if v == 0 { return Big::zero(); }
    let neg = v < 0;
    let mut uv: u128 = if neg { (v as i128).unsigned_abs() } else { v as u128 };
    let mut d = vec![];
    while uv > 0 { d.push((uv & 0xffffffff) as u32); uv >>= 32; }
    Big { sign: if neg { -1 } else { 1 }, d }
}
fn big_cmp(a: &Big, b: &Big) -> i32 { big_sub(a, b).sign }
fn pow10(k: i64) -> Big { big_pow(&big_from_small(10), &big_set_int(k)) }

fn big_isqrt(n: &Big) -> Big {
    if n.sign == 0 { return Big::zero(); }
    let bits = n.d.len() * 32;
    let mut top: i64 = bits as i64 - 1;
    while top >= 0 && (n.d[(top as usize) >> 5] >> ((top as usize) & 31)) & 1 == 0 { top -= 1; }
    let half = (top / 2 + 1) as usize;
    let mut x = Big { sign: 1, d: vec![0u32; half / 32 + 1] };
    x.d[half / 32] = 1u32 << (half % 32);
    for _ in 0..2000 {
        let (q, _) = big_divmod(n, &x).unwrap();
        let s = big_add(&x, &q);
        let (nx, _) = divmod_small(&s, 2);
        if big_cmp(&nx, &x) >= 0 { break; }
        x = nx;
    }
    for _ in 0..4 {
        let sq = big_mul(&x, &x);
        if big_cmp(&sq, n) <= 0 { break; }
        x = big_sub(&x, &big_one());
    }
    x
}

fn cmp_10e_den_num(e: i64, den: &Big, num: &Big) -> i32 {
    if e >= 0 { big_cmp(&big_mul(&pow10(e), den), num) }
    else { big_cmp(den, &big_mul(&pow10(-e), num)) }
}

fn round_sig_rat(num_in: &Big, den_in: &Big, n: i64) -> (Big, Big) {
    if num_in.sign == 0 { return (Big::zero(), big_one()); }
    let sign = num_in.sign;
    let mut num = num_in.clone(); num.sign = 1;
    let mut den = den_in.clone(); den.sign = 1;
    let mut e = to_dec(&num).len() as i64 - to_dec(&den).len() as i64;
    loop {
        if cmp_10e_den_num(e, &den, &num) > 0 { e -= 1; continue; }
        if cmp_10e_den_num(e + 1, &den, &num) <= 0 { e += 1; continue; }
        break;
    }
    let scale = n - 1 - e;
    let (snum, sden) = if scale >= 0 { (big_mul(&num, &pow10(scale)), den.clone()) }
                       else { (num.clone(), big_mul(&den, &pow10(-scale))) };
    let (mut q, r) = big_divmod(&snum, &sden).unwrap();
    let two_r = big_add(&r, &r);
    let c = big_cmp(&two_r, &sden);
    if c > 0 || (c == 0 && !q.d.is_empty() && (q.d[0] & 1) == 1) { q = big_add(&q, &big_one()); }
    let ex = e - n + 1;
    let (mut rnum, rden) = if ex >= 0 { (big_mul(&q, &pow10(ex)), big_one()) } else { (q, pow10(-ex)) };
    rnum.sign = if rnum.d.is_empty() { 0 } else { sign };
    (rnum, rden)
}

// ---- fixed-point transcendental functions ----
const WP: i64 = 120;
const FUNC_SIG: i64 = 80;
const OUT_SIG: i64 = 50;
const PI_STR: &str = "3.14159265358979323846264338327950288419716939937510\
5820974944592307816406286208998628034825342117067982148086513282306647093844609550582231725359408128";

struct FP { scale: Big, pi: Big, two_pi: Big, ln2: Big, ln10: Big, e: Big }
static FPC: OnceLock<FP> = OnceLock::new();
fn fpc() -> &'static FP { FPC.get_or_init(build_fp) }

fn fp_from_rat(num: &Big, den: &Big, s: &Big) -> Big { big_divmod(&big_mul(num, s), den).unwrap().0 }
fn fp_mul(a: &Big, b: &Big, s: &Big) -> Big { big_divmod(&big_mul(a, b), s).unwrap().0 }
fn fp_div(a: &Big, b: &Big, s: &Big) -> Big { big_divmod(&big_mul(a, s), b).unwrap().0 }
fn fp_div_int(a: &Big, k: u32) -> Big {
    let mut aa = a.clone(); let neg = aa.sign < 0; aa.sign = if aa.d.is_empty() { 0 } else { 1 };
    let (mut q, _) = divmod_small(&aa, k); if neg { q.sign = -q.sign; } q
}
fn fp_parse_const(s: &str, sc: &Big) -> Big {
    let (ip, fp) = match s.find('.') { Some(i) => (&s[..i], &s[i + 1..]), None => (s, "") };
    let num = from_dec(&format!("{}{}", ip, fp));
    fp_from_rat(&num, &pow10(fp.len() as i64), sc)
}
fn fp_atanh(y: &Big, s: &Big) -> Big {
    let mut sum = y.clone(); let mut term = y.clone(); let y2 = fp_mul(y, y, s);
    let mut k: u32 = 1;
    while term.sign != 0 && k < 200000 {
        term = fp_mul(&term, &y2, s);
        sum = big_add(&sum, &fp_div_int(&term, 2 * k + 1));
        k += 1;
    }
    sum
}
fn fp_exp(x: &Big, s: &Big) -> Big {
    let mut r = x.clone(); let mut m = 0;
    loop {
        let mut a = r.clone(); a.sign = if a.d.is_empty() { 0 } else { 1 };
        if cmp_abs(&a, s) < 0 || m > 8192 { break; }
        r = fp_div_int(&r, 2); m += 1;
    }
    let mut sum = s.clone(); let mut term = s.clone(); let mut k: u32 = 1;
    while term.sign != 0 && k < 200000 {
        let t = fp_mul(&term, &r, s); term = fp_div_int(&t, k); sum = big_add(&sum, &term); k += 1;
    }
    for _ in 0..m { sum = fp_mul(&sum, &sum, s); }
    sum
}
fn fp_ln(x: &Big, s: &Big, ln2: &Big) -> Big {
    let mut xx = x.clone(); let mut e2: i64 = 0;
    let two_s = big_add(s, s);
    while cmp_abs(&xx, &two_s) >= 0 { xx = fp_div_int(&xx, 2); e2 += 1; }
    while cmp_abs(&xx, s) < 0 { xx = big_add(&xx, &xx); e2 -= 1; }
    let y = fp_div(&big_sub(&xx, s), &big_add(&xx, s), s);
    let at = fp_atanh(&y, s);
    big_add(&big_add(&at, &at), &big_mul(ln2, &big_set_int(e2)))
}
fn round_fp_to_int(v: &Big, s: &Big) -> Big {
    let (q, r) = big_divmod(v, s).unwrap();
    let mut ar = r.clone(); ar.sign = if ar.d.is_empty() { 0 } else { 1 };
    if big_cmp(&big_add(&ar, &ar), s) >= 0 { big_add(&q, &big_set_int(if v.sign < 0 { -1 } else { 1 })) } else { q }
}
fn fp_reduce(x: &Big, s: &Big, two_pi: &Big) -> Big {
    let k = round_fp_to_int(&fp_div(x, two_pi, s), s);
    big_sub(x, &big_mul(&k, two_pi))
}
fn fp_sin(x: &Big, s: &Big, two_pi: &Big) -> Big {
    let xr = fp_reduce(x, s, two_pi);
    let mut sum = xr.clone(); let mut term = xr.clone(); let x2 = fp_mul(&xr, &xr, s);
    let mut k: u32 = 1;
    while term.sign != 0 && k < 200000 {
        let t = fp_mul(&term, &x2, s);
        term = fp_div_int(&t, (2 * k) * (2 * k + 1));
        term.sign = -term.sign;
        sum = big_add(&sum, &term);
        k += 1;
    }
    sum
}
fn fp_cos(x: &Big, s: &Big, two_pi: &Big) -> Big {
    let xr = fp_reduce(x, s, two_pi);
    let mut sum = s.clone(); let mut term = s.clone(); let x2 = fp_mul(&xr, &xr, s);
    let mut k: u32 = 1;
    while term.sign != 0 && k < 200000 {
        let t = fp_mul(&term, &x2, s);
        term = fp_div_int(&t, (2 * k - 1) * (2 * k));
        term.sign = -term.sign;
        sum = big_add(&sum, &term);
        k += 1;
    }
    sum
}
fn fp_sqrt(x: &Big, s: &Big) -> Big { big_isqrt(&big_mul(x, s)) }
fn fp_atan(x: &Big, s: &Big) -> Big {
    let mut xx = x.clone(); let neg = xx.sign < 0; xx.sign = if xx.d.is_empty() { 0 } else { 1 };
    let thresh = fp_div_int(s, 10);
    let mut m = 0;
    while cmp_abs(&xx, &thresh) > 0 && m < 4096 {
        let x2 = fp_mul(&xx, &xx, s);
        let rt = fp_sqrt(&big_add(s, &x2), s);
        xx = fp_div(&xx, &big_add(s, &rt), s);
        m += 1;
    }
    let mut sum = xx.clone(); let mut term = xx.clone(); let x2 = fp_mul(&xx, &xx, s);
    let mut k: u32 = 1;
    while term.sign != 0 && k < 200000 {
        term = fp_mul(&term, &x2, s); term.sign = -term.sign;
        sum = big_add(&sum, &fp_div_int(&term, 2 * k + 1));
        k += 1;
    }
    for _ in 0..m { sum = big_add(&sum, &sum); }
    if neg { sum.sign = -sum.sign; }
    sum
}
fn fp_asin(x: &Big, s: &Big, pi: &Big) -> Option<Big> {
    let mut ax = x.clone(); ax.sign = if ax.d.is_empty() { 0 } else { 1 };
    let c = cmp_abs(&ax, s);
    if c > 0 { return None; }
    if c == 0 { let mut h = fp_div_int(pi, 2); if x.sign < 0 { h.sign = -h.sign; } return Some(h); }
    let d = big_sub(s, &fp_mul(x, x, s));
    Some(fp_atan(&fp_div(x, &fp_sqrt(&d, s), s), s))
}
fn fp_acos(x: &Big, s: &Big, pi: &Big) -> Option<Big> {
    let a = fp_asin(x, s, pi)?;
    Some(big_sub(&fp_div_int(pi, 2), &a))
}
fn fp_sinh(x: &Big, s: &Big) -> Big {
    let mut nx = x.clone(); nx.sign = -nx.sign;
    fp_div_int(&big_sub(&fp_exp(x, s), &fp_exp(&nx, s)), 2)
}
fn fp_cosh(x: &Big, s: &Big) -> Big {
    let mut nx = x.clone(); nx.sign = -nx.sign;
    fp_div_int(&big_add(&fp_exp(x, s), &fp_exp(&nx, s)), 2)
}
fn fp_tanh(x: &Big, s: &Big) -> Big {
    let mut nx = x.clone(); nx.sign = -nx.sign;
    let e1 = fp_exp(x, s); let e2 = fp_exp(&nx, s);
    fp_div(&big_sub(&e1, &e2), &big_add(&e1, &e2), s)
}

fn fp_asinh(x: &Big, s: &Big, ln2: &Big) -> Big {
    let rt = fp_sqrt(&big_add(&fp_mul(x, x, s), s), s);
    fp_ln(&big_add(x, &rt), s, ln2)
}
fn fp_acosh(x: &Big, s: &Big, ln2: &Big) -> Option<Big> {
    if big_cmp(x, s) < 0 { return None; }
    let rt = fp_sqrt(&big_sub(&fp_mul(x, x, s), s), s);
    Some(fp_ln(&big_add(x, &rt), s, ln2))
}
fn fp_atanh_u(x: &Big, s: &Big, ln2: &Big) -> Option<Big> {
    let mut ax = x.clone(); ax.sign = if ax.d.is_empty() { 0 } else { 1 };
    if cmp_abs(&ax, s) >= 0 { return None; }
    let q = fp_div(&big_add(s, x), &big_sub(s, x), s);
    Some(fp_div_int(&fp_ln(&q, s, ln2), 2))
}
fn fp_logb(x: &Big, b: &Big, s: &Big, ln2: &Big) -> Option<Big> {
    if x.sign <= 0 || b.sign <= 0 { return None; }
    let lb = fp_ln(b, s, ln2);
    if lb.sign == 0 { return None; }
    Some(fp_div(&fp_ln(x, s, ln2), &lb, s))
}
fn fp_hypot(x: &Big, y: &Big, s: &Big) -> Big {
    fp_sqrt(&big_add(&fp_mul(x, x, s), &fp_mul(y, y, s)), s)
}
fn fp_atan2(y: &Big, x: &Big, s: &Big, pi: &Big) -> Big {
    if x.sign > 0 { return fp_atan(&fp_div(y, x, s), s); }
    if x.sign < 0 {
        let a = fp_atan(&fp_div(y, x, s), s);
        return if y.sign >= 0 { big_add(&a, pi) } else { big_sub(&a, pi) };
    }
    if y.sign > 0 { return fp_div_int(pi, 2); }
    if y.sign < 0 { let mut h = fp_div_int(pi, 2); h.sign = -h.sign; return h; }
    Big::zero()
}

fn build_fp() -> FP {
    let scale = pow10(WP);
    let pi = fp_parse_const(PI_STR, &scale);
    let two_pi = big_add(&pi, &pi);
    let third = fp_div_int(&scale, 3);
    let at = fp_atanh(&third, &scale);
    let ln2 = big_add(&at, &at);
    let tenfp = fp_from_rat(&big_from_small(10), &big_one(), &scale);
    let ln10 = fp_ln(&tenfp, &scale, &ln2);
    let e = fp_exp(&scale, &scale);
    FP { scale, pi, two_pi, ln2, ln10, e }
}

// ---- exact rationals (with an inexact flag for function-tainted values) ----
#[derive(Clone)]
struct Rat { num: Big, den: Big, inexact: bool }

fn rat_norm(r: &mut Rat) {
    if r.num.sign == 0 { r.den = big_one(); return; }
    if r.den.sign < 0 { r.num.sign = -r.num.sign; r.den.sign = 1; }
    let g = big_gcd(&r.num, &r.den);
    if !big_is_one(&g) {
        r.num = big_divmod(&r.num, &g).unwrap().0;
        r.den = big_divmod(&r.den, &g).unwrap().0;
    }
}
fn rat_parse(s: &str) -> Rat {
    let mut r = if let Some(i) = s.find('/') {
        Rat { num: from_dec(&s[..i]), den: from_dec(&s[i + 1..]), inexact: false }
    } else {
        Rat { num: from_dec(s), den: big_one(), inexact: false }
    };
    rat_norm(&mut r);
    r
}
fn rat_add(a: &Rat, b: &Rat) -> Rat {
    let n = big_add(&big_mul(&a.num, &b.den), &big_mul(&b.num, &a.den));
    let mut r = Rat { num: n, den: big_mul(&a.den, &b.den), inexact: a.inexact || b.inexact };
    rat_norm(&mut r); r
}
fn rat_sub(a: &Rat, b: &Rat) -> Rat {
    let n = big_sub(&big_mul(&a.num, &b.den), &big_mul(&b.num, &a.den));
    let mut r = Rat { num: n, den: big_mul(&a.den, &b.den), inexact: a.inexact || b.inexact };
    rat_norm(&mut r); r
}
fn rat_mul(a: &Rat, b: &Rat) -> Rat {
    let mut r = Rat { num: big_mul(&a.num, &b.num), den: big_mul(&a.den, &b.den), inexact: a.inexact || b.inexact };
    rat_norm(&mut r); r
}
fn rat_div(a: &Rat, b: &Rat) -> Option<Rat> {
    if b.num.sign == 0 { return None; }
    let mut r = Rat { num: big_mul(&a.num, &b.den), den: big_mul(&a.den, &b.num), inexact: a.inexact || b.inexact };
    rat_norm(&mut r); Some(r)
}
fn rat_idiv(a: &Rat, b: &Rat) -> Option<Rat> {
    let dd = big_mul(&a.den, &b.num);
    if dd.sign == 0 { return None; }
    let (q, _) = big_divmod(&big_mul(&a.num, &b.den), &dd)?;
    Some(Rat { num: q, den: big_one(), inexact: a.inexact || b.inexact })
}
fn rat_mod(a: &Rat, b: &Rat) -> Option<Rat> {
    let t = rat_idiv(a, b)?;
    Some(rat_sub(a, &rat_mul(b, &t)))
}
// Ok(rat); Err(0)=div-by-zero; Err(-1)=non-integer exponent (handled by caller)
fn rat_pow(a: &Rat, b: &Rat) -> Result<Rat, i32> {
    if !big_is_one(&b.den) { return Err(-1); }
    let inx = a.inexact || b.inexact;
    if b.num.sign == 0 { return Ok(Rat { num: big_one(), den: big_one(), inexact: inx }); }
    let mut m = b.num.clone(); m.sign = 1;
    let pn = big_pow(&a.num, &m);
    let pd = big_pow(&a.den, &m);
    let mut r = if b.num.sign > 0 {
        Rat { num: pn, den: pd, inexact: inx }
    } else {
        if a.num.sign == 0 { return Err(0); }
        Rat { num: pd, den: pn, inexact: inx }
    };
    rat_norm(&mut r);
    Ok(r)
}
fn rat_from_fp(v: &Big) -> Rat {
    let (n, d) = round_sig_rat(v, &fpc().scale, FUNC_SIG);
    let mut r = Rat { num: n, den: d, inexact: true };
    rat_norm(&mut r);
    r
}
fn rat_to_string(r: &Rat) -> String {
    if r.num.sign == 0 { return "0".to_string(); }
    if big_is_one(&r.den) { return to_dec(&r.num); }
    let mut q = r.den.clone(); q.sign = 1;
    let mut a = 0u32;
    let mut b = 0u32;
    loop { let (qq, rem) = divmod_small(&q, 2); if rem == 0 { q = qq; a += 1; } else { break; } }
    loop { let (qq, rem) = divmod_small(&q, 5); if rem == 0 { q = qq; b += 1; } else { break; } }
    if big_is_one(&q) {
        let k = a.max(b);
        let tenk = big_pow(&big_from_small(10), &big_from_small(k));
        let (scale, _) = big_divmod(&tenk, &r.den).unwrap();
        let n = big_mul(&r.num, &scale);
        let neg = n.sign < 0;
        let mut absn = n.clone(); absn.sign = if absn.d.is_empty() { 0 } else { 1 };
        let digits = to_dec(&absn);
        let k = k as usize;
        let (intp, frac) = if digits.len() <= k {
            let mut f = String::new();
            for _ in 0..(k - digits.len()) { f.push('0'); }
            f.push_str(&digits);
            ("0".to_string(), f)
        } else {
            let il = digits.len() - k;
            (digits[..il].to_string(), digits[il..].to_string())
        };
        let frac = frac.trim_end_matches('0');
        let mut out = String::new();
        if neg { out.push('-'); }
        out.push_str(&intp);
        if !frac.is_empty() { out.push('.'); out.push_str(frac); }
        out
    } else {
        format!("{}/{}", to_dec(&r.num), to_dec(&r.den))
    }
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    let mut src = String::new();
    if args.len() > 1 {
        src = std::fs::read_to_string(&args[1]).expect("engine_rust: read bytecode");
    } else {
        std::io::stdin().read_to_string(&mut src).unwrap();
    }

    let mut stack: Vec<Rat> = Vec::new();
    for line in src.lines() {
        let line = line.trim();
        if line.is_empty() { continue; }
        let mut parts = line.split_whitespace();
        let op = parts.next().unwrap();
        match op {
            "PUSH" => stack.push(rat_parse(parts.next().unwrap())),
            "NEG" => { let n = stack.len(); stack[n - 1].num.sign = -stack[n - 1].num.sign; }
            "CONST" => {
                let c = fpc();
                let v = match parts.next() {
                    Some("pi") => &c.pi,
                    Some("e") => &c.e,
                    _ => { println!("ERR:UNKNOWN"); return; }
                };
                stack.push(rat_from_fp(v));
            }
            "FUNC" => {
                let name = parts.next().unwrap();
                let fargc: usize = parts.next().map(|s| s.parse().unwrap()).unwrap_or(1);
                if fargc == 2 {
                    let c = fpc();
                    let b = stack.pop().unwrap();
                    let a = stack.pop().unwrap();
                    let res = match name {
                        "gcd" | "lcm" => {
                            if !big_is_one(&a.den) || !big_is_one(&b.den) { println!("ERR:DOMAIN"); return; }
                            let g = big_gcd(&a.num, &b.num);
                            let num = if name == "gcd" { g }
                                else if a.num.sign == 0 || b.num.sign == 0 { Big::zero() }
                                else { let mut p = big_mul(&a.num, &b.num); p.sign = if p.d.is_empty() { 0 } else { 1 }; big_divmod(&p, &g).unwrap().0 };
                            Rat { num, den: big_one(), inexact: a.inexact || b.inexact }
                        }
                        "max" | "min" => {
                            let c2 = big_cmp(&big_mul(&a.num, &b.den), &big_mul(&b.num, &a.den));
                            let pick_a = if name == "max" { c2 >= 0 } else { c2 <= 0 };
                            if pick_a { a } else { b }
                        }
                        "comb" | "perm" => {
                            if !big_is_one(&a.den) || !big_is_one(&b.den) || a.num.sign < 0 || b.num.sign < 0 || big_cmp(&a.num, &big_set_int(20000)) > 0 { println!("ERR:DOMAIN"); return; }
                            let nn: i64 = if a.num.d.is_empty() { 0 } else { a.num.d[0] as i64 };
                            let rr: i64 = if b.num.d.is_empty() { 0 } else { b.num.d[0] as i64 };
                            let num = if rr > nn { Big::zero() } else {
                                let mut p = big_one();
                                for i in 0..rr { p = big_mul(&p, &big_set_int(nn - i)); }
                                if name == "perm" { p } else {
                                    let mut rf = big_one();
                                    for i in 2..=rr { rf = big_mul(&rf, &big_set_int(i)); }
                                    big_divmod(&p, &rf).unwrap().0
                                }
                            };
                            Rat { num, den: big_one(), inexact: a.inexact || b.inexact }
                        }
                        "log" => { match fp_logb(&fp_from_rat(&a.num, &a.den, &c.scale), &fp_from_rat(&b.num, &b.den, &c.scale), &c.scale, &c.ln2) { Some(v) => rat_from_fp(&v), None => { println!("ERR:DOMAIN"); return; } } }
                        "hypot" => rat_from_fp(&fp_hypot(&fp_from_rat(&a.num, &a.den, &c.scale), &fp_from_rat(&b.num, &b.den, &c.scale), &c.scale)),
                        "atan2" => rat_from_fp(&fp_atan2(&fp_from_rat(&a.num, &a.den, &c.scale), &fp_from_rat(&b.num, &b.den, &c.scale), &c.scale, &c.pi)),
                        _ => { println!("ERR:UNKNOWN"); return; }
                    };
                    stack.push(res);
                    continue;
                }
                let a = stack.pop().unwrap();
                if name == "abs" {
                    let mut r = a.clone();
                    r.num.sign = if r.num.d.is_empty() { 0 } else { 1 };
                    stack.push(r);
                    continue;
                }
                if name == "fact" {
                    if !big_is_one(&a.den) || a.num.sign < 0 || big_cmp(&a.num, &big_set_int(20000)) > 0 {
                        println!("ERR:DOMAIN"); return;
                    }
                    let n: i64 = if a.num.d.is_empty() { 0 } else { a.num.d[0] as i64 };
                    let mut f = big_one();
                    for i in 2..=n { f = big_mul(&f, &big_set_int(i)); }
                    stack.push(Rat { num: f, den: big_one(), inexact: a.inexact });
                    continue;
                }
                let c = fpc();
                let x = fp_from_rat(&a.num, &a.den, &c.scale);
                let y = match name {
                    "sqrt" => { if x.sign < 0 { println!("ERR:DOMAIN"); return; } big_isqrt(&big_mul(&x, &c.scale)) }
                    "exp" => fp_exp(&x, &c.scale),
                    "ln" => { if x.sign <= 0 { println!("ERR:DOMAIN"); return; } fp_ln(&x, &c.scale, &c.ln2) }
                    "log" | "log10" => { if x.sign <= 0 { println!("ERR:DOMAIN"); return; } fp_div(&fp_ln(&x, &c.scale, &c.ln2), &c.ln10, &c.scale) }
                    "sin" => fp_sin(&x, &c.scale, &c.two_pi),
                    "cos" => fp_cos(&x, &c.scale, &c.two_pi),
                    "tan" => { let s = fp_sin(&x, &c.scale, &c.two_pi); let co = fp_cos(&x, &c.scale, &c.two_pi); if co.sign == 0 { println!("ERR:DOMAIN"); return; } fp_div(&s, &co, &c.scale) }
                    "asin" => match fp_asin(&x, &c.scale, &c.pi) { Some(v) => v, None => { println!("ERR:DOMAIN"); return; } },
                    "acos" => match fp_acos(&x, &c.scale, &c.pi) { Some(v) => v, None => { println!("ERR:DOMAIN"); return; } },
                    "atan" => fp_atan(&x, &c.scale),
                    "sinh" => fp_sinh(&x, &c.scale),
                    "cosh" => fp_cosh(&x, &c.scale),
                    "tanh" => fp_tanh(&x, &c.scale),
                    "asinh" => fp_asinh(&x, &c.scale, &c.ln2),
                    "acosh" => match fp_acosh(&x, &c.scale, &c.ln2) { Some(v) => v, None => { println!("ERR:DOMAIN"); return; } },
                    "atanh" => match fp_atanh_u(&x, &c.scale, &c.ln2) { Some(v) => v, None => { println!("ERR:DOMAIN"); return; } },
                    "rad" => fp_mul(&x, &fp_div_int(&c.pi, 180), &c.scale),
                    "deg" => { let f180 = fp_from_rat(&big_set_int(180), &big_one(), &c.scale); fp_div(&fp_mul(&x, &f180, &c.scale), &c.pi, &c.scale) }
                    "cbrt" => {
                        if x.sign == 0 { Big::zero() }
                        else { let mut ax = x.clone(); let neg = ax.sign < 0; ax.sign = 1; let mut y = fp_exp(&fp_div_int(&fp_ln(&ax, &c.scale, &c.ln2), 3), &c.scale); if neg { y.sign = -y.sign; } y }
                    }
                    _ => { println!("ERR:UNKNOWN"); return; }
                };
                stack.push(rat_from_fp(&y));
            }
            _ => {
                let b = stack.pop().unwrap();
                let a = stack.pop().unwrap();
                let res = match op {
                    "ADD" => rat_add(&a, &b),
                    "SUB" => rat_sub(&a, &b),
                    "MUL" => rat_mul(&a, &b),
                    "DIV" => match rat_div(&a, &b) { Some(x) => x, None => { println!("ERR:DIVZERO"); return; } },
                    "IDIV" => match rat_idiv(&a, &b) { Some(x) => x, None => { println!("ERR:DIVZERO"); return; } },
                    "MOD" => match rat_mod(&a, &b) { Some(x) => x, None => { println!("ERR:DIVZERO"); return; } },
                    "POW" => match rat_pow(&a, &b) {
                        Ok(x) => x,
                        Err(0) => { println!("ERR:DIVZERO"); return; }
                        Err(_) => {                       // non-integer exponent: exp(b ln a)
                            let c = fpc();
                            if a.num.sign < 0 { println!("ERR:DOMAIN"); return; }
                            if a.num.sign == 0 {
                                if b.num.sign <= 0 { println!("ERR:DOMAIN"); return; }
                                Rat { num: Big::zero(), den: big_one(), inexact: true }
                            } else {
                                let bb = fp_from_rat(&a.num, &a.den, &c.scale);
                                let eb = fp_from_rat(&b.num, &b.den, &c.scale);
                                let y = fp_exp(&fp_mul(&eb, &fp_ln(&bb, &c.scale, &c.ln2), &c.scale), &c.scale);
                                rat_from_fp(&y)
                            }
                        }
                    },
                    _ => { stack.push(a); stack.push(b); continue; }
                };
                stack.push(res);
            }
        }
    }
    let top = stack.last().expect("engine_rust: empty stack");
    if top.inexact {
        let (n, d) = round_sig_rat(&top.num, &top.den, OUT_SIG);
        let mut r2 = Rat { num: n, den: d, inexact: true };
        rat_norm(&mut r2);
        println!("{}", rat_to_string(&r2));
    } else {
        println!("{}", rat_to_string(top));
    }
}
