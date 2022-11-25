/******************************************************************************
 * @file            vector.h
 *****************************************************************************/
#ifndef     _VECTOR_H
#define     _VECTOR_H

#include    "types.h"

struct vector {

    void **data;
    int32_t capacity, length;

};

int vec_push (struct vector *vec, void *elem);
void *vec_pop (struct vector *vec);

#endif      /* _VECTOR_H */
