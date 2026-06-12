# THE HARD RULES (a.k.a. The Constitution)

This project is an intentionally, gloriously, unnecessarily over-engineered
calculator. The whole joke only works if it is *also correct*. These rules are
law. Breaking them is the only way to actually fail.

## Rule 1 — Correctness is sacred
The output must always equal the true mathematical answer. `1 + 2` is `3`.
Always. No exceptions, no "close enough". Enforced by the test suite in `tests/`.

## Rule 2 — It must actually run
No crashes on valid input. `make all` must build every engine. `tests/run_tests.sh`
must pass. If CI isn't green, the cathedral has collapsed and we have failed.

## Rule 3 — No dead complexity
Every absurd layer must be on the *real* execution path. No decorative code that
gets bypassed. If it is silly, it must still do load-bearing work. The lexer
really lexes, the gates really switch, the four engines really vote.

## Rule 4 — No cheating on the math itself
The actual arithmetic goes through the gate-level ALU (NAND → adder → ...).
Built-in operators (`+`, `*`, ...) are allowed ONLY for plumbing: loop counters,
array indexing, bit shifting. They may not compute the user's answer.
(Exception: the Java engine is deliberately the "sane oracle" — its whole comedic
job is to use boring `long` math so it can disagree with the insane engines. It
never does.)

## Rule 5 — Determinism
Same input → same correct output, every time. No randomness in the answer.

## Rule 6 — It stays a calculator
Real expression in, real answer out. Supports `+ - * / // % ^`, parentheses,
unary minus, decimal/scientific literals, one-argument functions
`sqrt cbrt exp ln log sin cos tan asin acos atan sinh cosh tanh asinh acosh atanh
rad deg fact abs`, two-argument functions `gcd lcm comb perm max min log hypot
atan2`, and constants `pi`/`e`, with correct precedence. Rational results are **exact arbitrary-precision** (p/q of bignums):
`/` exact division, `//` truncates toward zero, `%` takes the dividend's sign,
integer `^` exact. Irrational results (functions, non-integer powers) are
computed at high precision, correctly rounded to 80 significant digits internally
and displayed to 50; trig is in radians. Output is canonical: integer,
terminating decimal, or reduced fraction (exact), or a 50-sig-digit decimal
(inexact). The four rational engines (C/Rust/Go/Java) carry all of this; the Bash
engine is a bounded 64-bit integer gate engine that the Byzantine quorum outvotes
whenever a result overflows 64 bits, isn't a whole number, or needs a function.

## Rule 8 — Exactness is tracked, never faked
A value is "inexact" only once a transcendental function or non-integer power has
touched it. Such taint propagates through arithmetic. A result that can be exact
(pure rational) must be printed exactly — `1/3 + 1/6` is `0.5`, not `0.4999…`.

## Rule 7 — Maximize convolution within Rules 1–6
Subject to everything above, make it as unnecessarily complicated as humanly
defensible. More languages. More layers. More ceremony. This is the *objective*.
