#ifndef PLAT_TERMBOX_H
#define PLAT_TERMBOX_H

namespace Scintilla::Internal {
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

  class FontImpl : public Font {
    public:
    FontImpl(const FontParameters &fp);
    virtual ~FontImpl() noexcept override = default;

    uint32_t attrs = 0;
  };

  class SurfaceImpl : public Surface {
    PRectangle clip;
    WindowID win = nullptr;
    int width = 0;
    int height = 0;
    int pattern = false;
    ColourRGBA pattern_colour;

public:
    SurfaceImpl() = default;
    SurfaceImpl(int width, int height) noexcept;
    ~SurfaceImpl() noexcept override;

  void Init(WindowID wid) override;
  void Init(SurfaceID sid, WindowID wid) override;

  std::unique_ptr<Surface> AllocatePixMap(int width, int height) override;

  void SetMode(SurfaceMode mode) override;

  void Release() noexcept override;
  int SupportsFeature(Supports feature) noexcept override;
  bool Initialised() override;
  int LogPixelsY() override;
  int PixelDivisions() override;
  int DeviceHeightFont(int points) override;
  void LineDraw(Point start, Point end, Stroke stroke) override;
  void PolyLine(const Point *pts, size_t npts, Stroke stroke) override;
  void Polygon(const Point *pts, size_t npts, FillStroke fillStroke) override;
  void RectangleDraw(PRectangle rc, FillStroke fillStroke) override;
  void RectangleFrame(PRectangle rc, Stroke stroke) override;
  void FillRectangle(PRectangle rc, Fill fill) override;
  void FillRectangleAligned(PRectangle rc, Fill fill) override;
  void FillRectangle(PRectangle rc, Surface &surfacePattern) override;
  void RoundedRectangle(PRectangle rc, FillStroke fillStroke) override;
  void AlphaRectangle(PRectangle rc, XYPOSITION cornerSize, FillStroke fillStroke) override;
  void GradientRectangle(
    PRectangle rc, const std::vector<ColourStop> &stops, GradientOptions options) override;
  void DrawRGBAImage(
    PRectangle rc, int width, int height, const unsigned char *pixelsImage) override;
  void Ellipse(PRectangle rc, FillStroke fillStroke) override;
  void Stadium(PRectangle rc, FillStroke fillStroke, Ends ends) override;
  void Copy(PRectangle rc, Point from, Surface &surfaceSource) override;

  std::unique_ptr<IScreenLineLayout> Layout(const IScreenLine *screenLine) override;

  void DrawTextNoClip(PRectangle rc, const Font *font_, XYPOSITION ybase, std::string_view text,
    ColourRGBA fore, ColourRGBA back) override;
  void DrawTextClipped(PRectangle rc, const Font *font_, XYPOSITION ybase, std::string_view text,
    ColourRGBA fore, ColourRGBA back) override;
  void DrawTextTransparent(PRectangle rc, const Font *font_, XYPOSITION ybase,
    std::string_view text, ColourRGBA fore) override;
  void MeasureWidths(const Font *font_, std::string_view text, XYPOSITION *positions) override;
  XYPOSITION WidthText(const Font *font_, std::string_view text) override;

  void DrawTextNoClipUTF8(PRectangle rc, const Font *font_, XYPOSITION ybase, std::string_view text,
    ColourRGBA fore, ColourRGBA back) override;
  void DrawTextClippedUTF8(PRectangle rc, const Font *font_, XYPOSITION ybase,
    std::string_view text, ColourRGBA fore, ColourRGBA back) override;
  void DrawTextTransparentUTF8(PRectangle rc, const Font *font_, XYPOSITION ybase,
    std::string_view text, ColourRGBA fore) override;
  void MeasureWidthsUTF8(const Font *font_, std::string_view text, XYPOSITION *positions) override;
  XYPOSITION WidthTextUTF8(const Font *font_, std::string_view text) override;

  XYPOSITION Ascent(const Font *font_) override;
  XYPOSITION Descent(const Font *font_) override;
  XYPOSITION InternalLeading(const Font *font_) override;
  XYPOSITION Height(const Font *font_) override;
  XYPOSITION AverageCharWidth(const Font *font_) override;

  void SetClip(PRectangle rc) override;
  void PopClip() override;
  void FlushCachedState() override;
  void FlushDrawing() override;

  void DrawLineMarker(
    const PRectangle &rcWhole, const Font *fontForCharacter, int tFold, const void *data);
  void DrawWrapMarker(PRectangle rcPlace, bool isEndMarker, ColourRGBA wrapColour);
  void DrawTabArrow(PRectangle rcTab, const ViewStyle &vsDraw);

  bool isCallTip = false;
  };

class ListBoxImpl : public ListBox {
  int height = 5, width = 10;
  std::vector<std::string> list;
  char types[IMAGE_MAX + 1][5]; // UTF-8 character plus terminating '\0'
  int selection = 0;

public:
  IListBoxDelegate *delegate = nullptr;

  ListBoxImpl();
  ~ListBoxImpl() override = default;

  void SetFont(const Font *font) override;
  void Create(Window &parent, int ctrlID, Point location_, int lineHeight_, bool unicodeMode_,
    Technology technology_) override;
  void SetAverageCharWidth(int width) override;
  void SetVisibleRows(int rows) override;
  int GetVisibleRows() const override;
  PRectangle GetDesiredRect() override;
  int CaretFromEdge() override;
  void Clear() noexcept override;
  void Append(char *s, int type) override;
  int Length() override;
  void Select(int n) override;
  int GetSelection() override;
  int Find(const char *prefix) override;
  std::string GetValue(int n) override;
  void RegisterImage(int type, const char *xpm_data) override;
  void RegisterRGBAImage(
    int type, int width, int height, const unsigned char *pixelsImage) override;
  void ClearRegisteredImages() override;
  void SetDelegate(IListBoxDelegate *lbDelegate) override;
  void SetList(const char *listText, char separator, char typesep) override;
  void SetOptions(ListOptions options_) override;
};

}


#endif /* PLAT_TERMBOX_H */