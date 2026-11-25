"""Require real-source mutations to fail the receive admission fixtures."""

import json
from pathlib import Path
import re
import subprocess
import tempfile
import sys

ROOT = Path(__file__).resolve().parents[3]
RUNNER = Path(__file__).with_name("run.py")
paths = (
    "arch/arm/mach-msm/qdsp6v2/apr.c",
    "arch/arm/mach-msm/qdsp6v2/apr_tal.c",
    "arch/arm/mach-msm/include/mach/qdsp6v2/apr.h",
    "arch/arm/mach-msm/include/mach/qdsp6v2/apr_tal.h",
    "sound/soc/msm/qdsp6v2/q6asm.c",
    "include/sound/apr_audio-v2.h",
    "include/trace/events/q6asm.h",
)
originals = {name: (ROOT / name).read_text() for name in paths}
mutations = {}
for callback in ("q6asm_callback", "q6asm_srvc_callback"):
    text = originals[paths[4]]
    start = re.search(
        r"^static int32_t " + callback + r"\([^;{]*?\)\s*\n\{", text, re.M
    ).start()
    guard = text.index("\tif (!q6asm_payload_valid(data)) {", start)
    end = text.index("\n\n", guard)
    mutations[callback + "-unguarded"] = (paths[4], text[:guard] + text[end:])
mutations["apr-declared-length-unchecked"] = (
    paths[0],
    originals[paths[0]].replace(
        "if (hdr_size > hdr->pkt_size || hdr->pkt_size > len)",
        "if (hdr_size > hdr->pkt_size)",
    ),
)
mutations["tal-discard-reclassified-suffix"] = (
    paths[1],
    originals[paths[1]].replace(
        "check_pending:", "check_pending:\n\t\tapr_ch->discard_packet = false;"
    ),
)
mutations["read-done-stale-tail-accepted"] = (
    paths[4],
    originals[paths[4]].replace(
        "required_bytes = sizeof(struct asm_data_event_read_done_v2);",
        "required_bytes = 0;",
    ),
)
mutations["raw-total-word-swapped"] = (
    paths[6],
    originals[paths[6]].replace(
        "__entry->total_bytes = payload[4];", "__entry->total_bytes = payload[5];"
    ),
)
results = []
for name, (changed_path, changed_text) in mutations.items():
    assert changed_text != originals[changed_path]
    with tempfile.TemporaryDirectory() as directory:
        folder = Path(directory)
        for path, source in originals.items():
            target = folder / path
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(changed_text if path == changed_path else source)
        process = subprocess.run(
            [sys.executable, str(RUNNER), str(folder)],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        assert process.returncode != 0, name + " survives admission fixtures"
        results.append({"mutation": name, "rejected": True, "exit": process.returncode})
print(json.dumps({"mutations": results, "rejected": len(results)}, indent=2))
