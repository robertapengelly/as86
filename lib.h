/******************************************************************************
 * @file            lib.h
 *****************************************************************************/
#ifndef     _LIB_H
#define     _LIB_H

#if     defined (_WIN32)
# define    PATHSEP                     ";"
#else
# define    PATHSEP                     ":"
#endif

#include    <stddef.h>

char *skip_whitespace (char *p);
char *to_lower (const char *str);
char *xstrdup (const char *str);

int strstart (const char *val, const char **str);
int xstrcasecmp (const char *s1, const char *s2);
int xstrncasecmp (const char *s1, const char *s2, size_t n);

void *xmalloc (size_t size);
void *xrealloc (void *ptr, size_t size);

void dynarray_add (void *ptab, size_t *nb_ptr, void *data);
void parse_args (int *pargc, char ***pargv, int optind);

#endif      /* _LIB_H */
