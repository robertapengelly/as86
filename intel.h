/******************************************************************************
 * @file            intel.h
 *****************************************************************************/
#ifndef     _INTEL_H
#define     _INTEL_H

#define     NO_BSUF                     0x00000040
#define     NO_WSUF                     0x00000080
#define     NO_SSUF                     0x00000100
#define     NO_LSUF                     0x00200000
#define     NO_QSUF                     0x00400000
#define     NO_INTELSUF                 0x00800000

#define     NONE                        0x0000FFFF

#define     W                           0x00000001
#define     D                           0x00000002

#define     MODRM                       0x00000004
#define     SHORT_FORM                  0x00000008

#define     JUMP                        0x00000010
#define     CALL                        0x00000020

#define     IGNORE_SIZE                 0x00000200
#define     DEFAULT_SIZE                0x01000000
#define     SEGSHORTFORM                0x00040000

#define     FLOAT_D                     0x00000400          /* Must be 0x400. */

#define     JUMPINTERSEGMENT            0x00000800
#define     JUMPBYTE                    0x00001000

#define     SIZE16                      0x00002000
#define     SIZE32                      0x00004000

#define     IS_PREFIX                   0x00008000
#define     IS_STRING                   0x00010000

#define     REG_DUPLICATION             0x00020000

#define     FLOAT_MF                    0x00080000
#define     ADD_FWAIT                   0x00100000

#define     REG8                        0x00000001
#define     REG16                       0x00000002
#define     REG32                       0x00000004

#define     REG                         (REG8 | REG16 | REG32)
#define     WORD_REG                    (REG16 | REG32)

#define     SEGMENT1                    0x00000008
#define     SEGMENT2                    0x00020000
#define     CONTROL                     0x00000010
#define     DEBUG                       0x00100000
#define     TEST                        0x00200000
#define     FLOAT_REG                   0x00400000
#define     FLOAT_ACC                   0x00800000

#define     IMM8                        0x00000020
#define     IMM8S                       0x00000040
#define     IMM16                       0x00000080
#define     IMM32                       0x00000100
    
#define     IMM                         (IMM8 | IMM8S | IMM16 | IMM32)
#define     ENCODABLEIMM                (IMM8 | IMM16 | IMM32)

#define     DISP8                       0x00000200
#define     DISP16                      0x00000400
#define     DISP32                      0x00000800
    
#define     DISP                        (DISP8 | DISP16 | DISP32)
#define     BASE_INDEX                  0x00001000

/**
 * INV_MEM is for instruction with modrm where general register
 * encoding is allowed only in modrm.regmem (control register move).
 * */
#define     INV_MEM                     0x00040000
#define     ANY_MEM                     (DISP8 | DISP16 | DISP32 | BASE_INDEX | INV_MEM)

#define     ACC                         0x00002000
#define     PORT                        0x00004000
#define     SHIFT_COUNT                 0x00008000
#define     JUMP_ABSOLUTE               0x00010000

#define     IMPLICIT_REGISTER           (SHIFT_COUNT | ACC)

#include    "stdint.h"

struct reg_entry {

    const char *name;
    uint32_t type, number;

};

#define     MAX_OPERANDS                0x03
#define     MAX_REG_NAME_SIZE           0x08

struct template {

    const char *name;
    int32_t operands;
    
    uint32_t base_opcode;
    uint32_t extension_opcode;
    uint32_t opcode_modifier;
    uint32_t operand_types[MAX_OPERANDS];
    
    int minimum_cpu;

};

#define     ABSOLUTE_PREFIX             '*'
#define     IMMEDIATE_PREFIX            '$'
#define     REGISTER_PREFIX             '%'

#define     BYTE_SUFFIX                 'b'
#define     WORD_SUFFIX                 'w'
#define     SHORT_SUFFIX                's'
#define     DWORD_SUFFIX                'l'
#define     QWORD_SUFFIX                'q'

/* Internal suffix for .intel_syntax. It cannot be directly used by the user. */
#define     INTEL_SUFFIX                '\1'

/* Prefixes are emitted in the following order. */
#define     FWAIT_PREFIX                0x00
#define     REP_PREFIX                  0x01
#define     ADDR_PREFIX                 0x02
#define     DATA_PREFIX                 0x03
#define     SEGMENT_PREFIX              0x04
#define     MAX_PREFIXES                0x05

struct modrm_byte {

    uint32_t regmem;
    uint32_t reg;
    uint32_t mode;

};

struct sib_byte {

    uint32_t base;
    uint32_t index;
    uint32_t scale;

};

#define     MODRM_REGMEM_TWO_BYTE_ADDRESSING                0x04
#define     SIB_BASE_NO_BASE_REGISTER                       0x05
#define     SIB_BASE_NO_BASE_REGISTER_16                    0x06
#define     SIB_INDEX_NO_INDEX_REGISTER                     0x04

#define     NO_SUF                      (NO_BSUF | NO_WSUF | NO_SSUF | NO_LSUF | NO_QSUF | NO_INTELSUF)
#define     B_SUF                       (NO_WSUF | NO_SSUF | NO_LSUF | NO_QSUF | NO_INTELSUF)
#define     W_SUF                       (NO_BSUF | NO_SSUF | NO_LSUF | NO_QSUF | NO_INTELSUF)
#define     L_SUF                       (NO_BSUF | NO_WSUF | NO_SSUF | NO_QSUF | NO_INTELSUF)
#define     Q_SUF                       (NO_BSUF | NO_WSUF | NO_SSUF | NO_LSUF | NO_INTELSUF)
#define     INTEL_SUF                   (NO_BSUF | NO_WSUF | NO_SSUF | NO_LSUF | NO_QSUF)

#define     BW_SUF                      (NO_SSUF | NO_LSUF | NO_QSUF | NO_INTELSUF)
#define     WL_SUF                      (NO_BSUF | NO_SSUF | NO_QSUF | NO_INTELSUF)
#define     BWL_SUF                     (NO_SSUF | NO_QSUF | NO_INTELSUF)
#define     SL_SUF                      (NO_BSUF | NO_WSUF | NO_QSUF | NO_INTELSUF)

#include    "expr.h"
#include    "types.h"

enum expr_type machine_dependent_parse_operator (char **pp, char *name, char *original_saved_c, uint32_t operands);
section_t machine_dependent_simplified_expression_read_into (char **pp, struct expr *expr);

char *machine_dependent_assemble_line (char *line);

int machine_dependent_force_relocation_local (fixup_t fixup);
int machine_dependent_get_bits (void);
int machine_dependent_get_cpu (void);
int machine_dependent_is_register (const char *p);
int machine_dependent_need_index_operator (void);
int machine_dependent_parse_name (char **pp, struct expr *expr, char *name, char *original_saved_c);

long machine_dependent_estimate_size_before_relax (struct frag *frag, section_t section);
long machine_dependent_pcrel_from (struct fixup *fixup);
long machine_dependent_relax_frag (struct frag *frag, section_t section, long change);

void machine_dependent_number_to_chars (unsigned char *p, unsigned long number, unsigned long size);
void machine_dependent_apply_fixup (fixup_t fixup, unsigned long value);
void machine_dependent_finish_frag (frag_t frag);
void machine_dependent_init (void);
void machine_dependent_parse_operand (char **pp, struct expr *expr);
void machine_dependent_set_bits (int bits);
void machine_dependent_set_cpu (int bits);

#endif      /* _INTEL_H */
