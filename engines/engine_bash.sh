#!/usr/bin/env bash
# ===========================================================================
# ENGINE E of the cathedral: THE GATE-LEVEL VM, IN PURE BASH
# ---------------------------------------------------------------------------
# The slowest and proudest member of the quorum. Arithmetic is built from
# boolean gates (XOR/AND/OR/NOT); the native `+` and `*` operators are NEVER
# used to compute the user's answer. Shifts, indexing and comparisons are
# permitted only as plumbing (loop control and bit packing), consistent with
# the stance the C engine takes around its NAND primitive.
# Reads bytecode from $1 (or stdin), prints the top of stack as a 64-bit int.
# ===========================================================================
set -u

RES=0

# 64-bit ripple-carry adder, one gate-defined full-adder per bit.
g_add() {
    local a=$1 b=$2 r=0 c=0 i ai bi axb s
    for ((i = 0; i < 64; i++)); do
        ai=$(( (a >> i) & 1 ))
        bi=$(( (b >> i) & 1 ))
        axb=$(( ai ^ bi ))              # XOR gate
        s=$(( axb ^ c ))               # sum bit
        c=$(( (ai & bi) | (c & axb) )) # carry: AND/OR gates
        r=$(( r | (s << i) ))          # pack the bit (plumbing)
    done
    RES=$r
}

g_neg() { g_add $(( ~$1 )) 1; }                 # two's complement
g_sub() { local a=$1 b=$2; g_neg "$b"; g_add "$a" "$RES"; }

# Shift-and-add multiplication (low 64 bits).
g_mul() {
    local a=$1 b=$2 r=0 i
    for ((i = 0; i < 64; i++)); do
        if (( (b >> i) & 1 )); then
            g_add "$r" $(( a << i ))
            r=$RES
        fi
    done
    RES=$r
}

QUO=0
REM=0
# Signed division, truncating toward zero, via binary long division.
g_divmod() {
    local a=$1 b=$2 na=0 nb=0 ua ub q=0 r=0 i
    if (( b == 0 )); then return 1; fi
    if (( a < 0 )); then na=1; g_neg "$a"; ua=$RES; else ua=$a; fi
    if (( b < 0 )); then nb=1; g_neg "$b"; ub=$RES; else ub=$b; fi
    for ((i = 63; i >= 0; i--)); do
        r=$(( (r << 1) | ((ua >> i) & 1) ))
        if (( r >= ub )); then
            g_sub "$r" "$ub"; r=$RES
            q=$(( q | (1 << i) ))
        fi
    done
    if (( na ^ nb )); then g_neg "$q"; QUO=$RES; else QUO=$q; fi
    if (( na )); then g_neg "$r"; REM=$RES; else REM=$r; fi
    return 0
}

file="${1:-/dev/stdin}"
declare -a S=()
sp=0

while read -r op v; do
    [[ -z "$op" ]] && continue
    case "$op" in
        PUSH) S[sp]=$v; sp=$((sp + 1)) ;;
        NEG)  g_neg "${S[sp - 1]}"; S[sp - 1]=$RES ;;
        *)
            b=${S[sp - 1]}; a=${S[sp - 2]}; sp=$((sp - 2))
            res=0
            case "$op" in
                ADD) g_add "$a" "$b"; res=$RES ;;
                SUB) g_sub "$a" "$b"; res=$RES ;;
                MUL) g_mul "$a" "$b"; res=$RES ;;
                DIV) if g_divmod "$a" "$b"; then res=$QUO; else echo "ERR:DIVZERO"; exit 0; fi ;;
                MOD) if g_divmod "$a" "$b"; then res=$REM; else echo "ERR:DIVZERO"; exit 0; fi ;;
                *)   S[sp]=$a; sp=$((sp + 1)); S[sp]=$b; sp=$((sp + 1)); continue ;;
            esac
            S[sp]=$res; sp=$((sp + 1))
            ;;
    esac
done < "$file"

echo "${S[sp - 1]}"
