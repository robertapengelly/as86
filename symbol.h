/******************************************************************************
 * @file            symbol.h
 *****************************************************************************/
#ifndef     _SYMBOL_H
#define     _SYMBOL_H

#include    "expr.h"
#include    "types.h"


#define symbol_get_value_expression symgve
#define symbol_create symcreate
#define symbol_find symfind
#define symbol_find_or_make symformake
#define symbol_label symlab
#define symbol_make symmake
#define symbol_temp_new_now symtnn
#define symbol_get_frag symgfrag
#define symbol_get_section symgsec
#define symbol_get_value symgval
#define symbol_resolve_value symresval
#define symbol_get_name symgname
#define symbol_force_reloc symfreloc
#define symbol_is_external symisext
#define symbol_is_resolved symisres
#define symbol_is_section_symbol symisss
#define symbol_is_undefined symisund
#define symbol_uses_other_symbol symuos
#define symbol_uses_reloc_symbol symurelsym
#define symbol_get_symbol_table_index symgsti
#define symbol_add_to_chain symatoc
#define symbol_set_external symsext
#define symbol_set_frag symsfrag
#define symbol_set_section symssec
#define symbol_set_size symssiz
#define symbol_set_symbol_table_index symssti
#define symbol_set_value symsval
#define symbol_set_value_expression symsvexp
#define get_symbol_snapshot gsymsnap


struct symbol {

    char *name;
    
    struct expr value;
    section_t section;
    
    frag_t frag;
    unsigned long flags;
    
#define     SYMBOL_FLAG_EXTERNAL                            0x01
#define     SYMBOL_FLAG_SECTION_SYMBOL                      0x02
    
    struct symbol *next;
    
    unsigned long symbol_table_index;
    int resolved, resolving, size, write_name_to_string_table;
    
    void *object_format_dependent_data;

};

#define     FAKE_LABEL_NAME             "FAKE_PDAS_SYMBOL"

extern struct symbol *symbols;
extern int finalize_symbols;

struct expr *symbol_get_value_expression (struct symbol *symbol);

struct symbol *symbol_create (const char *name, section_t section, unsigned long value, frag_t frag);
struct symbol *symbol_find (const char *name);
struct symbol *symbol_find_or_make (const char *name);
struct symbol *symbol_label (const char *name);
struct symbol *symbol_make (const char *name);
struct symbol *symbol_temp_new_now (void);

frag_t symbol_get_frag (struct symbol *symbol);
section_t symbol_get_section (struct symbol *symbol);

value_t symbol_get_value (struct symbol *symbol);
value_t symbol_resolve_value (struct symbol *symbol);

char *symbol_get_name (struct symbol *symbol);

int get_symbol_snapshot (struct symbol **symbol_p, value_t *value_p, section_t *section_p, frag_t *frag_p);
int symbol_force_reloc (struct symbol *symbol);
int symbol_is_external (struct symbol *symbol);
int symbol_is_resolved (struct symbol *symbol);
int symbol_is_section_symbol (struct symbol *symbol);
int symbol_is_undefined (struct symbol *symbol);
int symbol_uses_other_symbol (struct symbol *symbol);
int symbol_uses_reloc_symbol (struct symbol *symbol);

unsigned long symbol_get_symbol_table_index (struct symbol *symbol);

void symbol_add_to_chain (struct symbol *symbol);
void symbol_set_external (struct symbol *symbol);
void symbol_set_frag (struct symbol *symbol, frag_t frag);
void symbol_set_section (struct symbol *symbol, section_t section);
void symbol_set_size (int size);
void symbol_set_symbol_table_index (struct symbol *symbol, unsigned long index);
void symbol_set_value (struct symbol *symbol, value_t value);
void symbol_set_value_expression (struct symbol *symbol, struct expr *expr);

#endif      /* _SYMBOL_H */
