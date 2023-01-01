/******************************************************************************
 * @file            frag.h
 *****************************************************************************/
#ifndef     _FRAG_H
#define     _FRAG_H

#include    "types.h"

struct frag {

    address_t address;
    value_t fixed_size, size;
    
    unsigned char *buf;
    
    relax_type_t relax_type;
    relax_subtype_t relax_subtype;
    
    symbol_t symbol;
    
    offset_t offset;
    value_t opcode_offset_in_buf;
    
    const char *filename;
    unsigned long line_number;
    
    int relax_marker, far_call, symbol_seen;
    frag_t next;

};

#define     FRAG_BUF_REALLOC_STEP       16

extern struct frag zero_address_frag;
extern frag_t current_frag;

struct frag *frag_alloc (void);

int frags_offset_is_fixed (const struct frag *frag1, const struct frag *frag2, offset_t *offset_p);
int frags_is_greater_than_offset (value_t offset2, const struct frag *frag2, value_t offset1, const struct frag *frag1, offset_t *offset_p);

unsigned char *frag_alloc_space (value_t space);
unsigned char *frag_increase_fixed_size (value_t increase);
unsigned char *finished_frag_increase_fixed_size_by_frag_offset (struct frag *frag);

void frag_align (offset_t alignment, int fill_char, offset_t max_bytes_to_skip);
void frag_align_code (offset_t alignment, offset_t max_bytes_to_skip);
void frag_append_1_char (unsigned char ch);
void frag_new (void);
void frag_set_as_variant (relax_type_t relax_type, relax_subtype_t relax_subtype, struct symbol *symbol, offset_t offset, value_t opcode_offset_in_buf, int far_call);

#endif      /* _FRAG_H */
