# ============================================================================
# STAGE 5 of the cathedral: BYZANTINE CONSENSUS  (language: awk)
# ----------------------------------------------------------------------------
# Reads one result per line from each independent engine. Tallies them. If every
# engine agrees, the verdict is emitted. If even one dissents, the cathedral
# panics: we would rather refuse to answer than answer wrongly (HARD_RULES #1).
# ============================================================================
{
    verdict[NR] = $0
    tally[$0]++
}
END {
    distinct = 0
    for (k in tally) distinct++

    if (NR == 0) {
        print "CONSENSUS_FAILURE: no votes cast" > "/dev/stderr"
        exit 1
    }
    if (distinct == 1) {
        print verdict[1]      # unanimous; the people have spoken
        exit 0
    }

    print "CONSENSUS_FAILURE: engines disagree (" distinct " factions):" > "/dev/stderr"
    for (i = 1; i <= NR; i++)
        print "  engine " i " voted: " verdict[i] > "/dev/stderr"
    exit 1
}
