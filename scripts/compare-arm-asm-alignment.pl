#!/usr/bin/env perl
# Compare assembler objects rather than composite objects or C-generated data.
use strict;
use warnings FATAL => 'all';
use File::Find;
use File::Spec;

@ARGV == 2 or die "Usage: $0 GCC-output Clang-output\n";
my ($gcc_output, $clang_output) = @ARGV;
my $readelf = $ENV{READELF} || 'readelf';

sub asm_objects {
    my ($output) = @_;
    -d $output or die "Missing output directory: $output\n";
    my %objects;
    find({ no_chdir => 1, wanted => sub {
        return unless -f $_ && /\/\.([^\/]+\.o)\.cmd$/;
        my $object_name = $1;
        open my $command_file, '<', $_ or die "Open $_: $!\n";
        local $/;
        my $command = <$command_file>;
        close $command_file or die "Close $_: $!\n";
        return unless $command =~ /^cmd_[^\n]*\s[^\s;]+\.S(?:\s|$)/m;
        my (undef, $directory) = File::Spec->splitpath($_);
        my $object = File::Spec->catfile($directory, $object_name);
        -f $object or die "Missing assembler object: $object\n";
        $objects{File::Spec->abs2rel($object, $output)} = $object;
    } }, $output);
    keys %objects or die "Output contains zero assembler objects: $output\n";
    return \%objects;
}

sub section_alignments {
    my ($object) = @_;
    open my $section_file, '-|', $readelf, '-W', '-S', $object
        or die "Run readelf: $!\n";
    my %sections;
    while (my $line = <$section_file>) {
        # Wide readelf output ends with flags, link, info, and sh_addralign.
        next unless $line =~ /^\s*\[\s*\d+\]\s+(\S+)\s+\S+\s+[0-9a-fA-F]+\s+[0-9a-fA-F]+\s+[0-9a-fA-F]+\s+[0-9a-fA-F]+\s+(?:\S+\s+)?\d+\s+\d+\s+(\d+)\s*$/;
        my ($section, $alignment) = ($1, $2);
        next if $section eq 'NULL';
        exists $sections{$section} and die "Duplicate section $section in $object\n";
        $sections{$section} = $alignment;
    }
    close $section_file or die "readelf failed for $object\n";
    keys %sections or die "Cannot parse sections for $object\n";
    return \%sections;
}

my $gcc_objects = asm_objects($gcc_output);
my $clang_objects = asm_objects($clang_output);
my %object_names = map { $_ => 1 } (keys %$gcc_objects, keys %$clang_objects);
my $failures = 0;
my $section_count = 0;
for my $object (sort keys %object_names) {
    if (!exists $gcc_objects->{$object} || !exists $clang_objects->{$object}) {
        print "FAIL $object: missing from one build\n";
        $failures++;
        next;
    }
    my $gcc_sections = section_alignments($gcc_objects->{$object});
    my $clang_sections = section_alignments($clang_objects->{$object});
    my %section_names = map { $_ => 1 } (keys %$gcc_sections, keys %$clang_sections);
    for my $section (sort keys %section_names) {
        my $gcc_alignment = $gcc_sections->{$section} // 'missing';
        my $clang_alignment = $clang_sections->{$section} // 'missing';
        $section_count++;
        if ($gcc_alignment ne $clang_alignment) {
            print "FAIL $object $section: GCC=$gcc_alignment Clang=$clang_alignment\n";
            $failures++;
        }
    }
}
printf "%s: %d assembler objects, %d sections, %d mismatches\n",
    $failures ? 'FAIL' : 'PASS', scalar(keys %object_names), $section_count, $failures;
exit($failures ? 1 : 0);
