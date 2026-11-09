#ifndef GMU_CONSOLE_H
#define GMU_CONSOLE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* libogc's text console, drawn into the screen framebuffer (console.c). stdout goes to it. */
void con_init(void);
void con_write(const char *s, size_t len);
void con_clear(void);
int  con_cols(void);
int  con_rows(void);
/* The 24 colours (normal, bright, dim; YUY2 pixel pairs) that themes rewrite. */
uint32_t *con_color_table(void);
void con_reset_colors(void);

#endif
