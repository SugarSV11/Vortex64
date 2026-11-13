#include "stdafx.h"
#include "../Vortex/Strings.h"

#include "MemoryView.h"
#include "DebugCommon.h"
#include "DebugDialogs.h"
#include "DebuggerUI.h"

#include <Vortex64-core/N64System/N64Rom.h>
#include <Vortex64-ui/UserInterface/Debugger/MemoryScanner.h>
#include <Vortex64-ui/UserInterface/Debugger/OpInfo.h>

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QGridLayout>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollBar>
#include <QStatusBar>
#include <QTabBar>
#include <memory>

using Vortex::Tr;

const MemoryView::JumpItem MemoryView::JumpItems[] = {
    {0x80000000, 0x00000000, 0x0800000, "RDRAM"},
    {0xA3F00000, 0x03F00000, 0x0000028, "RDRAM registers"},
    {0xA4000000, 0x04000000, 0x0001000, "SP DMEM"},
    {0xA4001000, 0x04001000, 0x0001000, "SP IMEM"},
    {0xA4040000, 0x04040000, 0x0000020, "SP registers"},
    {0xA4080000, 0x04080000, 0x0000004, "SP PC register"},
    {0xA4100000, 0x04100000, 0x0000020, "DP control registers"},
    {0xA4300000, 0x04300000, 0x0000010, "MI registers"},
    {0xA4400000, 0x04400000, 0x0000038, "VI registers"},
    {0xA4500000, 0x04500000, 0x0000018, "AI registers"},
    {0xA4600000, 0x04600000, 0x0000034, "PI registers"},
    {0xA4700000, 0x04700000, 0x0000020, "RI registers"},
    {0xA4800000, 0x04800000, 0x0000010, "SI registers"},
    {0xA5000500, 0x05000500, 0x000004C, "DD registers"},
    {0xA8000000, 0x08000000, 0x1000000, "Cartridge save data"},
    {0xB0000000, 0x10000000, 0xFC00000, "Cartridge ROM"},
    {0xBFC00000, 0x1FC00000, 0x00007C0, "PIF ROM"},
    {0xBFC007C0, 0x1FC007C0, 0x0000040, "PIF RAM"},
    {0, 0, 0, nullptr},
};

namespace
{
enum : uint32_t
{
    COLOR_DEFAULT = 0x000000,
    COLOR_READONLY = 0x006400,
    BKCOLOR_DEFAULT = 0xFFFFFF,
    COLOR_CHANGED = 0xFF0000,
    BKCOLOR_LOCKED = 0x646400,
    COLOR_BP = 0xFFFFFF,
    BKCOLOR_RWBP = 0x640064,
    BKCOLOR_RBP = 0x000064,
    BKCOLOR_WBP = 0x640000,
    BKCOLOR_CPUREAD = 0xC8C8FF,
    BKCOLOR_CPUWRITE = 0xFFC8C8,
    BKCOLOR_SYMBOL0 = 0xD0F0D0,
    BKCOLOR_SYMBOL1 = 0xB0D0B0,
};

// A label that reports clicks (the status bar panes)
class ClickLabel : public QLabel
{
public:
    std::function<void()> Clicked;
    using QLabel::QLabel;

protected:
    void mouseReleaseEvent(QMouseEvent *) override
    {
        if (Clicked)
        {
            Clicked();
        }
    }
};

// The tab bar reports the right click on release over the same tab (CMemTabCtrl)
class MemTabBar : public QTabBar
{
public:
    std::function<void(int)> RightClicked;
    std::function<void()> DoubleClicked;

protected:
    void mousePressEvent(QMouseEvent * Event) override
    {
        if (Event->button() == Qt::RightButton)
        {
            m_Item = tabAt(Event->position().toPoint());
            return;
        }
        QTabBar::mousePressEvent(Event);
    }
    void mouseReleaseEvent(QMouseEvent * Event) override
    {
        if (Event->button() == Qt::RightButton)
        {
            const int Item = tabAt(Event->position().toPoint());
            if (Item != -1 && Item == m_Item && RightClicked)
            {
                RightClicked(Item);
            }
            m_Item = -1;
            return;
        }
        QTabBar::mouseReleaseEvent(Event);
    }
    void mouseDoubleClickEvent(QMouseEvent * Event) override
    {
        if (Event->button() == Qt::LeftButton && DoubleClicked)
        {
            DoubleClicked();
        }
    }

private:
    int m_Item = -1;
};
} // namespace

MemoryView::MemoryView(CDebuggerUI * Debugger) :
    DebugWindow(Debugger, DebuggerUI_MemoryPos, true),
    m_Breakpoints(Debugger->Breakpoints())
{
    setWindowTitle(Tr("Memory"));
    resize(321 * 2, 199 * 2);
    QGridLayout * Layout = new QGridLayout(this);
    Layout->setContentsMargins(0, 3, 0, 0);
    Layout->setSpacing(2);

    MemTabBar * Tabs = new MemTabBar;
    m_Tabs = Tabs;
    m_Tabs->setExpanding(false);
    m_Tabs->setFocusPolicy(Qt::NoFocus);
    Layout->addWidget(m_Tabs, 0, 0, 1, 2);

    QHBoxLayout * Top = new QHBoxLayout;
    Top->setContentsMargins(5, 0, 5, 0);
    m_MemAddr = new NumberEdit(this, NumberEdit::DisplayHex);
    m_MemAddr->setFixedWidth(90);
    QPushButton * SymbolsButton = new QPushButton("...", this);
    SymbolsButton->setFixedWidth(30);
    SymbolsButton->setToolTip(Tr("Symbols..."));
    m_Virtual = new QCheckBox(Tr("Virtual"), this);
    m_Virtual->setToolTip(Tr("Checked = Use virtual address space (CPU), Unchecked = Use physical address space (RCP)"));
    m_Virtual->setChecked(true);
    m_CmbJump = new QComboBox(this);
    m_CmbJump->setMinimumWidth(200);
    Top->addWidget(m_MemAddr);
    Top->addWidget(SymbolsButton);
    Top->addWidget(m_Virtual);
    Top->addStretch();
    Top->addWidget(m_CmbJump);
    Layout->addLayout(Top, 1, 0, 1, 2);

    m_Hex = new HexEditWidget(this);
    Layout->addWidget(m_Hex, 2, 0);
    m_Scrollbar = new QScrollBar(Qt::Vertical, this);
    m_Scrollbar->setRange(0, 0xFFFF);
    m_Scrollbar->setValue(0x8000);
    m_Scrollbar->setPageStep(100);
    Layout->addWidget(m_Scrollbar, 2, 1);
    Layout->setRowStretch(2, 1);
    Layout->setColumnStretch(0, 1);

    QStatusBar * Status = new QStatusBar(this);
    Status->setSizeGripEnabled(false);
    static const int Widths[MEMSB_NUM_PANES] = {160, 120, 60, 60, 0};
    for (int i = 0; i < MEMSB_NUM_PANES; i++)
    {
        ClickLabel * Label = new ClickLabel(this);
        Label->setFrameStyle(QFrame::Panel | QFrame::Sunken);
        if (Widths[i] != 0)
        {
            Label->setMinimumWidth(Widths[i]);
        }
        const int Pane = i;
        Label->Clicked = [this, Pane]() { StatusClicked(Pane); };
        m_Status[i] = Label;
        Status->addWidget(Label, Widths[i] == 0 ? 1 : 0);
    }
    Layout->addWidget(Status, 3, 0, 1, 2);

    SetMemAddr(0x80000000);
    SetupJumpMenu(true);
    m_CmbJump->setCurrentIndex(0);
    AddTab(0x80000000, true, 4);
    SetupHex();
    m_Hex->Draw();

    connect(m_Virtual, &QCheckBox::clicked, this, [this]() {
        m_VirtualMemory = m_Virtual->isChecked();
        SetupJumpMenu(m_VirtualMemory);
        m_CmbJump->setCurrentIndex(GetJumpItemIndex(m_MemAddr->Value(), m_VirtualMemory));
    });
    connect(SymbolsButton, &QPushButton::clicked, this, [this]() { m_Debugger->OpenSymbolsWindow(); });
    connect(m_MemAddr, &NumberEdit::ValueEdited, this, [this]() { AddrChanged(); });
    connect(m_CmbJump, &QComboBox::activated, this, [this](int Item) {
        if (Item < 0)
        {
            return;
        }
        const uint32_t Address = m_VirtualMemory ? JumpItems[Item].vaddr : JumpItems[Item].paddr;
        SetMemAddr(Address);
        AddrChanged();
        m_Hex->setFocus();
    });
    connect(m_Tabs, &QTabBar::currentChanged, this, [this](int) { TabSelChanged(); });
    Tabs->RightClicked = [this](int Item) { CloseTab(Item); };
    Tabs->DoubleClicked = [this]() { OpenDuplicateTab(); };
    connect(m_Scrollbar, &QScrollBar::actionTriggered, this, [this](int Action) { ScrollAction(Action); });
    connect(m_Scrollbar, &QScrollBar::sliderReleased, this, [this]() {
        // SB_THUMBPOSITION
        SetMemAddr((uint32_t)m_Scrollbar->value() << 0x10);
        AddrChanged();
        m_Scrollbar->setValue(0x8000);
    });
    LoadWindowPos();
}

void MemoryView::SetupHex()
{
    m_Hex->OnRedrawStarted = [this]() { m_SymbolColorPhase = 0; };
    m_Hex->OnGetByteInfo = [this](uint32_t Address, size_t NumBytes, bool IgnoreDiff, HxByteInfo * OldBytes, HxByteInfo * NewBytes) { GetByteInfo(Address, NumBytes, IgnoreDiff, OldBytes, NewBytes); };
    m_Hex->OnSetNibble = [this](bool Insert, uint32_t Address, bool LoNibble, uint8_t Value) { SetNibble(Insert, Address, LoNibble, Value); };
    m_Hex->OnSetByte = [this](bool Insert, uint32_t Address, uint8_t Value) {
        if (g_MMU == nullptr)
        {
            return;
        }
        if (Insert)
        {
            m_SafeEditQueue.push_back({SE_FILL, Address, Address, Value});
        }
        else
        {
            SetByte(Address, Value);
        }
    };
    m_Hex->OnFillRange = [this](bool Insert, uint32_t Start, uint32_t End, uint8_t Value) {
        if (Insert)
        {
            m_SafeEditQueue.push_back({SE_FILL, Start, End, Value});
            return;
        }
        FillRange(Start, End, Value);
    };
    m_Hex->OnInsertModeChanged = [this]() {
        m_SafeEditQueue.clear();
        m_SafeEditMode = m_Hex->GetInsertMode();
        m_Status[MEMSB_SAFEMODE]->setText(m_SafeEditMode ? Tr("Safe mode") : QString());
    };
    m_Hex->OnEnterPressed = [this]() { ApplySafeEdits(); };
    m_Hex->OnSelectionChanged = [this]() { SelectionChanged(); };
    m_Hex->OnGroupSizeChanged = [this]() {
        const int Item = m_Tabs->currentIndex();
        if (Item != -1)
        {
            m_TabData[Item].numBytesPerGroup = m_Hex->GetNumBytesPerGroup();
        }
    };
    m_Hex->OnHotAddrChanged = [this]() { HotAddrChanged(); };
    m_Hex->OnBaseAddrChanged = [this]() { BaseAddrChanged(); };
    m_Hex->OnRightClick = [this](uint32_t Address) { ContextMenuAt(Address, QCursor::pos()); };
    m_Hex->OnCopy = [this]() {
        uint32_t Start, End;
        m_Hex->GetSelectionRange(&Start, &End);
        CopyTextToClipboard(CopyBytesText(Start, End, m_Hex->GetFocusedColumn() == HX_COL_HEXDATA, false, false));
    };
    m_Hex->OnPaste = [this](uint32_t Address, HxColumn Column) { return PasteData(Address, Column); };
    m_Hex->OnCtrlKeyPressed = [this](int Key) { CtrlKeyPressed(Key); };
}

void MemoryView::SetMemAddr(uint32_t Address)
{
    m_MemAddr->SetValue(Address);
}

void MemoryView::ShowAddress(uint32_t Address, bool Virtual)
{
    m_CmbJump->setCurrentIndex(GetJumpItemIndex(Address, Virtual));
    OpenNewTab(Address, Virtual, 4, true, true);
}

bool MemoryView::GetByte(uint32_t Address, uint8_t * Value)
{
    if (m_VirtualMemory)
    {
        return m_Debugger->DebugLoad_VAddr(Address, *Value);
    }
    return m_Debugger->DebugLoad_PAddr(Address, *Value);
}

bool MemoryView::SetByte(uint32_t Address, uint8_t Value)
{
    if (m_VirtualMemory)
    {
        return m_Debugger->DebugStore_VAddr(Address, Value);
    }
    return m_Debugger->DebugStore_PAddr(Address, Value);
}

void MemoryView::CopyTextToClipboard(const QString & Text)
{
    QApplication::clipboard()->setText(Text);
}

QString MemoryView::CopyBytesText(uint32_t StartAddress, uint32_t EndAddress, bool Hex, bool IncludeAddresses, bool RowAddresses)
{
    const uint32_t BaseAddress = m_Hex->GetBaseAddress();
    const int GroupSize = m_Hex->GetNumBytesPerGroup();
    const int RowSize = m_Hex->GetNumBytesPerRow();
    stdstr Str = "";
    for (uint32_t Address = StartAddress; Address <= EndAddress; Address++)
    {
        const int OffsetFromBase = Address - BaseAddress;
        const int OffsetFromSelStart = Address - StartAddress;
        uint8_t Value = 0;
        GetByte(Address, &Value);
        if (IncludeAddresses)
        {
            if ((RowAddresses && OffsetFromBase % RowSize == 0) || (!RowAddresses && OffsetFromBase % GroupSize == 0) || (OffsetFromSelStart == 0))
            {
                Str += stdstr_f("%08X: ", Address);
            }
        }
        if (Hex)
        {
            Str += stdstr_f("%02X", Value);
        }
        else
        {
            Str += HexEditWidget::ByteAscii(Value);
        }
        if ((OffsetFromBase + 1) % RowSize == 0 || (IncludeAddresses && !RowAddresses && (OffsetFromBase + 1) % GroupSize == 0))
        {
            Str += "\r\n";
        }
        else if (Hex && (OffsetFromBase + 1) % GroupSize == 0)
        {
            Str += " ";
        }
        if (Address == 0xFFFFFFFF)
        {
            break;
        }
    }
    return QString::fromLatin1(Str.Trim(" \r\n").c_str());
}

QString MemoryView::GameSharkText(uint32_t StartAddress, uint32_t EndAddress)
{
    stdstr Str = "";
    if (StartAddress & 1)
    {
        uint8_t Value = 0;
        GetByte(StartAddress, &Value);
        Str += stdstr_f("%08X %04X\r\n", StartAddress, Value);
        StartAddress++;
    }
    for (uint32_t Address = StartAddress; Address < EndAddress; Address += 2)
    {
        uint8_t Value0 = 0, Value1 = 0;
        GetByte(Address + 0, &Value0);
        GetByte(Address + 1, &Value1);
        Str += stdstr_f("%08X %02X%02X\r\n", Address | 0x01000000, Value0, Value1);
    }
    if (!(EndAddress & 1))
    {
        uint8_t Value = 0;
        GetByte(EndAddress, &Value);
        Str += stdstr_f("%08X %04X\r\n", EndAddress, Value);
    }
    return QString::fromLatin1(Str.Trim("\n").c_str());
}

void MemoryView::FillRange(uint32_t StartAddress, uint32_t EndAddress, uint8_t Value)
{
    for (uint32_t Address = StartAddress; Address <= EndAddress; Address++)
    {
        SetByte(Address, Value);
        if (Address == 0xFFFFFFFF)
        {
            break;
        }
    }
}

void MemoryView::FollowPointer(bool ContextMenuAddress)
{
    uint32_t Address;
    if (ContextMenuAddress)
    {
        Address = m_ContextMenuAddress & (~3);
    }
    else
    {
        uint32_t SelStart, SelEnd;
        m_Hex->GetSelectionRange(&SelStart, &SelEnd);
        Address = SelStart & (~3);
    }
    uint32_t Pointer;
    // upstream reads through the virtual address in both branches
    const bool Valid = m_Debugger->DebugLoad_VAddr(Address, Pointer);
    if (Valid)
    {
        OpenNewTab(Pointer, m_VirtualMemory, 4, true, true);
    }
}

void MemoryView::JumpToSelection()
{
    uint32_t Start, End;
    const bool HaveSelection = m_Hex->GetSelectionRange(&Start, &End);
    const uint32_t Target = HaveSelection ? Start : m_Hex->GetCaretAddress();
    SetMemAddr(Target);
    AddrChanged(); // EN_CHANGE
}

bool MemoryView::GetSafeEditValue(uint32_t Address, uint8_t * Value)
{
    if (m_SafeEditQueue.size() == 0)
    {
        return false;
    }
    for (size_t i = m_SafeEditQueue.size(); i-- > 0;)
    {
        const Edit & E = m_SafeEditQueue[i];
        if (Address >= E.startAddress && Address <= E.endAddress)
        {
            *Value = E.value;
            return true;
        }
    }
    return false;
}

void MemoryView::ApplySafeEdits()
{
    for (const Edit & E : m_SafeEditQueue)
    {
        if (E.type == SE_FILL)
        {
            FillRange(E.startAddress, E.endAddress, E.value);
        }
    }
    m_SafeEditQueue.clear();
}

void MemoryView::SetupJumpMenu(bool Virtual)
{
    m_CmbJump->clear();
    for (int i = 0; JumpItems[i].caption != nullptr; i++)
    {
        m_CmbJump->addItem(QString("%1 %2").arg(QString("%1").arg(Virtual ? JumpItems[i].vaddr : JumpItems[i].paddr, 8, 16, QChar('0')).toUpper()).arg(Tr(JumpItems[i].caption)));
    }
}

int MemoryView::GetJumpItemIndex(uint32_t Address, bool Virtual)
{
    for (int Item = 0; JumpItems[Item].caption != nullptr; Item++)
    {
        const uint32_t Start = Virtual ? JumpItems[Item].vaddr : JumpItems[Item].paddr;
        const uint32_t End = Start + JumpItems[Item].size - 1;
        if (Address >= Start && Address <= End)
        {
            return Item;
        }
    }
    return -1;
}

// OnAddrChanged
void MemoryView::AddrChanged()
{
    const uint32_t Address = m_MemAddr->Value();
    m_Hex->SetBaseAddress(Address);
    UpdateCurrentTab(Address);
    m_CmbJump->setCurrentIndex(GetJumpItemIndex(Address, m_VirtualMemory));
}

// OnVScroll
void MemoryView::ScrollAction(int Action)
{
    const uint32_t Address = m_MemAddr->Value();
    const int NumBytesPerRow = m_Hex->GetNumBytesPerRow();
    const int NumVisibleBytes = m_Hex->GetNumVisibleBytes();
    bool Changed = true;
    switch (Action)
    {
    case QAbstractSlider::SliderSingleStepAdd:
        SetMemAddr(Address < 0xFFFFFFEF ? Address + NumBytesPerRow : 0xFFFFFFFF);
        break;
    case QAbstractSlider::SliderSingleStepSub:
        SetMemAddr(Address > (uint32_t)NumBytesPerRow ? Address - NumBytesPerRow : 0);
        break;
    case QAbstractSlider::SliderPageStepAdd:
        SetMemAddr(Address < 0xFFFFFEFF ? Address + NumVisibleBytes : 0xFFFFFFFF);
        break;
    case QAbstractSlider::SliderPageStepSub:
        SetMemAddr(Address > (uint32_t)NumVisibleBytes ? Address - NumVisibleBytes : 0);
        break;
    default:
        Changed = false;
        break;
    }
    if (Changed)
    {
        AddrChanged();
        m_Scrollbar->setValue(0x8000);
    }
    m_CmbJump->setCurrentIndex(GetJumpItemIndex(Address, m_VirtualMemory));
}

// OnHxCtrlKeyPressed
void MemoryView::CtrlKeyPressed(int Key)
{
    const uint32_t Address = m_Hex->GetCaretAddress();
    if (Key >= Qt::Key_1 && Key <= Qt::Key_9)
    {
        m_Hex->SetByteGroupSize(Key - Qt::Key_0);
        return;
    }
    switch (Key)
    {
    case Qt::Key_G: JumpToSelection(); break;
    case Qt::Key_W: m_Breakpoints->WBPToggle(Address); break;
    case Qt::Key_N: AddTab(0x80000000, true, 4); break;
    case Qt::Key_R: m_Breakpoints->RBPToggle(Address); break;
    case Qt::Key_E: m_Breakpoints->ToggleMemLock(Address); break;
    case Qt::Key_Q:
        m_Breakpoints->WBPClear();
        m_Breakpoints->RBPClear();
        m_Breakpoints->ClearMemLocks();
        break;
    case Qt::Key_F:
        // TODO (upstream): put selection in the textbox
        m_Debugger->OpenMemorySearch();
        break;
    case Qt::Key_S:
        // TODO (upstream): set start and end address to selection
        m_Debugger->OpenMemoryDump();
        break;
    case Qt::Key_T: OpenDuplicateTab(); break;
    case Qt::Key_Z:
        if (m_SafeEditQueue.size() != 0)
        {
            m_SafeEditQueue.pop_back();
        }
        break;
    case Qt::Key_F4: CloseCurrentTab(); break;
    case Qt::Key_Space: FollowPointer(false); break;
    case Qt::Key_Tab:
    {
        const int CurSel = m_Tabs->currentIndex();
        m_Tabs->setCurrentIndex(CurSel + 1 < m_Tabs->count() ? CurSel + 1 : 0);
        TabSelChanged();
        break;
    }
    }
}

// OnHxSetNibble
void MemoryView::SetNibble(bool Insert, uint32_t Address, bool LoNibble, uint8_t Value)
{
    if (g_MMU == nullptr)
    {
        return;
    }
    uint8_t CurValue;
    if (!GetByte(Address, &CurValue))
    {
        return;
    }
    const uint8_t Mask = (LoNibble ? 0xF0 : 0x0F);
    uint8_t NewValue = (CurValue & Mask) | (Value << (LoNibble ? 0 : 4));
    if (Insert)
    {
        if (GetSafeEditValue(Address, &CurValue))
        {
            NewValue = (CurValue & Mask) | (Value << (LoNibble ? 0 : 4));
        }
        m_SafeEditQueue.push_back({SE_FILL, Address, Address, NewValue});
    }
    else
    {
        SetByte(Address, NewValue);
    }
}

void MemoryView::SelectionChanged()
{
    uint32_t Start, End;
    const bool HaveSelection = m_Hex->GetSelectionRange(&Start, &End);
    if (HaveSelection)
    {
        m_Status[MEMSB_BLOCK]->setText(QString("%1:%2").arg(Start, 8, 16, QChar('0')).arg(End, 8, 16, QChar('0')).toUpper());
        m_Status[MEMSB_BLOCKLEN]->setText(QString("%1").arg(End - Start + 1, 0, 16).toUpper());
    }
    else
    {
        m_Status[MEMSB_BLOCK]->setText(QString("%1").arg(Start, 8, 16, QChar('0')).toUpper());
        m_Status[MEMSB_BLOCKLEN]->setText("");
    }
    uint32_t RomAddr, Offset;
    DMALOGENTRY * Entry = m_Debugger->DMALog()->GetEntryByRamAddress(Start, &RomAddr, &Offset);
    m_Status[MEMSB_DMAINFO]->setText(Entry != nullptr ? Tr("Have DMA") : QString());
}

void MemoryView::HotAddrChanged()
{
    m_HotAddress = m_Hex->GetHotAddress();
    QString Info;
    CSymbol Symbol;
    if (m_Debugger->SymbolTable()->GetSymbolByAddress(m_HotAddress, &Symbol))
    {
        Info = QString("%1 %2 %3").arg(Symbol.m_Address, 8, 16, QChar('0')).toUpper() + " " + Symbol.TypeName() + " " + Symbol.m_Name;
    }
    else
    {
        Info = QString("%1").arg(m_HotAddress, 8, 16, QChar('0')).toUpper();
    }
    m_Status[MEMSB_HOTADDR]->setText(Info);
}

// Address was updated from the control
void MemoryView::BaseAddrChanged()
{
    const uint32_t Address = m_Hex->GetBaseAddress();
    SetMemAddr(Address);
    m_CmbJump->setCurrentIndex(GetJumpItemIndex(Address, m_VirtualMemory));
    UpdateCurrentTab(Address);
}

// OnHxGetByteInfo
void MemoryView::GetByteInfo(uint32_t Address0, size_t NumBytes, bool IgnoreDiff, HxByteInfo * OldBytes, HxByteInfo * NewBytes)
{
    bool HaveWriteTarget = false, HaveReadTarget = false;
    uint32_t CpuReadWriteAddress = 0;
    int CpuReadWriteNumBytes = 0;
    if (g_Settings->LoadBool(Debugger_SteppingOps) && g_System != nullptr)
    {
        COpInfo OpInfo(g_System->Opcode());
        if (OpInfo.IsStoreCommand())
        {
            CpuReadWriteAddress = OpInfo.GetLoadStoreAddress();
            CpuReadWriteNumBytes = OpInfo.NumBytesToStore();
            HaveWriteTarget = true;
        }
        else if (OpInfo.IsLoadCommand())
        {
            CpuReadWriteAddress = OpInfo.GetLoadStoreAddress();
            CpuReadWriteNumBytes = OpInfo.NumBytesToLoad();
            HaveReadTarget = true;
        }
    }
    for (uint32_t i = 0; i < NumBytes; i++)
    {
        const uint32_t Address = Address0 + i;
        uint32_t PAddress = Address;
        HxByteInfo * OldByte = &OldBytes[i];
        HxByteInfo * NewByte = &NewBytes[i];
        NewByte->bkColor = BKCOLOR_DEFAULT;
        NewByte->color = COLOR_DEFAULT;
        if (m_VirtualMemory && (g_MMU == nullptr || !g_MMU->VAddrToPAddr(Address, PAddress)))
        {
            NewByte->valid = false;
            continue;
        }
        NewByte->valid = GetByte(Address, &NewByte->value);
        if (!NewByte->valid)
        {
            continue;
        }
        // Always use virtual addresses for breakpoint and symbol info
        // TODO (upstream): should be the other way around
        const uint32_t VAddress = m_VirtualMemory ? Address : Address + 0x80000000;
        CSymbol Symbol;
        if (m_Debugger->SymbolTable()->GetSymbolByAddress(VAddress, &Symbol))
        {
            m_SymbolColorStride = Symbol.TypeSize();
            m_SymbolColorPhase = m_SymbolColorPhase ? 0 : 1;
        }
        if (HaveWriteTarget && Address == CpuReadWriteAddress)
        {
            m_WriteTargetColorStride = CpuReadWriteNumBytes;
        }
        else if (HaveReadTarget && Address == CpuReadWriteAddress)
        {
            m_ReadTargetColorStride = CpuReadWriteNumBytes;
        }
        const bool Locked = m_Breakpoints->MemLockExists(VAddress, 1);
        const bool ReadBP = m_Breakpoints->ReadBPExists8(VAddress) == CBreakpoints::BP_SET;
        const bool WriteBP = m_Breakpoints->WriteBPExists8(VAddress) == CBreakpoints::BP_SET;
        if (Locked)
        {
            NewByte->bkColor = BKCOLOR_LOCKED;
            NewByte->color = COLOR_BP;
        }
        else if (ReadBP && WriteBP)
        {
            NewByte->bkColor = BKCOLOR_RWBP;
            NewByte->color = COLOR_BP;
        }
        else if (ReadBP)
        {
            NewByte->bkColor = BKCOLOR_RBP;
            NewByte->color = COLOR_BP;
        }
        else if (WriteBP)
        {
            NewByte->bkColor = BKCOLOR_WBP;
            NewByte->color = COLOR_BP;
        }
        else if (m_ReadTargetColorStride > 0)
        {
            NewByte->bkColor = BKCOLOR_CPUREAD;
        }
        else if (m_WriteTargetColorStride > 0)
        {
            NewByte->bkColor = BKCOLOR_CPUWRITE;
        }
        else if (m_SymbolColorStride > 0)
        {
            NewByte->bkColor = m_SymbolColorPhase ? BKCOLOR_SYMBOL0 : BKCOLOR_SYMBOL1;
        }
        if (g_Rom != nullptr && PAddress >= 0x10000000 && PAddress < 0x10000000 + g_Rom->GetRomSize())
        {
            NewByte->color = COLOR_READONLY;
        }
        if (!IgnoreDiff && OldByte->value != NewByte->value)
        {
            NewByte->color = COLOR_CHANGED;
        }
        if (m_SymbolColorStride > 0)
        {
            m_SymbolColorStride--;
        }
        if (m_ReadTargetColorStride > 0)
        {
            m_ReadTargetColorStride--;
        }
        if (m_WriteTargetColorStride > 0)
        {
            m_WriteTargetColorStride--;
        }
        uint8_t SafeEditValue;
        if (GetSafeEditValue(Address, &SafeEditValue))
        {
            NewByte->valid = true;
            NewByte->value = SafeEditValue;
            NewByte->bkColor = 0xFFCCFF;
            NewByte->color = 0xFF00FF;
        }
        NewByte->hidden = false;
    }
}

// OnHxPaste: hex text in the hex column, raw characters in the ASCII column
int MemoryView::PasteData(uint32_t Address, HxColumn Column)
{
    if (g_MMU == nullptr)
    {
        return 0;
    }
    const QByteArray Text = QApplication::clipboard()->text().toLatin1();
    int RetDataLength = 0;
    if (Column == HX_COL_HEXDATA)
    {
        // TODO (upstream): move this function to some utility class
        const int Length = CMemoryScanner::ParseHexString(nullptr, Text.constData());
        if (Length != 0)
        {
            std::unique_ptr<char[]> Data = std::make_unique<char[]>(Length);
            CMemoryScanner::ParseHexString(Data.get(), Text.constData());
            for (int i = 0; i < Length; i++)
            {
                SetByte(Address + i, Data.get()[i]);
            }
        }
        RetDataLength = Length;
    }
    else if (Column == HX_COL_ASCII)
    {
        const size_t Length = (size_t)Text.size();
        for (size_t i = 0; i < Length; i++)
        {
            SetByte((uint32_t)(Address + i), Text[(int)i]);
        }
        RetDataLength = (int)Length;
    }
    return RetDataLength;
}

// OnStatusBarClick
void MemoryView::StatusClicked(int Pane)
{
    uint32_t Start, End;
    const bool HaveSelection = m_Hex->GetSelectionRange(&Start, &End);
    if (Pane == MEMSB_DMAINFO)
    {
        uint32_t RomAddress, BlockOffset;
        DMALOGENTRY * Entry = m_Debugger->DMALog()->GetEntryByRamAddress(Start, &RomAddress, &BlockOffset);
        if (Entry == nullptr)
        {
            return;
        }
        const QString Title = Tr("DMA information for 0x%1").arg(Start, 8, 16, QChar('0')).toUpper();
        const QString Info = Tr("Block:\nROM 0x%1 -> RAM 0x%2 ( 0x%3 bytes )\n\nROM address of byte:\n0x%4 ( 0x%5 + 0x%6 )")
                                 .arg(Entry->romAddr, 8, 16, QChar('0')).arg(Entry->ramAddr, 8, 16, QChar('0')).arg(Entry->length, 0, 16).arg(RomAddress, 8, 16, QChar('0')).arg(Entry->romAddr, 8, 16, QChar('0')).arg(BlockOffset, 8, 16, QChar('0'));
        QMessageBox::information(this, Title, Info.toUpper().replace("0X", "0x"));
    }
    else if (Pane == MEMSB_BLOCK)
    {
        QString Range;
        if (HaveSelection)
        {
            Range = QString("%1:%2").arg(Start, 8, 16, QChar('0')).arg(End, 8, 16, QChar('0')).toUpper();
        }
        else
        {
            Range = QString("%1").arg(Start, 8, 16, QChar('0')).toUpper();
        }
        CopyTextToClipboard(Range);
    }
}

// OnHxRightClick: the IDR_MEM_BP_POPUP menu
void MemoryView::ContextMenuAt(uint32_t Address, const QPoint & GlobalPos, bool Exec)
{
    m_ContextMenuAddress = Address;
    const bool HaveLock = m_Breakpoints->MemLockExists(m_ContextMenuAddress, 1);
    const bool HaveReadBP = (m_Breakpoints->ReadBPExists8(m_ContextMenuAddress) != CBreakpoints::BPSTATE::BP_NOT_SET);
    const bool HaveWriteBP = (m_Breakpoints->WriteBPExists8(m_ContextMenuAddress) != CBreakpoints::BPSTATE::BP_NOT_SET);

    QMenu Menu(this);
    auto Checked = [](QAction * Action, bool On) {
        Action->setCheckable(true);
        Action->setChecked(On);
    };
    Checked(Menu.addAction(Tr("Read breakpoint\tCtrl+R"), [this]() { m_Breakpoints->RBPToggle(m_ContextMenuAddress); }), HaveReadBP);
    Checked(Menu.addAction(Tr("Write breakpoint\tCtrl+W"), [this]() { m_Breakpoints->WBPToggle(m_ContextMenuAddress); }), HaveWriteBP);
    Checked(Menu.addAction(Tr("Lock value\tCtrl+E"), [this]() { m_Breakpoints->ToggleMemLock(m_ContextMenuAddress); }), HaveLock);
    Menu.addSeparator();
    Menu.addAction(Tr("Clear breakpoints\tCtrl+Q"), [this]() {
                m_Breakpoints->RBPClear();
                m_Breakpoints->WBPClear();
            })
        ->setEnabled(!(m_Breakpoints->ReadMem().size() == 0 && m_Breakpoints->WriteMem().size() == 0));
    Menu.addAction(Tr("Clear locks\tCtrl+Q"), [this]() { m_Breakpoints->ClearMemLocks(); })->setEnabled(m_Breakpoints->NumMemLocks() != 0);
    Menu.addSeparator();
    Menu.addAction(Tr("Jump here\tCtrl+G"), [this]() { JumpToSelection(); });
    Menu.addAction(Tr("Follow pointer\tCtrl+Space"), [this]() { FollowPointer(); });
    Menu.addSeparator();
    QMenu * Group = Menu.addMenu(Tr("Byte group size"));
    Group->addAction(Tr("1\tCtrl+1"), [this]() { m_Hex->SetByteGroupSize(1); });
    Group->addAction(Tr("2\tCtrl+2"), [this]() { m_Hex->SetByteGroupSize(2); });
    Group->addAction(Tr("4\tCtrl+4"), [this]() { m_Hex->SetByteGroupSize(4); });
    Group->addAction(Tr("8\tCtrl+8"), [this]() { m_Hex->SetByteGroupSize(8); });
    Checked(Menu.addAction(Tr("Safe mode\tINS"), [this]() { m_Hex->KeyDown(Qt::Key_Insert); }), m_SafeEditMode);
    Menu.addSeparator();
    Menu.addAction(Tr("Add symbol..."), [this]() { DebugDialogs::AddSymbol(m_Debugger, this, m_ContextMenuAddress); });
    Menu.addAction(Tr("View disassembly..."), [this]() { m_Debugger->Debug_ShowCommandsLocation(m_ContextMenuAddress, true); });
    Menu.addAction(Tr("Dump...\tCtrl+S"), [this]() { m_Debugger->OpenMemoryDump(); });
    Menu.addAction(Tr("Search...\tCtrl+F"), [this]() { m_Debugger->OpenMemorySearch(); });
    Menu.addSeparator();
    QMenu * Special = Menu.addMenu(Tr("Copy special"));
    Special->addAction(Tr("Data with group addresses"), [this]() {
        uint32_t Start, End;
        m_Hex->GetSelectionRange(&Start, &End);
        CopyTextToClipboard(CopyBytesText(Start, End, m_Hex->GetFocusedColumn() == HX_COL_HEXDATA, true, false));
    });
    Special->addAction(Tr("Data with row addresses"), [this]() {
        uint32_t Start, End;
        m_Hex->GetSelectionRange(&Start, &End);
        CopyTextToClipboard(CopyBytesText(Start, End, m_Hex->GetFocusedColumn() == HX_COL_HEXDATA, true, true));
    });
    Special->addAction(Tr("GameShark code"), [this]() {
        uint32_t Start, End;
        m_Hex->GetSelectionRange(&Start, &End);
        CopyTextToClipboard(GameSharkText(Start, End));
    });
    Menu.addAction(Tr("Copy\tCtrl+C"), [this]() { m_Hex->Copy(); });
    Menu.addAction(Tr("Paste\tCtrl+V"), [this]() { m_Hex->Paste(); });
    Menu.addAction(Tr("Zero-fill\tDEL"), [this]() { m_Hex->KeyDown(Qt::Key_Delete); });
    if (Exec)
    {
        Menu.exec(GlobalPos);
    }
}

// ---- tabs ----

void MemoryView::TabSelChanged()
{
    const int Item = m_Tabs->currentIndex();
    if (Item < 0 || Item >= (int)m_TabData.size())
    {
        return;
    }
    const TabInfo Info = m_TabData[Item];
    const uint32_t Address = Info.address;
    SetMemAddr(Address);
    if (m_VirtualMemory != Info.isVirtual)
    {
        m_VirtualMemory = Info.isVirtual;
        m_Virtual->setChecked(m_VirtualMemory);
        SetupJumpMenu(m_VirtualMemory);
    }
    m_CmbJump->setCurrentIndex(GetJumpItemIndex(Address, m_VirtualMemory));
    m_Hex->SetBaseAddress(Address);
    m_Hex->SetByteGroupSize(Info.numBytesPerGroup);
}

int MemoryView::AddTab(uint32_t Address, bool Virtual, int NumBytesPerGroup)
{
    m_TabData.push_back({Address, Virtual, NumBytesPerGroup});
    QSignalBlocker Block(m_Tabs);
    return m_Tabs->addTab(QString("%1").arg(Address, 8, 16, QChar('0')).toUpper());
}

int MemoryView::InsertTab(int Item, uint32_t Address, bool Virtual, int NumBytesPerGroup)
{
    m_TabData.insert(m_TabData.begin() + Item + 1, {Address, Virtual, NumBytesPerGroup});
    QSignalBlocker Block(m_Tabs);
    while (m_Tabs->count() > 0)
    {
        m_Tabs->removeTab(0);
    }
    for (size_t i = 0; i < m_TabData.size(); i++)
    {
        m_Tabs->addTab(QString("%1").arg(m_TabData[i].address, 8, 16, QChar('0')).toUpper());
    }
    return Item + 1;
}

void MemoryView::DeleteTab(int Item)
{
    m_TabData.erase(m_TabData.begin() + Item);
    QSignalBlocker Block(m_Tabs);
    m_Tabs->removeTab(Item);
}

void MemoryView::UpdateCurrentTab(uint32_t Address)
{
    const int Item = m_Tabs->currentIndex();
    if (Item == -1)
    {
        return;
    }
    m_Tabs->setTabText(Item, QString("%1").arg(Address, 8, 16, QChar('0')).toUpper());
    m_TabData[Item].address = Address;
    m_TabData[Item].isVirtual = m_VirtualMemory;
}

void MemoryView::OpenNewTab(uint32_t Address, bool Virtual, int NumBytesPerGroup, bool Insert, bool OpenExisting)
{
    int Item;
    if (OpenExisting)
    {
        for (size_t i = 0; i < m_TabData.size(); i++)
        {
            if (m_TabData[i].address == Address && m_TabData[i].isVirtual == Virtual)
            {
                QSignalBlocker Block(m_Tabs);
                m_Tabs->setCurrentIndex((int)i);
                Block.unblock();
                TabSelChanged();
                return;
            }
        }
    }
    if (Insert)
    {
        Item = InsertTab(m_Tabs->currentIndex(), Address, Virtual, NumBytesPerGroup);
    }
    else
    {
        Item = AddTab(Address, Virtual, NumBytesPerGroup);
    }
    {
        QSignalBlocker Block(m_Tabs);
        m_Tabs->setCurrentIndex(Item);
    }
    TabSelChanged();
}

void MemoryView::OpenDuplicateTab()
{
    const int Item = m_Tabs->currentIndex();
    if (Item < 0)
    {
        return;
    }
    const TabInfo Info = m_TabData[Item];
    const int NewItem = InsertTab(Item, Info.address, Info.isVirtual, Info.numBytesPerGroup);
    {
        QSignalBlocker Block(m_Tabs);
        m_Tabs->setCurrentIndex(NewItem);
    }
    TabSelChanged();
}

void MemoryView::CloseTab(int Item)
{
    const int ItemCount = m_Tabs->count();
    const int SelItem = m_Tabs->currentIndex();
    if (ItemCount < 2 || Item == -1)
    {
        return;
    }
    if (Item == SelItem)
    {
        {
            QSignalBlocker Block(m_Tabs);
            m_Tabs->setCurrentIndex(Item == m_Tabs->count() - 1 ? Item - 1 : Item + 1);
        }
        TabSelChanged();
    }
    DeleteTab(Item);
}

void MemoryView::CloseCurrentTab()
{
    CloseTab(m_Tabs->currentIndex());
}
