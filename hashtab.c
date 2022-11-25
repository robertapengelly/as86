/******************************************************************************
 * @file            hashtab.c
 *****************************************************************************/
#include    <stddef.h>
#include    <stdlib.h>
#include    <string.h>

#include    "hashtab.h"

static struct hashtab_entry *find_entry (struct hashtab_entry *entries, unsigned long capacity, struct hashtab_name *key);

static int adjust_capacity (struct hashtab *table, unsigned long new_capacity) {

    struct hashtab_entry *new_entries, *old_entries;
    unsigned long i, new_count, old_capacity;
    
    if ((new_entries = malloc (sizeof (*new_entries) * new_capacity)) == NULL) {
        return -2;
    }
    
    for (i = 0; i < new_capacity; ++i) {
    
        struct hashtab_entry *entry = &new_entries[i];
        
        entry->key = NULL;
        entry->value = NULL;
    
    }
    
    old_entries = table->entries;
    old_capacity = table->capacity;
    
    new_count = 0;
    
    for (i = 0; i < old_capacity; ++i) {
    
        struct hashtab_entry *entry = &old_entries[i], *dest;
        
        if (entry->key == NULL) {
            continue;
        }
        
        dest = find_entry (new_entries, new_capacity, entry->key);
        
        dest->key = entry->key;
        dest->value = entry->value;
        
        ++new_count;
    
    }
    
    free (old_entries);
    
    table->capacity = new_capacity;
    table->count = new_count;
    table->entries = new_entries;
    table->used = new_count;
    
    return 0;

}

static struct hashtab_entry *find_entry (struct hashtab_entry *entries, unsigned long capacity, struct hashtab_name *key) {

    struct hashtab_entry *tombstone = NULL;
    unsigned long index;
    
    for (index = key->hash % capacity; ; index = (index + 1) % capacity) {
    
        struct hashtab_entry *entry = &entries[index];
        
        if (entry->key == NULL) {
        
            if (entry->value == NULL) {
            
                if (tombstone == NULL) {
                    return entry;
                }
                
                return tombstone;
            
            } else if (tombstone == NULL) {
                tombstone = entry;
            }
        
        } else if (entry->key->bytes == key->bytes) {
        
            if (memcmp (entry->key->chars, key->chars, key->bytes) == 0 && entry->key->hash == key->hash) {
                return entry;
            }
        
        }
    
    }

}

static unsigned long hash_string (const void *p, unsigned long length) {

    const unsigned char *str = (const unsigned char *) p;
    unsigned long i, result = 0;
    
    for (i = 0; i < length; ++i) {
        result = (str[i] << 24) + (result >> 19) + (result << 16) + (result >> 13) + (str[i] << 8) - result;
    }
    
    return result;

}

struct hashtab_name *hashtab_alloc_name (const char *str) {

    struct hashtab_name *name;
    unsigned long bytes = strlen (str), hash = hash_string (str, bytes);
    
    if ((name = malloc (sizeof (*name))) == NULL) {
        return NULL;
    }
    
    name->bytes = bytes;
    name->chars = str;
    name->hash = hash;
    
    return name;

}

void *hashtab_get (struct hashtab *table, struct hashtab_name *key) {

    struct hashtab_entry *entry;
    
    if (table == NULL || table->count == 0) {
        return NULL;
    }
    
    entry = find_entry (table->entries, table->capacity, key);
    
    if (entry->key == NULL) {
        return NULL;
    }
    
    return entry->value;

}

int hashtab_put (struct hashtab *table, struct hashtab_name *key, void *value) {

    const long MIN_CAPACITY = 15;
    
    struct hashtab_entry *entry;
    int ret = 0;
    
    if (table->used >= table->capacity / 2) {
    
        long capacity = table->capacity * 2 - 1;
        
        if (capacity < MIN_CAPACITY) {
            capacity = MIN_CAPACITY;
        }
        
        if ((ret = adjust_capacity (table, capacity))) {
            return ret;
        }
    
    }
    
    entry = find_entry (table->entries, table->capacity, key);
    
    if (entry->key == NULL) {
    
        ++table->count;
        
        if (entry->value == NULL) {
            ++table->used;
        }
    
    }
    
    entry->key = key;
    entry->value = value;
    
    return 0;

}

void hashtab_remove (struct hashtab *table, struct hashtab_name *key) {

    struct hashtab_entry *entry;
    
    if ((entry = find_entry (table->entries, table->capacity, key)) != NULL) {
    
        entry->key = NULL;
        entry->value = NULL;
        
        --table->count;
    
    }

}
