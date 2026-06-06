#!/usr/bin/env python3
# ============================================================================
# THE ORACLE OF TRUTH  (independent reference implementation)
# ----------------------------------------------------------------------------
# A standalone, deliberately simple evaluator used only by the test suite to
# decide what the correct answer IS. It mirrors the calculator's semantics:
# 64-bit signed two's-complement, wrapping overflow, division truncating toward
# zero. If `calc` ever disagrees with this oracle, HARD_RULES #1 is broken.
#
# Modes:
#   reference.py "<expr>"     -> print the correct 64-bit answer
#   reference.py --cases      -> emit curated  "expr\tanswer" lines
#   reference.py --gen N      -> emit N random "expr\tanswer" lines
# ============================================================================
import sys
import random

M = 1 << 64


def w(x):
    """Wrap to signed 64-bit two's complement."""
    x %= M
    return x - M if x >= (1 << 63) else x


def _trunc_div(a, b):
    q = abs(a) // abs(b)
    if (a < 0) != (b < 0):
        q = -q
    return q


def ev_add(a, b): return w(a + b)
def ev_sub(a, b): return w(a - b)
def ev_mul(a, b): return w(a * b)


def ev_div(a, b):
    if b == 0:
        raise ZeroDivisionError
    return w(_trunc_div(a, b))


def ev_mod(a, b):
    if b == 0:
        raise ZeroDivisionError
    return w(a - _trunc_div(a, b) * b)


# ---- a tiny independent recursive-descent evaluator ----
class P:
    def __init__(self, s):
        self.s = s
        self.i = 0

    def ws(self):
        while self.i < len(self.s) and self.s[self.i].isspace():
            self.i += 1

    def peek(self):
        self.ws()
        return self.s[self.i] if self.i < len(self.s) else ''

    def expr(self):
        v = self.term()
        while self.peek() in ('+', '-'):
            op = self.s[self.i]; self.i += 1
            r = self.term()
            v = ev_add(v, r) if op == '+' else ev_sub(v, r)
        return v

    def term(self):
        v = self.factor()
        while self.peek() in ('*', '/', '%'):
            op = self.s[self.i]; self.i += 1
            r = self.factor()
            v = ev_mul(v, r) if op == '*' else (ev_div(v, r) if op == '/' else ev_mod(v, r))
        return v

    def factor(self):
        c = self.peek()
        if c == '-':
            self.i += 1
            return w(-self.factor())
        if c == '+':
            self.i += 1
            return self.factor()
        if c == '(':
            self.i += 1
            v = self.expr()
            self.peek()
            self.i += 1  # skip ')'
            return v
        # integer
        self.ws()
        start = self.i
        while self.i < len(self.s) and self.s[self.i].isdigit():
            self.i += 1
        return w(int(self.s[start:self.i]))


def evaluate(expr):
    return P(expr).expr()


# ---- random expression generator (avoids division by zero) ----
def gen_expr(rng, depth=0):
    if depth >= 3 or rng.random() < 0.45:
        return str(rng.randint(0, 9999))
    op = rng.choice(['+', '-', '*', '/', '%'])
    left = gen_expr(rng, depth + 1)
    right = gen_expr(rng, depth + 1)
    if op in '/%':
        right = str(rng.randint(1, 999))  # never zero
    expr = f"({left} {op} {right})"
    if rng.random() < 0.2:
        expr = "-" + expr
    return expr


CURATED = [
    "1 + 2",
    "2 + 2",
    "10 - 3",
    "6 * 7",
    "100 / 7",
    "100 % 7",
    "-5 + 3",
    "-(4 + 6)",
    "2 * (3 + 4)",
    "(1 + 2) * (3 + 4)",
    "1000000 * 1000000",
    "2 - -2",
    "7 / 2",
    "-7 / 2",
    "7 % -3",
    "-7 % 3",
    "0 - 0",
    "((((5))))",
    "3 * 3 * 3 * 3",
    "999999999 + 1",
    "1 + 2 * 3 - 4 / 2",
    "(10 - 2) % (1 + 2)",
]


def main():
    if len(sys.argv) >= 2 and sys.argv[1] == "--cases":
        for e in CURATED:
            print(f"{e}\t{evaluate(e)}")
        return
    if len(sys.argv) >= 3 and sys.argv[1] == "--gen":
        n = int(sys.argv[2])
        rng = random.Random(1337)  # deterministic test corpus
        emitted = 0
        while emitted < n:
            e = gen_expr(rng)
            try:
                v = evaluate(e)
            except ZeroDivisionError:
                continue
            print(f"{e}\t{v}")
            emitted += 1
        return
    # single expression
    print(evaluate(" ".join(sys.argv[1:])))


if __name__ == "__main__":
    main()
