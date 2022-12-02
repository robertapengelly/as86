/******************************************************************************
 * @file            cstr.c
 *****************************************************************************/
#include    <stdlib.h>
#include    <string.h>

#include    "cstr.h"
#include    "lib.h"
#include    "stdint.h"

static void cstr_realloc (CString *cstr, int32_t new_size) {

    int32_t size = cstr->size_allocated;
    
    if (size < 8) {
        size = 8;
    }
    
    while (size < new_size) {
        size *= 2;
    }
    
    cstr->data = xrealloc (cstr->data, size);
    cstr->size_allocated = size;

}

void cstr_ccat (CString *cstr, int ch) {

    int32_t size = cstr->size + 1;
    
    if (size > cstr->size_allocated) {
        cstr_realloc (cstr, size);
    }
    
    ((unsigned char *) cstr->data)[size - 1] = ch;
    cstr->size = size;

}

void cstr_cat (CString *cstr, const char *str, int32_t len) {

    int32_t size;
    
    if (len <= 0) {
        len = strlen (str) + 1 + len;
    }
    
    size = cstr->size + len;
    
    if (size > cstr->size_allocated) {
        cstr_realloc (cstr, size);
    }
    
    memmove (((unsigned char *) cstr->data) + cstr->size, str, len);
    cstr->size = size;

}

void cstr_new (CString *cstr) {
    memset (cstr, 0, sizeof (CString));
}

void cstr_free (CString *cstr) {

    free (cstr->data);
    cstr_new (cstr);

}
