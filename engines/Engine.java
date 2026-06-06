// ===========================================================================
// ENGINE D of the cathedral: THE SANE ORACLE  (language: Java)
// ---------------------------------------------------------------------------
// Every Byzantine quorum needs one boring, trustworthy participant. This engine
// uses java.math.BigInteger — no gates, no ceremony, true arbitrary precision.
// Its entire comedic purpose is to be the grown-up the three insane gate-level
// bignum engines must agree with. Division truncates toward zero and the
// remainder takes the dividend's sign, matching the other engines exactly.
// Reads bytecode from args[0] (or STDIN), prints top of stack in decimal.
// ===========================================================================
import java.io.BufferedReader;
import java.io.FileReader;
import java.io.InputStreamReader;
import java.math.BigInteger;
import java.util.ArrayDeque;
import java.util.Deque;

public class Engine {
    public static void main(String[] args) throws Exception {
        BufferedReader br = (args.length > 0)
                ? new BufferedReader(new FileReader(args[0]))
                : new BufferedReader(new InputStreamReader(System.in));

        Deque<BigInteger> stack = new ArrayDeque<>();
        String line;
        while ((line = br.readLine()) != null) {
            line = line.trim();
            if (line.isEmpty()) continue;
            String[] p = line.split("\\s+");
            String op = p[0];

            switch (op) {
                case "PUSH":
                    stack.push(new BigInteger(p[1]));
                    break;
                case "NEG":
                    stack.push(stack.pop().negate());
                    break;
                default: {
                    BigInteger b = stack.pop();
                    BigInteger a = stack.pop();
                    BigInteger r;
                    switch (op) {
                        case "ADD": r = a.add(b); break;
                        case "SUB": r = a.subtract(b); break;
                        case "MUL": r = a.multiply(b); break;
                        case "DIV":
                            if (b.signum() == 0) { System.out.println("ERR:DIVZERO"); return; }
                            r = a.divide(b); break;   // truncates toward zero
                        case "MOD":
                            if (b.signum() == 0) { System.out.println("ERR:DIVZERO"); return; }
                            r = a.remainder(b); break; // sign of dividend
                        default:
                            stack.push(a);
                            stack.push(b);
                            continue;
                    }
                    stack.push(r);
                }
            }
        }
        System.out.println(stack.peek());
    }
}
