/*
 * rastertotspl - native CUPS filter for the Munbyn ITPP130 (TSPL) label printer.
 *
 * Drop-in replacement for the vendor's x86_64-only
 * /Library/Printers/ITPP130/Filter/rastertolabel (model 20 / TSPL path).
 * For the 8-bit greyscale rasters the ITPP130 PPD asks CUPS for, it reads the
 * same PPD options and produces byte-identical output, except where noted
 * under "Differences" below.
 *
 * Per page:
 *   1024 x NUL                      same preamble as the vendor; only completes
 *                                   a BITMAP that an interrupted job left at
 *                                   most 1 KB short
 *   SIZE w mm,h mm                  ceil(dots / 8): 203 dpi is 8 dots/mm
 *   REFERENCE x,y                   AdjustHoriaontal / AdjustVertical, mm * 8
 *   DIRECTION n,0                   Rotate: 0 -> 0, 180 -> 1, 90/270 -> 0
 *   GAP h mm,o mm | BLINE h mm,o mm zeMediaTracking + GapOrMark{Height,Offset}
 *   DENSITY n                       Darkness (omitted for "Default")
 *   SPEED n                         zePrintRate, min 2 (omitted for "Default")
 *   SETC AUTODOTTED ON|OFF
 *   SETC PAUSEKEY ON
 *   SETC WATERMARK OFF
 *   CLS
 *   BITMAP 0,0,wb,h,1,<data>\n      1 bit/dot, MSB first, 1 = white
 *   PRINT 1,1
 *
 * Copies are always 1: the PPD sets cupsManualCopies so CUPS has already
 * rendered one raster page per copy.
 *
 * Differences from the vendor filter:
 *   - Rotate 90/270: the vendor sends DIRECTION 2/3, which TSPL doesn't define
 *     (n is 0 or 1). Here the bitmap is rotated (90 = clockwise) and
 *     DIRECTION 0 is sent.
 *   - sGray (SW) and K rasters print with the right polarity, and 1-bit
 *     rasters are supported. Other colour spaces are rejected.
 *   - Malformed or oversized raster headers, a missing PPD, truncated input
 *     and write errors fail the job (ERROR + exit 1) instead of printing
 *     something wrong or reading past buffers.
 *
 * Usage: rastertotspl job user title copies options [file]
 */

#include <cups/cups.h>
#include <cups/ppd.h>
#include <cups/raster.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define PREAMBLE_NULS   1024
#define WHITE_THRESHOLD 201   /* luminance values >= this print white */
#define DOTS_PER_MM     8
#define MAX_WIDTH       1024  /* dots; PPD MaxMediaWidth 294 pt = 829 dots at 203 dpi */
#define MAX_HEIGHT      16384 /* dots; PPD MaxMediaHeight 5670 pt = 15988 dots */

static volatile sig_atomic_t canceled = 0;

static void
cancel_job(int sig)
{
  (void)sig;
  canceled = 1;
}

/* Integer value of the marked choice for a PPD option, or def if unset/non-numeric. */
static int
choice_int(ppd_file_t *ppd, const char *keyword, int def)
{
  ppd_choice_t *c = ppdFindMarkedChoice(ppd, keyword);
  char         *end;
  long          v;

  if (!c)
    return def;
  v = strtol(c->choice, &end, 10);
  return (end == c->choice || *end) ? def : (int)v;
}

static const char *
choice_str(ppd_file_t *ppd, const char *keyword)
{
  ppd_choice_t *c = ppdFindMarkedChoice(ppd, keyword);

  return c ? c->choice : NULL;
}

/* realloc that frees the old block on failure. */
static void *
grow(void *p, size_t size)
{
  void *q = realloc(p, size);

  if (!q)
    free(p);
  return q;
}

/*
 * Reject anything pack_line can't read safely or the printer can't print.
 * libcups doesn't check that the header's dimensions are consistent, and a
 * raw application/vnd.cups-raster job reaches this filter unchanged.
 */
static const char *
check_header(const cups_page_header2_t *h, int rotated)
{
  unsigned bpp = h->cupsBitsPerPixel;
  unsigned out_w = rotated ? h->cupsHeight : h->cupsWidth;
  unsigned out_h = rotated ? h->cupsWidth : h->cupsHeight;

  if ((bpp != 1 && bpp != 8) || h->cupsBitsPerColor != bpp)
    return "only 1-bit and 8-bit rasters are supported";
  if (h->cupsColorSpace != CUPS_CSPACE_W && h->cupsColorSpace != CUPS_CSPACE_SW &&
      h->cupsColorSpace != CUPS_CSPACE_K)
    return "only greyscale (W, SW or K) rasters are supported";
  if (out_w == 0 || out_w > MAX_WIDTH || out_h == 0 || out_h > MAX_HEIGHT)
    return "the page size is outside what the printer can print";
  if (h->cupsBytesPerLine < (h->cupsWidth * bpp + 7) / 8 || h->cupsBytesPerLine > MAX_HEIGHT)
    return "bytes per line doesn't match the page width";
  return NULL;
}

static void
start_page(ppd_file_t *ppd, unsigned width, unsigned height, int direction)
{
  static const char nuls[PREAMBLE_NULS];
  const char       *tracking = choice_str(ppd, "zeMediaTracking");
  const char       *s;
  unsigned          wb = (width + 7) / 8;

  fwrite(nuls, 1, sizeof(nuls), stdout);

  printf("SIZE %u mm,%u mm\r\n", wb, (height + DOTS_PER_MM - 1) / DOTS_PER_MM);
  printf("REFERENCE %d,%d\r\n",
         choice_int(ppd, "AdjustHoriaontal", 0) * DOTS_PER_MM,
         choice_int(ppd, "AdjustVertical", 0) * DOTS_PER_MM);
  printf("DIRECTION %d,0\r\n", direction);

  if (tracking && !strcmp(tracking, "Continuous"))
    printf("GAP 0 mm,0 mm\r\n");
  else
    printf("%s %d mm,%d mm\r\n",
           tracking && !strcmp(tracking, "BLine") ? "BLINE" : "GAP",
           choice_int(ppd, "GapOrMarkHeight", 0),
           choice_int(ppd, "GapOrMarkOffset", 0));

  if ((s = choice_str(ppd, "Darkness")) && strcmp(s, "Default"))
    printf("DENSITY %d\r\n", choice_int(ppd, "Darkness", 8));

  if ((s = choice_str(ppd, "zePrintRate")) && strcmp(s, "Default"))
  {
    int speed = choice_int(ppd, "zePrintRate", 4);
    printf("SPEED %d\r\n", speed < 2 ? 2 : speed);
  }

  printf("SETC AUTODOTTED %s\r\n", choice_int(ppd, "AutoDotted", 0) ? "ON" : "OFF");
  printf("SETC PAUSEKEY ON\r\n");
  printf("SETC WATERMARK OFF\r\n");
  printf("CLS\r\n");
  printf("BITMAP 0,0,%u,%u,1,", wb, height);
}

/* Pack one raster line into TSPL bits (1 = white), padding bits white. */
static void
pack_line(const cups_page_header2_t *h, const unsigned char *src, unsigned char *dst)
{
  int      ink = h->cupsColorSpace == CUPS_CSPACE_K;   /* K: high = black; W/SW: high = white */
  unsigned x;

  memset(dst, 0xff, (h->cupsWidth + 7) / 8);
  for (x = 0; x < h->cupsWidth; x ++)
  {
    int black;

    if (h->cupsBitsPerPixel == 1)
      black = ((src[x >> 3] >> (7 - (x & 7))) & 1) == ink;
    else
      black = (ink ? 255 - src[x] : src[x]) < WHITE_THRESHOLD;

    if (black)
      dst[x >> 3] &= ~(0x80 >> (x & 7));
  }
}

/* Rotate a packed w x h page by 90 degrees into dst (h x w). */
static void
rotate_page(const unsigned char *src, unsigned w, unsigned h, int clockwise, unsigned char *dst)
{
  size_t   wbi = (w + 7) / 8, wbo = (h + 7) / 8;
  unsigned x, y;

  memset(dst, 0xff, wbo * w);
  for (y = 0; y < h; y ++)
    for (x = 0; x < w; x ++)
      if (!(src[y * wbi + (x >> 3)] & (0x80 >> (x & 7))))
      {
        unsigned nx = clockwise ? h - 1 - y : y;
        unsigned ny = clockwise ? x : w - 1 - x;

        dst[ny * wbo + (nx >> 3)] &= ~(0x80 >> (nx & 7));
      }
}

int
main(int argc, char *argv[])
{
  int                 fd = 0;
  cups_raster_t      *ras;
  cups_page_header2_t header;
  ppd_file_t         *ppd;
  cups_option_t      *options = NULL;
  int                 num_options;
  int                 page = 0, status = 0;
  int                 rotate, rotated, direction;
  unsigned char      *line = NULL, *out = NULL, *pagebuf = NULL;
  struct sigaction    action;

  if (argc < 6 || argc > 7)
  {
    fprintf(stderr, "ERROR: %s job-id user title copies options [file]\n", argv[0]);
    return 1;
  }

  if (argc == 7 && (fd = open(argv[6], O_RDONLY)) == -1)
  {
    perror("ERROR: Unable to open raster file");
    return 1;
  }

  /*
   * SA_RESTART so a cancel can't interrupt a write to the backend half way
   * (stdio drops the unwritten part of its buffer on EINTR).
   */
  memset(&action, 0, sizeof(action));
  sigemptyset(&action.sa_mask);
  action.sa_handler = cancel_job;
  action.sa_flags   = SA_RESTART;
  sigaction(SIGTERM, &action, NULL);

  if (!getenv("PPD") || (ppd = ppdOpenFile(getenv("PPD"))) == NULL)
  {
    fputs("ERROR: The PPD file could not be opened.\n", stderr);
    return 1;
  }

  ppdMarkDefaults(ppd);
  num_options = cupsParseOptions(argv[5], 0, &options);
  cupsMarkOptions(ppd, num_options, options);

  rotate    = choice_int(ppd, "Rotate", 0);   /* PPD choices: 0, 1 = 180, 2 = 90, 3 = 270 */
  rotated   = rotate == 2 || rotate == 3;
  direction = rotate == 1;

  ras = cupsRasterOpen(fd, CUPS_RASTER_READ);

  while (!canceled && cupsRasterReadHeader2(ras, &header))
  {
    const char *problem;
    unsigned    wb = (header.cupsWidth + 7) / 8;
    unsigned    y;

    if (canceled)
      break;

    if ((problem = check_header(&header, rotated)) != NULL)
    {
      fprintf(stderr, "ERROR: Unsupported raster on page %d: %s.\n", page + 1, problem);
      status = 1;
      break;
    }

    page ++;
    fprintf(stderr, "PAGE: %d 1\n", page);
    fprintf(stderr, "INFO: Starting page %d.\n", page);

    if ((line = grow(line, header.cupsBytesPerLine)) == NULL ||
        (out = grow(out, rotated ? (size_t)wb * header.cupsHeight : wb)) == NULL ||
        (rotated && (pagebuf = grow(pagebuf, (size_t)((header.cupsHeight + 7) / 8) * header.cupsWidth)) == NULL))
    {
      fputs("ERROR: Out of memory.\n", stderr);
      status = 1;
      break;
    }

    if (rotated)
    {
      /* Buffer the whole page; nothing is sent until it has all arrived. */
      for (y = 0; y < header.cupsHeight; y ++)
      {
        if (canceled || cupsRasterReadPixels(ras, line, header.cupsBytesPerLine) < 1)
          break;
        pack_line(&header, line, out + (size_t)y * wb);
      }

      if (y < header.cupsHeight)
      {
        if (!canceled)
        {
          fprintf(stderr, "ERROR: Raster data for page %d ended early.\n", page);
          status = 1;
        }
        break;
      }

      rotate_page(out, header.cupsWidth, header.cupsHeight, rotate == 2, pagebuf);
      start_page(ppd, header.cupsHeight, header.cupsWidth, direction);
      fwrite(pagebuf, 1, (size_t)((header.cupsHeight + 7) / 8) * header.cupsWidth, stdout);
    }
    else
    {
      start_page(ppd, header.cupsWidth, header.cupsHeight, direction);

      for (y = 0; y < header.cupsHeight; y ++)
      {
        if (canceled || cupsRasterReadPixels(ras, line, header.cupsBytesPerLine) < 1)
          break;
        pack_line(&header, line, out);
        fwrite(out, 1, wb, stdout);
      }

      if (y < header.cupsHeight)
      {
        /*
         * Cancelled or truncated: finish the BITMAP with white so the printer
         * isn't left waiting for data, and skip PRINT so nothing is fed. The
         * header check above bounds this to MAX_WIDTH x MAX_HEIGHT dots.
         */
        memset(out, 0xff, wb);
        for (; y < header.cupsHeight; y ++)
          fwrite(out, 1, wb, stdout);
        fputs("\n", stdout);
        fflush(stdout);
        if (!canceled)
        {
          fprintf(stderr, "ERROR: Raster data for page %d ended early.\n", page);
          status = 1;
        }
        break;
      }
    }

    fputs("\nPRINT 1,1\r\n", stdout);
    if (fflush(stdout) || ferror(stdout))
    {
      fputs("ERROR: Unable to send the page to the printer.\n", stderr);
      status = 1;
      break;
    }
    fprintf(stderr, "INFO: Finished page %d.\n", page);
  }

  cupsRasterClose(ras);
  if (fd != 0)
    close(fd);
  cupsFreeOptions(num_options, options);
  ppdClose(ppd);
  free(line);
  free(out);
  free(pagebuf);

  if (page == 0 && !status && !canceled)
  {
    fputs("ERROR: No pages found.\n", stderr);
    return 1;
  }
  return (status || canceled) ? 1 : 0;
}
