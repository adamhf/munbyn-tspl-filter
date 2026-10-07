# munbyn-tspl-filter

Native CUPS filter for the Munbyn ITPP130 label printer on Apple Silicon.

Munbyn's macOS driver ships `/Library/Printers/ITPP130/Filter/rastertolabel` as an x86_64-only binary, so it needs Rosetta. Without Rosetta, macOS shows "Software Incompatible" (`com.apple.badarch-error` in the CUPS log). Apple has said macOS 27 is the last release with general Rosetta support.

`rastertotspl` is a universal (arm64 + x86_64) replacement. For the 8-bit greyscale rasters the printer's PPD asks for, it reads the same PPD options and writes the same TSPL as the vendor filter, byte for byte. The exceptions are listed under "Differences".

## Requirements

- macOS on Apple Silicon (it also builds for Intel)
- Xcode Command Line Tools: `clang`, plus the CUPS headers in the SDK
- Python 3 for the tests
- Munbyn's ITPP130 macOS driver (v1.5.8), installed first. This project replaces its filter and reuses its PPD.

## Install

### 1. Install Munbyn's driver

Download the ITPP130 macOS driver from Munbyn (their FAQ links it as https://bit.ly/driver130) and run the installer. It provides the PPD and the Intel-only filter that this project replaces. You don't need Rosetta for any of this.

This project was built and tested against driver **v1.5.8**. With another version, see [Other driver versions](#other-driver-versions).

### 2. Add the printer

Plug the printer in by USB, then open **System Settings → Printers & Scanners → Add Printer**. Pick the ITPP130 and the "ITPP130 Label printer" driver. To see the queue name, run:

```bash
lpstat -p
```

It's usually `Printer_ITPP130`. The commands below use that name, so replace it if yours is different.

### 3. Build, test and install

```bash
xcode-select --install        # only if the Command Line Tools aren't installed yet
git clone https://github.com/adamhf/munbyn-tspl-filter.git
cd munbyn-tspl-filter
make && make test
sudo ./install.sh             # add --queue NAME if your queue isn't Printer_ITPP130
```

`install.sh` refuses to run while the queue has jobs waiting or printing, because changing a queue's PPD makes CUPS restart the active job. Wait for them to finish, or cancel them with `cancel -a Printer_ITPP130`.

### 4. Set your label defaults

For 3×2" labels:

```bash
lpadmin -p Printer_ITPP130 -o PageSize=w216h144 -o PageRegion=w216h144 -o AdjustHoriaontal=1
```

- **`AdjustHoriaontal`** (Munbyn's spelling) is the Horizontal Offset in whole millimetres. `1` moves prints 1 mm right, which centred the labels on the printer this was built against.
- **Other label sizes:** use that size's name from `lpoptions -p Printer_ITPP130 -l | grep PageSize`, for example `w288h432` for "4.00x6.00". Only the 3×2" size is corrected to its true dimensions (see below). Munbyn's other sizes are slightly smaller than their names: "4.00x6.00" is really 100×150 mm, which suits metric stock.

Then run `sudo ./install.sh` again. It copies these defaults into the installed PPD, so they survive the printer being removed and added again.

### 5. Check it worked

```bash
lpstat -p Printer_ITPP130                                 # should say "idle"
grep cupsFilter /private/etc/cups/ppd/Printer_ITPP130.ppd # should end in rastertotspl
```

Print a test label from any app. If it isn't centred, change `AdjustHoriaontal` (negative values move left) and print again.

### Updating

```bash
git pull && make && make test && sudo ./install.sh
```

The queue keeps its settings.

### Uninstalling

```bash
sudo ./install.sh --revert
sudo rm /Library/Printers/ITPP130/Filter/rastertotspl
```

This puts the vendor PPDs back and points the queue at Munbyn's Intel-only filter again, which needs Rosetta.

### Other driver versions

`make test` compares against the exact v1.5.8 PPD. With another driver version it stops with "Couldn't find Munbyn's original ITPP130 PPD". If Munbyn's PPD still has the same options, the filter and installer work as normal. You can run the behaviour checks against your PPD before installing:

```bash
cd test && PPD="/Library/Printers/ITPP130/PPDs/ITPP130 Label printer.ppd" python3 checks.py
```

Run these checks before `install.sh`: after it, that file is the patched copy.

### Troubleshooting

- **"Software Incompatible" or "Bad CPU type" in Printers & Scanners:**
  - Check the filter: `grep cupsFilter /private/etc/cups/ppd/Printer_ITPP130.ppd`. If it ends in `rastertolabel`, the queue still uses Munbyn's Intel-only filter, so run `sudo ./install.sh`.
  - If it already ends in `rastertotspl`, the state left over from the old filter can stick. It survives prints and CUPS restarts. Remove the printer in System Settings (or run `lpadmin -x Printer_ITPP130`), add it again, then repeat steps 3 and 4.
- **A QR code or fine detail won't scan, or looks smudged:** check the queue uses the 50% threshold (`lpoptions -p Printer_ITPP130 -l | grep Threshold` should show `*128`), then lower `Darkness`, for example `lpadmin -p Printer_ITPP130 -o Darkness=6`. Higher darkness spreads each dot. Export label images at 203 dpi where the app allows it, so CUPS doesn't have to scale them.
- **A print runs across several labels:** the job used a bigger page size than the loaded labels, usually the 4×6 default. Set the default size (step 4), or pick the label size in the print dialog.
- **"Printer drivers are deprecated and will stop working in a future version of CUPS":** CUPS prints this for every PPD-based driver. It doesn't affect printing.
- **CUPS log:** `/private/var/log/cups/error_log`. Filter problems appear as `ERROR:` lines.

## What install.sh does

1. Installs the filter as `/Library/Printers/ITPP130/Filter/rastertotspl`. The vendor filter stays in place.
2. Backs up both vendor PPDs to `/Library/Printers/ITPP130/vendor-backup`. This folder is outside the folders CUPS searches for drivers, so the backups don't show up as a second "ITPP130 Label printer" driver.
3. Replaces both vendor PPDs with one that:
   - uses the new filter
   - has a true-size 3×2" label (see below)
   - adds a Grey Threshold option, default 128 (see "Differences"); a re-install keeps the queue's chosen value
   - keeps the queue's current defaults

   Re-adding the printer in System Settings then gives the same setup.
4. Points the queue at that PPD and keeps its settings.

`--revert` restores the vendor PPDs from the backup and points the queue back at the vendor filter, keeping its settings. `--queue NAME` picks a queue other than `Printer_ITPP130`.

### The 3×2" label size

The vendor PPD's "3.00x2.00" size is really 212×142 pt (74.8×50 mm). On real 3×2" labels (76.2×50.8 mm), prints came out 1.4 mm narrow, sat to the left, and lost their right and top edges. The installed PPD makes it 216×144 pt.

The printer also starts printing about 1 mm to the left of the label edge. The queue's Horizontal Offset of 1 mm (`AdjustHoriaontal=1`) corrects that, and the installer keeps it.

## Differences from the vendor filter

- **Rotate 90/270:** the vendor sends `DIRECTION 2,0` / `DIRECTION 3,0`, which TSPL doesn't define (it only accepts 0 or 1). This filter rotates the bitmap itself, with 90 meaning clockwise, and sends `DIRECTION 0,0`. A page that is wider than the print head once rotated is rejected.
- **Grey threshold:** the vendor filter prints every grey value below 201 as black. That includes the light-grey edge pixels CUPS adds when it scales an image, so black areas grow by about a dot on each side. Add the printer's heat bleed and the white gaps in a small QR code close up: a 14 mm Bambuddy spool-label QR stopped scanning. The installed PPD has a **Grey Threshold** option (`Threshold`), with 128 (50%) as the default. Choose 201 for the vendor's output. Without the option in the PPD, `-o Threshold=N` (1–255) sets it, and if nothing sets it the filter uses 201, so `make test` still compares byte for byte with the vendor.
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

The image is sent at 1 bit per dot, with 1 meaning white. Each grey value below the threshold (128 with the installed PPD, 201 for the vendor) prints black. The header comment in `rastertotspl.c` maps each PPD option to its command.

If a job is cancelled or its input ends mid-page, the filter fills the rest of the bitmap with white and skips `PRINT`, so the printer doesn't feed a label. The NUL preamble only helps when the previous bitmap is at most 1 KB short. If CUPS kills a job outright in the middle of a bitmap, power-cycle the printer before the next job.

## Tests

All test files are in `test/`. GitHub Actions (`.github/workflows/ci.yml`) builds and runs `make test` on macOS for every push and pull request. It gets Munbyn's PPD from the repository secret `VENDOR_PPD_B64` (base64 of the PPD), because the PPD can't be committed. Without the secret, for example on pull requests from forks, CI only builds.

- **`compare.sh`:** checks the filter's output for every fixture and option set against `expected.sha256`, which holds hashes of the vendor filter's output. Run `compare.sh --regen` to rebuild the hashes from the vendor filter. That needs Rosetta, and the default printer must be the ITPP130 queue, because the vendor filter refuses to run otherwise.
- **`checks.py`:** tests the deliberate differences: malformed headers, colour spaces, missing PPD, truncation, rotation, copies, and cancel between pages.
- **`mkfixtures.sh`:** rebuilds every fixture. It runs `mkpdf.py` (test PDFs), `cupsfilter` with the vendor PPD (rasters from the real macOS pipeline), and `synth.py`. `synth.py` makes `2p.ras` (two pages), `ramp.ras` (grey ramp to find the threshold) and `nc3.ras` (`NumCopies` 3).
- **`vendor-ppd.sh`:** finds an unmodified copy of Munbyn's PPD (driver v1.5.8) on the Mac. It looks in the installed driver or `install.sh`'s backup, and checks the copy by its hash. The PPD isn't ours to redistribute, so it isn't in the repo. To use another copy, set `VENDOR_PPD=path`.
- **`hdr.py` / `tspl2png.py`:** `hdr.py` prints the text part of a TSPL job. `tspl2png.py` renders its first bitmap as a PNG.

## License

MIT; see [LICENSE](LICENSE). Munbyn's driver, PPD and filter are not part of this project and are not covered by this licence.
