/******************************************************************************
 * @file            coff.c
 *****************************************************************************/
#include    <stddef.h>
#include    <stdio.h>
#include    <stdlib.h>
#include    <string.h>

#include    "as.h"
#include    "coff.h"
#include    "frag.h"
#include    "lib.h"
#include    "pseudo_ops.h"
#include    "report.h"
#include    "section.h"
#include    "stdint.h"
#include    "symbol.h"

static int output_relocation (FILE *outfile, struct fixup *fixup) {

    struct relocation_entry reloc_entry;
    reloc_entry.VirtualAddress = fixup->frag->address + fixup->where;
    
    if (fixup->add_symbol == NULL) {
    
        report_at (__FILE__, __LINE__, REPORT_INTERNAL_ERROR, "+++output relocation fixup->add_symbol is NULL");
        exit (EXIT_FAILURE);
    
    }
    
    reloc_entry.SymbolTableIndex = symbol_get_symbol_table_index (fixup->add_symbol);
    
    switch (fixup->reloc_type) {
    
        case RELOC_TYPE_DEFAULT:
        
            switch (fixup->size) {
            
                case 2:
                
                    if (fixup->pcrel) {
                        reloc_entry.Type = IMAGE_REL_I386_REL16;
                    } else {
                        reloc_entry.Type = IMAGE_REL_I386_DIR16;
                    }
                    
                    break;
                
                case 4:
                
                    if (fixup->pcrel) {
                        reloc_entry.Type = IMAGE_REL_I386_REL32;
                    } else {
                        reloc_entry.Type = IMAGE_REL_I386_DIR32;
                    }
                    
                    break;
                
                default:
                
                    report_at (__FILE__, __LINE__, REPORT_INTERNAL_ERROR, "unsupported COFF relocation size %i for reloc_type RELOC_TYPE_DEFAULT", fixup->size);
                    exit (EXIT_FAILURE);
            
            }
            
            break;
        
        case RELOC_TYPE_RVA:
        
            reloc_entry.Type = IMAGE_REL_I386_DIR32NB;
            break;
        
        default:
        
            break;
    
    }
    
    if (fwrite (&reloc_entry, RELOCATION_ENTRY_SIZE, 1, outfile) != 1) {
        return 1;
    }
    
    return 0;

}

void coff_write_object (void) {

    struct coff_header header;
    struct symbol *symbol;
    
    FILE *outfile;
    uint32_t string_table_size = 4;
    
    section_t section;
    
    sections_number (1);
    memset (&header, 0, sizeof (header));
    
    if ((outfile = fopen (state->outfile, "wb")) == NULL) {
    
        report_at (NULL, 0, REPORT_ERROR, "Failed to open '%s' as output file", state->outfile);
        return;
    
    }
    
    header.Machine = IMAGE_FILE_MACHINE_I386;
    header.NumberOfSections = sections_get_count ();
    header.SizeOfOptionalHeader = 0;
    header.Characteristics = IMAGE_FILE_LINE_NUMS_STRIPPED;
    
    if (fseek (outfile, (sizeof (header) + sections_get_count () * sizeof (struct section_table_entry)), SEEK_SET)) {
    
        report_at (NULL, 0, REPORT_ERROR, "Failed to fseek");
        return;
    
    }
    
    for (section = sections; section; section = section_get_next_section (section)) {
    
        struct section_table_entry *section_header = xmalloc (sizeof (*section_header));
        section_set_object_format_dependent_data (section, section_header);
        
        memset (section_header, 0, sizeof (*section_header));
        strcpy (section_header->Name, section_get_name (section));
        
        if (section == text_section) {
            section_header->Characteristics = (IMAGE_SCN_CNT_CODE | IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_READ);
        } else if (section == data_section) {
            section_header->Characteristics = (IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_WRITE | IMAGE_SCN_MEM_READ);
        } else if (section == bss_section) {
            section_header->Characteristics = (IMAGE_SCN_CNT_UNINITIALIZED_DATA | IMAGE_SCN_MEM_WRITE | IMAGE_SCN_MEM_READ);
        } else {
            /* .idata, for example. */
            section_header->Characteristics = (IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_WRITE | IMAGE_SCN_MEM_READ);
        }
        
        if (section != bss_section) {
        
            struct frag *frag;
            section_set (section);
            
            section_header->PointerToRawData = ftell (outfile);
            section_header->SizeOfRawData = 0;
            
            for (frag = current_frag_chain->first_frag; frag; frag = frag->next) {
            
                if (frag->fixed_size == 0) {
                    continue;
                }
                
                if (fwrite (frag->buf, frag->fixed_size, 1, outfile) != 1) {
                
                    report_at (NULL, 0, REPORT_ERROR, "Failed whilst writing secton '%s'!", section_get_name (section));
                    return;
                
                }
                
                section_header->SizeOfRawData += frag->fixed_size;
            
            }
            
            if (section_header->SizeOfRawData == 0) {
                section_header->PointerToRawData = 0;
            }
        
        } else {
        
            struct frag *frag;
            
            section_set (section);
            section_header->VirtualSize = 0;
            
            for (frag = current_frag_chain->first_frag; frag; frag = frag->next) {
            
                if (frag->fixed_size == 0) {
                    continue;
                }
                
                section_header->VirtualSize += frag->fixed_size;
            
            }
        
        }
    
    }
    
    header.PointerToSymbolTable = ftell (outfile);
    header.NumberOfSymbols = 0;
    
    for (symbol = symbols; symbol; symbol = symbol->next) {
    
        struct symbol_table_entry sym_tbl_ent;
        
        if (symbol->object_format_dependent_data == NULL) {
            
            memset (&sym_tbl_ent, 0, sizeof (sym_tbl_ent));

            sym_tbl_ent.Value = symbol_get_value (symbol);
            
            if (symbol->section == undefined_section) {
                sym_tbl_ent.SectionNumber = IMAGE_SYM_UNDEFINED;
            } else {
                sym_tbl_ent.SectionNumber = section_get_number (symbol->section);
            }
        
            sym_tbl_ent.Type = ((IMAGE_SYM_DTYPE_NULL << 8) | IMAGE_SYM_TYPE_NULL);
            
            if (symbol_is_external (symbol) || symbol_is_undefined (symbol)) {
                sym_tbl_ent.StorageClass = IMAGE_SYM_CLASS_EXTERNAL;
            } else if (symbol_is_section_symbol (symbol)) {
                sym_tbl_ent.StorageClass = IMAGE_SYM_CLASS_STATIC;
            } else if (symbol_get_section (symbol) == text_section) {
                sym_tbl_ent.StorageClass = IMAGE_SYM_CLASS_LABEL;
            } else {
                sym_tbl_ent.StorageClass = IMAGE_SYM_CLASS_STATIC;
            }
            
            sym_tbl_ent.NumberOfAuxSymbols = 0;

        } else {

            sym_tbl_ent = *(struct symbol_table_entry *)(symbol->object_format_dependent_data);

            sym_tbl_ent.Value = symbol_get_value (symbol);

            if (!sym_tbl_ent.SectionNumber) {

                if (symbol->section == undefined_section) {
                    sym_tbl_ent.SectionNumber = IMAGE_SYM_UNDEFINED;
                } else {
                    sym_tbl_ent.SectionNumber = section_get_number (symbol->section);
                }

            }

            if (!sym_tbl_ent.Type) {
                sym_tbl_ent.Type = ((IMAGE_SYM_DTYPE_NULL << 8) | IMAGE_SYM_TYPE_NULL);
            }

            if (sym_tbl_ent.SectionNumber == IMAGE_SYM_UNDEFINED) {
                sym_tbl_ent.StorageClass = IMAGE_SYM_CLASS_EXTERNAL;
            } else if (sym_tbl_ent.StorageClass == IMAGE_SYM_CLASS_STATIC && symbol_is_external (symbol)) {
                sym_tbl_ent.StorageClass = IMAGE_SYM_CLASS_EXTERNAL;
            } else if (!sym_tbl_ent.StorageClass) {

                if (symbol_is_external (symbol)) {
                    sym_tbl_ent.StorageClass = IMAGE_SYM_CLASS_EXTERNAL;
                } else if (symbol_is_section_symbol (symbol)) {
                    sym_tbl_ent.StorageClass = IMAGE_SYM_CLASS_STATIC;
                } else if (symbol_get_section (symbol) == text_section) {
                    sym_tbl_ent.StorageClass = IMAGE_SYM_CLASS_LABEL;
                } else {
                    sym_tbl_ent.StorageClass = IMAGE_SYM_CLASS_STATIC;
                }

            }

        }
        
        symbol->write_name_to_string_table = 0;
        
        if (strlen (symbol->name) <= 8) {
            memcpy (sym_tbl_ent.Name, symbol->name, strlen (symbol->name));
        } else {
        
            memset (sym_tbl_ent.Name, 0, 4);
            
            sym_tbl_ent.Name[4] = string_table_size & 0xff;
            sym_tbl_ent.Name[5] = (string_table_size >> 8) & 0xff;
            sym_tbl_ent.Name[6] = (string_table_size >> 16) & 0xff;
            sym_tbl_ent.Name[7] = (string_table_size >> 24) & 0xff;
            
            string_table_size += strlen (symbol->name) + 1;
            symbol->write_name_to_string_table = 1;
        
        }
        
        if (fwrite (&sym_tbl_ent, SYMBOL_TABLE_ENTRY_SIZE, 1, outfile) != 1) {
        
            report_at (NULL, 0, REPORT_ERROR, "Error writing symbol table!");
            return;
        
        }
        
        symbol_set_symbol_table_index (symbol, header.NumberOfSymbols);
        header.NumberOfSymbols++;
    
    }
    
    if (fwrite (&string_table_size, 4, 1, outfile) != 1) {
    
        report_at (NULL, 0, REPORT_ERROR, "Failed to write string table!");
        return;
    
    }
    
    for (symbol = symbols; symbol; symbol = symbol->next) {
    
        if (symbol->write_name_to_string_table) {
        
            if (fwrite (symbol->name, strlen (symbol->name) + 1, 1, outfile) != 1) {
            
                report_at (NULL, 0, REPORT_ERROR, "Failed to write string table!");
                return;
            
            }
        
        }
    
    }
    
    for (section = sections; section; section = section_get_next_section (section)) {
    
        struct section_table_entry *section_header = section_get_object_format_dependent_data (section);
        struct fixup *fixup;
        
        section_header->PointerToRelocations = ftell (outfile);
        section_header->NumberOfRelocations = 0;
        
        section_set (section);
        
        for (fixup = current_frag_chain->first_fixup; fixup; fixup = fixup->next) {
        
            if (fixup->done) {
                continue;
            }
            
            if (output_relocation (outfile, fixup)) {
            
                report_at (NULL, 0, REPORT_ERROR, "Failed to write relocation!");
                return;
            
            }
            
            section_header->NumberOfRelocations++;
        
        }
        
        if (section_header->NumberOfRelocations == 0) {
            section_header->PointerToRelocations = 0;
        }
    
    }
    
    rewind (outfile);
    
    if (fwrite (&header, sizeof (header), 1, outfile) != 1) {
    
        report_at (NULL, 0, REPORT_ERROR, "Failed to write header!");
        return;
    
    }
    
    for (section = sections; section; section = section_get_next_section (section)) {
    
        struct section_table_entry *section_header = section_get_object_format_dependent_data (section);
        
        if (fwrite (section_header, sizeof (*section_header), 1, outfile) != 1) {
        
            report_at (NULL, 0, REPORT_ERROR, "Failed to write header!");
            return;
        
        }
    
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
    
    while (**pp && **pp != ' ' && **pp != '\n') {
        ++(*pp);
    }
    
    saved_ch = **pp;
    **pp = '\0';
    
    section_set_by_name (name);
    **pp = saved_ch;
    
    demand_empty_rest_of_line (pp);

}

static struct pseudo_op pseudo_ops[] = {

    { ".bss",           handler_bss         },
    { ".section",       handler_segment     },
    
    { "section",        handler_segment     },
    { "segment",        handler_segment     },
    
    { NULL,             NULL                }

};

void install_coff_pseudo_ops (void) {

    struct pseudo_op *poe;
    
    for (poe = pseudo_ops; poe->name; ++poe) {
        add_pseudo_op (poe);
    }

}
