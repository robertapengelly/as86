/******************************************************************************
 * @file            cstr.h
 *****************************************************************************/
#ifndef     _CSTR_H
#define     _CSTR_H

#include    "stdint.h"

typedef struct CString {

    int32_t size;
    void *data;
    
    int32_t size_allocated;

} CString;

void cstr_ccat (CString *cstr, int ch);
void cstr_cat (CString *cstr, const char *str, int32_t len);

void cstr_new (CString *cstr);
void cstr_free (CString *cstr);

#endif      /* _CSTR_H */
