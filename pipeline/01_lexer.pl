#!/usr/bin/perl
# ============================================================================
# STAGE 1 of the cathedral: THE LEXER  (language: Perl, because regexes)
# ----------------------------------------------------------------------------
# Reads a raw arithmetic expression (from @ARGV or STDIN) and tokenizes it with
# an anchored \G state-machine scan. Emits tokens as CSV to STDOUT, because the
# next stage speaks a completely different language and we insist every handoff
# use a different serialization format. This is the law (see HARD_RULES.md #7).
# ============================================================================
use strict;
use warnings;

my $src = @ARGV ? join(' ', @ARGV) : do { local $/; <STDIN> };
$src = '' unless defined $src;

my %SYM = (
    '+' => 'PLUS',  '-' => 'MINUS', '*' => 'STAR',
    '/' => 'SLASH', '%' => 'PERCENT',
    '(' => 'LPAREN', ')' => 'RPAREN',
);

print "idx,type,value\n";
my $idx = 0;

# Anchored scan: each iteration consumes leading whitespace then exactly one
# token. \G keeps us glued to where the previous match ended.
while ($src =~ /\G\s*(\d+|[-+*\/%()])/gc) {
    my $tok = $1;
    if ($tok =~ /^\d+$/) {
        print "$idx,INT,$tok\n";
    } else {
        print "$idx,$SYM{$tok},\n";
    }
    $idx++;
}

# Anything left that isn't trailing whitespace is a lexical crime.
if ($src =~ /\G\s*(\S)/) {
    die "lex error: unexpected character '$1'\n";
}

print "$idx,EOF,\n";
