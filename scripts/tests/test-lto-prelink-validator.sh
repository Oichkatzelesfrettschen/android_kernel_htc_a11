#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Mutation test for scripts/validate-lto-prelink.pl: the ThinLTO vmlinux.o
# passes, and copies carrying an unknown allocated section, no initcall
# output, or a writable .text each fail with their own diagnostic.
set -eu

perl=${PERL:-perl}

if test "$#" -ne 3; then
	printf 'usage: %s READELF OBJCOPY NATIVE_VMLINUX_OBJECT\n' "$0" >&2
	exit 2
fi

readelf_tool=$1
objcopy_tool=$2
native_object=$3
source_tree=$(CDPATH='' cd -- "$(dirname -- "$0")/../.." && pwd)
validator=$source_tree/scripts/validate-lto-prelink.pl
temporary_dir=$(mktemp -d)
trap 'rm -f "$temporary_dir"/*; rmdir "$temporary_dir"' EXIT HUP INT TERM

"$perl" "$validator" "$readelf_tool" "$native_object"

# CONFIG_DEBUG_INFO relocations pin every section the mutations remove.
"$objcopy_tool" --strip-debug "$native_object" "$temporary_dir/base.o"
"$perl" "$validator" "$readelf_tool" "$temporary_dir/base.o"

printf x > "$temporary_dir/unexpected-section.bin"
"$objcopy_tool" --add-section .unexpected_alloc="$temporary_dir/unexpected-section.bin" \
	--set-section-flags .unexpected_alloc=alloc,data \
	"$temporary_dir/base.o" "$temporary_dir/unknown-section.o"
if "$perl" "$validator" "$readelf_tool" "$temporary_dir/unknown-section.o" \
	> "$temporary_dir/output" 2>&1; then
	exit 1
fi
grep -Fq 'unrecognized sections: .unexpected_alloc' "$temporary_dir/output"

"$objcopy_tool" --remove-section '.initcall*.init' \
	--remove-section .con_initcall.init \
	--remove-section .security_initcall.init \
	"$temporary_dir/base.o" "$temporary_dir/missing-initcalls.o"
if "$perl" "$validator" "$readelf_tool" "$temporary_dir/missing-initcalls.o" \
	> "$temporary_dir/output" 2>&1; then
	exit 1
fi
grep -Fq 'missing ordered initcall output' "$temporary_dir/output"

"$objcopy_tool" --set-section-flags .text=alloc,data \
	"$temporary_dir/base.o" "$temporary_dir/writable-text.o"
if "$perl" "$validator" "$readelf_tool" "$temporary_dir/writable-text.o" \
	> "$temporary_dir/output" 2>&1; then
	exit 1
fi
grep -Fq '.text lacks flag X' "$temporary_dir/output"

printf 'ThinLTO prelink validator mutations passed\n'
