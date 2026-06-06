#!/usr/bin/env bash
# ============================================================================
# THE TEST SUITE — enforces HARD_RULES #1 (correctness) and #2 (it runs).
# Compares calc's output against the independent Python oracle across curated
# and randomly generated expressions, plus the division-by-zero contract.
# ============================================================================
set -u

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CALC="$HERE/calc"
REF="$HERE/tests/reference.py"

echo ">> building all engines..."
make -C "$HERE" all >/dev/null || { echo "BUILD FAILED"; exit 1; }

pass=0
fail=0

check() {
    local expr="$1" expected="$2" got
    got="$("$CALC" "$expr" 2>/dev/null)"
    if [[ "$got" == "$expected" ]]; then
        pass=$((pass + 1))
    else
        fail=$((fail + 1))
        echo "FAIL: '$expr'  expected=$expected  got=$got"
    fi
}

echo ">> curated cases..."
while IFS=$'\t' read -r e v; do
    check "$e" "$v"
done < <(python3 "$REF" --cases)

echo ">> 250 randomized cases..."
while IFS=$'\t' read -r e v; do
    check "$e" "$v"
done < <(python3 "$REF" --gen 250)

echo ">> division-by-zero contract..."
if "$CALC" "1 / 0" >/dev/null 2>&1; then
    echo "FAIL: '1 / 0' should exit non-zero"
    fail=$((fail + 1))
else
    pass=$((pass + 1))
fi
if "$CALC" "5 % (2 - 2)" >/dev/null 2>&1; then
    echo "FAIL: '5 % (2 - 2)' should exit non-zero"
    fail=$((fail + 1))
else
    pass=$((pass + 1))
fi

echo "---------------------------------------------"
echo "PASS=$pass  FAIL=$fail"
[[ $fail -eq 0 ]]
