# 🏛️ The Super Convoluted Calculator

[![ci](https://github.com/coldfront96/super_convoluted_calculator/actions/workflows/ci.yml/badge.svg)](https://github.com/coldfront96/super_convoluted_calculator/actions/workflows/ci.yml)

A calculator that computes `1 + 2 = 3` — **correctly, every time** — by way of an
eleven-language pipeline, a virtual machine whose ALU is built from individual
logic gates, five independent execution engines, and a Byzantine quorum vote.

It is deliberately, proudly, *unnecessarily* complicated. The only sacred rule is
that the answer must always be right. See [`HARD_RULES.md`](HARD_RULES.md) for the
constitution that governs this madness.

```
$ ./calc "1 + 2"
SUPER CONVOLUTED CALCULATOR — evaluating: 1 + 2
  · stage 1/6  [perl]   lexing into CSV tokens
  · stage 2/6  [python] parsing CSV into an AST
  · stage 3/6  [ruby]   compiling AST to base64 bytecode
  · stage 4/6  [c/rust/go/java] running four independent engines
  ·           C=3  Rust=3  Go=3  Java=3
  · stage 5/6  [awk]    tallying Byzantine consensus
  · stage 6/6  [node]   rendering & cross-verifying representations
CONSENSUS REACHED: 3
3
```

The ceremony goes to **stderr**; the headline answer goes to **stdout** (so you
can pipe it). Add `--report` for the full multi-representation render.

## The pipeline

| Stage | Language | Job | Output format |
|------:|----------|-----|---------------|
| 1 | **Perl** | lexer (anchored `\G` state machine) | CSV |
| 2 | **Python** | recursive-descent parser | JSON AST |
| 3 | **Ruby** | compiler → stack bytecode | base64 |
| — | **base64** | transport decode | bytecode |
| 4 | **C / Rust / Go / Java / Bash** | five independent VMs | one integer each |
| 5 | **awk** | Byzantine quorum (strict majority or panic) | the verdict |
| 6 | **Node** | render decimal/hex/binary/Roman/English + cross-verify | JSON |
| — | **jq** | extract the headline answer | the number |

That's **bash, Perl, Python, Ruby, C, Rust, Go, Java, Node/JS, awk** on the
critical path, with `base64`, `jq`, and `make` as accomplices.

## Byzantine quorum (why five engines?)

So they can **vote**, and tolerate a traitor. Each engine runs the same bytecode
independently. The awk consensus stage accepts a value only if it holds a *strict
majority* — so a single lying engine cannot change the answer, it can only get
itself outvoted and named in the log. You can prove this on the real path:

```sh
$ CALC_TRAITOR=99999999 ./calc "1 + 2"
  ...
  consensus: Byzantine dissent detected; majority prevails (5/6)
  consensus: traitor vote ignored -> 99999999
3
```

Four of the five engines (C, Rust, Go, Bash) build all arithmetic from logic
gates. The fifth (**Java**) is the "sane oracle" using boring `long` math purely
so it *could* disagree with the lunatics. It never does.

## The crown jewel: arithmetic from NAND gates

Four of the five engines (C, Rust, Go, Bash) compute nothing with the native `+`
or `*`. Instead they build everything from logic gates — the C/Rust/Go engines
from a single **NAND** primitive:

```
NAND ─► NOT, AND, OR, XOR ─► full adder ─► 64-bit ripple-carry adder
     ─► two's-complement negation ─► subtraction
     ─► shift-and-add (or Russian-peasant) multiplication
     ─► restoring long division
```

Native operators appear only as plumbing: loop counters, array indexing, bit
shifts. The fourth engine (**Java**) is the "sane oracle" — it uses boring
`long` math purely so it can disagree with the lunatics. It never does.

## Why four engines?

So they can **vote**. Each engine runs the same bytecode independently. The awk
consensus stage demands unanimity; if even one dissents, the calculator refuses
to answer rather than risk being wrong (Hard Rule #1). This has never happened,
which is the whole joke.

## Semantics

**Exact arbitrary-precision rationals.** Every number is a fraction `p/q` of two
bignums, so `0.1 + 0.2` is exactly `0.3` and `1/3 * 3` is exactly `1`. Supports:

| op | meaning | example |
|----|---------|---------|
| `+ - *` | exact | `0.1 + 0.2` → `0.3` |
| `/` | exact division | `1 / 3` → `1/3`, `7 / 2` → `3.5` |
| `//` | integer division (toward zero) | `7 // 2` → `3` |
| `%` | remainder (sign of dividend) | `100 % 7` → `2` |
| `^` | integer exponent (right-assoc) | `2 ^ 3 ^ 2` → `512`, `(1/2)^3` → `0.125` |

Decimal (`3.14`) and scientific (`1.5e-2`) literals are read exactly. Output is
canonical: an integer, a terminating decimal, or a reduced fraction `p/q`.
Division by zero exits 3; non-integer exponents (e.g. `2^(1/2)`) await the
function layer and exit 5.

The four rational engines (C, Rust, Go, Java) build this on their gate-level
bignum: `NAND → 64-bit ALU → bignum → rational`. The Bash engine stays a bounded
64-bit *integer* gate engine — it agrees on whole-number results within 64 bits
and is outvoted by the quorum on fractions or overflow.

The four big engines (C, Rust, Go, Java) are genuinely unbounded. Each gate
engine builds bignum arithmetic as a third layer **on top of** its 64-bit gate
ALU: `NAND → 64-bit ALU → base-2³² bignum`. Limb additions go through the gate
adder; limb products through the gate multiplier; division is binary long
division. Java uses `BigInteger` (the sane oracle).

The Bash engine remains a **bounded 64-bit** gate engine. When a result fits in
64 bits, all five agree. When it overflows, Bash produces a wrapped (wrong)
value, becomes a lone dissenter, and the four bignum engines outvote it 4-to-1:

```sh
$ ./calc "1000000000000000000 * 1000000000000000000"
  ...
  C=10^36  Rust=10^36  Go=10^36  Java=10^36  Bash=-5527149226598858752
  consensus: Byzantine dissent detected; majority prevails (4/5)
1000000000000000000000000000000000000
```

This is the point: the Byzantine quorum from Feature 1 is now genuinely
load-bearing — it exists precisely to tolerate the bounded engine's overflow.

## Two modes: direct pipe vs. microservices

By default the stages run as local subprocesses (fast). With `--service`, **every
stage becomes a real HTTP microservice** and the orchestrator calls them over the
network — because invoking a localhost function the hard way deserves
production-grade resilience.

```sh
./calc --service "123456789 * 987654321"
```

- **Native HTTP servers** in each stage's own language: Perl lexer (:7001),
  Python parser (:7002), Ruby compiler (:7003), Node renderer (:7030).
- A **generic Go sidecar** (:7011–7020) hosts the five engines + the awk
  consensus stage behind HTTP.
- The orchestrator talks to them with **exponential-backoff retries** and a
  **per-service circuit breaker** (`pipeline/http_client.sh`).
- A **YAML config nobody needs** (`services/services.yaml`) drives a launcher:

```sh
services/serviced.sh start     # boot the 10-service mesh
services/serviced.sh status    # health of every service
services/serviced.sh stop
./calc --service "2 ^ ... "    # (calc auto-starts the mesh if it's down)
```

## Usage

```sh
make all                       # build the engines + the HTTP sidecar
./calc "2 * (3 + 4)"           # -> 14
./calc "1/3 + 1/6"             # -> 0.5
./calc "0.1 + 0.2"             # -> 0.3   (exact, not 0.30000000000000004)
./calc "2 ^ 100"               # -> 1267650600228229401496703205376
./calc --report "1/3"          # full JSON render (value/exact/approx/roman/words)
./calc --service "6 * 7"       # same answer, but over an HTTP microservice mesh
./calc "1 / 0"; echo $?        # -> error on stderr, exit code 3
bash tests/run_tests.sh        # 425 checks vs. an independent Python oracle
```

## Requirements

`bash perl python3 ruby gcc rustc go javac/java node awk jq base64 make` — all of
which are, against all odds, things this calculator genuinely needs to add two
numbers together.

## Layout

```
calc                    # the bash orchestrator (entrypoint)
HARD_RULES.md           # the constitution
Makefile                # builds the four engines
pipeline/
  01_lexer.pl           # Perl   — lexer
  02_parser.py          # Python — parser
  03_compiler.rb        # Ruby   — compiler
  consensus.awk         # awk    — Byzantine consensus
  09_renderer.js        # Node   — renderer + cross-verification
engines/
  vm_gates.c            # C    — gate-level VM (engine A)
  engine_rust.rs        # Rust — gate-level VM (engine B)
  engine_go/main.go     # Go   — gate-level VM (engine C)
  Engine.java           # Java — the sane oracle (engine D)
  engine_bash.sh        # Bash — gate-level VM (engine E, slowest & proudest)
services/                 # --service mode: the HTTP microservices mesh
  services.yaml         # the YAML config nobody needs
  svc_config.py         # YAML reader (single source of truth)
  serviced.sh           # launcher: start/stop/status the mesh
  lexer_service.pl      # Perl   native HTTP server
  parser_service.py     # Python native HTTP server
  compiler_service.rb   # Ruby   native HTTP server
  renderer_service.js   # Node   native HTTP server
  sidecar/main.go       # generic Go HTTP sidecar (hosts engines + consensus)
pipeline/
  http_client.sh        # retries + circuit breaker for --service mode
tests/
  reference.py          # independent oracle of truth
  run_tests.sh          # the test suite
```
