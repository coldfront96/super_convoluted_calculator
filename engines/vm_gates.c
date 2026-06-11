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
    /* Q's bits are set at arbitrary positions below; pre-zero every limb so
     * any all-zero middle limb is initialized rather than left as garbage. */
    for (int i = 0; i < A->n; i++) Q->d[i] = 0;
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
    big_zero(q); big_zero(r);            /* always initialize outputs */
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

/* ---- small bignum helpers for the rational layer ---- */
static void big_one(Big *r) { r->sign = 1; r->n = 1; r->d[0] = 1; }
static void big_set_small(Big *r, uint32_t v) {
    if (v == 0) { big_zero(r); return; }
    r->sign = 1; r->n = 1; r->d[0] = v;
}
static int big_is_one(const Big *x) { return x->sign == 1 && x->n == 1 && x->d[0] == 1; }

/* divmod by a small divisor; returns remainder, optionally stores quotient */
static uint32_t divmod_small(const Big *x, uint32_t m, Big *q) {
    Big out; big_zero(&out); out.n = x->n;
    uint64_t rem = 0;
    for (int i = x->n - 1; i >= 0; i--) {
        uint64_t cur = (rem << 32) | x->d[i];
        uint64_t qq, rr;
        udivmod64(cur, (uint64_t)m, &qq, &rr);
        out.d[i] = (uint32_t)qq;
        rem = rr;
    }
    out.sign = 1; big_norm(&out);
    if (q) big_copy(&out, q);
    return (uint32_t)rem;
}

/* G = gcd(|A|, |B|) via Euclid */
static void big_gcd(const Big *A, const Big *B, Big *G) {
    Big a, b; big_copy(A, &a); a.sign = a.n ? 1 : 0;
    big_copy(B, &b); b.sign = b.n ? 1 : 0;
    while (b.sign != 0) {
        Big q, r; big_divmod(&a, &b, &q, &r);
        big_copy(&b, &a); big_copy(&r, &b);
    }
    big_copy(&a, G); G->sign = G->n ? 1 : 0;
}

/* out = base ** e, e >= 0 (exponentiation by squaring; sign handled by big_mul) */
static void big_pow(const Big *base, const Big *e, Big *out) {
    Big result; big_one(&result);
    if (e->sign == 0) { big_copy(&result, out); return; }
    Big b; big_copy(base, &b);
    int top = e->n * 32 - 1;
    while (top >= 0 && !((e->d[top >> 5] >> (top & 31)) & 1)) top--;
    for (int i = 0; i <= top; i++) {
        if ((e->d[i >> 5] >> (i & 31)) & 1) {
            Big t; big_mul(&result, &b, &t); big_copy(&t, &result);
        }
        if (i < top) { Big t; big_mul(&b, &b, &t); big_copy(&t, &b); }
    }
    big_copy(&result, out);
}

/* a few more small helpers used by the rounding / function layer */
static void big_set_int(Big *r, long v) {
    if (v == 0) { big_zero(r); return; }
    int neg = v < 0;
    unsigned long uv = neg ? (unsigned long)(-(v + 1)) + 1UL : (unsigned long)v;
    r->n = 0;
    while (uv) { r->d[r->n++] = (uint32_t)(uv & 0xffffffffUL); uv >>= 32; }
    r->sign = neg ? -1 : 1;
}
static int big_cmp(const Big *a, const Big *b) { Big t; big_sub(a, b, &t); return t.sign; }
static void pow10(int k, Big *out) {
    Big ten, kk; big_set_small(&ten, 10); big_set_int(&kk, k); big_pow(&ten, &kk, out);
}
static void big_isqrt(const Big *N, Big *out) {
    if (N->sign == 0) { big_zero(out); return; }
    int top = N->n * 32 - 1;
    while (top >= 0 && !((N->d[top >> 5] >> (top & 31)) & 1)) top--;
    int half = top / 2 + 1;
    Big x; big_zero(&x); x.n = (half >> 5) + 1;
    for (int i = 0; i < x.n; i++) x.d[i] = 0;
    x.d[half >> 5] = 1u << (half & 31); x.sign = 1;
    for (int it = 0; it < 2000; it++) {
        Big q, r; big_divmod(N, &x, &q, &r);
        Big s; big_add(&x, &q, &s);
        Big nx; divmod_small(&s, 2, &nx);
        if (big_cmp(&nx, &x) >= 0) break;
        big_copy(&nx, &x);
    }
    for (int i = 0; i < 4; i++) {
        Big sq; big_mul(&x, &x, &sq);
        if (big_cmp(&sq, N) <= 0) break;
        Big one, t; big_one(&one); big_sub(&x, &one, &t); big_copy(&t, &x);
    }
    big_copy(&x, out);
}

/* sign of (10^E * den - num), valid for negative E too (no fractional powers) */
static int cmp_10E_den_num(int E, const Big *den, const Big *num) {
    Big p, t;
    if (E >= 0) { pow10(E, &p); big_mul(&p, den, &t); return big_cmp(&t, num); }
    pow10(-E, &p); big_mul(&p, num, &t); return big_cmp(den, &t);
}

/* round |num/den| to n significant digits (round-half-even) -> rnum/rden */
static void round_sig_rat(const Big *numIn, const Big *denIn, int n, Big *rnum, Big *rden) {
    if (numIn->sign == 0) { big_zero(rnum); big_one(rden); return; }
    int sign = numIn->sign;
    Big num; big_copy(numIn, &num); num.sign = 1;
    Big den; big_copy(denIn, &den); den.sign = 1;
    char nb[LIMBS * 10], db[LIMBS * 10];
    to_dec(&num, nb); to_dec(&den, db);
    int E = (int)strlen(nb) - (int)strlen(db);
    for (;;) {
        if (cmp_10E_den_num(E, &den, &num) > 0) { E--; continue; }
        if (cmp_10E_den_num(E + 1, &den, &num) <= 0) { E++; continue; }
        break;
    }
    int scale = n - 1 - E;
    Big snum, sden;
    if (scale >= 0) { Big p; pow10(scale, &p); big_mul(&num, &p, &snum); big_copy(&den, &sden); }
    else { big_copy(&num, &snum); Big p; pow10(-scale, &p); big_mul(&den, &p, &sden); }
    Big q, r; big_divmod(&snum, &sden, &q, &r);
    Big two_r; big_add(&r, &r, &two_r);
    int c = big_cmp(&two_r, &sden), up = 0;
    if (c > 0) up = 1;
    else if (c == 0 && q.n > 0 && (q.d[0] & 1)) up = 1;
    if (up) { Big one, t; big_one(&one); big_add(&q, &one, &t); big_copy(&t, &q); }
    int ex = E - n + 1;
    if (ex >= 0) { Big p; pow10(ex, &p); big_mul(&q, &p, rnum); big_one(rden); }
    else { big_copy(&q, rnum); pow10(-ex, rden); }
    rnum->sign = rnum->n ? sign : 0;
}

/* ============== LAYER 3.6: fixed-point transcendental functions ========= */
#define WP 120          /* fixed-point working precision (fractional digits) */
#define FUNC_SIG 80     /* function results rounded to this many sig digits */
#define OUT_SIG 50      /* inexact answers displayed to this many sig digits */
static const char *PI_STR =
    "3.14159265358979323846264338327950288419716939937510"
    "58209749445923078164062862089986280348253421170679"
    "82148086513282306647093844609550582231725359408128";
static Big SCALE, FP_PI, FP_2PI, FP_LN2, FP_LN10, FP_E;
static int FP_INIT = 0;

static void fp_from_rat(const Big *num, const Big *den, Big *out) {
    Big t, q, r; big_mul(num, &SCALE, &t); big_divmod(&t, den, &q, &r); big_copy(&q, out);
}
static void fp_mul(const Big *a, const Big *b, Big *out) {
    Big t, q, r; big_mul(a, b, &t); big_divmod(&t, &SCALE, &q, &r); big_copy(&q, out);
}
static void fp_div(const Big *a, const Big *b, Big *out) {
    Big t, q, r; big_mul(a, &SCALE, &t); big_divmod(&t, b, &q, &r); big_copy(&q, out);
}
static void fp_div_int(const Big *a, uint32_t k, Big *out) {
    Big aa; big_copy(a, &aa); int neg = aa.sign < 0; aa.sign = aa.n ? 1 : 0;
    Big q; divmod_small(&aa, k, &q); if (neg) q.sign = -q.sign; big_copy(&q, out);
}
static void fp_parse_const(const char *s, Big *out) {
    char buf[512]; strncpy(buf, s, sizeof buf - 1); buf[sizeof buf - 1] = '\0';
    char *dot = strchr(buf, '.');
    Big num, den;
    if (dot) {
        char tmp[512]; *dot = '\0';
        strcpy(tmp, buf); strcat(tmp, dot + 1);
        from_dec(tmp, &num); pow10((int)strlen(dot + 1), &den);
    } else { from_dec(buf, &num); big_one(&den); }
    fp_from_rat(&num, &den, out);
}
static void fp_atanh(const Big *y, Big *out) {
    Big sum, term, y2; big_copy(y, &sum); big_copy(y, &term); fp_mul(y, y, &y2);
    uint32_t k = 1;
    while (term.sign != 0 && k < 200000) {
        Big t; fp_mul(&term, &y2, &t); big_copy(&t, &term);
        Big d; fp_div_int(&term, 2 * k + 1, &d);
        Big s; big_add(&sum, &d, &s); big_copy(&s, &sum);
        k++;
    }
    big_copy(&sum, out);
}
static void fp_exp(const Big *x, Big *out) {
    Big r; big_copy(x, &r); int m = 0;
    for (;;) {
        Big a; big_copy(&r, &a); a.sign = a.n ? 1 : 0;
        if (cmp_abs(&a, &SCALE) < 0 || m > 8192) break;
        Big h; fp_div_int(&r, 2, &h); big_copy(&h, &r); m++;
    }
    Big sum, term; big_copy(&SCALE, &sum); big_copy(&SCALE, &term);
    uint32_t k = 1;
    while (term.sign != 0 && k < 200000) {
        Big t; fp_mul(&term, &r, &t); fp_div_int(&t, k, &term);
        Big s; big_add(&sum, &term, &s); big_copy(&s, &sum);
        k++;
    }
    for (int i = 0; i < m; i++) { Big t; fp_mul(&sum, &sum, &t); big_copy(&t, &sum); }
    big_copy(&sum, out);
}
static void fp_ln(const Big *x, Big *out) {
    Big X; big_copy(x, &X); int e2 = 0;
    Big two_scale; big_add(&SCALE, &SCALE, &two_scale);
    while (cmp_abs(&X, &two_scale) >= 0) { Big h; fp_div_int(&X, 2, &h); big_copy(&h, &X); e2++; }
    while (cmp_abs(&X, &SCALE) < 0) { Big t; big_add(&X, &X, &t); big_copy(&t, &X); e2--; }
    Big numy, deny, y; big_sub(&X, &SCALE, &numy); big_add(&X, &SCALE, &deny); fp_div(&numy, &deny, &y);
    Big at, lnfrac; fp_atanh(&y, &at); big_add(&at, &at, &lnfrac);
    Big e2b, tmp, res; big_set_int(&e2b, e2); big_mul(&FP_LN2, &e2b, &tmp); big_add(&lnfrac, &tmp, &res);
    big_copy(&res, out);
}
static void round_fp_to_int(const Big *v, Big *out) {
    Big q, r; big_divmod(v, &SCALE, &q, &r);
    Big ar; big_copy(&r, &ar); ar.sign = ar.n ? 1 : 0;
    Big two_ar; big_add(&ar, &ar, &two_ar);
    if (big_cmp(&two_ar, &SCALE) >= 0) {
        Big one; big_set_int(&one, v->sign < 0 ? -1 : 1);
        Big t; big_add(&q, &one, &t); big_copy(&t, out);
    } else big_copy(&q, out);
}
static void fp_reduce_2pi(const Big *x, Big *out) {
    Big d; fp_div(x, &FP_2PI, &d);
    Big k; round_fp_to_int(&d, &k);
    Big kfp; big_mul(&k, &FP_2PI, &kfp);
    Big r; big_sub(x, &kfp, &r); big_copy(&r, out);
}
static void fp_sin(const Big *x, Big *out) {
    Big xr; fp_reduce_2pi(x, &xr);
    Big sum, term, x2; big_copy(&xr, &sum); big_copy(&xr, &term); fp_mul(&xr, &xr, &x2);
    uint32_t k = 1;
    while (term.sign != 0 && k < 200000) {
        Big t; fp_mul(&term, &x2, &t);
        fp_div_int(&t, (2 * k) * (2 * k + 1), &term);
        term.sign = -term.sign;
        Big s; big_add(&sum, &term, &s); big_copy(&s, &sum);
        k++;
    }
    big_copy(&sum, out);
}
static void fp_cos(const Big *x, Big *out) {
    Big xr; fp_reduce_2pi(x, &xr);
    Big sum, term, x2; big_copy(&SCALE, &sum); big_copy(&SCALE, &term); fp_mul(&xr, &xr, &x2);
    uint32_t k = 1;
    while (term.sign != 0 && k < 200000) {
        Big t; fp_mul(&term, &x2, &t);
        fp_div_int(&t, (2 * k - 1) * (2 * k), &term);
        term.sign = -term.sign;
        Big s; big_add(&sum, &term, &s); big_copy(&s, &sum);
        k++;
    }
    big_copy(&sum, out);
}
static void fp_sqrt(const Big *x, Big *out) { Big t; big_mul(x, &SCALE, &t); big_isqrt(&t, out); }
static void fp_atan(const Big *x, Big *out) {
    Big xx; big_copy(x, &xx); int neg = xx.sign < 0; xx.sign = xx.n ? 1 : 0;
    Big thresh; fp_div_int(&SCALE, 10, &thresh);    /* 0.1 */
    int m = 0;
    while (cmp_abs(&xx, &thresh) > 0 && m < 4096) {
        Big x2; fp_mul(&xx, &xx, &x2);
        Big s; big_add(&SCALE, &x2, &s);            /* 1 + x^2 */
        Big rt; fp_sqrt(&s, &rt);
        Big den; big_add(&SCALE, &rt, &den);        /* 1 + sqrt(1+x^2) */
        Big nx; fp_div(&xx, &den, &nx); big_copy(&nx, &xx); m++;
    }
    Big sum, term, x2; big_copy(&xx, &sum); big_copy(&xx, &term); fp_mul(&xx, &xx, &x2);
    uint32_t k = 1;
    while (term.sign != 0 && k < 200000) {
        Big t; fp_mul(&term, &x2, &t); big_copy(&t, &term); term.sign = -term.sign;
        Big d; fp_div_int(&term, 2 * k + 1, &d);
        Big ss; big_add(&sum, &d, &ss); big_copy(&ss, &sum);
        k++;
    }
    for (int i = 0; i < m; i++) { Big d2; big_add(&sum, &sum, &d2); big_copy(&d2, &sum); }
    if (neg) sum.sign = -sum.sign;
    big_copy(&sum, out);
}
static int fp_asin(const Big *x, Big *out) {       /* 1 ok, 0 domain */
    Big ax; big_copy(x, &ax); ax.sign = ax.n ? 1 : 0;
    if (cmp_abs(&ax, &SCALE) > 0) return 0;
    if (cmp_abs(&ax, &SCALE) == 0) {
        Big half; fp_div_int(&FP_PI, 2, &half); if (x->sign < 0) half.sign = -half.sign;
        big_copy(&half, out); return 1;
    }
    Big x2, d, rt, arg; fp_mul(x, x, &x2); big_sub(&SCALE, &x2, &d); fp_sqrt(&d, &rt); fp_div(x, &rt, &arg);
    fp_atan(&arg, out); return 1;
}
static int fp_acos(const Big *x, Big *out) {
    Big as; if (!fp_asin(x, &as)) return 0;
    Big half; fp_div_int(&FP_PI, 2, &half); big_sub(&half, &as, out); return 1;
}
static void fp_sinh(const Big *x, Big *out) {
    Big nx; big_copy(x, &nx); nx.sign = -nx.sign;
    Big e1, e2, d; fp_exp(x, &e1); fp_exp(&nx, &e2); big_sub(&e1, &e2, &d); fp_div_int(&d, 2, out);
}
static void fp_cosh(const Big *x, Big *out) {
    Big nx; big_copy(x, &nx); nx.sign = -nx.sign;
    Big e1, e2, d; fp_exp(x, &e1); fp_exp(&nx, &e2); big_add(&e1, &e2, &d); fp_div_int(&d, 2, out);
}
static void fp_tanh(const Big *x, Big *out) {
    Big nx; big_copy(x, &nx); nx.sign = -nx.sign;
    Big e1, e2, nu, de; fp_exp(x, &e1); fp_exp(&nx, &e2); big_sub(&e1, &e2, &nu); big_add(&e1, &e2, &de);
    fp_div(&nu, &de, out);
}

static void fp_init(void) {
    if (FP_INIT) return; FP_INIT = 1;
    pow10(WP, &SCALE);
    fp_parse_const(PI_STR, &FP_PI);
    big_add(&FP_PI, &FP_PI, &FP_2PI);
    Big third, at; fp_div_int(&SCALE, 3, &third); fp_atanh(&third, &at); big_add(&at, &at, &FP_LN2);
    Big ten, one, tenfp; big_set_small(&ten, 10); big_one(&one); fp_from_rat(&ten, &one, &tenfp);
    fp_ln(&tenfp, &FP_LN10);
    fp_exp(&SCALE, &FP_E);
}

/* ====================== LAYER 3.5: exact rationals ====================== */
/* A rational is num/den, kept reduced with den > 0; zero is 0/1. inexact marks
 * values that have passed through a transcendental function. */
typedef struct { Big num, den; int inexact; } Rat;

static void rat_norm(Rat *r) {
    if (r->num.sign == 0) { big_one(&r->den); return; }
    if (r->den.sign < 0) { r->num.sign = -r->num.sign; r->den.sign = 1; }
    Big g; big_gcd(&r->num, &r->den, &g);
    if (!big_is_one(&g)) {
        Big q, rr;
        big_divmod(&r->num, &g, &q, &rr); big_copy(&q, &r->num);
        big_divmod(&r->den, &g, &q, &rr); big_copy(&q, &r->den);
    }
}
static void rat_set_int(Rat *r, const Big *v) { big_copy(v, &r->num); big_one(&r->den); }

static void rat_add(const Rat *a, const Rat *b, Rat *o) {
    Big t1, t2, n, d;
    big_mul(&a->num, &b->den, &t1);
    big_mul(&b->num, &a->den, &t2);
    big_add(&t1, &t2, &n);
    big_mul(&a->den, &b->den, &d);
    big_copy(&n, &o->num); big_copy(&d, &o->den); o->inexact = a->inexact | b->inexact; rat_norm(o);
}
static void rat_sub(const Rat *a, const Rat *b, Rat *o) {
    Big t1, t2, n, d;
    big_mul(&a->num, &b->den, &t1);
    big_mul(&b->num, &a->den, &t2);
    big_sub(&t1, &t2, &n);
    big_mul(&a->den, &b->den, &d);
    big_copy(&n, &o->num); big_copy(&d, &o->den); o->inexact = a->inexact | b->inexact; rat_norm(o);
}
static void rat_mul(const Rat *a, const Rat *b, Rat *o) {
    Big n, d; big_mul(&a->num, &b->num, &n); big_mul(&a->den, &b->den, &d);
    big_copy(&n, &o->num); big_copy(&d, &o->den); o->inexact = a->inexact | b->inexact; rat_norm(o);
}
static int rat_div(const Rat *a, const Rat *b, Rat *o) {
    if (b->num.sign == 0) return 0;
    Big n, d; big_mul(&a->num, &b->den, &n); big_mul(&a->den, &b->num, &d);
    big_copy(&n, &o->num); big_copy(&d, &o->den); o->inexact = a->inexact | b->inexact; rat_norm(o); return 1;
}
static int rat_idiv(const Rat *a, const Rat *b, Rat *o) {
    Big N, D; big_mul(&a->num, &b->den, &N); big_mul(&a->den, &b->num, &D);
    if (D.sign == 0) return 0;
    Big q, r; big_divmod(&N, &D, &q, &r);
    rat_set_int(o, &q); o->inexact = a->inexact | b->inexact; return 1;
}
static int rat_mod(const Rat *a, const Rat *b, Rat *o) {
    Rat t; if (!rat_idiv(a, b, &t)) return 0;
    Rat bt; rat_mul(b, &t, &bt); rat_sub(a, &bt, o); o->inexact = a->inexact | b->inexact; return 1;
}
/* returns 1 ok, 0 div-by-zero, -1 non-integer exponent (handled by caller) */
static int rat_pow(const Rat *a, const Rat *b, Rat *o) {
    if (!big_is_one(&b->den)) return -1;
    if (b->num.sign == 0) { big_one(&o->num); big_one(&o->den); o->inexact = a->inexact | b->inexact; return 1; }
    Big m; big_copy(&b->num, &m); m.sign = 1;
    Big pn, pd; big_pow(&a->num, &m, &pn); big_pow(&a->den, &m, &pd);
    if (b->num.sign > 0) { big_copy(&pn, &o->num); big_copy(&pd, &o->den); }
    else { if (a->num.sign == 0) return 0; big_copy(&pd, &o->num); big_copy(&pn, &o->den); }
    o->inexact = a->inexact | b->inexact; rat_norm(o); return 1;
}
/* wrap a fixed-point value into an inexact rational, rounded to FUNC_SIG sig. */
static void rat_from_fp(const Big *v, Rat *o) {
    Big rn, rd; round_sig_rat(v, &SCALE, FUNC_SIG, &rn, &rd);
    big_copy(&rn, &o->num); big_copy(&rd, &o->den); o->inexact = 1; rat_norm(o);
}

/* canonical string: integer | terminating decimal | reduced fraction p/q */
static void rat_to_str(const Rat *r, char *out) {
    if (r->num.sign == 0) { strcpy(out, "0"); return; }
    if (big_is_one(&r->den)) { to_dec(&r->num, out); return; }

    Big q; big_copy(&r->den, &q); q.sign = 1;
    int a = 0, b = 0;
    while (divmod_small(&q, 2, NULL) == 0) { Big t; divmod_small(&q, 2, &t); big_copy(&t, &q); a++; }
    while (divmod_small(&q, 5, NULL) == 0) { Big t; divmod_small(&q, 5, &t); big_copy(&t, &q); b++; }

    if (big_is_one(&q)) {
        int k = a > b ? a : b;
        Big ten, kk, tenk; big_set_small(&ten, 10); big_set_small(&kk, (uint32_t)k);
        big_pow(&ten, &kk, &tenk);
        Big scale, rr; big_divmod(&tenk, &r->den, &scale, &rr);
        Big N; big_mul(&r->num, &scale, &N);
        Big absN; big_copy(&N, &absN); absN.sign = absN.n ? 1 : 0;
        char digits[LIMBS * 10]; to_dec(&absN, digits);
        int L = (int)strlen(digits);
        char intp[LIMBS * 10], frac[LIMBS * 10];
        if (L <= k) {
            strcpy(intp, "0");
            int p = 0; for (int i = 0; i < k - L; i++) frac[p++] = '0';
            strcpy(frac + p, digits);
        } else {
            int ilen = L - k; memcpy(intp, digits, ilen); intp[ilen] = '\0';
            strcpy(frac, digits + ilen);
        }
        int fl = (int)strlen(frac);
        while (fl > 0 && frac[fl - 1] == '0') frac[--fl] = '\0';
        int pos = 0;
        if (N.sign < 0) out[pos++] = '-';
        strcpy(out + pos, intp); pos += (int)strlen(intp);
        if (fl > 0) { out[pos++] = '.'; strcpy(out + pos, frac); }
        else out[pos] = '\0';
    } else {
        char nb[LIMBS * 10], db[LIMBS * 10];
        to_dec(&r->num, nb); to_dec(&r->den, db);
        sprintf(out, "%s/%s", nb, db);
    }
}

static void rat_parse(const char *operand, Rat *r) {
    char buf[LIMBS * 10];
    strncpy(buf, operand, sizeof buf - 1); buf[sizeof buf - 1] = '\0';
    char *slash = strchr(buf, '/');
    if (slash) { *slash = '\0'; from_dec(buf, &r->num); from_dec(slash + 1, &r->den); }
    else { from_dec(buf, &r->num); big_one(&r->den); }
    r->inexact = 0; rat_norm(r);
}

/* ====================== LAYER 4: the stack VM =========================== */
static Rat stack[512];

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
            rat_parse(operand, &stack[sp]); sp++;
        } else if (!strcmp(op, "NEG")) {
            stack[sp - 1].num.sign = -stack[sp - 1].num.sign;
        } else if (!strcmp(op, "CONST")) {
            fp_init();
            Big v;
            if (!strcmp(operand, "pi")) big_copy(&FP_PI, &v);
            else if (!strcmp(operand, "e")) big_copy(&FP_E, &v);
            else { printf("ERR:UNKNOWN\n"); return 0; }
            rat_from_fp(&v, &stack[sp]); sp++;
        } else if (!strcmp(op, "FUNC")) {
            fp_init();
            Rat a = stack[--sp];
            if (!strcmp(operand, "abs")) {
                a.num.sign = a.num.n ? 1 : 0;     /* abs preserves exactness */
                stack[sp] = a; sp++;
                continue;
            }
            if (!strcmp(operand, "fact")) {       /* exact factorial of a whole number */
                Big cap; big_set_int(&cap, 20000);
                if (!big_is_one(&a.den) || a.num.sign < 0 || big_cmp(&a.num, &cap) > 0) { printf("ERR:DOMAIN\n"); return 0; }
                long nn = a.num.n == 0 ? 0 : (long)a.num.d[0];
                Big f; big_one(&f);
                for (long i = 2; i <= nn; i++) { Big im, t; big_set_int(&im, i); big_mul(&f, &im, &t); big_copy(&t, &f); }
                Rat res; big_copy(&f, &res.num); big_one(&res.den); res.inexact = a.inexact;
                stack[sp] = res; sp++;
                continue;
            }
            Big X; fp_from_rat(&a.num, &a.den, &X);
            Big Y; int dom = 0;
            if (!strcmp(operand, "sqrt")) { if (X.sign < 0) dom = 1; else { Big t; big_mul(&X, &SCALE, &t); big_isqrt(&t, &Y); } }
            else if (!strcmp(operand, "exp")) fp_exp(&X, &Y);
            else if (!strcmp(operand, "ln")) { if (X.sign <= 0) dom = 1; else fp_ln(&X, &Y); }
            else if (!strcmp(operand, "log") || !strcmp(operand, "log10")) { if (X.sign <= 0) dom = 1; else { Big l; fp_ln(&X, &l); fp_div(&l, &FP_LN10, &Y); } }
            else if (!strcmp(operand, "sin")) fp_sin(&X, &Y);
            else if (!strcmp(operand, "cos")) fp_cos(&X, &Y);
            else if (!strcmp(operand, "tan")) { Big s, c; fp_sin(&X, &s); fp_cos(&X, &c); if (c.sign == 0) dom = 1; else fp_div(&s, &c, &Y); }
            else if (!strcmp(operand, "asin")) { if (!fp_asin(&X, &Y)) dom = 1; }
            else if (!strcmp(operand, "acos")) { if (!fp_acos(&X, &Y)) dom = 1; }
            else if (!strcmp(operand, "atan")) fp_atan(&X, &Y);
            else if (!strcmp(operand, "sinh")) fp_sinh(&X, &Y);
            else if (!strcmp(operand, "cosh")) fp_cosh(&X, &Y);
            else if (!strcmp(operand, "tanh")) fp_tanh(&X, &Y);
            else if (!strcmp(operand, "rad")) { Big k; fp_div_int(&FP_PI, 180, &k); fp_mul(&X, &k, &Y); }
            else if (!strcmp(operand, "deg")) { Big c180, one1, f180, t; big_set_int(&c180, 180); big_one(&one1); fp_from_rat(&c180, &one1, &f180); fp_mul(&X, &f180, &t); fp_div(&t, &FP_PI, &Y); }
            else if (!strcmp(operand, "cbrt")) {
                if (X.sign == 0) big_zero(&Y);
                else { Big ax; big_copy(&X, &ax); int neg = ax.sign < 0; ax.sign = 1; Big l, l3; fp_ln(&ax, &l); fp_div_int(&l, 3, &l3); fp_exp(&l3, &Y); if (neg) Y.sign = -Y.sign; }
            } else { printf("ERR:UNKNOWN\n"); return 0; }
            if (dom) { printf("ERR:DOMAIN\n"); return 0; }
            rat_from_fp(&Y, &stack[sp]); sp++;
        } else {
            Rat b = stack[--sp];
            Rat a = stack[--sp];
            Rat res; int ok = 1;
            if (!strcmp(op, "ADD")) rat_add(&a, &b, &res);
            else if (!strcmp(op, "SUB")) rat_sub(&a, &b, &res);
            else if (!strcmp(op, "MUL")) rat_mul(&a, &b, &res);
            else if (!strcmp(op, "DIV")) ok = rat_div(&a, &b, &res);
            else if (!strcmp(op, "IDIV")) ok = rat_idiv(&a, &b, &res);
            else if (!strcmp(op, "MOD")) ok = rat_mod(&a, &b, &res);
            else if (!strcmp(op, "POW")) {
                int pr = rat_pow(&a, &b, &res);
                if (pr == 0) { printf("ERR:DIVZERO\n"); return 0; }
                if (pr < 0) {                       /* non-integer exponent: a^b = exp(b ln a) */
                    fp_init();
                    if (a.num.sign < 0) { printf("ERR:DOMAIN\n"); return 0; }
                    if (a.num.sign == 0) {
                        if (b.num.sign <= 0) { printf("ERR:DOMAIN\n"); return 0; }
                        big_zero(&res.num); big_one(&res.den); res.inexact = 1;
                    } else {
                        Big B, Eb, lnB, prod, Y;
                        fp_from_rat(&a.num, &a.den, &B);
                        fp_from_rat(&b.num, &b.den, &Eb);
                        fp_ln(&B, &lnB); fp_mul(&Eb, &lnB, &prod); fp_exp(&prod, &Y);
                        rat_from_fp(&Y, &res);
                    }
                }
            } else { sp += 2; continue; }
            if (!ok) { printf("ERR:DIVZERO\n"); return 0; }
            stack[sp] = res; sp++;
        }
    }
    if (argc > 1) fclose(f);
    if (sp < 1) { fprintf(stderr, "vm_gates: empty stack\n"); return 4; }

    char out[LIMBS * 10];
    Rat top = stack[sp - 1];
    if (top.inexact) {
        Big rn, rd; round_sig_rat(&top.num, &top.den, OUT_SIG, &rn, &rd);
        Rat r2; big_copy(&rn, &r2.num); big_copy(&rd, &r2.den); r2.inexact = 1; rat_norm(&r2);
        rat_to_str(&r2, out);
    } else {
        rat_to_str(&top, out);
    }
    printf("%s\n", out);
    return 0;
}
