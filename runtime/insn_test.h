#ifndef INSN_TEST_H
#define INSN_TEST_H

#include <stdint.h>

typedef struct {
    const char *mnemonic;
    unsigned    len;
    uint8_t     bytes[16];
    void      (*lifted)(void);
} InsnCase;

extern const InsnCase insn_cases[];
extern const int insn_ncases;

#endif
