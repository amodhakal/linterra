#!/usr/bin/env bash
#
# Compile every shader in shaders/ with glslangValidator.
#
# The engine loads and compiles these at runtime, so a syntax error in one
# surfaces as a broken frame on a machine with a GL context -- never in the
# unit test suite, which runs headless. Validating here means a bad shader
# fails CI instead.
#
# The stage is inferred from the extension, which is what the engine's shader
# paths in src/config.h encode as well.

set -euo pipefail

if ! command -v glslangValidator >/dev/null 2>&1; then
    echo "error: glslangValidator not found on PATH." >&2
    echo "       Linux: sudo apt-get install -y glslang-tools" >&2
    echo "       macOS: brew install glslang" >&2
    exit 127
fi

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
shader_dir="${repo_root}/shaders"

if [[ ! -d "${shader_dir}" ]]; then
    echo "error: shader directory not found: ${shader_dir}" >&2
    exit 1
fi

status=0
validated=0

# Sorted so CI output is stable between runs.
while IFS= read -r shader; do
    case "${shader}" in
        *.vert) stage=vert ;;
        *.frag) stage=frag ;;
        *.comp) stage=comp ;;
        *)
            echo "skip: ${shader#"${repo_root}"/} (unrecognized extension)" >&2
            continue
            ;;
    esac

    printf 'validating %-24s as %s ... ' "${shader#"${repo_root}"/}" "${stage}"
    # glslangValidator echoes the filename on success too, so capture it and
    # only surface the text when something actually went wrong.
    if output="$(glslangValidator -S "${stage}" "${shader}" 2>&1)"; then
        echo "ok"
        validated=$((validated + 1))
    else
        echo "FAILED"
        printf '%s\n' "${output}" >&2
        status=1
    fi
done < <(find "${shader_dir}" -type f \( -name '*.vert' -o -name '*.frag' -o -name '*.comp' \) | sort)

if [[ "${validated}" -eq 0 ]]; then
    echo "error: no shaders found in ${shader_dir}" >&2
    exit 1
fi

if [[ "${status}" -ne 0 ]]; then
    echo "error: one or more shaders failed to compile" >&2
    exit 1
fi

echo "ok: ${validated} shader(s) compiled"
