#!/bin/bash
# Rebuild every test fixture: PDFs, rasters from the real macOS pipeline
# (cupsfilter -> cgpdftoraster, using the vendor PPD found by vendor-ppd.sh), and the
# synthetic rasters. After changing fixtures, rebuild expected.sha256 with
# ./compare.sh --regen (needs the vendor filter, i.e. Rosetta).
set -euo pipefail
cd "$(dirname "$0")"
PPD=$(./vendor-ppd.sh)
raster() { cupsfilter -p "$PPD" -m application/vnd.cups-raster -o PageSize="$1" "$2" 2>/dev/null; }

python3 mkpdf.py
raster w288h432 4x6.pdf > 4x6.ras
raster w216h144 3x2.pdf > sz_w216h144.ras
for ps in w90h18 w108h36 w144h26 w171h396 w234h419 w288h936 Custom.60x40mm Custom.2.25x1.25in; do
  raster "$ps" odd.pdf > "sz_$ps.ras"
done
python3 synth.py
