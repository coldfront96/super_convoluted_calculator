# 🏛️ The Super Convoluted Calculator

A calculator that computes `1 + 2 = 3` — **correctly, every time** — by way of a
ten-language pipeline, a virtual machine whose ALU is built from individual logic
gates, four independent execution engines, and a Byzantine consensus vote.

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
| 4 | **C / Rust / Go / Java** | four independent VMs | one integer each |
| 5 | **awk** | Byzantine consensus (unanimity or panic) | the verdict |
| 6 | **Node** | render decimal/hex/binary/Roman/English + cross-verify | JSON |
| — | **jq** | extract the headline answer | the number |

That's **bash, Perl, Python, Ruby, C, Rust, Go, Java, Node/JS, awk** on the
critical path, with `base64`, `jq`, and `make` as accomplices.

## The crown jewel: arithmetic from NAND gates

Three of the four engines (C, Rust, Go) compute nothing with the native `+` or
`*`. Instead they build everything from a single primitive — the **NAND gate**:

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

64-bit signed integers, two's-complement, wrapping overflow, division truncating
toward zero — identical across all four engines. Supports `+ - * / %`,
parentheses, and unary minus with correct precedence. Division by zero is
detected and reported as an error (exit code 3).

## Usage

```sh
make all                       # build the four engines
./calc "2 * (3 + 4)"           # -> 14
./calc --report "6 * 7"        # full JSON render (decimal/hex/binary/roman/words)
./calc "1 / 0"; echo $?        # -> error on stderr, exit code 3
bash tests/run_tests.sh        # 274 cases vs. an independent Python oracle
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
tests/
  reference.py          # independent oracle of truth
  run_tests.sh          # the test suite
```
