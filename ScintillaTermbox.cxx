// ScintillaTermbox.cxx is based on scinterm

// ScintillaCurses.cxx
// Copyright 2012-2021 Mitchell. See LICENSE.
// Scintilla implemented in a curses (terminal) environment.
// Contains platform facilities and a curses-specific subclass of ScintillaBase.
// Note: setlocale(LC_CTYPE, "") must be called before initializing curses in order to display
// UTF-8 characters properly in ncursesw.

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <assert.h>
#include <wchar.h>

#include <stdexcept>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <optional>
#include <algorithm>
#include <memory>
#include <chrono>

#include "ScintillaTypes.h"
#include "ScintillaMessages.h"
#include "ScintillaStructures.h"
#include "ILoader.h"
#include "ILexer.h"

#include "Debugging.h"
#include "Geometry.h"
#include "Platform.h"

#include "Scintilla.h"
#include "CharacterCategoryMap.h"
#include "Position.h"
#include "UniqueString.h"
#include "SplitVector.h"
#include "Partitioning.h"
#include "RunStyles.h"
#include "ContractionState.h"
#include "CellBuffer.h"
#include "CallTip.h"
#include "KeyMap.h"
#include "Indicator.h"
#include "LineMarker.h"
#include "Style.h"
#include "ViewStyle.h"
#include "CharClassify.h"
#include "Decoration.h"
#include "CaseFolder.h"
#include "Document.h"
#include "UniConversion.h"
#include "Selection.h"
#include "PositionCache.h"
#include "EditModel.h"
#include "MarginView.h"
#include "EditView.h"
#include "Editor.h"
#include "AutoComplete.h"
#include "ScintillaBase.h"

#include "ScintillaTermbox.h"
#include "PlatTermbox.h"

namespace Scintilla::Internal {

namespace {

/** Custom function for drawing line markers in curses. */
void DrawLineMarker(Surface *surface, const PRectangle &rcWhole,
  const Font *fontForCharacter, int tFold, MarginType marginStyle, const void *data) {
  reinterpret_cast<SurfaceImpl *>(surface)->DrawLineMarker(rcWhole, fontForCharacter, tFold, data);
}
/** Custom function for drawing wrap markers in curses. */
void DrawWrapVisualMarker(
  Surface *surface, PRectangle rcPlace, bool isEndMarker, ColourRGBA wrapColour) {
  reinterpret_cast<SurfaceImpl *>(surface)->DrawWrapMarker(rcPlace, isEndMarker, wrapColour);
}
/** Custom function for drawing tab arrows in curses. */
void DrawTabArrow(
  Surface *surface, PRectangle rcTab, int ymid, const ViewStyle &vsDraw, Stroke stroke) {
  reinterpret_cast<SurfaceImpl *>(surface)->DrawTabArrow(rcTab, vsDraw);
}

 /**
   * Uses the given UTF-8 code point to fill the given UTF-8 byte sequence and length.
   * This algorithm was inspired by Paul Evans' libtermkey.
   * (http://www.leonerd.org.uk/code/libtermkey)
   * @param code The UTF-8 code point.
   * @param s The string to write the UTF-8 byte sequence in. Must be at least 6 bytes in size.
   * @param len The integer to put the number of UTF-8 bytes written in.
   */
  void toutf8(int code, char *s, int *len) {
    if (code < 0x80)
      *len = 1;
    else if (code < 0x800)
      *len = 2;
    else if (code < 0x10000)
      *len = 3;
    else if (code < 0x200000)
      *len = 4;
    else if (code < 0x4000000)
      *len = 5;
    else
      *len = 6;
    for (int b = *len - 1; b > 0; b--) s[b] = 0x80 | (code & 0x3F), code >>= 6;
    if (*len == 1)
      s[0] = code & 0x7F;
    else if (*len == 2)
      s[0] = 0xC0 | (code & 0x1F);
    else if (*len == 3)
      s[0] = 0xE0 | (code & 0x0F);
    else if (*len == 4)
      s[0] = 0xF0 | (code & 0x07);
    else if (*len == 5)
      s[0] = 0xF8 | (code & 0x03);
    else if (*len == 6)
      s[0] = 0xFC | (code & 0x01);
  }

  } // namespace

/** Implementation of Scintilla for termbox. */
class ScintillaTermbox : public ScintillaBase {
  std::unique_ptr<Surface> sur; // window surface to draw on
  int width = 0, height = 0; // window dimensions
  void (*callback)(void *, int, SCNotification *, void *); // SCNotification cb
  void *userdata; // userdata for SCNotification callbacks
  int scrollBarVPos, scrollBarHPos; // positions of the scroll bars
  int scrollBarHeight = 1, scrollBarWidth = 1; // scroll bar height and width
  SelectionText clipboard; // current clipboard text
  bool capturedMouse; // whether or not the mouse is currently captured
  unsigned int autoCompleteLastClickTime; // last click time in the AC box
  bool draggingVScrollBar, draggingHScrollBar; // a scrollbar is being dragged
  int dragOffset; // the distance to the position of the scrollbar being dragged

public:
  ScintillaTermbox(void (*callback_)(void *, int, SCNotification *, void *), void *userdata_);
    virtual ~ScintillaTermbox() override;

    private:
  void Initialise() override;

  void StartDrag() override;

  void SetVerticalScrollPos() override;
  void SetHorizontalScrollPos() override;
  bool ModifyScrollBars(Sci::Line nMax, Sci::Line nPage) override;

  void Copy() override;
  void Paste() override;
  void ClaimSelection() override;
  
  void NotifyChange() override;
  void NotifyParent(NotificationData scn) override;

  int KeyDefault(Keys key, KeyMod modifiers) override;

  void CopyToClipboard(const SelectionText &selectedText) override;

  bool FineTickerRunning(TickReason reason) override;
  void FineTickerStart(TickReason reason, int millis, int tolerance) override;
  void FineTickerCancel(TickReason reason) override;

  void SetMouseCapture(bool on) override;
  bool HaveMouseCapture() override;

  std::string UTF8FromEncoded(std::string_view encoded) const override;
  std::string EncodedFromUTF8(std::string_view utf8) const override;

  sptr_t DefWndProc(Message iMessage, uptr_t wParam, sptr_t lParam) override;

  void CreateCallTipWindow(PRectangle rc) override;

  void AddToPopUp(const char *label, int cmd = 0, bool enabled = true) override;

  public:
  sptr_t WndProc(Message iMessage, uptr_t wParam, sptr_t lParam) override;

  /**
   * Returns the curses `WINDOW` associated with this Scintilla instance.
   * If the `WINDOW` has not been created yet, create it now.
   */
  TermboxWin *GetWINDOW();

  void UpdateCursor();

  void Refresh();

  void KeyPress(int key, bool shift, bool ctrl, bool alt);

  bool MousePress(int button, int y, int x, bool shift, bool ctrl, bool alt);
  bool MouseMove(int y, int x, bool shift, bool ctrl, bool alt);
  void MouseRelease(int y, int x, int ctrl);

  char *GetClipboard(int *len);

  void Resize(int width, int height);

  void Move(int new_x, int new_y);
};

  /**
   * Creates a new Scintilla instance in a curses `WINDOW`.
   * However, the `WINDOW` itself will not be created until it is absolutely necessary. When the
   * `WINDOW` is created, it will initially be full-screen.
   * @param callback_ Callback function for Scintilla notifications.
   */
  ScintillaTermbox::ScintillaTermbox(void (*callback_)(void *, int, SCNotification *, void *), void *userdata_)
      : sur(Surface::Allocate(Technology::Default)), callback(callback_), userdata(userdata_) {
    // Defaults for curses.
    marginView.wrapMarkerPaddingRight = 0; // no padding for margin wrap markers
    marginView.customDrawWrapMarker = DrawWrapVisualMarker; // draw text markers
    view.tabWidthMinimumPixels = 0; // no proportional fonts
    view.drawOverstrikeCaret = false; // always draw normal caret
    view.bufferedDraw = false; // draw directly to the screen
    view.tabArrowHeight = 0; // no additional tab arrow height
    view.customDrawTabArrow = DrawTabArrow; // draw text arrows for tabs
    view.customDrawWrapMarker = DrawWrapVisualMarker; // draw text wrap markers
    mouseSelectionRectangularSwitch = true; // easier rectangular selection
    doubleClickCloseThreshold = Point(0, 0); // double-clicks only in same cell
    horizontalScrollBarVisible = false; // no horizontal scroll bar
    scrollWidth = 5 * width; // reasonable default for any horizontal scroll bar
    vs.SetElementRGB(Element::SelectionText, 0x000000); // black on white selection
    vs.SetElementRGB(Element::SelectionAdditionalText, 0x000000);
    vs.SetElementRGB(Element::SelectionAdditionalBack, 0xFFFFFF);
    vs.SetElementRGB(Element::Caret, 0xFFFFFF); // white caret
    //    vs.caret.style = CaretStyle::Curses; // block carets
    vs.leftMarginWidth = 0, vs.rightMarginWidth = 0; // no margins
    vs.ms[1].width = 2; // marker margin width should be 1
    vs.extraDescent = -1; // hack to make lineHeight 1 instead of 2
    // Set default marker foreground and background colors.
    for (int i = 0; i <= MARKER_MAX; i++) {
      vs.markers[i].fore = ColourRGBA(0xC0, 0xC0, 0xC0);
      vs.markers[i].back = ColourRGBA(0, 0, 0);
      if (i >= 25) vs.markers[i].markType = MarkerSymbol::Empty;
      vs.markers[i].customDraw = DrawLineMarker;
    }
    // Use '+' and '-' fold markers.
    vs.markers[static_cast<int>(MarkerOutline::FolderOpen)].markType = MarkerSymbol::BoxMinus;
    vs.markers[static_cast<int>(MarkerOutline::Folder)].markType = MarkerSymbol::BoxPlus;
    vs.markers[static_cast<int>(MarkerOutline::FolderOpenMid)].markType = MarkerSymbol::BoxMinus;
    vs.markers[static_cast<int>(MarkerOutline::FolderEnd)].markType = MarkerSymbol::BoxPlus;
    vs.markers[static_cast<int>(MarkerOutline::FolderSub)].markType = MarkerSymbol::VLine;
    vs.markers[static_cast<int>(MarkerOutline::FolderTail)].markType = MarkerSymbol::LCorner;
    vs.markers[static_cast<int>(MarkerOutline::FolderMidTail)].markType = MarkerSymbol::TCorner;
    displayPopupMenu = PopUp::Never; // no context menu
    vs.marginNumberPadding = 0; // no number margin padding
    vs.ctrlCharPadding = 0; // no ctrl character text blob padding
    vs.lastSegItalicsOffset = 0; // no offset for italic characters at EOLs
    ac.widthLBDefault = 10; // more sane bound for autocomplete width
    ac.heightLBDefault = 10; // more sane bound for autocomplete  height
    ct.colourBG = ColourRGBA(0xff, 0xff, 0xc6); // background color
    ct.colourUnSel = ColourRGBA(0x0, 0x0, 0x0); // black text
    ct.insetX = 2; // border and arrow widths are 1 each
    ct.widthArrow = 1; // arrow width is 1 character
    ct.borderHeight = 1; // no extra empty lines in border height
    ct.verticalOffset = 0; // no extra offset of calltip from line

    // initialization code for Termbox
    height = tb_height();
    width = tb_width();
//    wMain = tb_cell_buffer();
    wMain = new TermboxWin(0, 0, width - 1, height - 1);
    if (sur) sur->Init(wMain.GetID());
    InvalidateStyleRedraw(); // needed to fully initialize Scintilla
  }
  /** Deletes the Scintilla instance. */
  ScintillaTermbox::~ScintillaTermbox() {
  }
  /** Initializing code is unnecessary. */
  void ScintillaTermbox::Initialise() { }
  /** Disable drag and drop since it is not implemented. */
  void ScintillaTermbox::StartDrag() {
   inDragDrop = DragDrop::none;
    SetDragPosition(SelectionPosition(Sci::invalidPosition));
  }
  /** Draws the vertical scroll bar. */
  void ScintillaTermbox::SetVerticalScrollPos() {
    if (!verticalScrollBarVisible) return;
    int maxy = reinterpret_cast<TermboxWin *>(wMain.GetID())->Height();
    int maxx = reinterpret_cast<TermboxWin *>(wMain.GetID())->Width();
    int left = reinterpret_cast<TermboxWin *>(wMain.GetID())->left;
    int top = reinterpret_cast<TermboxWin *>(wMain.GetID())->top;
    // Draw the gutter.
    for (int i = 0; i < maxy; i++) tb_change_cell(left + maxx - 1, top + i, ' ', 0x282828, 0x282828);
    // Draw the bar.
    scrollBarVPos = static_cast<float>(topLine) / (MaxScrollPos() + LinesOnScreen() - 1) * maxy;
    for (int i = scrollBarVPos; i < scrollBarVPos + scrollBarHeight; i++)
      tb_change_cell(left + maxx - 1, top + i, ' ', 0xd8d8d8, 0xd8d8d8);
  }
  /** Draws the horizontal scroll bar. */
  void ScintillaTermbox::SetHorizontalScrollPos() {
    if (!horizontalScrollBarVisible) return;
    int maxy = reinterpret_cast<TermboxWin *>(wMain.GetID())->Height();
    int maxx = reinterpret_cast<TermboxWin *>(wMain.GetID())->Width();
    int left = reinterpret_cast<TermboxWin *>(wMain.GetID())->left;
    int top = reinterpret_cast<TermboxWin *>(wMain.GetID())->top;
    // Draw the gutter.
//    wattr_set(w, 0, term_color_pair(COLOR_WHITE, COLOR_BLACK), nullptr);
    for (int i = 0; i < maxx; i++) tb_change_cell(left + i, top + maxy - 1, ' ', 0x282828, 0x282828);
    // Draw the bar.
    scrollBarHPos = static_cast<float>(xOffset) / scrollWidth * maxx;
//    wattr_set(w, 0, term_color_pair(COLOR_BLACK, COLOR_WHITE), nullptr);
    for (int i = scrollBarHPos; i < scrollBarHPos + scrollBarWidth; i++)
      tb_change_cell(left + i, top + maxy - 1, ' ', 0xd8d8d8, 0xd8d8d8);
  }
  /**
   * Sets the height of the vertical scroll bar and width of the horizontal scroll bar.
   * The height is based on the given size of a page and the total number of pages. The width
   * is based on the width of the view and the view's scroll width property.
   */
  bool ScintillaTermbox::ModifyScrollBars(Sci::Line nMax, Sci::Line nPage) {
    int maxy = reinterpret_cast<TermboxWin *>(wMain.GetID())->Height();
    int maxx = reinterpret_cast<TermboxWin *>(wMain.GetID())->Width();
    int height = roundf(static_cast<float>(nPage) / nMax * maxy);
    scrollBarHeight = std::clamp(height, 1, maxy);
    int width = roundf(static_cast<float>(maxx) / scrollWidth * maxx);
    scrollBarWidth = std::clamp(width, 1, maxx);
    return true;
  }
  /**
   * Copies the selected text to the internal clipboard.
   * The primary and secondary X selections are unaffected.
   */
  void ScintillaTermbox::Copy() {
    if (!sel.Empty()) CopySelectionRange(&clipboard);
  }
  /** Pastes text from the internal clipboard, not from primary or secondary X selections. */
  void ScintillaTermbox::Paste() {
    if (clipboard.Empty()) return;
    ClearSelection(multiPasteMode == MultiPaste::Each);
    InsertPasteShape(clipboard.Data(), static_cast<int>(clipboard.Length()),
      !clipboard.rectangular ? PasteShape::stream : PasteShape::rectangular);
    EnsureCaretVisible();
  }
  /** Setting of the primary and/or secondary X selections is not supported. */
  void ScintillaTermbox::ClaimSelection() {}
  /** Notifying the parent of text changes is not yet supported. */
  void ScintillaTermbox::NotifyChange() {}
  /** Send Scintilla notifications to the parent. */
  void ScintillaTermbox::NotifyParent(NotificationData scn) {
    if (callback)
      (*callback)(
        reinterpret_cast<void *>(this), 0, reinterpret_cast<SCNotification *>(&scn), userdata);
  }
  /**
   * Handles an unconsumed key.
   * If a character is being typed, add it to the editor. Otherwise, notify the container.
   */
  int ScintillaTermbox::KeyDefault(Keys key, KeyMod modifiers) {
    if ((IsUnicodeMode() || static_cast<int>(key) < 256) && modifiers == KeyMod::Norm) {
      if (IsUnicodeMode()) {
        char utf8[6];
        int len;
        toutf8(static_cast<int>(key), utf8, &len);
        InsertCharacter(std::string(utf8, len), CharacterSource::DirectInput);
        return 1;
      } else {
        char ch = static_cast<char>(key);
        InsertCharacter(std::string(&ch, 1), CharacterSource::DirectInput);
        return 1;
      }
    } else {
      NotificationData scn = {};
      scn.nmhdr.code = Notification::Key;
      scn.ch = static_cast<int>(key);
      scn.modifiers = modifiers;
      return (NotifyParent(scn), 0);
    }
  }
  /**
   * Copies the given text to the internal clipboard.
   * Like `Copy()`, does not affect the primary and secondary X selections.
   */
  void ScintillaTermbox::CopyToClipboard(const SelectionText &selectedText) { clipboard.Copy(selectedText); }
  /** A ticking caret is not implemented. */
  bool ScintillaTermbox::FineTickerRunning(TickReason reason) { return false; }
  /** A ticking caret is not implemented. */
  void ScintillaTermbox::FineTickerStart(TickReason reason, int millis, int tolerance) {}
  /** A ticking caret is not implemented. */
  void ScintillaTermbox::FineTickerCancel(TickReason reason) {}
  /**
   * Sets whether or not the mouse is captured.
   * This is used by Scintilla to handle mouse clicks, drags, and releases.
   */
  void ScintillaTermbox::SetMouseCapture(bool on) { capturedMouse = on; }
  /** Returns whether or not the mouse is captured. */
  bool ScintillaTermbox::HaveMouseCapture() { return capturedMouse; }
  /** All text is assumed to be in UTF-8. */
  std::string ScintillaTermbox::UTF8FromEncoded(std::string_view encoded) const {
    return std::string(encoded);
  }
  /** All text is assumed to be in UTF-8. */
  std::string ScintillaTermbox::EncodedFromUTF8(std::string_view utf8) const { return std::string(utf8); }
  /** A Scintilla direct pointer is not implemented. */
  sptr_t ScintillaTermbox::DefWndProc(Message iMessage, uptr_t wParam, sptr_t lParam) { return 0; }
  /** Draws a CallTip, creating the curses window for it if necessary. */
  void ScintillaTermbox::CreateCallTipWindow(PRectangle rc) {
   if (!wMain.GetID()) return;
    if (!ct.wCallTip.Created()) {
      rc.right -= 1; // remove right-side padding
      int begx = 0, begy = 0, maxx = 0, maxy = 0;
      begx = GetWINDOW()->left;
      begy = GetWINDOW()->top;
      int xoffset = begx - rc.left, yoffset = begy - rc.top;
      if (xoffset > 0) rc.left += xoffset, rc.right += xoffset;
      if (yoffset > 0) rc.top += yoffset, rc.bottom += yoffset;
      maxx = GetWINDOW()->Width();
      maxy = GetWINDOW()->Height();
      if (rc.Width() > maxx) rc.right = rc.left + maxx - 1;
      if (rc.Height() > maxy) rc.bottom = rc.top + maxy - 1;
      ct.wCallTip = new TermboxWin(rc.left, rc.top, rc.right, rc.bottom);
    }
    WindowID wid = ct.wCallTip.GetID();
    std::unique_ptr<Surface> sur = Surface::Allocate(Technology::Default);
    if (sur) {
      sur->Init(wid);
      dynamic_cast<SurfaceImpl *>(sur.get())->isCallTip = true;
      TermboxWin *w = reinterpret_cast<TermboxWin *>(ct.wCallTip.GetID());
      int bg = (ct.colourBG.GetRed() << 16) + (ct.colourBG.GetGreen() << 8)  + (ct.colourBG.GetBlue());
      for (int y = w->top; y < w->bottom; y++) {
        for (int x = w->left; x < w->right; x++) {
          tb_change_cell(x, y, ' ', bg, bg);
        }
      }
      ct.PaintCT(sur.get());
      tb_present();
    }
  }
  /** Adding menu items to the popup menu is not implemented. */
  void ScintillaTermbox::AddToPopUp(const char *label, int cmd, bool enabled) {}
  /**
   * Sends the given message and parameters to Scintilla unless it is a message that changes
   * an unsupported property.
   */
  sptr_t ScintillaTermbox::WndProc(Message iMessage, uptr_t wParam, sptr_t lParam) {
    try {
      switch (iMessage) {
      case Message::GetDirectFunction: return reinterpret_cast<sptr_t>(scintilla_send_message);
      case Message::GetDirectPointer: return reinterpret_cast<sptr_t>(this);
      // Ignore attempted changes of the following unsupported properties.
      case Message::SetBufferedDraw:
      case Message::SetWhitespaceSize:
      case Message::SetPhasesDraw:
      case Message::SetExtraAscent:
      case Message::SetExtraDescent: return 0;
      // Pass to Scintilla.
      default: return ScintillaBase::WndProc(iMessage, wParam, lParam);
      }
    } catch (std::bad_alloc &) {
      errorStatus = Status::BadAlloc;
    } catch (...) {
      errorStatus = Status::Failure;
    }
    return 0;
  }
  /**
   * Returns the curses `WINDOW` associated with this Scintilla instance.
   * If the `WINDOW` has not been created yet, create it now.
   */
  TermboxWin *ScintillaTermbox::GetWINDOW() {
    return reinterpret_cast<TermboxWin *>(wMain.GetID());
  }
  /**
   * Updates the cursor position, even if it's not visible, as the container may have a use for it.
   */
  void ScintillaTermbox::UpdateCursor() {
    int pos = WndProc(Message::GetCurrentPos, 0, 0);
    if (!WndProc(Message::GetSelectionEmpty, 0, 0) &&
      (WndProc(Message::GetCaretStyle, 0, 0) & static_cast<int>(CaretStyle::BlockAfter)) == 0 &&
      (WndProc(Message::GetCurrentPos, 0, 0) > WndProc(Message::GetAnchor, 0, 0)))
      pos = WndProc(Message::PositionBefore, pos, 0); // draw inside selection
    int y = WndProc(Message::PointYFromPosition, 0, pos);
    int x = WndProc(Message::PointXFromPosition, 0, pos);
#ifdef DEBUG
    fprintf(stderr, "update cursor pos = %d, %d, %d\n", pos, GetWINDOW()->left + x, GetWINDOW()->top + y);
#endif
    tb_set_cursor(GetWINDOW()->left + x, GetWINDOW()->top + y);
    tb_present();
  }
  /**
   * Repaints the Scintilla window on the physical screen.
   * If an autocompletion list, user list, or calltip is active, redraw it over the buffer's
   * contents.
   * To paint to the virtual screen instead, use `NoutRefresh()`.
   * @see NoutRefresh
   */
  void ScintillaTermbox::Refresh() {
    rcPaint.top = 0;
    rcPaint.left = 0; // paint from (0, 0), not (begy, begx)
    rcPaint.bottom = reinterpret_cast<TermboxWin *>(wMain.GetID())->Height();
    rcPaint.right = reinterpret_cast<TermboxWin *>(wMain.GetID())->Width();
    if (rcPaint.bottom != height || rcPaint.right != width) {
      height = rcPaint.bottom;
      width = rcPaint.right;
      ChangeSize();
    }
    Paint(sur.get(), rcPaint);
    SetVerticalScrollPos(), SetHorizontalScrollPos();
    tb_present();
    if (ac.Active())
      ac.lb->Select(ac.lb->GetSelection()); // redraw
    else if (ct.inCallTipMode)
      CreateCallTipWindow(PRectangle(0, 0, 0, 0)); // redraw
    if (hasFocus) UpdateCursor();
  }
  /**
   * Sends a key to Scintilla.
   * Usually if a key is consumed, the screen should be repainted. However, when autocomplete is
   * active, that window is consuming the keys and any repainting of the main Scintilla window
   * will overwrite the autocomplete window.
   * @param key The key pressed.
   * @param shift Flag indicating whether or not the shift modifier key is pressed.
   * @param shift Flag indicating whether or not the control modifier key is pressed.
   * @param shift Flag indicating whether or not the alt modifier key is pressed.
   */
  void ScintillaTermbox::KeyPress(int key, bool shift, bool ctrl, bool alt) {
    KeyDownWithModifiers(static_cast<Keys>(key), ModifierFlags(shift, ctrl, alt), nullptr);
  }
  /**
   * Handles a mouse button press.
   * @param button The button number pressed, or `0` if none.
   * @param y The y coordinate of the mouse event relative to this window.
   * @param x The x coordinate of the mouse event relative to this window.
   * @param shift Flag indicating whether or not the shift modifier key is pressed.
   * @param ctrl Flag indicating whether or not the control modifier key is pressed.
   * @param alt Flag indicating whether or not the alt modifier key is pressed.
   * @return whether or not the mouse event was handled
   */
  bool ScintillaTermbox::MousePress(int button, int y, int x, bool shift, bool ctrl, bool alt) {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    auto time = static_cast<unsigned int>(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
    if (ac.Active() && (button == 1 || button == 4 || button == 5)) {
      // Select an autocompletion list item if possible or scroll the list.
      TermboxWin *w = reinterpret_cast<TermboxWin *>(ac.lb->GetID()), *parent = GetWINDOW();
      int begy = w->top - parent->top; // y is relative to the view
      int begx = w->left - parent->left; // x is relative to the view
      int maxy = w->bottom, maxx = w->right - 1; // ignore border
      int ry = y - begy, rx = x - begx; // relative to list box
      if (ry >= 0 && ry <= maxy && rx > 0 && rx < maxx) {
        if (button == 1) {
          // Select a list item.
          // The currently selected item is normally displayed in the middle.
          int middle = ac.lb->GetVisibleRows() / 2;
          int n = ac.lb->GetSelection(), ny = middle;
          if (n < middle)
            ny = n; // the currently selected item is near the beginning
          else if (n >= ac.lb->Length() - middle)
            ny = (n - 1) % ac.lb->GetVisibleRows(); // it's near the end
          // Compute the index of the item to select.
          int offset = ry - ny; // -1 ignores list box border
          if (offset == 0 && time - autoCompleteLastClickTime < Platform::DoubleClickTime()) {
            ListBoxImpl *listbox = reinterpret_cast<ListBoxImpl *>(ac.lb.get());
            if (listbox->delegate) {
              ListBoxEvent event(ListBoxEvent::EventType::doubleClick);
              listbox->delegate->ListNotify(&event);
            }
          } else
            ac.lb->Select(n + offset);
          autoCompleteLastClickTime = time;
        } else {
          // Scroll the list.
          int n = ac.lb->GetSelection();
          if (button == 4 && n > 0)
            ac.lb->Select(n - 1);
          else if (button == 5 && n < ac.lb->Length() - 1)
            ac.lb->Select(n + 1);
        }
        return true;
      } else if (rx == 0 || rx == maxx)
        return true; // ignore border click
    } else if (ct.inCallTipMode && button == 1) {
      // Send the click to the CallTip.
      TermboxWin *w = reinterpret_cast<TermboxWin *>(ct.wCallTip.GetID()), *parent = GetWINDOW();
      int begy = w->top - parent->top; // y is relative to the view
      int begx = w->left - parent->left; // x is relative to the view
      int maxy = w->bottom - 1, maxx = w->right - 1; // ignore border
      int ry = y - begy, rx = x - begx; // relative to list box
      if (ry >= 0 && ry <= maxy && rx >= 0 && rx <= maxx) {
        ct.MouseClick(Point(rx, ry));
        return (CallTipClick(), true);
      }
    }

    if (button == 1) {
      if (verticalScrollBarVisible && x == GetWINDOW()->right) {
        // Scroll the vertical scrollbar.
        if (y < scrollBarVPos)
          return (ScrollTo(topLine - LinesOnScreen()), true);
        else if (y >= scrollBarVPos + scrollBarHeight)
          return (ScrollTo(topLine + LinesOnScreen()), true);
        else
          draggingVScrollBar = true, dragOffset = y - scrollBarVPos;
      } else if (horizontalScrollBarVisible && y == GetWINDOW()->bottom) {
        // Scroll the horizontal scroll bar.
        if (x < scrollBarHPos)
          return (HorizontalScrollTo(xOffset - GetWINDOW()->right / 2), true);
        else if (x >= scrollBarHPos + scrollBarWidth)
          return (HorizontalScrollTo(xOffset + GetWINDOW()->right / 2), true);
        else
          draggingHScrollBar = true, dragOffset = x - scrollBarHPos;
      } else {
        // Have Scintilla handle the click.
        ButtonDownWithModifiers(Point(x, y), time, ModifierFlags(shift, ctrl, alt));
        return true;
      }
    } else if (button == 4 || button == 5) {
      // Scroll the view.
      int lines = std::max(GetWINDOW()->bottom / 4, 1);
      if (button == 4) lines *= -1;
      return (ScrollTo(topLine + lines), true);
    }
    return false;
  }
  /**
   * Sends a mouse move event to Scintilla, returning whether or not Scintilla handled the
   * mouse event.
   * @param y The y coordinate of the mouse event relative to this window.
   * @param x The x coordinate of the mouse event relative to this window.
   * @param shift Flag indicating whether or not the shift modifier key is pressed.
   * @param ctrl Flag indicating whether or not the control modifier key is pressed.
   * @param alt Flag indicating whether or not the alt modifier key is pressed.
   * @return whether or not Scintilla handled the mouse event
   */
  bool ScintillaTermbox::MouseMove(int y, int x, bool shift, bool ctrl, bool alt) {
    if (!draggingVScrollBar && !draggingHScrollBar) {
      ButtonMoveWithModifiers(Point(x, y), 0, ModifierFlags(shift, ctrl, alt));
    } else if (draggingVScrollBar) {
      int maxy = GetWINDOW()->bottom - scrollBarHeight, pos = y - dragOffset;
      if (pos >= 0 && pos <= maxy) ScrollTo(pos * MaxScrollPos() / maxy);
      return true;
    } else if (draggingHScrollBar) {
      int maxx = GetWINDOW()->right - scrollBarWidth, pos = x - dragOffset;
      if (pos >= 0 && pos <= maxx)
        HorizontalScrollTo(pos * (scrollWidth - maxx - scrollBarWidth) / maxx);
      return true;
    }
    return HaveMouseCapture();
  }
  /**
   * Sends a mouse release event to Scintilla.
   * @param y The y coordinate of the mouse event relative to this window.
   * @param x The x coordinate of the mouse event relative to this window.
   * @param ctrl Flag indicating whether or not the control modifier key is pressed.
   */
  void ScintillaTermbox::MouseRelease(int y, int x, int ctrl) {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    auto time = static_cast<unsigned int>(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
    if (draggingVScrollBar || draggingHScrollBar)
      draggingVScrollBar = false, draggingHScrollBar = false;
    else if (HaveMouseCapture()) {
      ButtonUpWithModifiers(Point(x, y), time, ModifierFlags(ctrl, false, false));
      // TODO: ListBoxEvent event(ListBoxEvent::EventType::selectionChange);
      // TODO: listbox->delegate->ListNotify(&event);
    }
  }
  /**
   * Returns a NUL-terminated copy of the text on the internal clipboard, not the primary and/or
   * secondary X selections.
   * The caller is responsible for `free`ing the returned text.
   * @param len An optional pointer to store the length of the returned text in.
   * @return clipboard text
   */
  char *ScintillaTermbox::GetClipboard(int *len) {
    if (len) *len = clipboard.Length();
    char *text = new char[clipboard.Length() + 1];
    memcpy(text, clipboard.Data(), clipboard.Length() + 1);
    return text;
  }
  /**
   * Resize Scintilla Window.
   */
  void ScintillaTermbox::Resize(int width, int height) {
    reinterpret_cast<TermboxWin *>(wMain.GetID())->right =
    reinterpret_cast<TermboxWin *>(wMain.GetID())->left + width - 1;
    reinterpret_cast<TermboxWin *>(wMain.GetID())->bottom =
    reinterpret_cast<TermboxWin *>(wMain.GetID())->top + height - 1;
    tb_clear();
    Refresh();
  }
  /**
   * Move Scintilla Window.
   */
  void ScintillaTermbox::Move(int new_x, int new_y) {
    reinterpret_cast<TermboxWin *>(wMain.GetID())->Move(new_x, new_y);
    tb_clear();
    Refresh();
  }

  } // namespace Scintilla::Internal

  using ScintillaTermbox = Scintilla::Internal::ScintillaTermbox;
  
// Link with C. Documentation in ScintillaCurses.h.
extern "C" {
  void *scintilla_new(void (*callback)(void *, int, SCNotification *, void *), void *userdata) {
    return reinterpret_cast<void *>(new ScintillaTermbox(callback, userdata));
  }
  sptr_t scintilla_send_message(void *sci, unsigned int iMessage, uptr_t wParam, sptr_t lParam) {
    return reinterpret_cast<ScintillaTermbox *>(sci)->WndProc(
      static_cast<Scintilla::Message>(iMessage), wParam, lParam);
  }
  void scintilla_send_key(void *sci, int key, bool shift, bool ctrl, bool alt) {
    reinterpret_cast<ScintillaTermbox *>(sci)->KeyPress(key, shift, ctrl, alt);
  }
bool scintilla_send_mouse(void *sci, int event, int button, int y, int x,
  bool shift, bool ctrl, bool alt) {
  ScintillaTermbox *scitermbox = reinterpret_cast<ScintillaTermbox *>(sci);
  Scintilla::Internal::TermboxWin *w = scitermbox->GetWINDOW();
  int begy = w->top, begx = w->left;
  int maxy = w->bottom, maxx = w->right;
  // Ignore most events outside the window.
  if ((x < begx || x > begx + maxx || y < begy || y > begy + maxy) && button != 4 &&
    button != 5 && event != SCM_DRAG)
    return false;
  y = y - begy, x = x - begx;
  if (event == SCM_PRESS)
    return scitermbox->MousePress(button, y, x, shift, ctrl, alt);
  else if (event == SCM_DRAG)
    return scitermbox->MouseMove(y, x, shift, ctrl, alt);
  else if (event == SCM_RELEASE)
    return (scitermbox->MouseRelease(y, x, ctrl), true);
  return false;
}
char *scintilla_get_clipboard(void *sci, int *len) {
  return reinterpret_cast<ScintillaTermbox *>(sci)->GetClipboard(len);
}
  void scintilla_refresh(void *sci) { reinterpret_cast<ScintillaTermbox *>(sci)->Refresh(); }
  void scintilla_delete(void *sci) { delete reinterpret_cast<ScintillaTermbox *>(sci); }
  void scintilla_resize(void *sci, int width, int height) {
    reinterpret_cast<ScintillaTermbox *>(sci)->Resize(width, height);
  }
  void scintilla_move(void *sci, int new_x, int new_y) {
    reinterpret_cast<ScintillaTermbox *>(sci)->Move(new_x, new_y);
  }
}
