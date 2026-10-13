# APR receive and READ_DONE admission tests

The receive buffer retains bytes across messages. Header containment, complete-packet assembly, and opcode-specific payload lengths keep stale tails outside the callback contract. A built-in tracepoint copies the admitted eleven-word READ_DONE prefix before the application callback and preserves the four-argument app_cb interface.

Run from the kernel checkout:

    PYTHON=/usr/local/bin/python3
    "$PYTHON" tools/testing/apr-receive/run.py

The runner compiles the real APR receive, reset dispatch, TAL notification, Q6ASM payload validator and tracepoint assignments with Clang, warnings as errors, AddressSanitizer and UndefinedBehaviorSanitizer. Transport fixtures exercise fragmentation, capacity, persistent discard, short reads, multiple packets and reset/reopen. Definition-aware assertions require the guard before payload use in both actual Q6ASM callbacks. The fixtures establish host control-flow and field-copy behavior. Kernel build, registered trace format, module ABI, DSP execution and waveform/lifecycle gates remain separate.
