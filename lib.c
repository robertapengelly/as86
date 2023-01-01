/******************************************************************************
 * @file            lib.c
 *****************************************************************************/
#include    <ctype.h>
#include    <stddef.h>
#include    <stdio.h>
#include    <stdlib.h>
#include    <string.h>

#include    "as.h"
#include    "cstr.h"
#include    "lib.h"
#include    "report.h"

struct option {

    const char *name;
    int index, flags;

};

#define     OPTION_NO_ARG               0x0001
#define     OPTION_HAS_ARG              0x0002

enum options {

    OPTION_IGNORED = 0,
    OPTION_DEFINE,
    OPTION_FORMAT,
    OPTION_HELP,
    OPTION_INCLUDE,
    OPTION_KEEP_LOCALS,
    OPTION_LISTING,
    OPTION_NOWARN,
    OPTION_OUTFILE

};

static struct option opts[] = {

    { "D",              OPTION_DEFINE,      OPTION_HAS_ARG  },
    { "I",              OPTION_INCLUDE,     OPTION_HAS_ARG  },
    { "L",              OPTION_KEEP_LOCALS, OPTION_NO_ARG   },
    
    { "f",              OPTION_FORMAT,      OPTION_HAS_ARG  },
    { "l",              OPTION_LISTING,     OPTION_HAS_ARG  },
    { "o",              OPTION_OUTFILE,     OPTION_HAS_ARG  },
    
    { "-keep-locals",   OPTION_KEEP_LOCALS, OPTION_NO_ARG   },
    { "-nowarn",        OPTION_NOWARN,      OPTION_NO_ARG   },
    { "-help",          OPTION_HELP,        OPTION_NO_ARG   },
    { 0,                0,                  0               }

};

static void add_include_path (const char *pathname) {


    const char *in = pathname;
    const char *p;
    
    do {
    
        int c;
        
        CString str;
        cstr_new (&str);
        
        for (p = in; c = *p, c != '\0' && c != PATHSEP[0]; ++p) {
            cstr_ccat (&str, c);
        }
        
        if (str.size) {
        
            cstr_ccat (&str, '\0');
            dynarray_add (&state->inc_paths, &state->nb_inc_paths, xstrdup (str.data));
        
        }
        
        cstr_free (&str);
        in = (p + 1);
    
    } while (*p != '\0');

}

static void print_help (void) {

    if (!program_name) {
        goto _exit;
    }
    
    fprintf (stderr, "Usage: %s [options] asmfile...\n\n", program_name);
    fprintf (stderr, "Options:\n\n");
    
    fprintf (stderr, "    -D MACRO[=str]        Pre-define a macro\n");
    fprintf (stderr, "    -I DIR                Add DIR to search list for .include directives\n");
    fprintf (stderr, "    -L, --keep-locals     Keep local symbols (e.g. starting with `L')\n");
    
    fprintf (stderr, "    -f FORMAT             Create an output file in format FORMAT (default a.out)\n");
    fprintf (stderr, "                              Supported formats are: a.out, coff\n");
    fprintf (stderr, "    -l FILE               Print listings to file FILE\n");
    fprintf (stderr, "    -o OBJFILE            Name the object-file output OBJFILE (default a.out)\n");
    
    fprintf (stderr, "    --nowarn              Suppress warnings\n");
    fprintf (stderr, "    --help                Print this help information\n");
    fprintf (stderr, "\n");
    
_exit:
    
    exit (EXIT_SUCCESS);

}

char *skip_whitespace (char *p) {
    return *p == ' ' ? (p + 1) : p;
}

char *to_lower (const char *str) {

    unsigned long i, len = strlen (str);
    char *new_str;
    
    if ((new_str = malloc (len + 1)) == NULL) {
        return NULL;
    }
    
    for (i = 0; i < len; ++i) {
        new_str[i] = tolower (str[i]);
    }
    
    new_str[len] = '\0';
    return new_str;

}

char *xstrdup (const char *str) {

    char *ptr = xmalloc (strlen (str) + 1);
    strcpy (ptr, str);
    
    return ptr;

}

int strstart (const char *val, const char **str) {

    const char *p = val;
    const char *q = *str;
    
    while (*p != '\0') {
    
        if (*p != *q) {
            return 0;
        }
        
        ++p;
        ++q;
    
    }
    
    *str = q;
    return 1;

}

int xstrcasecmp (const char *s1, const char *s2) {

    const unsigned char *p1 = (const unsigned char *) s1;
    const unsigned char *p2 = (const unsigned char *) s2;
    
    while (*p1 != '\0') {
    
        if (toupper (*p1) < toupper (*p2)) {
            return (-1);
        } else if (toupper (*p1) > toupper (*p2)) {
            return (1);
        }
        
        p1++;
        p2++;
    
    }
    
    if (*p2 == '\0') {
        return (0);
    } else {
        return (-1);
    }

}

int xstrncasecmp (const char *s1, const char *s2, unsigned long n) {

    const unsigned char *p1 = (const unsigned char *) s1;
    const unsigned char *p2 = (const unsigned char *) s2;
    
    while (n-- > 0) {
    
        if (toupper (*p1) < toupper (*p2)) {
            return (-1);
        } else if (toupper (*p1) > toupper (*p2)) {
            return (1);
        }
        
        p1++;
        p2++;
    
    }
    
    if (*p2 == '\0') {
        return (0);
    } else {
        return (-1);
    }

}

void *xmalloc (unsigned long size) {

    void *ptr = malloc (size);
    
    if (ptr == NULL && size) {
    
        report_at (program_name, 0, REPORT_ERROR, "memory full (malloc)");
        exit (EXIT_FAILURE);
    
    }
    
    memset (ptr, 0, size);
    return ptr;

}

void *xrealloc (void *ptr, unsigned long size) {

    void *new_ptr = realloc (ptr, size);
    
    if (new_ptr == NULL && size) {
    
        report_at (program_name, 0, REPORT_ERROR, "memory full (realloc)");
        exit (EXIT_FAILURE);
    
    }
    
    return new_ptr;

}

void dynarray_add (void *ptab, unsigned long *nb_ptr, void *data) {

    int32_t nb, nb_alloc;
    void **pp;
    
    nb = *nb_ptr;
    pp = *(void ***) ptab;
    
    if ((nb & (nb - 1)) == 0) {
    
        if (!nb) {
            nb_alloc = 1;
        } else {
            nb_alloc = nb * 2;
        }
        
        pp = xrealloc (pp, nb_alloc * sizeof (void *));
        *(void ***) ptab = pp;
    
    }
    
    pp[nb++] = data;
    *nb_ptr = nb;

}

void parse_args (int *pargc, char ***pargv, int optind) {

    char **argv = *pargv;
    int argc = *pargc;
    
    struct option *popt;
    const char *optarg, *r;
    
    if (argc == optind) {
        print_help ();
    }
    
    while (optind < argc) {
    
        r = argv[optind++];
        
        if (r[0] != '-' || r[1] == '\0') {
        
            dynarray_add (&state->files, &state->nb_files, xstrdup (r));
            continue;
        
        }
        
        for (popt = opts; popt; ++popt) {
        
            const char *p1 = popt->name;
            const char *r1 = (r + 1);
            
            if (!p1) {
            
                report_at (program_name, 0, REPORT_ERROR, "invalid option -- '%s'", r);
                exit (EXIT_FAILURE);
            
            }
            
            if (!strstart (p1, &r1)) {
                continue;
            }
            
            optarg = r1;
            
            if (popt->flags & OPTION_HAS_ARG) {
            
                if (*optarg == '\0') {
                
                    if (optind >= argc) {
                    
                        report_at (program_name, 0, REPORT_ERROR, "argument to '%s' is missing", r);
                        exit (EXIT_FAILURE);
                    
                    }
                    
                    optarg = argv[optind++];
                
                }
            
            } else if (*optarg != '\0') {
                continue;
            }
            
            break;
        
        }
        
        switch (popt->index) {
        
            case OPTION_DEFINE: {
            
                dynarray_add (&state->defs, &state->nb_defs, xstrdup (optarg));
                break;
            
            }
            
            case OPTION_FORMAT: {
            
                char *format = to_lower (optarg);
                
                if (strcmp (format, "a.out") == 0) {
                    state->format = format;
                } else if (strcmp (format, "coff") == 0) {
                    state->format = format;
                } else {
                
                    report_at (program_name, 0, REPORT_ERROR, "unsupported format '%s' specified", optarg);
                    exit (EXIT_FAILURE);
                
                }
                
                break;
            
            }
            
            case OPTION_HELP: {
            
                print_help ();
                break;
            
            }
            
            case OPTION_INCLUDE: {
            
                add_include_path (optarg);
                break;
            
            }
            
            case OPTION_KEEP_LOCALS: {
            
                state->keep_locals = 1;
                break;
            
            }
            
            case OPTION_LISTING: {
            
                if (state->listing) {
                
                    report_at (program_name, 0, REPORT_ERROR, "multiple listing files provided");
                    exit (EXIT_FAILURE);
                
                }
                
                state->listing = xstrdup (optarg);
                break;
            
            }
            
            case OPTION_NOWARN: {
            
                state->nowarn = 1;
                break;
            
            }
            
            case OPTION_OUTFILE: {
            
                if (state->outfile) {
                
                    report_at (program_name, 0, REPORT_ERROR, "multiple output files provided");
                    exit (EXIT_FAILURE);
                
                }
                
                state->outfile = xstrdup (optarg);
                break;
            
            }
            
            default: {
            
                report_at (program_name, 0, REPORT_ERROR, "unsupported option '%s'", r);
                exit (EXIT_FAILURE);
            
            }
        
        }
    
    }
    
    if (!state->format) { state->format = "a.out"; }
    if (!state->outfile) { state->outfile = "a.out"; }

}
