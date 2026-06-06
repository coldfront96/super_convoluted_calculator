// ===========================================================================
// ENGINE D of the cathedral: THE SANE ORACLE  (language: Java)
// ---------------------------------------------------------------------------
// Every Byzantine quorum needs one boring, trustworthy participant. This engine
// uses plain `long` arithmetic — no gates, no ceremony. Its entire comedic
// purpose is to be the grown-up in the room that the three insane gate-level
// engines must agree with. Java's `long` is 64-bit two's complement with
// wrapping overflow and truncating division, so its semantics match exactly.
// Reads bytecode from args[0] (or STDIN), prints top of stack.
// ===========================================================================
import java.io.BufferedReader;
import java.io.FileReader;
import java.io.InputStreamReader;
import java.util.ArrayDeque;
import java.util.Deque;

public class Engine {
    public static void main(String[] args) throws Exception {
        BufferedReader br = (args.length > 0)
                ? new BufferedReader(new FileReader(args[0]))
                : new BufferedReader(new InputStreamReader(System.in));

        Deque<Long> stack = new ArrayDeque<>();
        String line;
        while ((line = br.readLine()) != null) {
            line = line.trim();
            if (line.isEmpty()) continue;
            String[] p = line.split("\\s+");
            String op = p[0];

            switch (op) {
                case "PUSH":
                    stack.push(Long.parseLong(p[1]));
                    break;
                case "NEG":
                    stack.push(-stack.pop());
                    break;
                default: {
                    long b = stack.pop();
                    long a = stack.pop();
                    long r;
                    switch (op) {
                        case "ADD": r = a + b; break;
                        case "SUB": r = a - b; break;
                        case "MUL": r = a * b; break;
                        case "DIV":
                            if (b == 0) { System.out.println("ERR:DIVZERO"); return; }
                            r = a / b; break;
                        case "MOD":
                            if (b == 0) { System.out.println("ERR:DIVZERO"); return; }
                            r = a % b; break;
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
