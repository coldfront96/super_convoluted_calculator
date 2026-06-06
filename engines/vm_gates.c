/* ===========================================================================
 * ENGINE A of the cathedral: THE GATE-LEVEL VM  (language: C)
 * ---------------------------------------------------------------------------
 * A stack virtual machine whose ALU performs ALL arithmetic out of a single
 * primitive logic gate: NAND. From NAND we build NOT/AND/OR/XOR, then a full
 * adder, then a 64-bit ripple-carry adder, then two's-complement negation,
 * subtraction, shift-and-add multiplication, and restoring long division.
 *
 * The native `+` and `*` operators are NEVER used to compute the user's answer
 * (HARD_RULES #4). They appear only as loop counters and bit shifts (plumbing).
 * Reads bytecode from argv[1] (or STDIN), prints the top of stack as a signed
 * 64-bit integer.
 * =========================================================================== */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* ---- The one true gate, and its children ---- */
static int NAND(int a, int b) { return (a & b) ? 0 : 1; }
static int NOT(int a)         { return NAND(a, a); }
static int AND(int a, int b)  { return NOT(NAND(a, b)); }
static int OR(int a, int b)   { return NAND(NOT(a), NOT(b)); }
static int XOR(int a, int b)  { int t = NAND(a, b); return NAND(NAND(a, t), NAND(b, t)); }

static void full_adder(int a, int b, int cin, int *sum, int *cout) {
    int axb = XOR(a, b);
    *sum  = XOR(axb, cin);
    *cout = OR(AND(axb, cin), AND(a, b));
}

static int bit(uint64_t x, int i) { return (int)((x >> i) & 1ULL); }

/* 64-bit ripple-carry adder. Returns the sum; reports the final carry-out. */
static uint64_t gate_add_c(uint64_t a, uint64_t b, int cin, int *carry_out) {
    uint64_t r = 0;
    int c = cin;
    for (int i = 0; i < 64; i++) {
        int s, co;
        full_adder(bit(a, i), bit(b, i), c, &s, &co);
        if (s) r |= (1ULL << i);
        c = co;
    }
    if (carry_out) *carry_out = c;
    return r;
}
static uint64_t gate_add(uint64_t a, uint64_t b) { return gate_add_c(a, b, 0, NULL); }

static uint64_t gate_not(uint64_t a) {
    uint64_t r = 0;
    for (int i = 0; i < 64; i++) if (NOT(bit(a, i))) r |= (1ULL << i);
    return r;
}
static uint64_t gate_neg(uint64_t a) { return gate_add(gate_not(a), 1ULL); }
static uint64_t gate_sub(uint64_t a, uint64_t b) { return gate_add(a, gate_neg(b)); }

/* Unsigned a >= b, decided purely by the carry-out of (a + ~b + 1). */
static int uge(uint64_t a, uint64_t b) {
    int c;
    gate_add_c(a, gate_not(b), 1, &c);
    return c;
}

/* Shift-and-add multiply: low 64 bits of a*b (correct for two's complement). */
static uint64_t gate_mul(uint64_t a, uint64_t b) {
    uint64_t r = 0;
    for (int i = 0; i < 64; i++)
        if (bit(b, i)) r = gate_add(r, a << i);
    return r;
}

static int is_neg(uint64_t a) { return bit(a, 63); }

/* Unsigned restoring long division. */
static void udivmod(uint64_t num, uint64_t den, uint64_t *q, uint64_t *r) {
    uint64_t Q = 0, R = 0;
    for (int i = 63; i >= 0; i--) {
        R = (R << 1) | ((num >> i) & 1ULL);
        if (uge(R, den)) {
            R = gate_sub(R, den);
            Q |= (1ULL << i);
        }
    }
    *q = Q;
    *r = R;
}

/* Signed division truncating toward zero; remainder takes sign of dividend. */
static int sdivmod(uint64_t a, uint64_t b, uint64_t *q, uint64_t *r) {
    if (b == 0) return 0;
    int na = is_neg(a), nb = is_neg(b);
    uint64_t ua = na ? gate_neg(a) : a;
    uint64_t ub = nb ? gate_neg(b) : b;
    uint64_t uq, ur;
    udivmod(ua, ub, &uq, &ur);
    *q = (na ^ nb) ? gate_neg(uq) : uq;
    *r = na ? gate_neg(ur) : ur;
    return 1;
}

int main(int argc, char **argv) {
    FILE *f = stdin;
    if (argc > 1) {
        f = fopen(argv[1], "r");
        if (!f) { perror("vm_gates: open"); return 2; }
    }

    uint64_t stack[4096];
    int sp = 0;
    char line[256];

    while (fgets(line, sizeof line, f)) {
        char op[32];
        long long v = 0;
        int n = sscanf(line, "%31s %lld", op, &v);
        if (n < 1) continue;

        if (!strcmp(op, "PUSH")) {
            stack[sp++] = (uint64_t)v;
        } else if (!strcmp(op, "NEG")) {
            stack[sp - 1] = gate_neg(stack[sp - 1]);
        } else {
            uint64_t b = stack[--sp];
            uint64_t a = stack[--sp];
            uint64_t res;
            if (!strcmp(op, "ADD")) {
                res = gate_add(a, b);
            } else if (!strcmp(op, "SUB")) {
                res = gate_sub(a, b);
            } else if (!strcmp(op, "MUL")) {
                res = gate_mul(a, b);
            } else if (!strcmp(op, "DIV") || !strcmp(op, "MOD")) {
                uint64_t q, r;
                if (!sdivmod(a, b, &q, &r)) {
                    printf("ERR:DIVZERO\n");
                    return 0; /* exit 0 so the consensus layer sees the vote */
                }
                res = (op[0] == 'D') ? q : r;
            } else {
                continue; /* unknown opcode: ignore */
            }
            stack[sp++] = res;
        }
    }

    if (argc > 1) fclose(f);
    if (sp < 1) { fprintf(stderr, "vm_gates: empty stack\n"); return 4; }
    printf("%lld\n", (long long)stack[sp - 1]);
    return 0;
}
