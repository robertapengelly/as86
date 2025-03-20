/******************************************************************************
 * @file            expr.h
 *****************************************************************************/
#ifndef     _EXPR_H
#define     _EXPR_H


#define current_location curloc


enum expr_type {

    EXPR_TYPE_INVALID,
    EXPR_TYPE_ABSENT,
    EXPR_TYPE_CONSTANT,
    EXPR_TYPE_SYMBOL,
    EXPR_TYPE_SYMBOL_RVA,
    EXPR_TYPE_REGISTER,
    EXPR_TYPE_INDEX,
    EXPR_TYPE_LOGICAL_OR,
    EXPR_TYPE_LOGICAL_AND,
    EXPR_TYPE_EQUAL,
    EXPR_TYPE_NOT_EQUAL,
    EXPR_TYPE_LESSER,
    EXPR_TYPE_LESSER_EQUAL,
    EXPR_TYPE_GREATER,
    EXPR_TYPE_GREATER_EQUAL,
    EXPR_TYPE_ADD,
    EXPR_TYPE_SUBTRACT,
    EXPR_TYPE_BIT_INCLUSIVE_OR,
    EXPR_TYPE_BIT_EXCLUSIVE_OR,
    EXPR_TYPE_BIT_AND,
    EXPR_TYPE_MULTIPLY,
    EXPR_TYPE_DIVIDE,
    EXPR_TYPE_MODULUS,
    EXPR_TYPE_LEFT_SHIFT,
    EXPR_TYPE_RIGHT_SHIFT,
    EXPR_TYPE_LOGICAL_NOT,
    EXPR_TYPE_BIT_NOT,
    EXPR_TYPE_UNARY_MINUS,
    
    /* Machine dependent operators. */
    EXPR_TYPE_MACHINE_DEPENDENT_0,
    EXPR_TYPE_MACHINE_DEPENDENT_1,
    EXPR_TYPE_MACHINE_DEPENDENT_2,
    EXPR_TYPE_MACHINE_DEPENDENT_3,
    EXPR_TYPE_MACHINE_DEPENDENT_4,
    EXPR_TYPE_MACHINE_DEPENDENT_5,
    EXPR_TYPE_MACHINE_DEPENDENT_6,
    EXPR_TYPE_MACHINE_DEPENDENT_7,
    EXPR_TYPE_MACHINE_DEPENDENT_8,
    EXPR_TYPE_MACHINE_DEPENDENT_9,
    EXPR_TYPE_MACHINE_DEPENDENT_10,
    
    /* How many expression types exist. */
    EXPR_TYPE_MAX

};

enum expr_mode {

    expr_mode_normal,
    expr_mode_evaluate

};

#include    "types.h"

struct expr {

    enum expr_type type;
    
    symbol_t add_symbol, op_symbol;
    value_t add_number;

};

#define     expression_read_into(pp, expr)                  (read_into ((pp), (expr), 0, expr_mode_normal))
#define     expression_evaluate_and_read_into(pp, expr)     (read_into ((pp), (expr), 0, expr_mode_evaluate))

section_t current_location (struct expr *expr);
section_t read_into (char **pp, struct expr *expr, uint32_t rank, enum expr_mode expr_mode);

symbol_t make_expr_symbol (struct expr *expr);

int expr_symbol_get_filename_and_line_number (struct symbol *symbol, const char **filename_p, unsigned long *line_number_p);
int resolve_expression (struct expr *expr);

offset_t absolute_expression_read_into (char **pp, struct expr *expr);
offset_t get_result_of_absolute_expression (char **pp);

void expr_type_set_rank (enum expr_type expr_type, uint32_t rank);

#endif      /* _EXPR_H */
