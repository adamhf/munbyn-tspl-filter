#!/bin/bash
# Vendor-parity test: rastertotspl must produce the same bytes as the vendor
# filter for every test raster x option set.
#
#   ./compare.sh          check against expected.sha256 (no Rosetta needed)
#   ./compare.sh --regen  rebuild expected.sha256 from the vendor filter
#                         (needs Rosetta, and the default printer must be the
#                         ITPP130 queue: the vendor filter refuses otherwise)
#
# Rotate 90/270 are left out on purpose: the vendor sends DIRECTION 2/3,
# which TSPL doesn't define, while rastertotspl rotates the bitmap. They are
# covered by checks.py, with the error-handling tests.
set -uo pipefail
cd "$(dirname "$0")"
REF=/Library/Printers/ITPP130/Filter/rastertolabel
NEW=../rastertotspl
PPD=$(./vendor-ppd.sh) || exit 1
export PPD
OPTS=(
  ""
  "Darkness=Default zePrintRate=Default"
  "Darkness=0 zePrintRate=1"
  "Darkness=15 zePrintRate=8"
  "zePrintRate=2"
  "zeMediaTracking=Continuous GapOrMarkHeight=6 GapOrMarkOffset=4"
  "zeMediaTracking=BLine GapOrMarkHeight=5 GapOrMarkOffset=2"
  "zeMediaTracking=Gap GapOrMarkHeight=0 GapOrMarkOffset=10"
  "AdjustHoriaontal=-20 AdjustVertical=13"
  "AdjustHoriaontal=20 AdjustVertical=-20"
  "Rotate=1"
  "AutoDotted=1"
  "MediaMethod=Direct"
  "Darkness=3 zePrintRate=6 Rotate=1 AutoDotted=1 zeMediaTracking=BLine GapOrMarkHeight=2 AdjustVertical=-5"
)

shopt -s nullglob
RASTERS=(*.ras)
if [ ${#RASTERS[@]} -eq 0 ]; then
  echo "No test rasters found; run ./mkfixtures.sh" >&2
  exit 1
fi

OUT=$(mktemp)
trap 'rm -f "$OUT"' EXIT

# sha256 of a filter's stdout; fails if the filter fails or prints nothing.
digest() {
  "$@" > "$OUT" 2>/dev/null && [ -s "$OUT" ] && shasum -a 256 < "$OUT" | cut -c1-64
}

if [ "${1:-}" = --regen ]; then
  : > expected.sha256.new
  for ras in "${RASTERS[@]}"; do
    for o in "${OPTS[@]}"; do
      sha=$(digest "$REF" 1 u t 1 "$o" "$ras") || { echo "vendor filter failed: $ras '$o'" >&2; rm -f expected.sha256.new; exit 1; }
      printf '%s\t%s\t%s\n' "$sha" "$ras" "$o" >> expected.sha256.new
    done
  done
  mv expected.sha256.new expected.sha256
  echo "Wrote $(wc -l < expected.sha256 | tr -d ' ') vendor hashes to expected.sha256"
  exit 0
fi

[ -f expected.sha256 ] || { echo "expected.sha256 missing; run ./compare.sh --regen" >&2; exit 1; }
want=$(( ${#RASTERS[@]} * ${#OPTS[@]} ))
have=$(wc -l < expected.sha256 | tr -d ' ')
[ "$have" -eq "$want" ] || { echo "expected.sha256 has $have cases, fixtures x options = $want; run ./compare.sh --regen" >&2; exit 1; }

pass=0 fail=0
while IFS=$'\t' read -r sha ras o; do
  got=$(digest "$NEW" 1 u t 1 "$o" "$ras") || got="(filter failed)"
  if [ "$got" = "$sha" ]; then pass=$((pass+1)); else
    fail=$((fail+1)); echo "FAIL $ras opts='$o'"
  fi
done < expected.sha256
echo "vendor parity: $pass passed, $fail failed"
[ $fail -eq 0 ]
