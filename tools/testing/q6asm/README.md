# AAC media-format DSP status regression

`__q6asm_media_format_block_multi_aac()` submits its unchanged AAC payload
through `q6asm_stream_fmt_update()`. The shared helper resets `cmd_status`,
retains the compressed session/stream token, and reports APR transport errors,
timeouts, and the DSP status. `q6asm_callback()` stores the response status
before releasing the command waiter; `q6asm_cmd_status()` maps that status
through `adsp_err.c`, accepting `ADSP_EOK` and `ADSP_EALREADY`.

Both `arch/arm/mach-msm/qdsp6v2/audio_aac.c` and `audio_multi_aac.c` check the
formatter result inside `AUDIO_START`. A negative result returns through the
ioctl before `audio_aio_enable()` starts the stream. The compressed ALSA
caller reaches the same formatter through its stream-specific wrapper.
The change leaves the DSP in charge of format admission, including channel
count; the fixture supplies an EOK response for seven channels to guard that
boundary separately from an EBADPARAM response for the same input.

## Replay

Run from the kernel source root. Choose a build output directory outside the
source checkout. The runner writes generated source, input SHA-256 values,
the compiler command/version, an executable, and `results.txt` there.

```sh
PYTHON=${PYTHON:-python3}
: "${AAC_TEST_OUTPUT:?Set AAC_TEST_OUTPUT to the build output directory}"
"$PYTHON" tools/testing/q6asm/run.py --output-dir "$AAC_TEST_OUTPUT/gcc"
CC=clang "$PYTHON" tools/testing/q6asm/run.py --output-dir "$AAC_TEST_OUTPUT/clang"
"$PYTHON" tools/testing/q6asm/run.py --output-dir "$AAC_TEST_OUTPUT/baseline" \
    --revision 7df7a1adcba9b250cc5bbd86ac95bf50f3b2ad59
```

The baseline command intentionally exits with status 1. The baseline reports
60 cases and 78 failed assertions: all 60 submissions retain the prior
`cmd_status`, six DSP rejections return success, six APR send failures lose
`-EPIPE`, and six timeouts lose `-ETIMEDOUT`. Both repaired host runs report
60 cases and zero failed assertions with `-Wall -Wextra -Werror -O2`.
The observed compilers are GCC 16.2.1 20260810 and Clang 23.1.1.

The runner extracts the actual AAC functions, header constructors, status
helper, ADSP errno table, APR macros, and packed structures from the selected
source. The response injector uses the exact media-format response case group
from `q6asm_callback()`. The surrounding callback dispatch, kernel locks,
atomic memory ordering, scheduler, and transport are outside the host fixture.
An immediate injected response exercises completion before the wait; a
withheld response exercises timeout handling without a five-second delay.

The 60 cases cover three exported AAC entry points, ordinary and compressed
tokens, mono/stereo/5.1 AAC-LC, HE-AAC, HE-AACv2, DSP-controlled seven-channel
admission, `ADSP_EBADPARAM`, `ADSP_EALREADY`, APR `-EPIPE`, and timeout. Each
case checks the literal 36-byte wire packet, command state, prior-status
reset, callback delivery, wait count, and returned errno. A success return
after an injected DSP rejection or a changed wire packet falsifies the fix.

## ARM target check

The changed `sound/soc/msm/qdsp6v2/q6asm.o` compiles under the shipped
`cyanogenmod_a11ul_defconfig`, `CONFIG_LTO_GCC=y`, and `CONFIG_LTO=y` using
the Android GCC 4.9 toolchain, the kernel's old Make executable, and its
Clang r416183b1 host compiler. Kernel and host compiler warnings are errors.
The output is an ARM EABI5 relocatable object; the configure and target logs
contain zero compiler diagnostic lines. The object check covers translation
and its required generated headers; kernel linking and device execution
remain separate gates.

The reproducible target command is:

```sh
: "${KERNEL_OUTPUT:?Set KERNEL_OUTPUT to a fresh build output directory}"
: "${MAKE_TOOL:?Set MAKE_TOOL to the kernel-compatible Make executable}"
: "${GCC_PREFIX:?Set GCC_PREFIX to the Android ARM GCC tool prefix}"
: "${HOST_TOOLS:?Set HOST_TOOLS to the Clang host-tools bin directory}"
for target in cyanogenmod_a11ul_defconfig sound/soc/msm/qdsp6v2/q6asm.o; do
    "$MAKE_TOOL" O="$KERNEL_OUTPUT" ARCH=arm CROSS_COMPILE="$GCC_PREFIX" \
        CFLAGS_MODULE=-fno-pic HOSTCC="$HOST_TOOLS/clang" \
        HOSTCXX="$HOST_TOOLS/clang++" HOSTCFLAGS='-fcommon -Werror' \
        HOSTLDFLAGS=-fuse-ld=lld KCFLAGS=-Werror LDFLAGS=--fatal-warnings \
        -j2 "$target"
done
```

An initial LLVM target attempt hit the explicit GCC-LTO/LLVM configuration
guard. The shipped GCC-LTO target above supplies the ARM compile verdict.

## Device acceptance boundary

The fixture proves error propagation and wire compatibility. It does not
establish firmware support for a rejected AAC layout or reproduce kernel
concurrency. Device acceptance requires the named hardware/DSP AAC component
to emit PCM and reach EOS on supported inputs, then survive close/reopen and
restart cycles. The seven-channel input must produce hardware/DSP output
for a seven-channel decode claim; a propagated rejection remains a capability
gap. CPU decoding cannot satisfy that hardware acceptance gate. Device
execution and OMX error-callback behavior were not run by the host fixture.
