/******************************************************************************
 * @file            vector.c
 *****************************************************************************/
#include    <stddef.h>
#include    <stdlib.h>

#include    "vector.h"

void *vec_pop (struct vector *vec) {

    if (!vec || vec == NULL) {
        return NULL;
    }
    
    if (vec->length <= 0) {
        return NULL;
    }
    
    return vec->data[--vec->length];

}

int vec_push (struct vector *vec, void *elem) {

    if (vec->capacity <= vec->length) {
    
        if (vec->capacity <= 0) {
            vec->capacity = 16;
        } else {
            vec->capacity <<= 1;
        }
        
        if ((vec->data = realloc (vec->data, sizeof (*vec->data) * vec->capacity)) == NULL) {
            return -2;
        }
    
    }
    
    vec->data[vec->length++] = (void *) elem;
    return 0;

}
