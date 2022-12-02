/******************************************************************************
 * @file            coff.h
 *****************************************************************************/
#ifndef     _COFF_H
#define     _COFF_H

#include    "stdint.h"

struct coff_header {

    uint16_t Machine;
    uint16_t NumberOfSections;
    
    uint32_t TimeDateStamp;
    uint32_t PointerToSymbolTable;
    uint32_t NumberOfSymbols;
    
    uint16_t SizeOfOptionalHeader;
    uint16_t Characteristics;

};

#define     IMAGE_FILE_MACHINE_I386                         0x014C
#define     IMAGE_FILE_LINE_NUMS_STRIPPED                   0x0004

struct section_table_entry {

    char Name[8];
    
    uint32_t VirtualSize;
    uint32_t VirtualAddress;
    
    uint32_t SizeOfRawData;
    uint32_t PointerToRawData;
    uint32_t PointerToRelocations;
    uint32_t PointerToLinenumbers;
    
    uint16_t NumberOfRelocations;
    uint16_t NumberOfLinenumbers;
    
    uint32_t Characteristics;

};

#define     IMAGE_SCN_CNT_CODE                              0x00000020
#define     IMAGE_SCN_CNT_INITIALIZED_DATA                  0x00000040
#define     IMAGE_SCN_CNT_UNINITIALIZED_DATA                0x00000080
#define     IMAGE_SCN_MEM_EXECUTE                           0x20000000
#define     IMAGE_SCN_MEM_READ                              0x40000000
#define     IMAGE_SCN_MEM_WRITE                             0x80000000

struct relocation_entry {

    uint32_t VirtualAddress;
    uint32_t SymbolTableIndex;
    
    uint16_t Type;

};

#define     RELOCATION_ENTRY_SIZE                           10

#define     IMAGE_REL_I386_ABSOLUTE                         0x0000
#define     IMAGE_REL_I386_DIR16                            0x0001
#define     IMAGE_REL_I386_REL16                            0x0002
#define     IMAGE_REL_I386_DIR32                            0x0006
#define     IMAGE_REL_I386_DIR32NB                          0x0007
#define     IMAGE_REL_I386_REL32                            0x0014

struct symbol_table_entry {

    char Name[8];
    uint32_t Value;
    
    signed short SectionNumber;
    uint16_t Type;
    
    unsigned char StorageClass;
    unsigned char NumberOfAuxSymbols;

};

#define     SYMBOL_TABLE_ENTRY_SIZE                         18

#define     IMAGE_SYM_UNDEFINED                             0
#define     IMAGE_SYM_ABSOLUTE                              -1
#define     IMAGE_SYM_DEBUG                                 -2

#define     IMAGE_SYM_TYPE_NULL                             0
#define     IMAGE_SYM_DTYPE_NULL                            0

#define     IMAGE_SYM_CLASS_EXTERNAL                        2
#define     IMAGE_SYM_CLASS_STATIC                          3
#define     IMAGE_SYM_CLASS_LABEL                           6
#define     IMAGE_SYM_CLASS_FILE                            103

void coff_write_object (void);
void install_coff_pseudo_ops (void);

#endif      /* _COFF_H */
