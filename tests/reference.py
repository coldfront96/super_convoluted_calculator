#!/usr/bin/env python3
# ============================================================================
# THE ORACLE OF TRUTH  (independent reference implementation)
# ----------------------------------------------------------------------------
# A standalone, deliberately simple evaluator used only by the test suite to
# decide what the correct answer IS. It mirrors the calculator's semantics:
# arbitrary-precision signed integers, division truncating toward zero, and a
# remainder that takes the dividend's sign. If `calc` ever disagrees with this
# oracle, HARD_RULES #1 is broken.
#
# Modes:
#   reference.py "<expr>"     -> print the correct answer
#   reference.py --cases      -> emit curated  "expr\tanswer" lines
#   reference.py --gen N      -> emit N random "expr\tanswer" lines
#   reference.py --gen-big N  -> emit N random BIG "expr\tanswer" lines
# ============================================================================
import sys
import random


def _trunc_div(a, b):
    q = abs(a) // abs(b)
    if (a < 0) != (b < 0):
        q = -q
    return q


def ev_add(a, b): return a + b
def ev_sub(a, b): return a - b
def ev_mul(a, b): return a * b


def ev_div(a, b):
    if b == 0:
        raise ZeroDivisionError
    return _trunc_div(a, b)


def ev_mod(a, b):
    if b == 0:
        raise ZeroDivisionError
    return a - _trunc_div(a, b) * b


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
            return -self.factor()
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
        return int(self.s[start:self.i])


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


def gen_big_expr(rng, depth=0):
    if depth >= 3 or rng.random() < 0.45:
        # numbers that comfortably exceed 64 bits
        digits = rng.randint(15, 40)
        n = rng.randint(10 ** (digits - 1), 10 ** digits)
        return ("-" if rng.random() < 0.3 else "") + str(n)
    op = rng.choice(['+', '-', '*', '/', '%'])
    left = gen_big_expr(rng, depth + 1)
    right = gen_big_expr(rng, depth + 1)
    if op in '/%':
        d = rng.randint(10 ** 14, 10 ** 30)
        right = ("-" if rng.random() < 0.3 else "") + str(d)
    return f"({left} {op} {right})"


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
    # ---- arbitrary precision: results far beyond 64 bits ----
    "1000000000000000000 * 1000000000000000000",
    "2 * 2 * 2 * 2 * 2 * 2 * 2 * 2 * 2 * 2 * 2 * 2 * 2 * 2 * 2 * 2",
    "123456789012345678901234567890 + 987654321098765432109876543210",
    "99999999999999999999 * 99999999999999999999",
    "(10000000000000000000000000000000000000000 / 7)",
    "(10000000000000000000000000000000000000000 % 7)",
    "-123456789012345678901234567890 * 2",
    "1000000000000000000000000000000 - 1",
]


def main():
    if len(sys.argv) >= 2 and sys.argv[1] == "--cases":
        for e in CURATED:
            print(f"{e}\t{evaluate(e)}")
        return
    if len(sys.argv) >= 3 and sys.argv[1] in ("--gen", "--gen-big"):
        big = sys.argv[1] == "--gen-big"
        n = int(sys.argv[2])
        rng = random.Random(1337 if not big else 4242)  # deterministic corpus
        emitted = 0
        while emitted < n:
            e = gen_big_expr(rng) if big else gen_expr(rng)
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
