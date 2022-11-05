/******************************************************************************
 * @file            vector.h
 *****************************************************************************/
#ifndef     _VECTOR_H
#define     _VECTOR_H

struct vector {

    void **data;
    int capacity, length;

};

int vec_push (struct vector *vec, void *elem);
void *vec_pop (struct vector *vec);

#endif      /* _VECTOR_H */
