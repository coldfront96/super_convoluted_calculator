# ============================================================================
# STAGE 5 of the cathedral: BYZANTINE QUORUM CONSENSUS  (language: awk)
# ----------------------------------------------------------------------------
# Reads one result per line from each independent engine and tallies the votes.
# A value wins if (and only if) it holds a STRICT MAJORITY of all votes. This
# tolerates Byzantine faults: a single lying engine cannot change the answer, it
# can only get itself outvoted and named. If no value commands a strict
# majority, the cathedral panics rather than guess (HARD_RULES #1).
# ============================================================================
{
    verdict[NR] = $0
    tally[$0]++
}
END {
    if (NR == 0) {
        print "CONSENSUS_FAILURE: no votes cast" > "/dev/stderr"
        exit 1
    }

    best = ""
    bestc = 0
    for (k in tally) {
        if (tally[k] > bestc) { bestc = tally[k]; best = k }
    }

    if (bestc * 2 > NR) {
        # We have a strict majority. Name any dissenters for the record.
        if (bestc != NR) {
            print "  consensus: Byzantine dissent detected; majority prevails (" \
                  bestc "/" NR ")" > "/dev/stderr"
            for (i = 1; i <= NR; i++)
                if (verdict[i] != best)
                    print "  consensus: traitor vote ignored -> " verdict[i] > "/dev/stderr"
        }
        print best
        exit 0
    }

    print "CONSENSUS_FAILURE: no strict majority among " NR " votes:" > "/dev/stderr"
    for (i = 1; i <= NR; i++)
        print "  engine " i " voted: " verdict[i] > "/dev/stderr"
    exit 1
}
