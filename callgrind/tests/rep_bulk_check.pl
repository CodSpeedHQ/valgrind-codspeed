#!/usr/bin/perl
# Check the callgrind output of the rep_bulk test.
# Usage: rep_bulk_check.pl <callgrind-output> on|off
use strict;
use warnings;

my ($file, $mode) = @ARGV;
die "usage: rep_bulk_check.pl <output> on|off\n"
    unless defined $file && defined $mode && ($mode eq 'on' || $mode eq 'off');

open(my $fh, '<', $file) or die "cannot open $file: $!\n";
my @events;
my @parts;
while (my $line = <$fh>) {
    chomp $line;
    if ($line =~ /^events:\s+(.*)$/) {
        @events = split(' ', $1) unless @events;
    } elsif ($line =~ /^summary:\s+(.*)$/) {
        my @v = split(' ', $1);
        my %p;
        for my $i (0 .. $#events) {
            $p{$events[$i]} = $v[$i];
        }
        push @parts, \%p;
    }
}
close($fh);

my @fail;
sub check {
    my ($name, $ok) = @_;
    if ($ok) {
        print "$name: ok\n";
    } else {
        print "$name: MISMATCH\n";
        push @fail, $name;
    }
}
sub val {
    my ($part, $name) = @_;
    die "part $part has no event $name\n" unless defined $parts[$part]{$name};
    return $parts[$part]{$name} + 0;
}

my $expected_events = 'Ir Dr Dw I1mr D1mr D1mw ILmr DLmr DLmw Ct Cl';
die "expected at least 7 dump parts, got ".scalar(@parts)."\n" if @parts < 7;

check('events', join(' ', @events) eq $expected_events);

# Deltas of a part against part 2 (zero-length rep: pure wrapper cost).
sub delta { my ($part, $name) = @_; return val($part, $name) - val(2, $name); }

if ($mode eq 'on') {
    check('one_byte_bulk_cost', val(0, 'Ct') > val(2, 'Ct'));

    check('hot_lines_suppressed',
          delta(1, 'Dr') == 0 && delta(1, 'Dw') == 0
          && delta(1, 'D1mr') == 0 && delta(1, 'D1mw') == 0);

    # Instruction-side counters are never modeled: one element executes one
    # more IMark than the zero-length dispatch (1 vs 2 dispatches).
    check('instruction_refs_kept',
          delta(0, 'Ir') == 1 && delta(1, 'Ir') == 10240);

    # The aligned curve prices 10 KiB at about 19K centicycles, the
    # misaligned one at about 38K: the class must come from the first element.
    my $ct = delta(1, 'Ct');
    check('bulk_cycles', $ct > 0 && $ct < 25000 && delta(1, 'Cl') > 0);

    # A 4 KiB backward copy costs more than the 10 KiB forward one only if
    # every element keeps the backward curve.
    check('backward',
          delta(3, 'Ct') > delta(1, 'Ct')
          && delta(3, 'Dr') == 0 && delta(3, 'Dw') == 0);

    check('stos',
          val(4, 'Ct') > val(2, 'Ct')
          && delta(4, 'Dr') == 0 && delta(4, 'Dw') == 0);

    check('self',
          val(5, 'Ct') > val(2, 'Ct')
          && delta(5, 'Dr') == 0 && delta(5, 'Dw') == 0);

    my $dr  = delta(6, 'Dr');
    my $dw  = delta(6, 'Dw');
    my $d1r = delta(6, 'D1mr');
    my $d1w = delta(6, 'D1mw');
    check('cold_miss_refs',
          $dr  >= 160 && $dr  < 1024 &&
          $dw  >= 160 && $dw  < 1024 &&
          $d1r >= 160 && $d1r < 1024 &&
          $d1w >= 160 && $d1w < 1024);

    check('cache_independent_cycles',
          val(1, 'Ct') == val(6, 'Ct') && val(1, 'Cl') == val(6, 'Cl'));
} else {
    check('element_refs', delta(0, 'Dr') == 1 && delta(0, 'Dw') == 1);

    check('legacy_hot_refs', delta(1, 'Dr') == 10240 && delta(1, 'Dw') == 10240);

    check('legacy_cycles', delta(1, 'Ct') > 500000);

    check('legacy_zero_dispatch', delta(0, 'Ct') >= 100);

    check('legacy_backward_refs', delta(3, 'Dr') == 4096 && delta(3, 'Dw') == 4096);

    check('legacy_stos_refs', delta(4, 'Dr') == 0 && delta(4, 'Dw') == 4096);


    my $dr  = delta(6, 'Dr');
    my $d1r = delta(6, 'D1mr');
    my $d1w = delta(6, 'D1mw');
    check('legacy_cold_refs',
          $dr >= 10240 && $d1r >= 160 && $d1r < 1024 &&
          $d1w >= 160 && $d1w < 1024);
}

if (@fail) {
    print "FAILED: ", join(', ', @fail), "\n";
    exit 1;
}
exit 0;
