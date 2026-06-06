#!/usr/bin/perl
# Native HTTP service wrapping the Perl lexer stage.
# POST /        -> body (the expression) piped to pipeline/01_lexer.pl
# GET  /health  -> "ok"
use strict;
use warnings;
use IO::Socket::INET;
use IPC::Open2;

my $port = shift @ARGV || 7001;
my $srv = IO::Socket::INET->new(
    LocalAddr => '127.0.0.1', LocalPort => $port,
    Listen => 32, Reuse => 1, Proto => 'tcp',
) or die "lexer_service: cannot bind $port: $!\n";

sub run_stage {
    my ($body) = @_;
    my ($rd, $wr);
    my $pid = open2($rd, $wr, 'perl', 'pipeline/01_lexer.pl');
    print $wr $body;
    close $wr;
    local $/;
    my $out = <$rd>;
    close $rd;
    waitpid($pid, 0);
    return defined $out ? $out : '';
}

while (my $cli = $srv->accept) {
    my $reqline = <$cli>;
    my $clen = 0;
    while (my $line = <$cli>) {
        last if $line eq "\r\n";
        $clen = $1 if $line =~ /Content-Length:\s*(\d+)/i;
    }
    if (defined $reqline && $reqline =~ m{^GET\s+/health}) {
        print $cli "HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\nok";
        close $cli;
        next;
    }
    my $body = '';
    read($cli, $body, $clen) if $clen;
    my $out = run_stage($body);
    print $cli "HTTP/1.1 200 OK\r\nContent-Length: " . length($out)
             . "\r\nConnection: close\r\n\r\n" . $out;
    close $cli;
}
