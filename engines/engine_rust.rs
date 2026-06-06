// ===========================================================================
// ENGINE B of the cathedral: THE GATE-LEVEL VM, AGAIN  (language: Rust)
// ---------------------------------------------------------------------------
// A wholly independent reimplementation of the gate-level ALU. Same NAND-derived
// philosophy, different codebase and different author's hand — which is the
// entire point of consensus: independent engines that must nevertheless agree.
// Reads bytecode from argv[1] (or STDIN), prints top of stack as i64.
// ===========================================================================
use std::io::Read;

fn nand(a: u8, b: u8) -> u8 { if a & b != 0 { 0 } else { 1 } }
fn not(a: u8) -> u8 { nand(a, a) }
fn and(a: u8, b: u8) -> u8 { not(nand(a, b)) }
fn or(a: u8, b: u8) -> u8 { nand(not(a), not(b)) }
fn xor(a: u8, b: u8) -> u8 { let t = nand(a, b); nand(nand(a, t), nand(b, t)) }

fn full_adder(a: u8, b: u8, c: u8) -> (u8, u8) {
    let axb = xor(a, b);
    (xor(axb, c), or(and(axb, c), and(a, b)))
}

fn bit(x: u64, i: u32) -> u8 { ((x >> i) & 1) as u8 }

fn add_c(a: u64, b: u64, cin: u8) -> (u64, u8) {
    let mut r: u64 = 0;
    let mut c = cin;
    for i in 0..64 {
        let (s, co) = full_adder(bit(a, i), bit(b, i), c);
        if s == 1 { r |= 1u64 << i; }
        c = co;
    }
    (r, c)
}
fn add(a: u64, b: u64) -> u64 { add_c(a, b, 0).0 }
fn notw(a: u64) -> u64 {
    let mut r: u64 = 0;
    for i in 0..64 { if not(bit(a, i)) == 1 { r |= 1u64 << i; } }
    r
}
fn neg(a: u64) -> u64 { add(notw(a), 1) }
fn sub(a: u64, b: u64) -> u64 { add(a, neg(b)) }
fn uge(a: u64, b: u64) -> bool { add_c(a, notw(b), 1).1 == 1 }

fn mul(a: u64, b: u64) -> u64 {
    let mut r: u64 = 0;
    for i in 0..64 {
        if bit(b, i) == 1 { r = add(r, a << i); }
    }
    r
}

fn udivmod(num: u64, den: u64) -> (u64, u64) {
    let mut q: u64 = 0;
    let mut r: u64 = 0;
    for i in (0..64).rev() {
        r = (r << 1) | ((num >> i) & 1);
        if uge(r, den) {
            r = sub(r, den);
            q |= 1u64 << i;
        }
    }
    (q, r)
}

fn is_neg(a: u64) -> bool { bit(a, 63) == 1 }

fn sdivmod(a: u64, b: u64) -> Option<(u64, u64)> {
    if b == 0 { return None; }
    let na = is_neg(a);
    let nb = is_neg(b);
    let ua = if na { neg(a) } else { a };
    let ub = if nb { neg(b) } else { b };
    let (uq, ur) = udivmod(ua, ub);
    let q = if na != nb { neg(uq) } else { uq };
    let r = if na { neg(ur) } else { ur };
    Some((q, r))
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    let mut src = String::new();
    if args.len() > 1 {
        src = std::fs::read_to_string(&args[1]).expect("engine_rust: read bytecode");
    } else {
        std::io::stdin().read_to_string(&mut src).unwrap();
    }

    let mut stack: Vec<u64> = Vec::new();
    for line in src.lines() {
        let line = line.trim();
        if line.is_empty() { continue; }
        let mut parts = line.split_whitespace();
        let op = parts.next().unwrap();
        match op {
            "PUSH" => {
                let v: i64 = parts.next().unwrap().parse().unwrap();
                stack.push(v as u64);
            }
            "NEG" => {
                let x = stack.pop().unwrap();
                stack.push(neg(x));
            }
            _ => {
                let b = stack.pop().unwrap();
                let a = stack.pop().unwrap();
                let res = match op {
                    "ADD" => add(a, b),
                    "SUB" => sub(a, b),
                    "MUL" => mul(a, b),
                    "DIV" | "MOD" => {
                        match sdivmod(a, b) {
                            Some((q, r)) => if op == "DIV" { q } else { r },
                            None => { println!("ERR:DIVZERO"); return; }
                        }
                    }
                    _ => continue,
                };
                stack.push(res);
            }
        }
    }

    println!("{}", *stack.last().expect("engine_rust: empty stack") as i64);
}
