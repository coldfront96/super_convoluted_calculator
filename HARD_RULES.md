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
Real expression in, real answer out. Supports `+ - * / %`, parentheses, unary
minus, and integer precedence. Arbitrary-precision signed integer semantics
(division truncates toward zero; remainder takes the dividend's sign) across the
four bignum engines. The Bash engine is a bounded 64-bit gate engine that the
Byzantine quorum outvotes whenever a result overflows 64 bits.

## Rule 7 — Maximize convolution within Rules 1–6
Subject to everything above, make it as unnecessarily complicated as humanly
defensible. More languages. More layers. More ceremony. This is the *objective*.
