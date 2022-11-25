/******************************************************************************
 * @file            as.c
 *****************************************************************************/
#include    <stddef.h>
#include    <stdio.h>
#include    <stdlib.h>
#include    <string.h>

#include    "aout.h"
#include    "as.h"
#include    "coff.h"
#include    "intel.h"
#include    "lib.h"
#include    "listing.h"
#include    "macro.h"
#include    "process.h"
#include    "pseudo_ops.h"
#include    "report.h"
#include    "section.h"
#include    "write.h"

static struct object_format obj_fmts[] = {

    { "a.out",      install_aout_pseudo_ops,    aout_adjust_code,   aout_write_object   },
    { "coff",       install_coff_pseudo_ops,    0,                  coff_write_object   }

};

static struct object_format *obj_fmt = 0;

struct as_state *state = 0;
const char *program_name = 0;

static void handle_defines (void) {

    unsigned long i;
    
    for (i = 0; i < state->nb_defs; ++i) {
    
        char *p1 = state->defs[i];
        char *p2 = p1;
        
        while (*p2 && *p2 != '=') {
            ++(p2);
        }
        
        if (*p2 == '=') {
            *(p2)++ = ' ';
        }
        
        handler_define (&p1);
    
    }
    
    if (get_error_count () > 0) {
        exit (EXIT_FAILURE);
    }

}

int main (int argc, char **argv) {

    unsigned long i;
    
    if (argc && *argv) {
    
        char *p;
        program_name = *argv;
        
        if ((p = strrchr (program_name, '/'))) {
            program_name = (p + 1);
        }
    
    }
    
    state = xmalloc (sizeof (*state));
    parse_args (&argc, &argv, 1);
    
    if (state->nb_files == 0) {
    
        report_at (program_name, 0, REPORT_ERROR, "no input files provided");
        return EXIT_FAILURE;
    
    }
    
    for (i = 0; i < ARRAY_SIZE (obj_fmts); ++i) {
    
        if (xstrcasecmp (obj_fmts[i].name, state->format) == 0) {
        
            obj_fmt = &obj_fmts[i];
            break;
        
        }
    
    }
    
    if (!obj_fmt) {
    
        report_at (program_name, 0, REPORT_INTERNAL_ERROR, "failed to obtain format %s", state->format);
        return EXIT_FAILURE;
    
    }
    
    obj_fmt->install_pseudo_ops ();
    
    machine_dependent_init ();
    pseudo_ops_init ();
    
    if (get_error_count () > 0) {
        return EXIT_FAILURE;
    }
    
    sections_init ();
    handle_defines ();
    
    for (i = 0; i < state->nb_files; ++i) {
    
        if (process_file (state->files[i])) {
        
            report_at (NULL, 0, REPORT_ERROR, "failed to open '%s' for reading", state->files[i]);
            continue;
        
        }
    
    }
    
    write_object_file (obj_fmt);
    generate_listing ();
    
    if (get_error_count () > 0) {
    
        remove (state->outfile);
        return EXIT_FAILURE;
    
    }
    
    return EXIT_SUCCESS;

}
