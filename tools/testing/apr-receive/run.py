"""Compile real APR/TAL receive functions against bounded host transport fixtures."""

import hashlib
import json
import os
import re
from pathlib import Path
import subprocess
import tempfile
import sys

ROOT = (
    Path(sys.argv[1]).resolve()
    if len(sys.argv) == 2
    else Path(__file__).resolve().parents[3]
)


def function(source, name):
    start = re.search(
        r"^[^\n;]*\b" + re.escape(name) + r"\([^;{]*?\)\s*\n\{", source, re.M
    ).start()
    body = source.index("{", start)
    depth = 1
    end = body + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


inputs = {
    name: (ROOT / name).read_text()
    for name in (
        "arch/arm/mach-msm/qdsp6v2/apr.c",
        "arch/arm/mach-msm/qdsp6v2/apr_tal.c",
        "arch/arm/mach-msm/include/mach/qdsp6v2/apr.h",
        "arch/arm/mach-msm/include/mach/qdsp6v2/apr_tal.h",
        "sound/soc/msm/qdsp6v2/q6asm.c",
        "include/sound/apr_audio-v2.h",
        "include/trace/events/q6asm.h",
    )
}
for callback_name in ("q6asm_callback", "q6asm_srvc_callback"):
    callback_body = function(inputs["sound/soc/msm/qdsp6v2/q6asm.c"], callback_name)
    guard = "if (!q6asm_payload_valid(data))"
    assert callback_body.count(guard) == 1, (
        callback_name + " needs one payload admission guard"
    )
    assert callback_body.index(guard) < callback_body.index(
        "payload = data->payload"
    ), callback_name + " accesses payload before admission"

with tempfile.TemporaryDirectory() as scratch:
    folder = Path(scratch)
    include = folder / "linux"
    include.mkdir()
    (include / "mutex.h").write_text("")
    (include / "types.h").write_text("")
    headers = "\n".join(
        line
        for name in inputs
        if name.endswith(("/apr.h", "/apr_tal.h"))
        for line in inputs[name].splitlines()
        if not line.startswith("#include")
    )
    funcs = "\n".join(
        function(inputs["arch/arm/mach-msm/qdsp6v2/apr.c"], name)
        for name in ("apr_cb_func", "dispatch_event")
    )
    funcs += "\n" + function(
        inputs["arch/arm/mach-msm/qdsp6v2/apr_tal.c"], "apr_tal_notify"
    )
    harness = (Path(__file__).with_name("fixture.c")).read_text()
    source = harness.replace("/* PRODUCTION_HEADERS */", headers).replace(
        "/* PRODUCTION_FUNCTIONS */", funcs
    )
    (folder / "case.c").write_text(source)
    compiler = os.environ.get("CC", "clang")
    command = [
        compiler,
        "-std=gnu99",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-Wno-unused-parameter",
        "-Wno-sign-compare",
        "-fsanitize=address,undefined",
        "-fno-omit-frame-pointer",
        "-I",
        str(folder),
        str(folder / "case.c"),
        "-o",
        str(folder / "case"),
    ]
    subprocess.run(command, check=True)
    subprocess.run([str(folder / "case")], check=True)
    asm_header = inputs["include/sound/apr_audio-v2.h"]
    structs = (
        "asm_data_event_read_done_v2",
        "asm_data_event_write_done_v2",
        "asm_session_cmdrsp_get_sessiontime_v3",
        "asm_data_event_sr_cm_change_notify",
        "asm_data_event_enc_sr_cm_change_notify",
    )
    definitions = "\n".join(
        re.search(r"struct " + name + r"\s*\{.*?\} __packed;", asm_header, re.S)[0]
        for name in structs
    )
    helper = function(inputs["sound/soc/msm/qdsp6v2/q6asm.c"], "q6asm_payload_valid")
    symbols = set(re.findall(r"\bASM_[A-Z0-9_]+\b", helper))
    definitions += "\n" + "\n".join(
        re.search(r"^#define " + symbol + r"\s+[^\n]+", asm_header, re.M)[0]
        for symbol in sorted(symbols)
    )
    (include / "tracepoint.h").write_text("")
    (folder / "trace").mkdir()
    (folder / "trace/define_trace.h").write_text("")
    payload_case = (
        (Path(__file__).with_name("payload_fixture.c"))
        .read_text()
        .replace("/* PRODUCTION_HEADERS */", headers + "\n" + definitions)
        .replace("/* PRODUCTION_VALIDATOR */", helper)
        .replace("/* PRODUCTION_TRACEPOINT */", inputs["include/trace/events/q6asm.h"])
    )
    (folder / "payload.c").write_text(payload_case)
    subprocess.run(
        command[:-3] + [str(folder / "payload.c"), "-o", str(folder / "payload")],
        check=True,
    )
    subprocess.run([str(folder / "payload")], check=True)
print(
    json.dumps(
        {
            "source_hashes": {
                name: hashlib.sha256(value.encode()).hexdigest()
                for name, value in inputs.items()
            },
            "compiled_real_functions": [
                "apr_cb_func",
                "dispatch_event",
                "apr_tal_notify",
            ],
            "sanitizers": ["address", "undefined"],
        },
        indent=2,
    )
)
