/******************************************************************************
 * @file            coff.h
 *****************************************************************************/
#ifndef     _COFF_H
#define     _COFF_H

#include    "stdint.h"

struct coff_header {

    unsigned char Machine[2];
    unsigned char NumberOfSections[2];
    
    unsigned char TimeDateStamp[4];
    unsigned char PointerToSymbolTable[4];
    unsigned char NumberOfSymbols[4];
    
    unsigned char SizeOfOptionalHeader[2];
    unsigned char Characteristics[2];

};

#define     IMAGE_FILE_MACHINE_AMD64                        0x8664
#define     IMAGE_FILE_MACHINE_I386                         0x014C

#define     IMAGE_FILE_LINE_NUMS_STRIPPED                   0x0004
#define     IMAGE_FILE_32BIT_MACHINE                        0x0100

struct section_table_entry {

    char Name[8];
    
    unsigned char VirtualSize[4];
    unsigned char VirtualAddress[4];
    
    unsigned char SizeOfRawData[4];
    unsigned char PointerToRawData[4];
    unsigned char PointerToRelocations[4];
    unsigned char PointerToLinenumbers[4];
    
    unsigned char NumberOfRelocations[2];
    unsigned char NumberOfLinenumbers[2];
    
    unsigned char Characteristics[4];

};

#define     IMAGE_SCN_TYPE_NOLOAD                           0x00000002
#define     IMAGE_SCN_TYPE_NO_PAD                           0x00000008 /* Obsolete, means the same as IMAGE_SCN_ALIGN_1BYTES. */
#define     IMAGE_SCN_CNT_CODE                              0x00000020
#define     IMAGE_SCN_CNT_INITIALIZED_DATA                  0x00000040
#define     IMAGE_SCN_CNT_UNINITIALIZED_DATA                0x00000080
#define     IMAGE_SCN_LNK_INFO                              0x00000200
#define     IMAGE_SCN_LNK_REMOVE                            0x00000800
#define     IMAGE_SCN_LNK_COMDAT                            0x00001000
#define     IMAGE_SCN_ALIGN_1BYTES                          0x00100000
#define     IMAGE_SCN_ALIGN_2BYTES                          0x00200000
#define     IMAGE_SCN_ALIGN_4BYTES                          0x00300000
#define     IMAGE_SCN_ALIGN_8BYTES                          0x00400000
#define     IMAGE_SCN_ALIGN_16BYTES                         0x00500000
#define     IMAGE_SCN_ALIGN_32BYTES                         0x00600000
#define     IMAGE_SCN_ALIGN_64BYTES                         0x00700000
#define     IMAGE_SCN_ALIGN_128BYTES                        0x00800000
#define     IMAGE_SCN_ALIGN_256BYTES                        0x00900000
#define     IMAGE_SCN_ALIGN_512BYTES                        0x00A00000
#define     IMAGE_SCN_ALIGN_1024BYTES                       0x00B00000
#define     IMAGE_SCN_ALIGN_2048BYTES                       0x00C00000
#define     IMAGE_SCN_ALIGN_4096BYTES                       0x00D00000
#define     IMAGE_SCN_ALIGN_8192BYTES                       0x00E00000
#define     IMAGE_SCN_MEM_SHARED                            0x10000000
#define     IMAGE_SCN_MEM_EXECUTE                           0x20000000
#define     IMAGE_SCN_MEM_READ                              0x40000000
#define     IMAGE_SCN_MEM_WRITE                             0x80000000

struct relocation_entry {

    unsigned char VirtualAddress[4];
    unsigned char SymbolTableIndex[4];
    
    unsigned char Type[2];

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
    unsigned char Value[4];
    
    unsigned char SectionNumber[2];
    unsigned char Type[2];
    
    unsigned char StorageClass[1];
    unsigned char NumberOfAuxSymbols[1];

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
