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
    '/' => 'SLASH', '%' => 'PERCENT', '^' => 'CARET',
    '(' => 'LPAREN', ')' => 'RPAREN',
);

print "idx,type,value\n";
my $idx = 0;

# Anchored scan: each iteration consumes leading whitespace then exactly one
# token. \G keeps us glued to where the previous match ended. Numbers may carry
# a decimal point and/or a scientific exponent; '//' is integer division.
while ($src =~ m{
        \G\s*
        ( //                                               # integer division
        | [A-Za-z][A-Za-z0-9]*                             # sqrt, sin, pi, e ...
        | [0-9]+\.?[0-9]*(?:[eE][+-]?[0-9]+)?              # 12, 3.14, 1e9, 2.5e-3
        | \.[0-9]+(?:[eE][+-]?[0-9]+)?                     # .5, .25e3
        | [-+*/%()^] )
    }gcx) {
    my $tok = $1;
    if ($tok =~ /^[0-9.]/) {
        print "$idx,NUM,$tok\n";
    } elsif ($tok =~ /^[A-Za-z]/) {
        print "$idx,IDENT,$tok\n";
    } elsif ($tok eq '//') {
        print "$idx,IDIV,\n";
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
