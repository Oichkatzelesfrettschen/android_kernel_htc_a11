"""Compile the AAC packet/status path from the selected kernel sources."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import subprocess


def extract_function(source, name):
    pattern = rf"^(?:static )?(?:int|void) {re.escape(name)}\(.*?^\}}"
    matches = re.findall(pattern, source, re.MULTILINE | re.DOTALL)
    if len(matches) != 1:
        raise ValueError(f"Expected one definition of {name}, found {len(matches)}")
    return matches[0]


def extract_struct(source, name):
    pattern = rf"^struct {re.escape(name)} \{{.*?^\}}\s*__packed;"
    matches = re.findall(pattern, source, re.MULTILINE | re.DOTALL)
    if len(matches) != 1:
        raise ValueError(f"Expected one packed structure {name}")
    return matches[0]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--source-root", type=Path, default=Path(__file__).resolve().parents[3]
    )
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--revision", help="Read source from a local Git revision")
    args = parser.parse_args()
    source_root = args.source_root.resolve()
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    inputs = {}

    def read_source(relative_path):
        if args.revision:
            source = subprocess.check_output(
                [
                    "git",
                    "-C",
                    str(source_root),
                    "show",
                    f"{args.revision}:{relative_path}",
                ],
                text=True,
            )
        else:
            source = (source_root / relative_path).read_text()
        inputs[relative_path] = hashlib.sha256(source.encode()).hexdigest()
        return source

    q6asm = read_source("sound/soc/msm/qdsp6v2/q6asm.c")
    adsp = read_source("sound/soc/msm/qdsp6v2/adsp_err.c")
    apr = read_source("arch/arm/mach-msm/include/mach/qdsp6v2/apr.h")
    formats = read_source("include/sound/apr_audio-v2.h")
    clients = read_source("include/sound/q6asm-v2.h")
    callback = q6asm[q6asm.index("static int32_t q6asm_callback(") :]
    callback = callback[
        callback.index("\t\tcase ASM_STREAM_CMD_OPEN_READ_V3:") : callback.index(
            "\t\tcase ASM_STREAM_CMD_GET_PP_PARAMS_V2:"
        )
    ]
    macro_names = {
        "APR_HDR_LEN",
        "APR_HDR_FIELD",
        "APR_PKT_VER",
        "APR_MSG_TYPE_SEQ_CMD",
        "APR_DOMAIN_APPS",
        "APR_DOMAIN_ADSP",
        "APR_SVC_ASM",
        "APR_BASIC_RSP_RESULT",
        "COMPRESSED_STREAM_IO",
    }
    macro_names.update(re.findall(r"\bASM_[A-Z0-9_]+", callback))
    macro_names.update(re.findall(r"^#define (ADSP_E\w+)", formats, re.MULTILINE))
    headers = "\n".join((apr, formats, clients))
    macros = []
    for name in sorted(macro_names):
        pattern = rf"^#define {name}(?:\([^\n]*?\))?[ \t]*(?:[^\n]*\\\n)*[^\n]*"
        matches = re.findall(pattern, headers, re.MULTILINE)
        matches = list(dict.fromkeys(matches))
        if len(matches) != 1:
            raise ValueError(f"Expected one definition of macro {name}: {matches}")
        macros.append(matches[0])
    structures = [extract_struct(apr, "apr_hdr")]
    for name in (
        "asm_aac_cfg",
        "asm_data_cmd_media_fmt_update_v2",
        "asm_aac_fmt_blk_v2",
    ):
        structures.append(extract_struct(formats, name))
    (output / "q6asm-types.h").write_text("\n\n".join(macros + structures) + "\n")

    functions = [re.sub(r"^#include[^\n]*\n", "", adsp, flags=re.MULTILINE)]
    functions.append(
        """static void fixture_complete(struct audio_client *ac, uint32_t status)
{
    uint32_t payload[] = { ASM_DATA_CMD_MEDIA_FMT_UPDATE_V2, status };
    struct apr_client_data response = {
        .opcode = APR_BASIC_RSP_RESULT, .token = fixture_token, .payload = payload,
    };
    struct apr_client_data *data = &response;
    uint32_t wakeup_flag = 1;

    switch (payload[0]) {
"""
        + callback
        + "\n    default: abort();\n    }\n}\n"
    )
    names = ["__q6asm_add_hdr", "q6asm_stream_add_hdr"]
    aac = extract_function(q6asm, "__q6asm_media_format_block_multi_aac")
    if "q6asm_stream_fmt_update(" in aac:
        names += ["q6asm_cmd_status", "q6asm_stream_fmt_update"]
    names += [
        "__q6asm_media_format_block_multi_aac",
        "q6asm_media_format_block_multi_aac",
        "q6asm_media_format_block_aac",
        "q6asm_stream_media_format_block_aac",
    ]
    functions.extend(extract_function(q6asm, name) for name in names)
    (output / "q6asm-functions.h").write_text("\n\n".join(functions) + "\n")

    compiler = shlex.split(os.environ.get("CC", "cc"))
    fixture = Path(__file__).with_name("aac-format-test.c")
    inputs[fixture.name] = hashlib.sha256(fixture.read_bytes()).hexdigest()
    executable = output / "aac-format-test"
    command = compiler + [
        "-std=gnu11",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-O2",
        "-I",
        str(output),
        str(fixture),
        "-o",
        str(executable),
    ]
    version = subprocess.check_output(compiler + ["--version"], text=True).splitlines()[
        0
    ]
    (output / "inputs.json").write_text(
        json.dumps(
            {
                "revision": args.revision,
                "sha256": inputs,
                "compiler": version,
                "command": command,
            },
            indent=2,
        )
        + "\n"
    )
    print(version, flush=True)
    print(shlex.join(command), flush=True)
    subprocess.run(command, check=True)
    result = subprocess.run(
        [str(executable)], check=False, capture_output=True, text=True
    )
    (output / "results.txt").write_text(result.stdout + result.stderr)
    print(result.stdout, end="")
    print(result.stderr, end="")
    return result.returncode


if __name__ == "__main__":
    raise SystemExit(main())
