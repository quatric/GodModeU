#ifndef GMU_SWKBD_H
#define GMU_SWKBD_H

#ifdef __cplusplus
extern "C" {
#endif

/* The Wii U's system software keyboard over the GamePad for one line of text: buf (UTF-8, at most maxlen
 * bytes, and the starting text) is replaced on OK. hint is shown in the empty field.
 * 1 OK, 0 cancelled, -1 the keyboard couldn't be started (use the character roller instead). */
int swkbd_input(const char *hint, char *buf, int maxlen);

#ifdef __cplusplus
}
#endif

#endif
