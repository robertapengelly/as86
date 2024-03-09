/******************************************************************************
 * @file            intel.c
 *****************************************************************************/
#include    <ctype.h>
#include    <stddef.h>
#include    <stdio.h>
#include    <stdlib.h>
#include    <string.h>

#include    "as.h"
#include    "expr.h"
#include    "fixup.h"
#include    "frag.h"
#include    "hashtab.h"
#include    "lex.h"
#include    "lib.h"
#include    "intel.h"
#include    "pseudo_ops.h"
#include    "report.h"
#include    "section.h"
#include    "stdint.h"
#include    "symbol.h"

static int allow_no_prefix_reg = 1;
static int intel_syntax = 1;

static int cpu_level = 0;
static int bits = 16;

#define     RELAX_SUBTYPE_SHORT_JUMP                        0x00
#define     RELAX_SUBTYPE_CODE16_JUMP                       0x01
#define     RELAX_SUBTYPE_LONG_JUMP                         0x02

#define     RELAX_SUBTYPE_SHORT16_JUMP                      (RELAX_SUBTYPE_SHORT_JUMP | RELAX_SUBTYPE_CODE16_JUMP)
#define     RELAX_SUBTYPE_LONG16_JUMP                       (RELAX_SUBTYPE_LONG_JUMP | RELAX_SUBTYPE_CODE16_JUMP)

#define     RELAX_SUBTYPE_UNCONDITIONAL_JUMP                0x00
#define     RELAX_SUBTYPE_CONDITIONAL_JUMP                  0x01
#define     RELAX_SUBTYPE_CONDITIONAL_JUMP86                0x02
#define     RELAX_SUBTYPE_FORCED_SHORT_JUMP                 0x03

#define     ENCODE_RELAX_SUBTYPE(type, size)                (((type) << 2) | (size))
#define     TYPE_FROM_RELAX_SUBTYPE(subtype)                ((subtype) >> 2)

#define     DISPLACEMENT_SIZE_FROM_RELAX_SUBSTATE(s)        \
    (((s) & 3) == RELAX_SUBTYPE_LONG_JUMP ? 4 : (((s) & 3) == RELAX_SUBTYPE_LONG16_JUMP ? 2 : 1))

struct relax_table_entry {

    long forward_reach;
    long backward_reach;
    long size_of_variable_part;
    
    relax_subtype_t next_subtype;

};

struct relax_table_entry relax_table[] = {

    /* Unconditional jumps. */
    { 127 + 1,  -128 + 1,   1,      ENCODE_RELAX_SUBTYPE (RELAX_SUBTYPE_UNCONDITIONAL_JUMP, RELAX_SUBTYPE_LONG_JUMP)    },
    { 127 + 1,  -128 + 1,   1,      ENCODE_RELAX_SUBTYPE (RELAX_SUBTYPE_UNCONDITIONAL_JUMP, RELAX_SUBTYPE_LONG16_JUMP)  },
    { 0,        0,          4,      0                                                                                   },
    { 0,        0,          2,      0                                                                                   },
    
    /* Conditional jumps. */
    { 127 + 1,  -128 + 1,   1,      ENCODE_RELAX_SUBTYPE (RELAX_SUBTYPE_CONDITIONAL_JUMP, RELAX_SUBTYPE_LONG_JUMP)      },
    { 127 + 1,  -128 + 1,   1,      ENCODE_RELAX_SUBTYPE (RELAX_SUBTYPE_CONDITIONAL_JUMP, RELAX_SUBTYPE_LONG16_JUMP)    },
    { 0,        0,          5,      0                                                                                   },
    { 0,        0,          3,      0                                                                                   },
    
    /* Conditional jumps 86. */
    { 127 + 1,  -128 + 1,   1,      ENCODE_RELAX_SUBTYPE (RELAX_SUBTYPE_CONDITIONAL_JUMP86, RELAX_SUBTYPE_LONG_JUMP)    },
    { 127 + 1,  -128 + 1,   1,      ENCODE_RELAX_SUBTYPE (RELAX_SUBTYPE_CONDITIONAL_JUMP86, RELAX_SUBTYPE_LONG16_JUMP)  },
    { 0,        0,          5,      0                                                                                   },
    { 0,        0,          4,      0                                                                                   },
    
    /* Forced short jump that cannot be relaxed. */
    { 127 + 1,  -128 + 1,   1,      0  },

};

#define     TWOBYTE_OPCODE              0x0F

/** Table for lexical analysis. */
static char register_chars_table[256] = { 0 };

#define     EXPR_TYPE_BYTE_PTR          EXPR_TYPE_MACHINE_DEPENDENT_0
#define     EXPR_TYPE_WORD_PTR          EXPR_TYPE_MACHINE_DEPENDENT_1
#define     EXPR_TYPE_DWORD_PTR         EXPR_TYPE_MACHINE_DEPENDENT_2
#define     EXPR_TYPE_FWORD_PTR         EXPR_TYPE_MACHINE_DEPENDENT_3
#define     EXPR_TYPE_QWORD_PTR         EXPR_TYPE_MACHINE_DEPENDENT_4
#define     EXPR_TYPE_SHORT             EXPR_TYPE_MACHINE_DEPENDENT_5
#define     EXPR_TYPE_OFFSET            EXPR_TYPE_MACHINE_DEPENDENT_6
#define     EXPR_TYPE_FULL_PTR          EXPR_TYPE_MACHINE_DEPENDENT_7
#define     EXPR_TYPE_NEAR_PTR          EXPR_TYPE_MACHINE_DEPENDENT_8
#define     EXPR_TYPE_FAR_PTR           EXPR_TYPE_MACHINE_DEPENDENT_9

struct intel_type {

    const char *name;
    enum expr_type expr_type;
    
    uint32_t size;

};

static const struct intel_type intel_types[] = {

#define INTEL_TYPE(name, size) { #name, EXPR_TYPE_##name##_PTR, size }
    
    INTEL_TYPE (BYTE,  1),
    
    INTEL_TYPE (NEAR,  2),
    INTEL_TYPE (WORD,  2),
    
    INTEL_TYPE (FAR,   4),
    INTEL_TYPE (DWORD, 4),
    
    INTEL_TYPE (FWORD, 6),
    INTEL_TYPE (QWORD, 8),
    
#undef INTEL_TYPE
    
    { NULL, EXPR_TYPE_INVALID, 0 }

};

struct intel_operator {

    const char *name;
    enum expr_type expr_type;
    
    uint32_t operands;

};

static const struct intel_operator intel_operators[] = {

    { "and",    EXPR_TYPE_BIT_AND,          2 },
    { "eq",     EXPR_TYPE_EQUAL,            2 },
    { "ge",     EXPR_TYPE_GREATER_EQUAL,    2 },
    { "gt",     EXPR_TYPE_GREATER,          2 },
    { "le",     EXPR_TYPE_LESSER_EQUAL,     2 },
    { "lt",     EXPR_TYPE_LESSER,           2 },
    { "mod",    EXPR_TYPE_MODULUS,          2 },
    { "ne",     EXPR_TYPE_NOT_EQUAL,        2 },
    { "not",    EXPR_TYPE_BIT_NOT,          1 },
    { "offset", EXPR_TYPE_OFFSET,           1 },
    { "or",     EXPR_TYPE_BIT_INCLUSIVE_OR, 2 },
    { "shl",    EXPR_TYPE_LEFT_SHIFT,       2 },
    { "shr",    EXPR_TYPE_RIGHT_SHIFT,      2 },
    { "short",  EXPR_TYPE_SHORT,            1 },
    { "xor",    EXPR_TYPE_BIT_EXCLUSIVE_OR, 2 },
    
    { NULL,     EXPR_TYPE_INVALID,          0 }

};

static struct {

    enum expr_type operand_modifier;

    int is_mem;
    int has_offset;

    int in_offset;
    int in_bracket;
    int in_scale;

    const struct reg_entry *base_reg;
    const struct reg_entry *index_reg;
    
    offset_t scale_factor;
    struct symbol *segment;

} intel_state;

extern char get_symbol_name_end (char **pp);

/*
 * All instructions with the exact same name must be together without any other instructions mixed in
 * for the code searching the template table to work properly.
 * */
static struct template template_table[] = {

    /* Move instructions. */
    { "mov", 2, 0xA0, NONE, BWL_SUF | D | W, { DISP16 | DISP32, ACC, 0 }, 0 },
    { "mov", 2, 0x88, NONE, BWL_SUF | D | W | MODRM, { REG, ANY_MEM, 0 }, 0 },
    { "mov", 2, 0x8A, NONE, BWL_SUF | D | W | MODRM, { REG, REG, 0 }, 0 },
    { "mov", 2, 0xB0, NONE, BWL_SUF | W | SHORT_FORM, { ENCODABLEIMM, REG8 | REG16 | REG32, 0 }, 0 },
    { "mov", 2, 0xC6, NONE, BWL_SUF | D | W | MODRM, { ENCODABLEIMM, REG | ANY_MEM, 0 }, 0 },
    
    /* Move instructions for segment registers. */
    { "mov", 2, 0x8C, NONE, WL_SUF | MODRM, { SEGMENT1, WORD_REG | INV_MEM, 0 }, 0 },
    { "mov", 2, 0x8C, NONE, W_SUF | MODRM | IGNORE_SIZE, { SEGMENT1, ANY_MEM, 0 }, 0 },
    { "mov", 2, 0x8C, NONE, WL_SUF | MODRM, { SEGMENT2, WORD_REG | INV_MEM, 0 }, 3 },
    { "mov", 2, 0x8C, NONE, W_SUF | MODRM | IGNORE_SIZE, { SEGMENT2, ANY_MEM, 0 }, 3 },
    { "mov", 2, 0x8E, NONE, WL_SUF | MODRM | IGNORE_SIZE, { WORD_REG | INV_MEM, SEGMENT1, 0 }, 0 },
    { "mov", 2, 0x8E, NONE, W_SUF | MODRM | IGNORE_SIZE, { ANY_MEM, SEGMENT1, 0 }, 0 },
    { "mov", 2, 0x8E, NONE, WL_SUF | MODRM | IGNORE_SIZE, { WORD_REG | INV_MEM, SEGMENT2, 0 }, 3 },
    { "mov", 2, 0x8E, NONE, W_SUF | MODRM | IGNORE_SIZE, { ANY_MEM, SEGMENT2, 0 }, 3 },
    
    /* Move instructions for control, debug and test registers. */
    { "mov", 2, 0x0F20, NONE, L_SUF | D | MODRM | IGNORE_SIZE, { CONTROL, REG32 | INV_MEM, 0 }, 3 },
    { "mov", 2, 0x0F21, NONE, L_SUF | D | MODRM | IGNORE_SIZE, { DEBUG, REG32 | INV_MEM, 0 }, 3 },
    { "mov", 2, 0x0F24, NONE, L_SUF | D | MODRM | IGNORE_SIZE, { TEST, REG32 | INV_MEM, 0 }, 3 },
    
    /* Move with sign extend. */
    /* "movsbl" and "movsbw" are not unified into "movsb" to prevent conflict with "movs". */
    { "movsbl", 2, 0x0FBE, NONE, NO_SUF | MODRM, { REG8 | ANY_MEM, REG32, 0 }, 3 },
    { "movsbw", 2, 0x0FBE, NONE, NO_SUF | MODRM, { REG8 | ANY_MEM, REG16, 0 }, 3 },
    { "movswl", 2, 0x0FBF, NONE, NO_SUF | MODRM, { REG16 | ANY_MEM, REG32, 0 }, 3 },
    
    /* Alternative syntax. */
    { "movsx", 2, 0x0FBE, NONE, BW_SUF | W | MODRM, { REG8 | REG16 | ANY_MEM, WORD_REG, 0 }, 3 },
    
    /* Move with zero extend. */
    { "movzb", 2, 0x0FB6, NONE, WL_SUF | MODRM, { REG8 | ANY_MEM, WORD_REG, 0 }, 3 },
    { "movzwl", 2, 0x0FB7, NONE, NO_SUF | MODRM, { REG16 | ANY_MEM, REG32, 0 }, 3 },
    
    /* Alternative syntax. */
    { "movzx", 2, 0x0FB6, NONE, BW_SUF | W | MODRM, { REG8 | REG16 | ANY_MEM, WORD_REG, 0 }, 3 },
    
    /* Push instructions. */
    { "push", 1, 0x50, NONE, WL_SUF | SHORT_FORM, { WORD_REG, 0, 0 }, 0 },
    { "push", 1, 0xFF, 6, WL_SUF | DEFAULT_SIZE | MODRM, { WORD_REG | ANY_MEM, 0, 0 }, 0 },
    { "push", 1, 0x6A, NONE, WL_SUF | DEFAULT_SIZE, { IMM8S, 0, 0 }, 1 },
    { "push", 1, 0x68, NONE, WL_SUF | DEFAULT_SIZE, { IMM16 | IMM32, 0, 0 }, 1 },
    
    { "push", 1, 0x06, NONE, WL_SUF | DEFAULT_SIZE | SEGSHORTFORM, { SEGMENT1, 0, 0 }, 0},
    { "push", 1, 0x0FA0, NONE, WL_SUF | DEFAULT_SIZE | SEGSHORTFORM, { SEGMENT2, 0, 0 }, 3},
    
    { "pusha", 0, 0x60, NONE, WL_SUF | DEFAULT_SIZE, { 0, 0, 0 }, 1 },
    
    /* Pop instructions. */
    { "pop", 1, 0x58, NONE, WL_SUF | SHORT_FORM, { WORD_REG, 0, 0 }, 0 },
    { "pop", 1, 0x8F, NONE, WL_SUF | DEFAULT_SIZE | MODRM, { WORD_REG | ANY_MEM, 0, 0 }, 0 },
    
#define     POP_SEGMENT_SHORT           0x07
    
    { "pop", 1, 0x07, NONE, WL_SUF | DEFAULT_SIZE | SEGSHORTFORM, { SEGMENT1, 0, 0 }, 0 },
    { "pop", 1, 0x0FA1, NONE, WL_SUF | DEFAULT_SIZE | SEGSHORTFORM, { SEGMENT2, 0, 0 }, 3 },
    
    { "popa", 0, 0x61, NONE, WL_SUF | DEFAULT_SIZE, { 0, 0, 0 }, 1 },
    
    /* Exchange instructions. */
    { "xchg", 2, 0x90, NONE, WL_SUF | SHORT_FORM, { WORD_REG, ACC, 0 }, 0 },
    { "xchg", 2, 0x90, NONE, WL_SUF | SHORT_FORM, { ACC, WORD_REG, 0 }, 0 },
    { "xchg", 2, 0x86, NONE, BWL_SUF | W | MODRM, { REG, REG | ANY_MEM, 0 }, 0 },
    { "xchg", 2, 0x86, NONE, BWL_SUF | W | MODRM, { REG | ANY_MEM, REG, 0 }, 0 },
    
    /* In/out for ports. */
    { "in", 2, 0xE4, NONE, BWL_SUF | W, { IMM8, ACC, 0 }, 0 },
    { "in", 2, 0xEC, NONE, BWL_SUF | W, { PORT, ACC, 0 }, 0 },
    { "in", 1, 0xE4, NONE, BWL_SUF | W, { IMM8, 0, 0 }, 0 },
    { "in", 1, 0xEC, NONE, BWL_SUF | W, { PORT, 0, 0 }, 0 },
    
    { "out", 2, 0xE6, NONE, BWL_SUF | W, { ACC, IMM8, 0 }, 0 },
    { "out", 2, 0xEE, NONE, BWL_SUF | W, { ACC, PORT, 0 }, 0 },
    { "out", 2, 0xE6, NONE, BWL_SUF | W, { IMM8, 0, 0 }, 0 },
    { "out", 2, 0xEE, NONE, BWL_SUF | W, { PORT, 0, 0 }, 0 },
    
    /* Load effective address. */
    { "lea", 2, 0x8D, NONE, WL_SUF | MODRM, { ANY_MEM, WORD_REG, 0 }, 0 },
    
    /* Load far pointer from memory. */
    { "lds", 2, 0xC5, NONE, WL_SUF | MODRM, { ANY_MEM, WORD_REG, 0 }, 0 },
    { "les", 2, 0xC4, NONE, WL_SUF | MODRM, { ANY_MEM, WORD_REG, 0 }, 0 },
    { "lfs", 2, 0x0FB4, NONE, WL_SUF | MODRM, { ANY_MEM, WORD_REG, 0 }, 3 },
    { "lgs", 2, 0x0FB5, NONE, WL_SUF | MODRM, { ANY_MEM, WORD_REG, 0 }, 3 },
    { "lss", 2, 0x0FB2, NONE, WL_SUF | MODRM, { ANY_MEM, WORD_REG, 0 }, 3 },
    
    /* Flags register instructions. */
    { "cmc", 0, 0xF5, NONE, NO_SUF, { 0, 0, 0 }, 0 },
    { "clc", 0, 0xF8, NONE, NO_SUF, { 0, 0, 0 }, 0 },
    { "stc", 0, 0xF9, NONE, NO_SUF, { 0, 0, 0 }, 0 },
    { "cli", 0, 0xFA, NONE, NO_SUF, { 0, 0, 0 }, 0 },
    { "sti", 0, 0xFB, NONE, NO_SUF, { 0, 0, 0 }, 0 },
    { "cld", 0, 0xFC, NONE, NO_SUF, { 0, 0, 0 }, 0 },
    { "std", 0, 0xFD, NONE, NO_SUF, { 0, 0, 0 }, 0 },
    { "clts", 0, 0x0F06, NONE, NO_SUF, { 0, 0, 0 }, 2 },
    { "lahf", 0, 0x9F, NONE, NO_SUF, { 0, 0, 0 }, 0 },
    { "sahf", 0, 0x9E, NONE, NO_SUF, { 0, 0, 0 }, 0 },
    { "pushf", 0, 0x9C, NONE, WL_SUF | DEFAULT_SIZE, { WORD_REG, 0, 0 }, 0 },
    { "popf", 0, 0x9D, NONE, WL_SUF | DEFAULT_SIZE, { WORD_REG, 0, 0 }, 0 },
    
    /* Arithmetic instructions. */
    { "add", 2, 0x00, NONE, BWL_SUF | D | W | MODRM, { REG, ANY_MEM, 0 }, 0 },
    { "add", 2, 0x02, NONE, BWL_SUF | D | W | MODRM, { REG, REG, 0 }, 0 },
    { "add", 2, 0x83, 0, WL_SUF | MODRM, { IMM8S, WORD_REG | ANY_MEM, 0 }, 0 },
    { "add", 2, 0x04, NONE, BWL_SUF | W, { ENCODABLEIMM, ACC, 0 }, 0 },
    { "add", 2, 0x80, 0, BWL_SUF | W | MODRM, { ENCODABLEIMM, REG | ANY_MEM, 0 }, 0 },
    
    { "inc", 1, 0x40, NONE, WL_SUF | SHORT_FORM, { WORD_REG, 0, 0 }, 0 },
    { "inc", 1, 0xFE, 0, BWL_SUF | W | MODRM, { REG | ANY_MEM, 0, 0 }, 0 },
    
    { "sub", 2, 0x28, NONE, BWL_SUF | D | W | MODRM, { REG, REG | ANY_MEM, 0 }, 0 },
    { "sub", 2, 0x83, 5, WL_SUF | MODRM, { IMM8S, WORD_REG | ANY_MEM, 0 }, 0 },
    { "sub", 2, 0x2C, NONE, BWL_SUF | W, { ENCODABLEIMM, ACC, 0 }, 0 },
    { "sub", 2, 0x80, 5, BWL_SUF | W | MODRM, { ENCODABLEIMM, REG | ANY_MEM, 0 }, 0 },
    
    { "dec", 1, 0x48, NONE, WL_SUF | SHORT_FORM, { WORD_REG, 0, 0 }, 0 },
    { "dec", 1, 0xFE, 1, BWL_SUF | W | MODRM, { REG | ANY_MEM, 0, 0 }, 0 },
    
    { "sbb", 2, 0x18, NONE, BWL_SUF | D | W | MODRM, { REG, REG | ANY_MEM, 0 }, 0 },
    { "sbb", 2, 0x83, 3, WL_SUF | MODRM, { IMM8S, WORD_REG | ANY_MEM, 0 }, 0 },
    { "sbb", 2, 0x1C, NONE, BWL_SUF | W, { ENCODABLEIMM, ACC, 0 }, 0 },
    { "sbb", 2, 0x80, 3, BWL_SUF | W | MODRM, { ENCODABLEIMM, REG | ANY_MEM, 0 }, 0 },
    
    { "cmp", 2, 0x38, NONE, BWL_SUF | D | W | MODRM, { REG, REG | ANY_MEM, 0 }, 0 },
    { "cmp", 2, 0x83, 7, WL_SUF | MODRM, { IMM8S, WORD_REG | ANY_MEM, 0 }, 0 },
    { "cmp", 2, 0x3C, NONE, BWL_SUF | W, { ENCODABLEIMM, ACC, 0 }, 0 },
    { "cmp", 2, 0x80, 7, BWL_SUF | W | MODRM, { ENCODABLEIMM, REG | ANY_MEM, 0 }, 0 },
    
    { "test", 2, 0x84, NONE, BWL_SUF | W | MODRM, { REG | ANY_MEM, REG, 0 }, 0 },
    { "test", 2, 0x84, NONE, BWL_SUF | W | MODRM, { REG, REG | ANY_MEM, 0 }, 0 },
    { "test", 2, 0xA8, NONE, BWL_SUF | W, { ENCODABLEIMM, ACC, 0 }, 0 },
    { "test", 2, 0xF6, 0, BWL_SUF | W | MODRM, { ENCODABLEIMM, REG | ANY_MEM, 0 }, 0 },
    
    { "and", 2, 0x20, NONE, BWL_SUF | D | W | MODRM, { REG, REG | ANY_MEM, 0 }, 0 },
    { "and", 2, 0x83, 4, WL_SUF | MODRM, { IMM8S, WORD_REG | ANY_MEM, 0 }, 0 },
    { "and", 2, 0x24, NONE, BWL_SUF | W, { ENCODABLEIMM, ACC, 0 }, 0 },
    { "and", 2, 0x80, 4, BWL_SUF | W | MODRM, { ENCODABLEIMM, REG | ANY_MEM, 0 }, 0 },
    
    { "or", 2, 0x08, NONE, BWL_SUF | D | W | MODRM, { REG, ANY_MEM, 0 }, 0 },
    { "or", 2, 0x0A, NONE, BWL_SUF | D | W | MODRM, { REG, REG, 0 }, 0 },
    { "or", 2, 0x83, 1, WL_SUF | MODRM, { IMM8S, WORD_REG | ANY_MEM, 0 }, 0 },
    { "or", 2, 0x0C, NONE, BWL_SUF | W, { ENCODABLEIMM, ACC, 0 }, 0 },
    { "or", 2, 0x80, 1, BWL_SUF | W | MODRM, { ENCODABLEIMM, REG | ANY_MEM, 0 }, 0 },
    
    { "xor", 2, 0x30, NONE, BWL_SUF | D | W | MODRM, { REG, ANY_MEM, 0 }, 0 },
    { "xor", 2, 0x32, NONE, BWL_SUF | D | W | MODRM, { REG, REG, 0 }, 0 },
    { "xor", 2, 0x83, 6, WL_SUF | MODRM, { IMM8S, WORD_REG | ANY_MEM, 0 }, 0 },
    { "xor", 2, 0x34, NONE, BWL_SUF | W, { ENCODABLEIMM, ACC, 0 }, 0 },
    { "xor", 2, 0x80, 6, BWL_SUF | W | MODRM, { ENCODABLEIMM, REG | ANY_MEM, 0 }, 0 },
    
    { "clr", 1, 0x30, NONE, BWL_SUF | W | MODRM | REG_DUPLICATION, { REG, 0, 0 }, 0 },
    
    { "adc", 2, 0x10, NONE, BWL_SUF | D | W | MODRM, { REG, ANY_MEM, 0 }, 0 },
    { "adc", 2, 0x12, NONE, BWL_SUF | D | W | MODRM, { REG, REG, 0 }, 0 },
    { "adc", 2, 0x83, 2, WL_SUF | MODRM, { IMM8S, WORD_REG | ANY_MEM, 0 }, 0 },
    { "adc", 2, 0x14, NONE, BWL_SUF | W, { ENCODABLEIMM, ACC, 0 }, 0 },
    { "adc", 2, 0x80, 2, BWL_SUF | W | MODRM, { ENCODABLEIMM, REG | ANY_MEM, 0 }, 0 },
    
    { "neg", 1, 0xF6, 3, BWL_SUF | W | MODRM, { REG | ANY_MEM, 0, 0 }, 0 },
    { "not", 1, 0xF6, 2, BWL_SUF | W | MODRM, { REG | ANY_MEM, 0, 0 }, 0 },
    
    { "aaa", 0, 0x37, NONE, NO_SUF, { 0, 0, 0 }, 0 },
    { "aas", 0, 0x3F, NONE, NO_SUF, { 0, 0, 0 }, 0 },
    
    { "daa", 0, 0x27, NONE, NO_SUF, { 0, 0, 0 }, 0 },
    { "das", 0, 0x2F, NONE, NO_SUF, { 0, 0, 0 }, 0 },
    
    { "aad", 0, 0xD50A, NONE, NO_SUF, { 0, 0, 0 }, 0 },
    { "aad", 1, 0xD5, NONE, NO_SUF, { IMM8, 0, 0 }, 0 },
    
    { "aam", 0, 0xD40A, NONE, NO_SUF, { 0, 0, 0 }, 0 },
    { "aam", 1, 0xD4, NONE, NO_SUF, { IMM8, 0, 0 }, 0 },
    
    /* Conversion instructions. */
    { "cbw", 0, 0x98, NONE, NO_SUF | SIZE16, { 0, 0, 0 }, 0 },
    { "cwde", 0, 0x98, NONE, NO_SUF | SIZE32, { 0, 0, 0 }, 0 },
    { "cwd", 0, 0x99, NONE, NO_SUF | SIZE16, { 0, 0, 0 }, 0 },
    { "cdq", 0, 0x99, NONE, NO_SUF | SIZE32, { 0, 0, 0 }, 3 },
    
    /* Other naming. */
    { "cbtw", 0, 0x98, NONE, NO_SUF | SIZE16, { 0, 0, 0 }, 0 },
    { "cwtl", 0, 0x98, NONE, NO_SUF | SIZE32, { 0, 0, 0 }, 0 },
    { "cwtd", 0, 0x99, NONE, NO_SUF | SIZE16, { 0, 0, 0 }, 0 },
    { "cltd", 0, 0x99, NONE, NO_SUF | SIZE32, { 0, 0, 0 }, 3 },
    
    { "mul", 1, 0xF6, 4, BWL_SUF | W | MODRM, { REG | ANY_MEM, 0, 0 }, 0 },
    
    { "imul", 1, 0xF6, 5, BWL_SUF | W | MODRM, { REG | ANY_MEM, 0, 0 }, 0 },
    { "imul", 2, 0x0FAF, NONE, WL_SUF | MODRM, { WORD_REG | ANY_MEM, WORD_REG, 0 }, 3 },
    { "imul", 3, 0x6B, NONE, WL_SUF | MODRM, { IMM8S, WORD_REG | ANY_MEM, WORD_REG }, 1 },
    { "imul", 3, 0x69, NONE, WL_SUF | MODRM, { IMM16 | IMM32, WORD_REG | ANY_MEM, WORD_REG }, 1 },
    { "imul", 2, 0x6B, NONE, WL_SUF | MODRM | REG_DUPLICATION, { IMM8S, WORD_REG, 0 }, 1 },
    { "imul", 2, 0x69, NONE, WL_SUF | MODRM | REG_DUPLICATION, { IMM16 | IMM32, WORD_REG, 0 }, 1 },
    
    { "div", 1, 0xF6, 6, BWL_SUF | W | MODRM, { REG | ANY_MEM, 0, 0 }, 0 },
    { "div", 2, 0xF6, 6, BWL_SUF | W | MODRM, { REG | ANY_MEM, ACC, 0 }, 0 },
    
    { "idiv", 1, 0xF6, 7, BWL_SUF | W | MODRM, { REG | ANY_MEM, 0, 0 }, 0 },
    { "idiv", 2, 0xF6, 7, BWL_SUF | W | MODRM, { REG | ANY_MEM, ACC, 0 }, 0 },
    
    { "rol", 2, 0xC0, 0, BWL_SUF | W | MODRM, { IMM8, REG | ANY_MEM, 0 }, 1 },
    { "rol", 2, 0xD2, 0, BWL_SUF | W | MODRM, { SHIFT_COUNT, REG | ANY_MEM, 0 }, 0 },
    { "rol", 1, 0xD0, 0, BWL_SUF | W | MODRM, { REG | ANY_MEM, 0, 0 }, 0 },
    
    { "ror", 2, 0xC0, 1, BWL_SUF | W | MODRM, { IMM8, REG | ANY_MEM, 0 }, 1 },
    { "ror", 2, 0xD2, 1, BWL_SUF | W | MODRM, { SHIFT_COUNT, REG | ANY_MEM, 0 }, 0 },
    { "ror", 1, 0xD0, 1, BWL_SUF | W | MODRM, { REG | ANY_MEM, 0, 0 }, 0 },
    
    { "rcl", 2, 0xC0, 2, BWL_SUF | W | MODRM, { IMM8, REG | ANY_MEM, 0 }, 1 },
    { "rcl", 2, 0xD2, 2, BWL_SUF | W | MODRM, { SHIFT_COUNT, REG | ANY_MEM, 0 }, 0 },
    { "rcl", 1, 0xD0, 2, BWL_SUF | W | MODRM, { REG | ANY_MEM, 0, 0 }, 0 },
    
    { "rcr", 2, 0xC0, 3, BWL_SUF | W | MODRM, { IMM8, REG | ANY_MEM, 0 }, 1 },
    { "rcr", 2, 0xD2, 3, BWL_SUF | W | MODRM, { SHIFT_COUNT, REG | ANY_MEM, 0 }, 0 },
    { "rcr", 1, 0xD0, 3, BWL_SUF | W | MODRM, { REG | ANY_MEM, 0, 0 }, 0 },
    
    { "sal", 2, 0xC0, 4, BWL_SUF | W | MODRM, { IMM8, REG | ANY_MEM, 0 }, 1 },
    { "sal", 2, 0xD2, 4, BWL_SUF | W | MODRM, { SHIFT_COUNT, REG | ANY_MEM, 0 }, 0 },
    { "sal", 1, 0xD0, 4, BWL_SUF | W | MODRM, { REG | ANY_MEM, 0, 0 }, 0 },
    
    { "shl", 2, 0xC0, 4, BWL_SUF | W | MODRM, { IMM8, REG | ANY_MEM, 0 }, 1 },
    { "shl", 2, 0xD2, 4, BWL_SUF | W | MODRM, { SHIFT_COUNT, REG | ANY_MEM, 0 }, 0 },
    { "shl", 1, 0xD0, 4, BWL_SUF | W | MODRM, { REG | ANY_MEM, 0, 0 }, 0 },
    
    { "shr", 2, 0xC0, 5, BWL_SUF | W | MODRM, { IMM8, REG | ANY_MEM, 0 }, 1 },
    { "shr", 2, 0xD2, 5, BWL_SUF | W | MODRM, { SHIFT_COUNT, REG | ANY_MEM, 0 }, 0 },
    { "shr", 1, 0xD0, 5, BWL_SUF | W | MODRM, { REG | ANY_MEM, 0, 0 }, 0 },
    
    { "sar", 2, 0xC0, 7, BWL_SUF | W | MODRM, { IMM8, REG | ANY_MEM, 0 }, 1 },
    { "sar", 2, 0xD2, 7, BWL_SUF | W | MODRM, { SHIFT_COUNT, REG | ANY_MEM, 0 }, 0 },
    { "sar", 1, 0xD0, 7, BWL_SUF | W | MODRM, { REG | ANY_MEM, 0, 0 }, 0 },
    
    { "shld", 3, 0x0FA4, NONE, WL_SUF | MODRM, { IMM8, WORD_REG, WORD_REG | ANY_MEM }, 3 },
    { "shld", 3, 0x0FA5, NONE, WL_SUF | MODRM, { SHIFT_COUNT, WORD_REG, WORD_REG | ANY_MEM }, 3 },
    { "shld", 2, 0x0FA5, NONE, WL_SUF | MODRM, { WORD_REG, WORD_REG | ANY_MEM, 0 }, 3 },
    
    { "shrd", 3, 0x0FAC, NONE, WL_SUF | MODRM, { IMM8, WORD_REG, WORD_REG | ANY_MEM }, 3 },
    { "shrd", 3, 0x0FAD, NONE, WL_SUF | MODRM, { SHIFT_COUNT, WORD_REG, WORD_REG | ANY_MEM }, 3 },
    { "shrd", 2, 0x0FAD, NONE, WL_SUF | MODRM, { WORD_REG, WORD_REG | ANY_MEM, 0 }, 3 },
    
    /* Program control transfer instructions. */
    { "call", 1, 0xE8, NONE, WL_SUF | DEFAULT_SIZE | CALL, { DISP16 | DISP32, 0, 0 }, 0 },
    { "call", 1, 0xFF, 2, WL_SUF | DEFAULT_SIZE | MODRM, { WORD_REG | ANY_MEM | JUMP_ABSOLUTE, 0, 0 }, 0 },
    { "call", 2, 0x9A, NONE, WL_SUF | DEFAULT_SIZE | JUMPINTERSEGMENT, { IMM16, IMM16 | IMM32, 0 }, 0 },
    { "call", 1, 0xFF, 3, INTEL_SUF | DEFAULT_SIZE | MODRM, { ANY_MEM | JUMP_ABSOLUTE, 0, 0 }, 0 },
    
    /* Alternative syntax. */
    { "lcall", 2, 0x9A, NONE, WL_SUF | DEFAULT_SIZE | JUMPINTERSEGMENT, { IMM16, IMM16 | IMM32, 0 }, 0 },
    { "lcall", 1, 0xFF, 3, WL_SUF | DEFAULT_SIZE | MODRM, { ANY_MEM | JUMP_ABSOLUTE, 0, 0 }, 0 },
    
#define     PC_RELATIVE_JUMP            0xEB
    
    { "jmp", 1, 0xEB, NONE, NO_SUF | JUMP, { DISP, 0, 0 }, 0 },
    { "jmp", 1, 0xFF, 4, WL_SUF | MODRM, { WORD_REG | ANY_MEM | JUMP_ABSOLUTE, 0, 0 }, 0 },
    { "jmp", 2, 0xEA, NONE, WL_SUF | JUMPINTERSEGMENT, { IMM16, IMM16 | IMM32, 0 }, 0 },
    { "jmp", 1, 0xFF, 5, INTEL_SUF | MODRM, { ANY_MEM | JUMP_ABSOLUTE, 0, 0 }, 0 },
    
    /* Alternative syntax. */
    { "ljmp", 2, 0xEA, NONE, WL_SUF | JUMPINTERSEGMENT, { IMM16, IMM16 | IMM32, 0 }, 0 },
    { "ljmp", 1, 0xFF, 5, WL_SUF | MODRM, { ANY_MEM | JUMP_ABSOLUTE, 0, 0 }, 0 },
    
    { "ret", 0, 0xC3, NONE, WL_SUF | DEFAULT_SIZE, { 0, 0, 0 }, 0 },
    { "ret", 1, 0xC2, NONE, WL_SUF | DEFAULT_SIZE, { IMM16, 0, 0 }, 0 },
    { "retn", 0, 0xC3, NONE, WL_SUF | DEFAULT_SIZE, { 0, 0, 0 }, 0 },
    { "retf", 0, 0xCB, NONE, WL_SUF | DEFAULT_SIZE, { 0, 0, 0 }, 0 },
    { "lret", 0, 0xCB, NONE, WL_SUF | DEFAULT_SIZE, { 0, 0, 0 }, 0 },
    { "lret", 1, 0xCA, NONE, WL_SUF | DEFAULT_SIZE, { IMM16, 0, 0 }, 0 },
    { "enter", 2, 0xC8, NONE, WL_SUF | DEFAULT_SIZE, { IMM16, IMM8, 0 }, 1 },
    { "leave", 0, 0xC9, NONE, WL_SUF | DEFAULT_SIZE, { 0, 0, 0 }, 1 },
    
    /* Conditional jumps. */
    { "jo", 1, 0x70, NONE, NO_SUF | JUMP, { DISP, 0, 0 }, 0 },
    { "jno", 1, 0x71, NONE, NO_SUF | JUMP, { DISP, 0, 0 }, 0 },
    { "jb", 1, 0x72, NONE, NO_SUF | JUMP, { DISP, 0, 0 }, 0 },
    { "jc", 1, 0x72, NONE, NO_SUF | JUMP, { DISP, 0, 0 }, 0 },
    { "jnb", 1, 0x73, NONE, NO_SUF | JUMP, { DISP, 0, 0 }, 0 },
    { "jnc", 1, 0x73, NONE, NO_SUF | JUMP, { DISP, 0, 0 }, 0 },
    { "jae", 1, 0x73, NONE, NO_SUF | JUMP, { DISP, 0, 0 }, 0 },
    { "je", 1, 0x74, NONE, NO_SUF | JUMP, { DISP, 0, 0 }, 0 },
    { "jz", 1, 0x74, NONE, NO_SUF | JUMP, { DISP, 0, 0 }, 0 },
    { "jne", 1, 0x75, NONE, NO_SUF | JUMP, { DISP, 0, 0 }, 0 },
    { "jnz", 1, 0x75, NONE, NO_SUF | JUMP, { DISP, 0, 0 }, 0 },
    { "jbe", 1, 0x76, NONE, NO_SUF | JUMP, { DISP, 0, 0 }, 0 },
    { "jna", 1, 0x76, NONE, NO_SUF | JUMP, { DISP, 0, 0 }, 0 },
    { "ja", 1, 0x77, NONE, NO_SUF | JUMP, { DISP, 0, 0 }, 0 },
    { "js", 1, 0x78, NONE, NO_SUF | JUMP, { DISP, 0, 0 }, 0 },
    { "jns", 1, 0x79, NONE, NO_SUF | JUMP, { DISP, 0, 0 }, 0 },
    { "jpe", 1, 0x7A, NONE, NO_SUF | JUMP, { DISP, 0, 0 }, 0 },
    { "jpo", 1, 0x7B, NONE, NO_SUF | JUMP, { DISP, 0, 0 }, 0 },
    { "jl", 1, 0x7C, NONE, NO_SUF | JUMP, { DISP, 0, 0 }, 0 },
    { "jge", 1, 0x7D, NONE, NO_SUF | JUMP, { DISP, 0, 0 }, 0 },
    { "jle", 1, 0x7E, NONE, NO_SUF | JUMP, { DISP, 0, 0 }, 0 },
    { "jg", 1, 0x7F, NONE, NO_SUF | JUMP, { DISP, 0, 0 }, 0 },
    
    { "jcxz", 1, 0xE3, NONE, NO_SUF | JUMPBYTE | SIZE16, { DISP, 0, 0 }, 0 },
    { "jecxz", 1, 0xE3, NONE, NO_SUF | JUMPBYTE | SIZE32, { DISP, 0, 0 }, 0 },
    
    /* Loop instructions. */
    { "loop", 1, 0xE2, NONE, WL_SUF | JUMPBYTE, { DISP, 0, 0 }, 0 },
    { "loopz", 1, 0xE1, NONE, WL_SUF | JUMPBYTE, { DISP, 0, 0 }, 0 },
    { "loope", 1, 0xE1, NONE, WL_SUF | JUMPBYTE, { DISP, 0, 0 }, 0 },
    { "loopnz", 1, 0xE0, NONE, WL_SUF | JUMPBYTE, { DISP, 0, 0 }, 0 },
    { "loopne", 1, 0xE0, NONE, WL_SUF | JUMPBYTE, { DISP, 0, 0 }, 0 },
    
    /* Set byte on flag instructions. */
    { "seto", 1, 0x0F90, 0, B_SUF | MODRM, { REG8 | ANY_MEM, 0, 0 }, 3 },
    { "setno", 1, 0x0F91, 0, B_SUF | MODRM, { REG8 | ANY_MEM, 0, 0 }, 3 },
    { "setb", 1, 0x0F92, 0, B_SUF | MODRM, { REG8 | ANY_MEM, 0, 0 }, 3 },
    { "setc", 1, 0x0F92, 0, B_SUF | MODRM, { REG8 | ANY_MEM, 0, 0 }, 3 },
    { "setnae", 1, 0x0F92, 0, B_SUF | MODRM, { REG8 | ANY_MEM, 0, 0 }, 3 },
    { "setnb", 1, 0x0F93, 0, B_SUF | MODRM, { REG8 | ANY_MEM, 0, 0 }, 3 },
    { "setnc", 1, 0x0F93, 0, B_SUF | MODRM, { REG8 | ANY_MEM, 0, 0 }, 3 },
    { "setae", 1, 0x0F93, 0, B_SUF | MODRM, { REG8 | ANY_MEM, 0, 0 }, 3 },
    { "sete", 1, 0x0F94, 0, B_SUF | MODRM, { REG8 | ANY_MEM, 0, 0 }, 3 },
    { "setz", 1, 0x0F94, 0, B_SUF | MODRM, { REG8 | ANY_MEM, 0, 0 }, 3 },
    { "setne", 1, 0x0F95, 0, B_SUF | MODRM, { REG8 | ANY_MEM, 0, 0 }, 3 },
    { "setnz", 1, 0x0F95, 0, B_SUF | MODRM, { REG8 | ANY_MEM, 0, 0 }, 3 },
    { "setbe", 1, 0x0F96, 0, B_SUF | MODRM, { REG8 | ANY_MEM, 0, 0 }, 3 },
    { "setna", 1, 0x0F96, 0, B_SUF | MODRM, { REG8 | ANY_MEM, 0, 0 }, 3 },
    { "setnbe", 1, 0x0F97, 0, B_SUF | MODRM, { REG8 | ANY_MEM, 0, 0 }, 3 },
    { "seta", 1, 0x0F97, 0, B_SUF | MODRM, { REG8 | ANY_MEM, 0, 0 }, 3 },
    { "sets", 1, 0x0F98, 0, B_SUF | MODRM, { REG8 | ANY_MEM, 0, 0 }, 3 },
    { "setns", 1, 0x0F99, 0, B_SUF | MODRM, { REG8 | ANY_MEM, 0, 0 }, 3 },
    { "setp", 1, 0x0F9A, 0, B_SUF | MODRM, { REG8 | ANY_MEM, 0, 0 }, 3 },
    { "setpe", 1, 0x0F9A, 0, B_SUF | MODRM, { REG8 | ANY_MEM, 0, 0 }, 3 },
    { "setnp", 1, 0x0F9B, 0, B_SUF | MODRM, { REG8 | ANY_MEM, 0, 0 }, 3 },
    { "setpo", 1, 0x0F9B, 0, B_SUF | MODRM, { REG8 | ANY_MEM, 0, 0 }, 3 },
    { "setl", 1, 0x0F9C, 0, B_SUF | MODRM, { REG8 | ANY_MEM, 0, 0 }, 3 },
    { "setnge", 1, 0x0F9C, 0, B_SUF | MODRM, { REG8 | ANY_MEM, 0, 0 }, 3 },
    { "setnl", 1, 0x0F9D, 0, B_SUF | MODRM, { REG8 | ANY_MEM, 0, 0 }, 3 },
    { "setge", 1, 0x0F9D, 0, B_SUF | MODRM, { REG8 | ANY_MEM, 0, 0 }, 3 },
    { "setle", 1, 0x0F9E, 0, B_SUF | MODRM, { REG8 | ANY_MEM, 0, 0 }, 3 },
    { "setng", 1, 0x0F9E, 0, B_SUF | MODRM, { REG8 | ANY_MEM, 0, 0 }, 3 },
    { "setnle", 1, 0x0F9F, 0, B_SUF | MODRM, { REG8 | ANY_MEM, 0, 0 }, 3 },
    { "setg", 1, 0x0F9F, 0, B_SUF | MODRM, { REG8 | ANY_MEM, 0, 0 }, 3 },
    
    /* String manipulation instructions. */
    { "cmps", 0, 0xA6, NONE, BWL_SUF | W | IS_STRING, { 0, 0, 0 }, 0 },
    { "scmp", 0, 0xA6, NONE, BWL_SUF | W | IS_STRING, { 0, 0, 0 }, 0 },
    { "ins", 0, 0x6C, NONE, BWL_SUF | W | IS_STRING, { 0, 0, 0 }, 1 },
    { "outs", 0, 0x6E, NONE, BWL_SUF | W | IS_STRING, { 0, 0, 0 }, 1 },
    { "lods", 0, 0xAC, NONE, BWL_SUF | W | IS_STRING, { 0, 0, 0 }, 0 },
    { "slod", 0, 0xAC, NONE, BWL_SUF | W | IS_STRING, { 0, 0, 0 }, 0 },
    { "movs", 0, 0xA4, NONE, BWL_SUF | W | IS_STRING, { 0, 0, 0 }, 0 },
    { "smov", 0, 0xA4, NONE, BWL_SUF | W | IS_STRING, { 0, 0, 0 }, 0 },
    { "scas", 0, 0xAE, NONE, BWL_SUF | W | IS_STRING, { 0, 0, 0 }, 0 },
    { "ssca", 0, 0xAE, NONE, BWL_SUF | W | IS_STRING, { 0, 0, 0 }, 0 },
    { "stos", 0, 0xAA, NONE, BWL_SUF | W | IS_STRING, { 0, 0, 0 }, 0 },
    { "ssto", 0, 0xAA, NONE, BWL_SUF | W | IS_STRING, { 0, 0, 0 }, 0 },
    { "xlat", 0, 0xD7, NONE, B_SUF | IS_STRING, { 0, 0, 0 }, 0 },
    
    /* Bit manipulation instructions. */
    { "bsf", 2, 0x0FBC, NONE, WL_SUF | MODRM, { WORD_REG | ANY_MEM, WORD_REG, 0 }, 3 },
    { "bsr", 2, 0x0FBD, NONE, WL_SUF | MODRM, { WORD_REG | ANY_MEM, WORD_REG, 0 }, 3 },
    { "bt", 2, 0x0FA3, NONE, WL_SUF | MODRM, { WORD_REG, WORD_REG | ANY_MEM, 0 }, 3 },
    { "bt", 2, 0x0FBA, 4, WL_SUF | MODRM, { IMM8, WORD_REG | ANY_MEM, 0 }, 3 },
    { "btc", 2, 0x0FBB, NONE, WL_SUF | MODRM, { WORD_REG, WORD_REG | ANY_MEM, 0 }, 3 },
    { "btc", 2, 0x0FBA, 7, WL_SUF | MODRM, { IMM8, WORD_REG | ANY_MEM, 0 }, 3 },
    { "btr", 2, 0x0FB3, NONE, WL_SUF | MODRM, { WORD_REG, WORD_REG | ANY_MEM, 0 }, 3 },
    { "btr", 2, 0x0FBA, 6, WL_SUF | MODRM, { IMM8, WORD_REG | ANY_MEM, 0 }, 3 },
    { "bts", 2, 0x0FAB, NONE, WL_SUF | MODRM, { WORD_REG, WORD_REG | ANY_MEM, 0 }, 3 },
    { "bts", 2, 0x0FBA, 5, WL_SUF | MODRM, { IMM8, WORD_REG | ANY_MEM, 0 }, 3 },
    
    /* Interrupts. */
    { "int", 1, 0xCD, NONE, 0, { IMM8, 0, 0 }, 0 },
    { "int3", 0, 0xCC, NONE, 0, { 0, 0, 0 }, 0 },
    { "into", 0, 0xCE, NONE, 0, { 0, 0, 0 }, 0 },
    { "iret", 0, 0xCF, NONE, WL_SUF | DEFAULT_SIZE, { 0, 0, 0 }, 0 },
    
    { "rsm", 0, 0x0FAA, NONE, 0, { 0, 0, 0 }, 3 },
    { "bound", 2, 0x62, NONE, 0, { WORD_REG, ANY_MEM, 0 }, 1 },
    
    { "hlt", 0, 0xF4, NONE, NO_SUF, { 0, 0, 0 }, 0 },
    { "nop", 0, 0x90, NONE, NO_SUF, { 0, 0, 0 }, 0 },
    
    /* Protection control. */
    { "arpl", 2, 0x63, NONE, W_SUF | MODRM | IGNORE_SIZE, { REG16, REG16 | ANY_MEM, 0 }, 2 },
    { "lar", 2, 0x0F02, NONE, WL_SUF | MODRM, { WORD_REG | ANY_MEM, WORD_REG, 0 }, 2 },
    { "lgdt", 1, 0x0F01, 2, WL_SUF | MODRM, { ANY_MEM, 0, 0 }, 2 },
    { "lidt", 1, 0x0F01, 3, WL_SUF | MODRM, { ANY_MEM, 0, 0 }, 2 },
    { "lldt", 1, 0x0F00, 2, W_SUF | MODRM | IGNORE_SIZE, { REG16 | ANY_MEM, 0, 0 }, 2 },
    { "lmsw", 1, 0x0F01, 6, W_SUF | MODRM | IGNORE_SIZE, { REG16 | ANY_MEM, 0, 0 }, 2 },
    { "lsl", 2, 0x0F03, NONE, WL_SUF | MODRM, { WORD_REG | ANY_MEM, WORD_REG, 0 }, 2 },
    { "ltr", 1, 0x0F00, 3, W_SUF | MODRM | IGNORE_SIZE, { REG16 | ANY_MEM, 0, 0 }, 2 },
    
    { "sgdt", 1, 0x0F01, 0, WL_SUF | MODRM, { ANY_MEM, 0, 0 }, 2 },
    { "sidt", 1, 0x0F01, 1, WL_SUF | MODRM, { ANY_MEM, 0, 0 }, 2 },
    { "sldt", 1, 0x0F00, 0, WL_SUF | MODRM, { WORD_REG | INV_MEM, 0, 0 }, 2 },
    { "sldt", 1, 0x0F00, 0, W_SUF | MODRM | IGNORE_SIZE, { ANY_MEM, 0, 0 }, 2 },
    { "smsw", 1, 0x0F01, 4, WL_SUF | MODRM, { WORD_REG | INV_MEM, 0, 0 }, 2 },
    { "smsw", 1, 0x0F01, 4, W_SUF | MODRM | IGNORE_SIZE, { ANY_MEM, 0, 0 }, 2 },
    { "str", 1, 0x0F00, 1, WL_SUF | MODRM, { WORD_REG | INV_MEM, 0, 0 }, 2 },
    { "str", 1, 0x0F00, 1, W_SUF | MODRM | IGNORE_SIZE, { ANY_MEM, 0, 0 }, 2 },
    
    { "verr", 1, 0x0F00, 4, W_SUF | MODRM | IGNORE_SIZE, { REG16 | ANY_MEM, 0, 0 }, 2 },
    { "verw", 1, 0x0F00, 5, W_SUF | MODRM | IGNORE_SIZE, { REG16 | ANY_MEM, 0, 0 }, 2 },
    
    /* Floating point instructions. */
    /* Load. */
    { "fld", 1, 0xD9C0, NONE, NO_SUF | SHORT_FORM, { FLOAT_REG, 0, 0 }, 0 },
    { "fld", 1, 0xD9, 0, SL_SUF | FLOAT_MF | MODRM, { ANY_MEM, 0, 0 }, 0 },
    { "fld", 1, 0xDB, 5, NO_SUF | MODRM, { ANY_MEM, 0, 0 }, 0 },
    { "fild", 1, 0xDF, 0, SL_SUF | FLOAT_MF | MODRM, { ANY_MEM, 0, 0 }, 0 },
    { "fild", 1, 0xDF, 5, Q_SUF | MODRM, { ANY_MEM, 0, 0 }, 0 },
    { "fildll", 1, 0xDF, 5, NO_SUF | MODRM, { ANY_MEM, 0, 0 }, 0 },
    { "fldt", 1, 0xDB, 5, NO_SUF | MODRM, { ANY_MEM, 0, 0 }, 0 },
    { "fbld", 1, 0xDF, 4, NO_SUF | MODRM, { ANY_MEM, 0, 0 }, 0 },
    
    /* Store without pop. */
    { "fst", 1, 0xDDD0, NONE, NO_SUF | SHORT_FORM, { FLOAT_REG, 0, 0 }, 0 },
    { "fst", 1, 0xD9, 2, SL_SUF | FLOAT_MF | MODRM, { ANY_MEM, 0, 0 }, 0 },
    { "fist", 1, 0xDF, 2, SL_SUF | FLOAT_MF | MODRM, { ANY_MEM, 0, 0 }, 0 },
    
    /* Store with pop. */
    { "fstp", 1, 0xDDD8, NONE, NO_SUF | SHORT_FORM, { FLOAT_REG, 0, 0 }, 0 },
    { "fstp", 1, 0xD9, 3, SL_SUF | FLOAT_MF | MODRM, { ANY_MEM, 0, 0 }, 0 },
    { "fstp", 1, 0xDB, 7, NO_SUF | MODRM, { ANY_MEM, 0, 0 }, 0 },
    { "fistp", 1, 0xDF, 3, SL_SUF | FLOAT_MF | MODRM, { ANY_MEM, 0, 0 }, 0 },
    { "fistp", 1, 0xDF, 7, Q_SUF | MODRM, { ANY_MEM, 0, 0 }, 0 },
    { "fistpll", 1, 0xDF, 7, NO_SUF | MODRM, { ANY_MEM, 0, 0 }, 0 },
    { "fstpt", 1, 0xDB, 7, NO_SUF | MODRM, { ANY_MEM, 0, 0 }, 0 },
    { "fbstp", 1, 0xDF, 6, NO_SUF | MODRM, { ANY_MEM, 0, 0 }, 0 },
    
    /* Exchange of %st(0) with %st(x). */
    { "fxch", 1, 0xD9C8, NONE, NO_SUF | SHORT_FORM, { FLOAT_REG, 0, 0 }, 0 },
    /* Exchange of %st(0) with %st(1). */
    { "fxch", 0, 0xD9C9, NONE, NO_SUF, { 0, 0, 0 }, 0 },
    
    /* Comparison without pop. */
    { "fcom", 1, 0xD8D0, NONE, NO_SUF | SHORT_FORM, { FLOAT_REG, 0, 0 }, 0 },
    { "fcom", 0, 0xD8D1, NONE, NO_SUF, { 0, 0, 0 }, 0 },
    { "fcom", 1, 0xD8, 2, SL_SUF | FLOAT_MF | MODRM, { ANY_MEM, 0, 0 }, 0 },
    { "ficom", 1, 0xDE, 2, SL_SUF | FLOAT_MF | MODRM, { ANY_MEM, 0, 0 }, 0 },
    
    /* Comparison with pop. */
    { "fcomp", 1, 0xD8D8, NONE, NO_SUF | SHORT_FORM, { FLOAT_REG, 0, 0 }, 0 },
    { "fcomp", 0, 0xD8D9, NONE, NO_SUF, { 0, 0, 0 }, 0 },
    { "fcomp", 1, 0xD8, 3, SL_SUF | FLOAT_MF | MODRM, { ANY_MEM, 0, 0 }, 0 },
    { "ficomp", 1, 0xDE, 3, SL_SUF | FLOAT_MF | MODRM, { ANY_MEM, 0, 0 }, 0 },
    { "fcompp", 0, 0xDED9, NONE, NO_SUF, { 0, 0, 0 }, 0 },
    
    /* Unordered comparison. */
    { "fucom", 1, 0xDDE0, NONE, NO_SUF | SHORT_FORM, { FLOAT_REG, 0, 0 }, 3 },
    { "fucom", 0, 0xDDE1, NONE, NO_SUF, { 0, 0, 0 }, 3 },
    { "fucomp", 1, 0xDDE8, NONE, NO_SUF | SHORT_FORM, { FLOAT_REG, 0, 0 }, 3 },
    { "fucomp", 0, 0xDDE9, NONE, NO_SUF, { 0, 0, 0 }, 3 },
    { "fucompp", 0, 0xDAE9, NONE, NO_SUF, { 0, 0, 0 }, 3 },
    
    { "ftst", 0, 0xD9E4, NONE, NO_SUF, { 0, 0, 0 }, 0 },
    { "fxam", 0, 0xD9E5, NONE, NO_SUF, { 0, 0, 0 }, 0 },
    
    /* Load constant. */
    { "fld1", 0, 0xD9E8, NONE, NO_SUF, { 0, 0, 0 }, 0 },
    { "fldl2t", 0, 0xD9E9, NONE, NO_SUF, { 0, 0, 0 }, 0 },
    { "fldl2e", 0, 0xD9EA, NONE, NO_SUF, { 0, 0, 0 }, 0 },
    { "fldpi", 0, 0xD9EB, NONE, NO_SUF, { 0, 0, 0 }, 0 },
    { "fldlg2", 0, 0xD9EC, NONE, NO_SUF, { 0, 0, 0 }, 0 },
    { "fldln2", 0, 0xD9ED, NONE, NO_SUF, { 0, 0, 0 }, 0 },
    { "fldz", 0, 0xD9EE, NONE, NO_SUF, { 0, 0, 0 }, 0 },
    
    /* Arithmetic floating point instructions. */
    /* Add. */
    { "fadd", 2, 0xD8C0, NONE, NO_SUF | SHORT_FORM | FLOAT_D, { FLOAT_REG, FLOAT_ACC, 0 }, 0 },
    { "fadd", 1, 0xD8C0, NONE, NO_SUF | SHORT_FORM, { FLOAT_REG, 0, 0 }, 0 },
    { "fadd", 1, 0xD8, 0, SL_SUF | FLOAT_MF | MODRM, { ANY_MEM, 0, 0 }, 0 },
    { "fiadd", 1, 0xDE, 0, SL_SUF | FLOAT_MF | MODRM, { ANY_MEM, 0, 0 }, 0 },
    
    { "faddp", 2, 0xDEC0, NONE, NO_SUF | SHORT_FORM | FLOAT_D, { FLOAT_ACC, FLOAT_REG, 0 }, 0 },
    { "faddp", 1, 0xDEC0, NONE, NO_SUF | SHORT_FORM, { FLOAT_REG, 0, 0 }, 0 },
    { "faddp", 0, 0xDEC1, NONE, NO_SUF, { 0, 0, 0 }, 0 },
    
    /* Subtract. */
    { "fsub", 2, 0xD8E0, NONE, NO_SUF | SHORT_FORM | FLOAT_D, { FLOAT_REG, FLOAT_ACC, 0 }, 0 },
    { "fsub", 1, 0xD8E0, NONE, NO_SUF | SHORT_FORM, { FLOAT_REG, 0, 0 }, 0 },
    { "fsub", 1, 0xD8, 4, SL_SUF | FLOAT_MF | MODRM, { ANY_MEM, 0, 0 }, 0 },
    { "fisub", 1, 0xDE, 4, SL_SUF | FLOAT_MF | MODRM, { ANY_MEM, 0, 0 }, 0 },
    
    { "fsubp", 2, 0xDEE0, NONE, NO_SUF | SHORT_FORM | FLOAT_D, { FLOAT_ACC, FLOAT_REG, 0 }, 0 },
    { "fsubp", 1, 0xDEE0, NONE, NO_SUF | SHORT_FORM, { FLOAT_REG, 0, 0 }, 0 },
    { "fsubp", 0, 0xDEE1, NONE, NO_SUF, { 0, 0, 0 }, 0 },
    
    /* Subtract reverse. */
    { "fsubr", 2, 0xD8E8, NONE, NO_SUF | SHORT_FORM | FLOAT_D, { FLOAT_REG, FLOAT_ACC, 0 }, 0 },
    { "fsubr", 1, 0xD8E8, NONE, NO_SUF | SHORT_FORM, { FLOAT_REG, 0, 0 }, 0 },
    { "fsubr", 1, 0xD8, 5, SL_SUF | FLOAT_MF | MODRM, { ANY_MEM, 0, 0 }, 0 },
    { "fisubr", 1, 0xDE, 5, SL_SUF | FLOAT_MF | MODRM, { ANY_MEM, 0, 0 }, 0 },
    
    { "fsubrp", 2, 0xDEE8, NONE, NO_SUF | SHORT_FORM | FLOAT_D, { FLOAT_ACC, FLOAT_REG, 0 }, 0 },
    { "fsubrp", 1, 0xDEE8, NONE, NO_SUF | SHORT_FORM, { FLOAT_REG, 0, 0 }, 0 },
    { "fsubrp", 0, 0xDEE9, NONE, NO_SUF, { 0, 0, 0 }, 0 },
    
    /* Multiply. */
    { "fmul", 2, 0xD8C8, NONE, NO_SUF | SHORT_FORM | FLOAT_D, { FLOAT_REG, FLOAT_ACC, 0 }, 0 },
    { "fmul", 1, 0xD8C8, NONE, NO_SUF | SHORT_FORM, { FLOAT_REG, 0, 0 }, 0 },
    { "fmul", 1, 0xD8, 1, SL_SUF | FLOAT_MF | MODRM, { ANY_MEM, 0, 0 }, 0 },
    { "fimul", 1, 0xDE, 1, SL_SUF | FLOAT_MF | MODRM, { ANY_MEM, 0, 0 }, 0 },
    
    { "fmulp", 2, 0xDEC8, NONE, NO_SUF | SHORT_FORM | FLOAT_D, { FLOAT_ACC, FLOAT_REG, 0 }, 0 },
    { "fmulp", 1, 0xDEC8, NONE, NO_SUF | SHORT_FORM, { FLOAT_REG, 0, 0 }, 0 },
    { "fmulp", 0, 0xDEC9, NONE, NO_SUF, { 0, 0, 0 }, 0 },
    
    /* Divide. */
    { "fdiv", 2, 0xD8F0, NONE, NO_SUF | SHORT_FORM | FLOAT_D, { FLOAT_REG, FLOAT_ACC, 0 }, 0 },
    { "fdiv", 1, 0xD8F0, NONE, NO_SUF | SHORT_FORM, { FLOAT_REG, 0, 0 }, 0 },
    { "fdiv", 1, 0xD8, 6, SL_SUF | FLOAT_MF | MODRM, { ANY_MEM, 0, 0 }, 0 },
    { "fidiv", 1, 0xDE, 6, SL_SUF | FLOAT_MF | MODRM, { ANY_MEM, 0, 0 }, 0 },
    
    { "fdivp", 2, 0xDEF0, NONE, NO_SUF | SHORT_FORM | FLOAT_D, { FLOAT_ACC, FLOAT_REG, 0 }, 0 },
    { "fdivp", 1, 0xDEF0, NONE, NO_SUF | SHORT_FORM, { FLOAT_REG, 0, 0 }, 0 },
    { "fdivp", 0, 0xDEF1, NONE, NO_SUF, { 0, 0, 0 }, 0 },
    
    /* Divide reverse. */
    { "fdivr", 2, 0xD8F8, NONE, NO_SUF | SHORT_FORM | FLOAT_D, { FLOAT_REG, FLOAT_ACC, 0 }, 0 },
    { "fdivr", 1, 0xD8F8, NONE, NO_SUF | SHORT_FORM, { FLOAT_REG, 0, 0 }, 0 },
    { "fdivr", 1, 0xD8, 7, SL_SUF | FLOAT_MF | MODRM, { ANY_MEM, 0, 0 }, 0 },
    { "fidivr", 1, 0xDE, 7, SL_SUF | FLOAT_MF | MODRM, { ANY_MEM, 0, 0 }, 0 },
    
    { "fdivrp", 2, 0xDEF8, NONE, NO_SUF | SHORT_FORM | FLOAT_D, { FLOAT_ACC, FLOAT_REG, 0 }, 0 },
    { "fdivrp", 1, 0xDEF8, NONE, NO_SUF | SHORT_FORM, { FLOAT_REG, 0, 0 }, 0 },
    { "fdivrp", 0, 0xDEF9, NONE, NO_SUF, { 0, 0, 0 }, 0 },
    
    /* Other operations. */
    { "f2xm1", 0, 0xD9F0, NONE, NO_SUF, { 0, 0, 0 }, 0 },
    { "fyl2x", 0, 0xD9F1, NONE, NO_SUF, { 0, 0, 0 }, 0 },
    { "fptan", 0, 0xD9F2, NONE, NO_SUF, { 0, 0, 0 }, 0 },
    { "fpatan", 0, 0xD9F3, NONE, NO_SUF, { 0, 0, 0 }, 0 },
    { "fxtract", 0, 0xD9F4, NONE, NO_SUF, { 0, 0, 0 }, 0 },
    { "fprem1", 0, 0xD9F5, NONE, NO_SUF, { 0, 0, 0 }, 3 },
    { "fdecstp", 0, 0xD9F6, NONE, NO_SUF, { 0, 0, 0 }, 0 },
    { "fincstp", 0, 0xD9F7, NONE, NO_SUF, { 0, 0, 0 }, 0 },
    { "fprem", 0, 0xD9F8, NONE, NO_SUF, { 0, 0, 0 }, 0 },
    { "fyl2xp1", 0, 0xD9F9, NONE, NO_SUF, { 0, 0, 0 }, 0 },
    { "fsqrt", 0, 0xD9FA, NONE, NO_SUF, { 0, 0, 0 }, 0 },
    { "fsincos", 0, 0xD9FB, NONE, NO_SUF, { 0, 0, 0 }, 3 },
    { "frndint", 0, 0xD9FC, NONE, NO_SUF, { 0, 0, 0 }, 0 },
    { "fscale", 0, 0xD9FD, NONE, NO_SUF, { 0, 0, 0 }, 0 },
    { "fsin", 0, 0xD9FE, NONE, NO_SUF, { 0, 0, 0 }, 3 },
    { "fcos", 0, 0xD9FF, NONE, NO_SUF, { 0, 0, 0 }, 3 },
    { "fchs", 0, 0xD9E0, NONE, NO_SUF, { 0, 0, 0 }, 0 },
    { "fabs", 0, 0xD9E1, NONE, NO_SUF, { 0, 0, 0 }, 0 },
    
    /* Processor control instructions. */
    { "fninit", 0, 0xDBE3, NONE, NO_SUF, { 0, 0, 0 }, 0 },
    { "finit", 0, 0xDBE3, NONE, NO_SUF | ADD_FWAIT, { 0, 0, 0 }, 0 },
    { "fldcw", 1, 0xD9, 5, W_SUF | IGNORE_SIZE | MODRM, { ANY_MEM, 0, 0 }, 0 },
    { "fnstcw", 1, 0xD9, 7, W_SUF | IGNORE_SIZE | MODRM, { ANY_MEM, 0, 0 }, 0 },
    { "fstcw", 1, 0xD9, 7, W_SUF | IGNORE_SIZE | ADD_FWAIT | MODRM, { ANY_MEM, 0, 0 }, 0 },
    
    { "fnstsw", 1, 0xDFE0, NONE, NO_SUF | IGNORE_SIZE, { ACC, 0, 0 }, 0 },
    { "fnstsw", 1, 0xDD, 7, W_SUF | IGNORE_SIZE | MODRM, { ANY_MEM, 0, 0 }, 0 },
    { "fnstsw", 0, 0xDFE0, NONE, NO_SUF, { 0, 0, 0 }, 0 },
    
    { "fstsw", 1, 0xDFE0, NONE, NO_SUF | ADD_FWAIT | IGNORE_SIZE, { ACC, 0, 0 }, 0 },
    { "fstsw", 1, 0xDD, 7, W_SUF | IGNORE_SIZE | ADD_FWAIT | MODRM, { ANY_MEM, 0, 0 }, 0 },
    { "fstsw", 0, 0xDFE0, NONE, NO_SUF | ADD_FWAIT, { 0, 0, 0 }, 0 },
    
    { "fnclex", 0, 0xDBE2, NONE, NO_SUF, { 0, 0, 0 }, 0 },
    { "fclex", 0, 0xDBE2, NONE, NO_SUF | ADD_FWAIT, { 0, 0, 0 }, 0 },
    
    { "fnstenv", 1, 0xD9, 6, SL_SUF | MODRM, { ANY_MEM, 0, 0 }, 0 },
    { "fstenv", 1, 0xD9, 6, SL_SUF | ADD_FWAIT | MODRM, { ANY_MEM, 0, 0 }, 0 },
    { "fldenv", 1, 0xD9, 4, SL_SUF | MODRM, { ANY_MEM, 0, 0 }, 0 },
    { "fnsave", 1, 0xDD, 6, SL_SUF | MODRM, { ANY_MEM, 0, 0 }, 0 },
    { "fsave", 1, 0xDD, 6, SL_SUF | ADD_FWAIT | MODRM, { ANY_MEM, 0, 0 }, 0 },
    { "frstor", 1, 0xDD, 4, SL_SUF | MODRM, { ANY_MEM, 0, 0 }, 0 },
    
    { "ffree", 1, 0xDDC0, NONE, NO_SUF | SHORT_FORM, { FLOAT_REG, 0, 0 }, 0 },
    { "ffreep", 1, 0xDFC0, NONE, NO_SUF | SHORT_FORM, { FLOAT_REG, 0, 0 }, 6 },
    { "fnop", 0, 0xD9D0, NONE, NO_SUF, { 0, 0, 0 }, 0 },
    
#define     FWAIT_OPCODE                0x9B
    
    { "fwait", 0, 0x9B, NONE, NO_SUF, { 0, 0, 0 }, 0 },
    
    /* Opcode prefixes. They are allowed as separate instructions too. */
#define     ADDR_PREFIX_OPCODE          0x67
    
    { "addr16", 0, 0x67, NONE, NO_SUF | IS_PREFIX | SIZE16 | IGNORE_SIZE, { 0, 0, 0 }, 3 },
    { "addr32", 0, 0x67, NONE, NO_SUF | IS_PREFIX | SIZE32 | IGNORE_SIZE, { 0, 0, 0 }, 3 },
    { "aword", 0, 0x67, NONE, NO_SUF | IS_PREFIX | SIZE16 | IGNORE_SIZE, { 0, 0, 0 }, 3 },
    { "adword", 0, 0x67, NONE, NO_SUF | IS_PREFIX | SIZE32 | IGNORE_SIZE, { 0, 0, 0 }, 3 },
    
#define     DATA_PREFIX_OPCODE          0x66
    
    { "data16", 0, 0x66, NONE, NO_SUF | IS_PREFIX | SIZE16 | IGNORE_SIZE, { 0, 0, 0 }, 3 },
    { "data32", 0, 0x66, NONE, NO_SUF | IS_PREFIX | SIZE32 | IGNORE_SIZE, { 0, 0, 0 }, 3 },
    { "word", 0, 0x66, NONE, NO_SUF | IS_PREFIX | SIZE16 | IGNORE_SIZE, { 0, 0, 0 }, 3 },
    { "dword", 0, 0x66, NONE, NO_SUF | IS_PREFIX | SIZE32 | IGNORE_SIZE, { 0, 0, 0 }, 3 },
    
#define     CS_PREFIX_OPCODE            0x2E
    { "cs", 0, 0x2E, NONE, NO_SUF | IS_PREFIX, { 0, 0, 0 }, 0 },
#define     DS_PREFIX_OPCODE            0x3E
    { "ds", 0, 0x3E, NONE, NO_SUF | IS_PREFIX, { 0, 0, 0 }, 0 },
#define     ES_PREFIX_OPCODE            0x26
    { "es", 0, 0x26, NONE, NO_SUF | IS_PREFIX, { 0, 0, 0 }, 0 },
#define     FS_PREFIX_OPCODE            0x64
    { "fs", 0, 0x64, NONE, NO_SUF | IS_PREFIX, { 0, 0, 0 }, 0 },
#define     GS_PREFIX_OPCODE            0x65
    { "gs", 0, 0x65, NONE, NO_SUF | IS_PREFIX, { 0, 0, 0 }, 0 },
#define     SS_PREFIX_OPCODE            0x36
    { "ss", 0, 0x36, NONE, NO_SUF | IS_PREFIX, { 0, 0, 0 }, 0 },
    
#define     REPNE_PREFIX_OPCODE         0xF2
#define     REPE_PREFIX_OPCODE          0xF3
    
    { "repne", 0, 0xF2, NONE, NO_SUF | IS_PREFIX, { 0, 0, 0 }, 0 },
    { "repnz", 0, 0xF2, NONE, NO_SUF | IS_PREFIX, { 0, 0, 0 }, 0 },
    { "rep", 0, 0xF3, NONE, NO_SUF | IS_PREFIX, { 0, 0, 0 }, 0 },
    { "repe", 0, 0xF3, NONE, NO_SUF | IS_PREFIX, { 0, 0, 0 }, 0 },
    { "repz", 0, 0xF3, NONE, NO_SUF | IS_PREFIX, { 0, 0, 0 }, 0 },
    
    /* End of instructions. */
    { 0, 0, 0, 0, 0, { 0, 0, 0 }, 0 }

};

#define     REG_FLAT_NUMBER             ((uint32_t) ~0)

static struct reg_entry reg_table[] = {

    /* 8 bit registers. */
    { "al",     REG8 | ACC,                 0 },
    { "cl",     REG8 | SHIFT_COUNT,         1 },
    { "dl",     REG8,                       2 },
    { "bl",     REG8,                       3 },
    { "ah",     REG8,                       4 },
    { "ch",     REG8,                       5 },
    { "dh",     REG8,                       6 },
    { "bh",     REG8,                       7 },

    /* 16 bit registers. */
    { "ax",     REG16 | ACC,                0 },
    { "cx",     REG16,                      1 },
    { "dx",     REG16 | PORT,               2 },
    { "bx",     REG16 | BASE_INDEX,         3 },
    { "sp",     REG16,                      4 },
    { "bp",     REG16 | BASE_INDEX,         5 },
    { "si",     REG16 | BASE_INDEX,         6 },
    { "di",     REG16 | BASE_INDEX,         7 },
    
    /* 32 bit registers. */
    { "eax",    REG32 | BASE_INDEX | ACC,   0 },
    { "ecx",    REG32 | BASE_INDEX,         1 },
    { "edx",    REG32 | BASE_INDEX,         2 },
    { "ebx",    REG32 | BASE_INDEX,         3 },
    { "esp",    REG32,                      4 },
    { "ebp",    REG32 | BASE_INDEX,         5 },
    { "esi",    REG32 | BASE_INDEX,         6 },
    { "edi",    REG32 | BASE_INDEX,         7 },
    
    /* Segment registers. */
    { "es",     SEGMENT1,               0 },
    { "cs",     SEGMENT1,               1 },
    { "ss",     SEGMENT1,               2 },
    { "ds",     SEGMENT1,               3 },
    { "fs",     SEGMENT2,               4 },
    { "gs",     SEGMENT2,               5 },
    
    /* Segment pseudo-register. */
    { "flat",   SEGMENT1,               REG_FLAT_NUMBER },
    
    /* Control registers. */
    { "cr0",    CONTROL,                0 },
    { "cr1",    CONTROL,                1 },
    { "cr2",    CONTROL,                2 },
    { "cr3",    CONTROL,                3 },
    { "cr4",    CONTROL,                4 },
    { "cr5",    CONTROL,                5 },
    { "cr6",    CONTROL,                6 },
    { "cr7",    CONTROL,                7 },
    
    /* Debug registers. */
    { "db0",    DEBUG,                  0 },
    { "db1",    DEBUG,                  1 },
    { "db2",    DEBUG,                  2 },
    { "db3",    DEBUG,                  3 },
    { "db4",    DEBUG,                  4 },
    { "db5",    DEBUG,                  5 },
    { "db6",    DEBUG,                  6 },
    { "db7",    DEBUG,                  7 },
    
    /* Other naming. */
    { "dr0",    DEBUG,                  0 },
    { "dr1",    DEBUG,                  1 },
    { "dr2",    DEBUG,                  2 },
    { "dr3",    DEBUG,                  3 },
    { "dr4",    DEBUG,                  4 },
    { "dr5",    DEBUG,                  5 },
    { "dr6",    DEBUG,                  6 },
    { "dr7",    DEBUG,                  7 },
    
    /* Test registers. */
    { "tr0",    TEST,                   0 },
    { "tr1",    TEST,                   1 },
    { "tr2",    TEST,                   2 },
    { "tr3",    TEST,                   3 },
    { "tr4",    TEST,                   4 },
    { "tr5",    TEST,                   5 },
    { "tr6",    TEST,                   6 },
    { "tr7",    TEST,                   7 },
    
    /* Floating point registers. Explicit "st(0)" is not needed. */
    { "st",     FLOAT_REG | FLOAT_ACC,  0 },
    { "st(1)",  FLOAT_REG,              1 },
    { "st(2)",  FLOAT_REG,              2 },
    { "st(3)",  FLOAT_REG,              3 },
    { "st(4)",  FLOAT_REG,              4 },
    { "st(5)",  FLOAT_REG,              5 },
    { "st(6)",  FLOAT_REG,              6 },
    { "st(7)",  FLOAT_REG,              7 },
    
    /* End of registers. */
    { 0,        0,                      0 }

};

static unsigned char segment_prefixes[] = {

    ES_PREFIX_OPCODE,
    CS_PREFIX_OPCODE,
    SS_PREFIX_OPCODE,
    DS_PREFIX_OPCODE,
    FS_PREFIX_OPCODE,
    GS_PREFIX_OPCODE

};

static const struct reg_entry *reg_esp;
static const struct reg_entry *reg_st0;

static const struct reg_entry bad_register = { "<bad>", 0, 0 };
static struct hashtab reg_entry_hashtab = { 0 };

struct templates {

    const char *name;
    struct template *start, *end;

};

static struct hashtab templates_hashtab = { 0 };

static int check_reg (const struct reg_entry *reg) {

    if (cpu_level < 3 && (reg->type & (REG32 | SEGMENT2 | CONTROL | DEBUG))) {
        return 0;
    }
    
    if ((reg->type & SEGMENT1) && reg->number == REG_FLAT_NUMBER && !intel_syntax) {
        return 0;
    }
    
    return 1;

}

static const struct reg_entry *find_reg_entry (const char *name) {

    char *p = to_lower (name);
    
    struct reg_entry *entry;
    struct hashtab_name *key;
    
    if ((key = hashtab_alloc_name (p)) == NULL) {
        return NULL;
    }
    
    entry = hashtab_get (&reg_entry_hashtab, key);
    
    free (key);
    free (p);
    
    return entry;

}

static int intel_parse_name (struct expr *expr, char *name) {

    int32_t i;

    if (strcmp (name, "$") == 0) {

        current_location (expr);
        return 1;

    }

    for (i = 0; intel_types[i].name; i++) {

        if (xstrcasecmp (name, intel_types[i].name) == 0) {

            expr->type = EXPR_TYPE_CONSTANT;
            expr->add_symbol = NULL;
            expr->op_symbol = NULL;
            expr->add_number = intel_types[i].size;

            return 1;

        }

    }

    return 0;

}

static const struct reg_entry *parse_register (const char *reg_string, char **end_pp) {

    const struct reg_entry *reg;
    char *p, *p_into_reg_name_cleaned;
    
    char reg_name_cleaned[MAX_REG_NAME_SIZE + 1];
    
    if (!(*reg_string == REGISTER_PREFIX || allow_no_prefix_reg)) {
        return NULL;
    }

    if (*reg_string == REGISTER_PREFIX) {
        reg_string++;
    }

    p = skip_whitespace ((char *) reg_string);

    for (p_into_reg_name_cleaned = reg_name_cleaned; (*(p_into_reg_name_cleaned++) = register_chars_table[(unsigned char) *p]) != '\0'; p++) {
    
        if (p_into_reg_name_cleaned >= reg_name_cleaned + MAX_REG_NAME_SIZE) {
            return NULL;
        }
    
    }
    
    /* If registers without prefix are allowed, it is necessary to check if the string
     * is really a register and not an identifier here
     * ("ax_var" must not be interpreted as register "ax" in this case).
     * This does not apply if prefixes are mandatory.*/
    if (allow_no_prefix_reg && is_name_part ((int) *p)) {
        return NULL;
    }
    
    reg = find_reg_entry (reg_name_cleaned);
    
    *end_pp = p;
    
    if (reg == NULL) {
        return NULL;
    }
    
    if (reg == reg_st0) {
    
        if (cpu_level < 2) {
            return NULL;
        }
        
        p = skip_whitespace (p);
        
        if (*p == '(') {
        
            p = skip_whitespace (++p);
            
            if  (*p >= '0' && *p <= '7') {
            
                reg = &(reg_st0[*p - '0']);
                
                p = skip_whitespace (++p);
                
                if (*p == ')') {
                
                    *end_pp = p + 1;
                    return reg;
                
                }
                
            }
            
            return NULL;
        
        }
    
    }       

    if (reg == NULL) {
        return NULL;
    } else if (check_reg (reg)) {
        return reg;
    }

    report (REPORT_ERROR, "register %%%s cannot be used here", reg->name);
    return &bad_register;

}

void machine_dependent_number_to_chars (unsigned char *p, unsigned long number, unsigned long size) {
    
    unsigned long i;
    
    for (i = 0; i < size; i++) {
        p[i] = (number >> (8 * i)) & 0xff;
    }

}


static void handler_8086 (char **pp) {

    (void) pp;
    
    cpu_level = 0;
    /*bits = 16;*/

}

static void handler_186 (char **pp) {

    (void) pp;
    
    cpu_level = 1;
    /*bits = 16;*/

}

static void handler_286 (char **pp) {

    (void) pp;
    
    cpu_level = 2;
    /*bits = 16;*/

}

static void handler_386 (char **pp) {

    (void) pp;
    
    cpu_level = 3;
    /*if (state->model > 2) { bits = 32; }*/

}

static void handler_486 (char **pp) {

    (void) pp;
    
    cpu_level = 4;
    /*if (state->model > 2) { bits = 32; }*/

}

static void handler_586 (char **pp) {

    (void) pp;
    
    cpu_level = 5;
    /*if (state->model > 2) { bits = 32; }*/

}

static void handler_686 (char **pp) {

    (void) pp;
    
    cpu_level = 6;
    /*if (state->model > 2) { bits = 32; }*/

}

static void handler_code16 (char **pp) {

    (void) pp;
    bits = 16;

}

static void handler_code32 (char **pp) {

    (void) pp;
    
    if (cpu_level < 3) {
    
        char *cpu = xmalloc (5);
        
        switch (cpu_level) {
        
            case 0:
            
                sprintf (cpu, "8086");
                break;
            
            default:
            
                sprintf (cpu, "%d86", cpu_level);
                break;
        
        }
        
        report (REPORT_ERROR, "32-bit addressing is unavailable for %s", cpu);
        free (cpu);
        
        return;
    
    }
    
    bits = 32;

}

extern void ignore_rest_of_line (char **pp);

static void handler_option (char **pp) {

    char saved_ch, *p;
    *pp = skip_whitespace (*pp);
    
    if (!**pp || **pp == '\n') {
    
        report (REPORT_ERROR, "syntax error in option directive");
        return;
    
    }
    
    p = *pp;
    saved_ch = get_symbol_name_end (pp);
    
    if (xstrcasecmp (p, "segment") == 0) {
    
        **pp = saved_ch;
        *pp = skip_whitespace (*pp);
        
        if (**pp && **pp == ':') {
        
            *pp = skip_whitespace (*pp + 1);
            
            p = *pp;
            saved_ch = get_symbol_name_end (pp);
            
            if (xstrcasecmp (p, "use16") == 0) {
                bits = 16;
            } else if (xstrcasecmp (p, "use32") == 0) {
                bits = 32;
            } else {
            
                report (REPORT_ERROR, "syntax error in option directive");
                goto out;
            
            }
            
            goto out;
        
        }
        
        report (REPORT_ERROR, "syntax error in option directive");
    
    }
    
out:
    
    **pp = saved_ch;
    ignore_rest_of_line (pp);

}

static struct pseudo_op pseudo_ops[] = {

    { ".8086",      handler_8086    },
    { ".186",       handler_186     },
    { ".286",       handler_286     },
    { ".386",       handler_386     },
    { ".387",       handler_386     },
    { ".486",       handler_486     },
    { ".586",       handler_586     },
    { ".686",       handler_686     },
    
    { ".386p",      handler_386     },
    { ".486p",      handler_486     },
    { ".586p",      handler_586     },
    { ".686p",      handler_686     },
    
    { ".code16",    handler_code16  },
    { ".code32",    handler_code32  },
    
    { ".option",    handler_option  },
    { "option",     handler_option  },
    
    { NULL,         NULL }

};


struct instruction {

    struct template template;
    char suffix;
    
    int32_t operands;
    int32_t reg_operands;
    int32_t disp_operands;
    int32_t mem_operands;
    
    uint32_t types[MAX_OPERANDS];
    const struct reg_entry *regs[MAX_OPERANDS];
    
    struct expr *imms[MAX_OPERANDS];
    struct expr *disps[MAX_OPERANDS];
    
    struct modrm_byte modrm;
    struct sib_byte sib;
    
    const struct reg_entry *base_reg;
    const struct reg_entry *index_reg;
    
    int far_call;
    uint32_t log2_scale_factor;
    
    uint32_t prefixes[MAX_PREFIXES];
    int32_t prefix_count;
    
    const struct reg_entry *segments[MAX_OPERANDS];
    int32_t force_short_jump;

};

static struct instruction instruction;
static struct expr operand_exprs[MAX_OPERANDS];

static const struct templates *current_templates;

/**
 * Returns 0 when the new prefix is of the same type as already present prefixes,
 * 2 when REPE or REPNE prefix is added and 1 when other prefix is added.
 */
static int add_prefix (unsigned char prefix) {

    int ret = 1;
    uint32_t prefix_type;

    switch (prefix) {
    
        case CS_PREFIX_OPCODE:
        case DS_PREFIX_OPCODE:
        case ES_PREFIX_OPCODE:
        case FS_PREFIX_OPCODE:
        case GS_PREFIX_OPCODE:
        case SS_PREFIX_OPCODE:
        
            prefix_type = SEGMENT_PREFIX;
            break;
        
        case REPNE_PREFIX_OPCODE:
        case REPE_PREFIX_OPCODE:
        
            prefix_type = REP_PREFIX;
            
            ret = 2;
            break;
        
        case FWAIT_OPCODE:
        
            prefix_type = FWAIT_PREFIX;
            break;
        
        case ADDR_PREFIX_OPCODE:
        
            prefix_type = ADDR_PREFIX;
            break;
        
        case DATA_PREFIX_OPCODE:
        
            prefix_type = DATA_PREFIX;
            break;

        default:
        
            report_at (__FILE__, __LINE__, REPORT_INTERNAL_ERROR, "add_prefix invalid case %i", prefix);
            exit (EXIT_FAILURE);
    
    }
    
    if (instruction.prefixes[prefix_type]) { ret = 0; }
    
    if (ret) {
    
        instruction.prefix_count++;
        instruction.prefixes[prefix_type] = prefix;
    
    } else {
        report (REPORT_ERROR, "same type of prefix used twice");
    }
    
    return (ret);

}

static int check_byte_reg (void) {

    int32_t op;
    
    for (op = instruction.operands; --op >= 0; ) {
    
        if (instruction.types[op] & REG8) {
            continue;
        }
        
        if ((instruction.types[op] & WORD_REG) && (instruction.regs[op]->number < 4)) {
        
            if (!(instruction.template.operand_types[op] & PORT)) {
                report (REPORT_WARNING, "using '%%%s' instead of '%%%s' due to '%c' suffix", (instruction.regs[op] - ((instruction.types[op] & REG16) ? 8 : 16))->name, instruction.regs[op]->name, instruction.suffix);
            }
            
            continue;
        
        }
        
        if (instruction.types[op] & (REG | SEGMENT1 | SEGMENT2 | CONTROL | DEBUG | TEST | FLOAT_REG)) {
        
            report (REPORT_ERROR, "'%%%s' not allowed with '%s%c'.", instruction.regs[op]->name, instruction.template.name, instruction.suffix);
            return 1;
        
        }
    
    }
    
    return 0;

}

static int check_word_reg (void) {

    int32_t op;
    
    for (op = instruction.operands; --op >= 0; ) {
    
        if ((instruction.types[op] & REG8) && (instruction.template.operand_types[op] & (REG16 | REG32 | ACC))) {
        
            report (REPORT_ERROR, "'%%%s' not allowed with '%s%c'.", instruction.regs[op]->name, instruction.template.name, instruction.suffix);
            return 1;
        
        }
        
        if ((instruction.types[op] & REG32) && (instruction.template.operand_types[op] & (REG16 | ACC))) {
            report (REPORT_WARNING, "using '%%%s' instead of '%%%s' due to '%c' suffix", (instruction.regs[op]-8)->name, instruction.regs[op]->name, instruction.suffix);
        }
    
    }
    
    return 0;

}

static int check_dword_reg (void) {

    int32_t op;
    
    for (op = instruction.operands; --op >= 0; ) {
    
        if ((instruction.types[op] & REG8) && (instruction.template.operand_types[op] & (REG16 | REG32 | ACC))) {
        
            report (REPORT_ERROR, "'%%%s' not allowed with '%s%c'.", instruction.regs[op]->name, instruction.template.name, instruction.suffix);
            return 1;
        
        }
        
        if ((instruction.types[op] & REG16) && (instruction.template.operand_types[op] & (REG32 | ACC))) {
            report (REPORT_WARNING, "using '%%%s' instead of '%%%s' due to '%c' suffix", (instruction.regs[op]+8)->name, instruction.regs[op]->name, instruction.suffix);
        }
    
    }
    
    return 0;

}

static int process_suffix (void) {

    int is_movsx_or_movzx = 0;
    
    if (instruction.template.opcode_modifier & (SIZE16 | SIZE32)) {
    
        if (instruction.template.opcode_modifier & SIZE16) {
            instruction.suffix = WORD_SUFFIX;
        } else {
            instruction.suffix = DWORD_SUFFIX;
        }
    
    } else if (instruction.reg_operands && (instruction.operands > 1 || (instruction.types[0] & REG))) {
    
        int32_t saved_operands = instruction.operands;
        
        is_movsx_or_movzx = ((instruction.template.base_opcode & 0xFF00) == 0x0F00
            && ((instruction.template.base_opcode & 0xFF) | 8) == 0xBE);
        
        /* For movsx/movzx only the source operand is considered for the ambiguity checking.
         * The suffix is replaced to represent the destination later. */
        if (is_movsx_or_movzx && (instruction.template.opcode_modifier & W)) {
            instruction.operands--;
        }
        
        if (!instruction.suffix) {
        
            int32_t op;

            for (op = instruction.operands; --op >= 0; ) {
            
                if ((instruction.types[op] & REG) && !(instruction.template.operand_types[op] & SHIFT_COUNT) && !(instruction.template.operand_types[op] & PORT)) {
                
                    instruction.suffix = ((instruction.types[op] & REG8) ? BYTE_SUFFIX : (instruction.types[op] & REG16) ? WORD_SUFFIX : DWORD_SUFFIX);
                    break;
                
                }
            
            }
            
            /* When .att_syntax is used, movsx and movzx silently default to byte memory source. */
            if (is_movsx_or_movzx && (instruction.template.opcode_modifier & W) && !instruction.suffix && !intel_syntax) {
                instruction.suffix = BYTE_SUFFIX;
            }
        
        } else {
        
            int ret;
            
            switch (instruction.suffix) {
            
                case BYTE_SUFFIX:
                
                    ret = check_byte_reg ();
                    break;
                
                case WORD_SUFFIX:
                
                    ret = check_word_reg ();
                    break;
                
                case DWORD_SUFFIX:
                
                    ret = check_dword_reg ();
                    break;

                default:
                
                    report_at (__FILE__, __LINE__, REPORT_INTERNAL_ERROR, "process_suffix invalid case %i", instruction.suffix);
                    exit (EXIT_FAILURE);
            
            }
            
            if (ret) { return 1; }
        
        }
        
        /* Undoes the movsx/movzx change done above. */
        instruction.operands = saved_operands;
    
    } else if ((instruction.template.opcode_modifier & DEFAULT_SIZE) && !instruction.suffix) {
        instruction.suffix = (bits == 32) ? DWORD_SUFFIX : WORD_SUFFIX;
    } else if (!instruction.suffix && ((instruction.template.operand_types[0] & JUMP_ABSOLUTE) || (instruction.template.opcode_modifier & JUMPBYTE) || (instruction.template.opcode_modifier & JUMPINTERSEGMENT) /* lgdt, lidt, sgdt, sidt */ || ((instruction.template.base_opcode == 0x0F01  && instruction.template.extension_opcode <= 3)))) {
    
        if (bits == 32) {
            
            if (!(instruction.template.opcode_modifier & NO_LSUF)) {
                instruction.suffix = DWORD_SUFFIX;
            }
        
        } else {
        
            if (!(instruction.template.opcode_modifier & NO_WSUF)) {
                instruction.suffix = WORD_SUFFIX;
            }
        
        }
    
    }
    
    if (!instruction.suffix && instruction.template.base_opcode == 0xC6) {
    
        int32_t op;
        
        for (op = instruction.operands; --op >= 0; ) {
        
            if ((instruction.types[op] & ANY_MEM) && instruction.disps[op] && instruction.disps[op]->add_symbol != NULL) {
            
                instruction.suffix = ((instruction.disps[op]->add_symbol->size == 1) ? BYTE_SUFFIX : (instruction.disps[op]->add_symbol->size == 2) ? WORD_SUFFIX : DWORD_SUFFIX);
                break;
            
            }
        
        }
    
    }
    
    if (!instruction.suffix && !(instruction.template.opcode_modifier & IGNORE_SIZE) && !(instruction.template.opcode_modifier & DEFAULT_SIZE) /* Explicit data size prefix allows determining the size. */ && !instruction.prefixes[DATA_PREFIX] /* fldenv and similar instructions do not require a suffix. */ && ((instruction.template.opcode_modifier & NO_SSUF) || (instruction.template.opcode_modifier & FLOAT_MF))) {
    
        int32_t suffixes = !(instruction.template.opcode_modifier & NO_BSUF);
        
        if (!(instruction.template.opcode_modifier & NO_WSUF)) {
            suffixes |= 1 << 1;
        }
        
        if (!(instruction.template.opcode_modifier & NO_SSUF)) {
            suffixes |= 1 << 2;
        }
        
        if (!(instruction.template.opcode_modifier & NO_LSUF)) {
            suffixes |= 1 << 3;
        }
        
        if (!(instruction.template.opcode_modifier & NO_INTELSUF)) {
            suffixes |= 1 << 4;
        }
        
        if (suffixes & (suffixes - 1)) {
        
            if (intel_syntax) {
                
                report (REPORT_ERROR, "ambiguous operand size for '%s'", instruction.template.name);
                return 1;
                
            }
            
            report (REPORT_WARNING, "%s, using default for '%s'", intel_syntax ? "ambiguous operand size" : "no instruction mnemonic suffix given and no register operands", instruction.template.name);
            
            if (instruction.template.opcode_modifier & FLOAT_MF) {
                instruction.suffix = SHORT_SUFFIX;
            } else if (is_movsx_or_movzx) {
                /* Handled below. */
            } else if (bits == 16) {
                instruction.suffix = WORD_SUFFIX;
            } else if (!(instruction.template.opcode_modifier & NO_LSUF)) {
                instruction.suffix = DWORD_SUFFIX;
            } else {
                instruction.suffix = QWORD_SUFFIX;
            }
        
        }
    
    }
    
    if (is_movsx_or_movzx) {
    
        /* The W modifier applies to the source memory or register, not to the destination register. */
        if ((instruction.template.opcode_modifier & W) && instruction.suffix && instruction.suffix != BYTE_SUFFIX) {
            instruction.template.base_opcode |= 1;
        }
        
        /* Changes the suffix to represent the destination and turns off the W modifier as it was already used above. */
        if (instruction.template.opcode_modifier & W && !instruction.suffix) {
        
            if (instruction.types[1] & REG16) {
                instruction.suffix = WORD_SUFFIX;
            } else {
                instruction.suffix = DWORD_SUFFIX;
            }
            
            instruction.template.opcode_modifier &= ~W;
        
        }
    
    }
    
    switch (instruction.suffix) {
    
        case DWORD_SUFFIX:
        
            if (instruction.template.opcode_modifier & FLOAT_MF) {
                instruction.template.base_opcode ^= 4;
            }
            
            /* fall through. */
        
        case WORD_SUFFIX:
        case QWORD_SUFFIX:
        
            /* Selects word/dword operation. */
            if (instruction.template.opcode_modifier & W) {
            
                if (instruction.template.opcode_modifier & SHORT_FORM) {
                    instruction.template.base_opcode |= 8;
                } else {
                    instruction.template.base_opcode |= 1;
                }
            
            }
            
            /* fall through. */
        
        case SHORT_SUFFIX:
        
            if (instruction.suffix != QWORD_SUFFIX && !(instruction.template.opcode_modifier & (IGNORE_SIZE | FLOAT_MF)) && ((instruction.suffix == DWORD_SUFFIX) == (bits == 16))) {
            
                uint32_t prefix = DATA_PREFIX_OPCODE;
                
                if (instruction.template.opcode_modifier & JUMPBYTE) {
                    prefix = ADDR_PREFIX_OPCODE;
                }
                
                if (!add_prefix (prefix)) {
                    return 1;
                }
            
            }
            
            break;
        
        case 0:
        
            /* Selects word/dword operation based on explicit data size prefix
             * if no suitable register are present. */
            if ((instruction.template.opcode_modifier & W) && instruction.prefixes[DATA_PREFIX] && (!instruction.reg_operands || (instruction.reg_operands == 1 && !(instruction.template.operand_types[0] & SHIFT_COUNT) && !(instruction.template.operand_types[0] & PORT) && !(instruction.template.operand_types[1] & PORT)))) {
                instruction.template.base_opcode |= 1;
            }
            
            break;
    
    }
    
    return 0;

}

static int finalize_imms (void) {

    int32_t operand;
    
    for (operand = 0; operand < instruction.operands; operand++) {
    
        uint32_t overlap = instruction.types[operand] & instruction.template.operand_types[operand];
        
        if ((overlap & IMM) && (overlap != IMM8) && (overlap != IMM8S) && (overlap != IMM16) && (overlap != IMM32)) {
        
            if (instruction.suffix) {
            
                switch (instruction.suffix) {
                
                    case BYTE_SUFFIX:
                    
                        overlap &= IMM8 | IMM8S;
                        break;
                    
                    case WORD_SUFFIX:
                    
                        overlap &= IMM16;
                        break;
                    
                    case DWORD_SUFFIX:
                    
                        overlap &= IMM32;
                        break;
                
                }
            
            } else if (overlap == (IMM16 | IMM32)) {
            
                if ((bits == 16) ^ (instruction.prefixes[DATA_PREFIX] != 0)) {
                    overlap = IMM16;
                } else {
                    overlap = IMM32;
                }
            
            } else if (instruction.prefixes[DATA_PREFIX]) {
                overlap &= (bits != 16) ? IMM16 : IMM32;
            }

            if ((overlap != IMM8) && (overlap != IMM8S) && (overlap != IMM16) && (overlap != IMM32)) {
            
                report (REPORT_ERROR, "no instruction suffix given; cannot determine immediate size");
                return 1;
            
            }
        
        }
        
        instruction.types[operand] = overlap;
    
    }
    
    return 0;

}

static uint32_t modrm_mode_from_disp_size (uint32_t type) {
    return ((type & DISP8) ? 1 : ((type & (DISP16 | DISP32)) ? 2 : 0));
}

static int process_operands (void) {

    if (instruction.template.opcode_modifier & REG_DUPLICATION) {
    
        uint32_t first_reg_operand = (instruction.types[0] & REG) ? 0 : 1;
        
        instruction.regs[first_reg_operand + 1] = instruction.regs[first_reg_operand];
        instruction.types[first_reg_operand + 1] = instruction.types[first_reg_operand];
        instruction.reg_operands = 2;
    
    }
    
    if (instruction.template.opcode_modifier & SHORT_FORM) {
    
        int32_t operand = (instruction.types[0] & (REG | FLOAT_REG)) ? 0 : 1;
        instruction.template.base_opcode |= instruction.regs[operand]->number;
    
    }
    
    if (instruction.template.opcode_modifier & MODRM) {
    
        if (instruction.reg_operands == 2) {
        
            uint32_t source, dest;
            
            source = (instruction.types[0] & (REG | SEGMENT1 | SEGMENT2 | CONTROL | DEBUG | TEST)) ? 0 : 1;
            dest = source + 1;
            
            instruction.modrm.mode = 3;
            
            if ((instruction.template.operand_types[dest] & ANY_MEM) == 0) {
            
                instruction.modrm.regmem = instruction.regs[source]->number;
                instruction.modrm.reg = instruction.regs[dest]->number;
            
            } else {
            
                instruction.modrm.regmem = instruction.regs[dest]->number;
                instruction.modrm.reg = instruction.regs[source]->number;
            
            }
        
        } else {
        
            if (instruction.mem_operands) {
            
                int32_t fake_zero_displacement = 0;
                int32_t operand = 0;
                
                if (instruction.types[0] & ANY_MEM) {
                    ;
                } else if (instruction.types[1] & ANY_MEM) {
                    operand = 1;
                } else {
                    operand = 2;
                }
                
                if (instruction.base_reg == NULL) {
                
                    instruction.modrm.mode = 0;
                    
                    if (instruction.disp_operands == 0) {
                        fake_zero_displacement = 1;
                    }
                    
                    if (instruction.index_reg == NULL) {
                    
                        if ((bits == 16) ^ (instruction.prefixes[ADDR_PREFIX] != 0)) {
                        
                            instruction.modrm.regmem = SIB_BASE_NO_BASE_REGISTER_16;
                            instruction.types[operand] = DISP16;
                        
                        } else {
                        
                            instruction.modrm.regmem = SIB_BASE_NO_BASE_REGISTER;
                            instruction.types[operand] = DISP32;
                        
                        }
                    
                    } else {
                    
                        instruction.sib.base = SIB_BASE_NO_BASE_REGISTER;
                        instruction.sib.index = instruction.index_reg->number;
                        instruction.sib.scale = instruction.log2_scale_factor;
                        
                        instruction.modrm.regmem = MODRM_REGMEM_TWO_BYTE_ADDRESSING;
                        
                        instruction.types[operand] &= ~DISP;
                        instruction.types[operand] |= DISP32;
                    
                    }
                
                } else if (instruction.base_reg->type & REG16) {
                
                    switch (instruction.base_reg->number) {
                    
                        case 3:
                        
                            if (instruction.index_reg == NULL) {
                                instruction.modrm.regmem = 7;
                            } else {
                                instruction.modrm.regmem = (instruction.index_reg->number - 6);
                            }
                            
                            break;
                        
                        case 5:
                        
                            if (instruction.index_reg == NULL) {
                            
                                instruction.modrm.regmem = 6;
                                
                                if ((instruction.types[operand] & DISP) == 0) {
                                
                                    fake_zero_displacement = 1;
                                    instruction.types[operand] |= DISP8;
                                
                                }
                            
                            } else {
                                instruction.modrm.regmem = (instruction.index_reg->number - 6 + 2);
                            }
                            
                            break;
                        
                        default:
                        
                            instruction.modrm.regmem = (instruction.base_reg->number - 6 + 4);
                            break;
                    
                    }
                    
                    instruction.modrm.mode = modrm_mode_from_disp_size (instruction.types[operand]);
                
                } else {
                
                    if (bits == 16 && (instruction.types[operand] & BASE_INDEX)) {
                        add_prefix (ADDR_PREFIX_OPCODE);
                    }
                    
                    instruction.modrm.regmem = instruction.base_reg->number;
                    
                    instruction.sib.base = instruction.base_reg->number;
                    instruction.sib.scale = instruction.log2_scale_factor;
                    
                    if (instruction.base_reg->number == 5 && instruction.disp_operands == 0) {
                    
                        fake_zero_displacement = 1;
                        instruction.types[operand] |= DISP8;
                    
                    }
                    
                    if (instruction.index_reg) {
                    
                        instruction.sib.index = instruction.index_reg->number;
                        instruction.modrm.regmem = MODRM_REGMEM_TWO_BYTE_ADDRESSING;
                    
                    } else {
                        instruction.sib.index = SIB_INDEX_NO_INDEX_REGISTER;
                    }
                    
                    instruction.modrm.mode = modrm_mode_from_disp_size (instruction.types[operand]);
                
                }
                
                if (fake_zero_displacement) {
                
                    struct expr *expr = &operand_exprs[operand];
                    instruction.disps[operand] = expr;
                    
                    expr->type = EXPR_TYPE_CONSTANT;
                    expr->add_number = 0;
                    expr->add_symbol = NULL;
                    expr->op_symbol = NULL;
                
                }
            
            }
            
            if (instruction.reg_operands) {
            
                int32_t operand = 0;
                
                if (instruction.types[0] & (REG | SEGMENT1 | SEGMENT2 | CONTROL | DEBUG | TEST)) {
                    ;
                } else if (instruction.types[1] & (REG | SEGMENT1 | SEGMENT2 | CONTROL | DEBUG | TEST)) {
                    operand = 1;
                } else {
                    operand = 2;
                }
                
                if (instruction.template.extension_opcode != NONE) {
                    instruction.modrm.regmem = instruction.regs[operand]->number;
                } else {
                    instruction.modrm.reg = instruction.regs[operand]->number;
                }
                
                if (instruction.mem_operands == 0) {
                    instruction.modrm.mode = 3;
                }
            
            }
            
            if (instruction.template.extension_opcode != NONE) {
                instruction.modrm.reg = instruction.template.extension_opcode;
            }
        
        }
    
    }
    
    if (instruction.template.opcode_modifier & SEGSHORTFORM) {
    
        if ((instruction.template.base_opcode == POP_SEGMENT_SHORT) && (instruction.regs[0]->number == 1)) {
        
            report (REPORT_ERROR, "'pop %%cs' is not valid");
            return 1;
        
        }
        
        instruction.template.base_opcode |= instruction.regs[0]->number << 3;
    
    }
    
    {
    
        int32_t operand;
        
        for (operand = 0; operand < instruction.operands; operand++) {
        
            if (instruction.segments[operand]) {
            
                add_prefix (segment_prefixes[instruction.segments[operand]->number]);
                break;
            
            }
        
        }
    
    }
    
    return 0;

}


#include    <limits.h>

static int fits_in_signed_byte (long number) {
    return ((number >= CHAR_MIN) && (number <= CHAR_MAX));
}

static int fits_in_unsigned_byte (long number) {
    return (number <= UCHAR_MAX);
}

static int fits_in_signed_word (long number) {
    return ((number >= SHRT_MIN) && (number <= SHRT_MAX));
}

static int fits_in_unsigned_word (long number) {
    return (number <= USHRT_MAX);
}


static void output_jump (void) {
    
    struct symbol *symbol;
    relax_subtype_t relax_subtype;
    
    unsigned long offset;
    unsigned long opcode_offset_in_buf;
    
    uint32_t code16 = 0;
    
    if (bits == 16) {
        code16 = RELAX_SUBTYPE_CODE16_JUMP;
    }
    
    if (instruction.prefixes[DATA_PREFIX]) {
    
        frag_append_1_char (instruction.prefixes[DATA_PREFIX]);
        instruction.prefix_count--;
        
        code16 ^= RELAX_SUBTYPE_CODE16_JUMP;
    
    }
    
    if ((instruction.prefixes[SEGMENT_PREFIX] == CS_PREFIX_OPCODE) || (instruction.prefixes[SEGMENT_PREFIX] == DS_PREFIX_OPCODE)) {
    
        frag_append_1_char (instruction.prefixes[SEGMENT_PREFIX]);
        instruction.prefix_count--;
    
    }
    
    if (instruction.prefix_count) {
        report (REPORT_WARNING, "skipping prefixes on this instruction");
    }
    
    frag_alloc_space (2 + 4);
    
    opcode_offset_in_buf = current_frag->fixed_size;
    frag_append_1_char (instruction.template.base_opcode);
    
    if (instruction.disps[0]->type == EXPR_TYPE_CONSTANT) {
    
        /* "jmp 5" is converted to "temp_label: jmp 1 + temp_label + 5".
         * The "1" is the size of the opcode
         * and it is included by calling symbol_temp_new_now ()
         * after the opcode is written above.
         */
        instruction.disps[0]->type = EXPR_TYPE_SYMBOL;
        instruction.disps[0]->add_symbol = symbol_temp_new_now ();
    
    }
    
    symbol = instruction.disps[0]->add_symbol;
    offset = instruction.disps[0]->add_number;
    
    if (!instruction.force_short_jump) {
    
        if (instruction.template.base_opcode == PC_RELATIVE_JUMP) {
            relax_subtype = ENCODE_RELAX_SUBTYPE (RELAX_SUBTYPE_UNCONDITIONAL_JUMP, RELAX_SUBTYPE_SHORT_JUMP);
        } else if (cpu_level >= 3) {
            relax_subtype = ENCODE_RELAX_SUBTYPE (RELAX_SUBTYPE_CONDITIONAL_JUMP, RELAX_SUBTYPE_SHORT_JUMP);
        } else {
            relax_subtype = ENCODE_RELAX_SUBTYPE (RELAX_SUBTYPE_CONDITIONAL_JUMP86, RELAX_SUBTYPE_SHORT_JUMP);
        }
        
        relax_subtype |= code16;
        frag_set_as_variant (RELAX_TYPE_MACHINE_DEPENDENT, relax_subtype, symbol, offset, opcode_offset_in_buf, instruction.far_call);
    
    } else {
    
        frag_set_as_variant (RELAX_TYPE_MACHINE_DEPENDENT,
                             ENCODE_RELAX_SUBTYPE (RELAX_SUBTYPE_FORCED_SHORT_JUMP, RELAX_SUBTYPE_SHORT_JUMP),
                             symbol,
                             offset,
                             opcode_offset_in_buf,
                             instruction.far_call);
    
    }

}

static void output_call_or_jumpbyte (void) {

    int32_t size;
    
    if (instruction.template.opcode_modifier & JUMPBYTE) {
    
        size = 1;
        
        if (instruction.prefixes[ADDR_PREFIX]) {
        
            frag_append_1_char (instruction.prefixes[ADDR_PREFIX]);
            instruction.prefix_count--;
        
        }
        
        if ((instruction.prefixes[SEGMENT_PREFIX] == CS_PREFIX_OPCODE) || (instruction.prefixes[SEGMENT_PREFIX] == DS_PREFIX_OPCODE)) {
        
            frag_append_1_char (instruction.prefixes[SEGMENT_PREFIX]);
            instruction.prefix_count--;
        
        }
    
    } else {
    
        uint32_t code16 = 0;
        
        if (bits == 16) {
            code16 = RELAX_SUBTYPE_CODE16_JUMP;
        }
        
        if (instruction.prefixes[DATA_PREFIX]) {
        
            frag_append_1_char (instruction.prefixes[DATA_PREFIX]);
            instruction.prefix_count--;
            
            code16 ^= RELAX_SUBTYPE_CODE16_JUMP;
        
        }
        
        size = code16 ? 2 : 4;
    
    }
    
    if (instruction.prefix_count) {
        report (REPORT_WARNING, "skipping prefixes on this instruction");
    }
    
    if (state->model < 7) {
    
        if (instruction.template.base_opcode == 0xE8 && size == 2 && (instruction.far_call || state->model >= 4)) {
        
            instruction.template.base_opcode = 0x9A;
            size += 2;
        
        }
    
    }
    
    frag_append_1_char (instruction.template.base_opcode);
    
    if (!instruction.far_call && (instruction.template.opcode_modifier & JUMPBYTE || state->model < 4)) {
    
        if (instruction.disps[0]->type == EXPR_TYPE_CONSTANT) {
        
            /* "call 5" is converted to "temp_label: call 1 + temp_label + 5".
            * The "1" is the size of the opcode
            * and it is included by calling symbol_temp_new_now ()
            * after the opcode is written above.
            */
            instruction.disps[0]->type = EXPR_TYPE_SYMBOL;
            instruction.disps[0]->add_symbol = symbol_temp_new_now ();
        
        }
        
        fixup_new_expr (current_frag, current_frag->fixed_size, size, instruction.disps[0], 1, RELOC_TYPE_DEFAULT, 0);
        frag_increase_fixed_size (size);
    
    } else if ((!instruction.far_call && size == 2) || state->model == 7) {
    
        if (instruction.disps[0]->type == EXPR_TYPE_CONSTANT) {
        
            /* "call 5" is converted to "temp_label: call 1 + temp_label + 5".
            * The "1" is the size of the opcode
            * and it is included by calling symbol_temp_new_now ()
            * after the opcode is written above.
            */
            instruction.disps[0]->type = EXPR_TYPE_SYMBOL;
            instruction.disps[0]->add_symbol = symbol_temp_new_now ();
        
        }
        
        fixup_new_expr (current_frag, current_frag->fixed_size, size, instruction.disps[0], 1, RELOC_TYPE_DEFAULT, 0);
        frag_increase_fixed_size (size);
    
    } else {
    
        unsigned char *p = frag_increase_fixed_size (size);
        
        if (instruction.disps[0]->type == EXPR_TYPE_CONSTANT) {
            machine_dependent_number_to_chars (p, instruction.disps[0]->add_number, size);
        } else {
            fixup_new_expr (current_frag, p - current_frag->buf, size, instruction.disps[0], 0, RELOC_TYPE_CALL, 1);
        }
    
    }

}

static void output_intersegment_jump (void) {

    uint32_t code16;
    uint32_t size;
    
    unsigned char *p;
    code16 = 0;
    
    if (bits == 16) {
        code16 = RELAX_SUBTYPE_CODE16_JUMP;
    }
    
    if (instruction.prefixes[DATA_PREFIX]) {
    
        frag_append_1_char (instruction.prefixes[DATA_PREFIX]);
        instruction.prefix_count--;
        
        code16 ^= RELAX_SUBTYPE_CODE16_JUMP;
    
    }
    
    if (instruction.prefix_count) {
        report (REPORT_WARNING, "skipping prefixes on this instruction");
    }
    
    size = code16 ? 2 : 4;
    frag_append_1_char (instruction.template.base_opcode);
    
    /* size for the offset, 2 for the segment. */
    p = frag_increase_fixed_size (size + 2);
    
    if (instruction.imms[1]->type == EXPR_TYPE_CONSTANT) {
    
        if ((size == 2) && !fits_in_unsigned_word (instruction.imms[1]->add_number) && !fits_in_signed_word (instruction.imms[1]->add_number)) {
        
            report (REPORT_ERROR, "16-bit jump out of range.");
            return;
        
        }
        
        machine_dependent_number_to_chars (p, instruction.imms[1]->add_number, size);
    
    } else {
        fixup_new_expr (current_frag, p - current_frag->buf, size, instruction.imms[1], 0, RELOC_TYPE_DEFAULT, instruction.far_call);
    }
    
    if (instruction.imms[0]->type != EXPR_TYPE_CONSTANT) {
        report (REPORT_ERROR, "cannot handle non absolute segment in '%s'", instruction.template.name);
    }
    
    machine_dependent_number_to_chars (p + size, instruction.imms[0]->add_number, size);

}

static struct templates *find_templates (const char *name) {

    struct hashtab_name *key;
    struct templates *templates;
    
    char *p = to_lower (name);
    
    if (p == NULL) {
        return NULL;
    }
    
    if ((key = hashtab_alloc_name (p)) == NULL) {
    
        free (p);
        return NULL;
    
    }
    
    templates = hashtab_get (&templates_hashtab, key);
    
    free (key);
    free (p);
    
    return templates;

}

/**
 * Return value meaning:
 *  1 - translate 'd' suffix to SHORT_SUFFIX (instead of DWORD_SUFFIX)
 *  2 - translate WORD_SUFFIX to SHORT_SUFFIX
 *  3 - translate WORD_SUFFIX to SHORT_SUFFIX but do no translation for WORD PTR
 * This applies only when intel_syntax is 1.
 */
static int intel_float_suffix_translation (const char *mnemonic) {

    if (mnemonic[0] != 'f') {
        return 0;                       /* Not a floating point operation. */
    }
    
    switch (mnemonic[1]) {
    
        case 'i':
        
            return 2;
        
        case 'l':
        
            if (mnemonic[2] == 'd' && (mnemonic[3] == 'c' || mnemonic[3] == 'e')) {
                return 3;               /* fldcw/fldenv */
            }
            
            break;
        
        case 'n':
        
            if (mnemonic[2] != 'o') {
                return 3;               /* Not fnop. */
            }
            
            break;
        
        case 'r':
        
            if (mnemonic[2] == 's') {
                return 3;               /* frstor */
            }
            
            break;
        
        case 's':
        
            if (mnemonic[2] == 'a') {
                return 3;               /* fsave */
            }
            
            if (mnemonic[2] == 't') {
            
                switch (mnemonic[3]) {
                
                    case 'c':           /* fstcw */
                    case 'e':           /* fstenv */
                    case 's':           /* fstsw */
                    
                        return 3;
                
                }
            
            }
            
            break;
    
    }
    
    return 1;

}

static int finalize_immediate (struct expr *expr, const char *imm_start) {

    if (expr->type == EXPR_TYPE_INVALID || expr->type == EXPR_TYPE_ABSENT) {
    
        if (imm_start) {
            report (REPORT_ERROR, "missing or invalid immediate expression '%s'", imm_start);
        }
        
        return 1;
    
    } else if (expr->type == EXPR_TYPE_CONSTANT) {
    
        /* Size will be determined later. */
        instruction.types[instruction.operands] |= IMM32;
    
    } else {
    
        /* It is an address and size will determined later. */
        instruction.types[instruction.operands] = IMM8 | IMM16 | IMM32;
    
    }
    
    return 0;

}

static int finalize_displacement (struct expr *expr, const char *disp_start) {

    if (expr->type == EXPR_TYPE_INVALID || expr->type == EXPR_TYPE_ABSENT) {
    
        if (disp_start) {
            report (REPORT_ERROR, "missing or invalid displacement expression '%s'", disp_start);
        }
        
        return 1;
    
    }
    
    return 0;

}

static int base_index_check (char *operand_string) {

    if (bits == 32) {
    
        if (instruction.base_reg && !(instruction.base_reg->type & REG16) && !(instruction.base_reg->type & REG32)) {
            goto bad;
        }
        
        if (instruction.index_reg && !(instruction.index_reg->type & REG16) && !(instruction.index_reg->type & REG32)) {
            goto bad;
        }
        
        if (instruction.index_reg && !(instruction.index_reg->type & BASE_INDEX)) {
        bad:
        
            report (REPORT_ERROR, "'%s' is not a valid base/index expression", operand_string);
            return 1;
        
        }
    
    } else {
    
        if ((instruction.base_reg && (!(instruction.base_reg->type & BASE_INDEX) || !(instruction.base_reg->type & REG16)))
            || (instruction.index_reg && (!(instruction.index_reg->type & BASE_INDEX) || !(instruction.index_reg->type & REG16)
                || !(instruction.base_reg && instruction.base_reg->number < 6 && instruction.index_reg->number >= 6
                    && instruction.log2_scale_factor == 0)))) {
            goto bad;
        }
    
    }
    
    return 0;

}

static int att_parse_operand (char *operand_string) {

    const struct reg_entry *reg;
    char *end_p;
    
    if (*operand_string == ABSOLUTE_PREFIX) {
    
        operand_string = skip_whitespace (operand_string + 1);
        instruction.types[instruction.operands] |= JUMP_ABSOLUTE;
    
    }
    
    if ((reg = parse_register (operand_string, &end_p)) != NULL) {

        if (reg == &bad_register) {
            return 1;
        }
        
        operand_string = skip_whitespace (end_p);
        
        if (*operand_string == ':' && (reg->type & (SEGMENT1 | SEGMENT2))) {
        
            instruction.segments[instruction.operands] = reg;
            
            operand_string = skip_whitespace (++operand_string);
            
            if (*operand_string == ABSOLUTE_PREFIX) {
            
                operand_string = skip_whitespace (++operand_string);
                instruction.types[instruction.operands] |= JUMP_ABSOLUTE;
            
            }
            
            goto do_memory_reference;
        
        }
        
        if (*operand_string) {

            report (REPORT_ERROR, "junk '%s' after register", operand_string);
            return 1;

        }
        
        instruction.types[instruction.operands] |= reg->type & ~BASE_INDEX;
        instruction.regs[instruction.operands] = reg;
        
        instruction.operands++;
        instruction.reg_operands++;
    
    } else if (*operand_string == REGISTER_PREFIX) {
    
        report (REPORT_ERROR, "bad register name '%s'", operand_string);
        return 1;
    
    } else if (*operand_string == IMMEDIATE_PREFIX) {
    
        struct expr *expr;
        section_t section_of_expr;
        
        char *imm_start;
        int ret;
        
        operand_string++;
        
        if (instruction.types[instruction.operands] & JUMP_ABSOLUTE) {
        
            report (REPORT_ERROR, "immediate operand is not allowed with absolute jump");
            return 1;
        
        }
        
        expr = &operand_exprs[instruction.operands];
        instruction.imms[instruction.operands] = expr;
        
        imm_start = operand_string;
        section_of_expr = expression_read_into (&operand_string, expr);
        
        operand_string = skip_whitespace (operand_string);
        
        if (*operand_string) {
        
            report (REPORT_ERROR, "junk '%s' after immediate expression", operand_string);
            return 1;
        
        }
        
        if (section_of_expr == reg_section) {
        
            report (REPORT_ERROR, "illegal immediate register operand %s", imm_start);
            return 1;
        
        }
        
        ret = finalize_immediate (expr, imm_start);
        instruction.operands++;
        
        return ret;
    
    } else {
    
        char *base_string, *displacement_string_end, *p2;
        int ret;
        
    do_memory_reference:
        
        ret = 0;
        p2 = operand_string + strlen (operand_string);
        
        base_string = p2 - 1;
        displacement_string_end = p2;
        
        if (*base_string == ')') {
        
            unsigned int paren_not_balanced = 1;
            char *temp_string;
            
            do {
            
                base_string--;
                
                if (*base_string == ')') {
                    paren_not_balanced++;
                } else if (*base_string == '(') {
                    paren_not_balanced--;
                }
            
            } while (paren_not_balanced);
            
            temp_string = base_string;
            base_string = skip_whitespace (base_string + 1);
            
            if (*base_string == ',' || (instruction.base_reg = parse_register (base_string, &end_p)) != NULL) {
            
                displacement_string_end = temp_string;
                instruction.types[instruction.operands] = BASE_INDEX;
                
                if (instruction.base_reg) {

                    if (reg == &bad_register) {
                        return 1;
                    }
                    
                    base_string = skip_whitespace (end_p);
                }
                
                /* Possible index or scale factor.*/
                if (*base_string == ',') {
                
                    base_string = skip_whitespace (base_string + 1);
                    
                    if ((instruction.index_reg = parse_register (base_string, &end_p)) != NULL) {

                        if (reg == &bad_register) {
                            return 1;
                        }
                        
                        base_string = skip_whitespace (end_p);
                        
                        if (*base_string == ',') {
                            base_string = skip_whitespace (base_string + 1);
                        } else if (*base_string != ')') {
                        
                            report (REPORT_ERROR, "expecting ',' or ')' after index register in '%s'", operand_string);
                            return 1;
                        
                        }
                    
                    } else if (*base_string == REGISTER_PREFIX) {
                    
                        end_p = strchr (base_string, ',');
                        
                        if (end_p) {
                            *end_p = '\0';
                        }
                        
                        report (REPORT_ERROR, "bad register name '%s'", base_string);
                        return 1;
                    
                    }
                    
                    if (*base_string != ')') {
                    
                        char *saved = base_string;
                        offset_t value = get_result_of_absolute_expression (&base_string);
                        
                        switch (value) {
                        
                            case 1:
                            
                                instruction.log2_scale_factor = 0;
                                break;
                            
                            case 2:
                            
                                instruction.log2_scale_factor = 1;
                                break;
                            
                            case 4:
                            
                                instruction.log2_scale_factor = 2;
                                break;
                            
                            case 8:
                            
                                instruction.log2_scale_factor = 3;
                                break;
                            
                            default:
                            
                                *base_string = '\0';
                                
                                report (REPORT_ERROR, "expecting scale factor of 1, 2, 4 or 8 but got '%s'", saved);
                                return 1;
                        
                        }
                        
                        if (instruction.log2_scale_factor && instruction.index_reg == NULL) {
                        
                            report (REPORT_WARNING, "scale factor of %i without an index register", 1 << instruction.log2_scale_factor);
                            instruction.log2_scale_factor = 0;
                        
                        }
                        
                        base_string = skip_whitespace (base_string);
                        
                        if (*base_string != ')') {
                        
                            report (REPORT_ERROR, "expecting ')' after scale factor in '%s'", operand_string);
                            return 1;
                        
                        }
                    
                    } else if (instruction.index_reg == NULL) {
                    
                        report (REPORT_ERROR, "expecting index register or scale factor after ',' but got '%c'", *base_string);
                        return 1;
                    
                    }
                
                } else if (*base_string != ')') {
                
                    report (REPORT_ERROR, "expecting ',' or ')' after base register in '%s'", operand_string);
                    return 1;
                
                }
            
            } else if (*base_string == REGISTER_PREFIX) {
            
                end_p = strchr (base_string, ',');
                
                if (end_p) {
                    *end_p = '\0';
                }
                
                report (REPORT_ERROR, "bad register name '%s'", base_string);
                return 1;
            
            }
        
        }
        
        if (base_index_check (operand_string)) {
            return 1;
        }
        
        if (operand_string != displacement_string_end) {
        
            struct expr *expr;
            unsigned int disp_type;
            
            if (bits == 32) {
                disp_type = DISP32;
            } else {
                disp_type = DISP16;
            }
            
            expr = &operand_exprs[instruction.operands];
            
            {
            
                char saved_ch;
                char *disp_start;
                
                disp_start = operand_string;
                
                saved_ch = *displacement_string_end;
                *displacement_string_end = '\0';
                
                expression_read_into (&operand_string, expr);
                
                if (*skip_whitespace (operand_string)) {
                    report (REPORT_ERROR, "junk '%s' after displacement expression", operand_string);
                }
                
                ret = finalize_displacement (expr, disp_start);
                
                *displacement_string_end = saved_ch;
            
            }
            
            instruction.types[instruction.operands] |= disp_type;
            instruction.disps[instruction.operands] = expr;
            
            instruction.disp_operands++;
        
        }
        
        instruction.operands++;
        instruction.mem_operands++;
        
        return ret;
    
    }
    
    return 0;

}


static int intel_simplify_expr (struct expr *expr);

static int intel_simplify_symbol (struct symbol *symbol) {

    int ret = intel_simplify_expr (symbol_get_value_expression (symbol));
    
    if (ret == 2) {
    
        symbol_set_section (symbol, absolute_section);
        ret = 1;
    
    }
    
    return ret;

}

static void swap_2_operands (uint32_t op1, uint32_t op2) {

    const struct reg_entry *temp_reg;
    uint32_t temp_type;
    
    struct expr *temp_expr;
    
    temp_type = instruction.types[op1];
    instruction.types[op1] = instruction.types[op2];
    instruction.types[op2] = temp_type;
    
    temp_reg = instruction.regs[op1];
    instruction.regs[op1] = instruction.regs[op2];
    instruction.regs[op2] = temp_reg;
    
    temp_expr = instruction.disps[op1];
    instruction.disps[op1] = instruction.disps[op2];
    instruction.disps[op2] = temp_expr;
    
    temp_expr = instruction.imms[op1];
    instruction.imms[op1] = instruction.imms[op2];
    instruction.imms[op2] = temp_expr;

}

static void swap_operands (void) {

    swap_2_operands (0, instruction.operands - 1);
    
    if (instruction.mem_operands == 2) {
    
        const struct reg_entry *seg;
        
        seg = instruction.segments[0];
        instruction.segments[0] = instruction.segments[1];
        instruction.segments[1] = seg;
    
    }

}

static void intel_fold_symbol_into_expr (struct expr *expr, struct symbol *symbol) {

    struct expr *symbol_expr = symbol_get_value_expression (symbol);
    
    if (symbol_get_section (symbol) == absolute_section) {
    
        offset_t add_number = expr->add_number;
        
        *expr = *symbol_expr;
        expr->add_number += add_number;
    
    } else {
    
        expr->type = EXPR_TYPE_SYMBOL;
        expr->add_symbol = symbol;
        expr->op_symbol = NULL;
    
    }

}

static int intel_process_register_expr (struct expr *expr) {

    int32_t reg_num = expr->add_number;
    
    if (intel_state.in_offset || instruction.operands < 0) {
    
        report (REPORT_ERROR, "invalid use of register");
        return 0;
    
    }
    
    if (!intel_state.in_bracket) {
    
        if (instruction.regs[instruction.operands]) {
        
            report (REPORT_ERROR, "invalid use of register");
            return 0;
        
        }
        
        if ((reg_table[reg_num].type & SEGMENT1) && reg_table[reg_num].number == REG_FLAT_NUMBER) {
        
            report (REPORT_ERROR, "invalid use of pseudo-register");
            return 0;
        
        }
        
        instruction.regs[instruction.operands] = reg_table + reg_num;
    
    } else if (!intel_state.base_reg && !intel_state.in_scale) {
        intel_state.base_reg = reg_table + reg_num;
    } else if (!intel_state.index_reg) {
        intel_state.index_reg = reg_table + reg_num;
    } else{
        intel_state.index_reg = reg_esp;
    }
    
    return 2;

}

static int intel_simplify_expr (struct expr *expr) {

    int ret;
    
    switch (expr->type) {
    
        case EXPR_TYPE_INDEX:
        
            if (expr->add_symbol) {
            
                if (!intel_simplify_symbol (expr->add_symbol)) {
                    return 0;
                }
            
            }
            
            if (!intel_state.in_offset) {
                intel_state.in_bracket++;
            }
            
            ret = intel_simplify_symbol (expr->op_symbol);
            
            if (!intel_state.in_offset) {
                intel_state.in_bracket--;
            }
            
            if (!ret) {
                return 0;
            }
            
            if (expr->add_symbol) {
                expr->type = EXPR_TYPE_ADD;
            } else {
                intel_fold_symbol_into_expr (expr, expr->op_symbol);
            }
            
            break;
        
        case EXPR_TYPE_OFFSET:
        
            intel_state.has_offset = 1;
            intel_state.in_offset++;
            
            ret = intel_simplify_symbol (expr->add_symbol);
            intel_state.in_offset--;
            
            if (!ret) { return 0; }
            
            intel_fold_symbol_into_expr (expr, expr->add_symbol);
            return ret;
        
        case EXPR_TYPE_MULTIPLY:
        
            if (intel_state.in_bracket) {
            
                struct expr *scale_expr = NULL;
                
                if (!intel_state.in_scale++) {
                    intel_state.scale_factor = 1;
                }
                
                ret = intel_simplify_symbol (expr->add_symbol);
                
                if (ret && intel_state.index_reg) {
                    scale_expr = symbol_get_value_expression (expr->op_symbol);
                }
                
                if (ret) {
                    ret = intel_simplify_symbol (expr->op_symbol);
                }
                
                if (ret && !scale_expr && intel_state.index_reg) {
                    scale_expr = symbol_get_value_expression (expr->add_symbol);
                }
                
                if (ret && scale_expr) {
                
                    resolve_expression (scale_expr);
                    
                    if (scale_expr->type != EXPR_TYPE_CONSTANT) {
                        scale_expr->add_number = 0;
                    }
                    
                    intel_state.scale_factor *= scale_expr->add_number;
                
                }
                
                intel_state.in_scale--;
                
                if (!ret) {
                    return 0;
                }
                
                if (!intel_state.in_scale) {
                
                    switch (intel_state.scale_factor) {
                    
                        case 1:
                        
                            instruction.log2_scale_factor = 0;
                            break;
                        
                        case 2:
                        
                            instruction.log2_scale_factor = 1;
                            break;
                        
                        case 4:
                        
                            instruction.log2_scale_factor = 2;
                            break;
                        
                        case 8:
                        
                            instruction.log2_scale_factor = 3;
                            break;
                        
                        default:
                        
                            /* %esp is invalid as index register. */
                            intel_state.index_reg = reg_esp;
                            break;
                    
                    }
                
                }
                
                break;
            
            }
            
            goto default_;
        
        case EXPR_TYPE_SHORT:
        
            instruction.force_short_jump = 1;
            goto ptr_after_setting_operand_modifier;
        
        case EXPR_TYPE_FAR_PTR:
        case EXPR_TYPE_BYTE_PTR:
        case EXPR_TYPE_NEAR_PTR:
        case EXPR_TYPE_WORD_PTR:
        case EXPR_TYPE_DWORD_PTR:
        case EXPR_TYPE_FWORD_PTR:
        case EXPR_TYPE_QWORD_PTR:
        
            if (intel_state.operand_modifier == EXPR_TYPE_ABSENT) {
                intel_state.operand_modifier = expr->type;
            }
            
        ptr_after_setting_operand_modifier:
            
            if (!intel_simplify_symbol (expr->add_symbol)) {
                return 0;
            }
            
            intel_fold_symbol_into_expr (expr, expr->add_symbol);
            break;
        
        case EXPR_TYPE_FULL_PTR:
        
            if (symbol_get_value_expression (expr->op_symbol)->type == EXPR_TYPE_REGISTER) {
            
                report (REPORT_ERROR, "invalid use of register");
                return 0;
            
            }
            
            if (!intel_simplify_symbol (expr->op_symbol)) {
                return 0;
            }
            
            if (!intel_state.in_offset) {
            
                if (!intel_state.segment) {
                    intel_state.segment = expr->add_symbol;
                } else {
                
                    struct expr temp_expr = {0};
                    
                    temp_expr.type = EXPR_TYPE_FULL_PTR;
                    temp_expr.add_symbol = expr->add_symbol;
                    temp_expr.op_symbol = intel_state.segment;
                    
                    intel_state.segment = make_expr_symbol (&temp_expr);
                
                }
            
            }
            
            intel_fold_symbol_into_expr (expr, expr->op_symbol);
            break;
        
        case EXPR_TYPE_REGISTER:
        
            ret = intel_process_register_expr (expr);
            
            if (ret == 2) {
            
                expr->type = EXPR_TYPE_CONSTANT;
                expr->add_number = 0;
            
            }
            
            return ret;
        
        default_:
        default:
        
            if (expr->add_symbol && !intel_simplify_symbol (expr->add_symbol)) {
                return 0;
            }
            
            if (expr->op_symbol && !intel_simplify_symbol (expr->op_symbol)) {
                return 0;
            }
            
            break;
    
    }
    
    if (expr->type == EXPR_TYPE_SYMBOL && !intel_state.in_offset) {
    
        section_t section = symbol_get_section (expr->add_symbol);
        
        if (section != absolute_section && section != expr_section && section != reg_section) {
            intel_state.is_mem |= 2 - !intel_state.in_bracket;
        }
    
    }   
    
    return 1;

}


static int intel_parse_operand (char *operand_string) {

    int ret;
    
    struct expr expr_buf, *expr;
    char *operand_start;
    
    memset (&intel_state, 0, sizeof (intel_state));
    intel_state.operand_modifier = EXPR_TYPE_ABSENT;
    
    expr = &expr_buf;
    operand_start = operand_string;
    
    intel_syntax = -1;
    expression_read_into (&operand_string, expr);
    
    if (expr->type == EXPR_TYPE_SYMBOL) {
    
        if (strcmp (expr->add_symbol->name, "DGROUP") == 0) {
            expr->type = EXPR_TYPE_OFFSET;
        }
    
    }
    
    ret = intel_simplify_expr (expr);
    intel_syntax = 1;
    
    operand_string = skip_whitespace (operand_string);
    
    if (*operand_string) {
    
        report (REPORT_ERROR, "junk '%s' after expression", operand_string);
        return 1;
    
    } else if (!intel_state.has_offset && operand_string > operand_start && strrchr (operand_start, ']') && skip_whitespace (strrchr (operand_start, ']') + 1) == operand_string) {
        intel_state.is_mem |= 1;
    }
    
    if (!ret) {
        return 1;
    }
    
    ret = 0;
    
    if (intel_state.operand_modifier != EXPR_TYPE_ABSENT && current_templates->start->base_opcode != 0x8D /* lea */) {
    
        char suffix = 0;
        
        switch (intel_state.operand_modifier) {
        
            case EXPR_TYPE_FAR_PTR:
            
                if (bits != 32 && (current_templates->start->opcode_modifier & CALL) && state->model < 7) {
                    instruction.far_call = 1;
                }
                
                break;
            
            case EXPR_TYPE_NEAR_PTR:
            
                break;
            
            case EXPR_TYPE_BYTE_PTR:
            
                if (strcmp (current_templates->name, "call")) {
                
                    suffix = BYTE_SUFFIX;
                    break;
                
                }
                
                /* fall through */
            
            case EXPR_TYPE_WORD_PTR:
            
                if (intel_float_suffix_translation (current_templates->name) == 2) {
                    suffix = SHORT_SUFFIX;
                } else {
                    suffix = WORD_SUFFIX;
                }
                
                break;
            
            case EXPR_TYPE_DWORD_PTR:
            
                if (bits != 32 && ((current_templates->start->opcode_modifier & JUMP) || (current_templates->start->opcode_modifier & CALL))) {
                    suffix = INTEL_SUFFIX;
                } else if (intel_float_suffix_translation (current_templates->name) == 1) {
                    suffix = SHORT_SUFFIX;
                } else if (strcmp (current_templates->name, "lds") == 0 || strcmp (current_templates->name, "les") == 0 || strcmp (current_templates->name, "lfs") == 0 || strcmp (current_templates->name, "lgs") == 0 || strcmp (current_templates->name, "lss") == 0) {
                    suffix = WORD_SUFFIX;
                } else {
                    suffix = DWORD_SUFFIX;
                }
                
                break;
            
            case EXPR_TYPE_FWORD_PTR:
            
                /* lgdt, lidt, sgdt, sidt accept fword ptr but ignore it. */
                if ((current_templates->name[0] == 'l' || current_templates->name[0] == 's') && (current_templates->name[1] == 'g' || current_templates->name[1] == 'i') && current_templates->name[2] == 'd' && current_templates->name[3] == 't' && current_templates->name[4] == '\0') {
                    break;
                }
                
                if (!intel_float_suffix_translation (current_templates->name)) {
                
                    if (bits == 16) {
                        add_prefix (DATA_PREFIX_OPCODE);
                    }
                    
                    suffix = INTEL_SUFFIX;
                
                }
                
                break;
            
            case EXPR_TYPE_QWORD_PTR:
            
                if (intel_float_suffix_translation (current_templates->name) == 1) {
                    suffix = DWORD_SUFFIX;
                } else {
                    suffix = QWORD_SUFFIX;
                }
                
                break;
            
            default:
            
                break;
        
        }
        
        if (!instruction.suffix) {
            instruction.suffix = suffix;
        } else if (instruction.suffix != suffix) {
        
            report (REPORT_ERROR, "conficting operand size modifiers");
            return 1;
        
        }
    
    }
    
    if ((current_templates->start->opcode_modifier & JUMP) || (current_templates->start->opcode_modifier & CALL) || (current_templates->start->opcode_modifier & JUMPINTERSEGMENT)) {
    
        int is_absolute_jump = 0;
        
        if (instruction.regs[instruction.operands] || intel_state.base_reg || intel_state.index_reg || intel_state.is_mem > 1) {
            is_absolute_jump = 1;
        } else {
        
            switch (intel_state.operand_modifier) {
            
                case EXPR_TYPE_ABSENT:
                case EXPR_TYPE_FAR_PTR:
                case EXPR_TYPE_NEAR_PTR:
                
                    intel_state.is_mem = 1;
                    break;
                
                default:
                
                    is_absolute_jump = 1;
                    break;
            
            }
        
        }
        
        if (is_absolute_jump) {
        
            instruction.types[instruction.operands] |= JUMP_ABSOLUTE;
            intel_state.is_mem |= 1;
        
        }
    
    }
    
    if (instruction.regs[instruction.operands]) {
    
        instruction.types[instruction.operands] |= instruction.regs[instruction.operands]->type & ~BASE_INDEX;
        instruction.reg_operands++;
    
    } else if (intel_state.base_reg || intel_state.index_reg || intel_state.segment || intel_state.is_mem) {
    
        if (instruction.mem_operands >= 1) {

            /**
             * Handles "call 0x9090, 0x9090", "lcall 0x9090, 0x9090",
             * "jmp 0x9090, 0x9090", "ljmp 0x9090, 0x9090".
             */
            if (((current_templates->start->opcode_modifier & JUMP) || (current_templates->start->opcode_modifier & CALL) || (current_templates->start->opcode_modifier & JUMPINTERSEGMENT)) && instruction.operands == 1 && instruction.mem_operands == 1 && instruction.disp_operands == 1 && intel_state.segment == NULL && intel_state.operand_modifier == EXPR_TYPE_ABSENT) {
            
                instruction.operands = 0;
                
                if (!finalize_immediate (instruction.disps[instruction.operands], NULL)) {
                
                    instruction.imms[instruction.operands] = instruction.disps[instruction.operands];
                    instruction.operands = 1;
                    
                    operand_exprs[instruction.operands] = *expr;
                    instruction.imms[instruction.operands] = &operand_exprs[instruction.operands];
                    
                    resolve_expression (instruction.imms[instruction.operands]);
                    
                    if (!finalize_immediate (instruction.imms[instruction.operands], operand_start)) {
                    
                        instruction.mem_operands = 0;
                        instruction.disp_operands = 0;
                        instruction.operands = 2;
                        
                        instruction.types[0] &= ~ANY_MEM;
                        return 0;
                    
                    }
                
                }
            
            }
            
            report (REPORT_ERROR, "too many memory references for '%s'", current_templates->name);
            return 1;
        
        }
        
        if (intel_state.base_reg && intel_state.index_reg && (intel_state.base_reg->type & REG16) && (intel_state.index_reg->type & REG16) && intel_state.base_reg->number >= 6 && intel_state.index_reg->number < 6) {
        
            /* Converts [si + bp] to [bp + si] as addition is commutative but other code accepts only (%bp,%si), not (%si,%bp). */
            instruction.base_reg = intel_state.index_reg;
            instruction.index_reg = intel_state.base_reg;
        
        } else {
        
            instruction.base_reg = intel_state.base_reg;
            instruction.index_reg = intel_state.index_reg;
        
        }
        
        if (instruction.base_reg || instruction.index_reg) {
            instruction.types[instruction.operands] |= BASE_INDEX;
        }
        
        operand_exprs[instruction.operands] = *expr;
        expr = &operand_exprs[instruction.operands];
        
        resolve_expression (expr);
        
        if (expr->type != EXPR_TYPE_CONSTANT || expr->add_number || !(instruction.types[instruction.operands] & BASE_INDEX)) {
        
            instruction.disps[instruction.operands] = expr;
            instruction.disp_operands++;
            
            if ((bits == 16) ^ !instruction.prefixes[ADDR_PREFIX]) {
                instruction.types[instruction.operands] |= DISP32;
            } else {
                instruction.types[instruction.operands] |= DISP16;
            }
            
            if (finalize_displacement (instruction.disps[instruction.operands], operand_start)) {
                return 1;
            }
        
        }
        
        if (intel_state.segment) {
        
            int more_than_1_segment = 0;
            
            while (1) {
            
                expr = symbol_get_value_expression (intel_state.segment);
                
                if (expr->type != EXPR_TYPE_FULL_PTR || symbol_get_value_expression (expr->op_symbol)->type != EXPR_TYPE_REGISTER) {
                    break;
                }
                
                intel_state.segment = expr->add_symbol;
                more_than_1_segment = 1;
            
            }
            
            if (expr->type != EXPR_TYPE_REGISTER) {
            
                report (REPORT_ERROR, "segment register name expected");
                return 1;
            
            }
            
            if ((reg_table[expr->add_number].type & (SEGMENT1 | SEGMENT2)) == 0) {
            
                report (REPORT_ERROR, "invalid use of register");
                return 1;
            
            }
            
            if (more_than_1_segment) {
                report (REPORT_WARNING, "redundant segment overrides");
            }
            
            if (reg_table[expr->add_number].number == REG_FLAT_NUMBER) {
                instruction.segments[instruction.operands] = NULL;
            } else {
                instruction.segments[instruction.operands] = &reg_table[expr->add_number];
            }
        
        }
        
        if (base_index_check (operand_start)) {
            return 1;
        }
        
        instruction.mem_operands++;
    
    } else {
    
        operand_exprs[instruction.operands] = *expr;
        instruction.imms[instruction.operands] = &operand_exprs[instruction.operands];
        
        resolve_expression (instruction.imms[instruction.operands]);
        ret = finalize_immediate (instruction.imms[instruction.operands], operand_start);
    
    }
    
    instruction.operands++;
    return ret;

}

static char *parse_instruction (char *line) {

    const char *expecting_string_instruction = NULL;
    
    char *p2;
    char saved_ch;
    
    current_templates = NULL;
    
    while (1) {
    
        p2 = line = skip_whitespace (line);
        
        while ((*p2 != ' ') && (*p2 != '\t') && (*p2 != '\0')) {
        
            *p2 = tolower (*p2);
            p2++;
        
        }
        
        saved_ch = *p2;
        *p2 = '\0';
        
        if (line == p2) {
        
            report (REPORT_ERROR, "expecting mnemonic; got nothing");
            return (line);
        
        }
        
        current_templates = find_templates (line);
        
        if (saved_ch && (*skip_whitespace (p2 + 1)) && current_templates && (current_templates->start->opcode_modifier & IS_PREFIX)) {
        
            if ((current_templates->start->opcode_modifier & (SIZE16 | SIZE32)) && (((current_templates->start->opcode_modifier & SIZE32) != 0) ^ (bits == 16))) {
            
                report (REPORT_ERROR, "redundant %s prefix", current_templates->name);
                return (line);
            
            }
            
            switch (add_prefix (current_templates->start->base_opcode)) {
            
                case 0:
                
                    return (line);
                
                case 2:
                
                    expecting_string_instruction = current_templates->name;
                    break;
            
            }
            
            *p2 = saved_ch;
            line = p2 + 1;
        
        } else {
            break;
        }
    
    }
    
    if (current_templates == NULL) {
    
        switch (p2[-1]) {
        
            case WORD_SUFFIX:
            
                if (intel_syntax && (intel_float_suffix_translation (line) & 2)) {
                    p2[-1] = SHORT_SUFFIX;
                }
                
                /* fall through. */
            
            case BYTE_SUFFIX:
            case QWORD_SUFFIX:
            
                instruction.suffix = p2[-1];
                p2[-1] = '\0';
                
                break;
            
            case SHORT_SUFFIX:
            case DWORD_SUFFIX:
            
                if (!intel_syntax) {
                
                    instruction.suffix = p2[-1];
                    p2[-1] = '\0';
                
                }
                
                break;
            
            /* Intel syntax only. */
            case 'd':
            
                if (intel_syntax) {
                
                    if (intel_float_suffix_translation (line) == 1) {
                        instruction.suffix = SHORT_SUFFIX;
                    } else {
                        instruction.suffix = DWORD_SUFFIX;
                    }
                    
                    p2[-1] = '\0';

                }
                
                break;
            
            default:
            
                report (REPORT_ERROR, "no such instruction '%s'", line);
                return (line);
        
        }
        
        current_templates = find_templates (line);
        
        if (current_templates == NULL) {
        
            report (REPORT_ERROR, "no such instruction '%s'", line);
            return (line);
        
        }
    
    }
    
    if (expecting_string_instruction) {
    
        if (!(current_templates->start->opcode_modifier & IS_STRING)) {
        
            report (REPORT_ERROR, "expecting string instruction after '%s'", expecting_string_instruction);
            return (line);
        
        }
    
    }
    
    *p2 = saved_ch;
    line = p2;
    
    return (line);

}

static int parse_operands (char **p_line) {

    char *line = *p_line;

    while (*line != '\0') {
    
        uint32_t paren_not_balanced = 0;
        char *token_start;
        
        line = skip_whitespace (line);
        token_start = line;
        
        while (paren_not_balanced || (*line != ',')) {
        
            if (*line == '\0') {
            
                if (paren_not_balanced) {
                
                    report (REPORT_ERROR, "unbalanced parentheses in operand %u", instruction.operands);
                    *p_line = line;
                    
                    return 1;
                
                } else {
                    break;
                }
            
            } else if (line[0] == '#' || (line[0] == '/' && (line[1] == '/' || line[1] == '*'))) {
            
                if (!paren_not_balanced) {
                    break;
                }
            
            }
            
            if (!intel_syntax) {
            
                if (*line == '(') { paren_not_balanced++; }
                if (*line == ')') { paren_not_balanced--; }
            
            }
            
            line++;
        
        }
        
        if (token_start != line) {
        
            int ret;
            char saved_ch;
            
            saved_ch = *line;
            *line = '\0';
            
            if (intel_syntax) {
                ret = intel_parse_operand (token_start);
            } else {
                ret = att_parse_operand (token_start);
            }
            
            *line = saved_ch;
            
            if (ret) {
            
                *p_line = line;
                return 1;
            
            }
        
        }
        
        if (line[0] == '#' || (line[0] == '/' && line[1] == '/')) {
            break;
        }
        
        if (*line == ',') { line++; }
    
    }
    
    *p_line = line;
    return 0;
    
}


#define MATCH(overlap, operand_type)    (((overlap) & ~JUMP_ABSOLUTE) && (((operand_type) & (BASE_INDEX | JUMP_ABSOLUTE)) == ((overlap) & (BASE_INDEX | JUMP_ABSOLUTE))))

static int match_template (void) {

    const struct template *template;
    
    uint32_t found_reverse_match = 0;
    uint32_t suffix_check = 0;
    
    switch (instruction.suffix) {
    
        case BYTE_SUFFIX:
        
            suffix_check = NO_BSUF;
            break;
        
        case WORD_SUFFIX:
        
            suffix_check = NO_WSUF;
            break;
        
        case SHORT_SUFFIX:
        
            suffix_check = NO_SSUF;
            break;
        
        case DWORD_SUFFIX:
        
            suffix_check = NO_LSUF;
            break;
        
        case QWORD_SUFFIX:
        
            suffix_check = NO_QSUF;
            break;
        
        case INTEL_SUFFIX:
        
            suffix_check = NO_INTELSUF;
            break;
    
    }
    
    if (current_templates[0].start->base_opcode == 0xCD && instruction.operands == 1 && instruction.types[0] & IMM) {

        if (instruction.imms[0]->type == EXPR_TYPE_CONSTANT && instruction.imms[0]->add_number == 3) {
        
            instruction.template.base_opcode = 0xCC;
            instruction.operands--;
            
            return 0;
        
        }
    
    }
    
    for (template = current_templates->start ; template < current_templates->end; template++) {
    
        uint32_t operand_type_overlap0, operand_type_overlap1, operand_type_overlap2;
        
        if (instruction.operands != template->operands) {
            continue;
        }
        
        if (template->opcode_modifier & suffix_check) {
            continue;
        }
        
        if (instruction.operands == 0) {
            break;
        }
        
        operand_type_overlap0 = instruction.types[0] & template->operand_types[0];
        
        switch (template->operands) {
        
            case 1:
            
                if (!MATCH (operand_type_overlap0, instruction.types[0])) {
                    continue;
                }
                
                if (operand_type_overlap0 == 0) {
                    continue;
                }
                
                break;
            
            case 2:
            case 3:
            
                operand_type_overlap1 = instruction.types[1] & template->operand_types[1];
                
                if (!MATCH (operand_type_overlap0, instruction.types[0]) || !MATCH (operand_type_overlap1, instruction.types[1])) {
                
                    if ((template->opcode_modifier & (D | FLOAT_D)) == 0) {
                        continue;
                    }
                    
                    operand_type_overlap0 = instruction.types[0] & template->operand_types[1];
                    operand_type_overlap1 = instruction.types[1] & template->operand_types[0];
                    
                    if (!MATCH (operand_type_overlap0, instruction.types[0]) || !MATCH (operand_type_overlap1, instruction.types[1])) {
                        continue;
                    }
                    
                    found_reverse_match = template->opcode_modifier & (D | FLOAT_D);
                
                } else if (instruction.operands == 3) {
                
                    operand_type_overlap2 = instruction.types[2] & template->operand_types[2];
                    
                    if (!MATCH (operand_type_overlap2, instruction.types[2])) {
                        continue;
                    }
                
                }
                
                break;
        
        }
        
        break;
    
    }
    
    if (template == current_templates->end) {
    
        /* No match was found. */
        report (REPORT_ERROR, "operands invalid for '%s'", current_templates->name);
        return 1;
    
    }
    
    instruction.template = *template;
    
    if (state->model < 7) {
    
        if (template->base_opcode == 0xC3 && xstrcasecmp (template->name, "retn") && instruction.operands == 0 && state->model >= 4 && state->procs.length > 0) {
            instruction.template.base_opcode = 0xCB;
        }
        
        if (template->base_opcode == 0xC2 && instruction.operands == 1 && state->model >= 4 && state->procs.length > 0) {
            instruction.template.base_opcode = 0xCA;
        }
    
    }
    
    if (found_reverse_match) {
    
        instruction.template.base_opcode |= found_reverse_match;
        
        instruction.template.operand_types[0] = template->operand_types[1];
        instruction.template.operand_types[1] = template->operand_types[0];
    
    }
    
    return 0;

}

uint32_t smallest_imm_type (long number) {

    if (fits_in_signed_byte (number)) {
        return (IMM8S | IMM8 | IMM16 | IMM32);
    }
    
    if (fits_in_unsigned_byte (number)) {
        return (IMM8 | IMM16 | IMM32);
    }
    
    if (fits_in_signed_word (number) || fits_in_unsigned_word (number)) {
        return (IMM16 | IMM32);
    }
    
    return IMM32;

}

static void optimize_size_of_disps (void) {

    int32_t operand;
    
    for (operand = 0; operand < instruction.operands; operand++) {
    
        if (instruction.types[operand] & DISP) {
        
            if (instruction.disps[operand]->type == EXPR_TYPE_CONSTANT) {
            
                unsigned long disp = instruction.disps[operand]->add_number;
                
                if (instruction.types[operand] & DISP32) {
                
                    disp &= 0xffffffff;
                    disp = (disp ^ (1UL << 31)) - (1UL << 31);
                
                }
                
                if ((instruction.types[operand] & (DISP16 | DISP32)) && fits_in_signed_byte (disp)) {
                    instruction.types[operand] |= DISP8;
                }
            
            }
        
        }
    
    }

}

static void optimize_size_of_imms (void) {

    char guessed_suffix = 0;
    int32_t operand;
    
    if (instruction.suffix) {
        guessed_suffix = instruction.suffix;
    } else if (instruction.reg_operands) {
    
        /**
         * Guesses a suffix from the last register operand
         * what is good enough for shortening immediates
         * but the real suffix cannot be set yet.
         * Example: mov $1234, %al
         */
        for (operand = instruction.operands; --operand >= 0; ) {
        
            if (instruction.types[operand] & REG) {
            
                guessed_suffix = ((instruction.types[operand] & REG8) ? BYTE_SUFFIX : (instruction.types[operand] & REG16) ? WORD_SUFFIX : DWORD_SUFFIX);
                break;
            
            }
        
        }
    
    } else if ((bits == 16) ^ (instruction.prefixes[DATA_PREFIX] != 0)) {
    
        /**
         * Immediate shortening for 16 bit code.
         * Example: .code16\n push $12341234
         */
        guessed_suffix = WORD_SUFFIX;
    
    }
    
    for (operand = 0; operand < instruction.operands; operand++) {
    
        if (instruction.types[operand] & IMM) {
        
            if (instruction.imms[operand]->type == EXPR_TYPE_CONSTANT) {
            
                /* If a suffix is given, it is allowed to shorten the immediate. */
                switch (guessed_suffix) {
                
                    case BYTE_SUFFIX:
                    
                        instruction.types[operand] |= IMM8 | IMM8S | IMM16 | IMM32;
                        break;
                    
                    case WORD_SUFFIX:
                    
                        instruction.types[operand] |= IMM16 | IMM32;
                        break;
                    
                    case DWORD_SUFFIX:
                    
                        instruction.types[operand] |= IMM32;
                        break;
                
                }
                
                if (instruction.types[0] & IMM32) {
                
                    instruction.imms[operand]->add_number &= 0xffffffff;
                    instruction.imms[operand]->add_number = ((instruction.imms[operand]->add_number ^ (1UL << 31)) - (1UL << 31));
                
                }
                
                instruction.types[operand] |= smallest_imm_type (instruction.imms[operand]->add_number);
            
            }
        
        }
    
    }

}

static long convert_number_to_size (unsigned long value, int32_t size) {

    unsigned long mask;
    
    switch (size) {
    
        case 1:
        
            mask = 0xff;
            break;
        
        case 2:
        
            mask = 0xffff;
            break;
        
        case 4:
        
            mask = 0xffffffff;
            break;

        default:
        
            report_at (__FILE__, __LINE__, REPORT_INTERNAL_ERROR, "convert_number_to_size invalid case %i", size);
            exit (EXIT_FAILURE);
    
    }
    
    if ((value & ~mask) && ((value & ~mask) != ~mask)) {
        report (REPORT_WARNING, "%ld shortened to %ld", value, value & mask);
    }
    
    value &= mask;
    return value;

}

static void output_disps (void) {

    int32_t operand;
    
    for (operand = 0; operand < instruction.operands; operand++) {
    
        if (instruction.types[operand] & DISP) {
        
            if (instruction.disps[operand]->type == EXPR_TYPE_CONSTANT) {
            
                int size = 4;
                unsigned long value;
                
                if (instruction.types[operand] & DISP8) {
                    size = 1;
                } else if (instruction.types[operand] & DISP16) {
                    size = 2;
                }
                
                value = convert_number_to_size (instruction.disps[operand]->add_number, size);
                machine_dependent_number_to_chars (frag_increase_fixed_size (size), value, size);
            
            } else {
            
                int size = 4;
                
                if (instruction.types[operand] & DISP8) {
                    size = 1;
                } else if (instruction.types[operand] & DISP16) {
                    size = 2;
                }
                
                fixup_new_expr (current_frag, current_frag->fixed_size, size, instruction.disps[operand], 0, RELOC_TYPE_DEFAULT, 0);
                frag_increase_fixed_size (size);
            
            }
        
        }
    
    }

}

static void output_imm (unsigned int operand) {

    if (instruction.types[operand] & IMM) {
    
        if (instruction.imms[operand]->type == EXPR_TYPE_CONSTANT) {
        
            int size = 4;
            unsigned long value;
            
            if (instruction.types[operand] & (IMM8 | IMM8S)) {
                size = 1;
            } else if (instruction.types[operand] & IMM16) {
                size = 2;
            }
            
            value = convert_number_to_size (instruction.imms[operand]->add_number, size);
            machine_dependent_number_to_chars (frag_increase_fixed_size (size), value, size);
        
        } else {
        
            int size = 4;
            
            if (instruction.types[operand] & (IMM8 | IMM8S)) {
                size = 1;
            } else if (instruction.types[operand] & IMM16) {
                size = 2;
            }
            
            fixup_new_expr (current_frag, current_frag->fixed_size, size, instruction.imms[operand], 0, RELOC_TYPE_DEFAULT, 0);
            frag_increase_fixed_size (size);
        
        }
    
    }

}

static void output_imms (void) {

    int operand;
    
    for (operand = 0; operand < instruction.operands; operand++) {
        output_imm (operand);
    }

}


enum expr_type machine_dependent_parse_operator (char **pp, char *name, char *original_saved_c, uint32_t operands) {

    size_t i;
    
    if (!intel_syntax) {
        return EXPR_TYPE_ABSENT;
    }
    
    if (name == NULL) {
    
        if (operands != 2) {
            return EXPR_TYPE_INVALID;
        }
        
        switch (**pp) {
        
            case ':':
            
                ++*pp;
                return EXPR_TYPE_FULL_PTR;
            
            case '[':
            
                ++*pp;
                return EXPR_TYPE_INDEX;
        
        }
        
        return EXPR_TYPE_INVALID;
    
    }
    
    for (i = 0; intel_types[i].name; i++) {
    
        if (xstrcasecmp (name, intel_types[i].name) == 0) {
            break;
        }
    
    }
    
    if (intel_types[i].name && *original_saved_c == ' ') {
    
        char *second_name;
        char c;
        
        ++*pp;
        second_name = *pp;
        c = get_symbol_name_end (pp);
        
        if (xstrcasecmp (second_name, "ptr") == 0) {
        
            second_name[-1] = *original_saved_c;
            *original_saved_c = c;
            
            return intel_types[i].expr_type;
        
        }
        
        **pp = c;
        *pp = second_name - 1;
        
        return EXPR_TYPE_ABSENT;
    
    }
    
    for (i = 0; intel_operators[i].name; i++) {
    
        if (xstrcasecmp (name, intel_operators[i].name) == 0) {
        
            if (operands != intel_operators[i].operands) {
                return EXPR_TYPE_INVALID;
            }
            
            return intel_operators[i].expr_type;
        
        }
    
    }
    
    return EXPR_TYPE_ABSENT;

}

section_t machine_dependent_simplified_expression_read_into (char **pp, struct expr *expr) {

    int ret;
    section_t ret_section;
    
    if (!intel_syntax) {
        return expression_read_into (pp, expr);
    }
    
    memset (&intel_state, 0, sizeof (intel_state));
    intel_state.operand_modifier = EXPR_TYPE_ABSENT;
    
    instruction.operands = -1;
    
    intel_syntax = -1;
    
    ret_section = expression_read_into (pp, expr);
    ret = intel_simplify_expr (expr);
    
    intel_syntax = 1;
    
    if (!ret) {
    
        report (REPORT_ERROR, "bad machine-dependent expression");
        expr->type = EXPR_TYPE_INVALID;
    
    }
    
    return ret_section;

}

char *machine_dependent_assemble_line (char *line) {

    memset (&instruction, 0, sizeof (instruction));
    memset (operand_exprs, 0, sizeof (operand_exprs));
    
    line = parse_instruction (line);
    
    if (current_templates == NULL || parse_operands (&line)) {
        goto skip;
    }
    
    if (current_templates[0].start->base_opcode == 0xC0 && instruction.operands == 2 && instruction.types[1] & IMM) {
    
        if (instruction.imms[1]->type == EXPR_TYPE_CONSTANT && instruction.imms[1]->add_number == 1) {
            instruction.operands--;
        }
    
    }
    
    /**
     * All Intel instructions have reversed operands except "bound" and some other.
     * "ljmp" and "lcall" with 2 immediate operands also do not have operands reversed.
     */
    if (intel_syntax && instruction.operands > 1 && strcmp (current_templates->name, "bound") && !((instruction.types[0] & IMM) && (instruction.types[1] & IMM))) {
        swap_operands ();
    }
    
    optimize_size_of_disps ();
    optimize_size_of_imms ();
    
    if (match_template () || process_suffix () || finalize_imms ()) {
        goto skip;
    }
    
    if (instruction.template.opcode_modifier & ADD_FWAIT) {
    
        if (!add_prefix (FWAIT_OPCODE)) {
            goto skip;
        }
    
    }
    
    if (cpu_level < 3) {
    
        if (instruction.suffix == DWORD_SUFFIX || instruction.template.minimum_cpu > cpu_level) {
        
            report (REPORT_ERROR, "no instruction for cpu level %d", cpu_level);
            goto skip;
        
        }
    
    }
    
    if (instruction.template.operand_types[0] & IMPLICIT_REGISTER) {
        instruction.reg_operands--;
    }
    
    if (instruction.template.operand_types[1] & IMPLICIT_REGISTER) {
        instruction.reg_operands--;
    }
    
    if (instruction.operands) {
    
        if (process_operands ()) {
            goto skip;
        }
    
    }
    
    if (instruction.template.opcode_modifier & JUMP) {
        output_jump ();
    } else if (instruction.template.opcode_modifier & (CALL | JUMPBYTE)) {
        output_call_or_jumpbyte ();
    } else if (instruction.template.opcode_modifier & JUMPINTERSEGMENT) {
        output_intersegment_jump ();
    } else {
    
        uint32_t i;

        for (i = 0; i < ARRAY_SIZE (instruction.prefixes); i++) {
        
            if (instruction.prefixes[i]) {
                frag_append_1_char (instruction.prefixes[i]);
            }
        
        }
        
        /*if (instruction.template.base_opcode == 0xff && instruction.template.extension_opcode < 6 && state->model >= 4) {
            frag_append_1_char (0x2E);
        }*/
        
        if (instruction.template.base_opcode & 0xff00) {
            frag_append_1_char ((instruction.template.base_opcode >> 8) & 0xff);
        }
        
        frag_append_1_char (instruction.template.base_opcode & 0xff);
        
        if (instruction.template.opcode_modifier & MODRM) {
        
            frag_append_1_char (((instruction.modrm.regmem << 0) | (instruction.modrm.reg << 3) | (instruction.modrm.mode << 6)));
            
            if ((instruction.modrm.regmem == MODRM_REGMEM_TWO_BYTE_ADDRESSING) && (instruction.modrm.mode != 3) && !(instruction.base_reg && (instruction.base_reg->type & REG16))) {
                frag_append_1_char (((instruction.sib.base << 0) | (instruction.sib.index << 3) | (instruction.sib.scale << 6)));
            }
        
        }
        
        output_disps ();
        output_imms ();
    
    }
    
    return line;
    
skip:
    
    while (*line != '\0') {
    
        if (line[0] == '#') {
            break;
        }
        
        ++(line);
    
    }
    
    return line;

}

int machine_dependent_force_relocation_local (struct fixup *fixup) {
    return fixup->pcrel == 0;
}

int machine_dependent_get_bits (void) {
    return bits;
}

int machine_dependent_get_cpu (void) {
    return cpu_level;
}

int machine_dependent_is_register (const char *p) {

    struct hashtab_name *key;
    
    char *new_p = to_lower (p);
    int is_reg = 0;
    
    if (new_p == NULL) {
        return is_reg;
    }
    
    if ((key = hashtab_alloc_name (new_p)) == NULL) {
    
        free (new_p);
        return is_reg;
    
    }
    
    is_reg = (hashtab_get (&reg_entry_hashtab, key) != NULL);
    
    free (key);
    free (new_p);
    
    return is_reg;

}

int machine_dependent_need_index_operator (void) {
    return intel_syntax < 0;
}

int machine_dependent_parse_name (char **pp, struct expr *expr, char *name, char *original_saved_c) {

    const struct reg_entry *reg;
    char *orig_end;
    
    orig_end = *pp;
    **pp = *original_saved_c;
    
    reg = parse_register (name, pp);
    
    if (reg && orig_end <= *pp) {
    
        *original_saved_c = **pp;
        **pp = '\0';

        if (reg != &bad_register) {
            
            expr->type = EXPR_TYPE_REGISTER;
            expr->add_number = reg - reg_table;

        } else {
            expr->type = EXPR_TYPE_INVALID;
        }
        
        return 1;
    
    }
    
    *pp = orig_end;
    **pp = '\0';
    
    return intel_syntax ? intel_parse_name (expr, name) : 0;

}

long machine_dependent_estimate_size_before_relax (struct frag *frag, section_t section) {

    if (symbol_get_section (frag->symbol) != section) {
    
        int size = (frag->relax_subtype & RELAX_SUBTYPE_CODE16_JUMP) ? 2 : 4;
        
        unsigned char *opcode_pos = frag->buf + frag->opcode_offset_in_buf;
        unsigned long old_frag_fixed_size = frag->fixed_size;
        
        switch (TYPE_FROM_RELAX_SUBTYPE (frag->relax_subtype)) {
        
            case RELAX_SUBTYPE_UNCONDITIONAL_JUMP:
            
                *opcode_pos = 0xE9;
                
                fixup_new (frag, frag->fixed_size, size, frag->symbol, frag->offset, 1, RELOC_TYPE_DEFAULT, 0);
                frag->fixed_size += size;
                
                break;
            
            case RELAX_SUBTYPE_CONDITIONAL_JUMP86:
            
                if (size == 2) {

                    /* Negates the condition and jumps past unconditional jump. */
                    opcode_pos[0] ^= 1;
                    opcode_pos[1] = 3;
                    
                    /* Inserts the unconditional jump. */
                    opcode_pos[2] = 0xE9;
                    
                    frag->fixed_size += 4;
                    fixup_new (frag, old_frag_fixed_size + 2, size, frag->symbol, frag->offset, 1, RELOC_TYPE_DEFAULT, 0);
                    
                    break;
                
                }
                
                /* fall through. */
            
            case RELAX_SUBTYPE_CONDITIONAL_JUMP:
                
                opcode_pos[1] = opcode_pos[0] + 0x10;
                opcode_pos[0] = TWOBYTE_OPCODE;
                
                fixup_new (frag, frag->fixed_size + 1, size, frag->symbol, frag->offset, 1, RELOC_TYPE_DEFAULT, 0);
                frag->fixed_size += size + 1;
                
                break;
            
            case RELAX_SUBTYPE_FORCED_SHORT_JUMP:
            
                if (strcmp (state->format, "coff") == 0) {
                
                    report_at (frag->filename, frag->line_number, REPORT_FATAL_ERROR, "forced short jump with target in different section (origin section %s and target section %s) is not supported for COFF", section_get_name (section), section_get_name (symbol_get_section (frag->symbol)));
                    exit (EXIT_FAILURE);
                
                }
                
                size = 1;
                
                fixup_new (frag, frag->fixed_size, size, frag->symbol, frag->offset, 1, RELOC_TYPE_DEFAULT, 0);
                frag->fixed_size += size;
                
                break;
            
            default:
            
                report_at (__FILE__, __LINE__, REPORT_INTERNAL_ERROR, "machine_dependent_estimate_size_before_relax invalid case");
                return 0;
        
        }
        
        frag->relax_type = RELAX_TYPE_NONE_NEEDED;
        return frag->fixed_size - old_frag_fixed_size;
    
    }
    
    return relax_table[frag->relax_subtype].size_of_variable_part;

}

long machine_dependent_pcrel_from (struct fixup *fixup) {
    return (fixup->size + fixup->where + fixup->frag->address);
}

long machine_dependent_relax_frag (struct frag *frag, section_t section, long change) {

    unsigned long target;
    
    long aim, growth;
    relax_subtype_t new_subtype;
    
    target = frag->offset;
    
    if (frag->symbol) {
    
        target += symbol_get_value (frag->symbol);
        
        if ((section == symbol_get_section (frag->symbol)) && (frag->relax_marker != frag->symbol->frag->relax_marker)) {
            target += change;
        }
    
    }
    
    aim = target - frag->address - frag->fixed_size;
    
    if (aim > 0) {
    
        for (new_subtype = frag->relax_subtype; relax_table[new_subtype].next_subtype; new_subtype = relax_table[new_subtype].next_subtype) {
        
            if (aim <= relax_table[new_subtype].forward_reach) {
                break;
            }
        
        }
    
    } else if (aim < 0) {
    
        for (new_subtype = frag->relax_subtype; relax_table[new_subtype].next_subtype; new_subtype = relax_table[new_subtype].next_subtype) {
        
            if (aim >= relax_table[new_subtype].backward_reach) {
                break;
            }
        
        }
    
    } else {
        return 0;
    }
    
    growth  = relax_table[new_subtype].size_of_variable_part;
    growth -= relax_table[frag->relax_subtype].size_of_variable_part;
    
    if (growth) { frag->relax_subtype = new_subtype; }
    return growth;

}

void machine_dependent_apply_fixup (struct fixup *fixup, unsigned long value) {

    unsigned char *p = fixup->frag->buf + fixup->where;
    
    if (fixup->add_symbol == NULL) {
        fixup->done = 1;
    }
    
    if (strcmp (state->format, "coff") == 0 && fixup->pcrel && fixup->add_symbol && symbol_is_undefined (fixup->add_symbol)) {
        value += machine_dependent_pcrel_from (fixup);
    }
    
    if (*(p - 1) == 0x9A) {
    
        if (fixup->far_call && fixup->add_symbol == NULL) {
        
            /*if ((long) value >= 32767) {*/
            if ((long) value >= 65535) {
            
                value--;
                
                machine_dependent_number_to_chars (p, value % 16, 2);
                machine_dependent_number_to_chars (p + 2, value / 16, 2);
            
            } else {
            
                value -= (fixup->where + fixup->frag->address);
                value -= fixup->size;
                
                machine_dependent_number_to_chars (p - 1, 0x0E, 1);
                machine_dependent_number_to_chars (p + 1, value + 1, 2);
                
                machine_dependent_number_to_chars (p, 0xE8, 1);
                machine_dependent_number_to_chars (p + 3, 0x90, 1);
            
            }
        
        } else {
            machine_dependent_number_to_chars (p, 0, fixup->size);
        }
    
    } else {
        machine_dependent_number_to_chars (p, value, fixup->size);
    }

}

void machine_dependent_finish_frag (struct frag *frag) {

    unsigned char *opcode_pos;
    
    unsigned char *displacement_pos;
    long displacement;
    
    int size;
    unsigned long extension = 0;
    
    opcode_pos = frag->buf + frag->opcode_offset_in_buf;
    
    displacement_pos = opcode_pos + 1;
    displacement = (symbol_get_value (frag->symbol) + frag->offset - frag->address - frag->fixed_size);
    
    if ((frag->relax_subtype & RELAX_SUBTYPE_LONG_JUMP) == 0) {
    
        displacement_pos = opcode_pos + 1;
        extension = relax_table[frag->relax_subtype].size_of_variable_part;
        
        if (RELAX_SUBTYPE_FORCED_SHORT_JUMP) {
        
            if (displacement > relax_table[frag->relax_subtype].forward_reach || displacement < relax_table[frag->relax_subtype].backward_reach) {
                report_at (frag->filename, frag->line_number, REPORT_ERROR, "forced short jump out of range");
            }
        
        }
    
    } else {
    
        switch (frag->relax_subtype) {
        
            case ENCODE_RELAX_SUBTYPE (RELAX_SUBTYPE_UNCONDITIONAL_JUMP, RELAX_SUBTYPE_LONG_JUMP):
            case ENCODE_RELAX_SUBTYPE (RELAX_SUBTYPE_UNCONDITIONAL_JUMP, RELAX_SUBTYPE_LONG16_JUMP):
            
                extension = relax_table[frag->relax_subtype].size_of_variable_part;
                opcode_pos[0] = 0xE9;
                
                displacement_pos = opcode_pos + 1;
                break;

            case ENCODE_RELAX_SUBTYPE (RELAX_SUBTYPE_CONDITIONAL_JUMP, RELAX_SUBTYPE_LONG_JUMP):
            case ENCODE_RELAX_SUBTYPE (RELAX_SUBTYPE_CONDITIONAL_JUMP86, RELAX_SUBTYPE_LONG_JUMP):
            case ENCODE_RELAX_SUBTYPE (RELAX_SUBTYPE_CONDITIONAL_JUMP, RELAX_SUBTYPE_LONG16_JUMP):
            
                extension = relax_table[frag->relax_subtype].size_of_variable_part;
                
                opcode_pos[1] = opcode_pos[0] + 0x10;
                opcode_pos[0] = TWOBYTE_OPCODE;
                
                displacement_pos = opcode_pos + 2;
                break;
            
            case ENCODE_RELAX_SUBTYPE (RELAX_SUBTYPE_CONDITIONAL_JUMP86, RELAX_SUBTYPE_LONG16_JUMP):
            
                extension = relax_table[frag->relax_subtype].size_of_variable_part;
                
                /* Negates the condition and jumps past unconditional jump. */
                opcode_pos[0] ^= 1;
                opcode_pos[1] = 3;
            
                /* Inserts the unconditional jump. */
                opcode_pos[2] = 0xE9;
                
                displacement_pos = opcode_pos + 3;
                break;
            
        } 
    
    }
    
    size = DISPLACEMENT_SIZE_FROM_RELAX_SUBSTATE (frag->relax_subtype);
    displacement -= extension;

    machine_dependent_number_to_chars (displacement_pos, displacement, size);
    frag->fixed_size += extension;

}

void machine_dependent_init (void) {

    struct template *template;
    struct reg_entry *reg_entry;
    struct pseudo_op *poe;
    
    struct hashtab_name *key;
    struct templates *templates;
    
    int c;
    
    template = template_table;
    
    templates = xmalloc (sizeof (*templates));
    templates->name = template->name;
    templates->start = template;
    
    while (1) {
    
        ++(template);
        
        if (template->name == NULL || strcmp (template->name, (template - 1)->name)) {
        
            templates->end = template;
            
            if ((key = hashtab_alloc_name (templates->name)) == NULL) {
                continue;
            }
            
            if (hashtab_get (&templates_hashtab, key) != NULL) {
            
                report_at (program_name, 0, REPORT_INTERNAL_ERROR, "template %s already exists", templates->name);
                exit (EXIT_FAILURE);
            
            } else if (hashtab_put (&templates_hashtab, key, templates) < 0) {
            
                report_at (program_name, 0, REPORT_INTERNAL_ERROR, "failed to insert %s into templates_hashtab", templates->name);
                free (key);
                
                return;
            
            }
            
            if (template->name == NULL) {
                break;
            }
            
            templates = xmalloc (sizeof (*templates));
            templates->name = template->name;
            templates->start = template;
        
        }
    
    }
    
    for (poe = pseudo_ops; poe->name; ++poe) {
        add_pseudo_op (poe);
    }
    
    for (reg_entry = reg_table; reg_entry->name; ++reg_entry) {
    
        if (reg_entry->type & FLOAT_REG) {
        
            if (!(reg_entry->type & FLOAT_ACC)) {
                continue;
            }
            
            reg_st0 = reg_entry;
        
        }
        
        if ((reg_entry->type & REG32) && reg_entry->number == 4) {
            reg_esp = reg_entry;
        }
        
        if ((key = hashtab_alloc_name (reg_entry->name)) == NULL) {
            continue;
        }
        
        if (hashtab_get (&reg_entry_hashtab, key) != NULL) {
        
            report_at (program_name, 0, REPORT_INTERNAL_ERROR, "register (%s) already exists", reg_entry->name);
            exit (EXIT_FAILURE);
        
        }
        
        if (hashtab_put (&reg_entry_hashtab, key, reg_entry) < 0) {
        
            free (key);
            continue;
        
        }
    
    }
    
    /* Fills lexical table. */
    for (c = 0; c < 256; c++) {
    
        if (islower (c) || isdigit (c)) {
            register_chars_table[c] = c;
        } else if (isupper (c)) {
            register_chars_table[c] = tolower (c);
        }
    
    }
    
    expr_type_set_rank (EXPR_TYPE_FULL_PTR, intel_syntax ? 10 : 0);

}

void machine_dependent_parse_operand (char **pp, struct expr *expr) {

    const struct reg_entry *reg;
    char *end;
    
    switch (**pp) {
    
        case REGISTER_PREFIX:
        
            reg = parse_register (*pp, &end);
            
            if (reg) {
            
                expr->type = EXPR_TYPE_REGISTER;
                expr->add_number = reg - reg_table;
                
                *pp = end;
            
            }
            
            break;
        
        case '[':
        
            end = (*pp)++;
            expression_read_into (pp, expr);
            
            if (**pp == ']' && expr->type != EXPR_TYPE_INVALID) {
            
                ++*pp;
                
                if (expr->type == EXPR_TYPE_SYMBOL && expr->add_number == 0) {
                
                    /* Forces make_expr_symbol to create an expression symbol instead of returning the used symbol. */
                    expr->add_number = 1;
                    expr->op_symbol = make_expr_symbol (expr);
                    
                    symbol_get_value_expression (expr->op_symbol)->add_number = 0;
                
                } else {
                    expr->op_symbol = make_expr_symbol (expr);
                }
                
                expr->add_symbol = NULL;
                expr->add_number = 0;
                expr->type = EXPR_TYPE_INDEX;
            
            } else {
            
                expr->type = EXPR_TYPE_ABSENT;
                *pp = end;
            
            }
            
            break;
    
    }

}

void machine_dependent_set_bits (int new_bits) {

    if (new_bits != 16 && new_bits != 32) {
        report_at (NULL, 0, REPORT_ERROR, "bits must either be 16 or 32");
    } else {
        bits = new_bits;
    }

}

void machine_dependent_set_cpu (int level) {

    if (level < 0 || level > 6) {
        report_at (NULL, 0, REPORT_ERROR, "invalid cpu level provided");
    } else {
        cpu_level = level;
    }

}
