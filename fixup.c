/******************************************************************************
 * @file            fixup.c
 *****************************************************************************/
#include    <stddef.h>

#include    "as.h"
#include    "expr.h"
#include    "fixup.h"
#include    "frag.h"
#include    "lib.h"
#include    "report.h"
#include    "section.h"
#include    "symbol.h"
#include    "types.h"

static struct fixup *fixup_new_internal (struct frag *frag, unsigned long where, int32_t size, struct symbol *add_symbol, long add_number, int pcrel, reloc_type_t reloc_type, int far_call) {

    struct fixup *fixup = xmalloc (sizeof (*fixup));
    
    fixup->frag         = frag;
    fixup->where        = where;
    fixup->size         = size;
    fixup->add_symbol   = add_symbol;
    fixup->add_number   = add_number;
    fixup->pcrel        = pcrel;
    fixup->reloc_type   = reloc_type;
    fixup->next         = NULL;
    fixup->far_call     = far_call;
    
    if (current_frag_chain->last_fixup) {
    
        current_frag_chain->last_fixup->next = fixup;
        current_frag_chain->last_fixup = fixup;
    
    } else {
        current_frag_chain->last_fixup = current_frag_chain->first_fixup = fixup;
    }
    
    return fixup;

}

struct fixup *fixup_new (struct frag *frag, unsigned long where, int32_t size, struct symbol *add_symbol, long add_number, int pcrel, reloc_type_t reloc_type, int far_call) {
    return fixup_new_internal (frag, where, size, add_symbol, add_number, pcrel, reloc_type, far_call);
}

struct fixup *fixup_new_expr (struct frag *frag, unsigned long where, int32_t size, struct expr *expr, int pcrel, reloc_type_t reloc_type, int far_call) {

    struct symbol *add_symbol = NULL;
    offset_t add_number = 0;
    
    switch (expr->type) {
    
        case EXPR_TYPE_ABSENT:
        
            break;
        
        case EXPR_TYPE_CONSTANT:
        
            add_number = expr->add_number;
            break;
        
        case EXPR_TYPE_SYMBOL:
        
            add_symbol = expr->add_symbol;
            add_number = expr->add_number;
            
            break;
        
        case EXPR_TYPE_SYMBOL_RVA:
        
            add_symbol = expr->add_symbol;
            add_number = expr->add_number;
            
            reloc_type = RELOC_TYPE_RVA;
            break;
        
        default:
        
            add_symbol = make_expr_symbol (expr);
            break;
        
    }
    
    return fixup_new_internal (frag, where, size, add_symbol, add_number, pcrel, reloc_type, far_call);

}
