/******************************************************************************
 * @file            lex.h
 *****************************************************************************/
#ifndef     _LEX_H
#define     _LEX_H

#define     LEX_NAME_PART               0x0001
#define     LEX_NAME_START              0x0002


/* use a shortened name to failsafe in case there is
   name truncation on MVS */
#define is_end_of_line_array ieola

extern char is_end_of_line_array[];
extern char lex_table[];

#if     defined (CONV_CHARSET)
# include       "tasc.h"
# define    is_name_beginner(c)         (lex_table[(int) tasc (c)] & LEX_NAME_START)
# define    is_name_part(c)             (lex_table[(int) tasc (c)] & LEX_NAME_PART)
# define    is_end_of_line(c)           (is_end_of_line_array[(int) tasc (c)])
#else
# define    is_name_beginner(c)         (lex_table[(int) (c)] & LEX_NAME_START)
# define    is_name_part(c)             (lex_table[(int) (c)] & LEX_NAME_PART)
# define    is_end_of_line(c)           (is_end_of_line_array[(int) (c)])
#endif

#endif      /* _LEX_H */
