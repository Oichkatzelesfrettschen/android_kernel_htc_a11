#!/usr/bin/env perl
# SPDX-License-Identifier: GPL-2.0
# Emit the linker script that places ThinLTO initcall sections in link order.
#
# Each input is a vmlinux object or archive in link order. Within one
# archive member, initcalls keep their __COUNTER__ order; the output names
# every per-initcall section of include/linux/init.h inside the shared
# .initcall<level>.init, .con_initcall.init and .security_initcall.init
# output sections.

use strict;
use warnings;

my $nm = $ENV{NM} or die "NM must name the selected LLVM symbol reader\n";
@ARGV or die "at least one ordered vmlinux input is required\n";

my @levels = qw(early 0 0s 1 1s 2 2s 3 3s 4 4s 5 5s rootfs 6 6s 7 7s con sec);
my %valid_level = map { $_ => 1 } @levels;
my %sections;
my %seen_symbols;
my $ordinary_count = 0;

sub append_member {
	my ($file, $member, $calls) = @_;
	my %seen_counters;
	for my $call (sort { $a->{counter} <=> $b->{counter} } @$calls) {
		my $symbol = $call->{symbol};
		my $level = $call->{level};
		my $counter = $call->{counter};
		die "duplicate initcall counter $counter in $file:$member\n"
			if $seen_counters{$counter}++;
		die "duplicate initcall symbol $symbol\n" if $seen_symbols{$symbol}++;
		my $section = $level eq 'con' ? '.con_initcall.init' :
			$level eq 'sec' ? '.security_initcall.init' :
			".initcall${level}.init";
		push @{ $sections{$level} }, "$section..$symbol";
		$ordinary_count++ unless $level eq 'con' || $level eq 'sec';
	}
}

for my $file (@ARGV) {
	-f $file or die "missing initcall input $file\n";
	open my $input, '-|', $nm, '--quiet', '--defined-only', $file
		or die "cannot execute $nm for $file: $!\n";
	my $member = $file;
	my @calls;
	while (my $line = <$input>) {
		chomp $line;
		if ($line =~ /^([^\s].*):$/) {
			append_member($file, $member, \@calls);
			@calls = ();
			$member = $1;
			next;
		}
		my ($symbol) = $line =~ /^\s*\S+\s+[A-Za-z]\s+(\S+)\s*$/;
		next unless defined $symbol && $symbol =~ /^__initcall_/;
		my ($module, $counter, $source_line, $tail) =
			$symbol =~ /^__initcall__(kmod_[A-Za-z0-9_]+)__(\d+)_(\d+)_(.+)$/;
		die "malformed initcall symbol $symbol in $file:$member\n"
			unless defined $tail;
		my ($function, $level) =
			$tail =~ /^(.+?)(early|rootfs|[0-7]s?|con|sec)$/;
		die "unknown initcall level in $symbol in $file:$member\n"
			unless defined $function && $valid_level{$level};
		push @calls, { symbol => $symbol, level => $level,
			counter => int($counter) };
	}
	close $input or die "$nm failed for $file (status $?)\n";
	append_member($file, $member, \@calls);
}

die "no ordinary initcalls found\n" unless $ordinary_count;
for my $required (split /,/, ($ENV{INITCALL_REQUIRED_CLASSES} || 'ordinary')) {
	next if $required eq 'ordinary' && $ordinary_count;
	die "unknown required initcall class $required\n"
		unless $required eq 'ordinary' || $required eq 'con' || $required eq 'sec';
	die "required initcall class $required is empty\n"
		unless $sections{$required} && @{ $sections{$required} };
}

print "SECTIONS {\n";
for my $level (@levels) {
	next unless $sections{$level};
	my $output = $level eq 'con' ? '.con_initcall.init' :
		$level eq 'sec' ? '.security_initcall.init' :
		".initcall${level}.init";
	print "  $output : {\n";
	for my $input (@{ $sections{$level} }) {
		print "    KEEP(*($input))\n";
	}
	print "  }\n";
}
print "}\n";
