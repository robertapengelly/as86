/******************************************************************************
 * @file            types.h
 *****************************************************************************/
#ifndef     _TYPE_H
#define     _TYPE_H

#include    "stdint.h"

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
    RELOC_TYPE_CALL,
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
