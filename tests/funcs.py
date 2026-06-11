#!/usr/bin/env python3
# ============================================================================
# THE NUMERIC SPEC for the function layer, shared by the oracle. Implemented on
# Python's stdlib `decimal` (no external deps). Every transcendental function is
# computed to >=100 digits internally and the public helpers return Fractions
# rounded to ROUND_SIG significant digits, so the engines (which implement the
# same functions independently) only need to match to that many digits.
# ============================================================================
from decimal import Decimal, getcontext, localcontext, ROUND_HALF_EVEN
from fractions import Fraction

getcontext().prec = 120

FUNC_SIG = 80   # function results are correctly rounded to this many sig digits
OUT_SIG = 50    # inexact final answers are displayed to this many sig digits

# pi to 110 digits
PI = Decimal(
    "3."
    "14159265358979323846264338327950288419716939937510"
    "58209749445923078164062862089986280348253421170679"
    "8214808651"
)


class DomainError(Exception):
    pass


def _to_dec(fr: Fraction) -> Decimal:
    return Decimal(fr.numerator) / Decimal(fr.denominator)


def round_sig_fraction(fr: Fraction, n: int) -> Fraction:
    """Round a Fraction to n significant digits, round-half-even -> Fraction."""
    if fr == 0:
        return Fraction(0)
    sign = 1 if fr > 0 else -1
    a = fr if fr > 0 else -fr
    # find E = floor(log10(a))
    E = len(str(a.numerator)) - len(str(a.denominator))
    while Fraction(10) ** E > a:
        E -= 1
    while Fraction(10) ** (E + 1) <= a:
        E += 1
    scale = n - 1 - E
    scaled = a * (Fraction(10) ** scale)
    num, den = scaled.numerator, scaled.denominator
    q, r = divmod(num, den)
    if 2 * r > den or (2 * r == den and q % 2 == 1):
        q += 1
    return sign * Fraction(q) * (Fraction(10) ** (-scale))


def _sin_dec(x: Decimal) -> Decimal:
    two_pi = 2 * PI
    k = (x / two_pi).to_integral_value(rounding=ROUND_HALF_EVEN)
    x = x - k * two_pi
    term = x
    s = x
    x2 = x * x
    n = 1
    tiny = Decimal(10) ** (-(getcontext().prec - 5))
    while abs(term) > tiny:
        term = -term * x2 / Decimal((2 * n) * (2 * n + 1))
        s += term
        n += 1
    return s


def _cos_dec(x: Decimal) -> Decimal:
    two_pi = 2 * PI
    k = (x / two_pi).to_integral_value(rounding=ROUND_HALF_EVEN)
    x = x - k * two_pi
    term = Decimal(1)
    s = Decimal(1)
    x2 = x * x
    n = 1
    tiny = Decimal(10) ** (-(getcontext().prec - 5))
    while abs(term) > tiny:
        term = -term * x2 / Decimal((2 * n - 1) * (2 * n))
        s += term
        n += 1
    return s


def _atan_dec(x: Decimal) -> Decimal:
    neg = x < 0
    if neg:
        x = -x
    # argument reduction: atan(x) = 2*atan(x/(1+sqrt(1+x^2)))
    m = 0
    thresh = Decimal("0.1")
    while x > thresh:
        x = x / (1 + (1 + x * x).sqrt())
        m += 1
    term = x
    s = x
    x2 = x * x
    k = 1
    tiny = Decimal(10) ** (-(getcontext().prec - 5))
    while abs(term) > tiny:
        term = -term * x2
        s += term / Decimal(2 * k + 1)
        k += 1
    s = s * (1 << m)
    return -s if neg else s


def _func_dec(name: str, x: Decimal) -> Decimal:
    if name == "sqrt":
        if x < 0:
            raise DomainError
        return x.sqrt()
    if name == "cbrt":
        if x == 0:
            return Decimal(0)
        r = (abs(x).ln() / 3).exp()
        return -r if x < 0 else r
    if name == "exp":
        return x.exp()
    if name == "ln":
        if x <= 0:
            raise DomainError
        return x.ln()
    if name in ("log", "log10"):
        if x <= 0:
            raise DomainError
        return x.log10()
    if name == "sin":
        return _sin_dec(x)
    if name == "cos":
        return _cos_dec(x)
    if name == "tan":
        c = _cos_dec(x)
        if c == 0:
            raise DomainError
        return _sin_dec(x) / c
    if name == "atan":
        return _atan_dec(x)
    if name == "asin":
        if x < -1 or x > 1:
            raise DomainError
        if x == 1:
            return PI / 2
        if x == -1:
            return -PI / 2
        return _atan_dec(x / (1 - x * x).sqrt())
    if name == "acos":
        if x < -1 or x > 1:
            raise DomainError
        return PI / 2 - _func_dec("asin", x)
    if name == "sinh":
        e = x.exp()
        return (e - 1 / e) / 2
    if name == "cosh":
        e = x.exp()
        return (e + 1 / e) / 2
    if name == "tanh":
        e = x.exp(); f = 1 / e
        return (e - f) / (e + f)
    if name == "rad":
        return x * PI / 180
    if name == "deg":
        return x * 180 / PI
    raise ValueError("unknown function " + name)


def call_func(name: str, arg: Fraction) -> Fraction:
    """Apply a transcendental function, returning a Fraction rounded to FUNC_SIG."""
    with localcontext() as ctx:
        ctx.prec = 120
        d = _func_dec(name, _to_dec(arg))
        return round_sig_fraction(Fraction(d), FUNC_SIG)


def const(name: str) -> Fraction:
    with localcontext() as ctx:
        ctx.prec = 120
        if name == "pi":
            return round_sig_fraction(Fraction(PI), FUNC_SIG)
        if name == "e":
            return round_sig_fraction(Fraction(Decimal(1).exp()), FUNC_SIG)
    raise ValueError("unknown constant " + name)


def pow_inexact(base: Fraction, exp: Fraction) -> Fraction:
    """base ** exp for the inexact (non-integer-exponent) case -> Fraction@FUNC_SIG."""
    with localcontext() as ctx:
        ctx.prec = 120
        b = _to_dec(base)
        if b < 0:
            raise DomainError
        if b == 0:
            if exp <= 0:
                raise DomainError
            return Fraction(0)
        d = (_to_dec(exp) * b.ln()).exp()
        return round_sig_fraction(Fraction(d), FUNC_SIG)
