/******************************************************************************
 * @file            report.c
 *****************************************************************************/
#include    <stdarg.h>
#include    <stdio.h>

#include    "as.h"
#include    "report.h"

extern void get_filename_and_line_number (const char **filename_p, unsigned long *line_number_p);
static unsigned long errors = 0;

#ifndef     __PDOS__
#if     defined (_WIN32)
# include   <windows.h>
static int OriginalConsoleColor = -1;
#endif

static void reset_console_color (void) {

#if     defined (_WIN32)

    HANDLE hStdError = GetStdHandle (STD_ERROR_HANDLE);
    
    if (OriginalConsoleColor == -1) { return; }
    
    SetConsoleTextAttribute (hStdError, OriginalConsoleColor);
    OriginalConsoleColor = -1;

#else

    fprintf (stderr, "\033[0m");

#endif

}

static void set_console_color (int color) {

#if     defined (_WIN32)

    HANDLE hStdError = GetStdHandle (STD_ERROR_HANDLE);
    WORD wColor;
    
    if (OriginalConsoleColor == -1) {
    
        CONSOLE_SCREEN_BUFFER_INFO csbi;
        
        if (!GetConsoleScreenBufferInfo (hStdError, &csbi)) {
            return;
        }
        
        OriginalConsoleColor = csbi.wAttributes;
    
    }
    
    wColor = (OriginalConsoleColor & 0xF0) + (color & 0xF);
    SetConsoleTextAttribute (hStdError, wColor);

#else

    fprintf (stderr, "\033[%dm", color);

#endif

}
#endif

unsigned long get_error_count (void) {
    return errors;
}

void report (int type, const char *fmt, ...) {

    va_list ap;
    
    const char *filename;
    unsigned long line_number;
    
    if (type == REPORT_WARNING && state->nowarn) {
        return;
    }
    
    get_filename_and_line_number (&filename, &line_number);
    
    if (filename) {
    
        if (line_number == 0) {
            fprintf (stderr, "%s: ", filename);
        } else {
            fprintf (stderr, "%s:", filename);
        }
    
    }
    
    if (line_number > 0) {
        fprintf (stderr, "%lu: ", line_number);
    }
    
    if (type == REPORT_ERROR || type == REPORT_FATAL_ERROR) {
    
#ifndef     __PDOS__
        set_console_color (COLOR_ERROR);
#endif
        
        if (type == REPORT_ERROR) {
            fprintf (stderr, "error:");
        } else {
            fprintf (stderr, "fatal error:");
        }
    
    } else if (type == REPORT_INTERNAL_ERROR) {
    
#ifndef     __PDOS__
        set_console_color (COLOR_INTERNAL_ERROR);
#endif
        
        fprintf (stderr, "internal error:");
    
    } else if (type == REPORT_WARNING) {
    
#ifndef     __PDOS__
        set_console_color (COLOR_WARNING);
#endif
        
        fprintf (stderr, "warning:");
    
    }
    
#ifndef     __PDOS__
    reset_console_color ();
#endif
    
    fprintf (stderr, " ");
    
    va_start (ap, fmt);
    vfprintf (stderr, fmt, ap);
    va_end (ap);
    
    fprintf (stderr, "\n");
    
    if (type == REPORT_ERROR || type == REPORT_FATAL_ERROR || type == REPORT_INTERNAL_ERROR) {
        ++errors;
    }

}

void report_at (const char *filename, unsigned long line_number, int type, const char *fmt, ...) {

    va_list ap;
    
    if (type == REPORT_WARNING && state->nowarn) {
        return;
    }
    
    if (filename) {
    
        if (line_number == 0) {
            fprintf (stderr, "%s: ", filename);
        } else {
            fprintf (stderr, "%s:", filename);
        }
    
    }
    
    if (line_number > 0) {
        fprintf (stderr, "%lu: ", line_number);
    }
    
    if (type == REPORT_ERROR || type == REPORT_FATAL_ERROR) {
    
#ifndef     __PDOS__
        set_console_color (COLOR_ERROR);
#endif
        
        if (type == REPORT_ERROR) {
            fprintf (stderr, "error:");
        } else {
            fprintf (stderr, "fatal error:");
        }
    
    } else if (type == REPORT_INTERNAL_ERROR) {
    
#ifndef     __PDOS__
        set_console_color (COLOR_INTERNAL_ERROR);
#endif
        
        fprintf (stderr, "internal error:");
    
    } else if (type == REPORT_WARNING) {
    
#ifndef     __PDOS__
        set_console_color (COLOR_WARNING);
#endif
        
        fprintf (stderr, "warning:");
    
    }
    
#ifndef     __PDOS__
    reset_console_color ();
#endif
    
    fprintf (stderr, " ");
    
    va_start (ap, fmt);
    vfprintf (stderr, fmt, ap);
    va_end (ap);
    
    fprintf (stderr, "\n");
    
    if (type == REPORT_ERROR || type == REPORT_FATAL_ERROR || type == REPORT_INTERNAL_ERROR) {
        ++errors;
    }

}
