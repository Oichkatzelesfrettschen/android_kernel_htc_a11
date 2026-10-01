/*
 * arch/arm/include/asm/vfpmacros.h
 *
 * Assembler-only file containing VFP macros and register definitions.
 */
#include <asm/hwcap.h>

#include "vfp.h"

#ifdef CONFIG_VFPv3
	.fpu vfpv3
#else
	.fpu vfp
#endif

@ Transfer VFP system registers without changing the instruction encoding.
	.macro	VFPFMRX, rd, sysreg, cond
	VMRS\cond	\rd, \sysreg	@ FMRX	\rd, \sysreg
	.endm

	.macro	VFPFMXR, sysreg, rd, cond
	VMSR\cond	\sysreg, \rd	@ FMXR	\sysreg, \rd
	.endm

	@ read all the working registers back into the VFP
	.macro	VFPFLDMIA, base, tmp
#if __LINUX_ARM_ARCH__ < 6
	LDC	p11, cr0, [\base],#33*4		    @ FLDMIAX \base!, {d0-d15}
#else
	vldmia	\base!, {d0-d15}		    @ FLDMIAD \base!, {d0-d15}
#endif
#ifdef CONFIG_VFPv3
#if __LINUX_ARM_ARCH__ <= 6
	ldr	\tmp, =elf_hwcap		    @ may not have MVFR regs
	ldr	\tmp, [\tmp, #0]
	tst	\tmp, #HWCAP_VFPv3D16
	ldceq	p11, cr0, [\base],#32*4		    @ FLDMIAD \base!, {d16-d31}
	addne	\base, \base, #32*4		    @ step over unused register space
#else
	VFPFMRX	\tmp, MVFR0			    @ Media and VFP Feature Register 0
	and	\tmp, \tmp, #MVFR0_A_SIMD_MASK	    @ A_SIMD field
	cmp	\tmp, #2			    @ 32 x 64bit registers?
	vldmiaeq	\base!, {d16-d31}		    @ FLDMIAD \base!, {d16-d31}
	addne	\base, \base, #32*4		    @ step over unused register space
#endif
#endif
	.endm

	@ write all the working registers out of the VFP
	.macro	VFPFSTMIA, base, tmp
#if __LINUX_ARM_ARCH__ < 6
	STC	p11, cr0, [\base],#33*4		    @ FSTMIAX \base!, {d0-d15}
#else
	vstmia	\base!, {d0-d15}		    @ FSTMIAD \base!, {d0-d15}
#endif
#ifdef CONFIG_VFPv3
#if __LINUX_ARM_ARCH__ <= 6
	ldr	\tmp, =elf_hwcap		    @ may not have MVFR regs
	ldr	\tmp, [\tmp, #0]
	tst	\tmp, #HWCAP_VFPv3D16
	stceq	p11, cr0, [\base],#32*4		    @ FSTMIAD \base!, {d16-d31}
	addne	\base, \base, #32*4		    @ step over unused register space
#else
	VFPFMRX	\tmp, MVFR0			    @ Media and VFP Feature Register 0
	and	\tmp, \tmp, #MVFR0_A_SIMD_MASK	    @ A_SIMD field
	cmp	\tmp, #2			    @ 32 x 64bit registers?
	vstmiaeq	\base!, {d16-d31}		    @ FSTMIAD \base!, {d16-d31}
	addne	\base, \base, #32*4		    @ step over unused register space
#endif
#endif
	.endm
