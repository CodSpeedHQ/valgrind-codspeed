#!/usr/bin/perl
# Check the callgrind output of the rep_bulk test.
# Usage: rep_bulk_check.pl <callgrind-output> on|off

use strict;
use warnings;

my ($file, $mode) = @ARGV;
open(my $fh, '<', $file) or die "cannot open $file: $!\n";
my (@events, @parts);
while (<$fh>) {
    @events = split(' ', $1) if !@events && /^events:\s+(.*)$/;
    if (/^summary:\s+(.*)$/) {
        my %p;
        @p{@events} = split(' ', $1);
        push @parts, \%p;
    }
}
close($fh);
die "expected at least 6 dump parts, got " . @parts . "\n" if @parts < 6;

sub val {
    my ($part, $name) = @_;
    return $parts[$part]{$name} // die "part $part has no event $name\n";
}
# Deltas of a part against part 2 (zero-length rep: pure wrapper cost).
sub delta { my ($part, $name) = @_; return val($part, $name) - val(2, $name); }
# Cold 10 KiB copy: one miss per 64B line (160), well below one per element.
sub cold_misses { return !grep { delta(5, $_) < 160 || delta(5, $_) >= 1024 } @_; }

my $fail = 0;
sub check {
    my ($name, $ok) = @_;
    print "$name: ", ($ok ? "ok" : "MISMATCH"), "\n";
    $fail = 1 unless $ok;
}

check('events', "@events" eq 'Ir Dr Dw I1mr D1mr D1mw ILmr DLmr DLmw Ct Cl');
if ($mode eq 'on') {
    check('one_byte_bulk_cost', val(0, 'Ct') > val(2, 'Ct'));
    check('hot_lines_suppressed',
          !grep { delta(1, $_) != 0 } qw(Dr Dw D1mr D1mw));
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
    check('cold_miss_refs', cold_misses(qw(Dr Dw D1mr D1mw)));
    check('cache_independent_cycles',
          val(1, 'Ct') == val(5, 'Ct') && val(1, 'Cl') == val(5, 'Cl'));
} else {
    check('element_refs', delta(0, 'Dr') == 1 && delta(0, 'Dw') == 1);
    check('legacy_hot_refs', delta(1, 'Dr') == 10240 && delta(1, 'Dw') == 10240);
    check('legacy_cycles', delta(1, 'Ct') > 500000);
    check('legacy_zero_dispatch', delta(0, 'Ct') >= 100);
    check('legacy_backward_refs', delta(3, 'Dr') == 4096 && delta(3, 'Dw') == 4096);
    check('legacy_stos_refs', delta(4, 'Dr') == 0 && delta(4, 'Dw') == 4096);
    check('legacy_cold_refs', delta(5, 'Dr') >= 10240 && cold_misses(qw(D1mr D1mw)));
}
exit $fail;
