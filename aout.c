/******************************************************************************
 * @file            aout.c
 *****************************************************************************/
#include    <stddef.h>
#include    <stdio.h>
#include    <stdlib.h>
#include    <string.h>

#include    "aout.h"
#include    "as.h"
#include    "frag.h"
#include    "pseudo_ops.h"
#include    "report.h"
#include    "section.h"
#include    "symbol.h"

void aout_adjust_code (void) {

    struct frag *frag;
    unsigned long address, text_section_size;
    
    section_set (text_section);
    
    text_section_size = current_frag_chain->last_frag->address + current_frag_chain->last_frag->fixed_size;
    address = text_section_size;
    
    section_set (data_section);
    
    for (frag = current_frag_chain->first_frag; frag; frag = frag->next) {
    
        frag->address = address;
        address += frag->fixed_size;
    
    }
    
    section_set (bss_section);
    
    for (frag = current_frag_chain->first_frag; frag; frag = frag->next) {
    
        frag->address = address;
        address += frag->fixed_size;
    
    }

}

void aout_write_object (void) {

    struct exec header;
    struct frag *frag;
    
    struct fixup *fixup;
    unsigned long start_address_of_data;
    
    struct symbol *symbol;
    unsigned long symbol_table_size;
    
    FILE *outfile;
    unsigned int string_table_pos = 4;
    
    memset (&header, 0, sizeof (header));
    header.a_info = 0x00640000 | OMAGIC;
    
    if ((outfile = fopen (state->outfile, "wb")) == NULL) {
    
        report_at (NULL, 0, REPORT_ERROR, "Failed to open '%s' as output file", state->outfile);
        return;
    
    }
    
    if (fseek (outfile, sizeof (header), SEEK_SET)) {
    
        report_at (NULL, 0, REPORT_ERROR, "Failed whilst seeking passed header");
        return;
    
    }
    
    section_set (text_section);
    header.a_text = 0;
    
    for (frag = current_frag_chain->first_frag; frag; frag = frag->next) {
    
        if (frag->fixed_size == 0) {
            continue;
        }
        
        if (fwrite (frag->buf, frag->fixed_size, 1, outfile) != 1) {
        
            report_at (NULL, 0, REPORT_ERROR, "Failed whilst writing text!");
            return;
        
        }
        
        header.a_text += frag->fixed_size;
    
    }
    
    section_set (data_section);
    header.a_data = 0;
    
    for (frag = current_frag_chain->first_frag; frag; frag = frag->next) {
    
        if (frag->fixed_size == 0) {
            continue;
        }
        
        if (fwrite (frag->buf, frag->fixed_size, 1, outfile) != 1) {
        
            report_at (NULL, 0, REPORT_ERROR, "Failed whilst writing data!");
            return;
        
        }
        
        header.a_data += frag->fixed_size;
    
    }
    
    section_set (bss_section);
    header.a_bss = 0;
    
    for (frag = current_frag_chain->first_frag; frag; frag = frag->next) {
    
        if (frag->fixed_size == 0) {
            continue;
        }
        
        header.a_bss += frag->fixed_size;
    
    }
    
    header.a_trsize = 0;
    section_set (text_section);
    
    for (fixup = current_frag_chain->first_fixup; fixup; fixup = fixup->next) {
    
        struct relocation_info reloc;
        
        if (fixup->done) {
            continue;
        }
        
        reloc.r_address = fixup->frag->address + fixup->where;
        
        if (symbol_is_section_symbol (fixup->add_symbol)) {
        
            if (symbol_get_section (fixup->add_symbol) == text_section) {
                reloc.r_symbolnum = N_TEXT;
            } else if (symbol_get_section (fixup->add_symbol) == data_section) {
                reloc.r_symbolnum = N_DATA;
            } else if (symbol_get_section (fixup->add_symbol) == bss_section) {
                reloc.r_symbolnum = N_BSS;
            } else {
                report_at (NULL, 0, REPORT_ERROR, "Undefined section");
            }
        
        } else {
        
            struct symbol *symbol;
            int symbol_number;
            
            for (symbol = symbols, symbol_number = 0; symbol && (symbol != fixup->add_symbol); symbol = symbol->next) {
            
                if (!symbol_is_section_symbol (symbol)) {
                    symbol_number++;
                }
            
            }
            
            reloc.r_symbolnum  = symbol_number;
            reloc.r_symbolnum |= 1 << 27;
        
        }
        
        reloc.r_symbolnum |= (!!(fixup->pcrel)) << 24;
        
        {
        
            int log2_of_size, size;
            
            for (log2_of_size = -1, size = fixup->size; size; size >>= 1, log2_of_size++);
            reloc.r_symbolnum |= log2_of_size << 25;
        
        }
        
        if (fwrite (&reloc, sizeof (reloc), 1, outfile) != 1) {
        
            report_at (NULL, 0, REPORT_ERROR, "Error writing text relocations!");
            return;
        
        }
        
        header.a_trsize += sizeof (reloc);
    
    }
    
    header.a_drsize = 0;
    section_set (data_section);
    
    start_address_of_data = current_frag_chain->first_frag->address;
    
    for (fixup = current_frag_chain->first_fixup; fixup; fixup = fixup->next) {
    
        struct relocation_info reloc;
        
        if (fixup->done) {
            continue;
        }
        
        reloc.r_address = fixup->frag->address + fixup->where - start_address_of_data;
        
        if (symbol_is_section_symbol (fixup->add_symbol)) {
        
            if (symbol_get_section (fixup->add_symbol) == text_section) {
                reloc.r_symbolnum = N_TEXT;
            } else if (symbol_get_section (fixup->add_symbol) == data_section) {
                reloc.r_symbolnum = N_DATA;
            } else if (symbol_get_section (fixup->add_symbol) == bss_section) {
                reloc.r_symbolnum = N_BSS;
            } else {
            
                report_at (__FILE__, __LINE__, REPORT_INTERNAL_ERROR, "invalid section %s", section_get_name (symbol_get_section (fixup->add_symbol)));
                exit (EXIT_FAILURE);
            
            }
        
        } else {
        
            struct symbol *symbol;
            int symbol_number;
            
            for (symbol = symbols, symbol_number = 0; symbol && (symbol != fixup->add_symbol); symbol = symbol->next) {
            
                if (!symbol_is_section_symbol (symbol)) {
                    symbol_number++;
                }
            
            }
            
            reloc.r_symbolnum  = symbol_number;
            reloc.r_symbolnum |= 1 << 27;
        
        }
        
        reloc.r_symbolnum |= (!!(fixup->pcrel)) << 24;
        
        {
        
            int log2_of_size, size;
            
            for (log2_of_size = -1, size = fixup->size; size; size >>= 1, log2_of_size++);
            reloc.r_symbolnum |= log2_of_size << 25;
        
        }
        
        if (fwrite (&reloc, sizeof (reloc), 1, outfile) != 1) {
        
            report_at (NULL, 0, REPORT_ERROR, "Error writing data relocations!");
            return;
        
        }
        
        header.a_drsize += sizeof (reloc);
    
    }
    
    symbol_table_size = 0;
    
    for (symbol = symbols; symbol; symbol = symbol->next) {
    
        if (!symbol_is_section_symbol (symbol)) {
        
            struct nlist symbol_entry;
            memset (&symbol_entry, 0, sizeof (symbol_entry));
            
            symbol_entry.n_strx = string_table_pos;
            string_table_pos += strlen (symbol->name) + 1;
            
            if (symbol->section == undefined_section) {
                symbol_entry.n_type = N_UNDF;
            } else if (symbol->section == text_section) {
                symbol_entry.n_type = N_TEXT;
            } else if (symbol->section == data_section) {
                symbol_entry.n_type = N_DATA;
            } else if (symbol->section == bss_section) {
                symbol_entry.n_type = N_BSS;
            } else if (symbol->section == absolute_section) {
                symbol_entry.n_type = N_ABS;
            } else {
            
                report_at (__FILE__, __LINE__, REPORT_INTERNAL_ERROR, "invalid section %s", section_get_name (symbol->section));
                exit (EXIT_FAILURE);
            
            }
            
            if (!symbol_is_section_symbol (symbol)) {
                symbol_entry.n_type |= N_EXT;
            }
            
            symbol_entry.n_value = symbol_get_value (symbol);
            
            if (fwrite (&symbol_entry, sizeof (symbol_entry), 1, outfile) != 1) {
            
                report_at (NULL, 0, REPORT_ERROR, "Error writing symbol table!");
                return;
            
            }
            
            symbol_table_size += sizeof (symbol_entry);
        
        }
    
    }
    
    header.a_syms = symbol_table_size;
    
    if (fwrite (&string_table_pos, sizeof (string_table_pos), 1, outfile) != 1) {
    
        report_at (NULL, 0, REPORT_ERROR, "Failed to write string table!");
        return;
    
    }
    
    for (symbol = symbols; symbol; symbol = symbol->next) {
    
        if (!symbol_is_section_symbol (symbol)) {
        
            if (fwrite (symbol->name, strlen (symbol->name) + 1, 1, outfile) != 1) {
            
                report_at (NULL, 0, REPORT_ERROR, "Failed to write string table!");
                return;
            
            }
        
        }
    
    }
    
    rewind (outfile);
    
    if (fwrite (&header, sizeof (header), 1, outfile) != 1) {
    
        report_at (NULL, 0, REPORT_ERROR, "Failed to write header!");
        return;
    
    }
    
    if (fclose (outfile)) {
        report_at (NULL, 0, REPORT_ERROR, "Failed to close file!");
    }

}


extern void demand_empty_rest_of_line (char **pp);

static void handler_bss (char **pp) {

    section_subsection_set (bss_section, (subsection_t) get_result_of_absolute_expression (pp));
    demand_empty_rest_of_line (pp);

}

static void handler_segment (char **pp) {

    char saved_ch, *name = *pp;
    
    while (**pp && **pp != '\n') {
        ++(*pp);
    }
    
    saved_ch = **pp;
    **pp = '\0';
    
    if (section_find_by_name (name)) {
    
        report (REPORT_ERROR, "segment name '%s' not recognised", name);
        goto out;
    
    }
    
    section_set_by_name (name);
    
out:
    
    **pp = saved_ch;

}

static struct pseudo_op pseudo_ops[] = {

    { ".bss",           handler_bss         },
    { ".section",       handler_segment     },
    
    { "section",        handler_segment     },
    { "segment",        handler_segment     },
    
    { NULL,             NULL                }

};

void install_aout_pseudo_ops (void) {

    struct pseudo_op *poe;
    
    for (poe = pseudo_ops; poe->name; ++poe) {
        add_pseudo_op (poe);
    }

}
