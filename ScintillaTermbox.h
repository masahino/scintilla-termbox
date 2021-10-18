#ifndef SCINTILLATERMBOX_H
#define SCINTILLATERMBOX_H

#include <termbox.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Creates a new Scintilla window.
 * Curses does not have to be initialized before calling this function.
 * @param callback A callback function for Scintilla notifications.
 * @param userdata Userdata to pass to *callback*.
 */
void *scintilla_new(
  void (*callback)(void *sci, int iMessage, SCNotification *n, void *userdata), void *userdata);
/**
 * Sends the given message with parameters to the given Scintilla window.
 * Curses does not have to be initialized before calling this function.
 * @param sci The Scintilla window returned by `scintilla_new()`.
 * @param iMessage The message ID.
 * @param wParam The first parameter.
 * @param lParam The second parameter.
 */
sptr_t scintilla_send_message(void *sci, unsigned int iMessage, uptr_t wParam, sptr_t lParam);
/**
 * Deletes the given Scintilla window.
 * Curses must have been initialized prior to calling this function.
 * @param sci The Scintilla window returned by `scintilla_new()`.
 */
/**
 * Sends the specified key to the given Scintilla window for processing.
 * If it is not consumed, an SCNotification will be emitted.
 * Curses does not have to be initialized before calling this function.
 * @param sci The Scintilla window returned by `scintilla_new()`.
 * @param key The keycode of the key.
 * @param shift Flag indicating whether or not the shift modifier key is pressed.
 * @param ctrl Flag indicating whether or not the control modifier key is pressed.
 * @param alt Flag indicating whether or not the alt modifier key is pressed.
 */
void scintilla_send_key(void *sci, int key, bool shift, bool ctrl, bool alt);
/**
 * Refreshes the Scintilla window on the physical screen.
 * This should be done along with the normal curses `refresh()`, as the physical screen is
 * updated when calling this function.
 * Curses must have been initialized prior to calling this function.
 * @param sci The Scintilla window returned by `scintilla_new()`.
 */
void scintilla_refresh(void *sci);
/**
 * Deletes the given Scintilla window.
 * Curses must have been initialized prior to calling this function.
 * @param sci The Scintilla window returned by `scintilla_new()`.
 */
void scintilla_delete(void *sci);
/**
 * Resize Scintilla window.
 */
void scintilla_resize(void *sci, int width, int height);

#define IMAGE_MAX 31

#ifdef __cplusplus
}
#endif

#endif