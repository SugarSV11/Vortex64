#include "HexEditWidget.h"

#include <QApplication>
#include <QFocusEvent>
#include <QFontDatabase>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QTimer>
#include <QWheelEvent>
#include <algorithm>
#include <cstdlib>
#include <cstring>

HexEditWidget::HexEditWidget(QWidget * Parent) :
    QWidget(Parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAttribute(Qt::WA_OpaquePaintEvent);
    QFont Font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    if (QFontDatabase::families().contains("Consolas"))
    {
        Font = QFont("Consolas");
    }
    Font.setPixelSize(14);
    setFont(Font);
    const QFontMetrics Metrics(font());
    m_CharWidth = std::max(1, Metrics.horizontalAdvance(QChar('0')));
    m_CharHeight = Metrics.height();
    UpdateLayoutInfo();
    ReallocByteBuffers();

    m_RefreshTimer = new QTimer(this);
    connect(m_RefreshTimer, &QTimer::timeout, this, [this]() { Draw(); });
    m_RefreshTimer->start(20);
    m_DragTimer = new QTimer(this);
    connect(m_DragTimer, &QTimer::timeout, this, [this]() { DragScrollTick(); });
    m_DragTimer->start(50);
    m_CaretTimer = new QTimer(this);
    connect(m_CaretTimer, &QTimer::timeout, this, [this]() {
        if (m_HaveCaret)
        {
            m_CaretBlinkOn = !m_CaretBlinkOn;
            update(CaretRect());
        }
    });
    m_CaretTimer->start(530);
}

QSize HexEditWidget::sizeHint() const
{
    return QSize(m_CharWidth * (11 + 4 * (4 * 2 + 1 + 4)), m_CharHeight * 12);
}

char HexEditWidget::ByteAscii(uint8_t Value)
{
    // ISO 8859-1
    if ((Value >= 0x20 && Value <= 0x7E) || Value >= 0xA1)
    {
        return (char)Value;
    }
    return '.';
}

uint8_t HexEditWidget::HexCharValue(char C)
{
    if (C >= '0' && C <= '9') return (C - '0');
    if (C >= 'A' && C <= 'F') return (C - 'A') + 0x0A;
    if (C >= 'a' && C <= 'f') return (C - 'a') + 0x0A;
    return 0;
}

uint32_t HexEditWidget::SatAdd32(uint32_t A, int B)
{
    const int64_t C = (int64_t)A + B;
    if (C > UINT32_MAX)
    {
        return UINT32_MAX;
    }
    if (C < 0)
    {
        return 0;
    }
    return (uint32_t)C;
}

uint32_t HexEditWidget::SatAdd32(uint32_t A, uint32_t B)
{
    const uint32_t C = A + B;
    if (C < A)
    {
        return (uint32_t)-1;
    }
    return C;
}

uint32_t HexEditWidget::BlendColor(uint32_t C1, uint32_t C2)
{
    const int R1 = (C1 >> 16) & 0xFF, G1 = (C1 >> 8) & 0xFF, B1 = C1 & 0xFF;
    const int R2 = (C2 >> 16) & 0xFF, G2 = (C2 >> 8) & 0xFF, B2 = C2 & 0xFF;
    return (((R1 + R2 * 2) / 3) << 16) | (((G1 + G2 * 2) / 3) << 8) | ((B1 + B2 * 2) / 3);
}

// ---- layout ----

void HexEditWidget::UpdateLayoutInfo()
{
    const int AddressColumnWidth = 11 * m_CharWidth;
    const int ByteWidth = m_CharWidth * 2;
    const int ByteGroupWidth = (m_NumBytesPerGroup * ByteWidth) + (m_CharWidth * 1);
    const int AsciiGroupWidth = (m_NumBytesPerGroup * m_CharWidth);
    const int HeaderHeight = m_CharHeight;
    m_NumByteGroupsPerRow = std::max(0, (width() - AddressColumnWidth) / (ByteGroupWidth + AsciiGroupWidth));
    m_NumBytesPerRow = m_NumByteGroupsPerRow * m_NumBytesPerGroup;
    m_NumVisibleRows = std::max(0, (height() - HeaderHeight) / m_CharHeight);
    m_NumVisibleBytes = m_NumVisibleRows * m_NumBytesPerRow;

    const int HexDataColumnWidth = m_NumByteGroupsPerRow * ByteGroupWidth;
    const int AsciiColumnWidth = m_NumBytesPerRow * m_CharWidth;
    const int AddressColumnRight = AddressColumnWidth;
    const int HexDataColumnRight = AddressColumnRight + HexDataColumnWidth;
    const int AsciiColumnRight = HexDataColumnRight + AsciiColumnWidth;
    const int ColumnsTop = HeaderHeight;
    const int ColumnsBottom = ColumnsTop + m_NumVisibleRows * m_CharHeight;
    m_AddressColumnRect = QRect(QPoint(0, ColumnsTop), QPoint(AddressColumnRight - 1, ColumnsBottom - 1));
    m_HexDataColumnRect = QRect(QPoint(AddressColumnRight, ColumnsTop), QPoint(HexDataColumnRight - 1, ColumnsBottom - 1));
    m_AsciiColumnRect = QRect(QPoint(HexDataColumnRight, ColumnsTop), QPoint(AsciiColumnRight - 1, ColumnsBottom - 1));
    m_LayoutChanged = true;
}

void HexEditWidget::ReallocByteBuffers()
{
    HxByteInfo Zero;
    memset(&Zero, 0, sizeof(Zero));
    m_NewBytes.assign(std::max(0, m_NumVisibleBytes), Zero);
    m_OldBytes.assign(std::max(0, m_NumVisibleBytes), Zero);
}

void HexEditWidget::resizeEvent(QResizeEvent * Event)
{
    QWidget::resizeEvent(Event);
    const int OldNumRows = m_NumVisibleRows;
    const int OldNumGroups = m_NumByteGroupsPerRow;
    UpdateLayoutInfo();
    if (OldNumRows != m_NumVisibleRows || OldNumGroups != m_NumByteGroupsPerRow)
    {
        ReallocByteBuffers();
        Draw();
    }
    m_Dirty = true;
    update();
}

void HexEditWidget::SetByteGroupSize(int NumBytes)
{
    m_NumBytesPerGroup = NumBytes;
    if (OnGroupSizeChanged)
    {
        OnGroupSizeChanged();
    }
    UpdateLayoutInfo();
    ReallocByteBuffers();
    Draw();
    m_Dirty = true;
    update();
}

QRect HexEditWidget::HexCellRect(int Index) const
{
    const int Row = Index / m_NumBytesPerRow;
    const int RowOffs = (Index % m_NumBytesPerRow);
    const int Group = RowOffs / m_NumBytesPerGroup;
    const int ByteOffs = RowOffs % m_NumBytesPerGroup;
    const int AddrColumnWidth = (m_CharWidth * 11);
    const int ByteWidth = (m_CharWidth * 2);
    const int HexGroupWidth = (ByteWidth * m_NumBytesPerGroup) + (m_CharWidth * 1);
    const int Left = AddrColumnWidth + (Group * HexGroupWidth) + (ByteOffs * ByteWidth);
    const int Top = m_CharHeight + Row * m_CharHeight;
    return QRect(Left, Top, m_CharWidth * 2, m_CharHeight);
}

QRect HexEditWidget::AsciiCellRect(int Index) const
{
    const int Row = Index / m_NumBytesPerRow;
    const int RowOffs = (Index % m_NumBytesPerRow);
    const int AddrColumnWidth = (m_CharWidth * 11);
    const int ByteWidth = (m_CharWidth * 2);
    const int HexGroupWidth = (ByteWidth * m_NumBytesPerGroup) + (m_CharWidth * 1);
    const int HexColumnWidth = (m_NumByteGroupsPerRow * HexGroupWidth);
    const int AsciiColumnX = AddrColumnWidth + HexColumnWidth;
    return QRect(AsciiColumnX + (RowOffs * m_CharWidth), m_CharHeight + Row * m_CharHeight, m_CharWidth, m_CharHeight);
}

HexEditWidget::HitTestResult HexEditWidget::HitTest(int X, int Y) const
{
    HitTestResult Ht;
    memset(&Ht, 0, sizeof(Ht));
    Ht.column = HX_COL_NONE;
    if (m_NumBytesPerRow == 0)
    {
        return Ht;
    }
    const QPoint Pt(X, Y);
    if (m_AddressColumnRect.contains(Pt))
    {
        Ht.column = HX_COL_ADDRESS;
    }
    const int HeaderHeight = m_CharHeight;
    // Clamp row
    int Row = (Y - HeaderHeight) / m_CharHeight;
    Row = std::max(0, Row);
    Row = std::min(m_NumVisibleRows - 1, Row);
    const uint32_t RowAddress = SatAdd32(m_BaseAddress, Row * m_NumBytesPerRow);
    if (X >= m_HexDataColumnRect.left() && X <= m_HexDataColumnRect.right())
    {
        if (m_HexDataColumnRect.contains(Pt))
        {
            Ht.column = HX_COL_HEXDATA;
        }
        const int GroupWidth = (m_NumBytesPerGroup * m_CharWidth * 2) + (m_CharWidth * 1);
        const int Group = (X - m_HexDataColumnRect.left()) / GroupWidth;
        const int GroupX = m_HexDataColumnRect.left() + Group * GroupWidth;
        const int GroupCharIdx = (X - GroupX) / (m_CharWidth);
        const uint32_t Address = SatAdd32(RowAddress, Group * m_NumBytesPerGroup + GroupCharIdx / 2);
        Ht.hexAddress = Address;
        Ht.hexCellSide = (GroupCharIdx & 1) ? HX_RIGHT : HX_LEFT; // TODO (upstream): Fix for wrap
        Ht.asciiAddress = Address;                                // Approximate
        Ht.asciiCellSide = HX_LEFT;
    }
    else if (X >= m_AsciiColumnRect.left() && X <= m_AsciiColumnRect.right())
    {
        if (m_AsciiColumnRect.contains(Pt))
        {
            Ht.column = HX_COL_ASCII;
        }
        const int AsciiX = X - m_AsciiColumnRect.left();
        const int Idx = (AsciiX / m_CharWidth);
        Ht.asciiAddress = SatAdd32(RowAddress, Idx);
        Ht.asciiCellSide = ((AsciiX % m_CharWidth) > (m_CharWidth / 2)) ? HX_RIGHT : HX_LEFT;
        Ht.hexAddress = SatAdd32(RowAddress, (m_NumBytesPerRow - 1)); // Approximate
        Ht.hexCellSide = HX_RIGHT;
    }
    else if (X < m_HexDataColumnRect.left())
    {
        // Approximate
        Ht.hexAddress = RowAddress;
        Ht.hexCellSide = HX_LEFT;
        Ht.asciiAddress = RowAddress;
        Ht.asciiCellSide = HX_LEFT;
    }
    else if (X > m_AsciiColumnRect.right())
    {
        // Approximate
        Ht.hexAddress = SatAdd32(RowAddress, (m_NumBytesPerRow - 1));
        Ht.hexCellSide = HX_RIGHT;
        Ht.asciiAddress = SatAdd32(RowAddress, (m_NumBytesPerRow - 1));
        Ht.asciiCellSide = HX_RIGHT;
    }
    return Ht;
}

// ---- drawing ----

// Draw(): asks the owner for the bytes, applies the selection/hot colours and repaints
// when something changed
void HexEditWidget::Draw()
{
    if (m_NumVisibleBytes <= 0 || m_NumBytesPerRow == 0)
    {
        return;
    }
    if (OnRedrawStarted)
    {
        OnRedrawStarted();
    }
    int StartCellIndex = 0;
    uint32_t StartAddress = m_BaseAddress;
    int NumBytesToUpdate = m_NumVisibleBytes;
    bool IgnoreDiff = false;
    if (m_BaseAddress != m_DrawnBaseAddress)
    {
        m_ShowHotAddress = false;
        const int64_t AddrDelta = (int64_t)m_BaseAddress - (int64_t)m_DrawnBaseAddress;
        // Scroll optimization: keep the bytes that stay on screen (their old values are the
        // reference for the "changed" colour)
        if ((AddrDelta % m_NumBytesPerRow) == 0 && llabs(AddrDelta) < (m_NumVisibleBytes - m_NumBytesPerRow))
        {
            const int RowDelta = (int)(AddrDelta / m_NumBytesPerRow);
            const int NumBytesScrolled = abs(RowDelta) * m_NumBytesPerRow;
            const int NumBytesToShift = (m_NumVisibleBytes - NumBytesScrolled) - m_NumBytesPerRow;
            int ShiftSrcIndex = 0, ShiftDstIndex = 0;
            NumBytesToUpdate = NumBytesScrolled + m_NumBytesPerRow;
            if (RowDelta > 0)
            {
                StartCellIndex = m_NumVisibleBytes - NumBytesToUpdate;
                ShiftSrcIndex = 0 + NumBytesScrolled;
                ShiftDstIndex = 0;
            }
            else if (RowDelta < 0)
            {
                StartCellIndex = 0;
                ShiftSrcIndex = 0 + m_NumBytesPerRow;
                ShiftDstIndex = ShiftSrcIndex + NumBytesScrolled;
            }
            StartAddress = m_BaseAddress + StartCellIndex;
            memmove(&m_OldBytes[ShiftDstIndex], &m_OldBytes[ShiftSrcIndex], NumBytesToShift * sizeof(HxByteInfo));
            memmove(&m_NewBytes[ShiftDstIndex], &m_NewBytes[ShiftSrcIndex], NumBytesToShift * sizeof(HxByteInfo));
        }
        m_DrawnBaseAddress = m_BaseAddress;
        IgnoreDiff = true;
        m_Dirty = true;
    }
    if (m_LayoutChanged)
    {
        IgnoreDiff = true;
        m_LayoutChanged = false;
        m_Dirty = true;
    }
    int NumOverflowBytes = 0;
    if (StartAddress + NumBytesToUpdate < StartAddress)
    {
        NumOverflowBytes = (StartAddress + NumBytesToUpdate);
    }
    for (int i = 0; i < NumOverflowBytes; i++)
    {
        m_NewBytes[StartCellIndex + NumBytesToUpdate - NumOverflowBytes + i].hidden = true;
    }
    if (OnGetByteInfo)
    {
        OnGetByteInfo(StartAddress, NumBytesToUpdate - NumOverflowBytes, IgnoreDiff, &m_OldBytes[StartCellIndex], &m_NewBytes[StartCellIndex]);
    }
    for (int i = 0; i < NumBytesToUpdate; i++)
    {
        const uint32_t Address = StartAddress + i;
        HxByteInfo * OldByte = &m_OldBytes[StartCellIndex + i];
        HxByteInfo * NewByte = &m_NewBytes[StartCellIndex + i];
        if (IsSelected(Address))
        {
            // Override owner-provided colors if selected
            if (NewByte->bkColor != BKCOLOR_DEFAULT)
            {
                // Blend owner color with selection color if bkcolor isn't default
                NewByte->bkColor = BlendColor(BKCOLOR_SEL_FOCUSED, NewByte->bkColor);
                NewByte->color = COLOR_SEL_FOCUSED;
            }
            else
            {
                NewByte->bkColor = BKCOLOR_SEL_FOCUSED;
                NewByte->color = COLOR_SEL_FOCUSED;
            }
        }
        if (Address == m_HotAddress && m_ShowHotAddress && !m_MouseDragging)
        {
            NewByte->bkColor = BlendColor(BKCOLOR_HOT, NewByte->bkColor);
        }
        // Redraw when the value or the colours have changed
        if (*NewByte != *OldByte)
        {
            m_Dirty = true;
        }
        *OldByte = *NewByte;
    }
    UpdateCaretUI(false);
    if (m_Dirty)
    {
        m_Dirty = false;
        update();
    }
}

void HexEditWidget::DrawText(QPainter & Painter, int X, int Y, const QString & Text, uint32_t Bg, uint32_t Fg) const
{
    const QRect Rect(X, Y, Text.length() * m_CharWidth, m_CharHeight);
    Painter.fillRect(Rect, QColor::fromRgb(Bg));
    Painter.setPen(QColor::fromRgb(Fg));
    Painter.drawText(Rect, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextDontClip, Text);
}

void HexEditWidget::paintEvent(QPaintEvent *)
{
    QPainter Painter(this);
    Painter.setFont(font());
    Painter.fillRect(rect(), QColor::fromRgb(BKCOLOR_DEFAULT));
    if (m_NumBytesPerRow == 0)
    {
        return;
    }
    // Header
    Painter.fillRect(QRect(0, 0, width(), m_CharHeight), QColor::fromRgb(BKCOLOR_ADDR));
    const int GroupWidth = m_NumBytesPerGroup * m_CharWidth * 2 + m_CharWidth;
    for (int Group = 0; Group < m_NumByteGroupsPerRow; Group++)
    {
        const int GroupX = m_HexDataColumnRect.left() + Group * GroupWidth;
        DrawText(Painter, GroupX, 0, QString("%1").arg(Group * m_NumBytesPerGroup, 2, 16, QChar('0')).toUpper(), BKCOLOR_ADDR, COLOR_ADDR);
    }
    // Address column
    Painter.fillRect(QRect(0, m_CharHeight, 11 * m_CharWidth, m_NumVisibleRows * m_CharHeight), QColor::fromRgb(BKCOLOR_ADDR));
    for (int Row = 0; Row < m_NumVisibleRows; Row++)
    {
        const uint32_t RowAddress = m_BaseAddress + (Row * m_NumBytesPerRow);
        const int Y = m_CharHeight + Row * m_CharHeight;
        if (RowAddress >= m_BaseAddress)
        {
            DrawText(Painter, 0, Y, QString(" %1 ").arg(RowAddress, 8, 16, QChar('0')).toUpper(), BKCOLOR_ADDR, COLOR_ADDR);
        }
    }
    // Bytes
    for (int i = 0; i < m_NumVisibleBytes && i < (int)m_NewBytes.size(); i++)
    {
        const uint32_t Address = m_BaseAddress + i;
        const HxByteInfo & Byte = m_NewBytes[i];
        const QRect Hex = HexCellRect(i), Ascii = AsciiCellRect(i);
        if (Byte.hidden)
        {
            Painter.fillRect(Hex, QColor::fromRgb(BKCOLOR_DEFAULT));
            Painter.fillRect(Ascii, QColor::fromRgb(BKCOLOR_DEFAULT));
            continue;
        }
        uint32_t HexBk = Byte.bkColor, HexFg = Byte.color, AsciiBk = Byte.bkColor, AsciiFg = Byte.color;
        if (IsSelected(Address))
        {
            if (m_FocusedColumn == HX_COL_ASCII)
            {
                HexBk = BKCOLOR_SEL_UNFOCUSED;
                HexFg = COLOR_SEL_UNFOCUSED;
            }
            else
            {
                AsciiBk = BKCOLOR_SEL_UNFOCUSED;
                AsciiFg = COLOR_SEL_UNFOCUSED;
            }
        }
        if (Byte.valid)
        {
            DrawText(Painter, Hex.left(), Hex.top(), QString("%1").arg(Byte.value, 2, 16, QChar('0')).toUpper(), HexBk, HexFg);
            DrawText(Painter, Ascii.left(), Ascii.top(), QString(QChar::fromLatin1(ByteAscii(Byte.value))), AsciiBk, AsciiFg);
        }
        else
        {
            DrawText(Painter, Hex.left(), Hex.top(), "**", HexBk, HexFg);
            DrawText(Painter, Ascii.left(), Ascii.top(), ".", AsciiBk, AsciiFg);
        }
    }
    if (m_HaveCaret && m_CaretBlinkOn && IsCaretAddressVisible())
    {
        Painter.fillRect(CaretRect(), Qt::black);
    }
}

QRect HexEditWidget::CaretRect() const
{
    if (m_NumBytesPerRow == 0 || !IsCaretAddressVisible())
    {
        return QRect();
    }
    int Index = m_CaretAddress - m_BaseAddress;
    int XOffs = 0;
    QRect Cell;
    if (m_FocusedColumn == HX_COL_ASCII)
    {
        if ((int)((m_RealSelEndAddress - m_BaseAddress) % m_NumBytesPerRow) == m_NumBytesPerRow - 1)
        {
            // Left-to-right selection ends on the end of a row
            Index--;
            XOffs = m_CharWidth;
        }
        Cell = AsciiCellRect(std::max(0, Index));
        return QRect(Cell.left() + XOffs, Cell.top(), 2, m_CharHeight);
    }
    if (GetSelDirection() > 0)
    {
        if ((int)((m_RealSelEndAddress - m_BaseAddress) % m_NumBytesPerRow) == m_NumBytesPerRow - 1)
        {
            // Left-to-right selection ends on the end of a row
            Index--;
            XOffs = m_CharWidth * 2;
        }
        else if ((int)((m_RealSelEndAddress - m_BaseAddress) % m_NumBytesPerGroup) == m_NumBytesPerGroup - 1)
        {
            // Left-to-right selection ends on the end of a group
            XOffs = -m_CharWidth;
        }
    }
    Cell = HexCellRect(std::max(0, Index));
    return QRect(Cell.left() + (m_CaretLoNibble ? m_CharWidth : 0) + XOffs, Cell.top(), 2, m_CharHeight);
}

// UpdateCaretUI: keeps the caret on screen (the caret itself is painted in paintEvent)
bool HexEditWidget::UpdateCaretUI(bool EnsureVisible, bool Top)
{
    if (EnsureVisible)
    {
        EnsureCaretAddressVisible(Top);
    }
    m_Dirty = true;
    m_CaretBlinkOn = true;
    return m_HaveCaret && IsCaretAddressVisible();
}

// ---- selection ----

int HexEditWidget::GetSelDirection() const
{
    if (m_SelStartAddress < m_SelEndAddress) return 1;                            // Right
    if (m_SelStartAddress > m_SelEndAddress) return -1;                           // Left
    if (m_SelStartCellSide == m_SelEndCellSide) return 0;                         // No selection
    if (m_SelStartCellSide == HX_LEFT && m_SelEndCellSide == HX_RIGHT) return 1;  // Right (single byte)
    if (m_SelStartCellSide == HX_RIGHT && m_SelEndCellSide == HX_LEFT) return -1; // Left (single byte)
    return 0;
}

void HexEditWidget::UpdateRealSelection()
{
    uint32_t Start = m_SelStartAddress;
    uint32_t End = m_SelEndAddress;
    bool HaveSel = true;
    if (Start < End)
    {
        if (m_SelEndCellSide == HX_LEFT) End--;
        if (m_SelStartCellSide == HX_RIGHT) Start++;
    }
    else if (End < Start)
    {
        if (Start - End == 1)
        {
            if (m_SelStartCellSide == HX_LEFT && m_SelEndCellSide == HX_RIGHT)
            {
                HaveSel = false;
            }
        }
        if (m_SelStartCellSide == HX_LEFT) Start--;
        if (m_SelEndCellSide == HX_RIGHT) End++;
        std::swap(Start, End);
    }
    else if (Start == End)
    {
        if (m_SelStartCellSide == m_SelEndCellSide)
        {
            HaveSel = false;
        }
    }
    if (m_RealSelStartAddress != Start || m_RealSelEndAddress != End || m_HaveRealSel != HaveSel)
    {
        m_HaveRealSel = HaveSel;
        m_RealSelStartAddress = Start;
        m_RealSelEndAddress = End;
        if (OnSelectionChanged)
        {
            OnSelectionChanged();
        }
    }
}

bool HexEditWidget::IsSelected(uint32_t Address) const
{
    return m_HaveRealSel && (Address >= m_RealSelStartAddress && Address <= m_RealSelEndAddress);
}

void HexEditWidget::CancelSelection()
{
    m_SelStartAddress = m_CaretAddress;
    m_SelEndAddress = m_CaretAddress;
    m_SelStartCellSide = m_CaretLoNibble ? HX_RIGHT : HX_LEFT;
    m_SelEndCellSide = m_CaretLoNibble ? HX_RIGHT : HX_LEFT;
    UpdateRealSelection();
}

void HexEditWidget::SelectAllVisible()
{
    const uint32_t LastVisibleByteAddress = (m_BaseAddress + m_NumVisibleBytes) - 1;
    m_SelStartAddress = m_BaseAddress;
    m_SelStartCellSide = HX_LEFT;
    m_SelEndAddress = LastVisibleByteAddress;
    m_SelEndCellSide = HX_RIGHT;
    m_CaretAddress = LastVisibleByteAddress + 1;
    m_CaretLoNibble = false;
    UpdateRealSelection();
}

bool HexEditWidget::GetSelectionRange(uint32_t * StartAddress, uint32_t * EndAddress) const
{
    *StartAddress = m_RealSelStartAddress;
    *EndAddress = m_RealSelEndAddress;
    return m_HaveRealSel;
}

void HexEditWidget::SelectRange(uint32_t Start, uint32_t End, HxColumn Column)
{
    m_FocusedColumn = Column;
    m_SelStartAddress = Start;
    m_SelStartCellSide = HX_LEFT;
    m_SelEndAddress = End;
    m_SelEndCellSide = HX_RIGHT;
    m_CaretAddress = End + 1;
    m_CaretLoNibble = false;
    UpdateRealSelection();
    Draw();
}

void HexEditWidget::SetCaret(uint32_t Address, bool LoNibble, HxColumn Column)
{
    m_FocusedColumn = Column;
    m_CaretAddress = Address;
    m_CaretLoNibble = LoNibble;
    CancelSelection();
    UpdateCaretUI(true);
}

uint32_t HexEditWidget::LineAddress(uint32_t Address) const
{
    return Address - ((Address - (m_BaseAddress % m_NumBytesPerRow)) % m_NumBytesPerRow);
}

bool HexEditWidget::IsCaretAddressVisible() const
{
    return m_CaretAddress >= m_BaseAddress && m_CaretAddress <= SatAdd32(m_BaseAddress, m_NumVisibleBytes);
}

void HexEditWidget::EnsureCaretAddressVisible(bool Top)
{
    if (m_NumBytesPerRow == 0)
    {
        return;
    }
    const uint32_t OldBaseAddress = m_BaseAddress;
    const uint32_t CaretLineAddress = LineAddress(m_CaretAddress);
    const uint32_t LastVisibleLineAddress = m_BaseAddress + (m_NumVisibleBytes - m_NumBytesPerRow);
    if (Top || CaretLineAddress < m_BaseAddress)
    {
        m_BaseAddress = CaretLineAddress;
    }
    else if (CaretLineAddress >= LastVisibleLineAddress + m_NumBytesPerRow)
    {
        m_BaseAddress = SatAdd32(m_BaseAddress, CaretLineAddress - LastVisibleLineAddress);
    }
    if (OldBaseAddress != m_BaseAddress && OnBaseAddrChanged)
    {
        OnBaseAddrChanged();
    }
}

void HexEditWidget::CaretIncrementNibble()
{
    if (!m_CaretLoNibble)
    {
        m_CaretLoNibble = true;
    }
    else
    {
        m_CaretLoNibble = false;
        m_CaretAddress++;
    }
}

void HexEditWidget::CaretDecrementNibble()
{
    if (m_CaretLoNibble)
    {
        m_CaretLoNibble = false;
    }
    else
    {
        m_CaretLoNibble = true;
        m_CaretAddress--;
    }
}

// ---- mouse ----

void HexEditWidget::mousePressEvent(QMouseEvent * Event)
{
    setFocus();
    const QPoint Point = Event->position().toPoint();
    const HitTestResult Ht = HitTest(Point.x(), Point.y());
    if (Event->button() == Qt::RightButton)
    {
        // OnRButtonDown
        if (Ht.column == HX_COL_HEXDATA)
        {
            if (!IsSelected(Ht.hexAddress))
            {
                m_CaretAddress = Ht.hexAddress;
                m_CaretLoNibble = false;
                CancelSelection();
            }
        }
        else if (Ht.column == HX_COL_ASCII)
        {
            if (!IsSelected(Ht.asciiAddress))
            {
                m_CaretAddress = Ht.asciiAddress;
                CancelSelection();
            }
        }
        return;
    }
    if (Event->button() != Qt::LeftButton)
    {
        return;
    }
    m_LButtonDown = true;
    m_FocusedColumn = Ht.column;
    const bool Shift = (Event->modifiers() & Qt::ShiftModifier) != 0;
    if (m_FocusedColumn == HX_COL_HEXDATA)
    {
        m_CaretAddress = Ht.hexAddress;
        m_CaretLoNibble = (Ht.hexCellSide == HX_RIGHT);
        if (Shift)
        {
            m_SelEndAddress = Ht.hexAddress;
            m_SelEndCellSide = Ht.hexCellSide;
            UpdateRealSelection();
            if (GetSelDirection() > 0)
            {
                m_CaretAddress = m_RealSelEndAddress + 1;
            }
            else
            {
                m_CaretAddress = m_RealSelStartAddress;
            }
            m_CaretLoNibble = false;
        }
        else
        {
            m_SelStartAddress = Ht.hexAddress;
            m_SelEndAddress = Ht.hexAddress;
            m_SelStartCellSide = Ht.hexCellSide;
            m_SelEndCellSide = Ht.hexCellSide;
            UpdateRealSelection();
        }
    }
    else if (m_FocusedColumn == HX_COL_ASCII)
    {
        m_CaretAddress = Ht.asciiAddress;
        if (Shift)
        {
            m_SelEndAddress = Ht.asciiAddress;
        }
        else
        {
            m_CaretAddress = Ht.asciiAddress;
            m_SelStartCellSide = Ht.asciiCellSide;
            m_SelEndCellSide = Ht.asciiCellSide;
            m_SelStartAddress = Ht.asciiAddress;
            m_SelEndAddress = Ht.asciiAddress;
            if (Ht.asciiCellSide)
            {
                m_CaretAddress++;
            }
        }
        UpdateRealSelection();
    }
    UpdateCaretUI(false);
    Draw();
    m_Dirty = true;
    update();
}

void HexEditWidget::mouseDoubleClickEvent(QMouseEvent * Event)
{
    if (Event->button() != Qt::LeftButton)
    {
        return;
    }
    m_DblClicked = true;
    const QPoint Point = Event->position().toPoint();
    const HitTestResult Ht = HitTest(Point.x(), Point.y());
    if (m_FocusedColumn == HX_COL_HEXDATA)
    {
        // Select word
        const uint32_t Offset = (Ht.hexAddress - m_BaseAddress);
        const uint32_t WordOffset = Offset - (Offset % m_NumBytesPerGroup);
        const uint32_t WordAddress = m_BaseAddress + WordOffset;
        m_SelStartAddress = WordAddress;
        m_SelEndAddress = WordAddress + (m_NumBytesPerGroup - 1);
        m_SelStartCellSide = HX_LEFT;
        m_SelEndCellSide = HX_RIGHT;
        m_CaretAddress = m_SelEndAddress + 1;
        m_CaretLoNibble = false;
        UpdateRealSelection();
        UpdateCaretUI(false);
    }
    if (m_FocusedColumn == HX_COL_ASCII)
    {
        // Select row
        const uint32_t Offset = (Ht.asciiAddress - m_BaseAddress);
        const uint32_t RowOffset = (Ht.asciiAddress - m_BaseAddress) - (Offset % m_NumBytesPerRow);
        const uint32_t RowAddress = m_BaseAddress + RowOffset;
        m_SelStartAddress = RowAddress;
        m_SelEndAddress = RowAddress + (m_NumBytesPerRow - 1);
        m_SelStartCellSide = HX_LEFT;
        m_SelEndCellSide = HX_RIGHT;
        m_CaretAddress = m_SelEndAddress + 1;
        UpdateRealSelection();
        UpdateCaretUI(false);
    }
}

void HexEditWidget::mouseReleaseEvent(QMouseEvent * Event)
{
    const QPoint Point = Event->position().toPoint();
    if (Event->button() == Qt::RightButton)
    {
        // OnRButtonUp
        const HitTestResult Ht = HitTest(Point.x(), Point.y());
        if (Ht.column == HX_COL_HEXDATA && OnRightClick)
        {
            OnRightClick(Ht.hexAddress);
        }
        else if (Ht.column == HX_COL_ASCII && OnRightClick)
        {
            OnRightClick(Ht.asciiAddress);
        }
        return;
    }
    if (Event->button() != Qt::LeftButton)
    {
        return;
    }
    m_LButtonDown = false;
    m_MouseDragging = false;
    if (m_DragScrollDelta != 0)
    {
        m_DblClicked = false;
        m_DragScrollDelta = 0;
        return;
    }
    m_DblClicked = false;
}

void HexEditWidget::mouseMoveEvent(QMouseEvent * Event)
{
    const QPoint Point = Event->position().toPoint();
    if (m_LButtonDown)
    {
        m_MouseDragging = true;
    }
    const HitTestResult Ht = HitTest(Point.x(), Point.y());
    if (Ht.column == HX_COL_NONE || Ht.column == HX_COL_ADDRESS)
    {
        m_ShowHotAddress = false;
    }
    else
    {
        m_ShowHotAddress = true;
        if (Ht.column == HX_COL_HEXDATA)
        {
            if (m_HotAddress != Ht.hexAddress)
            {
                m_HotAddress = Ht.hexAddress;
                if (OnHotAddrChanged)
                {
                    OnHotAddrChanged();
                }
            }
        }
        else if (Ht.column == HX_COL_ASCII)
        {
            if (m_HotAddress != Ht.asciiAddress)
            {
                m_HotAddress = Ht.asciiAddress;
                if (OnHotAddrChanged)
                {
                    OnHotAddrChanged();
                }
            }
        }
    }
    setCursor((Ht.column == HX_COL_HEXDATA || Ht.column == HX_COL_ASCII) ? Qt::IBeamCursor : Qt::ArrowCursor);
    if (!m_LButtonDown)
    {
        return;
    }
    m_DragScrollDelta = 0;
    if (Point.y() > m_HexDataColumnRect.bottom())
    {
        m_DragScrollDelta = 1 + (Point.y() - m_HexDataColumnRect.bottom()) / m_CharHeight;
    }
    else if (Point.y() < m_HexDataColumnRect.top())
    {
        m_DragScrollDelta = -1 + (Point.y() - m_HexDataColumnRect.top()) / m_CharHeight;
    }
    if (m_FocusedColumn == HX_COL_HEXDATA)
    {
        m_CaretAddress = Ht.hexAddress;
        m_SelEndAddress = Ht.hexAddress;
        m_SelEndCellSide = Ht.hexCellSide;
        m_CaretLoNibble = (Ht.hexCellSide == HX_RIGHT);
        if (m_SelEndAddress - m_SelStartAddress == 1 && m_SelStartCellSide == HX_RIGHT && m_SelEndCellSide == HX_LEFT)
        {
            m_SelStartCellSide = HX_LEFT;
        }
        if (GetSelDirection() != 0 && m_SelEndCellSide == HX_RIGHT)
        {
            m_CaretLoNibble = false;
            m_CaretAddress++;
        }
    }
    else if (m_FocusedColumn == HX_COL_ASCII)
    {
        m_CaretAddress = Ht.asciiAddress;
        m_SelEndAddress = Ht.asciiAddress;
        m_SelEndCellSide = Ht.asciiCellSide;
        if (GetSelDirection() != 0 && m_SelEndCellSide == HX_RIGHT)
        {
            m_CaretAddress++;
        }
    }
    UpdateRealSelection();
    m_Dirty = true;
}

// TIMER_ID_DRAG_SCROLL
void HexEditWidget::DragScrollTick()
{
    if (m_DragScrollDelta == 0)
    {
        return;
    }
    const int NumBytesToScroll = m_DragScrollDelta * m_NumBytesPerRow;
    const int64_t NewCaretAddress = (int64_t)m_CaretAddress + NumBytesToScroll;
    if (NewCaretAddress < 0 && m_BaseAddress == 0)
    {
        return;
    }
    else if (NewCaretAddress > UINT32_MAX)
    {
        return;
    }
    m_CaretAddress = SatAdd32(m_CaretAddress, NumBytesToScroll);
    m_SelEndAddress = SatAdd32(m_SelEndAddress, NumBytesToScroll);
    UpdateRealSelection();
    UpdateCaretUI(true);
}

void HexEditWidget::wheelEvent(QWheelEvent * Event)
{
    const int Delta = Event->angleDelta().y();
    m_BaseAddress = SatAdd32(m_BaseAddress, -(Delta / 120) * m_NumBytesPerRow);
    if (OnBaseAddrChanged)
    {
        OnBaseAddrChanged();
    }
    Event->accept();
}

// ---- keyboard ----

// DLGC_WANTALLKEYS: Tab and the arrow keys are ours
bool HexEditWidget::event(QEvent * Event)
{
    if (Event->type() == QEvent::KeyPress)
    {
        QKeyEvent * Key = static_cast<QKeyEvent *>(Event);
        if (Key->key() == Qt::Key_Tab || Key->key() == Qt::Key_Backtab)
        {
            keyPressEvent(Key);
            return true;
        }
    }
    return QWidget::event(Event);
}

void HexEditWidget::focusInEvent(QFocusEvent * Event)
{
    QWidget::focusInEvent(Event);
    m_HaveCaret = true;
    UpdateCaretUI(false);
}

void HexEditWidget::focusOutEvent(QFocusEvent * Event)
{
    QWidget::focusOutEvent(Event);
    m_HaveCaret = false;
    m_Dirty = true;
}

void HexEditWidget::keyPressEvent(QKeyEvent * Event)
{
    const int Key = Event->key();
    if (Key == Qt::Key_Control || Key == Qt::Key_Shift || Key == Qt::Key_Alt || Key == Qt::Key_Meta)
    {
        return;
    }
    KeyDown(Key, Event->modifiers());
    // OnChar: printable characters without Ctrl (Alt Gr may report Control+Alt)
    if (!(Event->modifiers() & Qt::ControlModifier) || (Event->modifiers() & Qt::AltModifier))
    {
        const QString Text = Event->text();
        if (!Text.isEmpty() && Key != Qt::Key_Backspace && Key != Qt::Key_Tab && Key != Qt::Key_Return && Key != Qt::Key_Enter && Key != Qt::Key_Escape && Key != Qt::Key_Delete)
        {
            CharTyped(Text);
        }
    }
}

void HexEditWidget::CharTyped(const QString & Text)
{
    for (const QChar & Ch : Text)
    {
        const uint16_t NChar = Ch.unicode();
        if (NChar < 0x20)
        {
            continue;
        }
        if (m_FocusedColumn == HX_COL_HEXDATA)
        {
            if (NChar < 0x80 && isxdigit(NChar))
            {
                const int SelDirection = GetSelDirection();
                if (SelDirection < 0)
                {
                    m_CaretAddress = m_SelEndAddress;
                    m_CaretLoNibble = false;
                }
                else if (SelDirection > 0)
                {
                    m_CaretAddress = m_SelStartAddress;
                    m_CaretLoNibble = false;
                }
                if (OnSetNibble)
                {
                    OnSetNibble(m_InsertMode, m_CaretAddress, m_CaretLoNibble, HexCharValue((char)NChar));
                }
                CancelSelection();
                CaretIncrementNibble();
                UpdateCaretUI(true);
            }
        }
        else if (m_FocusedColumn == HX_COL_ASCII)
        {
            const int SelDirection = GetSelDirection();
            if (SelDirection < 0)
            {
                m_CaretAddress = m_SelEndAddress;
            }
            else if (SelDirection > 0)
            {
                m_CaretAddress = m_SelStartAddress;
            }
            if (OnSetByte)
            {
                OnSetByte(m_InsertMode, m_CaretAddress, (uint8_t)NChar);
            }
            CancelSelection();
            m_CaretAddress++;
            UpdateCaretUI(true);
        }
    }
}

void HexEditWidget::Paste(bool AdvanceCaret)
{
    const uint32_t TargetAddress = m_HaveRealSel ? m_RealSelStartAddress : m_CaretAddress;
    const int RetLength = OnPaste ? OnPaste(TargetAddress, m_FocusedColumn) : 0;
    if (RetLength != 0)
    {
        if (AdvanceCaret)
        {
            m_CaretAddress = TargetAddress + RetLength;
            UpdateCaretUI(true);
        }
        CancelSelection();
    }
}

void HexEditWidget::Copy()
{
    if (m_HaveRealSel && OnCopy)
    {
        OnCopy();
    }
}

void HexEditWidget::SetBaseAddress(uint32_t Address)
{
    if (m_BaseAddress != Address)
    {
        m_BaseAddress = Address;
        Draw();
    }
}

// OnKeyDown
void HexEditWidget::KeyDown(int Key, Qt::KeyboardModifiers Modifiers)
{
    const bool Ctrl = (Modifiers & Qt::ControlModifier) != 0;
    const bool Shift = (Modifiers & Qt::ShiftModifier) != 0;
    if (Ctrl)
    {
        if (OnCtrlKeyPressed)
        {
            OnCtrlKeyPressed(Key);
        }
        if (Key == Qt::Key_V)
        {
            Paste();
        }
        else if (Key == Qt::Key_B)
        {
            Paste(false);
        }
        else if (Key == Qt::Key_C)
        {
            Copy();
        }
        else if (Key == Qt::Key_X)
        {
            Copy();
            if (m_HaveRealSel)
            {
                if (OnFillRange)
                {
                    OnFillRange(m_InsertMode, m_RealSelStartAddress, m_RealSelEndAddress, 0);
                }
                m_CaretAddress = m_RealSelStartAddress;
                m_CaretLoNibble = false;
                CancelSelection();
            }
        }
        else if (Key == Qt::Key_A)
        {
            SelectAllVisible();
        }
    }
    if (m_NumBytesPerRow == 0)
    {
        return;
    }
    if (Key == Qt::Key_Down)
    {
        m_CaretAddress = SatAdd32(m_CaretAddress, m_NumBytesPerRow);
        if (Shift)
        {
            m_SelEndAddress += m_NumBytesPerRow;
            if (m_CaretLoNibble)
            {
                m_SelStartCellSide = HX_LEFT;
                m_SelEndCellSide = HX_LEFT;
                m_CaretLoNibble = false;
            }
            UpdateRealSelection();
        }
        else
        {
            CancelSelection();
        }
        UpdateCaretUI(true);
    }
    else if (Key == Qt::Key_Up)
    {
        m_CaretAddress -= m_NumBytesPerRow;
        if (Shift)
        {
            m_SelEndAddress -= m_NumBytesPerRow;
            if (m_CaretLoNibble)
            {
                m_SelStartCellSide = HX_LEFT;
                m_SelEndCellSide = HX_LEFT;
                m_CaretLoNibble = false;
            }
            UpdateRealSelection();
        }
        else
        {
            CancelSelection();
        }
        UpdateCaretUI(true);
    }
    else if (Key == Qt::Key_Right)
    {
        if (m_FocusedColumn == HX_COL_HEXDATA)
        {
            if (Shift)
            {
                if (m_SelStartAddress == m_SelEndAddress && m_SelStartCellSide == HX_RIGHT && m_SelEndCellSide == HX_RIGHT)
                {
                    m_SelStartCellSide = HX_LEFT;
                    m_SelEndCellSide = HX_RIGHT;
                }
                else
                {
                    m_SelEndAddress++;
                }
                m_CaretLoNibble = false;
                m_CaretAddress++;
                UpdateRealSelection();
            }
            else if (Ctrl)
            {
                CaretIncrementNibble();
                CancelSelection();
            }
            else
            {
                m_CaretLoNibble = false;
                m_CaretAddress++;
                m_SelStartAddress = m_CaretAddress;
                m_SelEndAddress = m_CaretAddress;
                m_SelStartCellSide = HX_LEFT;
                m_SelEndCellSide = HX_LEFT;
                CancelSelection();
            }
        }
        else if (m_FocusedColumn == HX_COL_ASCII)
        {
            m_CaretAddress++;
            if (Shift)
            {
                m_SelEndCellSide = HX_LEFT;
                m_SelEndAddress = m_CaretAddress;
                UpdateRealSelection();
            }
            else
            {
                CancelSelection();
            }
        }
        UpdateCaretUI(true);
    }
    else if (Key == Qt::Key_Left)
    {
        if (m_FocusedColumn == HX_COL_HEXDATA)
        {
            if (Shift)
            {
                m_SelEndCellSide = HX_LEFT;
                m_SelEndAddress--;
                if (m_CaretLoNibble)
                {
                    m_SelStartCellSide = HX_LEFT;
                }
                m_CaretAddress--;
                m_CaretLoNibble = false;
                UpdateRealSelection();
            }
            else if (Ctrl)
            {
                CaretDecrementNibble();
                CancelSelection();
            }
            else
            {
                if (m_CaretLoNibble)
                {
                    CaretDecrementNibble();
                }
                else
                {
                    m_CaretAddress--;
                }
                CancelSelection();
            }
        }
        else if (m_FocusedColumn == HX_COL_ASCII)
        {
            m_CaretAddress--;
            if (Shift)
            {
                m_SelEndCellSide = HX_LEFT;
                m_SelEndAddress = m_CaretAddress;
                UpdateRealSelection();
            }
            else
            {
                CancelSelection();
            }
        }
        UpdateCaretUI(true);
    }
    else if (Key == Qt::Key_PageDown || Key == Qt::Key_PageUp)
    {
        const int Delta = (Key == Qt::Key_PageDown) ? m_NumVisibleBytes : -m_NumVisibleBytes;
        if (IsCaretAddressVisible())
        {
            m_BaseAddress += Delta;
            m_CaretAddress += Delta;
            if (OnBaseAddrChanged)
            {
                OnBaseAddrChanged();
            }
        }
        else
        {
            m_CaretAddress += Delta;
            UpdateCaretUI(true, true);
        }
        CancelSelection();
    }
    else if (Key == Qt::Key_Insert)
    {
        m_InsertMode = !m_InsertMode;
        if (OnInsertModeChanged)
        {
            OnInsertModeChanged();
        }
    }
    else if (Key == Qt::Key_Return || Key == Qt::Key_Enter)
    {
        if (OnEnterPressed)
        {
            OnEnterPressed();
        }
    }
    else if (Key == Qt::Key_Home)
    {
        UpdateCaretUI(true);
    }
    else if (Key == Qt::Key_Backspace)
    {
        if (m_HaveRealSel)
        {
            if (OnFillRange)
            {
                OnFillRange(m_InsertMode, m_RealSelStartAddress, m_RealSelEndAddress, 0);
            }
            CancelSelection();
            m_CaretAddress = m_RealSelStartAddress;
            UpdateCaretUI(true);
        }
        else
        {
            if (m_FocusedColumn == HX_COL_HEXDATA)
            {
                const uint32_t Address = m_CaretAddress + (m_CaretLoNibble ? 0 : -1);
                if (OnSetNibble)
                {
                    OnSetNibble(m_InsertMode, Address, !m_CaretLoNibble, 0);
                }
                CaretDecrementNibble();
                UpdateCaretUI(true);
            }
            else if (m_FocusedColumn == HX_COL_ASCII)
            {
                if (OnSetByte)
                {
                    OnSetByte(m_InsertMode, m_CaretAddress - 1, 0);
                }
                m_CaretAddress--;
                UpdateCaretUI(true);
            }
        }
    }
    else if (Key == Qt::Key_Delete)
    {
        if (m_FocusedColumn == HX_COL_HEXDATA || m_FocusedColumn == HX_COL_ASCII)
        {
            if (!m_HaveRealSel)
            {
                if (OnSetByte)
                {
                    OnSetByte(m_InsertMode, m_CaretAddress, 0);
                }
            }
            else
            {
                if (OnFillRange)
                {
                    OnFillRange(m_InsertMode, m_RealSelStartAddress, m_RealSelEndAddress, 0);
                }
            }
        }
    }
    m_Dirty = true;
}
