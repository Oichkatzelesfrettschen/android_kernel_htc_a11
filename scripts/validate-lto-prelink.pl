#!/usr/bin/env perl
# SPDX-License-Identifier: GPL-2.0
# Check the native ARM object that the ThinLTO prelink writes as vmlinux.o
# before modpost, kallsyms and the final vmlinux link consume it.
#
# The object must be an ARM EABI5 little-endian relocatable ELF32. Every
# section name must belong to a known kernel class with that class's ELF
# type and flags, every relocation section must target the allocated
# section its name implies, the ordered .initcall*.init output must exist,
# and no export CRC symbol may stay undefined.

use strict;
use warnings;

@ARGV == 2 or die "usage: $0 READELF VMLINUX_OBJECT\n";
my ($readelf, $object) = @ARGV;

sub readelf_output {
	my (@arguments) = @_;
	open my $pipe, '-|', $readelf, @arguments, $object
		or die "cannot run $readelf: $!\n";
	local $/;
	my $output = <$pipe>;
	close $pipe or die "$readelf failed for $object\n";
	return $output;
}

my $header = readelf_output('-h');
$header =~ /^\s*Class:\s+ELF32\s*$/m or die "$object: expected ELF32\n";
$header =~ /^\s*Data:\s+2's complement, little endian\s*$/m
	or die "$object: expected little-endian ELF\n";
$header =~ /^\s*Type:\s+REL\s/m or die "$object: expected relocatable ELF\n";
$header =~ /^\s*Machine:\s+ARM\s*$/m or die "$object: expected ARM ELF\n";
my ($elf_flags) = $header =~ /^\s*Flags:\s+(0x[0-9a-fA-F]+)/m;
defined($elf_flags) && (hex($elf_flags) & 0xff000000) == 0x05000000
	or die "$object: expected ARM EABI5 ELF\n";

my $table = readelf_output('-SW');
my ($count) = $table =~ /^There are (\d+) section headers/m;
defined $count or die "$object: missing section count\n";
my @sections = ({ name => '', type => 'NULL' });
my %by_name;
for my $line (split /\n/, $table) {
	$line =~ s/SYMTAB SECTION INDICES/SYMTAB_SHNDX/;
	next unless $line =~ /^\s*\[\s*(\d+)\]\s+(\S+)\s+(\S+)\s+([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+([A-Z]*)\s+(\d+)\s+(\d+)\s+(\d+)\s*$/;
	my ($index, $name, $type, $flags, $link, $info) =
		($1, $2, $3, $8, $9, $10);
	$index == @sections or die "$object: section index gap at $index\n";
	exists $by_name{$name} and die "$object: duplicate section $name\n";
	my $section = { name => $name, type => $type, flags => $flags,
		link => $link, info => $info };
	push @sections, $section;
	$by_name{$name} = $section;
}
@sections == $count or die "$object: parsed " . scalar(@sections) .
	" of $count sections\n";

sub check_section {
	my ($section, $type, $required, $forbidden) = @_;
	my $name = $section->{name};
	$section->{type} eq $type or die "$object: $name has type $section->{type}, expected $type\n";
	$section->{flags} =~ /$_/ or die "$object: $name lacks flag $_\n"
		for split //, $required;
	$section->{flags} !~ /$_/ or die "$object: $name has forbidden flag $_\n"
		for split //, $forbidden;
}

# Section lifetimes from include/linux/init.h and asm-generic/vmlinux.lds.h.
my $lifetime = qr/(?:init|exit|ref|devinit|devexit|cpuinit|cpuexit|meminit|memexit)/;
my $initcall_section = qr/\.(?:initcall(?:early|rootfs|[0-7]s?)|con_initcall|security_initcall)\.init/;
my $export_table = qr/___(?:ksymtab|kcrctab)(?:_gpl|_unused|_unused_gpl|_gpl_future)?\+.+/;

my $initcall_count = 0;
my %unrecognized;
for my $section (@sections) {
	my ($name, $type) = @{$section}{qw(name type)};
	next if $type eq 'NULL';
	if ($type eq 'REL' || $type eq 'RELA') {
		$name =~ /^\.rela?(.+)$/ or die "$object: unexpected relocation $name\n";
		my $expected = $1;
		my $target = $sections[$section->{info}]
			or die "$object: $name has invalid relocation target\n";
		my $matches = $target->{name} eq $expected;
		if (!$matches && $expected =~ /^($initcall_section)\.\./) {
			$matches = $target->{name} eq $1;
		}
		$matches or die "$object: $name targets $target->{name}, expected $expected\n";
		# CONFIG_DEBUG_INFO relocates the DWARF sections, which never load.
		$target->{flags} =~ /A/ || $target->{name} =~ /^\.debug_/
			or die "$object: $name targets a non-allocated section\n";
		my $symbols = $sections[$section->{link}]
			or die "$object: $name has invalid symbol table link\n";
		$symbols->{type} eq 'SYMTAB'
			or die "$object: $name does not link to SYMTAB\n";
		check_section($section, $type, '', 'AWX');
	} elsif ($name =~ /^\.ARM\.exidx(?:\.(.+))?$/) {
		my $target_name = defined($1) ? ".$1" : '.text';
		my $target = $sections[$section->{link}]
			or die "$object: $name has invalid unwind link\n";
		$target->{name} eq $target_name
			or die "$object: $name links $target->{name}, expected $target_name\n";
		check_section($section, 'ARM_EXIDX', 'AL', 'WX');
	} elsif ($name =~ /^\.(?:text|$lifetime\.text|sched\.text|head\.text|entry\.text|spinlock\.text|kprobes\.text|idmap\.text)(?:\..+)?$/ ||
		 $name =~ /^\.(?:fixup|irqentry\.text|exception\.text|vectors|stubs)$/) {
		check_section($section, 'PROGBITS', 'AX', 'W');
	} elsif ($name eq '.proc.info.init') {
		# Processor records are assembled with #alloc, #execinstr.
		check_section($section, 'PROGBITS', 'A', 'W');
	} elsif ($name =~ /^\.(?:bss|sbss)(?:\..+)?$/) {
		check_section($section, 'NOBITS', 'WA', 'X');
	} elsif ($name =~ /^\.(?:data|sdata|$lifetime\.data|data\.rel\.ro)(?:\..+)?$/ ||
		 $name =~ /^\.(?:init\.setup|exitcall\.exit|arch\.info\.init|taglist\.init|exportcompat\.init)$/ ||
		 $name =~ /^(?:__param|__modver|__tracepoints|__tracepoints_ptrs|_ftrace_events|__verbose|__trace_printk_fmt)$/ ||
		 $name =~ /^$export_table$/ ||
		 $name =~ /^$initcall_section$/) {
		check_section($section, 'PROGBITS', 'WA', 'X');
		$initcall_count++ if $name =~ /^$initcall_section$/;
	} elsif ($name =~ /^\.$lifetime\.rodata(?:\..+)?$/) {
		# Lifetime rodata holds const pointer tables, which a compiler
		# may emit writable; the final link places it by name.
		check_section($section, 'PROGBITS', 'A', 'X');
	} elsif ($name =~ /^\.(?:rodata|init\.ramfs(?:\.info)?|alt\.smp\.init|pv_table|builtin_fw|ARM\.extab)(?:\..+)?$/ ||
		 $name =~ /^(?:__ex_table|__ksymtab_strings|__bug_table|__tracepoints_strings)$/) {
		check_section($section, 'PROGBITS', 'A', 'WX');
	} elsif ($name eq '.symtab') {
		check_section($section, 'SYMTAB', '', 'AWX');
	} elsif ($name eq '.symtab_shndx') {
		check_section($section, 'SYMTAB_SHNDX', '', 'AWX');
		my $symbols = $sections[$section->{link}]
			or die "$object: extended symbol indices lack a symbol table\n";
		$symbols->{name} eq '.symtab'
			or die "$object: extended symbol indices link to $symbols->{name}\n";
	} elsif ($name =~ /^\.(?:strtab|shstrtab)$/) {
		check_section($section, 'STRTAB', '', 'AWX');
	} elsif ($name eq '.ARM.attributes') {
		check_section($section, 'ARM_ATTRIBUTES', '', 'AWX');
	} elsif ($name eq '.llvm_addrsig') {
		check_section($section, 'LLVM_ADDRSIG', '', 'AWX');
	} elsif ($name =~ /^\.(?:debug|zdebug|comment|note\.GNU-stack)/) {
		$section->{flags} !~ /[AWX]/
			or die "$object: metadata section $name is allocated or executable\n";
	} else {
		$unrecognized{$name}++;
	}
}

if (%unrecognized) {
	my @names = sort keys %unrecognized;
	die "$object: unrecognized sections: " . join(', ', @names[0 ..
		($#names < 49 ? $#names : 49)]) .
		(@names > 50 ? ' ... (' . scalar(@names) . ' total)' : '') . "\n";
}

$initcall_count or die "$object: missing ordered initcall output\n";
exists $by_name{'.symtab'} or die "$object: missing symbol table\n";
open my $symbol_pipe, '-|', $readelf, '-sW', $object
	or die "cannot read symbols in $object: $!\n";
my @unresolved_crc;
while (my $line = <$symbol_pipe>) {
	next if index($line, '__crc_') < 0;
	push @unresolved_crc, $1 if $line =~ /[ \t]UND[ \t]+(__crc_\S+)/;
}
close $symbol_pipe or die "$readelf failed to read symbols in $object\n";
@unresolved_crc and die "$object: unresolved export CRC: " .
	join(', ', sort @unresolved_crc) . "\n";
