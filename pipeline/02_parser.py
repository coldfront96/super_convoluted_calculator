#!/usr/bin/env python3
# ============================================================================
# STAGE 2 of the cathedral: THE PARSER  (language: Python)
# ----------------------------------------------------------------------------
# Consumes CSV tokens from STDIN, runs a textbook recursive-descent parser that
# honours arithmetic precedence and parentheses, and emits an Abstract Syntax
# Tree as JSON. Integers are carried as STRINGS so JSON never mangles 64-bit
# values. We compute nothing here. Computing is for the engines.
# ============================================================================
import sys
import json


def main() -> None:
    rows = [line.rstrip("\n") for line in sys.stdin if line.strip()]
    # rows[0] is the CSV header from the lexer; discard it.
    tokens = []
    for row in rows[1:]:
        parts = (row.split(",", 2) + ["", ""])[:3]
        _idx, typ, val = parts
        tokens.append((typ, val))

    pos = 0

    def peek():
        return tokens[pos][0]

    def advance():
        nonlocal pos
        tok = tokens[pos]
        pos += 1
        return tok

    def expect(tp):
        if peek() != tp:
            sys.exit(f"parse error: expected {tp}, got {peek()}")
        return advance()

    # expr := term (('+'|'-') term)*
    def parse_expr():
        node = parse_term()
        while peek() in ("PLUS", "MINUS"):
            op = advance()[0]
            right = parse_term()
            node = {"op": "add" if op == "PLUS" else "sub", "l": node, "r": right}
        return node

    # term := unary (('*'|'/'|'//'|'%') unary)*
    def parse_term():
        node = parse_unary()
        mapping = {"STAR": "mul", "SLASH": "div", "IDIV": "idiv", "PERCENT": "mod"}
        while peek() in mapping:
            op = advance()[0]
            right = parse_unary()
            node = {"op": mapping[op], "l": node, "r": right}
        return node

    # unary := ('-'|'+') unary | power
    def parse_unary():
        tp = peek()
        if tp == "MINUS":
            advance()
            return {"op": "neg", "x": parse_unary()}
        if tp == "PLUS":
            advance()
            return parse_unary()
        return parse_power()

    # power := atom ('^' unary)?   (right associative; exponent may be unary)
    def parse_power():
        base = parse_atom()
        if peek() == "CARET":
            advance()
            return {"op": "pow", "l": base, "r": parse_unary()}
        return base

    # atom := NUM | '(' expr ')' | IDENT [ '(' expr ')' ]
    def parse_atom():
        tp = peek()
        if tp == "LPAREN":
            advance()
            inner = parse_expr()
            expect("RPAREN")
            return inner
        if tp == "NUM":
            return {"num": advance()[1]}
        if tp == "IDENT":
            name = advance()[1]
            if peek() == "LPAREN":
                advance()
                arg = parse_expr()
                expect("RPAREN")
                return {"func": name, "x": arg}
            return {"const": name}
        sys.exit(f"parse error: unexpected token {tp}")

    ast = parse_expr()
    if peek() != "EOF":
        sys.exit("parse error: trailing tokens after expression")

    json.dump(ast, sys.stdout, separators=(",", ":"))


if __name__ == "__main__":
    main()
