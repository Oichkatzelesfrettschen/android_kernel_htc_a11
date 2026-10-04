#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Fixture test for scripts/generate-lto-initcall-order.pl: link order across
# archive members, __COUNTER__ order within a member, every initcall class,
# and loud failure on malformed, duplicate, unknown-level, empty-class and
# symbol-reader-failure inputs.
set -eu

perl=${PERL:-perl}

source_tree=$(CDPATH='' cd -- "$(dirname -- "$0")/../.." && pwd)
temporary_dir=$(mktemp -d)
trap 'rm -f "$temporary_dir"/*; rmdir "$temporary_dir"' EXIT HUP INT TERM

cat > "$temporary_dir/fake-nm" <<'EOF'
#!/bin/sh
set -eu
test "$1" = --quiet
test "$2" = --defined-only
if test "$3" = "${FAIL_INPUT:-}"; then
	exit 7
fi
cat "$3"
EOF
chmod +x "$temporary_dir/fake-nm"

cat > "$temporary_dir/ordered" <<'EOF'
first.o:
-------- d __initcall__kmod_a_first__2_20_same6
-------- d __initcall__kmod_a_first__1_10_early_init0
second.o:
-------- d __initcall__kmod_b_second__1_10_same6
-------- d __initcall__kmod_b_second__2_30_rootfs_initrootfs
-------- d __initcall__kmod_b_second__3_40_console_initcon
-------- d __initcall__kmod_b_second__4_50_security_initsec
EOF

NM="$temporary_dir/fake-nm" INITCALL_REQUIRED_CLASSES=ordinary,con,sec \
	"$perl" "$source_tree/scripts/generate-lto-initcall-order.pl" \
	"$temporary_dir/ordered" > "$temporary_dir/order.lds"
grep -Fq '.initcall0.init..__initcall__kmod_a_first__1_10_early_init0' "$temporary_dir/order.lds"
grep -Fq '.initcallrootfs.init..__initcall__kmod_b_second__2_30_rootfs_initrootfs' "$temporary_dir/order.lds"
grep -Fq '.con_initcall.init..__initcall__kmod_b_second__3_40_console_initcon' "$temporary_dir/order.lds"
grep -Fq '.security_initcall.init..__initcall__kmod_b_second__4_50_security_initsec' "$temporary_dir/order.lds"
test "$(grep -Fc '.initcall6.init..__initcall__' "$temporary_dir/order.lds")" -eq 2

cat > "$temporary_dir/malformed" <<'EOF'
-------- d __initcall_bad
EOF
if NM="$temporary_dir/fake-nm" "$perl" "$source_tree/scripts/generate-lto-initcall-order.pl" \
	"$temporary_dir/malformed" > "$temporary_dir/invalid.lds" 2> "$temporary_dir/error"; then
	exit 1
fi
grep -Fq 'malformed initcall symbol' "$temporary_dir/error"

cat > "$temporary_dir/duplicate" <<'EOF'
-------- d __initcall__kmod_a_first__1_10_same6
-------- d __initcall__kmod_a_first__1_11_other6
EOF
if NM="$temporary_dir/fake-nm" "$perl" "$source_tree/scripts/generate-lto-initcall-order.pl" \
	"$temporary_dir/duplicate" > "$temporary_dir/invalid.lds" 2> "$temporary_dir/error"; then
	exit 1
fi
grep -Fq 'duplicate initcall counter' "$temporary_dir/error"

cat > "$temporary_dir/ordinary-only" <<'EOF'
-------- d __initcall__kmod_a_first__1_10_same6
EOF
if NM="$temporary_dir/fake-nm" INITCALL_REQUIRED_CLASSES=ordinary,sec \
	"$perl" "$source_tree/scripts/generate-lto-initcall-order.pl" \
	"$temporary_dir/ordinary-only" > "$temporary_dir/invalid.lds" 2> "$temporary_dir/error"; then
	exit 1
fi
grep -Fq 'required initcall class sec is empty' "$temporary_dir/error"

cat > "$temporary_dir/unknown-level" <<'EOF'
-------- d __initcall__kmod_a_first__1_10_unknown8
EOF
if NM="$temporary_dir/fake-nm" "$perl" "$source_tree/scripts/generate-lto-initcall-order.pl" \
	"$temporary_dir/unknown-level" > "$temporary_dir/invalid.lds" 2> "$temporary_dir/error"; then
	exit 1
fi
grep -Fq 'unknown initcall level' "$temporary_dir/error"

FAIL_INPUT="$temporary_dir/ordered" NM="$temporary_dir/fake-nm" \
	"$perl" "$source_tree/scripts/generate-lto-initcall-order.pl" \
	"$temporary_dir/ordered" > "$temporary_dir/invalid.lds" 2> "$temporary_dir/error" && exit 1
grep -Fq 'failed for' "$temporary_dir/error"

printf 'ThinLTO initcall order fixtures passed\n'
