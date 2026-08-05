/* fn_004148a0 -- the data-file decrypt the game did one byte at a time.
 *
 * One of the hand-written native replacements for recompiled functions; see
 * runtime/overrides/overrides.h for how the set is divided and why.
 */

#include "overrides.h"

#include "../guest_ops.h"
#include "../guest_map.h"
#include "../hostwin.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------------------
 * fn_004148a0 -- decrypt one data file into data\temporary.txt.
 *
 * This is the data load. Every one of the game's ~150 object files goes through it, and
 * the original does it ONE BYTE AT A TIME through the C runtime:
 *
 *     fscanf(in, "%c", &c);  ...  fprintf(out, "%c", c - key[i]);
 *
 * which is fine at native speed and is not fine through a recompiled CPU, where each of
 * those is a guest call into a host import. It came to 2.5 million fscanf calls per load,
 * and it is why every attempt to speed the load up by touching the RENDERING failed: the
 * drawing was measured at 14% of the load (LF2_LOAD_PROF), and this is most of the rest.
 *
 * The cipher, read straight out of the decompiled body rather than guessed:
 *   key    "SiuHungIsAGoodBearBecauseHeIsVeryGood", 37 bytes
 *   header the first 0x7b = 123 bytes are consumed and discarded, and the key index is
 *          advanced once per consumed byte, so the payload starts at key index 123 % 37 = 12
 *   byte   out = (in - key[i]) mod 256, then i = (i + 1) % 37
 *
 * (Index 12 is where "odBearBecauseHeIsVeryGood" starts, which is why the widely circulated
 * 25-character key decrypts the first 25 bytes of a file and then turns to noise -- it is
 * this key seen from its offset, with the wrap missing.)
 *
 * Byte-exactness matters more than speed here, so the two things the CRT does that a naive
 * port would not are both reproduced: the input is opened in TEXT mode, so CRLF collapses
 * to LF before decryption (lf2_open_text), and the output is written raw, which is what the
 * port's own "w" fopen does. Anything that cannot be done -- a missing input, an unwritable
 * output -- falls through to the original body rather than silently producing a short file,
 * because a truncated decrypt would show up as the game quietly missing objects.
 *
 * Calling convention: cdecl. The generated body ends in `R(ESP) += 4`, so the argument is
 * the caller's to pop and only the return address comes off here.
 * ------------------------------------------------------------------------ */
const char *lf2_host_path(const char *guest_style);      /* imports.c */
char       *lf2_read_text(const char *host_path, size_t *len);   /* imports.c */

long decrypt_files, decrypt_bytes;

void fn_004148a0__orig(void);

/* LF2_DECRYPT_DUMP=<dir> copies each decrypted file out as NNN.txt, in order. Run once with
 * LF2_SLOW_DECRYPT=1 and once without, diff the two directories, and the native decrypt is
 * either byte-identical to the game's own on every file or it is not -- which is the only
 * check worth having, since a decrypt that is subtly wrong shows up as the game quietly
 * missing frames rather than as a crash. It has to sit in the override and not in the fast
 * path, or the control run dumps nothing and the diff reads as a pass. */
static void decrypt_dump(void)
{
    const char *dir = getenv("LF2_DECRYPT_DUMP");
    if (!dir || !*dir) return;
    static int n;
    char dst[512];
    snprintf(dst, sizeof dst, "%s/%04d.txt", dir, n++);
    FILE *in = fopen(lf2_host_path("data\\temporary.txt"), "rb");
    if (!in) { fprintf(stderr, "decrypt dump: cannot read the output for %s\n", dst); return; }
    FILE *out = fopen(dst, "wb");
    if (!out) { fclose(in); fprintf(stderr, "decrypt dump: cannot write %s\n", dst); return; }
    char b[65536]; size_t got;
    while ((got = fread(b, 1, sizeof b, in)) > 0) fwrite(b, 1, got, out);
    fclose(in); fclose(out);
}

void fn_004148a0(void)
{
    static int native = -1;
    if (native < 0) native = getenv("LF2_SLOW_DECRYPT") == NULL;
    if (!native) { fn_004148a0__orig(); decrypt_dump(); return; }

    static const char KEY[] = "SiuHungIsAGoodBearBecauseHeIsVeryGood";
    enum { KEYLEN = 37, HEADER = 0x7b };
    _Static_assert(sizeof KEY - 1 == KEYLEN, "the key length is part of the cipher");

    const uint32_t arg = LD32(R(ESP) + 4);
    const char *src = lf2_host_path((const char *)(g_mem + arg));

    size_t n = 0;
    char *buf = lf2_read_text(src, &n);
    if (!buf) { fn_004148a0__orig(); return; }

    FILE *out = fopen(lf2_host_path("data\\temporary.txt"), "w");
    if (!out) { free(buf); fn_004148a0__orig(); return; }

    unsigned ki = HEADER % KEYLEN;
    for (size_t i = HEADER; i < n; i++) {
        const int v = ((int)(unsigned char)buf[i] - (int)(unsigned char)KEY[ki]) & 0xff;
        ki = (ki + 1u) % KEYLEN;
        fputc(v, out);
    }
    decrypt_files++;
    decrypt_bytes += (long)(n > HEADER ? n - HEADER : 0);

    fclose(out);
    free(buf);
    decrypt_dump();
    R(ESP) += 4;                 /* cdecl: the return address only */
}
