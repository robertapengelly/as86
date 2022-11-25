/******************************************************************************
 * @file            aout.h
 *****************************************************************************/
#ifndef     _AOUT_H
#define     _AOUT_H

#include    <stdint.h>
#define     SEGMENT_SIZE                0x10000UL

struct exec {

    uint32_t a_info;
    uint32_t a_text;
    uint32_t a_data;
    uint32_t a_bss;
    uint32_t a_syms;
    uint32_t a_entry;
    uint32_t a_trsize;
    uint32_t a_drsize;

};

#define     OMAGIC                      0407
#define     NMAGIC                      0410
#define     ZMAGIC                      0413
#define     QMAGIC                      0314

/* Relocation entry. */
struct relocation_info {

    int32_t r_address;
    uint32_t r_symbolnum;

};

/* Symbol table entry. */
struct nlist {

    /*union {
    
        char *n_name;
        int n_strx;
    
    } n_un;*/
    
    int32_t n_strx;
    unsigned char n_type;
    
    char n_other;
    short n_desc;
    
    uint32_t n_value;

};

/* n_type values: */
#define     N_UNDF                      0x00
#define     N_ABS                       0x02
#define     N_TEXT                      0x04
#define     N_DATA                      0x06
#define     N_BSS                       0x08
#define     N_COMM                      0x12
#define     N_FN                        0x1f

#define     N_EXT                       0x01
#define     N_TYPE                      0x1e

/* Next is the string table,
 * starting with 4 bytes length including the field (so minimum is 4). */

#define     N_TXTOFF(e)                 (0x400)
#define     N_TXTADDR(e)                (SEGMENT_SIZE)
/* this formula doesn't work when the text size is exactly 64k */
#define     N_DATADDR(e)                \
    (N_TXTADDR (e) + SEGMENT_SIZE + ((e).a_text & 0xffff0000UL))
#define     N_BSSADDR(e)                (N_DATADDR (e) + (e).a_data)

#define     N_GETMAGIC(exec)            ((exec).a_info & 0xffff)

void aout_adjust_code (void);
void aout_write_object (void);
void install_aout_pseudo_ops (void);

#endif      /* _AOUT_H */
