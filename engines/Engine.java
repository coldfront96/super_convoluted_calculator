// ===========================================================================
// ENGINE D of the cathedral: THE SANE ORACLE  (language: Java)
// ---------------------------------------------------------------------------
// Numbers are exact rationals (BigInteger num/den) tagged exact/inexact. The
// transcendental functions are computed in fixed-point (scaled by 10^WP) on
// BigInteger and rounded to FUNC_SIG significant digits, matching the gate
// engines. No gates here -- this is the boring, trustworthy member of the
// Byzantine quorum the insane engines must agree with.
// Reads bytecode from args[0] (or STDIN); prints the canonical value.
// ===========================================================================
import java.io.BufferedReader;
import java.io.FileReader;
import java.io.InputStreamReader;
import java.math.BigInteger;
import java.util.ArrayDeque;
import java.util.Deque;

public class Engine {
    static final BigInteger ZERO = BigInteger.ZERO, ONE = BigInteger.ONE, TWO = BigInteger.valueOf(2);
    static final BigInteger FIVE = BigInteger.valueOf(5), TEN = BigInteger.TEN;

    static final int WP = 120, FUNC_SIG = 80, OUT_SIG = 50;
    static final String PI_STR =
        "3.14159265358979323846264338327950288419716939937510"
      + "5820974944592307816406286208998628034825342117067982148086513282306647093844609550582231725359408128";

    static BigInteger SCALE, FP_PI, FP_2PI, FP_LN2, FP_LN10, FP_E;
    static boolean fpReady = false;

    // ---------- exact rational ----------
    static final class Rat {
        BigInteger num, den; boolean inexact;
        Rat(BigInteger n, BigInteger d, boolean x) {
            if (d.signum() < 0) { n = n.negate(); d = d.negate(); }
            if (n.signum() == 0) d = ONE;
            else { BigInteger g = n.gcd(d); if (!g.equals(ONE)) { n = n.divide(g); d = d.divide(g); } }
            num = n; den = d; inexact = x;
        }
    }
    static Rat rat(BigInteger n, BigInteger d, boolean x) { return new Rat(n, d, x); }

    static Rat parse(String s) {
        int i = s.indexOf('/');
        boolean inx = false;
        if (i >= 0) return rat(new BigInteger(s.substring(0, i)), new BigInteger(s.substring(i + 1)), inx);
        return rat(new BigInteger(s), ONE, inx);
    }
    static Rat add(Rat a, Rat b) { return rat(a.num.multiply(b.den).add(b.num.multiply(a.den)), a.den.multiply(b.den), a.inexact || b.inexact); }
    static Rat sub(Rat a, Rat b) { return rat(a.num.multiply(b.den).subtract(b.num.multiply(a.den)), a.den.multiply(b.den), a.inexact || b.inexact); }
    static Rat mul(Rat a, Rat b) { return rat(a.num.multiply(b.num), a.den.multiply(b.den), a.inexact || b.inexact); }
    static Rat div(Rat a, Rat b) {
        if (b.num.signum() == 0) throw new ArithmeticException();
        return rat(a.num.multiply(b.den), a.den.multiply(b.num), a.inexact || b.inexact);
    }
    static Rat idiv(Rat a, Rat b) {
        BigInteger nn = a.num.multiply(b.den), dd = a.den.multiply(b.num);
        if (dd.signum() == 0) throw new ArithmeticException();
        BigInteger q = nn.abs().divide(dd.abs());
        if (nn.signum() * dd.signum() < 0) q = q.negate();
        return rat(q, ONE, a.inexact || b.inexact);
    }
    static Rat mod(Rat a, Rat b) { return sub(a, mul(b, idiv(a, b))); }
    static Rat pow(Rat a, Rat b) {           // integer exponent only; caller handles non-integer
        boolean inx = a.inexact || b.inexact;
        int n = b.num.intValueExact();
        if (n == 0) return rat(ONE, ONE, inx);
        if (n > 0) return rat(a.num.pow(n), a.den.pow(n), inx);
        if (a.num.signum() == 0) throw new ArithmeticException();
        return rat(a.den.pow(-n), a.num.pow(-n), inx);
    }

    // ---------- significant-digit rounding ----------
    static int cmp10e(int e, BigInteger den, BigInteger num) {
        if (e >= 0) return TEN.pow(e).multiply(den).compareTo(num);
        return den.compareTo(TEN.pow(-e).multiply(num));
    }
    static BigInteger[] roundSig(BigInteger numIn, BigInteger denIn, int n) {
        if (numIn.signum() == 0) return new BigInteger[]{ZERO, ONE};
        int sign = numIn.signum();
        BigInteger num = numIn.abs(), den = denIn.abs();
        int e = num.toString().length() - den.toString().length();
        while (cmp10e(e, den, num) > 0) e--;
        while (cmp10e(e + 1, den, num) <= 0) e++;
        int scale = n - 1 - e;
        BigInteger snum, sden;
        if (scale >= 0) { snum = num.multiply(TEN.pow(scale)); sden = den; }
        else { snum = num; sden = den.multiply(TEN.pow(-scale)); }
        BigInteger[] qr = snum.divideAndRemainder(sden);
        BigInteger q = qr[0], r = qr[1];
        int c = r.shiftLeft(1).compareTo(sden);
        if (c > 0 || (c == 0 && q.testBit(0))) q = q.add(ONE);
        int ex = e - n + 1;
        BigInteger rnum, rden;
        if (ex >= 0) { rnum = q.multiply(TEN.pow(ex)); rden = ONE; } else { rnum = q; rden = TEN.pow(-ex); }
        if (sign < 0) rnum = rnum.negate();
        return new BigInteger[]{rnum, rden};
    }

    // ---------- fixed-point transcendentals (scaled by 10^WP) ----------
    static BigInteger fpFromRat(BigInteger num, BigInteger den) { return num.multiply(SCALE).divide(den); }
    static BigInteger fpMul(BigInteger a, BigInteger b) { return a.multiply(b).divide(SCALE); }
    static BigInteger fpDiv(BigInteger a, BigInteger b) { return a.multiply(SCALE).divide(b); }
    static BigInteger fpDivInt(BigInteger a, long k) { return a.divide(BigInteger.valueOf(k)); }
    static BigInteger fpConst(String s) {
        int dot = s.indexOf('.');
        String ip = dot >= 0 ? s.substring(0, dot) : s;
        String fp = dot >= 0 ? s.substring(dot + 1) : "";
        return fpFromRat(new BigInteger(ip + fp), TEN.pow(fp.length()));
    }
    static BigInteger fpAtanh(BigInteger y) {
        BigInteger sum = y, term = y, y2 = fpMul(y, y);
        long k = 1;
        while (term.signum() != 0 && k < 200000) {
            term = fpMul(term, y2);
            sum = sum.add(fpDivInt(term, 2 * k + 1));
            k++;
        }
        return sum;
    }
    static BigInteger fpExp(BigInteger x) {
        BigInteger r = x; int m = 0;
        while (r.abs().compareTo(SCALE) >= 0 && m <= 8192) { r = fpDivInt(r, 2); m++; }
        BigInteger sum = SCALE, term = SCALE;
        long k = 1;
        while (term.signum() != 0 && k < 200000) {
            term = fpDivInt(fpMul(term, r), k);
            sum = sum.add(term);
            k++;
        }
        for (int i = 0; i < m; i++) sum = fpMul(sum, sum);
        return sum;
    }
    static BigInteger fpLn(BigInteger x) {
        BigInteger xx = x; long e2 = 0;
        BigInteger twoS = SCALE.shiftLeft(1);
        while (xx.compareTo(twoS) >= 0) { xx = fpDivInt(xx, 2); e2++; }
        while (xx.compareTo(SCALE) < 0) { xx = xx.shiftLeft(1); e2--; }
        BigInteger y = fpDiv(xx.subtract(SCALE), xx.add(SCALE));
        BigInteger at = fpAtanh(y);
        return at.shiftLeft(1).add(FP_LN2.multiply(BigInteger.valueOf(e2)));
    }
    static BigInteger roundFpToInt(BigInteger v) {
        BigInteger[] qr = v.divideAndRemainder(SCALE);
        if (qr[1].abs().shiftLeft(1).compareTo(SCALE) >= 0)
            return qr[0].add(BigInteger.valueOf(v.signum() < 0 ? -1 : 1));
        return qr[0];
    }
    static BigInteger fpReduce(BigInteger x) {
        BigInteger k = roundFpToInt(fpDiv(x, FP_2PI));
        return x.subtract(k.multiply(FP_2PI));
    }
    static BigInteger fpSin(BigInteger x) {
        BigInteger xr = fpReduce(x), sum = xr, term = xr, x2 = fpMul(xr, xr);
        long k = 1;
        while (term.signum() != 0 && k < 200000) {
            term = fpDivInt(fpMul(term, x2), (2 * k) * (2 * k + 1)).negate();
            sum = sum.add(term);
            k++;
        }
        return sum;
    }
    static BigInteger fpCos(BigInteger x) {
        BigInteger xr = fpReduce(x), sum = SCALE, term = SCALE, x2 = fpMul(xr, xr);
        long k = 1;
        while (term.signum() != 0 && k < 200000) {
            term = fpDivInt(fpMul(term, x2), (2 * k - 1) * (2 * k)).negate();
            sum = sum.add(term);
            k++;
        }
        return sum;
    }
    static BigInteger fpSqrt(BigInteger x) { return x.multiply(SCALE).sqrt(); }
    static BigInteger fpAtan(BigInteger x) {
        boolean neg = x.signum() < 0;
        BigInteger xx = x.abs();
        BigInteger thresh = SCALE.divide(TEN);
        int m = 0;
        while (xx.compareTo(thresh) > 0 && m < 4096) {
            BigInteger rt = fpSqrt(SCALE.add(fpMul(xx, xx)));
            xx = fpDiv(xx, SCALE.add(rt));
            m++;
        }
        BigInteger sum = xx, term = xx, x2 = fpMul(xx, xx);
        long k = 1;
        while (term.signum() != 0 && k < 200000) {
            term = fpMul(term, x2).negate();
            sum = sum.add(fpDivInt(term, 2 * k + 1));
            k++;
        }
        for (int i = 0; i < m; i++) sum = sum.shiftLeft(1);
        return neg ? sum.negate() : sum;
    }
    static BigInteger fpAsin(BigInteger x) {      // null on domain error
        BigInteger ax = x.abs();
        int c = ax.compareTo(SCALE);
        if (c > 0) return null;
        if (c == 0) { BigInteger h = FP_PI.divide(TWO); return x.signum() < 0 ? h.negate() : h; }
        BigInteger d = SCALE.subtract(fpMul(x, x));
        return fpAtan(fpDiv(x, fpSqrt(d)));
    }
    static BigInteger fpAcos(BigInteger x) {
        BigInteger a = fpAsin(x);
        if (a == null) return null;
        return FP_PI.divide(TWO).subtract(a);
    }
    static BigInteger fpSinh(BigInteger x) { BigInteger e1 = fpExp(x), e2 = fpExp(x.negate()); return fpDivInt(e1.subtract(e2), 2); }
    static BigInteger fpCosh(BigInteger x) { BigInteger e1 = fpExp(x), e2 = fpExp(x.negate()); return fpDivInt(e1.add(e2), 2); }
    static BigInteger fpTanh(BigInteger x) { BigInteger e1 = fpExp(x), e2 = fpExp(x.negate()); return fpDiv(e1.subtract(e2), e1.add(e2)); }

    static BigInteger fpAsinh(BigInteger x) { return fpLn(x.add(fpSqrt(SCALE.add(fpMul(x, x))))); }
    static BigInteger fpAcosh(BigInteger x) { if (x.compareTo(SCALE) < 0) return null; return fpLn(x.add(fpSqrt(fpMul(x, x).subtract(SCALE)))); }
    static BigInteger fpAtanhU(BigInteger x) { if (x.abs().compareTo(SCALE) >= 0) return null; return fpDivInt(fpLn(fpDiv(SCALE.add(x), SCALE.subtract(x))), 2); }
    static BigInteger fpLogb(BigInteger x, BigInteger b) { if (x.signum() <= 0 || b.signum() <= 0) return null; BigInteger lb = fpLn(b); if (lb.signum() == 0) return null; return fpDiv(fpLn(x), lb); }
    static BigInteger fpHypot(BigInteger x, BigInteger y) { return fpSqrt(fpMul(x, x).add(fpMul(y, y))); }
    static BigInteger fpAtan2(BigInteger y, BigInteger x) {
        if (x.signum() > 0) return fpAtan(fpDiv(y, x));
        if (x.signum() < 0) { BigInteger a = fpAtan(fpDiv(y, x)); return y.signum() >= 0 ? a.add(FP_PI) : a.subtract(FP_PI); }
        if (y.signum() > 0) return FP_PI.divide(TWO);
        if (y.signum() < 0) return FP_PI.divide(TWO).negate();
        return ZERO;
    }

    static void fpInit() {
        if (fpReady) return;
        fpReady = true;
        SCALE = TEN.pow(WP);
        FP_PI = fpConst(PI_STR);
        FP_2PI = FP_PI.shiftLeft(1);
        FP_LN2 = fpAtanh(fpDivInt(SCALE, 3)).shiftLeft(1);
        FP_LN10 = fpLn(fpFromRat(TEN, ONE));
        FP_E = fpExp(SCALE);
    }
    static Rat ratFromFp(BigInteger v) {
        BigInteger[] nd = roundSig(v, SCALE, FUNC_SIG);
        return rat(nd[0], nd[1], true);
    }

    // ---------- canonical output ----------
    static String toStr(Rat r) {
        if (r.num.signum() == 0) return "0";
        if (r.den.equals(ONE)) return r.num.toString();
        BigInteger q = r.den; int a = 0, b = 0;
        while (q.mod(TWO).signum() == 0) { q = q.divide(TWO); a++; }
        while (q.mod(FIVE).signum() == 0) { q = q.divide(FIVE); b++; }
        if (q.equals(ONE)) {
            int k = Math.max(a, b);
            BigInteger scale = TEN.pow(k).divide(r.den);
            BigInteger N = r.num.multiply(scale);
            boolean neg = N.signum() < 0;
            String digits = N.abs().toString();
            String intp, frac;
            if (digits.length() <= k) {
                intp = "0";
                StringBuilder f = new StringBuilder();
                for (int z = 0; z < k - digits.length(); z++) f.append('0');
                frac = f.append(digits).toString();
            } else { int il = digits.length() - k; intp = digits.substring(0, il); frac = digits.substring(il); }
            int fl = frac.length();
            while (fl > 0 && frac.charAt(fl - 1) == '0') fl--;
            frac = frac.substring(0, fl);
            return (neg ? "-" : "") + intp + (frac.isEmpty() ? "" : "." + frac);
        }
        return r.num + "/" + r.den;
    }

    public static void main(String[] args) throws Exception {
        BufferedReader br = (args.length > 0)
                ? new BufferedReader(new FileReader(args[0]))
                : new BufferedReader(new InputStreamReader(System.in));
        Deque<Rat> st = new ArrayDeque<>();
        String line;
        try {
            while ((line = br.readLine()) != null) {
                line = line.trim();
                if (line.isEmpty()) continue;
                String[] p = line.split("\\s+");
                String op = p[0];
                switch (op) {
                    case "PUSH": st.push(parse(p[1])); break;
                    case "NEG": { Rat x = st.pop(); st.push(rat(x.num.negate(), x.den, x.inexact)); break; }
                    case "CONST": {
                        fpInit();
                        BigInteger cv;
                        if (p[1].equals("pi")) cv = FP_PI;
                        else if (p[1].equals("e")) cv = FP_E;
                        else { System.out.println("ERR:UNKNOWN"); return; }
                        st.push(ratFromFp(cv));
                        break;
                    }
                    case "FUNC": {
                        if (p.length > 2 && p[2].equals("2")) {
                            fpInit();
                            Rat b = st.pop(), a = st.pop();
                            Rat res;
                            switch (p[1]) {
                                case "gcd": case "lcm": {
                                    if (!a.den.equals(ONE) || !b.den.equals(ONE)) { System.out.println("ERR:DOMAIN"); return; }
                                    BigInteger g = a.num.gcd(b.num), num;
                                    if (p[1].equals("gcd")) num = g;
                                    else if (a.num.signum() == 0 || b.num.signum() == 0) num = ZERO;
                                    else num = a.num.multiply(b.num).abs().divide(g);
                                    res = rat(num, ONE, a.inexact || b.inexact); break;
                                }
                                case "max": case "min": {
                                    int c = a.num.multiply(b.den).compareTo(b.num.multiply(a.den));
                                    res = ((p[1].equals("max") && c >= 0) || (p[1].equals("min") && c <= 0)) ? a : b; break;
                                }
                                case "comb": case "perm": {
                                    if (!a.den.equals(ONE) || !b.den.equals(ONE) || a.num.signum() < 0 || b.num.signum() < 0 || a.num.compareTo(BigInteger.valueOf(20000)) > 0) { System.out.println("ERR:DOMAIN"); return; }
                                    long nn = a.num.longValueExact(), rr = b.num.longValueExact();
                                    BigInteger num;
                                    if (rr > nn) num = ZERO;
                                    else {
                                        BigInteger pp = ONE;
                                        for (long i = 0; i < rr; i++) pp = pp.multiply(BigInteger.valueOf(nn - i));
                                        if (p[1].equals("perm")) num = pp;
                                        else { BigInteger rf = ONE; for (long i = 2; i <= rr; i++) rf = rf.multiply(BigInteger.valueOf(i)); num = pp.divide(rf); }
                                    }
                                    res = rat(num, ONE, a.inexact || b.inexact); break;
                                }
                                case "log": { BigInteger r = fpLogb(fpFromRat(a.num, a.den), fpFromRat(b.num, b.den)); if (r == null) { System.out.println("ERR:DOMAIN"); return; } res = ratFromFp(r); break; }
                                case "hypot": res = ratFromFp(fpHypot(fpFromRat(a.num, a.den), fpFromRat(b.num, b.den))); break;
                                case "atan2": res = ratFromFp(fpAtan2(fpFromRat(a.num, a.den), fpFromRat(b.num, b.den))); break;
                                default: System.out.println("ERR:UNKNOWN"); return;
                            }
                            st.push(res); break;
                        }
                        Rat a = st.pop();
                        if (p[1].equals("abs")) { st.push(rat(a.num.abs(), a.den, a.inexact)); break; }
                        if (p[1].equals("fact")) {
                            if (!a.den.equals(ONE) || a.num.signum() < 0 || a.num.compareTo(BigInteger.valueOf(20000)) > 0) { System.out.println("ERR:DOMAIN"); return; }
                            int n = a.num.intValueExact();
                            BigInteger f = ONE;
                            for (int i = 2; i <= n; i++) f = f.multiply(BigInteger.valueOf(i));
                            st.push(rat(f, ONE, a.inexact));
                            break;
                        }
                        fpInit();
                        BigInteger x = fpFromRat(a.num, a.den), y;
                        switch (p[1]) {
                            case "sqrt": if (x.signum() < 0) { System.out.println("ERR:DOMAIN"); return; } y = x.multiply(SCALE).sqrt(); break;
                            case "exp": y = fpExp(x); break;
                            case "ln": if (x.signum() <= 0) { System.out.println("ERR:DOMAIN"); return; } y = fpLn(x); break;
                            case "log": case "log10": if (x.signum() <= 0) { System.out.println("ERR:DOMAIN"); return; } y = fpDiv(fpLn(x), FP_LN10); break;
                            case "sin": y = fpSin(x); break;
                            case "cos": y = fpCos(x); break;
                            case "tan": { BigInteger c = fpCos(x); if (c.signum() == 0) { System.out.println("ERR:DOMAIN"); return; } y = fpDiv(fpSin(x), c); break; }
                            case "asin": { BigInteger r = fpAsin(x); if (r == null) { System.out.println("ERR:DOMAIN"); return; } y = r; break; }
                            case "acos": { BigInteger r = fpAcos(x); if (r == null) { System.out.println("ERR:DOMAIN"); return; } y = r; break; }
                            case "atan": y = fpAtan(x); break;
                            case "sinh": y = fpSinh(x); break;
                            case "cosh": y = fpCosh(x); break;
                            case "tanh": y = fpTanh(x); break;
                            case "asinh": y = fpAsinh(x); break;
                            case "acosh": { BigInteger r = fpAcosh(x); if (r == null) { System.out.println("ERR:DOMAIN"); return; } y = r; break; }
                            case "atanh": { BigInteger r = fpAtanhU(x); if (r == null) { System.out.println("ERR:DOMAIN"); return; } y = r; break; }
                            case "rad": y = fpMul(x, FP_PI.divide(BigInteger.valueOf(180))); break;
                            case "deg": { BigInteger f180 = fpFromRat(BigInteger.valueOf(180), ONE); y = fpDiv(fpMul(x, f180), FP_PI); break; }
                            case "cbrt":
                                if (x.signum() == 0) y = ZERO;
                                else { boolean neg = x.signum() < 0; BigInteger r = fpExp(fpDivInt(fpLn(x.abs()), 3)); y = neg ? r.negate() : r; }
                                break;
                            default: System.out.println("ERR:UNKNOWN"); return;
                        }
                        st.push(ratFromFp(y));
                        break;
                    }
                    default: {
                        Rat b = st.pop(), a = st.pop();
                        switch (op) {
                            case "ADD": st.push(add(a, b)); break;
                            case "SUB": st.push(sub(a, b)); break;
                            case "MUL": st.push(mul(a, b)); break;
                            case "DIV": st.push(div(a, b)); break;
                            case "IDIV": st.push(idiv(a, b)); break;
                            case "MOD": st.push(mod(a, b)); break;
                            case "POW":
                                if (b.den.equals(ONE)) { st.push(pow(a, b)); }
                                else {                       // non-integer exponent: exp(b ln a)
                                    fpInit();
                                    if (a.num.signum() < 0) { System.out.println("ERR:DOMAIN"); return; }
                                    if (a.num.signum() == 0) {
                                        if (b.num.signum() <= 0) { System.out.println("ERR:DOMAIN"); return; }
                                        st.push(rat(ZERO, ONE, true));
                                    } else {
                                        BigInteger bb = fpFromRat(a.num, a.den), eb = fpFromRat(b.num, b.den);
                                        st.push(ratFromFp(fpExp(fpMul(eb, fpLn(bb)))));
                                    }
                                }
                                break;
                            default: st.push(a); st.push(b);
                        }
                    }
                }
            }
        } catch (ArithmeticException e) {
            System.out.println("ERR:DIVZERO");
            return;
        }
        Rat top = st.peek();
        if (top.inexact) {
            BigInteger[] nd = roundSig(top.num, top.den, OUT_SIG);
            System.out.println(toStr(rat(nd[0], nd[1], true)));
        } else {
            System.out.println(toStr(top));
        }
    }
}
