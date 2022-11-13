/******************************************************************************
 * @file            as.h
 *****************************************************************************/
#ifndef     _AS_H
#define     _AS_H

#include    <stddef.h>

#include    "vector.h"

struct proc {

    char *name;
    struct vector regs;

};

struct as_state {

    char **defs, **files, **inc_paths;
    size_t nb_defs, nb_files, nb_inc_paths;
    
    const char *format, *listing, *outfile;
    int nowarn, model;
    
    const char *sym_start;
    struct vector procs;

};

extern struct as_state *state;
extern const char *program_name;

struct object_format {

    const char *name;
    
    void (*install_pseudo_ops) (void);
    void (*adjust_code) (void);
    void (*write_object) (void);

};

#define     ARRAY_SIZE(arr)             (sizeof (arr) / sizeof (arr[0]))

#endif      /* _AS_H */
