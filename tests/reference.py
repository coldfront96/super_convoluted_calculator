#!/usr/bin/env python3
# ============================================================================
# THE ORACLE OF TRUTH  (independent reference implementation)
# ----------------------------------------------------------------------------
# Decides what the correct answer IS, using Python's exact Fraction. Mirrors the
# calculator's semantics: exact rationals; `/` exact division; `//` integer
# division truncating toward zero; `%` remainder with the dividend's sign; `^`
# integer exponentiation; decimals and scientific notation as exact rationals.
# Canonical output: integer | terminating decimal | reduced fraction p/q.
#
# Modes:
#   reference.py "<expr>"     -> print the correct answer
#   reference.py --cases      -> curated  "expr\tanswer" lines
#   reference.py --gen N      -> random rational "expr\tanswer" lines
#   reference.py --gen-big N  -> random BIG integer "expr\tanswer" lines
# ============================================================================
import sys
import re
import random
from fractions import Fraction

TOKEN = re.compile(
    r"\s*(//|[0-9]+\.?[0-9]*(?:[eE][+-]?[0-9]+)?|\.[0-9]+(?:[eE][+-]?[0-9]+)?|[-+*/%()^])"
)


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
            m = TOKEN.match(s, i)
            if not m:
                raise ValueError(f"bad token at {i!r}")
            self.toks.append(m.group(1))
            i = m.end()
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
            v = v + r if op == "+" else v - r
        return v

    def term(self):
        v = self.unary()
        while self.peek() in ("*", "/", "//", "%"):
            op = self.nxt()
            r = self.unary()
            if op == "*":
                v = v * r
            elif op == "/":
                if r == 0:
                    raise ZeroDivisionError
                v = v / r
            elif op == "//":
                v = trunc_div(v, r)
            else:
                v = v - r * trunc_div(v, r)
        return v

    def unary(self):
        t = self.peek()
        if t == "-":
            self.nxt()
            return -self.unary()
        if t == "+":
            self.nxt()
            return self.unary()
        return self.power()

    def power(self):
        base = self.atom()
        if self.peek() == "^":
            self.nxt()
            e = self.unary()
            if e.denominator != 1:
                raise ValueError("non-integer exponent")
            n = e.numerator
            if n == 0:
                return Fraction(1)
            if n < 0 and base == 0:
                raise ZeroDivisionError
            return base ** n
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
            return lit_to_fraction(self.nxt())
        raise ValueError(f"unexpected token {t}")


def evaluate(s):
    return Parser(s).expr()


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


CURATED = [
    "1 + 2", "2 + 2", "10 - 3", "6 * 7", "-5 + 3", "-(4 + 6)",
    "2 * (3 + 4)", "(1 + 2) * (3 + 4)", "1 + 2 * 3 - 4 // 2",
    # exact division and fractions
    "1 / 2", "1 / 3", "7 / 2", "-7 / 2", "1 / 3 + 1 / 6", "2 / 4",
    "3.14 * 2", "0.1 + 0.2", "0.5 * 0.5", "10 / 4", "1 / 3 * 3",
    "(1 / 3 + 1 / 3 + 1 / 3)", ".25 + .75", "100 / 7", "22 / 7",
    # integer division / modulo
    "7 // 2", "-7 // 2", "100 // 7", "100 % 7", "7 % -3", "-7 % 3",
    # powers
    "2 ^ 10", "2 ^ 0", "2 ^ -1", "(1 / 2) ^ 3", "(-3) ^ 3", "-3 ^ 2",
    "10 ^ 20", "2 ^ 3 ^ 2", "(2 / 3) ^ -2",
    # scientific + big
    "1e3 + 1", "1.5e-2 * 4", "1000000000000000000 * 1000000000000000000",
    "123456789012345678901234567890 + 1", "0",
]


def main():
    if len(sys.argv) >= 2 and sys.argv[1] == "--cases":
        for e in CURATED:
            print(f"{e}\t{canon(evaluate(e))}")
        return
    if len(sys.argv) >= 3 and sys.argv[1] in ("--gen", "--gen-big"):
        big = sys.argv[1] == "--gen-big"
        n = int(sys.argv[2])
        rng = random.Random(4242 if big else 1337)
        out = 0
        while out < n:
            e = gen_big(rng) if big else gen(rng)
            try:
                v = canon(evaluate(e))
            except (ZeroDivisionError, ValueError):
                continue
            print(f"{e}\t{v}")
            out += 1
        return
    print(canon(evaluate(" ".join(sys.argv[1:]))))


if __name__ == "__main__":
    main()
