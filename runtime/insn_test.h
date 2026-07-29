#ifndef INSN_TEST_H
#define INSN_TEST_H

#include <stdint.h>

typedef struct {
    const char *mnemonic;
    unsigned    len;
    uint8_t     bytes[16];
    void      (*lifted)(void);
    int         uses_memory;
    int         base_reg;      /* register holding the address base, or -1 */
    int         index_reg;     /* scaled index register, or -1 */
    int         addr_reg;      /* register receiving a computed address (LEA), or -1 */
    int32_t     disp;          /* displacement baked into the encoding */
    int         scale;         /* index scale */
    int         is_x87;
} InsnCase;

extern const InsnCase insn_cases[];
extern const int insn_ncases;

#endif
