#!/usr/bin/env bash
set -euo pipefail

NSPA_ROOT="${1:-$HOME/Projects/NSPA}"
SVF_BUILD_DIR="${2:-$NSPA_ROOT/SVF/Release-build}"
CURL_BC_DIR="${3:-$NSPA_ROOT/workspace/curl-bc}"
JSON_FILE="${4:-$NSPA_ROOT/outputs/nspa_curl_validated_memory_functions.json}"

echo "[+] NSPA root     : $NSPA_ROOT"
echo "[+] SVF build dir : $SVF_BUILD_DIR"
echo "[+] Curl bc dir   : $CURL_BC_DIR"
echo "[+] JSON file     : $JSON_FILE"

if [ ! -d "$SVF_BUILD_DIR" ]; then
  echo "[-] SVF build directory not found: $SVF_BUILD_DIR"
  exit 1
fi

if [ ! -d "$CURL_BC_DIR" ]; then
  echo "[-] Curl bitcode directory not found: $CURL_BC_DIR"
  exit 1
fi

if [ ! -f "$JSON_FILE" ]; then
  echo "[-] JSON file not found: $JSON_FILE"
  exit 1
fi

echo "[+] Rebuilding SVF/Saber..."
cd "$SVF_BUILD_DIR"
make -j"$(nproc)"

echo "[+] Current saber:"
which saber || true
ls -lh "$(which saber)" || true

TMP_FUNCS="$(mktemp)"
TMP_REPORT="$NSPA_ROOT/workspace/curl_saber_function_presence.tsv"
mkdir -p "$NSPA_ROOT/workspace"

python3 - "$JSON_FILE" > "$TMP_FUNCS" <<'PY'
import json
import sys

path = sys.argv[1]
data = json.load(open(path, encoding="utf-8"))

skip_names = {"main", "CURLX_MALLOC"}
skip_prefixes = ("docs/examples/", "tests/")

for fn in data.get("functions", []):
    name = fn.get("name", "")
    file_path = fn.get("file", "")
    entity = fn.get("cfr", {}).get("entity_kind", "")
    conf = float(fn.get("confidence", 0.0))
    cat = fn.get("category", "")

    if name in skip_names:
        continue
    if entity == "function_like_macro":
        continue
    if file_path.startswith(skip_prefixes):
        continue
    if conf < 0.75:
        continue

    print(name)
PY

sort -u "$TMP_FUNCS" -o "$TMP_FUNCS"

echo -e "function\tstatus\tbc_file" > "$TMP_REPORT"

echo "[+] Checking whether functions exist in Curl bitcode..."
while read -r fn; do
  [ -z "$fn" ] && continue

  hit_files="$(
    find "$CURL_BC_DIR" -name "*.bc" -type f | while read -r bc; do
      if llvm-nm "$bc" 2>/dev/null | grep -Eq "([[:space:]]|^)$fn$|([[:space:]]|^)$fn[[:space:]]"; then
        echo "$bc"
      fi
    done
  )"

  if [ -n "$hit_files" ]; then
    echo "$hit_files" | while read -r f; do
      echo -e "$fn\tFOUND\t${f#$NSPA_ROOT/}" >> "$TMP_REPORT"
    done
  else
    echo -e "$fn\tNOT_FOUND\t-" >> "$TMP_REPORT"
  fi
done < "$TMP_FUNCS"

rm -f "$TMP_FUNCS"

echo "[+] Function presence report:"
echo "    $TMP_REPORT"

echo "[+] Summary:"
echo "    FOUND     : $(grep -c $'\tFOUND\t' "$TMP_REPORT" || true)"
echo "    NOT_FOUND : $(grep -c $'\tNOT_FOUND\t' "$TMP_REPORT" || true)"

echo "[+] First 30 lines:"
head -n 30 "$TMP_REPORT"