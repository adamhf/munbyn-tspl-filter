# CLAUDE.md

Native CUPS filter (`rastertotspl.c`) for the Munbyn ITPP130 thermal label printer on Apple Silicon Macs. It replaces Munbyn's x86_64-only filter, `/Library/Printers/ITPP130/Filter/rastertolabel`. That filter needs Rosetta, and Apple says macOS 27 is the last release with general Rosetta support. See README.md for what the filter sends and how it differs from the vendor's.

## Commands

```bash
make              # universal arm64 + x86_64 build, ad-hoc signed
make test         # test/compare.sh (vendor parity) + test/checks.py (behaviour)
sudo ./install.sh [--queue NAME]   # needs the user's password; ask them to run it
sudo ./install.sh --revert
```

Test helpers live in `test/`:
- `mkfixtures.sh` rebuilds the fixtures.
- `compare.sh --regen` rebuilds `expected.sha256` from the vendor filter. It needs Rosetta, and the default printer must be the ITPP130 queue.
- `hdr.py` dumps the text part of a TSPL job.
- `tspl2png.py` renders a job's first bitmap.

## Rules

- **Vendor parity is the contract.** For 8-bit greyscale rasters, output must stay byte-identical to the vendor filter. `make test` checks this against stored hashes, so Rosetta isn't needed.
  - Any deliberate difference goes in the "Differences" list in README.md and the header comment of `rastertotspl.c`.
  - Exclude it from `compare.sh`'s option list and add a test to `checks.py`. Rotate 90/270 is the existing example.
- **Treat raster headers as untrusted.** Raw `application/vnd.cups-raster` jobs reach the filter unchanged, and macOS libcups does not check that the dimensions are consistent.
  - Every header goes through `check_header()` before any buffer is sized from it.
  - Keep a `checks.py` case for each kind of malformed input.
- **Failures fail the job.** On failure, print an `ERROR:` line and exit 1. Never print a guess, and never exit 0 after dropping a page. On cancel (SIGTERM), finish the current BITMAP with white and skip `PRINT`.
- **Never commit vendor files.** That includes the PPD, the filter binary, and anything from Munbyn's driver package. The repo is public.
- **Get raster header offsets from `offsetof`, never by counting.** One fixture was once patched at `Jog` (304) instead of `NumCopies` (340). In the file, add the 4-byte sync word.
- **Don't send real print jobs** (`lp`/`lpr`) without asking: every job uses a physical label. Test by running the filter directly: `PPD=$(test/vendor-ppd.sh) ./rastertotspl 1 u t 1 "<options>" <file.ras>`.
- **Don't change the live queue or `/Library/Printers`** (lpadmin, cupsenable, the installer) without asking. Changing a queue's PPD makes CUPS restart any job that is printing.
- **The 3x2" label is 216x144 pt.** `install.sh` corrects the vendor PPD's 212x142 pt entry. The 1 mm Horizontal Offset (`AdjustHoriaontal=1`, the vendor's spelling) makes up for the printer starting about 1 mm left of the label edge. It is a calibration setting, not a bug.

## Layout

| Path | Contents |
|---|---|
| `rastertotspl.c` | The filter. The header comment maps each PPD option to the TSPL command it produces. |
| `install.sh` | Installs the filter. Backs up the vendor PPDs to `/Library/Printers/ITPP130/vendor-backup`, outside CUPS's driver search path. Writes a native PPD that carries the queue's defaults. |
| `test/vendor-ppd.sh` | Finds an unmodified copy of Munbyn's PPD on the Mac. The tests need it, but it's Munbyn's file, so it is never committed. |
| `test/*.ras` | Fixtures: rasters from the real macOS pipeline, plus synthetic ones from `synth.py`. |
| `test/expected.sha256` | sha256 of the vendor filter's output for every fixture and option set. |
