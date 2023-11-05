// PlatTermbox.cxx
//
// This code is based on PlatCurses.cxx from scinterm,
// which is available under the MIT License.You can find the original
// https://orbitalquark.github.io/scinterm/

#include <cassert>
#include <cstring>
#include <cmath>

#include <algorithm>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include "ScintillaTypes.h"
#include "ScintillaMessages.h"
#include "ScintillaStructures.h"
#include "ILoader.h"
#include "ILexer.h"

#include "Debugging.h"
#include "Geometry.h"
#include "Platform.h"

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

#include "Scintilla.h"
#include "ScintillaBase.h"

#include "ScintillaTermbox.h"
#include "PlatTermbox.h"

namespace Scintilla::Internal {
/**
 * Returns the number of columns used to display the first UTF-8 character in
 * `s`, taking into account zero-width combining characters.
 * @param s The string that contains the first UTF-8 character to display.
 */
int grapheme_width(const char *s) {
  wchar_t wch;
  if (mbtowc(&wch, s, MB_CUR_MAX) < 1)
    return 1;
  int width = wcwidth(wch);
  return width >= 0 ? width : 1;
}

// Font handling.

FontImpl::FontImpl(const FontParameters &fp) {
  if (fp.weight == FontWeight::Bold)
    attrs = TB_BOLD;
  else if (fp.weight != FontWeight::Normal && fp.weight != FontWeight::SemiBold)
    attrs =
        static_cast<int>(fp.weight); // font attributes are stored in fp.weight
#ifdef TB_ITALIC
  if (fp.italic == true)
    attrs |= TB_ITALIC;
#endif
}

std::shared_ptr<Font> Font::Allocate(const FontParameters &fp) {
  return std::make_shared<FontImpl>(fp);
}

// Surface handling.

int to_rgb(ColourRGBA c) {
  return (c.GetRed() << 16) + (c.GetGreen() << 8) + (c.GetBlue());
}

SurfaceImpl::SurfaceImpl(int w, int h) noexcept {
  width = w;
  height = h;
  pattern = true;
}

SurfaceImpl::~SurfaceImpl() noexcept { Release(); }

/**
 * Initializes/reinitializes the surface with a curses `WINDOW` for drawing on.
 * @param wid Curses `WINDOW`.
 */
void SurfaceImpl::Init(WindowID wid) {
  Release();
  win = wid;
}

void SurfaceImpl::Init(SurfaceID sid, WindowID wid) { Init(wid); }

/**
 * Surface pixmaps are not implemented.
 * Cannot return a nullptr because Scintilla assumes the allocation succeeded.
 */
std::unique_ptr<Surface> SurfaceImpl::AllocatePixMap(int width, int height) {
#ifdef DEBUG
  fprintf(stderr, "allocatePixmap %d, %d\n", width, height);
#endif
  return std::make_unique<SurfaceImpl>(width, height);
}

/** Surface modes other than UTF-8 (like DBCS and bidirectional) are not
 * implemented. */
void SurfaceImpl::SetMode(SurfaceMode mode) {}
/** Releases the surface's resources. */
void SurfaceImpl::Release() noexcept {}
/** Extra graphics features are ill-suited for drawing in the terminal and not
 * implemented. */
int SurfaceImpl::SupportsFeature(Supports feature) noexcept { return 0; }
/**
 * Returns `true` since this method is only called for pixmap surfaces and those
 * surfaces are not implemented.
 */
bool SurfaceImpl::Initialised() { return true; }
/** Unused; return value irrelevant. */
int SurfaceImpl::LogPixelsY() { return 1; }
/** Returns 1 since one "pixel" is always 1 character cell in curses. */
int SurfaceImpl::PixelDivisions() { return 1; }
/** Returns 1 since font height is always 1 in curses. */
int SurfaceImpl::DeviceHeightFont(int points) { return 1; }
/**
 * Drawing lines is not implemented because more often than not lines are being
 * drawn for decoration (e.g. line markers, underlines, indicators, arrows,
 * etc.).
 */
void SurfaceImpl::LineDraw(Point start, Point end, Stroke stroke) {}
void SurfaceImpl::PolyLine(const Point *pts, size_t npts, Stroke stroke) {}
/**
 * Draws the character equivalent of shape outlined by the given polygon's
 * points. Scintilla only calls this method for CallTip arrows and
 * INDIC_POINT[CHARACTER]. Assume the former. Line markers that Scintilla would
 * normally draw as polygons are handled in `DrawLineMarker()`.
 */
void SurfaceImpl::Polygon(const Point *pts, size_t npts,
                          FillStroke fillStroke) {
#ifdef DEBUG
  fprintf(stderr, "Polygon\n");
  fprintf(stderr, "pts[0].x = %d, pts[0].y = %d\n", static_cast<int>(pts[0].x),
          static_cast<int>(pts[0].y));
#endif
  int top = reinterpret_cast<TermboxWin *>(win)->top;
  int left = reinterpret_cast<TermboxWin *>(win)->left;
  ColourRGBA &back = fillStroke.fill.colour;

  if (pts[0].y < pts[npts - 1].y) // up arrow
    tb_change_cell(left + static_cast<int>(pts[npts - 1].x - 2),
                   top + static_cast<int>(pts[0].y), 0x25B2, 0x000000,
                   to_rgb(back));
  else if (pts[0].y > pts[npts - 1].y) // down arrow
    tb_change_cell(left + static_cast<int>(pts[npts - 1].x - 2),
                   top + static_cast<int>(pts[0].y - 2), 0x25BC, 0x000000,
                   to_rgb(back));
}

/**
 * Scintilla will never call this method.
 * Line markers that Scintilla would normally draw as rectangles are handled in
 * `DrawLineMarker()`.
 */
void SurfaceImpl::RectangleDraw(PRectangle rc, FillStroke fillStroke) {}

/**
 * Drawing framed rectangles like fold display text, EOL annotations, and
 * INDIC_BOX is not implemented.
 */
void SurfaceImpl::RectangleFrame(PRectangle rc, Stroke stroke) {}

/**
 * Clears the given portion of the screen with the given background color.
 * In some cases, it can be determined that whitespace is being drawn. If so,
 * draw it appropriately instead of clearing the given portion of the screen.
 */
void SurfaceImpl::FillRectangle(PRectangle rc, Fill fill) {
#ifdef DEBUG
  fprintf(stderr,
          "FillRectangle win = %x\t pattern = %d, (%lf, %lf, %lf, %lf) %x\n",
          win, pattern, rc.left, rc.top, rc.right, rc.bottom,
          fill.colour.OpaqueRGB());
#endif
  if (pattern == true) {
    pattern_colour = fill.colour;
    return;
  }
  if (!win)
    return;

  // wattr_set(win, 0, term_color_pair(COLOR_WHITE, fill.colour), nullptr);
  char ch = ' ';
  if (fabs(rc.left - static_cast<int>(rc.left)) > 0.1) {
#ifdef DEBUG
    fprintf(stderr, "fractional\n");
#endif
    // If rc.left is a fractional value (e.g. 4.5) then whitespace dots are
    // being drawn. Draw them appropriately.
    // TODO: set color to vs.whitespaceColours.fore and back.
    //      wcolor_set(win, term_color_pair(COLOR_BLACK, COLOR_BLACK), nullptr);
    //      rc.right = static_cast<int>(rc.right), ch = ACS_BULLET | A_BOLD;
  }
  // int right = std::min(static_cast<int>(rc.right),
  // reinterpret_cast<TermboxWin *>(win)->right);
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
 * Identical to `FillRectangle()` since suecial alignment to pixel boundaries is
 * not needed.
 */
void SurfaceImpl::FillRectangleAligned(PRectangle rc, Fill fill) {
  FillRectangle(rc, fill);
}

/**
 * Instead of filling a portion of the screen with a surface pixmap, fills the
 * the screen portion with black.
 */
void SurfaceImpl::FillRectangle(PRectangle rc, Surface &surfacePattern) {
#ifdef DEBUG
  fprintf(stderr, "FillRctangle with SurfacePattern (%f, %f) -> (%f, %f)\n",
          rc.left, rc.top, rc.right, rc.bottom);
#endif
  SurfaceImpl &surfi = dynamic_cast<SurfaceImpl &>(surfacePattern);
  if (surfi.pattern == true) {
#ifdef DEBUG
    fprintf(stderr, "FillRectangle pattern %x\n", surfi.pattern_colour);
#endif
    FillRectangle(rc, surfi.pattern_colour);
  } else {
    FillRectangle(rc, ColourRGBA(0, 0, 0));
  }
}

/**
 * Scintilla will never call this method.
 * Line markers that Scintilla would normally draw as rounded rectangles are
 * handled in `DrawLineMarker()`.
 */
void SurfaceImpl::RoundedRectangle(PRectangle rc, FillStroke fillStroke) {}
/**
 * Drawing alpha rectangles is not fully supported.
 * Instead, fills the background color of the given rectangle with the fill
 * color, emulating INDIC_STRAIGHTBOX with no transparency. This is called by
 * Scintilla to draw INDIC_ROUNDBOX and INDIC_STRAIGHTBOX indicators, text
 * blobs, and translucent line states and selections.
 */
void SurfaceImpl::AlphaRectangle(PRectangle rc, XYPOSITION cornerSize,
                                 FillStroke fillStroke) {
#ifdef DEBUG
  fprintf(stderr, "AlphaRectangle\n");
#endif
}

/** Drawing gradients is not implemented. */
void SurfaceImpl::GradientRectangle(PRectangle rc,
                                    const std::vector<ColourStop> &stops,
                                    GradientOptions options) {}
/** Drawing images is not implemented. */
void SurfaceImpl::DrawRGBAImage(PRectangle rc, int width, int height,
                                const unsigned char *pixelsImage) {}
/**
 * Scintilla will never call this method.
 * Line markers that Scintilla would normally draw as circles are handled in
 * `DrawLineMarker()`.
 */
void SurfaceImpl::Ellipse(PRectangle rc, FillStroke fillStroke) {}

/** Drawing curved ends on EOL annotations is not implemented. */
void SurfaceImpl::Stadium(PRectangle rc, FillStroke fillStroke, Ends ends) {}
/**
 * Draw an indentation guide.
 * Scintilla will only call this method when drawing indentation guides or
 * during certain drawing operations when double buffering is enabled. Since the
 * latter is not supported, assume the former.
 */
void SurfaceImpl::Copy(PRectangle rc, Point from, Surface &surfaceSource) {
#ifdef DEBUG
  fprintf(stderr, "Copy\n");
#endif
}

/** Bidirectional input is not implemented. */
std::unique_ptr<IScreenLineLayout>
SurfaceImpl::Layout(const IScreenLine *screenLine) {
  return nullptr;
}

/**
 * Draws the given text at the given position on the screen with the given
 * foreground and background colors. Takes into account any clipping boundaries
 * previously specified.
 */
void SurfaceImpl::DrawTextNoClip(PRectangle rc, const Font *font_,
                                 XYPOSITION ybase, std::string_view text,
                                 ColourRGBA fore, ColourRGBA back) {
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
    if (chars > clip_chars)
      break;
  }
#ifdef DEBUG
  fprintf(stderr, "%ld(%d, %d, %d, %06x, %06x)[%s]\n", bytes, x, y,
          (int)rc.bottom, fore.OpaqueRGB(), back.OpaqueRGB(), text.data());
#endif
  /*
      for (int i = 0; i < bytes; i++) {
        tb_change_cell(x, y, text.at(i), fore.OpaqueRGB() | attrs,
     back.OpaqueRGB()); x++;
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
 * Scintilla calls this method for drawing the caret, text blobs, and
 * `MarkerSymbol::Character` line markers. When drawing control characters, *rc*
 * needs to have its pixel padding removed since curses has smaller resolution.
 * Similarly when drawing line markers, *rc* needs to be reshaped.
 * @see DrawTextNoClip
 */
void SurfaceImpl::DrawTextClipped(PRectangle rc, const Font *font_,
                                  XYPOSITION ybase, std::string_view text,
                                  ColourRGBA fore, ColourRGBA back) {
  if (rc.left >= rc.right) // when drawing text blobs
    rc.left -= 2, rc.right -= 2, rc.top -= 1, rc.bottom -= 1;
  DrawTextNoClip(rc, font_, ybase, text, fore, back);
}
/**
 * Similar to `DrawTextNoClip()`.
 * Scintilla calls this method for drawing CallTip text and two-phase buffer
 * text.
 */
void SurfaceImpl::DrawTextTransparent(PRectangle rc, const Font *font_,
                                      XYPOSITION ybase, std::string_view text,
                                      ColourRGBA fore) {
  if (static_cast<int>(rc.top) > reinterpret_cast<TermboxWin *>(win)->bottom)
    return;
  int y = reinterpret_cast<TermboxWin *>(win)->top + static_cast<int>(rc.top);
  int x = reinterpret_cast<TermboxWin *>(win)->left + static_cast<int>(rc.left);
  struct tb_cell *buffer = tb_cell_buffer();
  int tb_color = buffer[y * tb_width() + x].bg;
  DrawTextNoClip(rc, font_, ybase, text, fore,
                 ColourRGBA(tb_color >> 16, (tb_color & 0x00ff00) >> 8,
                            tb_color & 0x0000ff));
}
/**
 * Measures the width of characters in the given string and writes them to the
 * given position list. Curses characters always have a width of 1 if they are
 * not UTF-8 trailing bytes.
 */
void SurfaceImpl::MeasureWidths(const Font *font_, std::string_view text,
                                XYPOSITION *positions) {
  for (size_t i = 0, j = 0; i < text.length(); i++) {
    if (!UTF8IsTrailByte(static_cast<unsigned char>(text[i])))
      j += grapheme_width(text.data() + i);
    positions[i] = j;
  }
}

/**
 * Returns the number of UTF-8 characters in the given string since curses
 * characters always have a width of 1.
 */
XYPOSITION SurfaceImpl::WidthText(const Font *font_, std::string_view text) {
  int width = 0;
  for (size_t i = 0; i < text.length(); i++)
    if (!UTF8IsTrailByte(static_cast<unsigned char>(text[i])))
      width += grapheme_width(text.data() + i);
  return width;
}
/** Identical to `DrawTextNoClip()` since UTF-8 is assumed. */
void SurfaceImpl::DrawTextNoClipUTF8(PRectangle rc, const Font *font_,
                                     XYPOSITION ybase, std::string_view text,
                                     ColourRGBA fore, ColourRGBA back) {
  DrawTextNoClip(rc, font_, ybase, text, fore, back);
}
/** Identical to `DrawTextClipped()` since UTF-8 is assumed. */
void SurfaceImpl::DrawTextClippedUTF8(PRectangle rc, const Font *font_,
                                      XYPOSITION ybase, std::string_view text,
                                      ColourRGBA fore, ColourRGBA back) {
  DrawTextClipped(rc, font_, ybase, text, fore, back);
}
/** Identical to `DrawTextTransparent()` since UTF-8 is assumed. */
void SurfaceImpl::DrawTextTransparentUTF8(PRectangle rc, const Font *font_,
                                          XYPOSITION ybase,
                                          std::string_view text,
                                          ColourRGBA fore) {
  DrawTextTransparent(rc, font_, ybase, text, fore);
}
/** Identical to `MeasureWidths()` since UTF-8 is assumed. */
void SurfaceImpl::MeasureWidthsUTF8(const Font *font_, std::string_view text,
                                    XYPOSITION *positions) {
  MeasureWidths(font_, text, positions);
}

/** Identical to `WidthText()` since UTF-8 is assumed. */
XYPOSITION SurfaceImpl::WidthTextUTF8(const Font *font_,
                                      std::string_view text) {
  return WidthText(font_, text);
}
/** Returns 0 since curses characters have no ascent. */
XYPOSITION SurfaceImpl::Ascent(const Font *font_) { return 0; }
/** Returns 0 since curses characters have no descent. */
XYPOSITION SurfaceImpl::Descent(const Font *font_) { return 0; }
/** Returns 0 since curses characters have no leading. */
XYPOSITION SurfaceImpl::InternalLeading(const Font *font_) { return 0; }
/** Returns 1 since curses characters always have a height of 1. */
XYPOSITION SurfaceImpl::Height(const Font *font_) { return 1; }
/** Returns 1 since curses characters always have a width of 1. */
XYPOSITION SurfaceImpl::AverageCharWidth(const Font *font_) { return 1; }

/**
 * Ensure text to be drawn in subsequent calls to `DrawText*()` is drawn within
 * the given rectangle. This is needed in order to prevent long lines from
 * overwriting margin text when scrolling to the right.
 */
void SurfaceImpl::SetClip(PRectangle rc) {
  clip.left = rc.left, clip.top = rc.top;
  clip.right = rc.right, clip.bottom = rc.bottom;
}
/** Remove the clip set in `SetClip()`. */
void SurfaceImpl::PopClip() {
  clip.left = 0, clip.top = 0, clip.right = 0, clip.bottom = 0;
}
/** Flushing cache is not implemented. */
void SurfaceImpl::FlushCachedState() {}
/** Flushing is not implemented since surface pixmaps are not implemented. */
void SurfaceImpl::FlushDrawing() {}

/** Draws the text representation of a line marker, if possible. */
void SurfaceImpl::DrawLineMarker(const PRectangle &rcWhole,
                                 const Font *fontForCharacter, int tFold,
                                 const void *data) {
  int top = reinterpret_cast<TermboxWin *>(win)->top;
  int left = reinterpret_cast<TermboxWin *>(win)->left;

  // TODO: handle fold marker highlighting.
  const LineMarker *marker = reinterpret_cast<const LineMarker *>(data);
  // wattr_set(win, 0, term_color_pair(marker->fore, marker->back), nullptr);
#ifdef DEBUG
  fprintf(stderr, "drawlinemarker %d (%d, %d) -> (%f, %f)\n", marker->markType,
          left, top, rcWhole.left, rcWhole.top);
#endif
  switch (marker->markType) {
  case MarkerSymbol::Circle:
    tb_change_cell(left + rcWhole.left, top + rcWhole.top, 0x25CF,
                   to_rgb(marker->fore), to_rgb(marker->back));
    return;
  case MarkerSymbol::SmallRect:
  case MarkerSymbol::RoundRect:
    tb_change_cell(left + rcWhole.left, top + rcWhole.top, 0x25A0,
                   to_rgb(marker->fore), to_rgb(marker->back));
    return;
  case MarkerSymbol::Arrow:
    tb_change_cell(left + rcWhole.left, top + rcWhole.top, 0x25B6,
                   to_rgb(marker->fore), to_rgb(marker->back));
    return;
  case MarkerSymbol::ShortArrow:
    tb_change_cell(left + rcWhole.left, top + rcWhole.top, 0x2192,
                   to_rgb(marker->fore), to_rgb(marker->back));
    return;
  case MarkerSymbol::Empty:
    tb_change_cell(left + rcWhole.left, top + rcWhole.top, ' ',
                   to_rgb(marker->fore), to_rgb(marker->back));
    return;
  case MarkerSymbol::ArrowDown:
    tb_change_cell(left + rcWhole.left, top + rcWhole.top, 0x25BC,
                   to_rgb(marker->fore), to_rgb(marker->back));
    return;
  case MarkerSymbol::Minus:
    tb_change_cell(left + rcWhole.left, top + rcWhole.top, 0x2500,
                   to_rgb(marker->fore), to_rgb(marker->back));
    return;
  case MarkerSymbol::BoxMinus:
  case MarkerSymbol::BoxMinusConnected:
    tb_change_cell(left + rcWhole.left, top + rcWhole.top, 0x229F,
                   to_rgb(marker->fore), to_rgb(marker->back));
    return;
  case MarkerSymbol::CircleMinus:
  case MarkerSymbol::CircleMinusConnected:
    tb_change_cell(left + rcWhole.left, top + rcWhole.top, 0x2295,
                   to_rgb(marker->fore), to_rgb(marker->back));
    return;
  case MarkerSymbol::Plus:
    tb_change_cell(left + rcWhole.left, top + rcWhole.top, 0x253C,
                   to_rgb(marker->fore), to_rgb(marker->back));
    return;
  case MarkerSymbol::BoxPlus:
  case MarkerSymbol::BoxPlusConnected:
    tb_change_cell(left + rcWhole.left, top + rcWhole.top, 0x229E,
                   to_rgb(marker->fore), to_rgb(marker->back));
    return;
  case MarkerSymbol::CirclePlus:
  case MarkerSymbol::CirclePlusConnected:
    tb_change_cell(left + rcWhole.left, top + rcWhole.top, 0x2296,
                   to_rgb(marker->fore), to_rgb(marker->back));
    return;
  case MarkerSymbol::VLine:
    tb_change_cell(left + rcWhole.left, top + rcWhole.top, 0x2502,
                   to_rgb(marker->fore), to_rgb(marker->back));
    return;
  case MarkerSymbol::LCorner:
  case MarkerSymbol::LCornerCurve:
    tb_change_cell(left + rcWhole.left, top + rcWhole.top, 0x2514,
                   to_rgb(marker->fore), to_rgb(marker->back));
    return;
  case MarkerSymbol::TCorner:
  case MarkerSymbol::TCornerCurve:
    tb_change_cell(left + rcWhole.left, top + rcWhole.top, 0x251C,
                   to_rgb(marker->fore), to_rgb(marker->back));
    return;
  case MarkerSymbol::DotDotDot:
    tb_change_cell(left + rcWhole.left, top + rcWhole.top, 0x22EF,
                   to_rgb(marker->fore), to_rgb(marker->back));
    return;
  case MarkerSymbol::Arrows:
    tb_change_cell(left + rcWhole.left, top + rcWhole.top, 0x22D9,
                   to_rgb(marker->fore), to_rgb(marker->back));
    return;
  case MarkerSymbol::FullRect:
    FillRectangle(rcWhole, marker->back);
    return;
  case MarkerSymbol::LeftRect:
    tb_change_cell(left + rcWhole.left, top + rcWhole.top, 0x258E,
                   to_rgb(marker->fore), to_rgb(marker->back));
    return;
  case MarkerSymbol::Bookmark:
    tb_change_cell(left + rcWhole.left, top + rcWhole.top, 0x2211,
                   to_rgb(marker->fore), to_rgb(marker->back));
    return;
  case MarkerSymbol::Bar:
    tb_change_cell(left + rcWhole.left, top + rcWhole.top, 0x2590,
                   to_rgb(marker->fore), to_rgb(marker->back));
    return;
  default:
    break; // prevent warning
  }
  if (marker->markType >= MarkerSymbol::Character) {
    char ch = static_cast<char>(static_cast<int>(marker->markType) -
                                static_cast<int>(MarkerSymbol::Character));
    DrawTextClipped(rcWhole, fontForCharacter, rcWhole.bottom,
                    std::string(&ch, 1), marker->fore, marker->back);
    return;
  }
#ifdef DEBUG
  fprintf(stderr, "DrawLineMarker %d\n", static_cast<int>(marker->markType));
#endif
}
/** Draws the text representation of a wrap marker. */
void SurfaceImpl::DrawWrapMarker(PRectangle rcPlace, bool isEndMarker,
                                 ColourRGBA wrapColour) {
#ifdef DEBUG
  fprintf(stderr, "DrawWrapMaker\n");
#endif
}
/** Draws the text representation of a tab arrow. */
void SurfaceImpl::DrawTabArrow(PRectangle rcTab, const ViewStyle &vsDraw) {
#ifdef DEBUG
  fprintf(stderr, "DrawTabArrow\n");
#endif
}

/** Creates a new curses surface. */
std::unique_ptr<Surface> Surface::Allocate(Technology) {
  return std::make_unique<SurfaceImpl>();
}

// Window handling.

/** Deletes the window. */
Window::~Window() noexcept {}
/**
 * Releases the window's resources.
 * Since the only Windows created are AutoComplete and CallTip windows, and
 * since those windows are created in `ListBox::Create()` and
 * `ScintillaCurses::CreateCallTipWindow()`, respectively, via `newwin()`, it is
 * safe to use `delwin()`. It is important to note that even though
 * `ScintillaCurses::wMain` is a Window, its `Destroy()` function is never
 * called, hence why `scintilla_delete()` is the complement to
 * `scintilla_new()`.
 */
void Window::Destroy() noexcept { wid = nullptr; }
/**
 * Returns the window's boundaries.
 * Unlike other platforms, Scintilla paints in coordinates relative to the
 * window in curses. Therefore, this function should always return the window
 * bounds to ensure all of it is painted.
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
  if (x < begx)
    x = begx;
  y = begy + rc.top;
  if (y < begy)
    y = begy;
  // Correct to fit the parent if necessary.
  int sizex = rc.right - rc.left;
  int sizey = rc.bottom - rc.top;
  int screen_width =
      reinterpret_cast<TermboxWin *>(relativeTo->GetID())->Width();
  int screen_height =
      reinterpret_cast<TermboxWin *>(relativeTo->GetID())->Height();
  if (sizex > screen_width)
    x = begx; // align left
  else if (x + sizex > begx + screen_width)
    x = begx + screen_width - sizex; // align right
  if (y + sizey > begy + screen_height) {
    y = begy + screen_height - sizey; // align bottom
    if (screen_height == 1)
      y--; // show directly above the relative window
  }
  if (y < 0)
    y = begy; // align top
  // Update the location.
  reinterpret_cast<TermboxWin *>(wid)->Move(x, y);
#ifdef DEBUG
  fprintf(stderr, "SetPositionRelative %d, %d\n", x, y);
#endif
}
/** Identical to `Window::GetPosition()`. */
PRectangle Window::GetClientPosition() const { return GetPosition(); }
void Window::Show(bool show) {}                    // TODO:
void Window::InvalidateAll() {}                    // notify repaint
void Window::InvalidateRectangle(PRectangle rc) {} // notify repaint
/** Setting the cursor icon is not implemented. */
void Window::SetCursor(Cursor curs) {}
/** Identical to `Window::GetPosition()`. */
PRectangle Window::GetMonitorRect(Point pt) { return GetPosition(); }

// ListBoxImpl

/** Allocates a new Scintilla ListBox for curses. */
ListBoxImpl::ListBoxImpl() {
  list.reserve(10);
  ClearRegisteredImages();
}

/** Setting the font is not implemented. */
void ListBoxImpl::SetFont(const Font *font) {}
/**
 * Creates a new listbox.
 * The `Show()` function resizes window with the appropriate height and width.
 */
void ListBoxImpl::Create(Window &parent, int ctrlID, Point location_,
                         int lineHeight_, bool unicodeMode_,
                         Technology technology_) {
  wid = new TermboxWin(0, 0, 1, 1);
}
/**
 * Setting average char width is not implemented since all curses characters
 * have a width of 1.
 */
void ListBoxImpl::SetAverageCharWidth(int width) {}
/** Sets the number of visible rows in the listbox. */
void ListBoxImpl::SetVisibleRows(int rows) {
  height = rows;
  if (wid) {
    reinterpret_cast<TermboxWin *>(wid)->bottom =
        reinterpret_cast<TermboxWin *>(wid)->top + height - 1;
  }
}
/** Returns the number of visible rows in the listbox. */
int ListBoxImpl::GetVisibleRows() const { return height; }
/** Returns the desired size of the listbox. */
PRectangle ListBoxImpl::GetDesiredRect() {
  return PRectangle(0, 0, width, height); // add border widths
}
/**
 * Returns the left-offset of the ListBox with respect to the caret.
 * Takes into account the border width and type character width.
 * @return 2 to shift the ListBox to the left two characters.
 */
int ListBoxImpl::CaretFromEdge() { return 2; }
/** Clears the contents of the listbox. */
void ListBoxImpl::Clear() noexcept {
  list.clear();
  width = 0;
}
/**
 * Adds the given string list item to the listbox.
 * Prepends the item's type character (if any) to the list item for display.
 */
void ListBoxImpl::Append(char *s, int type) {
  if (type >= 0 && type <= IMAGE_MAX) {
    char *chtype = types[type];
    list.push_back(std::string(chtype, strlen(chtype)) + s);
  } else
    list.push_back(std::string(" ") + s);
  int len = strlen(s); // TODO: UTF-8 awareness?
  if (width < len + 2) {
    width = len + 2; // include type character len
  }
  reinterpret_cast<TermboxWin *>(wid)->right =
      reinterpret_cast<TermboxWin *>(wid)->left + width - 1;
  reinterpret_cast<TermboxWin *>(wid)->bottom =
      reinterpret_cast<TermboxWin *>(wid)->top + height - 1;
}
/** Returns the number of items in the listbox. */
int ListBoxImpl::Length() { return list.size(); }
/** Selects the given item in the listbox and repaints the listbox. */
void ListBoxImpl::Select(int n) {
  int fore = 0;
  int back = 0;
  int left = reinterpret_cast<TermboxWin *>(wid)->left;
  int top = reinterpret_cast<TermboxWin *>(wid)->top;

  int len = static_cast<int>(list.size());
  int s = n - height / 2;
  if (s + height > len)
    s = len - height;
  if (s < 0)
    s = 0;
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
      int text_offset = 1;
      int text_width = 0;
      int x = 1;
      const char *str = list.at(i).c_str();
      while (*str) {
        uint32_t uni;
        text_width = grapheme_width(str + text_offset);
        text_offset += utf8_char_to_unicode(&uni, str + text_offset);
        tb_change_cell(left + x, top + i - s, uni, fore, back);
        x += text_width;
        if (text_offset >= list.at(i).size()) {
          break;
        }
      }
      for (int j = x; j < width; j++) {
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
  if (delegate) {
    ListBoxEvent event(ListBoxEvent::EventType::selectionChange);
    delegate->ListNotify(&event);
  }
}
/** Returns the currently selected item in the listbox. */
int ListBoxImpl::GetSelection() { return selection; }
/**
 * Searches the listbox for the items matching the given prefix string and
 * returns the index of the first match. Since the type is displayed as the
 * first character, the value starts on the second character; match strings
 * starting there.
 */
int ListBoxImpl::Find(const char *prefix) {
  int len = strlen(prefix);
  for (unsigned int i = 0; i < list.size(); i++) {
    const char *item = list.at(i).c_str();
    item += UTF8DrawBytes(reinterpret_cast<const char *>(item), strlen(item));
    if (strncmp(prefix, item, len) == 0)
      return i;
  }
  return -1;
}
/**
 * Returns the string item in the listbox at the given index.
 * Since the type is displayed as the first character, the value starts on the
 * second character.
 */
std::string ListBoxImpl::GetValue(int n) {
  const char *item = list.at(n).c_str();
  item += UTF8DrawBytes(reinterpret_cast<const char *>(item), strlen(item));
  return item;
}
/**
 * Registers the first UTF-8 character of the given string to the given type.
 * By default, ' ' (space) is registered to all types.
 * @usage SCI_REGISTERIMAGE(1, "*") // type 1 shows '*' in front of list item.
 * @usage SCI_REGISTERIMAGE(2, "+") // type 2 shows '+' in front of list item.
 * @usage SCI_REGISTERIMAGE(3, "■") // type 3 shows '■' in front of list item.
 */
void ListBoxImpl::RegisterImage(int type, const char *xpm_data) {
  if (type < 0 || type > IMAGE_MAX)
    return;
  int len =
      UTF8DrawBytes(reinterpret_cast<const char *>(xpm_data), strlen(xpm_data));
  for (int i = 0; i < len; i++)
    types[type][i] = xpm_data[i];
  types[type][len] = '\0';
}
/** Registering images is not implemented. */
void ListBoxImpl::RegisterRGBAImage(int type, int width, int height,
                                    const unsigned char *pixelsImage) {}
/** Clears all registered types back to ' ' (space). */
void ListBoxImpl::ClearRegisteredImages() {
  for (int i = 0; i <= IMAGE_MAX; i++)
    types[i][0] = ' ', types[i][1] = '\0';
}
/** Defines the delegate for ListBox actions. */
void ListBoxImpl::SetDelegate(IListBoxDelegate *lbDelegate) {
  delegate = lbDelegate;
}
/** Sets the list items in the listbox to the given items. */
void ListBoxImpl::SetList(const char *listText, char separator, char typesep) {
  Clear();
  int len = strlen(listText);
  char *text = new char[len + 1];
  if (!text)
    return;
  memcpy(text, listText, len + 1);
  char *word = text, *type = nullptr;
  for (int i = 0; i <= len; i++) {
    if (text[i] == separator || i == len) {
      text[i] = '\0';
      if (type)
        *type = '\0';
      Append(word, type ? atoi(type + 1) : -1);
      word = text + i + 1, type = nullptr;
    } else if (text[i] == typesep)
      type = text + i;
  }
  delete[] text;
}
/** List options are not implemented. */
void ListBoxImpl::SetOptions(ListOptions options_) {}

/** Creates a new Scintilla ListBox. */
ListBox::ListBox() noexcept = default;
/** Deletes the ListBox. */
ListBox::~ListBox() noexcept = default;
/** Creates a new curses ListBox. */
std::unique_ptr<ListBox> ListBox::Allocate() {
  return std::make_unique<ListBoxImpl>();
}

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
void Platform::DebugDisplay(const char *s) noexcept {
  fprintf(stderr, "%s", s);
}
void Platform::DebugPrintf(const char *format, ...) noexcept {}
// bool Platform::ShowAssertionPopUps(bool assertionPopUps_) noexcept { return
// true; }
void Platform::Assert(const char *c, const char *file, int line) noexcept {
  char buffer[2000];
  snprintf(buffer, sizeof(buffer), "Assertion [%s] failed at %s %d\r\n", c, file, line);
  Platform::DebugDisplay(buffer);
  abort();
}

} // namespace Scintilla::Internal