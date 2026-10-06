#!/bin/bash
# Print the path of an unmodified copy of Munbyn's ITPP130 PPD (macOS driver
# v1.5.8). The tests need it, but it isn't ours to redistribute, so it's
# copied from the installed driver (or its install.sh backup) to .vendor.ppd.
# Set VENDOR_PPD=path to use another copy.
set -euo pipefail
cd "$(dirname "$0")"
SHA=0cd6692d86e281e6be232ad429d31a22ee93d5ef82b6ad98603a83391d8c904f
OUT=.vendor.ppd
ok() { [ "$(shasum -a 256 < "$1" | cut -c1-64)" = "$SHA" ]; }

if [ -f "$OUT" ] && ok "$OUT"; then echo "$PWD/$OUT"; exit 0; fi

TMP=$(mktemp)
trap 'rm -f "$TMP"' EXIT
for src in "${VENDOR_PPD:-}" \
           "/Library/Printers/ITPP130/vendor-backup/ITPP130 Label printer.ppd" \
           "/Library/Printers/ITPP130/vendor-backup/ITPP130 Label printer.ppd.gz" \
           "/Library/Printers/ITPP130/PPDs/ITPP130 Label printer.ppd" \
           "/Library/Printers/PPDs/Contents/Resources/ITPP130 Label printer.ppd.gz"; do
  [ -n "$src" ] && [ -f "$src" ] || continue
  case $src in *.gz) gzcat "$src" > "$TMP" ;; *) cp "$src" "$TMP" ;; esac
  if ok "$TMP"; then cp "$TMP" "$OUT"; echo "$PWD/$OUT"; exit 0; fi
done
echo "Couldn't find Munbyn's original ITPP130 PPD (driver v1.5.8). Install the driver, or set VENDOR_PPD=path." >&2
exit 1
