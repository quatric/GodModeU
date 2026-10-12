#ifndef UI_H
#define UI_H

#include <stdbool.h>
#include "gmu_types.h"

enum { C_BLACK, C_RED, C_GREEN, C_YELLOW, C_BLUE, C_MAGENTA, C_CYAN, C_WHITE };

void ui_init(void);
void ui_shutdown(void);
int  ui_cols(void);
int  ui_rows(void);

void ui_clear(void);
void ui_goto(int row, int col);
void ui_color(int fg, bool bold);   /* no-op: text is white on the TV */
void ui_bg(int bg);                 /* background colour for what is drawn next */
void ui_reset(void);

/* Text output at the cursor set by ui_goto (replaces printf / putchar). */
void ui_putc(int c);
void ui_puts(const char *s);
void ui_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/* Prints exactly `width` columns at row/col (truncated or padded). */
void ui_field(int row, int col, int width, const char *fmt, ...);
void ui_fill(int row, int col, int width, char c);
void ui_bar(int row, int col, int width, int pct, int color);
void ui_fmt_size(char *out, size_t n, u64 bytes);

/* Shows the grid as drawn so far. Input polling does this on its own. */
void ui_present(void);

/* Dialogs. All of them block and leave the screen for the caller to redraw. */
void ui_message(const char *title, int color, const char *text);
bool ui_confirm(const char *title, int color, const char *text);
#define UI_MENU_HOME (-2)      /* HOME pressed inside a menu (B / cancel is -1) */
int  ui_menu(const char *title, const char **items, const bool *enabled, int n, int start);
int  ui_menu_ex(const char *title, const char **items, const bool *enabled, int n, int start, const char *strip);
bool ui_text_input(const char *title, char *buf, int maxlen);

void ui_splash(const char *status);

#endif
