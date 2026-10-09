obj-y := core.o
CFLAGS_core.o += $(call cc-disable-warning, override-init)
# The interpreter's static jump table holds label addresses of
# __bpf_prog_run. GCC 4.9 LTO can emit that table in a partition apart from
# the function body, where the local labels it names do not exist, so the
# object compiles natively.
CFLAGS_core.o += $(DISABLE_LTO)

obj-$(CONFIG_BPF_SYSCALL) += syscall.o verifier.o inode.o helpers.o
obj-$(CONFIG_BPF_SYSCALL) += hashtab.o arraymap.o percpu_freelist.o
ifeq ($(CONFIG_PERF_EVENTS),y)
obj-$(CONFIG_BPF_SYSCALL) += stackmap.o
endif
obj-$(CONFIG_CGROUP_BPF) += cgroup.o
