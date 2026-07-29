/* Replay every instruction Ghidra found and check our decoder agrees on the length.
 *
 * Usage: test_corpus <instructions.tsv>
 * TSV columns: address, length, mnemonic, hex bytes, disassembly text. */
#include "x86_decode.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { MAX_LINE = 1024, MAX_REPORT = 15 };

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Per-mnemonic failure tally, so a miss points at the opcode group to fix next. */
typedef struct { char name[32]; long count; } Tally;
static Tally tally[256];
static int tally_len;

static void tally_add(const char *mnemonic)
{
    for (int i = 0; i < tally_len; i++) {
        if (strcmp(tally[i].name, mnemonic) == 0) { tally[i].count++; return; }
    }
    if (tally_len == (int)(sizeof tally / sizeof tally[0])) return;
    snprintf(tally[tally_len].name, sizeof tally[0].name, "%s", mnemonic);
    tally[tally_len++].count = 1;
}

static int by_count(const void *a, const void *b)
{
    long d = ((const Tally *)b)->count - ((const Tally *)a)->count;
    return d < 0 ? -1 : d > 0 ? 1 : 0;
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: %s <instructions.tsv>\n", argv[0]);
        return 2;
    }
    FILE *fh = fopen(argv[1], "r");
    if (!fh) { perror(argv[1]); return 2; }

    long total = 0, ok = 0, bad_len = 0, undecodable = 0, reported = 0;
    char line[MAX_LINE];

    while (fgets(line, sizeof line, fh)) {
        char *addr = strtok(line, "\t");
        char *len_s = strtok(NULL, "\t");
        char *mnemonic = strtok(NULL, "\t");
        char *hex = strtok(NULL, "\t");
        if (!addr || !len_s || !mnemonic || !hex) continue;

        uint8_t bytes[X86_MAX_INSN_LEN];
        size_t n = 0;
        for (const char *q = hex; q[0] && q[1] && n < sizeof bytes; q += 2) {
            int hi = hexval(q[0]), lo = hexval(q[1]);
            if (hi < 0 || lo < 0) break;
            bytes[n++] = (uint8_t)((hi << 4) | lo);
        }
        if (!n) continue;

        total++;
        const long want = strtol(len_s, NULL, 10);

        x86_insn insn;
        if (!x86_decode(bytes, n, &insn)) {
            undecodable++;
            tally_add(mnemonic);
            if (reported < MAX_REPORT) {
                fprintf(stderr, "  UNDECODABLE %s  %-12s %s\n", addr, mnemonic, hex);
                reported++;
            }
            continue;
        }
        if (insn.length != want) {
            bad_len++;
            tally_add(mnemonic);
            if (reported < MAX_REPORT) {
                fprintf(stderr, "  LEN %s  %-12s want %ld got %u   %s\n",
                        addr, mnemonic, want, insn.length, hex);
                reported++;
            }
            continue;
        }
        ok++;
    }
    fclose(fh);

    /* An empty corpus would otherwise satisfy ok == total and report success having
     * tested nothing -- a missing or unparsable TSV must fail loudly, not pass. */
    if (total == 0) {
        fprintf(stderr, "corpus is empty: %s parsed 0 instructions\n", argv[1]);
        return 2;
    }

    printf("\n%ld instructions: %ld ok (%.4f%%), %ld wrong length, %ld undecodable\n",
           total, ok, ok * 100.0 / (double)total, bad_len, undecodable);

    if (tally_len) {
        qsort(tally, (size_t)tally_len, sizeof tally[0], by_count);
        printf("failures by mnemonic:");
        for (int i = 0; i < tally_len && i < 12; i++) printf(" %s(%ld)", tally[i].name, tally[i].count);
        printf("\n");
    }
    return (ok == total) ? 0 : 1;
}
