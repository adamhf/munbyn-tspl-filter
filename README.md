# munbyn-tspl-filter

Native CUPS filter for the Munbyn ITPP130 label printer on Apple Silicon.

Munbyn's macOS driver ships `/Library/Printers/ITPP130/Filter/rastertolabel` as an x86_64-only binary, so it needs Rosetta. Without Rosetta, macOS shows "Software Incompatible" (`com.apple.badarch-error` in the CUPS log). Apple has said macOS 27 is the last release with general Rosetta support.

`rastertotspl` is a universal (arm64 + x86_64) replacement. For the 8-bit greyscale rasters the printer's PPD asks for, it reads the same PPD options and writes the same TSPL as the vendor filter, byte for byte. The exceptions are listed under "Differences".

## Requirements

- macOS on Apple Silicon (it also builds for Intel)
- Xcode Command Line Tools: `clang`, plus the CUPS headers in the SDK
- Python 3 for the tests
- Munbyn's ITPP130 macOS driver, installed first. This project replaces its filter and reuses its PPD.

## Install on a new Mac

1. **Install Munbyn's driver.** Use the ITPP130 macOS driver from Munbyn's support site (this project was built against v1.5.8). It provides the PPD and the Intel-only filter that this project replaces. You don't need Rosetta.
2. **Add the printer.** Plug it in by USB, then open **System Settings → Printers & Scanners → Add Printer**. Pick the ITPP130 and the "ITPP130 Label printer" driver. The queue is usually named `Printer_ITPP130`; check with `lpstat -p`.
3. **Build, test and install:**

   ```bash
   xcode-select --install        # if the Command Line Tools aren't installed yet
   git clone https://github.com/adamhf/munbyn-tspl-filter.git
   cd munbyn-tspl-filter
   make && make test
   sudo ./install.sh             # add --queue NAME if the queue isn't Printer_ITPP130
   ```

4. **Set your label defaults.** For 3×2" labels:

   ```bash
   lpadmin -p Printer_ITPP130 -o PageSize=w216h144 -o PageRegion=w216h144 -o AdjustHoriaontal=1
   ```

   `AdjustHoriaontal=1` (Munbyn's spelling) moves prints 1 mm right. On the printer this was built against, that offset centres the label. Print one test label and adjust if needed.

5. **Optional:** run `sudo ./install.sh` again. It copies the queue's defaults into the installed PPD, so the printer keeps them if you ever re-add it.

## Build, test, install

```bash
make            # build + ad-hoc sign
make test       # vendor-parity hashes + behaviour checks (no Rosetta needed)
sudo ./install.sh
```

`install.sh` does four things:

1. Installs the filter as `/Library/Printers/ITPP130/Filter/rastertotspl`. The vendor filter stays in place.
2. Backs up both vendor PPDs to `/Library/Printers/ITPP130/vendor-backup`. This folder is outside the folders CUPS searches for drivers, so the backups don't show up as a second "ITPP130 Label printer" driver.
3. Replaces both vendor PPDs with one that:
   - uses the new filter
   - has a true-size 3×2" label (see below)
   - keeps the queue's current defaults

   Re-adding the printer in System Settings then gives the same setup.
4. Points the queue at that PPD and keeps its settings. The default queue is `Printer_ITPP130`; for a different queue, use `sudo ./install.sh --queue NAME`.

It refuses to run while the queue has jobs waiting or printing, because changing a queue's PPD makes CUPS restart the active job.

To undo: `sudo ./install.sh --revert`. This restores the vendor PPDs from the backup and points the queue back at the vendor filter, keeping its settings.

### The 3×2" label size

The vendor PPD's "3.00x2.00" size is really 212×142 pt (74.8×50 mm). On real 3×2" labels (76.2×50.8 mm), prints came out 1.4 mm narrow, sat to the left, and lost their right and top edges. The installed PPD makes it 216×144 pt.

The printer also starts printing about 1 mm to the left of the label edge. The queue's Horizontal Offset of 1 mm (`AdjustHoriaontal=1`) corrects that, and the installer keeps it.

## Differences from the vendor filter

- **Rotate 90/270:** the vendor sends `DIRECTION 2,0` / `DIRECTION 3,0`, which TSPL doesn't define (it only accepts 0 or 1). This filter rotates the bitmap itself, with 90 meaning clockwise, and sends `DIRECTION 0,0`. A page that is wider than the print head once rotated is rejected.
- **Other colour formats:** sGray (SW) and K rasters print with the right polarity, and 1-bit rasters work. Any other colour space is rejected.
- **Bad input fails the job:** the job fails (`ERROR:` plus exit 1) instead of printing something wrong when:
  - a raster header is malformed or oversized
  - the PPD can't be opened
  - the input is truncated
  - a write fails

  Raw `application/vnd.cups-raster` jobs reach the filter unchanged, so it checks the header's dimensions itself.

## What it sends

For each page:

1. 1024 NUL bytes.
2. Setup commands: `SIZE`, `REFERENCE`, `DIRECTION`, `GAP`/`BLINE`, `DENSITY`, `SPEED`, `SETC AUTODOTTED|PAUSEKEY|WATERMARK`, `CLS`.
3. `BITMAP 0,0,wb,h,1,` followed by the image data.
4. `PRINT 1,1`.

The image is sent at 1 bit per dot, with 1 meaning white. Each grey value below 201 prints black. The header comment in `rastertotspl.c` maps each PPD option to its command.

If a job is cancelled or its input ends mid-page, the filter fills the rest of the bitmap with white and skips `PRINT`, so the printer doesn't feed a label. The NUL preamble only helps when the previous bitmap is at most 1 KB short. If CUPS kills a job outright in the middle of a bitmap, power-cycle the printer before the next job.

## Tests

All test files are in `test/`.

- **`compare.sh`:** checks the filter's output for every fixture and option set against `expected.sha256`, which holds hashes of the vendor filter's output. Run `compare.sh --regen` to rebuild the hashes from the vendor filter. That needs Rosetta, and the default printer must be the ITPP130 queue, because the vendor filter refuses to run otherwise.
- **`checks.py`:** tests the deliberate differences: malformed headers, colour spaces, missing PPD, truncation, rotation, copies, and cancel between pages.
- **`mkfixtures.sh`:** rebuilds every fixture. It runs `mkpdf.py` (test PDFs), `cupsfilter` with the vendor PPD (rasters from the real macOS pipeline), and `synth.py`. `synth.py` makes `2p.ras` (two pages), `ramp.ras` (grey ramp to find the threshold) and `nc3.ras` (`NumCopies` 3).
- **`vendor-ppd.sh`:** finds an unmodified copy of Munbyn's PPD (driver v1.5.8) on the Mac. It looks in the installed driver or `install.sh`'s backup, and checks the copy by its hash. The PPD isn't ours to redistribute, so it isn't in the repo. To use another copy, set `VENDOR_PPD=path`.
- **`hdr.py` / `tspl2png.py`:** `hdr.py` prints the text part of a TSPL job. `tspl2png.py` renders its first bitmap as a PNG.

## License

MIT; see [LICENSE](LICENSE). Munbyn's driver, PPD and filter are not part of this project and are not covered by this licence.
