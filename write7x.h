/******************************************************************************
 * @file            write7x.h
 *****************************************************************************/
#ifndef     _WRITE7X_H
#define     _WRITE7X_H

#include    "stdint.h"

void write721_to_byte_array (unsigned char *dest, uint16_t val);
void write741_to_byte_array (unsigned char *dest, uint32_t val);

#endif      /* _WRITE7X_H */
