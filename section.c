/******************************************************************************
 * @file            section.c
 *****************************************************************************/
#include    <stdlib.h>
#include    <string.h>

#include    "frag.h"
#include    "lib.h"
#include    "report.h"
#include    "section.h"
#include    "symbol.h"
#include    "types.h"

struct section {

    char *name;
    
    struct symbol *symbol;
    struct frag_chain *frag_chain;
    
    unsigned int flags;
    section_t next;
    
    void *object_format_dependent_data;
    uint32_t number;

};

static struct section internal_sections[4];
static struct symbol section_symbols[4];

section_t undefined_section;
section_t absolute_section;
section_t expr_section;
section_t reg_section;
section_t text_section;
section_t data_section;
section_t bss_section;

section_t sections = 0;

section_t current_section;
subsection_t current_subsection;

static int frags_chained = 0;
struct frag_chain *current_frag_chain;

static section_t find_or_make_section_by_name (const char *name) {

    section_t section, *p_next;
    
    for (p_next = &sections, section = sections; section; p_next = &(section->next), section = *p_next) {
        
        if (strcmp (name, section->name) == 0) {
            break;
        }
    
    }
    
    if (section == NULL) {
    
        section = xmalloc (sizeof (*section));
        section->name = xstrdup (name);
        
        section->symbol = symbol_create (name, section, 0, &zero_address_frag);
        section->symbol->flags |= SYMBOL_FLAG_SECTION_SYMBOL;
        symbol_add_to_chain (section->symbol);
        
        section->frag_chain = NULL;
        section->next = NULL;
        
        section->object_format_dependent_data = NULL;
        section->number = 0;
        
        *p_next = section;
    
    }
    
    return section;

}

static void subsection_set (subsection_t subsection) {

    struct frag_chain *frag_chain, **p_next;
    
    if (frags_chained && subsection) {
    
        report_at (__FILE__, __LINE__, REPORT_INTERNAL_ERROR,
            "frags have been already chained, switching to subsection %li is invalid", subsection);
        exit (EXIT_FAILURE);
    
    }
    
    for (frag_chain = *(p_next = &(current_section->frag_chain)); frag_chain; frag_chain = *(p_next = &(frag_chain->next))) {
    
        if (frag_chain->subsection >= subsection) {
            break;
        }
    
    }
    
    if (!frag_chain || frag_chain->subsection != subsection) {
    
        struct frag_chain *new_frag_chain = xmalloc (sizeof (*new_frag_chain));
        
        new_frag_chain->last_frag  = new_frag_chain->first_frag  = frag_alloc ();
        new_frag_chain->last_fixup = new_frag_chain->first_fixup = NULL;
        new_frag_chain->subsection = subsection;
        
        *p_next = new_frag_chain;
        new_frag_chain->next = frag_chain;
        
        frag_chain = new_frag_chain;
    
    }
    
    current_frag_chain = frag_chain;
    current_frag       = current_frag_chain->last_frag;
    
    current_subsection = subsection;

}

section_t section_get_next_section (section_t section) {
    return section->next;
}

section_t section_set (section_t section) {
    return section_subsection_set (section, 0);
}

section_t section_set_by_name (const char *name) {
    return section_subsection_set_by_name (name, 0);
}

section_t section_subsection_set (section_t section, subsection_t subsection) {

    current_section = section;
    subsection_set (subsection);
    
    return section;

}

section_t section_subsection_set_by_name (const char *name, subsection_t subsection) {
    return section_subsection_set (find_or_make_section_by_name (name), subsection);
}

symbol_t section_symbol (section_t section) {
    return section->symbol;
}

const char *section_get_name (section_t section) {
    return section->name;
}

int section_find_by_name (const char *name) {

    section_t section, *p_next;
    
    for (p_next = &sections, section = sections; section; p_next = &(section->next), section = *p_next) {
        
        if (strcmp (name, section->name) == 0) {
            break;
        }
    
    }
    
    return (section == NULL);

}

uint32_t sections_get_count (void) {

    section_t section;
    uint32_t count;

    for (section = sections, count = 0; section; section = section->next, count++);
    return count;

}

uint32_t section_get_number (section_t section) {
    return section->number;
}

void sections_chain_subsection_frags (void) {

    section_t section;
    
    for (section = sections; section; section = section->next) {
    
        struct frag_chain *frag_chain;
        
        section->frag_chain->subsection = 0;
        
        for (frag_chain = section->frag_chain->next; frag_chain; frag_chain = frag_chain->next) {
        
            section->frag_chain->last_frag->next = frag_chain->first_frag;
            section->frag_chain->last_frag = frag_chain->last_frag;
            
            if (section->frag_chain->first_fixup) {
            
                section->frag_chain->last_fixup->next = frag_chain->first_fixup;
                section->frag_chain->last_fixup = frag_chain->last_fixup;
            
            } else {
            
                section->frag_chain->first_fixup = frag_chain->first_fixup;
                section->frag_chain->last_fixup = frag_chain->last_fixup;
            
            }
        
        }
    
    }
    
    frags_chained = 1;

}

void sections_init (void) {

#define CREATE_INTERNAL_SECTION(section_var, section_name, section_index) \
    (section_var) = &internal_sections[(section_index)]; \
    (section_var)->name = (section_name); \
    (section_var)->symbol = &section_symbols[(section_index)]; \
    (section_var)->symbol->name    = (section_name); \
    (section_var)->symbol->section = (section_var); \
    (section_var)->symbol->frag    = &zero_address_frag; \
    symbol_set_value ((section_var)->symbol, 0); \
    (section_var)->symbol->flags  |= SYMBOL_FLAG_SECTION_SYMBOL

    CREATE_INTERNAL_SECTION (undefined_section, "*UND*",  0);
    CREATE_INTERNAL_SECTION (absolute_section,  "*ABS*",  1);
    CREATE_INTERNAL_SECTION (expr_section,      "*EXPR*", 2);
    CREATE_INTERNAL_SECTION (reg_section,       "*REG*",  3);

#undef CREATE_INTERNAL_SECTION
    
    text_section      = section_set_by_name (".text");
    section_set_flags (text_section, SECTION_FLAG_LOAD | SECTION_FLAG_ALLOC | SECTION_FLAG_READONLY | SECTION_FLAG_CODE);
    
    data_section      = section_set_by_name (".data");
    section_set_flags (data_section, SECTION_FLAG_LOAD | SECTION_FLAG_ALLOC | SECTION_FLAG_DATA);
    
    bss_section       = section_set_by_name (".bss");
    section_set_flags (bss_section, SECTION_FLAG_ALLOC);
    
    /* .text section is the default section. */
    section_set (text_section);

}

void sections_number (uint32_t start_at) {

    section_t section;
    
    for (section = sections; section; section = section->next) {
        section->number = start_at++;
    }

}

void *section_get_object_format_dependent_data (section_t section) {
    return section->object_format_dependent_data;
}

void section_set_object_format_dependent_data (section_t section, void *data) {
    section->object_format_dependent_data = data;
}

void section_set_flags (section_t section, unsigned int flags) {
    section->flags = flags;
}

unsigned int section_get_flags (section_t section) {
    return section->flags;
}
