#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Fixture test for scripts/compare-initcall-order.pl: a native System.map
# and a ThinLTO System.map with the same (level, function) sequence match,
# and a swapped pair or a missing initcall fails with its own diagnostic.
set -eu

perl=${PERL:-perl}
source_tree=$(CDPATH='' cd -- "$(dirname -- "$0")/../.." && pwd)
compare=$source_tree/scripts/compare-initcall-order.pl
temporary_dir=$(mktemp -d)
trap 'rm -f "$temporary_dir"/*; rmdir "$temporary_dir"' EXIT HUP INT TERM

boundaries() {
	cat <<'EOF'
c0010000 T __initcall_start
c0010004 T __initcall0_start
c0010004 T __initcall1_start
c0010008 T __initcall2_start
c0010008 T __initcall3_start
c0010008 T __initcall4_start
c0010008 T __initcall5_start
c0010008 T __initcallrootfs_start
c001000c T __initcall6_start
c0010018 T __initcall7_start
c001001c T __initcall_end
c001001c T __con_initcall_start
c0010020 T __con_initcall_end
c0010020 T __security_initcall_start
c0010024 T __security_initcall_end
EOF
}

{
	boundaries
	cat <<'EOF'
c0010000 t __initcall_spawn_ksoftirqdearly
c0010004 t __initcall_init_mmap_min_addr1
c0010008 t __initcall_populate_rootfsrootfs
c001000c t __initcall_msm_init6
c0010010 t __initcall_msm_init6
c0010014 t __initcall_board_init6s
c0010018 t __initcall_late_probe7
c001001c t __initcall_con_init
c0010020 t __initcall_selinux_init
EOF
} > "$temporary_dir/control.map"

{
	boundaries
	cat <<'EOF'
c0010000 t __initcall__kmod_kernel_softirq__1_740_spawn_ksoftirqdearly
c0010004 t __initcall__kmod_security_min_addr__1_53_init_mmap_min_addr1
c0010008 t __initcall__kmod_init_noinitramfs__1_37_populate_rootfsrootfs
c001000c t __initcall__kmod_arch_arm_a__1_10_msm_init6
c0010010 t __initcall__kmod_arch_arm_b__1_20_msm_init6
c0010014 t __initcall__kmod_arch_arm_board__2_30_board_init6s
c0010018 t __initcall__kmod_drivers_late__1_40_late_probe7
c001001c t __initcall__kmod_drivers_tty_vt_vt__1_50_con_initcon
c0010020 t __initcall__kmod_security_selinux_hooks__1_60_selinux_initsec
EOF
} > "$temporary_dir/thinlto.map"

"$perl" "$compare" "$temporary_dir/control.map" "$temporary_dir/thinlto.map" \
	"$temporary_dir/match" > "$temporary_dir/output"
grep -Fqx 'matched 9 initcalls' "$temporary_dir/output"
grep -Fq "5	6s	board_init" "$temporary_dir/match.thinlto.tsv"

sed -e 's/c0010014 t __initcall__kmod_arch_arm_board__2_30_board_init6s/c0010018 t __initcall__kmod_arch_arm_board__2_30_board_init6s/' \
	-e 's/c0010018 t __initcall__kmod_drivers_late__1_40_late_probe7/c0010014 t __initcall__kmod_drivers_late__1_40_late_probe7/' \
	"$temporary_dir/thinlto.map" > "$temporary_dir/swapped.map"
if "$perl" "$compare" "$temporary_dir/control.map" "$temporary_dir/swapped.map" \
	"$temporary_dir/swapped" > "$temporary_dir/output" 2>&1; then
	exit 1
fi
grep -Fq 'lacks level' "$temporary_dir/output"

grep -v 'late_probe7' "$temporary_dir/thinlto.map" > "$temporary_dir/missing.map"
if "$perl" "$compare" "$temporary_dir/control.map" "$temporary_dir/missing.map" \
	"$temporary_dir/missing" > "$temporary_dir/output" 2>&1; then
	exit 1
fi
grep -Fq 'initcall counts differ: 9 control, 8 ThinLTO' "$temporary_dir/output"

sed -e 's/__initcall__kmod_arch_arm_a__1_10_msm_init6/__initcall__kmod_arch_arm_a__1_10_omap_init6/' \
	"$temporary_dir/thinlto.map" > "$temporary_dir/renamed.map"
if "$perl" "$compare" "$temporary_dir/control.map" "$temporary_dir/renamed.map" \
	"$temporary_dir/renamed" > "$temporary_dir/output" 2>&1; then
	exit 1
fi
grep -Fq 'initcall order differs at 3' "$temporary_dir/output"

printf 'initcall order comparison fixtures passed\n'
