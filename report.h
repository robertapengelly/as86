/******************************************************************************
 * @file            report.h
 *****************************************************************************/
#ifndef     _REPORT_H
#define     _REPORT_H

enum {

    REPORT_ERROR = 0,
    REPORT_FATAL_ERROR,
    REPORT_INTERNAL_ERROR,
    REPORT_WARNING

};

#if     defined (_WIN32)
# define    COLOR_ERROR                 12
# define    COLOR_WARNING               13
# define    COLOR_INTERNAL_ERROR        19
#else
# define    COLOR_ERROR                 91
# define    COLOR_INTERNAL_ERROR        94
# define    COLOR_WARNING               95
#endif

unsigned long get_error_count (void);

void report (int type, const char *fmt, ...);
void report_at (const char *filename, unsigned long line_number, int type, const char *fmt, ...);

#endif      /* _REPORT_H */
