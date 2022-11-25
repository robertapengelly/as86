/******************************************************************************
 * @file            types.h
 *****************************************************************************/
#ifndef     _TYPE_H
#define     _TYPE_H

#include    <limits.h>

typedef     signed char                 int8_t;
typedef     unsigned char               uint8_t;

typedef     signed short                int16_t;
typedef     unsigned short              uint16_t;

#if     INT_MAX == 32767
typedef     signed long                 int32_t;
typedef     unsigned long               uint32_t;
#else
typedef     signed int                  int32_t;
typedef     unsigned int                uint32_t;
#endif

typedef     signed long                 int64_t;
typedef     unsigned long               uint64_t;


typedef     struct section              *section_t;
typedef     signed long                 subsection_t;

typedef enum {

    RELAX_TYPE_NONE_NEEDED = 0,
    RELAX_TYPE_ALIGN,
    RELAX_TYPE_ALIGN_CODE,
    RELAX_TYPE_CALL,
    RELAX_TYPE_ORG,
    RELAX_TYPE_SPACE,
    RELAX_TYPE_MACHINE_DEPENDENT

} relax_type_t;

typedef     uint32_t                relax_subtype_t;

typedef enum {

    RELOC_TYPE_DEFAULT,
    RELOC_TYPE_RVA

} reloc_type_t;

typedef     unsigned long               address_t;

typedef     signed long                 offset_t;
typedef     unsigned long               value_t;

typedef     struct expr                 *expr_t;
typedef     struct fixup                *fixup_t;
typedef     struct frag                 *frag_t;
typedef     struct symbol               *symbol_t;

#endif      /* _TYPE_H */
