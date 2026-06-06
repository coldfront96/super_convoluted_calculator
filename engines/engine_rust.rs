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

fn main() {
    let args: Vec<String> = std::env::args().collect();
    let mut src = String::new();
    if args.len() > 1 {
        src = std::fs::read_to_string(&args[1]).expect("engine_rust: read bytecode");
    } else {
        std::io::stdin().read_to_string(&mut src).unwrap();
    }

    let mut stack: Vec<Big> = Vec::new();
    for line in src.lines() {
        let line = line.trim();
        if line.is_empty() { continue; }
        let mut parts = line.split_whitespace();
        let op = parts.next().unwrap();
        match op {
            "PUSH" => stack.push(from_dec(parts.next().unwrap())),
            "NEG" => { let n = stack.len(); stack[n - 1].sign = -stack[n - 1].sign; }
            _ => {
                let b = stack.pop().unwrap();
                let a = stack.pop().unwrap();
                let res = match op {
                    "ADD" => big_add(&a, &b),
                    "SUB" => big_sub(&a, &b),
                    "MUL" => big_mul(&a, &b),
                    "DIV" | "MOD" => match big_divmod(&a, &b) {
                        Some((q, r)) => if op == "DIV" { q } else { r },
                        None => { println!("ERR:DIVZERO"); return; }
                    },
                    _ => { stack.push(a); stack.push(b); continue; }
                };
                stack.push(res);
            }
        }
    }
    println!("{}", to_dec(stack.last().expect("engine_rust: empty stack")));
}
