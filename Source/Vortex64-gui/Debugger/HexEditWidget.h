#pragma once

#include <QColor>
#include <QWidget>
#include <functional>
#include <vector>

class QTimer;

enum HxColumn
{
    HX_COL_NONE = -1,
    HX_COL_ADDRESS,
    HX_COL_HEXDATA,
    HX_COL_ASCII
};

// HXBYTEINFO: what the owner tells the control about one byte (colours are 0xRRGGBB)
struct HxByteInfo
{
    bool hidden;
    bool valid;
    uint8_t value;
    uint32_t color;
    uint32_t bkColor;
    bool operator==(const HxByteInfo & b) const
    {
        return hidden == b.hidden && valid == b.valid && value == b.value && color == b.color && bkColor == b.bkColor;
    }
    bool operator!=(const HxByteInfo & b) const
    {
        return !(*this == b);
    }
};

// Qt port of CHexEditCtrl (WTLControls/HexEditCtrl.cpp): the hex/ASCII editor of the memory
// view. The WM_NOTIFY messages (HXN_*) are callbacks that the owner sets.
class HexEditWidget : public QWidget
{
    Q_OBJECT
public:
    explicit HexEditWidget(QWidget * Parent = nullptr);

    // HXN_GETBYTEINFO: fill NewBytes[0..NumBytes) for the bytes at Address
    std::function<void(uint32_t Address, size_t NumBytes, bool IgnoreDiff, HxByteInfo * OldBytes, HxByteInfo * NewBytes)> OnGetByteInfo;
    std::function<void()> OnRedrawStarted;
    std::function<void(bool Insert, uint32_t Address, uint8_t Value)> OnSetByte;
    std::function<void(bool Insert, uint32_t Address, bool LoNibble, uint8_t Value)> OnSetNibble;
    std::function<void(bool Insert, uint32_t Start, uint32_t End, uint8_t Value)> OnFillRange;
    std::function<void(uint32_t Address)> OnRightClick;
    std::function<void()> OnInsertModeChanged;
    std::function<void()> OnHotAddrChanged;
    std::function<void()> OnBaseAddrChanged;
    std::function<void()> OnGroupSizeChanged;
    std::function<void()> OnSelectionChanged;
    std::function<void(int QtKey)> OnCtrlKeyPressed;
    std::function<void()> OnEnterPressed;
    std::function<void()> OnCopy;
    // HXN_PASTE: returns the number of bytes pasted
    std::function<int(uint32_t Address, HxColumn Column)> OnPaste;

    static char ByteAscii(uint8_t Value);
    static uint8_t HexCharValue(char C);

    void Draw();
    void Copy();
    void Paste(bool AdvanceCaret = true);
    void SetBaseAddress(uint32_t Address);
    void SetByteGroupSize(int NumBytes);
    uint32_t GetBaseAddress() const
    {
        return m_BaseAddress;
    }
    uint32_t GetCaretAddress() const
    {
        return m_CaretAddress;
    }
    uint32_t GetHotAddress() const
    {
        return m_HotAddress;
    }
    int GetNumBytesPerGroup() const
    {
        return m_NumBytesPerGroup;
    }
    int GetNumBytesPerRow() const
    {
        return m_NumBytesPerRow;
    }
    int GetNumVisibleBytes() const
    {
        return m_NumVisibleBytes;
    }
    bool GetSelectionRange(uint32_t * StartAddress, uint32_t * EndAddress) const;
    HxColumn GetFocusedColumn() const
    {
        return m_FocusedColumn;
    }
    bool GetInsertMode() const
    {
        return m_InsertMode;
    }
    // The key handling of WM_KEYDOWN (also used for the context menu's Safe mode / Zero-fill)
    void KeyDown(int QtKey, Qt::KeyboardModifiers Modifiers = Qt::NoModifier);
    void CharTyped(const QString & Text);

    // Test hooks
    void SelectRange(uint32_t Start, uint32_t End, HxColumn Column = HX_COL_HEXDATA);
    void SetCaret(uint32_t Address, bool LoNibble = false, HxColumn Column = HX_COL_HEXDATA);
    const std::vector<HxByteInfo> & Bytes() const
    {
        return m_NewBytes;
    }

protected:
    void paintEvent(QPaintEvent * Event) override;
    void resizeEvent(QResizeEvent * Event) override;
    void mousePressEvent(QMouseEvent * Event) override;
    void mouseReleaseEvent(QMouseEvent * Event) override;
    void mouseMoveEvent(QMouseEvent * Event) override;
    void mouseDoubleClickEvent(QMouseEvent * Event) override;
    void wheelEvent(QWheelEvent * Event) override;
    void keyPressEvent(QKeyEvent * Event) override;
    void focusInEvent(QFocusEvent * Event) override;
    void focusOutEvent(QFocusEvent * Event) override;
    bool event(QEvent * Event) override;
    QSize sizeHint() const override;

private:
    enum CellSide
    {
        HX_LEFT,
        HX_RIGHT
    };
    struct HitTestResult
    {
        HxColumn column;
        uint32_t asciiAddress;
        CellSide asciiCellSide;
        uint32_t hexAddress;
        CellSide hexCellSide;
    };
    enum
    {
        BKCOLOR_DEFAULT = 0xFFFFFF,
        BKCOLOR_ADDR = 0xDCDCDC,
        COLOR_ADDR = 0x282828,
        BKCOLOR_SEL_FOCUSED = 0x3399FF,
        COLOR_SEL_FOCUSED = 0xFFFFFF,
        BKCOLOR_SEL_UNFOCUSED = 0xC8C8C8,
        COLOR_SEL_UNFOCUSED = 0x000000,
        BKCOLOR_HOT = 0x8C8C8C
    };

    static uint32_t BlendColor(uint32_t C1, uint32_t C2);
    static uint32_t SatAdd32(uint32_t A, uint32_t B);
    static uint32_t SatAdd32(uint32_t A, int B);
    void DragScrollTick();
    bool IsSelected(uint32_t Address) const;
    int GetSelDirection() const;
    void CancelSelection();
    void SelectAllVisible();
    void UpdateRealSelection();
    uint32_t LineAddress(uint32_t Address) const;
    QRect HexCellRect(int Index) const;
    QRect AsciiCellRect(int Index) const;
    HitTestResult HitTest(int X, int Y) const;
    void CaretIncrementNibble();
    void CaretDecrementNibble();
    bool UpdateCaretUI(bool EnsureVisible, bool Top = false);
    void EnsureCaretAddressVisible(bool Top = false);
    bool IsCaretAddressVisible() const;
    void UpdateLayoutInfo();
    void ReallocByteBuffers();
    QRect CaretRect() const;
    void DrawText(QPainter & Painter, int X, int Y, const QString & Text, uint32_t Bg, uint32_t Fg) const;

    uint32_t m_BaseAddress = 0x80000000;
    uint32_t m_DrawnBaseAddress = 0xFFFFFFFF;
    uint32_t m_SelStartAddress = 0;
    CellSide m_SelStartCellSide = HX_LEFT;
    uint32_t m_SelEndAddress = 0;
    CellSide m_SelEndCellSide = HX_LEFT;
    uint32_t m_RealSelStartAddress = 0;
    uint32_t m_RealSelEndAddress = 0;
    bool m_HaveRealSel = false;
    bool m_InsertMode = false;
    bool m_HaveCaret = false;
    bool m_CaretBlinkOn = true;
    uint32_t m_CaretAddress = 0;
    bool m_CaretLoNibble = false;
    bool m_ShowHotAddress = false;
    uint32_t m_HotAddress = 0;
    HxColumn m_FocusedColumn = HX_COL_NONE;
    int m_DragScrollDelta = 0;
    bool m_DblClicked = false;
    bool m_LButtonDown = false;
    bool m_MouseDragging = false;
    bool m_LayoutChanged = false;
    int m_CharWidth = 8;
    int m_CharHeight = 14;
    QRect m_AddressColumnRect, m_HexDataColumnRect, m_AsciiColumnRect;
    int m_NumBytesPerGroup = 4;
    int m_NumByteGroupsPerRow = 0;
    int m_NumBytesPerRow = 0;
    int m_NumVisibleRows = 0;
    int m_NumVisibleBytes = 0;
    std::vector<HxByteInfo> m_NewBytes, m_OldBytes;
    bool m_Dirty = true;
    QTimer * m_RefreshTimer;
    QTimer * m_DragTimer;
    QTimer * m_CaretTimer;
};
