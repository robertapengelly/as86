/******************************************************************************
 * @file            pseudo_ops.h
 *****************************************************************************/
#ifndef     _PSEUDO_OPS_H
#define     _PSEUDO_OPS_H

struct pseudo_op {

    const char *name;
    void (*handler) (char **pp);

};

struct pseudo_op *find_pseudo_op (const char *name);

void add_pseudo_op (struct pseudo_op *poe);
void handler_equ (char **pp, char *name);
void handler_ignore (char **pp);
void pseudo_ops_init (void);

int is_defined (char *p);
void handler_define (char **pp);

#endif      /* _PSEUDO_OPS_H */
