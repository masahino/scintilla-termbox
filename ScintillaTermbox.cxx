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

using namespace Scintilla;
using namespace Scintilla::Internal;

struct TermboxWin {
  int left;
  int top;
  int right;
  int bottom;

  explicit TermboxWin(int left_, int top_, int right_, int bottom_) noexcept :
                left(left_), top(top_), right(right_), bottom(bottom_) {
        }
        int Width() const noexcept { return right - left + 1; }
        int Height() const noexcept { return bottom - top + 1; }
        void Move(int newx, int newy) noexcept {
          right += newx - left;
          bottom += newy - top;
          left = newx;
          top = newy;
        }
};

// Font handling.

/**
 * Allocates a new Scintilla font for curses.
 * Since terminals handle fonts on their own, the only use for Scintilla font objects is to
 * indicate which attributes terminal characters have.
 */
class FontImpl : public Font {
public:
  /**
   * Sets terminal character attributes for a particular font.
   * These attributes are a union of curses attributes and stored in the font's `attrs`.
   * The curses attributes are not constructed from various fields in *fp* since there is no
   * `underline` parameter. Instead, you need to manually set the `weight` parameter to be the
   * union of your desired attributes. Scintilla's lexers/LexLPeg.cxx has an example of this.
   */
  FontImpl(const FontParameters &fp) {
    if (fp.weight == FontWeight::Bold)
      attrs = TB_BOLD;
    else if (fp.weight != FontWeight::Normal && fp.weight != FontWeight::SemiBold)
      attrs = static_cast<int>(fp.weight); // font attributes are stored in fp.weight
#ifdef TB_ITALIC
    if (fp.italic == true)
      attrs |= TB_ITALIC;
#endif
  }
  ~FontImpl() noexcept override = default;
  uint32_t attrs = 0;
};
std::shared_ptr<Font> Font::Allocate(const FontParameters &fp) {
  return std::make_shared<FontImpl>(fp);
}

// Surface handling.

/**
 * Implementation of a Scintilla surface for curses.
 * The surface is initialized with a curses `WINDOW` for drawing on. Since curses can only show
 * text, many of Scintilla's pixel-based functions are not implemented.
 */
class SurfaceImpl : public Surface {
  PRectangle clip;
  WindowID win = nullptr;

  /**
   * Returns the number of columns used to display the first UTF-8 character in `s`, taking
   * into account zero-width combining characters.
   * @param s The string that contains the first UTF-8 character to display.
   */
  int grapheme_width(const char *s) {
    int len = utf8_char_length(s[0]);
    if (len > 1) return 2;
    return 1;
  }

  int to_rgb(ColourRGBA c) {
    return (c.GetRed() << 16) + (c.GetGreen() << 8)  + (c.GetBlue());
  }

public:
  /** Allocates a new Scintilla surface for curses. */
  SurfaceImpl() = default;
  /** Deletes the surface. */
  ~SurfaceImpl() noexcept override { Release(); }

  /**
   * Initializes/reinitializes the surface with a curses `WINDOW` for drawing on.
   * @param wid Curses `WINDOW`.
   */
  void Init(WindowID wid) override {
    Release();
    win = wid;
  }
  /** Identical to `Init()` using the given curses `WINDOW`. */
  void Init(SurfaceID sid, WindowID wid) override { Init(wid); }
  /**
   * Surface pixmaps are not implemented.
   * Cannot return a nullptr because Scintilla assumes the allocation succeeded.
   */
  std::unique_ptr<Surface> AllocatePixMap(int width, int height) override {
    return std::make_unique<SurfaceImpl>();
  }

  /** Surface modes other than UTF-8 (like DBCS and bidirectional) are not implemented. */
  void SetMode(SurfaceMode mode) override {}

  /** Releases the surface's resources. */
  void Release() noexcept override { }
  /** Extra graphics features are ill-suited for drawing in the terminal and not implemented. */
  int SupportsFeature(Supports feature) noexcept override { return 0; }
  /**
   * Returns `true` since this method is only called for pixmap surfaces and those surfaces
   * are not implemented.
   */
  bool Initialised() override { return true; }
  /** Unused; return value irrelevant. */
  int LogPixelsY() override { return 1; }
  /** Returns 1 since one "pixel" is always 1 character cell in curses. */
  int PixelDivisions() override { return 1; }
  /** Returns 1 since font height is always 1 in curses. */
  int DeviceHeightFont(int points) override { return 1; }
  /**
   * Drawing lines is not implemented because more often than not lines are being drawn for
   * decoration (e.g. line markers, underlines, indicators, arrows, etc.).
   */
  void LineDraw(Point start, Point end, Stroke stroke) override {}
  void PolyLine(const Point *pts, size_t npts, Stroke stroke) override {}
  /**
   * Draws the character equivalent of shape outlined by the given polygon's points.
   * Scintilla only calls this method for CallTip arrows and INDIC_POINT[CHARACTER]. Assume
   * the former. Line markers that Scintilla would normally draw as polygons are handled in
   * `DrawLineMarker()`.
   */
  void Polygon(const Point *pts, size_t npts, FillStroke fillStroke) override {
#ifdef DEBUG
    fprintf(stderr, "Polygon\n");
#endif
  }
  /**
   * Scintilla will never call this method.
   * Line markers that Scintilla would normally draw as rectangles are handled in
   * `DrawLineMarker()`.
   */
  void RectangleDraw(PRectangle rc, FillStroke fillStroke) override {}
  /**
   * Drawing framed rectangles like fold display text, EOL annotations, and INDIC_BOX is not
   * implemented.
   */
  void RectangleFrame(PRectangle rc, Stroke stroke) override {}
  /**
   * Clears the given portion of the screen with the given background color.
   * In some cases, it can be determined that whitespace is being drawn. If so, draw it
   * appropriately instead of clearing the given portion of the screen.
   */
  void FillRectangle(PRectangle rc, Fill fill) override {
    if (!win) return;
#ifdef DEBUG
    fprintf(stderr, "FillRectangle (%lf, %lf, %lf, %lf) %x\n", rc.left, rc.top, rc.right, rc.bottom, fill.colour.OpaqueRGB());
#endif
    //wattr_set(win, 0, term_color_pair(COLOR_WHITE, fill.colour), nullptr);
    char ch = ' ';
    if (fabs(rc.left - static_cast<int>(rc.left)) > 0.1) {
#ifdef DEBUG
      fprintf(stderr, "fractional\n");
#endif
      // If rc.left is a fractional value (e.g. 4.5) then whitespace dots are being drawn. Draw
      // them appropriately.
      // TODO: set color to vs.whitespaceColours.fore and back.
//      wcolor_set(win, term_color_pair(COLOR_BLACK, COLOR_BLACK), nullptr);
//      rc.right = static_cast<int>(rc.right), ch = ACS_BULLET | A_BOLD;
    }
    //int right = std::min(static_cast<int>(rc.right), reinterpret_cast<TermboxWin *>(win)->right);
    int right = static_cast<int>(rc.right);
    int bottom = static_cast<int>(rc.bottom);
    int top = 0;
    int left = 0;
    if (win) {
      right = std::min(right, reinterpret_cast<TermboxWin *>(win)->Width());
      bottom = std::min(bottom, reinterpret_cast<TermboxWin *>(win)->Height());
      top = reinterpret_cast<TermboxWin *>(win)->top;
      left = reinterpret_cast<TermboxWin *>(win)->left;
    }
    for (int y = rc.top; y < rc.bottom; y++) {
      for (int x = rc.left; x < right; x++) {
        tb_change_cell(left + x, top + y, ch, 0xffffff, to_rgb(fill.colour));
      }
    }
  }
  /**
   * Identical to `FillRectangle()` since suecial alignment to pixel boundaries is not needed.
   */
  void FillRectangleAligned(PRectangle rc, Fill fill) override { FillRectangle(rc, fill); }
  /**
   * Instead of filling a portion of the screen with a surface pixmap, fills the the screen
   * portion with black.
   */
  void FillRectangle(PRectangle rc, Surface &surfacePattern) override {
#ifdef DEBUG
    fprintf(stderr, "FillRctangle with SurfacePattern \n");
#endif
    FillRectangle(rc, ColourRGBA(0, 0, 0));
  }
  /**
   * Scintilla will never call this method.
   * Line markers that Scintilla would normally draw as rounded rectangles are handled in
   * `DrawLineMarker()`.
   */
  void RoundedRectangle(PRectangle rc, FillStroke fillStroke) override {}
  /**
   * Drawing alpha rectangles is not fully supported.
   * Instead, fills the background color of the given rectangle with the fill color, emulating
   * INDIC_STRAIGHTBOX with no transparency.
   * This is called by Scintilla to draw INDIC_ROUNDBOX and INDIC_STRAIGHTBOX indicators,
   * text blobs, and translucent line states and selections.
   */
  void AlphaRectangle(PRectangle rc, XYPOSITION cornerSize, FillStroke fillStroke) override {
#ifdef DEBUG
    fprintf(stderr, "AlphaRectangle\n");
#endif
  }
  /** Drawing gradients is not implemented. */
  void GradientRectangle(
    PRectangle rc, const std::vector<ColourStop> &stops, GradientOptions options) override {}
  /** Drawing images is not implemented. */
  void DrawRGBAImage(
    PRectangle rc, int width, int height, const unsigned char *pixelsImage) override {}
  /**
   * Scintilla will never call this method.
   * Line markers that Scintilla would normally draw as circles are handled in `DrawLineMarker()`.
   */
  void Ellipse(PRectangle rc, FillStroke fillStroke) override {}
  /** Drawing curved ends on EOL annotations is not implemented. */
  void Stadium(PRectangle rc, FillStroke fillStroke, Ends ends) override {}
  /**
   * Draw an indentation guide.
   * Scintilla will only call this method when drawing indentation guides or during certain
   * drawing operations when double buffering is enabled. Since the latter is not supported,
   * assume the former.
   */
  void Copy(PRectangle rc, Point from, Surface &surfaceSource) override {
#ifdef DEBUG
    fprintf(stderr, "Copy\n");
#endif
  }

  /** Bidirectional input is not implemented. */
  std::unique_ptr<IScreenLineLayout> Layout(const IScreenLine *screenLine) override {
    return nullptr;
  }

  /**
   * Draws the given text at the given position on the screen with the given foreground and
   * background colors.
   * Takes into account any clipping boundaries previously specified.
   */
  void DrawTextNoClip(PRectangle rc, const Font *font_, XYPOSITION ybase, std::string_view text,
    ColourRGBA fore, ColourRGBA back) override {

    uint32_t attrs = dynamic_cast<const FontImpl *>(font_)->attrs;
    if (rc.left < clip.left) {
      // Do not overwrite margin text.
      int clip_chars = static_cast<int>(clip.left - rc.left);
      size_t offset = 0;
      for (int chars = 0; offset < text.length(); offset++) {
        if (!UTF8IsTrailByte(static_cast<unsigned char>(text[offset]))) {
          chars += grapheme_width(text.data() + offset);
        }
        if (chars > clip_chars) {
          break;
        }
      }
      text.remove_prefix(offset);
      rc.left = clip.left;
    }
    // Do not write beyond right window boundary.
    int clip_chars = reinterpret_cast<TermboxWin *>(win)->Width() - rc.left;
    int top = reinterpret_cast<TermboxWin *>(win)->top;
    int left = reinterpret_cast<TermboxWin *>(win)->left;
    size_t bytes = 0;
    int x = rc.left;
    int y = rc.top;
    for (int chars = 0; bytes < text.length(); bytes++) {
      if (!UTF8IsTrailByte(static_cast<unsigned char>(text[bytes])))
        chars += grapheme_width(text.data() + bytes);
      if (chars > clip_chars) break;
    }
#ifdef DEBUG
    fprintf(stderr, "%ld(%d, %d, %06x, %06x)[%s]\n", bytes, x, y, fore.OpaqueRGB(), back.OpaqueRGB(), text.data());
#endif
/*
    for (int i = 0; i < bytes; i++) {
      tb_change_cell(x, y, text.at(i), fore.OpaqueRGB() | attrs, back.OpaqueRGB());
      x++;
    }
*/
    if (bytes == 0) {
      return;
    }
    int len = 0;
    int width = 0;
    const char *str = text.data();
    while (*str) {
      uint32_t uni;
      width = grapheme_width(str + len);
      len += utf8_char_to_unicode(&uni, str + len);
      tb_change_cell(left + x, top + y, uni, to_rgb(fore) | attrs, to_rgb(back));
      x += width;
      if (len >= bytes) {
        break;
      }
    }

  }
  /**
   * Similar to `DrawTextNoClip()`.
   * Scintilla calls this method for drawing the caret, text blobs, and `MarkerSymbol::Character`
   * line markers.
   * When drawing control characters, *rc* needs to have its pixel padding removed since curses
   * has smaller resolution. Similarly when drawing line markers, *rc* needs to be reshaped.
   * @see DrawTextNoClip
   */
  void DrawTextClipped(PRectangle rc, const Font *font_, XYPOSITION ybase, std::string_view text,
    ColourRGBA fore, ColourRGBA back) override {
    if (rc.left >= rc.right) // when drawing text blobs
      rc.left -= 2, rc.right -= 2, rc.top -= 1, rc.bottom -= 1;
    DrawTextNoClip(rc, font_, ybase, text, fore, back);
  }
  /**
   * Similar to `DrawTextNoClip()`.
   * Scintilla calls this method for drawing CallTip text and two-phase buffer text.
   */
  void DrawTextTransparent(PRectangle rc, const Font *font_, XYPOSITION ybase,
    std::string_view text, ColourRGBA fore) override {
    if (static_cast<int>(rc.top) > reinterpret_cast<TermboxWin *>(win)->bottom) return;
    int y = reinterpret_cast<TermboxWin *>(win)->top + static_cast<int>(rc.top);
    int x = reinterpret_cast<TermboxWin *>(win)->left + static_cast<int>(rc.left);
    struct tb_cell *buffer = tb_cell_buffer();
    int tb_color = buffer[y * tb_width() + x].bg;
    DrawTextNoClip(rc, font_, ybase, text, fore,
      ColourRGBA(tb_color >> 16, (tb_color & 0x00ff00) >> 8, tb_color & 0x0000ff));
  }
  /**
   * Measures the width of characters in the given string and writes them to the given position
   * list.
   * Curses characters always have a width of 1 if they are not UTF-8 trailing bytes.
   */
  void MeasureWidths(const Font *font_, std::string_view text, XYPOSITION *positions) override {
    for (size_t i = 0, j = 0; i < text.length(); i++) {
      if (!UTF8IsTrailByte(static_cast<unsigned char>(text[i])))
        j += grapheme_width(text.data() + i);
      positions[i] = j;
    }
  }
  /**
   * Returns the number of UTF-8 characters in the given string since curses characters always
   * have a width of 1.
   */
  XYPOSITION WidthText(const Font *font_, std::string_view text) override {
    int width = 0;
    for (size_t i = 0; i < text.length(); i++)
      if (!UTF8IsTrailByte(static_cast<unsigned char>(text[i])))
        width += grapheme_width(text.data() + i);
    return width;
  }
  /** Identical to `DrawTextNoClip()` since UTF-8 is assumed. */
  void DrawTextNoClipUTF8(PRectangle rc, const Font *font_, XYPOSITION ybase, std::string_view text,
    ColourRGBA fore, ColourRGBA back) override {
    DrawTextNoClip(rc, font_, ybase, text, fore, back);
  }
  /** Identical to `DrawTextClipped()` since UTF-8 is assumed. */
  void DrawTextClippedUTF8(PRectangle rc, const Font *font_, XYPOSITION ybase,
    std::string_view text, ColourRGBA fore, ColourRGBA back) override {
    DrawTextClipped(rc, font_, ybase, text, fore, back);
  }
  /** Identical to `DrawTextTransparent()` since UTF-8 is assumed. */
  void DrawTextTransparentUTF8(PRectangle rc, const Font *font_, XYPOSITION ybase,
    std::string_view text, ColourRGBA fore) override {
    DrawTextTransparent(rc, font_, ybase, text, fore);
  }
  /** Identical to `MeasureWidths()` since UTF-8 is assumed. */
  void MeasureWidthsUTF8(const Font *font_, std::string_view text, XYPOSITION *positions) override {
    MeasureWidths(font_, text, positions);
  }
  /** Identical to `WidthText()` since UTF-8 is assumed. */
  XYPOSITION WidthTextUTF8(const Font *font_, std::string_view text) override {
    return WidthText(font_, text);
  }
  /** Returns 0 since curses characters have no ascent. */
  XYPOSITION Ascent(const Font *font_) override { return 0; }
  /** Returns 0 since curses characters have no descent. */
  XYPOSITION Descent(const Font *font_) override { return 0; }
  /** Returns 0 since curses characters have no leading. */
  XYPOSITION InternalLeading(const Font *font_) override { return 0; }
  /** Returns 1 since curses characters always have a height of 1. */
  XYPOSITION Height(const Font *font_) override { return 1; }
  /** Returns 1 since curses characters always have a width of 1. */
  XYPOSITION AverageCharWidth(const Font *font_) override { return 1; }

  /**
   * Ensure text to be drawn in subsequent calls to `DrawText*()` is drawn within the given
   * rectangle.
   * This is needed in order to prevent long lines from overwriting margin text when scrolling
   * to the right.
   */
  void SetClip(PRectangle rc) override {
    clip.left = rc.left, clip.top = rc.top;
    clip.right = rc.right, clip.bottom = rc.bottom;
  }
  /** Remove the clip set in `SetClip()`. */
  void PopClip() override { clip.left = 0, clip.top = 0, clip.right = 0, clip.bottom = 0; }
  /** Flushing cache is not implemented. */
  void FlushCachedState() override {}
  /** Flushing is not implemented since surface pixmaps are not implemented. */
  void FlushDrawing() override {}

  /** Draws the text representation of a line marker, if possible. */
  void DrawLineMarker(
    const PRectangle &rcWhole, const Font *fontForCharacter, int tFold, const void *data) {
    int top = reinterpret_cast<TermboxWin *>(win)->top;
    int left = reinterpret_cast<TermboxWin *>(win)->left;

    // TODO: handle fold marker highlighting.
    const LineMarker *marker = reinterpret_cast<const LineMarker *>(data);
    //wattr_set(win, 0, term_color_pair(marker->fore, marker->back), nullptr);
    switch (marker->markType) {
    case MarkerSymbol::Circle: tb_change_cell(left + rcWhole.left, top + rcWhole.top, 0x25CF, to_rgb(marker->fore), to_rgb(marker->back)); return;
    case MarkerSymbol::SmallRect:
    case MarkerSymbol::RoundRect: tb_change_cell(left + rcWhole.left, top + rcWhole.top, 0x25A0, to_rgb(marker->fore), to_rgb(marker->back)); return;
    case MarkerSymbol::Arrow: tb_change_cell(left + rcWhole.left, top + rcWhole.top, 0x25B6, to_rgb(marker->fore), to_rgb(marker->back)); return;
    case MarkerSymbol::ShortArrow:tb_change_cell(left + rcWhole.left, top + rcWhole.top, 0x2192, to_rgb(marker->fore), to_rgb(marker->back)); return;
    case MarkerSymbol::Empty: tb_change_cell(left + rcWhole.left, top + rcWhole.top, ' ', to_rgb(marker->fore), to_rgb(marker->back)); return;
    case MarkerSymbol::ArrowDown: tb_change_cell(left + rcWhole.left, top + rcWhole.top, 0x25BC, to_rgb(marker->fore), to_rgb(marker->back)); return;
    case MarkerSymbol::Minus: tb_change_cell(left + rcWhole.left, top + rcWhole.top, 0x2500, to_rgb(marker->fore), to_rgb(marker->back)); return;
    case MarkerSymbol::BoxMinus:
    case MarkerSymbol::BoxMinusConnected: tb_change_cell(left + rcWhole.left, top + rcWhole.top, 0x229F, to_rgb(marker->fore), to_rgb(marker->back)); return;
    case MarkerSymbol::CircleMinus:
    case MarkerSymbol::CircleMinusConnected: tb_change_cell(left + rcWhole.left, top + rcWhole.top, 0x2295, to_rgb(marker->fore), to_rgb(marker->back)); return;
    case MarkerSymbol::Plus: tb_change_cell(left + rcWhole.left, top + rcWhole.top, 0x253C, to_rgb(marker->fore), to_rgb(marker->back)); return;
    case MarkerSymbol::BoxPlus:
    case MarkerSymbol::BoxPlusConnected: tb_change_cell(left + rcWhole.left, top + rcWhole.top, 0x229E, to_rgb(marker->fore), to_rgb(marker->back)); return;
    case MarkerSymbol::CirclePlus:
    case MarkerSymbol::CirclePlusConnected: tb_change_cell(left + rcWhole.left, top + rcWhole.top, 0x2296, to_rgb(marker->fore), to_rgb(marker->back)); return;
    case MarkerSymbol::VLine: tb_change_cell(left + rcWhole.left, top + rcWhole.top, 0x2502, to_rgb(marker->fore), to_rgb(marker->back)); return;
    case MarkerSymbol::LCorner:
    case MarkerSymbol::LCornerCurve: tb_change_cell(left + rcWhole.left, top + rcWhole.top, 0x2514, to_rgb(marker->fore), to_rgb(marker->back)); return;
    case MarkerSymbol::TCorner:
    case MarkerSymbol::TCornerCurve: tb_change_cell(left + rcWhole.left, top + rcWhole.top, 0x251C, to_rgb(marker->fore), to_rgb(marker->back)); return;
    case MarkerSymbol::DotDotDot: tb_change_cell(left + rcWhole.left, top + rcWhole.top, 0x22EF, to_rgb(marker->fore), to_rgb(marker->back)); return;
    case MarkerSymbol::Arrows: tb_change_cell(left + rcWhole.left, top + rcWhole.top, 0x22D9, to_rgb(marker->fore), to_rgb(marker->back)); return;
    case MarkerSymbol::FullRect: FillRectangle(rcWhole, marker->back); return;
    case MarkerSymbol::LeftRect: tb_change_cell(left + rcWhole.left, top + rcWhole.top, 0x258E, to_rgb(marker->fore), to_rgb(marker->back)); return;
    case MarkerSymbol::Bookmark: tb_change_cell(left + rcWhole.left, top + rcWhole.top, 0x2211, to_rgb(marker->fore), to_rgb(marker->back)); return;
    default:
      break; // prevent warning
    }
   if (marker->markType >= MarkerSymbol::Character) {
      char ch = static_cast<char>(
        static_cast<int>(marker->markType) - static_cast<int>(MarkerSymbol::Character));
      DrawTextClipped(
        rcWhole, fontForCharacter, rcWhole.bottom, std::string(&ch, 1), marker->fore, marker->back);
      return;
    }
#ifdef DEBUG
    fprintf(stderr, "DrawLineMarker %d\n", static_cast<int>(marker->markType));
#endif
  }
  /** Draws the text representation of a wrap marker. */
  void DrawWrapMarker(PRectangle rcPlace, bool isEndMarker, ColourRGBA wrapColour) {
#ifdef DEBUG
    fprintf(stderr, "DrawWrapMaker\n");
#endif
  }
  /** Draws the text representation of a tab arrow. */
  void DrawTabArrow(PRectangle rcTab, const ViewStyle &vsDraw) {
#ifdef DEBUG
    fprintf(stderr, "DrawTabArrow\n");
#endif
  }

  bool isCallTip = false;
};

/** Creates a new curses surface. */
std::unique_ptr<Surface> Surface::Allocate(Technology) { return std::make_unique<SurfaceImpl>(); }

/** Custom function for drawing line markers in curses. */
static void DrawLineMarker(Surface *surface, const PRectangle &rcWhole,
  const Font *fontForCharacter, int tFold, MarginType marginStyle, const void *data) {
  reinterpret_cast<SurfaceImpl *>(surface)->DrawLineMarker(rcWhole, fontForCharacter, tFold, data);
}
/** Custom function for drawing wrap markers in curses. */
static void DrawWrapVisualMarker(
  Surface *surface, PRectangle rcPlace, bool isEndMarker, ColourRGBA wrapColour) {
  reinterpret_cast<SurfaceImpl *>(surface)->DrawWrapMarker(rcPlace, isEndMarker, wrapColour);
}
/** Custom function for drawing tab arrows in curses. */
static void DrawTabArrow(
  Surface *surface, PRectangle rcTab, int ymid, const ViewStyle &vsDraw, Stroke stroke) {
  reinterpret_cast<SurfaceImpl *>(surface)->DrawTabArrow(rcTab, vsDraw);
}

// Window handling.

/** Deletes the window. */
Window::~Window() noexcept {}
/**
 * Releases the window's resources.
 * Since the only Windows created are AutoComplete and CallTip windows, and since those windows
 * are created in `ListBox::Create()` and `ScintillaCurses::CreateCallTipWindow()`, respectively,
 * via `newwin()`, it is safe to use `delwin()`.
 * It is important to note that even though `ScintillaCurses::wMain` is a Window, its `Destroy()`
 * function is never called, hence why `scintilla_delete()` is the complement to `scintilla_new()`.
 */
void Window::Destroy() noexcept {
  wid = nullptr;
}
/**
 * Returns the window's boundaries.
 * Unlike other platforms, Scintilla paints in coordinates relative to the window in
 * curses. Therefore, this function should always return the window bounds to ensure all of it
 * is painted.
 * @return PRectangle with the window's boundaries.
 */
PRectangle Window::GetPosition() const {
  int maxx = wid ? reinterpret_cast<TermboxWin *>(wid)->Width() : 0;
  int maxy = wid ? reinterpret_cast<TermboxWin *>(wid)->Height() : 0;
  return PRectangle(0, 0, maxx, maxy);
}
/**
 * Sets the position of the window relative to its parent window.
 * It will take care not to exceed the boundaries of the parent.
 * @param rc The position relative to the parent window.
 * @param relativeTo The parent window.
 */
void Window::SetPositionRelative(PRectangle rc, const Window *relativeTo) {
  int begx = 0, begy = 0, x = 0, y = 0;
  // Determine the relative position.
  begx = reinterpret_cast<TermboxWin *>(relativeTo->GetID())->left;
  begy = reinterpret_cast<TermboxWin *>(relativeTo->GetID())->top;
  x = begx + rc.left;
  if (x < begx) x = begx;
  y = begy + rc.top;
  if (y < begy) y = begy;
  // Correct to fit the parent if necessary.
  int sizex = rc.right - rc.left;
  int sizey = rc.bottom - rc.top;
  int screen_width = reinterpret_cast<TermboxWin *>(relativeTo->GetID())->Width();
  int screen_height = reinterpret_cast<TermboxWin *>(relativeTo->GetID())->Height();
  if (sizex > screen_width)
    x = begx; // align left
  else if (x + sizex > begx + screen_width)
    x = begx + screen_width - sizex; // align right
  if (y + sizey > begy + screen_height) {
    y = begy + screen_height - sizey; // align bottom
    if (screen_height == 1) y--; // show directly above the relative window
  }
  if (y < 0) y = begy; // align top
  // Update the location.
  reinterpret_cast<TermboxWin *>(wid)->Move(x, y);
#ifdef DEBUG
  fprintf(stderr, "SetPositionRelative %d, %d\n", x, y);
#endif
}
/** Identical to `Window::GetPosition()`. */
PRectangle Window::GetClientPosition() const { return GetPosition(); }
void Window::Show(bool show) {} // TODO:
void Window::InvalidateAll() {} // notify repaint
void Window::InvalidateRectangle(PRectangle rc) {} // notify repaint
/** Setting the cursor icon is not implemented. */
void Window::SetCursor(Cursor curs) {}
/** Identical to `Window::GetPosition()`. */
PRectangle Window::GetMonitorRect(Point pt) { return GetPosition(); }

/**
 * Implementation of a Scintilla ListBox for curses.
 * Instead of registering images to types, printable UTF-8 characters are registered to types.
 */
class ListBoxImpl : public ListBox {
  int height = 5, width = 10;
  std::vector<std::string> list;
  char types[IMAGE_MAX + 1][5]; // UTF-8 character plus terminating '\0'
  int selection = 0;

public:
  IListBoxDelegate *delegate = nullptr;

  /** Allocates a new Scintilla ListBox for curses. */
  ListBoxImpl() {
    list.reserve(10);
    ClearRegisteredImages();
  }
  /** Deletes the ListBox. */
  ~ListBoxImpl() override = default;

  /** Setting the font is not implemented. */
  void SetFont(const Font *font) override {}
  /**
   * Creates a new listbox.
   * The `Show()` function resizes window with the appropriate height and width.
   */
  void Create(Window &parent, int ctrlID, Point location_, int lineHeight_, bool unicodeMode_,
    Technology technology_) override {
    wid = new TermboxWin(0, 0, 1, 1);
  }
  /**
   * Setting average char width is not implemented since all curses characters have a width of 1.
   */
  void SetAverageCharWidth(int width) override {}
  /** Sets the number of visible rows in the listbox. */
  void SetVisibleRows(int rows) override {
    height = rows;
    if (wid) {
      reinterpret_cast<TermboxWin *>(wid)->bottom = reinterpret_cast<TermboxWin *>(wid)->top + height - 1;
    }
  }
  /** Returns the number of visible rows in the listbox. */
  int GetVisibleRows() const override { return height; }
  /** Returns the desired size of the listbox. */
  PRectangle GetDesiredRect() override {
    return PRectangle(0, 0, width, height); // add border widths
  }
  /**
   * Returns the left-offset of the ListBox with respect to the caret.
   * Takes into account the border width and type character width.
   * @return 2 to shift the ListBox to the left two characters.
   */
  int CaretFromEdge() override { return 2; }
  /** Clears the contents of the listbox. */
  void Clear() noexcept override {
    list.clear();
    width = 0;
  }
  /**
   * Adds the given string list item to the listbox.
   * Prepends the item's type character (if any) to the list item for display.
   */
  void Append(char *s, int type) override {
    if (type >= 0 && type <= IMAGE_MAX) {
      char *chtype = types[type];
      list.push_back(std::string(chtype, strlen(chtype)) + s);
    } else
      list.push_back(std::string(" ") + s);
    int len = strlen(s); // TODO: UTF-8 awareness?
    if (width < len + 2) {
      width = len + 2; // include type character len
    }
    reinterpret_cast<TermboxWin *>(wid)->right = reinterpret_cast<TermboxWin *>(wid)->left + width - 1;
    reinterpret_cast<TermboxWin *>(wid)->bottom = reinterpret_cast<TermboxWin *>(wid)->top + height - 1;
  }
  /** Returns the number of items in the listbox. */
  int Length() override { return list.size(); }
  /** Selects the given item in the listbox and repaints the listbox. */
  void Select(int n) override {
    int fore = 0;
    int back = 0;
    int left = reinterpret_cast<TermboxWin *>(wid)->left;
    int top = reinterpret_cast<TermboxWin *>(wid)->top;

    int len = static_cast<int>(list.size());
    int s = n - height / 2;
    if (s + height > len) s = len - height;
    if (s < 0) s = 0;
    for (int i = s; i < s + height; i++) {
      if (i == n) {
        fore = 0x383838;
        back = 0x7cafc2;
      } else {
        fore = 0xd8d8d8;
        back = 0x383838;
      }
      if (i < len) {
        tb_change_cell(left, top + i - s, ' ', fore, back);
        for (int j = 1; j < list.at(i).size(); j++) {
          tb_change_cell(left + j, top + i - s, list.at(i).c_str()[j], fore, back);
        }
        for (int j = list.at(i).size(); j < width; j++) {
          tb_change_cell(left + j, top + i - s, ' ', fore, back);
        }
      } else {
        for (int j = 0; j < width; j++) {
          tb_change_cell(left + j, top + i - s, ' ', fore, back);
        }
      }
    }
    tb_present();
    selection = n;
  }
  /** Returns the currently selected item in the listbox. */
  int GetSelection() override { return selection; }
  /**
   * Searches the listbox for the items matching the given prefix string and returns the index
   * of the first match.
   * Since the type is displayed as the first character, the value starts on the second character;
   * match strings starting there.
   */
  int Find(const char *prefix) override {
    int len = strlen(prefix);
    for (unsigned int i = 0; i < list.size(); i++) {
      const char *item = list.at(i).c_str();
      item += UTF8DrawBytes(reinterpret_cast<const unsigned char *>(item), strlen(item));
      if (strncmp(prefix, item, len) == 0) return i;
    }
    return -1;
  }
  /**
   * Returns the string item in the listbox at the given index.
   * Since the type is displayed as the first character, the value starts on the second character.
   */
  std::string GetValue(int n) override {
    const char *item = list.at(n).c_str();
    item += UTF8DrawBytes(reinterpret_cast<const unsigned char *>(item), strlen(item));
    return item;
  }
  /**
   * Registers the first UTF-8 character of the given string to the given type.
   * By default, ' ' (space) is registered to all types.
   * @usage SCI_REGISTERIMAGE(1, "*") // type 1 shows '*' in front of list item.
   * @usage SCI_REGISTERIMAGE(2, "+") // type 2 shows '+' in front of list item.
   * @usage SCI_REGISTERIMAGE(3, "■") // type 3 shows '■' in front of list item.
   */
  void RegisterImage(int type, const char *xpm_data) override {
    if (type < 0 || type > IMAGE_MAX) return;
    int len = UTF8DrawBytes(reinterpret_cast<const unsigned char *>(xpm_data), strlen(xpm_data));
    for (int i = 0; i < len; i++) types[type][i] = xpm_data[i];
    types[type][len] = '\0';
  }
  /** Registering images is not implemented. */
  void RegisterRGBAImage(
    int type, int width, int height, const unsigned char *pixelsImage) override {}
  /** Clears all registered types back to ' ' (space). */
  void ClearRegisteredImages() override {
    for (int i = 0; i <= IMAGE_MAX; i++) types[i][0] = ' ', types[i][1] = '\0';
  }
  /** Defines the delegate for ListBox actions. */
  void SetDelegate(IListBoxDelegate *lbDelegate) override { delegate = lbDelegate; }
  /** Sets the list items in the listbox to the given items. */
  void SetList(const char *listText, char separator, char typesep) override {
    Clear();
    int len = strlen(listText);
    char *text = new char[len + 1];
    if (!text) return;
    memcpy(text, listText, len + 1);
    char *word = text, *type = nullptr;
    for (int i = 0; i <= len; i++) {
      if (text[i] == separator || i == len) {
        text[i] = '\0';
        if (type) *type = '\0';
        Append(word, type ? atoi(type + 1) : -1);
        word = text + i + 1, type = nullptr;
      } else if (text[i] == typesep)
        type = text + i;
    }
    delete[] text;
  }
  /** List options are not implemented. */
  void SetOptions(ListOptions options_) override {}
};

/** Creates a new Scintilla ListBox. */
ListBox::ListBox() noexcept = default;
/** Deletes the ListBox. */
ListBox::~ListBox() noexcept = default;
/** Creates a new curses ListBox. */
std::unique_ptr<ListBox> ListBox::Allocate() { return std::make_unique<ListBoxImpl>(); }


// Menus are not implemented.
Menu::Menu() noexcept : mid(nullptr) {}
void Menu::CreatePopUp() {}
void Menu::Destroy() noexcept {}
void Menu::Show(Point pt, const Window &w) {}

ColourRGBA Platform::Chrome() { return ColourRGBA(0, 0, 0); }
ColourRGBA Platform::ChromeHighlight() { return ColourRGBA(0, 0, 0); }
const char *Platform::DefaultFont() { return "monospace"; }
int Platform::DefaultFontSize() { return 10; }
unsigned int Platform::DoubleClickTime() { return 500; /* ms */ }
void Platform::DebugDisplay(const char *s) noexcept { fprintf(stderr, "%s", s); }
void Platform::DebugPrintf(const char *format, ...) noexcept {}
// bool Platform::ShowAssertionPopUps(bool assertionPopUps_) noexcept { return true; }
void Platform::Assert(const char *c, const char *file, int line) noexcept {
  char buffer[2000];
  sprintf(buffer, "Assertion [%s] failed at %s %d\r\n", c, file, line);
  Platform::DebugDisplay(buffer);
  abort();
}

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

public:
  /**
   * Creates a new Scintilla instance in a curses `WINDOW`.
   * However, the `WINDOW` itself will not be created until it is absolutely necessary. When the
   * `WINDOW` is created, it will initially be full-screen.
   * @param callback_ Callback function for Scintilla notifications.
   */
  ScintillaTermbox(void (*callback_)(void *, int, SCNotification *, void *), void *userdata_)
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
  ~ScintillaTermbox() override {
  }
  /** Initializing code is unnecessary. */
  void Initialise() override { }
  /** Disable drag and drop since it is not implemented. */
  void StartDrag() override {
   inDragDrop = DragDrop::none;
    SetDragPosition(SelectionPosition(Sci::invalidPosition));
  }
  /** Draws the vertical scroll bar. */
  void SetVerticalScrollPos() override {
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
  void SetHorizontalScrollPos() override {
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
  bool ModifyScrollBars(Sci::Line nMax, Sci::Line nPage) override {
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
  void Copy() override {
    if (!sel.Empty()) CopySelectionRange(&clipboard);
  }
  /** Pastes text from the internal clipboard, not from primary or secondary X selections. */
  void Paste() override {
    if (clipboard.Empty()) return;
    ClearSelection(multiPasteMode == MultiPaste::Each);
    InsertPasteShape(clipboard.Data(), static_cast<int>(clipboard.Length()),
      !clipboard.rectangular ? PasteShape::stream : PasteShape::rectangular);
    EnsureCaretVisible();
  }
  /** Setting of the primary and/or secondary X selections is not supported. */
  void ClaimSelection() override {}
  /** Notifying the parent of text changes is not yet supported. */
  void NotifyChange() override {}
  /** Send Scintilla notifications to the parent. */
  void NotifyParent(NotificationData scn) override {
    if (callback)
      (*callback)(
        reinterpret_cast<void *>(this), 0, reinterpret_cast<SCNotification *>(&scn), userdata);
  }
  /**
   * Handles an unconsumed key.
   * If a character is being typed, add it to the editor. Otherwise, notify the container.
   */
  int KeyDefault(Keys key, KeyMod modifiers) override {
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
  void CopyToClipboard(const SelectionText &selectedText) override { clipboard.Copy(selectedText); }
  /** A ticking caret is not implemented. */
  bool FineTickerRunning(TickReason reason) override { return false; }
  /** A ticking caret is not implemented. */
  void FineTickerStart(TickReason reason, int millis, int tolerance) override {}
  /** A ticking caret is not implemented. */
  void FineTickerCancel(TickReason reason) override {}
  /**
   * Sets whether or not the mouse is captured.
   * This is used by Scintilla to handle mouse clicks, drags, and releases.
   */
  void SetMouseCapture(bool on) override { capturedMouse = on; }
  /** Returns whether or not the mouse is captured. */
  bool HaveMouseCapture() override { return capturedMouse; }
  /** All text is assumed to be in UTF-8. */
  std::string UTF8FromEncoded(std::string_view encoded) const override {
    return std::string(encoded);
  }
  /** All text is assumed to be in UTF-8. */
  std::string EncodedFromUTF8(std::string_view utf8) const override { return std::string(utf8); }
  /** A Scintilla direct pointer is not implemented. */
  sptr_t DefWndProc(Message iMessage, uptr_t wParam, sptr_t lParam) override { return 0; }
  /** Draws a CallTip, creating the curses window for it if necessary. */
  void CreateCallTipWindow(PRectangle rc) override {
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
  void AddToPopUp(const char *label, int cmd = 0, bool enabled = true) override {}
  /**
   * Sends the given message and parameters to Scintilla unless it is a message that changes
   * an unsupported property.
   */
  sptr_t WndProc(Message iMessage, uptr_t wParam, sptr_t lParam) override {
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
  TermboxWin *GetWINDOW() {
    return reinterpret_cast<TermboxWin *>(wMain.GetID());
  }
  /**
   * Updates the cursor position, even if it's not visible, as the container may have a use for it.
   */
  void UpdateCursor() {
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
  void Refresh() {
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
  void KeyPress(int key, bool shift, bool ctrl, bool alt) {
    KeyDownWithModifiers(static_cast<Keys>(key), ModifierFlags(shift, ctrl, alt), nullptr);
  }
  /**
   * Handles a mouse button press.
   * @param button The button number pressed, or `0` if none.
   * @param time The time in milliseconds of the mouse event.
   * @param y The y coordinate of the mouse event relative to this window.
   * @param x The x coordinate of the mouse event relative to this window.
   * @param shift Flag indicating whether or not the shift modifier key is pressed.
   * @param ctrl Flag indicating whether or not the control modifier key is pressed.
   * @param alt Flag indicating whether or not the alt modifier key is pressed.
   * @return whether or not the mouse event was handled
   */
  bool MousePress(int button, unsigned int time, int y, int x, bool shift, bool ctrl, bool alt) {
    if (ac.Active() && (button == 1 || button == 4 || button == 5)) {
      // Select an autocompletion list item if possible or scroll the list.
      TermboxWin *w = reinterpret_cast<TermboxWin *>(ac.lb->GetID()), *parent = GetWINDOW();
      int begy = w->top - parent->top; // y is relative to the view
      int begx = w->left - parent->left; // x is relative to the view
      int maxy = w->bottom - 1, maxx = w->right - 1; // ignore border
      int ry = y - begy, rx = x - begx; // relative to list box
      if (ry > 0 && ry < maxy && rx > 0 && rx < maxx) {
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
          int offset = ry - ny - 1; // -1 ignores list box border
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
      } else if (ry == 0 || ry == maxy || rx == 0 || rx == maxx)
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
  bool MouseMove(int y, int x, bool shift, bool ctrl, bool alt) {
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
   * @param time The time in milliseconds of the mouse event.
   * @param y The y coordinate of the mouse event relative to this window.
   * @param x The x coordinate of the mouse event relative to this window.
   * @param ctrl Flag indicating whether or not the control modifier key is pressed.
   */
  void MouseRelease(int time, int y, int x, int ctrl) {
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
  char *GetClipboard(int *len) {
    if (len) *len = clipboard.Length();
    char *text = new char[clipboard.Length() + 1];
    memcpy(text, clipboard.Data(), clipboard.Length() + 1);
    return text;
  }
  /**
   * Resize Scintilla Window.
   */
  void Resize(int width, int height) {
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
  void Move(int new_x, int new_y) {
    reinterpret_cast<TermboxWin *>(wMain.GetID())->Move(new_x, new_y);
    tb_clear();
    Refresh();
  }

};

// Link with C. Documentation in ScintillaCurses.h.
extern "C" {
  void *scintilla_new(void (*callback)(void *, int, SCNotification *, void *), void *userdata) {
    return reinterpret_cast<void *>(new ScintillaTermbox(callback, userdata));
  }
  sptr_t scintilla_send_message(void *sci, unsigned int iMessage, uptr_t wParam, sptr_t lParam) {
    return reinterpret_cast<ScintillaTermbox *>(sci)->WndProc(
      static_cast<Message>(iMessage), wParam, lParam);
  }
  void scintilla_send_key(void *sci, int key, bool shift, bool ctrl, bool alt) {
    reinterpret_cast<ScintillaTermbox *>(sci)->KeyPress(key, shift, ctrl, alt);
  }
bool scintilla_send_mouse(void *sci, int event, unsigned int time, int button, int y, int x,
  bool shift, bool ctrl, bool alt) {
  ScintillaTermbox *scitermbox = reinterpret_cast<ScintillaTermbox *>(sci);
  TermboxWin *w = scitermbox->GetWINDOW();
  int begy = w->top, begx = w->left;
  int maxy = w->bottom, maxx = w->right;
  // Ignore most events outside the window.
  if ((x < begx || x > begx + maxx || y < begy || y > begy + maxy) && button != 4 &&
    button != 5 && event != SCM_DRAG)
    return false;
  y = y - begy, x = x - begx;
  if (event == SCM_PRESS)
    return scitermbox->MousePress(button, time, y, x, shift, ctrl, alt);
  else if (event == SCM_DRAG)
    return scitermbox->MouseMove(y, x, shift, ctrl, alt);
  else if (event == SCM_RELEASE)
    return (scitermbox->MouseRelease(time, y, x, ctrl), true);
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

