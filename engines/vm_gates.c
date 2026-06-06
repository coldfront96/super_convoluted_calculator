/* ===========================================================================
 * ENGINE A of the cathedral: THE GATE-LEVEL VM  (language: C)
 * ---------------------------------------------------------------------------
 * A stack VM whose 64-bit ALU is built entirely from a single NAND gate, and
 * whose ARBITRARY-PRECISION integers are built, in turn, on top of that 64-bit
 * gate ALU (base-2^32 limbs). So: NAND -> 64-bit ALU -> bignum ALU. The native
 * `+`/`*` operators never compute the user's answer (HARD_RULES #4); they only
 * appear as loop counters, indexing and bit shifts (plumbing).
 * Reads bytecode from argv[1] (or STDIN), prints the top of stack in decimal.
 * =========================================================================== */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* ====================== LAYER 1: logic gates ============================= */
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
static int bit64(uint64_t x, int i) { return (int)((x >> i) & 1ULL); }

/* ====================== LAYER 2: 64-bit gate ALU ========================= */
static uint64_t gate_add_c(uint64_t a, uint64_t b, int cin, int *carry_out) {
    uint64_t r = 0;
    int c = cin;
    for (int i = 0; i < 64; i++) {
        int s, co;
        full_adder(bit64(a, i), bit64(b, i), c, &s, &co);
        if (s) r |= (1ULL << i);
        c = co;
    }
    if (carry_out) *carry_out = c;
    return r;
}
static uint64_t gate_add(uint64_t a, uint64_t b) { return gate_add_c(a, b, 0, NULL); }
static uint64_t gate_not(uint64_t a) {
    uint64_t r = 0;
    for (int i = 0; i < 64; i++) if (NOT(bit64(a, i))) r |= (1ULL << i);
    return r;
}
static uint64_t gate_neg(uint64_t a) { return gate_add(gate_not(a), 1ULL); }
static uint64_t gate_sub(uint64_t a, uint64_t b) { return gate_add(a, gate_neg(b)); }
/* low 64 bits of a*b via shift-and-add (exact when a,b < 2^32) */
static uint64_t gate_mul(uint64_t a, uint64_t b) {
    uint64_t r = 0;
    for (int i = 0; i < 64; i++)
        if (bit64(b, i)) r = gate_add(r, a << i);
    return r;
}
/* unsigned 64-bit division (used only for small base-conversion divisors) */
static int uge64(uint64_t a, uint64_t b) {
    int c; gate_add_c(a, gate_not(b), 1, &c); return c;
}
static void udivmod64(uint64_t num, uint64_t den, uint64_t *q, uint64_t *r) {
    uint64_t Q = 0, R = 0;
    for (int i = 63; i >= 0; i--) {
        R = (R << 1) | ((num >> i) & 1ULL);
        if (uge64(R, den)) { R = gate_sub(R, den); Q |= (1ULL << i); }
    }
    *q = Q; *r = R;
}

/* ====================== LAYER 3: bignum on the 64-bit ALU ================ */
#define LIMBS 1024            /* base-2^32 limbs => up to ~9864 decimal digits */
typedef struct { int sign; int n; uint32_t d[LIMBS]; } Big;  /* sign in {-1,0,1} */

static void big_zero(Big *x) { x->sign = 0; x->n = 0; }
static void big_norm(Big *x) {
    while (x->n > 0 && x->d[x->n - 1] == 0) x->n--;
    if (x->n == 0) x->sign = 0;
}
static void big_copy(const Big *src, Big *dst) {
    dst->sign = src->sign; dst->n = src->n;
    for (int i = 0; i < src->n; i++) dst->d[i] = src->d[i];
}
static int cmp_abs(const Big *a, const Big *b) {
    if (a->n != b->n) return a->n < b->n ? -1 : 1;
    for (int i = a->n - 1; i >= 0; i--)
        if (a->d[i] != b->d[i]) return a->d[i] < b->d[i] ? -1 : 1;
    return 0;
}

/* res = |a| + |b| (limb adds via the gate adder) */
static void add_abs(const Big *a, const Big *b, Big *res) {
    int n = a->n > b->n ? a->n : b->n;
    uint64_t carry = 0;
    for (int i = 0; i < n; i++) {
        uint64_t av = i < a->n ? a->d[i] : 0;
        uint64_t bv = i < b->n ? b->d[i] : 0;
        uint64_t s = gate_add(gate_add(av, bv), carry);   /* < 2^33, no wrap */
        res->d[i] = (uint32_t)(s & 0xffffffffULL);
        carry = s >> 32;
    }
    if (carry) res->d[n++] = (uint32_t)carry;
    res->n = n; res->sign = 1; big_norm(res);
}
/* res = |a| - |b|, requires |a| >= |b| (limb subs via the gate subtractor) */
static void sub_abs(const Big *a, const Big *b, Big *res) {
    uint64_t borrow = 0;
    for (int i = 0; i < a->n; i++) {
        uint64_t av = a->d[i];
        uint64_t bv = i < b->n ? b->d[i] : 0;
        uint64_t s = gate_sub(gate_sub(av, bv), borrow);
        res->d[i] = (uint32_t)(s & 0xffffffffULL);
        borrow = (s >> 32) ? 1 : 0;   /* underflow sets the high word to all 1s */
    }
    res->n = a->n; res->sign = 1; big_norm(res);
}

static void big_add(const Big *a, const Big *b, Big *res);
static void big_neg_into(const Big *a, Big *res) { big_copy(a, res); res->sign = -res->sign; }

static void big_add(const Big *a, const Big *b, Big *res) {
    if (a->sign == 0) { big_copy(b, res); return; }
    if (b->sign == 0) { big_copy(a, res); return; }
    if (a->sign == b->sign) {
        int s = a->sign; add_abs(a, b, res); res->sign = res->n ? s : 0;
    } else {
        int c = cmp_abs(a, b);
        if (c == 0) { big_zero(res); }
        else if (c > 0) { sub_abs(a, b, res); res->sign = res->n ? a->sign : 0; }
        else { sub_abs(b, a, res); res->sign = res->n ? b->sign : 0; }
    }
}
static void big_sub(const Big *a, const Big *b, Big *res) {
    Big nb; big_neg_into(b, &nb); big_add(a, &nb, res);
}

/* res = |a| * |b| (schoolbook; limb products via the gate multiplier) */
static void big_mul(const Big *a, const Big *b, Big *res) {
    Big out; big_zero(&out);
    if (a->sign == 0 || b->sign == 0) { big_copy(&out, res); return; }
    int total = a->n + b->n;
    for (int i = 0; i < total; i++) out.d[i] = 0;
    for (int i = 0; i < a->n; i++) {
        uint64_t carry = 0;
        for (int j = 0; j < b->n; j++) {
            uint64_t prod = gate_mul((uint64_t)a->d[i], (uint64_t)b->d[j]);
            uint64_t cur  = out.d[i + j];
            uint64_t s = gate_add(gate_add(prod, cur), carry); /* <= 2^64-1 */
            out.d[i + j] = (uint32_t)(s & 0xffffffffULL);
            carry = s >> 32;
        }
        int k = i + b->n;
        while (carry) {
            uint64_t s = gate_add((uint64_t)out.d[k], carry);
            out.d[k] = (uint32_t)(s & 0xffffffffULL);
            carry = s >> 32; k++;
        }
    }
    out.n = total; out.sign = 1; big_norm(&out);
    if (out.sign != 0) out.sign = (a->sign == b->sign) ? 1 : -1;
    big_copy(&out, res);
}

static void shl1(Big *x) {   /* x <<= 1 (plumbing: bit shifts) */
    uint32_t carry = 0;
    for (int i = 0; i < x->n; i++) {
        uint32_t nv = (x->d[i] << 1) | carry;
        carry = x->d[i] >> 31;
        x->d[i] = nv;
    }
    if (carry) x->d[x->n++] = carry;
}

/* unsigned binary long division: A,B >= 0 -> Q = A/B, R = A%B */
static void udiv(const Big *A, const Big *B, Big *Q, Big *R) {
    big_zero(Q); big_zero(R);
    int bits = A->n * 32;
    for (int p = bits - 1; p >= 0; p--) {
        shl1(R);
        int abit = (A->d[p >> 5] >> (p & 31)) & 1;
        if (abit) { if (R->n == 0) { R->d[0] = 1; R->n = 1; } else R->d[0] |= 1u; }
        big_norm(R);
        if (cmp_abs(R, B) >= 0) {
            Big t; sub_abs(R, B, &t); big_copy(&t, R);
            int w = p >> 5, b = p & 31;
            Q->d[w] |= (1u << b);
            if (w + 1 > Q->n) Q->n = w + 1;
        }
    }
    Q->sign = 1; R->sign = 1; big_norm(Q); big_norm(R);
}

/* signed division truncating toward zero; remainder takes the dividend's sign */
static int big_divmod(const Big *a, const Big *b, Big *q, Big *r) {
    if (b->sign == 0) return 0;
    Big A, B; big_copy(a, &A); A.sign = A.n ? 1 : 0;
    big_copy(b, &B); B.sign = 1;
    udiv(&A, &B, q, r);
    q->sign = q->n ? (a->sign == b->sign ? 1 : -1) : 0;
    r->sign = r->n ? a->sign : 0;
    return 1;
}

/* ---- decimal <-> bignum (using the 64-bit gate divider for base chunks) ---- */
static void mul_small(Big *x, uint32_t m) {
    uint64_t carry = 0;
    for (int i = 0; i < x->n; i++) {
        uint64_t s = gate_add(gate_mul((uint64_t)x->d[i], (uint64_t)m), carry);
        x->d[i] = (uint32_t)(s & 0xffffffffULL);
        carry = s >> 32;
    }
    while (carry) { x->d[x->n++] = (uint32_t)(carry & 0xffffffffULL); carry >>= 32; }
    if (x->n && x->sign == 0) x->sign = 1;
    big_norm(x);
}
static void add_small(Big *x, uint32_t a) {
    uint64_t carry = a;
    int i = 0;
    while (carry) {
        uint64_t cur = i < x->n ? x->d[i] : 0;
        uint64_t s = gate_add(cur, carry);
        if (i >= x->n) x->n = i + 1;
        x->d[i] = (uint32_t)(s & 0xffffffffULL);
        carry = s >> 32; i++;
    }
    if (x->n && x->sign == 0) x->sign = 1;
    big_norm(x);
}
static void from_dec(const char *s, Big *res) {
    big_zero(res);
    int neg = 0;
    if (*s == '-') { neg = 1; s++; }
    else if (*s == '+') { s++; }
    for (; *s; s++) {
        if (*s < '0' || *s > '9') continue;
        mul_small(res, 10);
        add_small(res, (uint32_t)(*s - '0'));
    }
    if (res->n) res->sign = neg ? -1 : 1; else res->sign = 0;
}
static void to_dec(const Big *x, char *out) {
    if (x->sign == 0) { strcpy(out, "0"); return; }
    Big t; big_copy(x, &t); t.sign = 1;
    char buf[LIMBS * 10];
    int len = 0;
    while (t.n > 0) {
        uint64_t rem = 0;
        for (int i = t.n - 1; i >= 0; i--) {
            uint64_t cur = (rem << 32) | t.d[i];
            uint64_t q, r;
            udivmod64(cur, 1000000000ULL, &q, &r);
            t.d[i] = (uint32_t)q;
            rem = r;
        }
        big_norm(&t);
        if (t.n > 0) {
            for (int k = 0; k < 9; k++) { uint64_t q, r; udivmod64(rem, 10, &q, &r); buf[len++] = '0' + (int)r; rem = q; }
        } else {
            while (rem > 0) { uint64_t q, r; udivmod64(rem, 10, &q, &r); buf[len++] = '0' + (int)r; rem = q; }
        }
    }
    int pos = 0;
    if (x->sign < 0) out[pos++] = '-';
    for (int i = len - 1; i >= 0; i--) out[pos++] = buf[i];
    out[pos] = '\0';
}

/* ====================== LAYER 4: the stack VM =========================== */
static Big stack[512];

int main(int argc, char **argv) {
    FILE *f = stdin;
    if (argc > 1) {
        f = fopen(argv[1], "r");
        if (!f) { perror("vm_gates: open"); return 2; }
    }
    int sp = 0;
    char line[LIMBS * 11];

    while (fgets(line, sizeof line, f)) {
        char op[32];
        char operand[LIMBS * 10];
        int nf = sscanf(line, "%31s %s", op, operand);
        if (nf < 1) continue;

        if (!strcmp(op, "PUSH")) {
            from_dec(operand, &stack[sp]); sp++;
        } else if (!strcmp(op, "NEG")) {
            stack[sp - 1].sign = -stack[sp - 1].sign;
        } else {
            Big b; big_copy(&stack[--sp], &b);
            Big a; big_copy(&stack[--sp], &a);
            Big res;
            if (!strcmp(op, "ADD")) big_add(&a, &b, &res);
            else if (!strcmp(op, "SUB")) big_sub(&a, &b, &res);
            else if (!strcmp(op, "MUL")) big_mul(&a, &b, &res);
            else if (!strcmp(op, "DIV") || !strcmp(op, "MOD")) {
                Big q, r;
                if (!big_divmod(&a, &b, &q, &r)) { printf("ERR:DIVZERO\n"); return 0; }
                big_copy(op[0] == 'D' ? &q : &r, &res);
            } else { continue; }
            big_copy(&res, &stack[sp]); sp++;
        }
    }
    if (argc > 1) fclose(f);
    if (sp < 1) { fprintf(stderr, "vm_gates: empty stack\n"); return 4; }

    char out[LIMBS * 10];
    to_dec(&stack[sp - 1], out);
    printf("%s\n", out);
    return 0;
}
