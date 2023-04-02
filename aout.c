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
#include    "intel.h"
#include    "lib.h"
#include    "pseudo_ops.h"
#include    "report.h"
#include    "section.h"
#include    "stdint.h"
#include    "symbol.h"
#include    "write7x.h"

void aout_adjust_code (void) {

    struct frag *frag;
    unsigned long address;
    
    section_set (text_section);
    
    state->text_section_size = current_frag_chain->last_frag->address + current_frag_chain->last_frag->fixed_size;
    address = state->text_section_size;
    
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

static int output_relocation (FILE *outfile, struct fixup *fixup, unsigned long start_address_of_section) {

    struct relocation_info reloc;
    
    int32_t log2_of_size, size;
    uint32_t r_symbolnum;
    
    write741_to_byte_array (reloc.r_address, fixup->frag->address + fixup->where - start_address_of_section);
    
    if (symbol_is_section_symbol (fixup->add_symbol)) {
    
        if (symbol_get_section (fixup->add_symbol) == text_section) {
            r_symbolnum = N_TEXT;
        } else if (symbol_get_section (fixup->add_symbol) == data_section) {
            r_symbolnum = N_DATA;
        } else if (symbol_get_section (fixup->add_symbol) == bss_section) {
            r_symbolnum = N_BSS;
        } else {
            report_at (__FILE__, __LINE__, REPORT_INTERNAL_ERROR, "invalid section %s", section_get_name (symbol_get_section (fixup->add_symbol)));
        }
    
    } else {
    
        struct symbol *symbol;
        int32_t symbol_number;
        
        for (symbol = symbols, symbol_number = 0; symbol && (symbol != fixup->add_symbol); symbol = symbol->next) {
        
            if (symbol_is_section_symbol (symbol)) {
                continue;
            }
            
            if (!state->keep_locals && *symbol->name == 'L') {
                continue;
            }
            
            symbol_number++;
        
        }
        
        r_symbolnum  = symbol_number;
        r_symbolnum |= 1L << 27;
    
    }
    
    if (fixup->pcrel) {
        r_symbolnum |= 1L << 24;
    }
    
    for (log2_of_size = -1, size = fixup->size; size; size >>= 1, log2_of_size++);
    r_symbolnum |= (uint32_t) log2_of_size << 25;
    
    write741_to_byte_array (reloc.r_symbolnum, r_symbolnum);
    
    if (fwrite (&reloc, sizeof (reloc), 1, outfile) != 1) {
    
        report_at (NULL, 0, REPORT_ERROR, "Error writing text relocations!");
        return 1;
    
    }
    
    return 0;

}

void aout_write_object (void) {

    struct exec header;
    struct frag *frag;
    
    struct fixup *fixup;
    unsigned long start_address_of_data;
    
    struct symbol *symbol;
    unsigned long symbol_table_size;
    
    int32_t string_table_pos;
    FILE *outfile;
    
    uint32_t a_text, a_data, a_bss;
    uint32_t a_trsize, a_drsize;
    
    memset (&header, 0, sizeof (header));
    write741_to_byte_array (header.a_info, 0x00640000 | OMAGIC);
    
    if ((outfile = fopen (state->outfile, "wb")) == NULL) {
    
        report_at (NULL, 0, REPORT_ERROR, "Failed to open '%s' as output file", state->outfile);
        return;
    
    }
    
    if (fseek (outfile, sizeof (header), SEEK_SET)) {
    
        report_at (NULL, 0, REPORT_ERROR, "Failed whilst seeking passed header");
        return;
    
    }
    
    section_set (text_section);
    a_text = 0;
    
    for (frag = current_frag_chain->first_frag; frag; frag = frag->next) {
    
        if (frag->fixed_size == 0) {
            continue;
        }
        
        if (fwrite (frag->buf, frag->fixed_size, 1, outfile) != 1) {
        
            report_at (NULL, 0, REPORT_ERROR, "Failed whilst writing text!");
            return;
        
        }
        
        a_text += frag->fixed_size;
    
    }
    
    write741_to_byte_array (header.a_text, a_text);
    
    section_set (data_section);
    a_data = 0;
    
    for (frag = current_frag_chain->first_frag; frag; frag = frag->next) {
    
        if (frag->fixed_size == 0) {
            continue;
        }
        
        if (fwrite (frag->buf, frag->fixed_size, 1, outfile) != 1) {
        
            report_at (NULL, 0, REPORT_ERROR, "Failed whilst writing data!");
            return;
        
        }
        
        a_data += frag->fixed_size;
    
    }
    
    write741_to_byte_array (header.a_data, a_data);
    
    section_set (bss_section);
    a_bss = 0;
    
    for (frag = current_frag_chain->first_frag; frag; frag = frag->next) {
    
        if (frag->fixed_size == 0) {
            continue;
        }
        
        a_bss += frag->fixed_size;
    
    }
    
    write741_to_byte_array (header.a_bss, a_bss);
    
    section_set (text_section);
    a_trsize = 0;
    
    for (fixup = current_frag_chain->first_fixup; fixup; fixup = fixup->next) {
    
        struct relocation_info reloc;
        
        if (fixup->done) {
            continue;
        }
        
        if (output_relocation (outfile, fixup, 0)) {
            return;
        }
        
        a_trsize += sizeof (reloc);
    
    }
    
    write741_to_byte_array (header.a_trsize, a_trsize);
    
    section_set (data_section);
    a_drsize = 0;
    
    start_address_of_data = current_frag_chain->first_frag->address;
    
    for (fixup = current_frag_chain->first_fixup; fixup; fixup = fixup->next) {
    
        struct relocation_info reloc;
        
        if (fixup->done) {
            continue;
        }
        
        if (output_relocation (outfile, fixup, start_address_of_data)) {
            return;
        }
        
        a_drsize += sizeof (reloc);
    
    }
    
    write741_to_byte_array (header.a_drsize, a_drsize);
    
    symbol_table_size = 0;
    string_table_pos = 4;
    
    for (symbol = symbols; symbol; symbol = symbol->next) {
    
        struct nlist symbol_entry;
        memset (&symbol_entry, 0, sizeof (symbol_entry));
        
        if (symbol_is_section_symbol (symbol)) {
            continue;
        }
        
        if (!state->keep_locals && *symbol->name == 'L') {
            continue;
        }
        
        write741_to_byte_array (symbol_entry.n_strx, string_table_pos);
        
        if (state->sym_start) {
        
            if (symbol_is_external (symbol)) {
                string_table_pos++;
            } else if (symbol_is_undefined (symbol)) {
            
                struct hashtab_name *key;
                
                if ((key = hashtab_alloc_name (symbol->name)) != NULL) {
                
                    if (hashtab_get (&state->hashtab_externs, key) != NULL) {
                        string_table_pos++;
                    }
                    
                    free (key);
                
                }
            
            }
        
        }
        
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
        
        symbol_entry.n_type |= N_EXT;
        write741_to_byte_array (symbol_entry.n_value, symbol_get_value (symbol));
        
        if (fwrite (&symbol_entry, sizeof (symbol_entry), 1, outfile) != 1) {
        
            report_at (NULL, 0, REPORT_ERROR, "Error writing symbol table!");
            return;
        
        }
        
        symbol_table_size += sizeof (symbol_entry);
    
    }
    
    write741_to_byte_array (header.a_syms, symbol_table_size);
    
    if (fwrite (&string_table_pos, sizeof (string_table_pos), 1, outfile) != 1) {
    
        report_at (NULL, 0, REPORT_ERROR, "Failed to write string table!");
        return;
    
    }
    
    for (symbol = symbols; symbol; symbol = symbol->next) {
    
        if (symbol_is_section_symbol (symbol)) {
            continue;
        }
        
        if (!state->keep_locals && *symbol->name == 'L') {
            continue;
        }
        
        if (state->sym_start) {
        
            if (symbol_is_external (symbol)) {
            
                if (fwrite (state->sym_start, strlen (state->sym_start), 1, outfile) != 1) {
                
                    report_at (NULL, 0, REPORT_ERROR, "Failed to write string table!");
                    return;
                
                }
            
            } else if (symbol_is_undefined (symbol)) {
            
                struct hashtab_name *key;
                
                if ((key = hashtab_alloc_name (symbol->name)) != NULL) {
                
                    if (hashtab_get (&state->hashtab_externs, key) != NULL) {
                    
                        if (fwrite (state->sym_start, strlen (state->sym_start), 1, outfile) != 1) {
                        
                            report_at (NULL, 0, REPORT_ERROR, "Failed to write string table!");
                            return;
                        
                        }
                    
                    }
                    
                    free (key);
                
                }
            
            }
        
        }
        
        if (fwrite (symbol->name, strlen (symbol->name) + 1, 1, outfile) != 1) {
        
            report_at (NULL, 0, REPORT_ERROR, "Failed to write string table!");
            return;
        
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

extern const char is_end_of_line[];

extern char get_symbol_name_end (char **pp);
extern void ignore_rest_of_line (char **pp);

static void handler_masm_segment (char **pp) {

    char *arg, saved_ch;
    
    int32_t last = state->segs.length - 1;

    struct seg *seg = state->segs.data[last];
    seg->bits = machine_dependent_get_bits ();
    
    for (;;) {
    
        *pp = skip_whitespace (*pp);
        
        if (is_end_of_line[(int) **pp]) {
            break;
        }
        
        if (**pp == '\'') {
        
            arg = ++(*pp);
            
            while (!is_end_of_line[(int) **pp] && **pp != '\'') {
                (*pp)++;
            }
            
            saved_ch = **pp;
            **pp = '\0';
        
        } else {
        
            arg = *pp;
            saved_ch = get_symbol_name_end (pp);
        
        }
        
        if (xstrcasecmp (arg, "byte") == 0 || xstrcasecmp (arg, "word") == 0 || xstrcasecmp (arg, "dword") == 0 || xstrcasecmp (arg, "para") == 0 || xstrcasecmp (arg, "page") == 0) {
            ;
        } else if (xstrcasecmp (arg, "public") == 0 || xstrcasecmp (arg, "stack") == 0 || xstrcasecmp (arg, "common") == 0 || xstrcasecmp (arg, "memory") == 0 || xstrcasecmp (arg, "private") == 0) {
            ;
        } else if (xstrcasecmp (arg, "use16") == 0) {
            machine_dependent_set_bits (16);
        } else if (xstrcasecmp (arg, "use32") == 0 || xstrcasecmp (arg, "flat") == 0) {
            machine_dependent_set_bits (32);
        } else if (xstrcasecmp (arg, "code") == 0) {
            section_set_by_name (".text");
        } else if (xstrcasecmp (arg, "data") == 0) {
            section_set_by_name (".data");
        } else if (xstrcasecmp (arg, "bss") == 0) {
            section_set_by_name (".bss");
        } else {
        
            **pp = saved_ch;
            
            report (REPORT_ERROR, "invalid option passed to segment");
            ignore_rest_of_line (pp);
            
            return;
        
        }
        
        **pp = saved_ch;
        
        if (**pp == '\'') {
            (*pp)++;
        }
    
    }

}

static struct pseudo_op pseudo_ops[] = {

    { ".bss",           handler_bss             },
    { ".data?",         handler_bss             },
    { ".section",       handler_segment         },
    
    { "section",        handler_segment         },
    { "segment",        handler_segment         },
    
    { "masm_segment",   handler_masm_segment    },
    { NULL,             NULL                    }

};

void install_aout_pseudo_ops (void) {

    struct pseudo_op *poe;
    
    for (poe = pseudo_ops; poe->name; ++poe) {
        add_pseudo_op (poe);
    }

}
