#!/usr/bin/env python3
# ============================================================================
# THE ORACLE OF TRUTH  (independent reference implementation)
# ----------------------------------------------------------------------------
# Decides the correct answer. Numbers are exact Fractions tagged exact/inexact.
# Pure rational results print exactly (integer | terminating decimal | p/q).
# Anything touched by a transcendental function (sqrt, trig, ln, exp, ...) or a
# non-integer power becomes inexact and prints rounded to 50 significant digits.
# The transcendental spec lives in funcs.py (stdlib `decimal`, no external deps).
#
# Modes:
#   reference.py "<expr>"     -> print the correct answer
#   reference.py --cases      -> curated  "expr\tanswer" lines
#   reference.py --gen N      -> random rational "expr\tanswer" lines
#   reference.py --gen-big N  -> random BIG integer "expr\tanswer" lines
#   reference.py --gen-fn N   -> random function "expr\tanswer" lines
# ============================================================================
import sys
import os
import re
import random
from fractions import Fraction

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from funcs import (call_func, const, pow_inexact, round_sig_fraction,
                   DomainError, OUT_SIG)

FUNCS = {"sqrt", "cbrt", "exp", "ln", "log", "log10", "sin", "cos", "tan",
         "asin", "acos", "atan", "sinh", "cosh", "tanh", "rad", "deg",
         "fact", "abs"}
CONSTS = {"pi", "e"}

TOKEN = re.compile(
    r"\s*(//|[A-Za-z][A-Za-z0-9]*"
    r"|[0-9]+\.?[0-9]*(?:[eE][+-]?[0-9]+)?|\.[0-9]+(?:[eE][+-]?[0-9]+)?"
    r"|[-+*/%()^])"
)


# ---- value model: (Fraction, inexact) ----
class V:
    __slots__ = ("f", "x")

    def __init__(self, f, x=False):
        self.f = f
        self.x = x


def lit_to_fraction(s):
    m = s.lower()
    exp = 0
    if "e" in m:
        base, e = m.split("e", 1)
        exp = int(e)
        m = base
    if "." in m:
        intp, frac = m.split(".", 1)
    else:
        intp, frac = m, ""
    intp = intp or "0"
    num = int(intp + frac) if (intp + frac) else 0
    den = 10 ** len(frac)
    if exp >= 0:
        num *= 10 ** exp
    else:
        den *= 10 ** (-exp)
    return Fraction(num, den)


def trunc_div(a, b):
    if b == 0:
        raise ZeroDivisionError
    q = abs(a.numerator * b.denominator) // abs(a.denominator * b.numerator)
    if (a < 0) != (b < 0):
        q = -q
    return Fraction(q)


class Parser:
    def __init__(self, s):
        self.toks = []
        i = 0
        while i < len(s):
            if s[i].isspace():
                i += 1
                continue
            mt = TOKEN.match(s, i)
            if not mt:
                raise ValueError(f"bad token at {i}")
            self.toks.append(mt.group(1))
            i = mt.end()
        self.p = 0

    def peek(self):
        return self.toks[self.p] if self.p < len(self.toks) else None

    def nxt(self):
        t = self.toks[self.p]
        self.p += 1
        return t

    def expr(self):
        v = self.term()
        while self.peek() in ("+", "-"):
            op = self.nxt()
            r = self.term()
            v = V(v.f + r.f if op == "+" else v.f - r.f, v.x or r.x)
        return v

    def term(self):
        v = self.unary()
        while self.peek() in ("*", "/", "//", "%"):
            op = self.nxt()
            r = self.unary()
            if op == "*":
                v = V(v.f * r.f, v.x or r.x)
            elif op == "/":
                if r.f == 0:
                    raise ZeroDivisionError
                v = V(v.f / r.f, v.x or r.x)
            elif op == "//":
                v = V(trunc_div(v.f, r.f), v.x or r.x)
            else:
                v = V(v.f - r.f * trunc_div(v.f, r.f), v.x or r.x)
        return v

    def unary(self):
        t = self.peek()
        if t == "-":
            self.nxt()
            u = self.unary()
            return V(-u.f, u.x)
        if t == "+":
            self.nxt()
            return self.unary()
        return self.power()

    def power(self):
        base = self.atom()
        if self.peek() == "^":
            self.nxt()
            e = self.unary()
            if e.f.denominator == 1:           # integer exponent -> exact power
                n = e.f.numerator
                if n == 0:
                    return V(Fraction(1), base.x or e.x)
                if n < 0 and base.f == 0:
                    raise ZeroDivisionError
                return V(base.f ** n, base.x or e.x)
            return V(pow_inexact(base.f, e.f), True)   # non-integer exponent
        return base

    def atom(self):
        t = self.peek()
        if t == "(":
            self.nxt()
            v = self.expr()
            if self.peek() != ")":
                raise ValueError("missing )")
            self.nxt()
            return v
        if t is not None and (t[0].isdigit() or t[0] == "."):
            return V(lit_to_fraction(self.nxt()), False)
        if t is not None and t[0].isalpha():
            name = self.nxt()
            if self.peek() == "(":
                self.nxt()
                arg = self.expr()
                if self.peek() != ")":
                    raise ValueError("missing )")
                self.nxt()
                if name == "abs":
                    return V(abs(arg.f), arg.x)   # abs preserves exactness
                if name == "fact":                # exact factorial of a whole number
                    if arg.f.denominator != 1 or arg.f < 0 or arg.f.numerator > 20000:
                        raise DomainError
                    r = 1
                    for i in range(2, arg.f.numerator + 1):
                        r *= i
                    return V(Fraction(r), arg.x)
                if name not in FUNCS:
                    raise ValueError(f"unknown function {name}")
                return V(call_func("log10" if name == "log" else name, arg.f), True)
            if name in CONSTS:
                return V(const(name), True)
            raise ValueError(f"unknown name {name}")
        raise ValueError(f"unexpected token {t}")


def canon(fr: Fraction) -> str:
    p, q = fr.numerator, fr.denominator
    if q == 1:
        return str(p)
    qq, a, b = q, 0, 0
    while qq % 2 == 0:
        qq //= 2
        a += 1
    while qq % 5 == 0:
        qq //= 5
        b += 1
    if qq == 1:
        k = max(a, b)
        scale = 10 ** k // q
        N = p * scale
        s = str(abs(N))
        if len(s) <= k:
            intp, frac = "0", "0" * (k - len(s)) + s
        else:
            intp, frac = s[: len(s) - k], s[len(s) - k:]
        frac = frac.rstrip("0")
        return ("-" if N < 0 else "") + intp + ("." + frac if frac else "")
    return f"{p}/{q}"


def render(v: V) -> str:
    if v.x:
        return canon(round_sig_fraction(v.f, OUT_SIG))
    return canon(v.f)


def evaluate(s):
    return render(Parser(s).expr())


# ---- generators ----
def gen_num(rng):
    r = rng.random()
    if r < 0.45:
        return str(rng.randint(-50, 50))
    if r < 0.8:
        ip = rng.randint(0, 60)
        fl = rng.randint(1, 3)
        frac = "".join(rng.choice("0123456789") for _ in range(fl))
        s = f"{ip}.{frac}"
        return ("-" + s) if rng.random() < 0.3 else s
    return str(rng.randint(1, 9)) + "e" + str(rng.randint(0, 4))


def gen(rng, depth=0):
    if depth >= 3 or rng.random() < 0.5:
        return gen_num(rng)
    op = rng.choice(["+", "-", "*", "/", "//", "%", "^"])
    if op == "^":
        return f"({gen(rng, depth + 1)})^{rng.randint(0, 6)}"
    return f"({gen(rng, depth + 1)}){op}({gen(rng, depth + 1)})"


def gen_big(rng, depth=0):
    if depth >= 3 or rng.random() < 0.5:
        d = rng.randint(12, 36)
        n = rng.randint(10 ** (d - 1), 10 ** d)
        return ("-" if rng.random() < 0.3 else "") + str(n)
    op = rng.choice(["+", "-", "*", "//", "%", "^"])
    if op == "^":
        return f"({gen_big(rng, depth + 1)})^{rng.randint(0, 4)}"
    return f"({gen_big(rng, depth + 1)}){op}({gen_big(rng, depth + 1)})"


# function args kept in safe, non-pathological ranges
def gen_fn_arg(rng):
    r = rng.random()
    if r < 0.5:
        return f"{rng.randint(1, 20)}.{rng.randint(0, 999)}"
    if r < 0.8:
        return str(rng.randint(1, 50))
    return f"0.{rng.randint(1, 999)}"


def gen_fn(rng, depth=0):
    if depth >= 2 or rng.random() < 0.5:
        kind = rng.random()
        if kind < 0.4:
            fn = rng.choice(["sqrt", "exp", "ln", "log", "sin", "cos", "tan", "cbrt",
                             "atan", "sinh", "cosh", "tanh", "rad", "deg"])
            return f"{fn}({gen_fn_arg(rng)})"
        if kind < 0.5:
            fn = rng.choice(["asin", "acos"])
            sign = "-" if rng.random() < 0.3 else ""
            return f"{fn}({sign}0.{rng.randint(0, 999):03d})"
        if kind < 0.58:
            return f"fact({rng.randint(0, 12)})"
        if kind < 0.68:
            return rng.choice(["pi", "e"])
        return gen_fn_arg(rng)
    op = rng.choice(["+", "-", "*", "/"])
    left = gen_fn(rng, depth + 1)
    right = gen_fn(rng, depth + 1)
    if op == "/":
        return f"({left}){op}({gen_fn_arg(rng)})"
    return f"({left}){op}({right})"


CURATED = [
    "1 + 2", "6 * 7", "1 / 2", "1 / 3", "7 / 2", "1 / 3 + 1 / 6", "0.1 + 0.2",
    "3.14 * 2", "7 // 2", "100 % 7", "2 ^ 10", "2 ^ 0", "2 ^ -1", "(1 / 2) ^ 3",
    "-3 ^ 2", "2 ^ 3 ^ 2", "1e3 + 1", "1000000000000000000 * 1000000000000000000",
    # ---- function layer ----
    "sqrt(2)", "sqrt(4)", "sqrt(2) * sqrt(2)", "sqrt(9) + 1", "cbrt(27)",
    "exp(0)", "exp(1)", "ln(e)", "ln(1)", "log(1000)", "log10(100)",
    "sin(0)", "cos(0)", "tan(0)", "sin(pi / 2)", "cos(pi)", "tan(pi / 4)",
    "pi", "e", "2 * pi", "pi * 2 + 1", "abs(-5)", "abs(-1 / 3)", "abs(3.5)",
    "2 ^ (1 / 2)", "4 ^ 0.5", "8 ^ (1 / 3)", "exp(ln(5))", "sqrt(2) + 1 / 3",
    "sin(1) ^ 2 + cos(1) ^ 2", "ln(exp(3))", "10 ^ 0.5",
    # ---- Core+ functions ----
    "atan(1) * 4", "asin(1)", "asin(0.5)", "acos(0)", "acos(-1)", "atan(0)",
    "sinh(0)", "cosh(0)", "tanh(0)", "cosh(1)", "tanh(2)",
    "fact(0)", "fact(1)", "fact(5)", "fact(10)", "fact(20)",
    "rad(180)", "deg(pi)", "sin(rad(30))", "cos(rad(60))", "deg(atan(1))",
]


def main():
    if len(sys.argv) >= 2 and sys.argv[1] == "--cases":
        for e in CURATED:
            print(f"{e}\t{evaluate(e)}")
        return
    if len(sys.argv) >= 3 and sys.argv[1] in ("--gen", "--gen-big", "--gen-fn"):
        mode = sys.argv[1]
        n = int(sys.argv[2])
        rng = random.Random({"--gen": 1337, "--gen-big": 4242, "--gen-fn": 271828}[mode])
        gfn = {"--gen": gen, "--gen-big": gen_big, "--gen-fn": gen_fn}[mode]
        out = 0
        while out < n:
            e = gfn(rng)
            try:
                v = evaluate(e)
            except (ZeroDivisionError, ValueError, DomainError):
                continue
            print(f"{e}\t{v}")
            out += 1
        return
    print(evaluate(" ".join(sys.argv[1:])))


if __name__ == "__main__":
    main()
