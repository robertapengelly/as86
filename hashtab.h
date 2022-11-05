/******************************************************************************
 * @file            hashtab.h
 *****************************************************************************/
#ifndef     _HASHTAB_H
#define     _HASHTAB_H

#include    <stddef.h>

struct hashtab_name {

    const char *chars;
    size_t bytes, hash;

};

struct hashtab_entry {

    struct hashtab_name *key;
    void *value;

};

struct hashtab {

    struct hashtab_entry *entries;
    size_t capacity, count, used;

};

struct hashtab_name *hashtab_alloc_name (const char *str);
int hashtab_put (struct hashtab *table, struct hashtab_name *key, void *value);

void *hashtab_get (struct hashtab *table, struct hashtab_name *key);
void hashtab_remove (struct hashtab *table, struct hashtab_name *key);

#endif      /* _HASHTAB_H */
