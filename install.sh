#!/bin/bash
# Install rastertotspl in place of the vendor's Intel-only rastertolabel.
#
#   sudo ./install.sh [--queue NAME]            install (queue defaults to Printer_ITPP130)
#   sudo ./install.sh --revert [--queue NAME]   go back to the vendor filter and PPDs
#
# Install copies the filter next to the vendor's, backs up both vendor PPDs
# to /Library/Printers/ITPP130/vendor-backup (outside the folders CUPS
# searches for drivers), and replaces them with a PPD that uses the new
# filter, has a true-size 3x2" label, and carries the queue's current
# defaults, so a re-added printer comes back set up the same way. The queue
# is then pointed at that PPD, keeping its settings.
#
# The queue must have no jobs waiting or printing: changing its PPD makes
# CUPS restart the active job.
set -euo pipefail

QUEUE=Printer_ITPP130
REVERT=0
usage() { sed -n '2,15s/^# \{0,1\}//p' "$0"; exit "${1:-1}"; }
while [ $# -gt 0 ]; do
  case $1 in
    --revert)  REVERT=1 ;;
    --queue)   [ $# -ge 2 ] || usage; QUEUE=$2; shift ;;
    -h|--help) usage 0 ;;
    *)         echo "Unknown option: $1" >&2; usage ;;
  esac
  shift
done
[ "$(id -u)" -eq 0 ] || { echo "Run with sudo." >&2; exit 1; }

DIR=/Library/Printers/ITPP130
NEW=$DIR/Filter/rastertotspl
OLD=$DIR/Filter/rastertolabel
BACKUP=$DIR/vendor-backup
RES_PPD="/Library/Printers/PPDs/Contents/Resources/ITPP130 Label printer.ppd.gz"
DIR_PPD="$DIR/PPDs/ITPP130 Label printer.ppd"
QUEUE_PPD="/private/etc/cups/ppd/$QUEUE.ppd"
HERE=$(cd "$(dirname "$0")" && pwd)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

# The vendor's "3.00x2.00" label is 212 x 142 pt (74.8 x 50 mm). Real 3x2"
# labels are 216 x 144 pt, so prints came out 1.4 mm narrow and cropped.
to_native() {
  sed -E -e "s|^\*cupsFilter:.*|*cupsFilter: \"application/vnd.cups-raster 0 $NEW\"|" \
         -e '/^\*(PageSize|PageRegion|ImageableArea|PaperDimension) w216h144\//s/212 142/216 144/'
}
to_vendor() {
  sed -E -e "s|^\*cupsFilter:.*|*cupsFilter: \"application/vnd.cups-raster 0 $OLD\"|" \
         -e '/^\*(PageSize|PageRegion|ImageableArea|PaperDimension) w216h144\//s/216 144/212 142/'
}

have_queue=0
if lpstat -p "$QUEUE" >/dev/null 2>&1; then
  have_queue=1
  if [ -n "$(lpstat -o "$QUEUE" 2>/dev/null)" ]; then
    echo "$QUEUE has jobs waiting or printing. Let them finish (or cancel them) and run this again." >&2
    exit 1
  fi
fi

# Back up the vendor PPDs once. Earlier versions of this script left the
# backup next to the vendor PPD, where CUPS listed it as a second driver.
mkdir -p "$BACKUP"
RES_BAK="$BACKUP/$(basename "$RES_PPD")"
DIR_BAK="$BACKUP/$(basename "$DIR_PPD")"
if [ -f "$RES_PPD.orig" ]; then
  if [ -f "$RES_BAK" ]; then rm -f "$RES_PPD.orig"; else mv "$RES_PPD.orig" "$RES_BAK"; fi
fi
[ -f "$RES_BAK" ] || cp -p "$RES_PPD" "$RES_BAK"
[ -f "$DIR_BAK" ] || cp -p "$DIR_PPD" "$DIR_BAK"

if [ $REVERT -eq 0 ]; then
  FILTER=$NEW
  install -o root -g wheel -m 755 "$HERE/rastertotspl" "$NEW"
  echo "Installed $NEW ($(lipo -archs "$NEW"))"

  # Start from the queue's PPD so its defaults (label size, offsets...) carry over.
  if [ $have_queue -eq 1 ]; then cp "$QUEUE_PPD" "$WORK/base.ppd"; else gzcat "$RES_BAK" > "$WORK/base.ppd"; fi
  to_native < "$WORK/base.ppd" > "$WORK/queue.ppd"
  gzip -9 -c "$WORK/queue.ppd" > "$WORK/queue.ppd.gz"
  install -o root -g admin -m 755 "$WORK/queue.ppd.gz" "$RES_PPD"
  install -o root -g admin -m 755 "$WORK/queue.ppd" "$DIR_PPD"
  echo "Vendor PPDs now use $NEW (originals in $BACKUP)"
else
  FILTER=$OLD
  install -o root -g admin -m 755 "$RES_BAK" "$RES_PPD"
  install -o root -g admin -m 755 "$DIR_BAK" "$DIR_PPD"
  rm -rf "$BACKUP"
  echo "Vendor PPDs restored"
  [ $have_queue -eq 0 ] || to_vendor < "$QUEUE_PPD" > "$WORK/queue.ppd"
fi

if [ $have_queue -eq 1 ]; then
  if ! msg=$(lpadmin -p "$QUEUE" -P "$WORK/queue.ppd" 2>&1); then
    printf '%s\n' "$msg" >&2
    echo "lpadmin failed; $QUEUE was not changed." >&2
    exit 1
  fi
  if ! grep -qF "$FILTER" "$QUEUE_PPD"; then
    echo "$QUEUE's PPD still doesn't use $FILTER." >&2
    exit 1
  fi
  echo "$QUEUE now uses $FILTER, with its settings kept."
else
  echo "Queue $QUEUE not found (use --queue NAME for a different name)."
  echo "Adding the printer in System Settings will use the updated PPD."
fi
