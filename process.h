/******************************************************************************
 * @file            process.h
 *****************************************************************************/
#ifndef     _PROCESS_H
#define     _PROCESS_H

#if     defined (CONV_CHARSET)
# define    handler_include             handincl
#endif


int process_file (const char *fname);

#endif      /* _PROCESS_H */
