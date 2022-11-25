/******************************************************************************
 * @file            load_line.h
 *****************************************************************************/
#ifndef     _LOAD_LINE_H
#define     _LOAD_LINE_H

#include    <stddef.h>
#include    <stdio.h>

int load_line (char **line_p, char **line_end_p, char **real_line_p, unsigned long *real_line_len_p, unsigned long *newlines_p, FILE *ifp, void **load_line_internal_data_p);

void *load_line_create_internal_data (unsigned long *new_line_number_p);
void load_line_destory_internal_data (void *load_line_internal_data);

#endif      /* _LOAD_LINE_H */
