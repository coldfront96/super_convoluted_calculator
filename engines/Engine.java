// ===========================================================================
// ENGINE D of the cathedral: THE SANE ORACLE  (language: Java)
// ---------------------------------------------------------------------------
// Every Byzantine quorum needs one boring, trustworthy participant. This engine
// represents numbers as exact rationals (a pair of BigIntegers) — no gates, no
// ceremony, true arbitrary precision. Its job is to be the grown-up the three
// insane gate-level rational engines must agree with. Division truncates toward
// zero (//, %), remainder takes the dividend's sign, ^ is integer exponentiation.
// Reads bytecode from args[0] (or STDIN), prints the canonical value.
// ===========================================================================
import java.io.BufferedReader;
import java.io.FileReader;
import java.io.InputStreamReader;
import java.math.BigInteger;
import java.util.ArrayDeque;
import java.util.Deque;

public class Engine {
    static final BigInteger TEN = BigInteger.TEN;
    static final BigInteger ONE = BigInteger.ONE;
    static final BigInteger ZERO = BigInteger.ZERO;
    static final BigInteger TWO = BigInteger.valueOf(2);
    static final BigInteger FIVE = BigInteger.valueOf(5);

    // An exact rational, kept reduced with den > 0.
    static final class Rat {
        BigInteger num, den;
        Rat(BigInteger n, BigInteger d) {
            if (d.signum() == 0) throw new ArithmeticException("zero denominator");
            if (d.signum() < 0) { n = n.negate(); d = d.negate(); }
            BigInteger g = n.gcd(d);
            if (!g.equals(ONE) && g.signum() != 0) { n = n.divide(g); d = d.divide(g); }
            num = n; den = d;
        }
    }

    static Rat parse(String s) {
        int i = s.indexOf('/');
        if (i >= 0) return new Rat(new BigInteger(s.substring(0, i)), new BigInteger(s.substring(i + 1)));
        return new Rat(new BigInteger(s), ONE);
    }

    static Rat add(Rat a, Rat b) { return new Rat(a.num.multiply(b.den).add(b.num.multiply(a.den)), a.den.multiply(b.den)); }
    static Rat sub(Rat a, Rat b) { return new Rat(a.num.multiply(b.den).subtract(b.num.multiply(a.den)), a.den.multiply(b.den)); }
    static Rat mul(Rat a, Rat b) { return new Rat(a.num.multiply(b.num), a.den.multiply(b.den)); }
    static Rat div(Rat a, Rat b) {
        if (b.num.signum() == 0) throw new ArithmeticException("div0");
        return new Rat(a.num.multiply(b.den), a.den.multiply(b.num));
    }
    static Rat idiv(Rat a, Rat b) {
        BigInteger n = a.num.multiply(b.den), d = a.den.multiply(b.num);
        if (d.signum() == 0) throw new ArithmeticException("div0");
        // truncate toward zero
        BigInteger[] qr = n.abs().divideAndRemainder(d.abs());
        BigInteger q = qr[0];
        if (n.signum() * d.signum() < 0) q = q.negate();
        return new Rat(q, ONE);
    }
    static Rat mod(Rat a, Rat b) { return sub(a, mul(b, idiv(a, b))); }
    static Rat pow(Rat a, Rat b) {
        if (!b.den.equals(ONE)) { System.out.println("ERR:NONINT"); System.exit(0); }
        int n = b.num.intValueExact();
        if (n == 0) return new Rat(ONE, ONE);
        if (n > 0) return new Rat(a.num.pow(n), a.den.pow(n));
        if (a.num.signum() == 0) throw new ArithmeticException("div0");
        return new Rat(a.den.pow(-n), a.num.pow(-n));
    }

    // canonical: integer | terminating decimal | reduced fraction p/q
    static String toStr(Rat r) {
        if (r.num.signum() == 0) return "0";
        if (r.den.equals(ONE)) return r.num.toString();
        BigInteger q = r.den;
        int a = 0, b = 0;
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
            } else {
                int il = digits.length() - k;
                intp = digits.substring(0, il);
                frac = digits.substring(il);
            }
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

        Deque<Rat> stack = new ArrayDeque<>();
        String line;
        try {
            while ((line = br.readLine()) != null) {
                line = line.trim();
                if (line.isEmpty()) continue;
                String[] p = line.split("\\s+");
                String op = p[0];
                switch (op) {
                    case "PUSH": stack.push(parse(p[1])); break;
                    case "NEG": {
                        Rat x = stack.pop();
                        stack.push(new Rat(x.num.negate(), x.den));
                        break;
                    }
                    default: {
                        Rat b = stack.pop();
                        Rat a = stack.pop();
                        switch (op) {
                            case "ADD": stack.push(add(a, b)); break;
                            case "SUB": stack.push(sub(a, b)); break;
                            case "MUL": stack.push(mul(a, b)); break;
                            case "DIV": stack.push(div(a, b)); break;
                            case "IDIV": stack.push(idiv(a, b)); break;
                            case "MOD": stack.push(mod(a, b)); break;
                            case "POW": stack.push(pow(a, b)); break;
                            default: stack.push(a); stack.push(b);
                        }
                    }
                }
            }
        } catch (ArithmeticException e) {
            System.out.println("ERR:DIVZERO");
            return;
        }
        System.out.println(toStr(stack.peek()));
    }
}
