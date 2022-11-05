/******************************************************************************
 * @file            lex.h
 *****************************************************************************/
#ifndef     _LEX_H
#define     _LEX_H

#define     LEX_NAME_PART               0x0001
#define     LEX_NAME_START              0x0002

extern char lex_table[];

#define     is_name_beginner(c)         (lex_table[(c)] & LEX_NAME_START)
#define     is_name_part(c)             (lex_table[(c)] & LEX_NAME_PART)

#endif      /* _LEX_H */
