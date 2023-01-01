/******************************************************************************
 * @file            fixup.h
 *****************************************************************************/
#ifndef     _FIXUP_H
#define     _FIXUP_H

#include    "types.h"

struct fixup {

    frag_t frag;
    
    unsigned long where;
    uint32_t size;
    
    symbol_t add_symbol;
    long add_number;
    
    int32_t pcrel;
    int done;
    
    reloc_type_t reloc_type;
    struct fixup *next;
    
    int far_call;

};

struct fixup *fixup_new (struct frag *frag, unsigned long where, int32_t size, struct symbol *add_symbol, long add_number, int pcrel, reloc_type_t reloc_type, int far_call);
struct fixup *fixup_new_expr (frag_t frag, unsigned long where, int32_t size, expr_t expr, int pcrel, reloc_type_t reloc_type, int far_call);

#endif      /* _FIXUP_H */
