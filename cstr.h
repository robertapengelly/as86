/******************************************************************************
 * @file            cstr.h
 *****************************************************************************/
#ifndef     _CSTR_H
#define     _CSTR_H

typedef struct CString {

    int size;
    void *data;
    
    int size_allocated;

} CString;

void cstr_ccat (CString *cstr, int ch);
void cstr_cat (CString *cstr, const char *str, int len);

void cstr_new (CString *cstr);
void cstr_free (CString *cstr);

#endif      /* _CSTR_H */
