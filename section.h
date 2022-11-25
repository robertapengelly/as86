/******************************************************************************
 * @file            section.h
 *****************************************************************************/
#ifndef     _SECTION_H
#define     _SECTION_H

#include    <stdint.h>

#include    "fixup.h"
#include    "types.h"

struct frag_chain {

    frag_t first_frag, last_frag;
    struct fixup *first_fixup, *last_fixup;
    
    subsection_t subsection;
    struct frag_chain *next;

};

extern struct frag_chain *current_frag_chain;

extern section_t undefined_section;
extern section_t absolute_section;
extern section_t expr_section;
extern section_t reg_section;
extern section_t text_section;
extern section_t data_section;
extern section_t bss_section;

extern section_t sections;

extern section_t current_section;
extern subsection_t current_subsection;

#define     SECTION_IS_NORMAL(section)  \
    ((section != undefined_section) && (section != absolute_section) && (section != expr_section) && (section != reg_section))

section_t section_get_next_section (section_t section);
section_t section_set (section_t section);
section_t section_set_by_name (const char *name);

section_t section_subsection_set (section_t section, subsection_t subsection);
section_t section_subsection_set_by_name (const char *name, subsection_t subsection);

symbol_t section_symbol (section_t section);
const char *section_get_name (section_t section);

int section_find_by_name (const char *name);

uint32_t sections_get_count (void);
uint32_t section_get_number (section_t section);

void sections_chain_subsection_frags (void);
void sections_init (void);
void sections_number (uint32_t start_at);


void *section_get_object_format_dependent_data (section_t section);
void section_set_object_format_dependent_data (section_t section, void *data);

#endif      /* _SECTION_H */
