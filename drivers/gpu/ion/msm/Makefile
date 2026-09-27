ccflags-y := -O3 -ffast-math -mcpu=cortex-a15 -mtune=cortex-a15 -marm -mfpu=neon-vfpv4
ccflags-y += $(call cc-option,-fgcse-lm) $(call cc-option,-fgcse-sm)
ccflags-y += $(call cc-option,-fsched-spec-load) $(call cc-option,-fforce-addr)
ccflags-y += $(call cc-option,-fsingle-precision-constant)
obj-y += msm_ion.o ion_cp_common.o ion_iommu_map.o
