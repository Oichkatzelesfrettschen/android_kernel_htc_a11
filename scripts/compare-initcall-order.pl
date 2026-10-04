#!/usr/bin/env perl
# SPDX-License-Identifier: GPL-2.0
# Compare the initcall order of a native System.map with a ThinLTO System.map
# or with the order script scripts/generate-lto-initcall-order.pl wrote.
# Each side is reduced to (level, function) rows in address or script order,
# written to OUTPUT_PREFIX.control.tsv and OUTPUT_PREFIX.thinlto.tsv, and the
# script exits non-zero at the first row that differs.

use strict;
use warnings;

@ARGV == 3 or die "usage: $0 CONTROL_MAP THINLTO_MAP OUTPUT_PREFIX\n";
my ($control_path, $thin_path, $output_prefix) = @ARGV;

sub read_map {
	my ($path) = @_;
	open my $input, '<', $path or die "cannot read $path: $!\n";
	my (%boundaries, @symbols);
	while (my $line = <$input>) {
		my ($address, $name) = $line =~ /^([0-9a-fA-F]+)\s+\S\s+(\S+)$/;
		next unless defined $name;
		if ($name =~ /^__(?:initcall(?:[0-7]|rootfs)?|con_initcall|security_initcall)_(?:start|end)$/) {
			$boundaries{$name} = hex($address);
		} elsif ($name =~ /^__initcall_/ || $name =~ /^__initcall__/) {
			push @symbols, [hex($address), $name];
		}
	}
	close $input or die "cannot close $path: $!\n";
	for my $required (qw(__initcall_start __initcall0_start __initcall1_start
		__initcall2_start __initcall3_start __initcall4_start
		__initcall5_start __initcallrootfs_start __initcall6_start
		__initcall7_start __initcall_end __con_initcall_start
		__con_initcall_end __security_initcall_start
		__security_initcall_end)) {
		exists $boundaries{$required} or die "$path: missing $required\n";
	}
	my @ranges = (
		['early', '__initcall_start', '__initcall0_start'],
		(map { ["$_", "__initcall${_}_start", "__initcall" . ($_ + 1) . "_start"] } 0 .. 4),
		['5', '__initcall5_start', '__initcallrootfs_start'],
		['rootfs', '__initcallrootfs_start', '__initcall6_start'],
		['6', '__initcall6_start', '__initcall7_start'],
		['7', '__initcall7_start', '__initcall_end'],
		['con', '__con_initcall_start', '__con_initcall_end'],
		['sec', '__security_initcall_start', '__security_initcall_end'],
	);
	my @rows;
	for my $symbol (@symbols) {
		my ($address, $name) = @$symbol;
		my @matching = grep {
			$address >= $boundaries{$_->[1]} && $address < $boundaries{$_->[2]}
		} @ranges;
		@matching == 1 or die "$path: $name at $address belongs to " .
			scalar(@matching) . " ranges\n";
		my $level = $matching[0][0];
		my $function = $name;
		if ($function =~ /^__initcall__(?:kmod_[A-Za-z0-9_]+)__(?:\d+)_(?:\d+)_(.+)$/) {
			$function = $1;
		} else {
			$function =~ s/^__initcall_// or die "$path: malformed $name\n";
		}
		my $suffix = $level eq 'sec' || $level eq 'con' ? '' : $level;
		if ($suffix ne '') {
			$function =~ s/\Q$suffix\Es?$//
				or die "$path: $name lacks level $level\n";
			$level .= 's' if $name =~ /\Q$suffix\Es$/;
		}
		$function =~ s/(?:con|sec)$// if $level eq 'con' || $level eq 'sec';
		length($function) or die "$path: empty function in $name\n";
		push @rows, [$level, $function, $name];
	}
	return \@rows;
}

sub read_order_script {
	my ($path) = @_;
	open my $input, '<', $path or die "cannot read $path: $!\n";
	my @rows;
	while (my $line = <$input>) {
		next unless $line =~ /KEEP/;
		my ($section, $name) = $line =~
			/^\s*KEEP\(\*\(\.(initcall(?:early|rootfs|[0-7]s?)|con_initcall|security_initcall)\.init\.\.([^()]+)\)\)\s*$/;
		defined $name or die "$path: malformed initcall order line $line";
		my $level = $section eq 'con_initcall' ? 'con' :
			$section eq 'security_initcall' ? 'sec' :
			$section =~ /^initcall(.+)$/ ? $1 : die "$path: unknown $section\n";
		my ($function) = $name =~
			/^__initcall__kmod_[A-Za-z0-9_]+__\d+_\d+_(.+)$/;
		defined $function or die "$path: malformed symbol $name\n";
		$function =~ s/\Q$level\E$//
			or die "$path: $name lacks level $level\n";
		length($function) or die "$path: empty function in $name\n";
		push @rows, [$level, $function, $name];
	}
	close $input or die "cannot close $path: $!\n";
	@rows or die "$path: empty initcall order\n";
	return \@rows;
}

my $control = read_map($control_path);
my $thin = $thin_path =~ /\.lds$/ ? read_order_script($thin_path) :
	read_map($thin_path);
for my $side (['control', $control], ['thinlto', $thin]) {
	my ($name, $rows) = @$side;
	open my $output, '>', "$output_prefix.$name.tsv"
		or die "cannot write $output_prefix.$name.tsv: $!\n";
	for my $index (0 .. $#$rows) {
		print {$output} join("\t", $index, @{ $rows->[$index] }), "\n";
	}
	close $output or die "cannot close $output_prefix.$name.tsv: $!\n";
}
@$control == @$thin or die "initcall counts differ: " . scalar(@$control) .
	" control, " . scalar(@$thin) . " ThinLTO\n";
for my $index (0 .. $#$control) {
	my $expected = join("\t", @{ $control->[$index] }[0, 1]);
	my $actual = join("\t", @{ $thin->[$index] }[0, 1]);
	$expected eq $actual or die "initcall order differs at $index: $expected vs $actual\n";
}
print "matched " . scalar(@$control) . " initcalls\n";
