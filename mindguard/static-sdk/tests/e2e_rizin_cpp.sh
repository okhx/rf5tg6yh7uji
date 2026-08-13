#!/usr/bin/env bash
set -Eeuo pipefail

repo_root=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
cargo_bin=${CARGO:-cargo}
rustc_bin=${RUSTC:-rustc}
rustdoc_bin=${RUSTDOC:-rustdoc}
rizin_bin=${RIZIN:-rizin}
rz_diff_bin=${RZ_DIFF:-rz-diff}
rz_sign_bin=${RZ_SIGN:-rz-sign}
for command_name in "$cargo_bin" "$rustc_bin" "$rustdoc_bin" clang++-18 cmake dd find llvm-config-18 llvm-strip nm python3 "$rizin_bin" "$rz_diff_bin" "$rz_sign_bin" sed sha256sum sort; do
  command -v "$command_name" >/dev/null 2>&1 || {
    printf 'ОШИБКА: отсутствует команда: %s\n' "$command_name" >&2
    exit 2
  }
done

old_umask=$(umask)
umask 077
tmp_dir=$(mktemp -d "${TMPDIR:-/tmp}/mindguard-rizin-cpp.XXXXXX")
seed="$tmp_dir/seed"
cleanup() {
  status=$?
  trap - EXIT HUP INT TERM
  [[ ! -f "$seed" ]] || shred -u -n 1 "$seed" 2>/dev/null || true
  find "$tmp_dir" -depth -delete 2>/dev/null || true
  umask "$old_umask" || true
  exit "$status"
}
trap cleanup EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

dd if=/dev/urandom of="$seed" bs=32 count=1 status=none
export CARGO_TARGET_DIR="$tmp_dir/tools" RUSTC="$rustc_bin" RUSTDOC="$rustdoc_bin"
"$cargo_bin" build --quiet --offline --locked --manifest-path "$repo_root/static-sdk/build/Cargo.toml"
builder="$CARGO_TARGET_DIR/debug/mindguard-build"
scanner="$CARGO_TARGET_DIR/debug/mindguard-scan"
cmake -S "$repo_root/static-sdk/llvm-pass" -B "$tmp_dir/llvm-pass" \
  -DCMAKE_CXX_COMPILER=clang++-18 -DCMAKE_BUILD_TYPE=Release \
  -DLLVM_DIR="$(llvm-config-18 --cmakedir)" >/dev/null
cmake --build "$tmp_dir/llvm-pass" --parallel >/dev/null
pass_plugin="$tmp_dir/llvm-pass/MindGuardPass.so"

for index in 0 1 2; do
  build_dir="$tmp_dir/build-$index"
  MG_BUILD_SEED_FILE="$seed" cmake \
    -S "$repo_root/static-sdk/cpp/tests/hardened_project" -B "$build_dir" \
    -DCMAKE_CXX_COMPILER=clang++-18 -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON \
    -DMINDGUARD_PROFILE=Hardened -DMINDGUARD_BUILD_TOOL="$builder" \
    -DMINDGUARD_SCAN_TOOL="$scanner" \
    -DMINDGUARD_LLVM_PASS_PLUGIN="$pass_plugin" >/dev/null
  MG_BUILD_SEED_FILE="$seed" cmake --build "$build_dir" --parallel >/dev/null
  "$build_dir/hardened_generated"
  nm -C "$build_dir/hardened_generated" | sed -nE \
    's/^([0-9a-f]+) [tT] .*materialize_embedded_([0-3])\(.*/\2 \1/p' \
    >"$tmp_dir/functions-$index"
  [[ "$(wc -l <"$tmp_dir/functions-$index")" -ge 2 ]]
  cp "$build_dir/hardened_generated" "$tmp_dir/analysis-$index"
  cp "$build_dir/hardened_generated" "$tmp_dir/artifact-$index"
  llvm-strip --strip-all "$tmp_dir/artifact-$index"
done

[[ "$(sed -n 's/^seed=//p' "$tmp_dir"/build-*/mindguard-static/mindguard/obfuscation-manifest.txt | sort -u | wc -l)" == 3 ]]
[[ "$(sha256sum "$tmp_dir"/artifact-* | sed 's/ .*//' | sort -u | wc -l)" == 3 ]]

: >"$tmp_dir/graph.tsv"
: >"$tmp_dir/graph-attempts.tsv"
for left in 0 1; do
  for right in $(seq $((left + 1)) 2); do
    while read -r variant left_address; do
      right_address=$(sed -n "s/^$variant //p" "$tmp_dir/functions-$right")
      [[ -n "$right_address" ]] || continue
      printf '%s\t%s\t%s\n' "$left" "$right" "$variant" >>"$tmp_dir/graph-attempts.tsv"
      dot="$tmp_dir/graph-$left-$right-$variant.dot"
      RZ_COLOR=0 "$rz_diff_bin" -B -C -t graphs \
        -0 "0x$left_address" -1 "0x$right_address" \
        "$tmp_dir/analysis-$left" "$tmp_dir/analysis-$right" >"$dot" 2>/dev/null || true
      [[ -s "$dot" ]] || continue
      python3 - "$left" "$right" "$variant" "$dot" >>"$tmp_dir/graph.tsv" <<'PY'
import re, sys
text = open(sys.argv[4], encoding="utf-8").read()
colors = re.findall(r'^\s*"0x[0-9a-f]+" \[fillcolor="([^"]+)"', text, re.M)
assert colors
same = colors.count("lightgray")
print(sys.argv[1], sys.argv[2], sys.argv[3], same, len(colors), same / len(colors), sep="\t")
PY
    done <"$tmp_dir/functions-$left"
  done
done

: >"$tmp_dir/signatures.tsv"
for baseline in 0 1 2; do
  signature="$tmp_dir/baseline-$baseline.sig"
  RZ_COLOR=0 "$rz_sign_bin" -aa -q -e scr.color=0 \
    -e flirt.ignore.unknown=false -o "$signature" "$tmp_dir/artifact-$baseline" \
    2>>"$tmp_dir/rizin-diagnostics.err"
  for candidate in 0 1 2; do
    [[ "$candidate" != "$baseline" ]] || continue
    output="$tmp_dir/sign-$baseline-$candidate.out"
    RZ_COLOR=0 "$rizin_bin" -e scr.color=0 -A -q \
      -c "Fs $signature" -c aflj "$tmp_dir/artifact-$candidate" >"$output" \
      2>>"$tmp_dir/rizin-diagnostics.err"
    python3 - "$baseline" "$candidate" "$tmp_dir/functions-$baseline" "$output" \
      >>"$tmp_dir/signatures.tsv" <<'PY'
import json, sys
targets = {f"flirt.fcn.{int(line.split()[1], 16):08x}" for line in open(sys.argv[3])}
text = open(sys.argv[4], encoding="utf-8", errors="replace").read()
functions = json.loads(text[text.find("[{"):])
hits = len(targets & {function["name"] for function in functions})
print(sys.argv[1], sys.argv[2], hits, len(targets), hits / len(targets), sep="\t")
PY
  done
done

RZ_COLOR=0 "$rizin_bin" -e scr.color=0 -A -q -c aflj "$tmp_dir/artifact-0" \
  >"$tmp_dir/stripped-functions.json" 2>"$tmp_dir/stripped-analysis.err"
generated=$(find "$tmp_dir/build-0/mindguard/hardened_generated" -name mindguard_generated.hpp -print -quit)
material_address=$(python3 - "$tmp_dir/artifact-0" "$generated" <<'PY'
import re, struct, sys
artifact = open(sys.argv[1], "rb").read()
header = open(sys.argv[2], encoding="utf-8").read()
match = re.search(r"material\{\{(.*?)\}\};", header, re.S)
material = bytes(int(value, 16) for value in re.findall(r"0x([0-9a-f]{2})", match.group(1)))
offset = artifact.find(material)
assert offset >= 0 and artifact.find(material, offset + 1) < 0
program_offset = struct.unpack_from("<Q", artifact, 32)[0]
entry_size, count = struct.unpack_from("<HH", artifact, 54)
for index in range(count):
    item = struct.unpack_from("<IIQQQQQ", artifact, program_offset + index * entry_size)
    if item[0] == 1 and item[2] <= offset < item[2] + item[5]:
        print(hex(item[3] + offset - item[2]))
        break
else:
    raise AssertionError("material is not mapped")
PY
)
RZ_COLOR=0 "$rizin_bin" -e scr.color=0 -A -q -c "axtj @ $material_address" \
  "$tmp_dir/artifact-0" >"$tmp_dir/material-xrefs.json" 2>>"$tmp_dir/stripped-analysis.err"
RZ_COLOR=0 "$rizin_bin" -e scr.color=0 -A -q -c aae "$tmp_dir/artifact-0" \
  >"$tmp_dir/emulation.out" 2>"$tmp_dir/emulation.err"
python3 - "$tmp_dir/stripped-functions.json" "$tmp_dir/material-xrefs.json" \
  "$tmp_dir/emulation.out" "$tmp_dir/emulation.err" "$material_address" <<'PY'
import json, pathlib, sys
functions = json.load(open(sys.argv[1]))
xrefs = json.load(open(sys.argv[2]))
address = int(sys.argv[5], 16)
material_function = any(function["offset"] <= address < function.get("maxbound", function["offset"])
                        for function in functions)
assert not material_function and len(xrefs) <= 1
assert not any("mindguard" in function["name"].lower() for function in functions)
emulation = pathlib.Path(sys.argv[3]).read_bytes() + pathlib.Path(sys.argv[4]).read_bytes()
assert b"hardened-secret" not in emulation and b"packed-byte-secret" not in emulation
errors = emulation.lower().count(b"error") + emulation.lower().count(b"unhandled")
print(f"rz_stripped_autoanalysis functions={len(functions)} material_xrefs={len(xrefs)} material_function_boundary={int(material_function)}")
print(f"rz_bounded_aae plaintext_hits=0 diagnostic_events={errors}")
PY

python3 - "$tmp_dir/graph.tsv" "$tmp_dir/graph-attempts.tsv" "$tmp_dir/signatures.tsv" <<'PY'
import statistics, sys
def values(path):
    return [float(line.split("\t")[-1]) for line in open(path)]
graph, signatures = values(sys.argv[1]), values(sys.argv[3])
attempts = sum(1 for _ in open(sys.argv[2]))
print(f"rz_gate_samples graphs={len(graph)} attempts={attempts} signatures={len(signatures)}")
assert attempts and len(signatures) == 6
print(f"rz_protected_function_recognition recognized={len(graph)} attempted={attempts}")
if graph:
    print(f"rz_graph_exact_block_similarity count={len(graph)} min={min(graph):.6f} median={statistics.median(graph):.6f} max={max(graph):.6f}")
    assert max(graph) <= 0.50, "Rizin graph similarity gate failed"
else:
    print("rz_graph_exact_block_similarity count=0 result=protected-functions-not-recovered")
print(f"rz_signature_cross_hit_ratio count={len(signatures)} min={min(signatures):.6f} median={statistics.median(signatures):.6f} max={max(signatures):.6f}")
assert max(signatures) <= 0.70, "Rizin signature cross-hit gate failed"
PY

printf 'MindGuard C++ Rizin N-build gate: УСПЕХ\n'
