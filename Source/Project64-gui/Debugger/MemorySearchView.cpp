#include "stdafx.h"
#include "../Vortex/Strings.h"

#include "MemorySearchView.h"
#include "DebugCommon.h"
#include "DebugDialogs.h"
#include "DebuggerUI.h"

#include <Common/File.h>
#include <Common/path.h>
#include <Project64-core/N64System/N64Rom.h>
#include <Project64-core/Settings/DebugSettings.h>

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollBar>
#include <QSplitter>
#include <QTableWidget>
#include <QThread>
#include <QTimer>
#include <QWheelEvent>

using Vortex::Tr;

// ---------------------------------------------------------------------------------------
// MixedEdit (CEditMixed)
// ---------------------------------------------------------------------------------------

MixedEdit::MixedEdit(QWidget * Parent) :
    QLineEdit(Parent)
{
}

bool MixedEdit::GetValue(uint8_t & Value)
{
    uint64_t ValueU64;
    const bool Valid = GetValue(ValueU64);
    if (!Valid || ValueU64 > UINT8_MAX)
    {
        return false;
    }
    Value = (uint8_t)(ValueU64 & 0xFF);
    return true;
}

bool MixedEdit::GetValue(int8_t & Value)
{
    if (m_DisplayFormat == DisplayHex)
    {
        return GetValue((uint8_t &)Value);
    }
    int64_t ValueS64;
    const bool Valid = GetValue(ValueS64);
    if (!Valid || ValueS64 > INT8_MAX || ValueS64 < INT8_MIN)
    {
        return false;
    }
    Value = (int8_t)(ValueS64 & 0xFF);
    return true;
}

bool MixedEdit::GetValue(uint16_t & Value)
{
    uint64_t ValueU64;
    const bool Valid = GetValue(ValueU64);
    if (!Valid || ValueU64 > UINT16_MAX)
    {
        return false;
    }
    Value = (uint16_t)(ValueU64 & 0xFFFF);
    return true;
}

bool MixedEdit::GetValue(int16_t & Value)
{
    if (m_DisplayFormat == DisplayHex)
    {
        return GetValue((uint16_t &)Value);
    }
    int64_t ValueS64;
    const bool Valid = GetValue(ValueS64);
    if (!Valid || ValueS64 > INT16_MAX || ValueS64 < INT16_MIN)
    {
        return false;
    }
    Value = (int16_t)(ValueS64 & 0xFFFF);
    return true;
}

bool MixedEdit::GetValue(uint32_t & Value)
{
    uint64_t ValueU64;
    const bool Valid = GetValue(ValueU64);
    if (!Valid || ValueU64 > UINT32_MAX)
    {
        return false;
    }
    Value = (uint32_t)(ValueU64 & 0xFFFFFFFF);
    return true;
}

bool MixedEdit::GetValue(int32_t & Value)
{
    if (m_DisplayFormat == DisplayHex)
    {
        return GetValue((uint32_t &)Value);
    }
    int64_t ValueS64;
    const bool Valid = GetValue(ValueS64);
    if (!Valid || (ValueS64 > INT32_MAX) || (ValueS64 < INT32_MIN))
    {
        return false;
    }
    Value = (int32_t)(ValueS64 & 0xFFFFFFFF);
    return true;
}

bool MixedEdit::GetValue(uint64_t & Value)
{
    const QByteArray Text = text().toUtf8();
    char * End;
    const uint64_t Res = strtoull(Text.constData(), &End, m_DisplayFormat == DisplayHex ? 16 : 10);
    if (*End != '\0')
    {
        return false; // Parse failure
    }
    Value = Res;
    return true;
}

bool MixedEdit::GetValue(int64_t & Value)
{
    if (m_DisplayFormat == DisplayHex)
    {
        return GetValue((uint64_t &)Value);
    }
    const QByteArray Text = text().toUtf8();
    char * End;
    const uint64_t Res = strtoll(Text.constData(), &End, 10);
    if (*End != '\0')
    {
        return false; // Parse failure
    }
    Value = Res;
    return true;
}

bool MixedEdit::GetValue(float & Value)
{
    if (m_DisplayFormat == DisplayHex)
    {
        return GetValue((uint32_t &)Value);
    }
    const QByteArray Text = text().toUtf8();
    char * End;
    const float ValueF32 = strtof(Text.constData(), &End);
    if (*End != '\0')
    {
        return false;
    }
    Value = ValueF32;
    return true;
}

bool MixedEdit::GetValue(double & Value)
{
    if (m_DisplayFormat == DisplayHex)
    {
        return GetValue((uint64_t &)Value);
    }
    const QByteArray Text = text().toUtf8();
    char * End;
    const double ValueF64 = strtod(Text.constData(), &End);
    if (*End != '\0')
    {
        return false;
    }
    Value = ValueF64;
    return true;
}

bool MixedEdit::GetValueString(const wchar_t *& Value, int & Length)
{
    m_String = text().toStdWString();
    if (m_String.empty())
    {
        return false;
    }
    Value = m_String.c_str();
    Length = (int)m_String.length();
    return true;
}

bool MixedEdit::GetValueHexString(const wchar_t *& Value, int & Length)
{
    const std::wstring Text = text().toStdWString();
    if (Text.empty())
    {
        return false;
    }
    const stdstr String = text().toStdString();
    const int NumBytes = CMemoryScanner::ParseHexString(nullptr, String.c_str());
    if (NumBytes == 0)
    {
        return false;
    }
    std::string HexString(NumBytes, '\0');
    CMemoryScanner::ParseHexString(&HexString[0], String.c_str());
    // each byte is one character of the string that the scanner compares
    m_String.clear();
    for (int i = 0; i < NumBytes; i++)
    {
        m_String.push_back((wchar_t)(uint8_t)HexString[i]);
    }
    Value = m_String.c_str();
    Length = NumBytes;
    return true;
}

// ---------------------------------------------------------------------------------------
// MemorySearchView
// ---------------------------------------------------------------------------------------

const MemorySearchView::ModalItem MemorySearchView::ModalChangeTypeItems[] = {
    {"uint8", ValueType_uint8},
    {"int8", ValueType_int8},
    {"uint16", ValueType_uint16},
    {"int16", ValueType_int16},
    {"uint32", ValueType_uint32},
    {"int32", ValueType_int32},
    {"uint64", ValueType_uint64},
    {"int64", ValueType_int64},
    {"float", ValueType_float},
    {"double", ValueType_double},
    {"string", ValueType_string},
    {nullptr, 0},
};

namespace
{
enum
{
    GS_TWOBYTE = 0x01000000
};
QString Hex8(uint32_t Value)
{
    return QString("%1").arg(Value, 8, 16, QChar('0')).toUpper();
}
} // namespace

MemorySearchView::MemorySearchView(CDebuggerUI * Debugger) :
    DebugWindow(Debugger, DebuggerUI_MemorySearchPos, true)
{
    setWindowTitle(Tr("Search Memory"));
    resize(357 * 2, 257 * 1.8);
    QGridLayout * Top = new QGridLayout;

    // Results
    m_ResultsGroup = new QGroupBox(Tr("Results"), this);
    QGridLayout * ResultsLayout = new QGridLayout(m_ResultsGroup);
    m_Results = new QTableWidget(0, 3, m_ResultsGroup);
    m_Results->setHorizontalHeaderLabels({Tr("Address"), Tr("Value"), Tr("Previous")});
    m_Results->verticalHeader()->hide();
    m_Results->verticalHeader()->setDefaultSectionSize(m_Results->fontMetrics().height() + 4);
    m_Results->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    m_Results->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_Results->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_Results->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_Results->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_Results->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_Results->setContextMenuPolicy(Qt::CustomContextMenu);
    m_Results->horizontalHeader()->setStretchLastSection(true);
    m_Results->setColumnWidth(0, 100);
    m_Results->setColumnWidth(1, 100);
    m_ResultsScrollbar = new QScrollBar(Qt::Vertical, m_ResultsGroup);
    m_ResultsScrollbar->setEnabled(false);
    ResultsLayout->addWidget(m_Results, 0, 0);
    ResultsLayout->addWidget(m_ResultsScrollbar, 0, 1);
    Top->addWidget(m_ResultsGroup, 0, 0, 1, 1);

    // Search options
    QWidget * Options = new QWidget(this);
    QGridLayout * OptionsLayout = new QGridLayout(Options);
    OptionsLayout->setContentsMargins(0, 0, 0, 0);
    m_SearchButton = new QPushButton(Tr("Search"), Options);
    m_ResetButton = new QPushButton(Tr("Reset"), Options);
    QHBoxLayout * Buttons = new QHBoxLayout;
    Buttons->addWidget(m_SearchButton);
    Buttons->addWidget(m_ResetButton);
    Buttons->addStretch();
    OptionsLayout->addLayout(Buttons, 0, 0);
    QGroupBox * SearchGroup = new QGroupBox(Tr("Search value"), Options);
    QGridLayout * SearchLayout = new QGridLayout(SearchGroup);
    m_HexCheckbox = new QCheckBox(Tr("Hex"), SearchGroup);
    m_SearchValue = new MixedEdit(SearchGroup);
    m_SearchTypeOptions = new QComboBox(SearchGroup);
    m_ValueTypeOptions = new QComboBox(SearchGroup);
    m_UnsignedCheckbox = new QCheckBox(Tr("Unsigned"), SearchGroup);
    m_IgnoreCaseCheckbox = new QCheckBox(Tr("Ignore case"), SearchGroup);
    m_UnkEncodingCheckbox = new QCheckBox(Tr("Unknown encoding"), SearchGroup);
    m_IgnoreCaseCheckbox->hide();
    m_UnkEncodingCheckbox->hide();
    SearchLayout->addWidget(m_HexCheckbox, 0, 0);
    SearchLayout->addWidget(m_SearchValue, 0, 1, 1, 2);
    SearchLayout->addWidget(new QLabel(Tr("Scan type"), SearchGroup), 1, 0);
    SearchLayout->addWidget(m_SearchTypeOptions, 1, 1, 1, 2);
    SearchLayout->addWidget(new QLabel(Tr("Value type"), SearchGroup), 2, 0);
    SearchLayout->addWidget(m_ValueTypeOptions, 2, 1, 1, 2);
    QHBoxLayout * Checks = new QHBoxLayout;
    Checks->addWidget(m_IgnoreCaseCheckbox);
    Checks->addWidget(m_UnkEncodingCheckbox);
    Checks->addWidget(m_UnsignedCheckbox);
    SearchLayout->addLayout(Checks, 3, 0, 1, 3);
    OptionsLayout->addWidget(SearchGroup, 1, 0);
    QGroupBox * AddressGroup = new QGroupBox(Tr("Address range"), Options);
    QGridLayout * AddressLayout = new QGridLayout(AddressGroup);
    m_AddrStart = new NumberEdit(AddressGroup, NumberEdit::DisplayHex);
    m_AddrEnd = new NumberEdit(AddressGroup, NumberEdit::DisplayHex);
    m_PhysicalCheckbox = new QCheckBox(Tr("Physical"), AddressGroup);
    m_RdramButton = new QPushButton("RDRAM", AddressGroup);
    m_RomButton = new QPushButton("ROM", AddressGroup);
    m_SpmemButton = new QPushButton(Tr("SPMEM"), AddressGroup);
    AddressLayout->addWidget(new QLabel(Tr("Start: "), AddressGroup), 0, 0);
    AddressLayout->addWidget(m_AddrStart, 0, 1);
    AddressLayout->addWidget(new QLabel(Tr("End:"), AddressGroup), 0, 2);
    AddressLayout->addWidget(m_AddrEnd, 0, 3);
    AddressLayout->addWidget(m_PhysicalCheckbox, 1, 0);
    AddressLayout->addWidget(m_RdramButton, 1, 1);
    AddressLayout->addWidget(m_RomButton, 1, 2);
    AddressLayout->addWidget(m_SpmemButton, 1, 3);
    OptionsLayout->addWidget(AddressGroup, 2, 0);
    Top->addWidget(Options, 0, 1);
    Top->setColumnStretch(0, 1);
    QWidget * TopWidget = new QWidget(this);
    TopWidget->setLayout(Top);

    // Watch list
    QWidget * Bottom = new QWidget(this);
    QGridLayout * BottomLayout = new QGridLayout(Bottom);
    BottomLayout->setContentsMargins(0, 0, 0, 0);
    m_Watch = new QTableWidget(0, WatchList_Num_Columns, Bottom);
    m_Watch->setHorizontalHeaderLabels({Tr("Lock"), "BP", Tr("Address"), Tr("Description"), Tr("Type"), Tr("Value")});
    m_Watch->verticalHeader()->hide();
    m_Watch->verticalHeader()->setDefaultSectionSize(m_Watch->fontMetrics().height() + 4);
    m_Watch->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    m_Watch->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_Watch->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_Watch->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_Watch->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_Watch->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_Watch->setContextMenuPolicy(Qt::CustomContextMenu);
    m_Watch->horizontalHeader()->setStretchLastSection(true);
    m_Watch->setColumnWidth(WatchList_Col_Lock, 45);
    m_Watch->setColumnWidth(WatchList_Col_BP, 45);
    m_Watch->setColumnWidth(WatchList_Col_Address, 100);
    m_Watch->setColumnWidth(WatchList_Col_Description, 200);
    m_Watch->setColumnWidth(WatchList_Col_Type, 70);
    m_WatchScrollbar = new QScrollBar(Qt::Vertical, Bottom);
    m_WatchScrollbar->setEnabled(false);
    BottomLayout->addWidget(m_Watch, 0, 0);
    BottomLayout->addWidget(m_WatchScrollbar, 0, 1);

    m_Splitter = new QSplitter(Qt::Vertical, this);
    m_Splitter->addWidget(TopWidget);
    m_Splitter->addWidget(Bottom);
    m_Splitter->setStretchFactor(1, 1);
    QVBoxLayout * Main = new QVBoxLayout(this);
    Main->setContentsMargins(4, 4, 4, 4);
    Main->addWidget(m_Splitter);

    m_SearchValue->SetDisplayFormat(DisplayDefault);
    UpdateOptions(); // Setup search type combo box
    m_ValueTypeOptions->addItem("int8", (int)ValueType_int8);
    m_ValueTypeOptions->addItem("int16", (int)ValueType_int16);
    m_ValueTypeOptions->addItem("int32", (int)ValueType_int32);
    m_ValueTypeOptions->addItem("int64", (int)ValueType_int64);
    m_ValueTypeOptions->addItem("float", (int)ValueType_float);
    m_ValueTypeOptions->addItem("double", (int)ValueType_double);
    m_ValueTypeOptions->addItem("string", (int)ValueType_string);
    m_ValueTypeOptions->setCurrentIndex(0);

    const uint32_t RamSize = (g_MMU != nullptr) ? g_MMU->RdramSize() : 0x00400000;
    m_AddrStart->SetValue(0x80000000);
    m_AddrEnd->SetValue(0x80000000 + RamSize - 1);

    connect(m_HexCheckbox, &QCheckBox::clicked, this, [this]() { m_SearchValue->SetDisplayFormat(m_HexCheckbox->isChecked() ? DisplayHex : DisplayDefault); });
    connect(m_SearchButton, &QPushButton::clicked, this, [this]() { DoSearch(); });
    connect(m_ResetButton, &QPushButton::clicked, this, [this]() { DoReset(); });
    connect(m_SearchTypeOptions, &QComboBox::activated, this, [this](int) { ScanTypeChanged(); });
    connect(m_ValueTypeOptions, &QComboBox::activated, this, [this](int) { ValueTypeChanged(); });
    connect(m_RdramButton, &QPushButton::clicked, this, [this]() {
        const bool PhysicalChecked = m_PhysicalCheckbox->isChecked();
        const uint32_t AddrStart = PhysicalChecked ? 0 : 0x80000000;
        const uint32_t RamSize2 = (g_MMU != nullptr) ? g_MMU->RdramSize() : 0x00400000;
        m_AddrStart->SetValue(AddrStart);
        m_AddrEnd->SetValue(AddrStart + RamSize2 - 1);
    });
    connect(m_RomButton, &QPushButton::clicked, this, [this]() {
        const bool PhysicalChecked = m_PhysicalCheckbox->isChecked();
        const uint32_t AddrStart = PhysicalChecked ? 0x10000000 : 0xB0000000;
        m_AddrStart->SetValue(AddrStart);
        m_AddrEnd->SetValue(AddrStart + (g_Rom != nullptr ? g_Rom->GetRomSize() : 0) - 1);
    });
    connect(m_SpmemButton, &QPushButton::clicked, this, [this]() {
        const bool PhysicalChecked = m_PhysicalCheckbox->isChecked();
        const uint32_t AddrStart = PhysicalChecked ? 0x04000000 : 0xA4000000;
        m_AddrStart->SetValue(AddrStart);
        m_AddrEnd->SetValue(AddrStart + 0x1FFF);
    });
    connect(m_ResultsScrollbar, &QScrollBar::actionTriggered, this, [this](int Action) {
        int NewPos = m_ResultsScrollbar->value();
        if (Action == QAbstractSlider::SliderSingleStepSub) NewPos--;
        else if (Action == QAbstractSlider::SliderSingleStepAdd) NewPos++;
        else if (Action == QAbstractSlider::SliderMove) NewPos = m_ResultsScrollbar->sliderPosition();
        else return;
        m_ResultsScrollbar->setValue(NewPos);
        UpdateResultsList();
    });
    connect(m_WatchScrollbar, &QScrollBar::actionTriggered, this, [this](int Action) {
        int NewPos = m_WatchScrollbar->value();
        if (Action == QAbstractSlider::SliderSingleStepSub) NewPos--;
        else if (Action == QAbstractSlider::SliderSingleStepAdd) NewPos++;
        else if (Action == QAbstractSlider::SliderMove) NewPos = m_WatchScrollbar->sliderPosition();
        else return;
        m_WatchScrollbar->setValue(NewPos);
        UpdateWatchList();
    });
    connect(m_Results, &QTableWidget::cellDoubleClicked, this, [this](int Row, int) {
        // Copy result to watch list
        QTableWidgetItem * Item = m_Results->item(Row, 0);
        if (Item == nullptr)
        {
            return;
        }
        AddResultToWatchList(Item->data(Qt::UserRole).toInt());
        UpdateWatchList(true);
    });
    connect(m_Results, &QWidget::customContextMenuRequested, this, [this](const QPoint & Pos) { ResultsContextMenu(m_Results->viewport()->mapToGlobal(Pos)); });
    connect(m_Watch, &QTableWidget::cellDoubleClicked, this, [this](int Row, int Column) { WatchDoubleClicked(Row, Column); });
    connect(m_Watch, &QWidget::customContextMenuRequested, this, [this](const QPoint & Pos) { WatchContextMenu(m_Watch->viewport()->mapToGlobal(Pos)); });
    m_Results->viewport()->installEventFilter(this);
    m_Watch->viewport()->installEventFilter(this);
    m_ResultsScrollbar->installEventFilter(this);
    m_WatchScrollbar->installEventFilter(this);

    UpdateResultsList(true);
    UpdateWatchList();
    LoadWindowPos();
    m_Timer = new QTimer(this);
    connect(m_Timer, &QTimer::timeout, this, [this]() {
        if (isVisible())
        {
            RefreshResultsListValues();
            RefreshWatchListValues();
        }
    });
    m_Timer->start(100);
    GameReset();
}

MemorySearchView::~MemorySearchView()
{
    for (CScanResult & Result : m_WatchList)
    {
        Result.DeleteDescription();
    }
}

void MemorySearchView::showEvent(QShowEvent * Event)
{
    DebugWindow::showEvent(Event);
    UpdateResultsList(true, false);
    UpdateWatchList(true);
}

void MemorySearchView::resizeEvent(QResizeEvent * Event)
{
    DebugWindow::resizeEvent(Event);
    UpdateResultsList(true, false);
    UpdateWatchList(true);
}

// The wheel scrolls the list under the cursor (the upstream message hook)
bool MemorySearchView::eventFilter(QObject * Watched, QEvent * Event)
{
    if (Event->type() == QEvent::Wheel)
    {
        QWheelEvent * Wheel = static_cast<QWheelEvent *>(Event);
        const int Scroll = -(Wheel->angleDelta().y() / 120);
        if (Watched == m_Results->viewport() || Watched == m_ResultsScrollbar)
        {
            m_ResultsScrollbar->setValue(std::max(m_ResultsScrollbar->minimum(), std::min(m_ResultsScrollbar->maximum(), m_ResultsScrollbar->value() + Scroll)));
            UpdateResultsList();
            return true;
        }
        if (Watched == m_Watch->viewport() || Watched == m_WatchScrollbar)
        {
            m_WatchScrollbar->setValue(std::max(m_WatchScrollbar->minimum(), std::min(m_WatchScrollbar->maximum(), m_WatchScrollbar->value() + Scroll)));
            UpdateWatchList();
            return true;
        }
    }
    return DebugWindow::eventFilter(Watched, Event);
}

int MemorySearchView::NumVisibleRows(QTableWidget * Table) const
{
    return std::max(0, Table->viewport()->height() / Table->verticalHeader()->defaultSectionSize());
}

// GameReset: the watch list belongs to the game
void MemorySearchView::GameReset()
{
    ReloadWatchList();
}

void MemorySearchView::ReloadWatchList()
{
    const stdstr Game = g_Settings->LoadStringVal(Game_GameName);
    FlushWatchList();
    if (m_StrGame != Game)
    {
        m_StrGame = Game;
        LoadWatchList();
    }
}

void MemorySearchView::SetComboBoxSelByData(QComboBox * Combo, int Data)
{
    for (int i = 0; i < Combo->count(); i++)
    {
        if (Combo->itemData(i).toInt() == Data)
        {
            Combo->setCurrentIndex(i);
            break;
        }
    }
}

void MemorySearchView::SetScanType(int SearchTypeValue)
{
    SetComboBoxSelByData(m_SearchTypeOptions, SearchTypeValue);
    ScanTypeChanged();
}

void MemorySearchView::SetValueTypeByData(int ValueTypeValue)
{
    SetComboBoxSelByData(m_ValueTypeOptions, ValueTypeValue);
    ValueTypeChanged();
}

// The pause/resume pair is skipped while the CPU waits in the debugger (see MemoryDumpView)
void MemorySearchView::PauseCPU()
{
    if (g_BaseSystem != nullptr && !g_DebugSettings.waitingForStep)
    {
        g_BaseSystem->ExternalEvent(SysEvent_PauseCPU_SearchMemory);
    }
}

void MemorySearchView::ResumeCPU()
{
    if (g_BaseSystem != nullptr && !g_DebugSettings.waitingForStep)
    {
        g_BaseSystem->ExternalEvent(SysEvent_ResumeCPU_SearchMemory);
    }
}

void MemorySearchView::DoSearch()
{
    PauseCPU();
    Search();
    // Emulator won't resume sometimes unless there's a sleep() here (upstream)
    QThread::msleep(50);
    ResumeCPU();
}

void MemorySearchView::DoReset()
{
    m_ResultsGroup->setTitle(Tr("Results"));
    m_MemoryScanner.Reset();
    UpdateResultsList(true);
    UpdateOptions();
}

// OnScanTypeChanged
void MemorySearchView::ScanTypeChanged()
{
    if (m_SearchTypeOptions->currentIndex() < 0)
    {
        return;
    }
    const ::SearchType ScanType = (::SearchType)m_SearchTypeOptions->currentData().toInt();
    const bool DidFirstScan = m_MemoryScanner.DidFirstScan();
    if (ScanType == SearchType_JalTo)
    {
        m_JalSelected = true;
        m_SearchValue->setEnabled(true);
        SetComboBoxSelByData(m_ValueTypeOptions, ValueType_int32);
        m_ValueTypeOptions->setEnabled(false);
        // Remember checkbox states
        m_JalHexWasChecked = m_HexCheckbox->isChecked();
        m_JalUnsignedWasChecked = m_UnsignedCheckbox->isChecked();
        m_SearchValue->SetDisplayFormat(DisplayHex);
        m_HexCheckbox->setChecked(true);
        m_HexCheckbox->setEnabled(false);
        m_UnsignedCheckbox->setChecked(true);
        m_UnsignedCheckbox->setEnabled(false);
    }
    else if (m_JalSelected)
    {
        m_JalSelected = false;
        m_SearchValue->SetDisplayFormat(m_JalHexWasChecked ? DisplayHex : DisplayDefault);
        m_HexCheckbox->setChecked(m_JalHexWasChecked);
        m_HexCheckbox->setEnabled(true);
        m_UnsignedCheckbox->setChecked(m_JalUnsignedWasChecked);
        m_UnsignedCheckbox->setEnabled(true);
    }
    switch (ScanType)
    {
    case SearchType_JalTo:
        break;
    case SearchType_UnknownValue:
    case SearchType_IncreasedValue:
    case SearchType_DecreasedValue:
    case SearchType_ChangedValue:
    case SearchType_UnchangedValue:
        m_ValueTypeOptions->setEnabled(!DidFirstScan);
        m_SearchValue->setEnabled(false);
        break;
    default:
        m_ValueTypeOptions->setEnabled(!DidFirstScan);
        m_SearchValue->setEnabled(true);
        break;
    }
}

// OnValueTypeChanged
void MemorySearchView::ValueTypeChanged()
{
    if (m_ValueTypeOptions->currentIndex() < 0)
    {
        return;
    }
    const ::ValueType Type = (::ValueType)m_ValueTypeOptions->currentData().toInt();
    if (Type == ValueType_string || Type == ValueType_istring || Type == ValueType_unkstring)
    {
        m_SearchValue->setEnabled(true);
        SetComboBoxSelByData(m_SearchTypeOptions, SearchType_ExactValue);
        m_SearchTypeOptions->setEnabled(false);
        m_UnkEncodingCheckbox->show();
        m_IgnoreCaseCheckbox->show();
        m_UnsignedCheckbox->hide();
    }
    else
    {
        const bool ShowUnsigned = (Type != ValueType_float && Type != ValueType_double);
        m_UnsignedCheckbox->setVisible(ShowUnsigned);
        m_UnkEncodingCheckbox->hide();
        m_IgnoreCaseCheckbox->hide();
        m_SearchTypeOptions->setEnabled(true);
    }
}

void MemorySearchView::UpdateOptions()
{
    const bool DidFirstScan = m_MemoryScanner.DidFirstScan();
    const bool HaveResults = (m_MemoryScanner.GetNumResults() > 0);
    m_ValueTypeOptions->setEnabled(!DidFirstScan);
    m_ResetButton->setEnabled(DidFirstScan);
    m_RdramButton->setEnabled(!DidFirstScan);
    m_RomButton->setEnabled(!DidFirstScan);
    m_SpmemButton->setEnabled(!DidFirstScan);
    m_PhysicalCheckbox->setEnabled(!DidFirstScan);
    m_AddrStart->setEnabled(!DidFirstScan);
    m_AddrEnd->setEnabled(!DidFirstScan);
    m_UnsignedCheckbox->setEnabled(!DidFirstScan);
    m_IgnoreCaseCheckbox->setEnabled(!DidFirstScan);
    m_UnkEncodingCheckbox->setEnabled(!DidFirstScan);
    const ::SearchType ScanType = m_SearchTypeOptions->currentIndex() >= 0 ? (::SearchType)m_SearchTypeOptions->currentData().toInt() : SearchType_ExactValue;
    const ::ValueType Type = m_ValueTypeOptions->currentIndex() >= 0 ? (::ValueType)m_ValueTypeOptions->currentData().toInt() : ValueType_int8;
    QComboBox * Combo = m_SearchTypeOptions;
    if (!DidFirstScan)
    {
        m_SearchTypeOptions->setEnabled(true);
        Combo->clear();
        Combo->addItem(Tr("Exact value"), (int)SearchType_ExactValue);
        Combo->addItem(Tr("Unknown initial value"), (int)SearchType_UnknownValue);
        Combo->addItem(Tr("Greater than..."), (int)SearchType_GreaterThanValue);
        Combo->addItem(Tr("Less than..."), (int)SearchType_LessThanValue);
        Combo->addItem(Tr("JAL to..."), (int)SearchType_JalTo);
        Combo->setCurrentIndex(0);
        if (Type == ValueType_string)
        {
            m_SearchTypeOptions->setEnabled(false);
        }
        m_SearchValue->setEnabled(true);
        m_SearchButton->setEnabled(true);
    }
    else
    {
        Combo->clear();
        Combo->addItem(Tr("Exact value"), (int)SearchType_ExactValue);
        Combo->addItem(Tr("Changed value"), (int)SearchType_ChangedValue);
        Combo->addItem(Tr("Unchanged value"), (int)SearchType_UnchangedValue);
        Combo->addItem(Tr("Greater than..."), (int)SearchType_GreaterThanValue);
        Combo->addItem(Tr("Less than..."), (int)SearchType_LessThanValue);
        Combo->addItem(Tr("Increased value"), (int)SearchType_IncreasedValue);
        Combo->addItem(Tr("Decreased value"), (int)SearchType_DecreasedValue);
        Combo->setCurrentIndex(0);
        if (m_JalSelected)
        {
            m_HexCheckbox->setChecked(m_JalHexWasChecked);
            m_UnsignedCheckbox->setChecked(m_JalUnsignedWasChecked);
            m_JalSelected = false;
            m_JalHexWasChecked = false;
            m_JalUnsignedWasChecked = false;
        }
        m_HexCheckbox->setEnabled(true);
        m_UnsignedCheckbox->setEnabled(true);
        m_SearchValue->setEnabled(true);
        if (ScanType == SearchType_JalTo || Type == ValueType_string || Type == ValueType_istring || Type == ValueType_unkstring)
        {
            // Complex search types, disable next search
            m_SearchButton->setEnabled(false);
            m_SearchTypeOptions->setEnabled(false);
        }
        else
        {
            m_UnsignedCheckbox->show();
            m_SearchButton->setEnabled(HaveResults);
            m_SearchTypeOptions->setEnabled(HaveResults);
        }
    }
}

// Search
void MemorySearchView::Search()
{
    ::ValueType Type = (::ValueType)m_ValueTypeOptions->currentData().toInt();
    const ::SearchType ScanType = (::SearchType)m_SearchTypeOptions->currentData().toInt();
    const uint32_t StartAddress = m_AddrStart->Value();
    const uint32_t EndAddress = m_AddrEnd->Value();
    const bool HexChecked = m_HexCheckbox->isChecked();
    const bool PhysicalChecked = m_PhysicalCheckbox->isChecked();
    MixedValue Value;
    int StringValueLength = 0;
    bool UseSearchValue;
    auto ParseError = [this]() { QMessageBox::warning(this, Tr("Invalid value"), Tr("Invalid value")); };

    if (Type == ValueType_string)
    {
        if (m_UnkEncodingCheckbox->isChecked())
        {
            Type = ValueType_unkstring;
        }
        else if (m_IgnoreCaseCheckbox->isChecked())
        {
            Type = ValueType_istring;
        }
    }
    else if (m_UnsignedCheckbox->isChecked())
    {
        switch (Type)
        {
        case ValueType_int8: Type = ValueType_uint8; break;
        case ValueType_int16: Type = ValueType_uint16; break;
        case ValueType_int32: Type = ValueType_uint32; break;
        case ValueType_int64: Type = ValueType_uint64; break;
        default: break;
        }
    }
    switch (ScanType)
    {
    case SearchType_UnknownValue:
    case SearchType_ChangedValue:
    case SearchType_UnchangedValue:
    case SearchType_IncreasedValue:
    case SearchType_DecreasedValue:
        UseSearchValue = false;
        break;
    default:
        UseSearchValue = true;
        m_SearchValue->SetType(Type);
        break;
    }
    m_MemoryScanner.SetSearchType(ScanType);
    m_MemoryScanner.SetValueType(Type);
    if (UseSearchValue)
    {
        switch (Type)
        {
        case ValueType_uint8:
            if (!m_SearchValue->GetValue(Value._uint8)) return ParseError();
            m_MemoryScanner.SetValue<uint8_t>(Value._uint8);
            break;
        case ValueType_int8:
            if (!m_SearchValue->GetValue(Value._sint8)) return ParseError();
            m_MemoryScanner.SetValue<int8_t>(Value._sint8);
            break;
        case ValueType_uint16:
            if (!m_SearchValue->GetValue(Value._uint16)) return ParseError();
            m_MemoryScanner.SetValue<uint16_t>(Value._uint16);
            break;
        case ValueType_int16:
            if (!m_SearchValue->GetValue(Value._sint16)) return ParseError();
            m_MemoryScanner.SetValue<int16_t>(Value._sint16);
            break;
        case ValueType_uint32:
            if (!m_SearchValue->GetValue(Value._uint32)) return ParseError();
            m_MemoryScanner.SetValue<uint32_t>(Value._uint32);
            break;
        case ValueType_int32:
            if (!m_SearchValue->GetValue(Value._sint32)) return ParseError();
            m_MemoryScanner.SetValue<int32_t>(Value._sint32);
            break;
        case ValueType_uint64:
            if (!m_SearchValue->GetValue(Value._uint64)) return ParseError();
            m_MemoryScanner.SetValue<uint64_t>(Value._uint64);
            break;
        case ValueType_int64:
            if (!m_SearchValue->GetValue(Value._sint64)) return ParseError();
            m_MemoryScanner.SetValue<int64_t>(Value._sint64);
            break;
        case ValueType_float:
            if (!m_SearchValue->GetValue(Value._float)) return ParseError();
            m_MemoryScanner.SetValue<float>(Value._float);
            break;
        case ValueType_double:
            if (!m_SearchValue->GetValue(Value._double)) return ParseError();
            m_MemoryScanner.SetValue<double>(Value._double);
            break;
        case ValueType_string:
            if (HexChecked)
            {
                if (!m_SearchValue->GetValueHexString(Value._string, StringValueLength)) return ParseError();
            }
            else
            {
                if (!m_SearchValue->GetValueString(Value._string, StringValueLength)) return ParseError();
            }
            m_MemoryScanner.SetValue<const wchar_t *>(Value._string);
            m_MemoryScanner.SetStringValueLength(StringValueLength);
            break;
        case ValueType_unkstring:
        case ValueType_istring:
            if (!m_SearchValue->GetValueString(Value._string, StringValueLength)) return ParseError();
            m_MemoryScanner.SetValue<const wchar_t *>(Value._string);
            m_MemoryScanner.SetStringValueLength(StringValueLength);
            break;
        default:
            QMessageBox::warning(this, Tr("Unimplemented"), Tr("Unimplemented value type"));
            return;
        }
    }
    if (!m_MemoryScanner.DidFirstScan())
    {
        m_MemoryScanner.SetAddressType(PhysicalChecked ? AddressType_Physical : AddressType_Virtual);
        const bool AddressRangeValid = m_MemoryScanner.SetAddressRange(StartAddress, EndAddress);
        if (!AddressRangeValid)
        {
            QMessageBox::warning(this, Tr("Invalid address range"), Tr("Invalid address range"));
            return;
        }
        m_MemoryScanner.FirstScan(m_SearchValue->GetDisplayFormat());
        UpdateOptions();
    }
    else
    {
        m_MemoryScanner.NextScan();
        if (m_MemoryScanner.GetNumResults() == 0)
        {
            UpdateOptions();
        }
    }
    UpdateResultsList(true);
}

std::vector<int> MemorySearchView::SelectedRows(QTableWidget * Table) const
{
    std::vector<int> Rows;
    for (int Row = 0; Row < Table->rowCount(); Row++)
    {
        QTableWidgetItem * Item = Table->item(Row, 0);
        if (Item != nullptr && Item->isSelected())
        {
            Rows.push_back(Row);
        }
    }
    return Rows;
}

CScanResult * MemorySearchView::FirstSelectedScanResult()
{
    const std::vector<int> Rows = SelectedRows(m_Results);
    if (Rows.empty())
    {
        return nullptr;
    }
    return m_MemoryScanner.GetResult(m_Results->item(Rows[0], 0)->data(Qt::UserRole).toInt());
}

CScanResult * MemorySearchView::FirstSelectedWatchListResult()
{
    const std::vector<int> Rows = SelectedRows(m_Watch);
    if (Rows.empty())
    {
        return nullptr;
    }
    return &m_WatchList[m_Watch->item(Rows[0], 0)->data(Qt::UserRole).toInt()];
}

void MemorySearchView::UpdateResultsList(bool UpdateScrollbar, bool ResetScrollPos)
{
    const size_t NumResults = m_MemoryScanner.GetNumResults();
    const size_t NumVisible = (size_t)NumVisibleRows(m_Results);
    if (UpdateScrollbar)
    {
        const bool CanDisplayAll = (NumVisible >= NumResults);
        const int ScrollRangeMax = CanDisplayAll ? 0 : (int)(NumResults - NumVisible);
        m_ResultsScrollbar->setEnabled(!CanDisplayAll);
        m_ResultsScrollbar->setRange(0, ScrollRangeMax);
        if (ResetScrollPos)
        {
            m_ResultsScrollbar->setValue(0);
        }
    }
    const size_t Start = (size_t)m_ResultsScrollbar->value();
    size_t End = Start + NumVisible;
    if (End > NumResults)
    {
        End = NumResults;
    }
    m_Results->setUpdatesEnabled(false);
    m_Results->clearContents();
    m_Results->setRowCount((int)(End > Start ? End - Start : 0));
    int Item = 0;
    for (size_t Index = Start; Index < End; Index++)
    {
        char Address[32];
        char CurrentValue[1024];
        char Value[1024];
        CScanResult * Result = m_MemoryScanner.GetResult(Index);
        Result->GetAddressString(Address);
        Result->GetMemoryValueString(CurrentValue, 1024);
        Result->GetValueString(Value, 1024);
        QTableWidgetItem * AddressItem = new QTableWidgetItem(QString::fromLatin1(Address));
        AddressItem->setData(Qt::UserRole, (int)Index);
        if (Result->m_AddressType == AddressType_Physical)
        {
            AddressItem->setForeground(QColor(0x44, 0x88, 0x44)); // Green if address is physical
        }
        m_Results->setItem(Item, Results_Col_Address, AddressItem);
        QTableWidgetItem * ValueItem = new QTableWidgetItem(QString::fromLatin1(CurrentValue));
        if (Result->IsStringType())
        {
            if (Result->m_DisplayFormat == DisplayHex)
            {
                ValueItem->setForeground(QColor(0, 0, 255)); // Blue if hex string
            }
        }
        else if (strcmp(CurrentValue, Value) != 0)
        {
            ValueItem->setForeground(QColor(255, 0, 0)); // Red if value has changed
        }
        m_Results->setItem(Item, Results_Col_Value, ValueItem);
        m_Results->setItem(Item, Results_Col_Previous, new QTableWidgetItem(QString::fromLatin1(Value)));
        Item++;
    }
    m_Results->setUpdatesEnabled(true);
    m_ResultsGroup->setTitle(Tr("Results (%1)").arg((int)NumResults));
}

void MemorySearchView::UpdateWatchList(bool UpdateScrollbar)
{
    const size_t NumEntries = m_WatchList.size();
    const size_t NumVisible = (size_t)NumVisibleRows(m_Watch);
    if (UpdateScrollbar)
    {
        const bool CanDisplayAll = (NumVisible >= NumEntries);
        const int ScrollRangeMax = CanDisplayAll ? 0 : (int)(NumEntries - NumVisible);
        m_WatchScrollbar->setRange(0, ScrollRangeMax);
        m_WatchScrollbar->setValue(0);
        m_WatchScrollbar->setEnabled(!CanDisplayAll);
    }
    const size_t Start = (size_t)m_WatchScrollbar->value();
    size_t End = Start + NumVisible;
    if (End > NumEntries)
    {
        End = NumEntries;
    }
    m_Watch->setUpdatesEnabled(false);
    m_Watch->clearContents();
    m_Watch->setRowCount((int)(End > Start ? End - Start : 0));
    int Item = 0;
    for (size_t Index = Start; Index < End; Index++)
    {
        CScanResult * Result = &m_WatchList[Index];
        char Address[16];
        char Value[1024];
        char ValueTypeText[32];
        const char * TypeName;
        Result->GetAddressString(Address);
        Result->GetMemoryValueString(Value, 1024);
        TypeName = Result->GetTypeName();
        const char * Description = Result->GetDescription();
        switch (Result->GetType())
        {
        case ValueType_string:
        case ValueType_istring:
        case ValueType_unkstring:
            sprintf(ValueTypeText, "%s[%d]", TypeName, Result->GetStrLength());
            TypeName = ValueTypeText;
            break;
        default:
            break;
        }
        QTableWidgetItem * LockItem = new QTableWidgetItem("");
        LockItem->setData(Qt::UserRole, (int)Index);
        m_Watch->setItem(Item, WatchList_Col_Lock, LockItem);
        m_Watch->setItem(Item, WatchList_Col_BP, new QTableWidgetItem(""));
        QTableWidgetItem * AddressItem = new QTableWidgetItem(QString::fromLatin1(Address));
        if (Result->m_AddressType == AddressType_Physical)
        {
            AddressItem->setForeground(QColor(0x44, 0x88, 0x44));
        }
        m_Watch->setItem(Item, WatchList_Col_Address, AddressItem);
        m_Watch->setItem(Item, WatchList_Col_Description, new QTableWidgetItem(QString::fromUtf8(Description)));
        m_Watch->setItem(Item, WatchList_Col_Type, new QTableWidgetItem(QString::fromLatin1(TypeName)));
        QTableWidgetItem * ValueItem = new QTableWidgetItem(QString::fromLatin1(Value));
        if (Result->IsStringType() && Result->m_DisplayFormat == DisplayHex)
        {
            ValueItem->setForeground(QColor(0, 0, 255));
        }
        m_Watch->setItem(Item, WatchList_Col_Value, ValueItem);
        Item++;
    }
    m_Watch->setUpdatesEnabled(true);
}

void MemorySearchView::RefreshResultsListValues()
{
    if (!g_MMU)
    {
        return;
    }
    const int NumShown = m_Results->rowCount();
    if (NumShown > 0)
    {
        m_Results->setUpdatesEnabled(false);
        for (int Item = 0; Item < NumShown; Item++)
        {
            QTableWidgetItem * First = m_Results->item(Item, 0);
            if (First == nullptr)
            {
                continue;
            }
            CScanResult * Result = m_MemoryScanner.GetResult((size_t)First->data(Qt::UserRole).toInt());
            if (Result == nullptr)
            {
                g_Notify->BreakPoint(__FILE__, __LINE__);
                break;
            }
            char CurrentValue[1024];
            Result->GetMemoryValueString(CurrentValue, 1024);
            QTableWidgetItem * ValueItem = m_Results->item(Item, Results_Col_Value);
            if (ValueItem != nullptr && ValueItem->text() != QString::fromLatin1(CurrentValue))
            {
                ValueItem->setText(QString::fromLatin1(CurrentValue));
            }
        }
        m_Results->setUpdatesEnabled(true);
    }
}

void MemorySearchView::RefreshWatchListValues()
{
    if (!g_MMU)
    {
        return;
    }
    const int NumShown = m_Watch->rowCount();
    if (NumShown > 0)
    {
        m_Watch->setUpdatesEnabled(false);
        CBreakpoints * Breakpoints = m_Debugger->Breakpoints();
        for (int Item = 0; Item < NumShown; Item++)
        {
            QTableWidgetItem * First = m_Watch->item(Item, 0);
            if (First == nullptr)
            {
                continue;
            }
            const size_t Index = (size_t)First->data(Qt::UserRole).toInt();
            if (Index >= m_WatchList.size())
            {
                continue;
            }
            CScanResult * Result = &m_WatchList[Index];
            const bool HaveLock = Breakpoints->MemLockExists(Result->m_Address, 1);
            const bool HaveReadBP = (Breakpoints->ReadBPExists8(Result->m_Address) != CBreakpoints::BPSTATE::BP_NOT_SET);
            const bool HaveWriteBP = (Breakpoints->WriteBPExists8(Result->m_Address) != CBreakpoints::BPSTATE::BP_NOT_SET);
            const bool HaveExecBP = (Breakpoints->ExecutionBPExists(Result->m_Address) != CBreakpoints::BPSTATE::BP_NOT_SET);
            const QString BPStates = QString("%1%2%3").arg(HaveReadBP ? "R" : "").arg(HaveWriteBP ? "W" : "").arg(HaveExecBP ? "E" : "");
            char CurrentValue[1024];
            Result->GetMemoryValueString(CurrentValue, 1024);
            auto Set = [this, Item](int Column, const QString & Text) {
                QTableWidgetItem * Cell = m_Watch->item(Item, Column);
                if (Cell != nullptr && Cell->text() != Text)
                {
                    Cell->setText(Text);
                }
            };
            Set(WatchList_Col_Lock, HaveLock ? "X" : "");
            Set(WatchList_Col_BP, BPStates);
            Set(WatchList_Col_Value, QString::fromLatin1(CurrentValue));
        }
        m_Watch->setUpdatesEnabled(true);
    }
}

void MemorySearchView::AddResultToWatchList(int ResultIndex)
{
    CScanResult Result = *m_MemoryScanner.GetResult(ResultIndex);
    m_WatchList.push_back(Result);
}

void MemorySearchView::ClearWatchList()
{
    for (CScanResult & Result : m_WatchList)
    {
        Result.DeleteDescription();
    }
    m_WatchList.clear();
    UpdateWatchList(true);
}

void MemorySearchView::RemoveWatchListItem(int Index)
{
    m_WatchList[Index].DeleteDescription();
    m_WatchList.erase(m_WatchList.begin() + Index);
}

void MemorySearchView::RemoveSelectedWatchListItems()
{
    const std::vector<int> Rows = SelectedRows(m_Watch);
    for (size_t i = Rows.size(); i-- > 0;)
    {
        RemoveWatchListItem(m_Watch->item(Rows[i], 0)->data(Qt::UserRole).toInt());
    }
    UpdateWatchList();
}

// OnResultsRClick: the IDR_MEM_SEARCH menu
void MemorySearchView::ResultsContextMenu(const QPoint & GlobalPos)
{
    if (SelectedRows(m_Results).empty())
    {
        return;
    }
    QMenu Menu(this);
    Menu.addAction(Tr("Add to watch list"), [this]() {
        for (int Row : SelectedRows(m_Results))
        {
            AddResultToWatchList(m_Results->item(Row, 0)->data(Qt::UserRole).toInt());
        }
        UpdateWatchList(true);
    });
    Menu.addAction(Tr("Add all to watch list"), [this]() {
        const size_t NumResults = m_MemoryScanner.GetNumResults();
        for (size_t i = 0; i < NumResults; i++)
        {
            AddResultToWatchList((int)i);
        }
        UpdateWatchList(true);
    });
    Menu.addAction(Tr("Change value"), [this]() {
        CScanResult * First = FirstSelectedScanResult();
        if (First == nullptr)
        {
            return;
        }
        char InitialValue[1024];
        First->GetMemoryValueString(InitialValue, 1024);
        std::string Entered;
        if (DebugDialogs::SetValueText(this, Tr("Change value"), Tr("New value:"), InitialValue, Entered))
        {
            for (int Row : SelectedRows(m_Results))
            {
                CScanResult * Result = m_MemoryScanner.GetResult(m_Results->item(Row, 0)->data(Qt::UserRole).toInt());
                Result->SetMemoryValueFromString(Entered.c_str());
                m_Results->item(Row, Results_Col_Value)->setText(QString::fromStdString(Entered));
            }
        }
    });
    Menu.addAction(Tr("&View memory..."), [this]() {
        CScanResult * Result = FirstSelectedScanResult();
        if (Result == nullptr)
        {
            return;
        }
        m_Debugger->Debug_ShowMemoryLocation(Result->m_Address, Result->m_AddressType == AddressType_Virtual);
    });
    Menu.addSeparator();
    Menu.addAction(Tr("Remove selected"), [this]() {
        const std::vector<int> Rows = SelectedRows(m_Results);
        for (size_t i = Rows.size(); i-- > 0;)
        {
            m_MemoryScanner.RemoveResult((size_t)m_Results->item(Rows[i], 0)->data(Qt::UserRole).toInt());
        }
        UpdateResultsList(true);
    });
    Menu.exec(GlobalPos);
}

// OnWatchListDblClick: the column picks the action
void MemorySearchView::WatchDoubleClicked(int Row, int Column)
{
    QTableWidgetItem * First = m_Watch->item(Row, 0);
    if (First == nullptr)
    {
        return;
    }
    const int Index = First->data(Qt::UserRole).toInt();
    CScanResult * Result = &m_WatchList[Index];
    std::string Entered;
    switch (Column)
    {
    case WatchList_Col_Lock:
        m_Debugger->Breakpoints()->ToggleMemLock(Result->m_Address);
        break;
    case WatchList_Col_Address:
        if (DebugDialogs::SetValueText(this, Tr("Change address"), Tr("New address:"), stdstr_f("0x%08X", Result->m_Address), Entered))
        {
            const uint32_t NewAddr = strtoul(Entered.c_str(), nullptr, 0);
            if (Result->SetAddressSafe(NewAddr))
            {
                m_Watch->item(Row, WatchList_Col_Address)->setText(QString("0x%1").arg(Hex8(NewAddr)));
            }
        }
        break;
    case WatchList_Col_Value:
    {
        char InitialValue[1024];
        Result->GetMemoryValueString(InitialValue, 1024);
        if (DebugDialogs::SetValueText(this, Tr("Change value"), Tr("New value:"), InitialValue, Entered))
        {
            Result->SetMemoryValueFromString(Entered.c_str());
        }
        break;
    }
    case WatchList_Col_Description:
        if (DebugDialogs::SetValueText(this, Tr("Set description"), Tr("New description:"), Result->GetDescription(), Entered))
        {
            Result->SetDescription(Entered.c_str());
            m_Watch->item(Row, WatchList_Col_Description)->setText(QString::fromStdString(Entered));
        }
        break;
    case WatchList_Col_Type:
    {
        DebugDialogs::ComboItem Items[sizeof(ModalChangeTypeItems) / sizeof(ModalChangeTypeItems[0])];
        for (size_t i = 0; i < sizeof(ModalChangeTypeItems) / sizeof(ModalChangeTypeItems[0]); i++)
        {
            Items[i] = {ModalChangeTypeItems[i].text, ModalChangeTypeItems[i].data};
        }
        uintptr_t Data;
        if (DebugDialogs::SetValueCombo(this, Tr("Change type"), Tr("New type:"), Result->GetType(), Items, Data))
        {
            const ::ValueType Type = (::ValueType)Data;
            Result->SetType(Type);
            if (Type == ValueType_string)
            {
                if (DebugDialogs::SetValueText(this, Tr("String length"), Tr("New string length:"), stdstr_f("%d", Result->GetStrLength()), Entered))
                {
                    Result->SetStrLengthSafe(atoi(Entered.c_str()));
                }
                m_Watch->item(Row, WatchList_Col_Type)->setText(QString("char[%1]").arg(Result->GetStrLength()));
            }
            else
            {
                m_Watch->item(Row, WatchList_Col_Type)->setText(QString::fromLatin1(Result->GetTypeName()));
            }
        }
        break;
    }
    }
}

// OnWatchListRClick: the IDR_MEM_WATCHLIST menu
void MemorySearchView::WatchContextMenu(const QPoint & GlobalPos)
{
    if (SelectedRows(m_Watch).empty())
    {
        return;
    }
    CScanResult * Result = FirstSelectedWatchListResult();
    CBreakpoints * Breakpoints = m_Debugger->Breakpoints();
    const bool HaveLock = Breakpoints->MemLockExists(Result->m_Address, 1);
    const bool HaveReadBP = (Breakpoints->ReadBPExists8(Result->m_Address) != CBreakpoints::BPSTATE::BP_NOT_SET);
    const bool HaveWriteBP = (Breakpoints->WriteBPExists8(Result->m_Address) != CBreakpoints::BPSTATE::BP_NOT_SET);
    const bool Hex = (Result->m_DisplayFormat == DisplayHex);

    auto ForSelected = [this](std::function<void(int Row, CScanResult * Result)> Function) {
        for (int Row : SelectedRows(m_Watch))
        {
            Function(Row, &m_WatchList[m_Watch->item(Row, 0)->data(Qt::UserRole).toInt()]);
        }
    };
    auto Checked = [](QAction * Action, bool On) {
        Action->setCheckable(true);
        Action->setChecked(On);
    };

    QMenu Menu(this);
    QMenu * Change = Menu.addMenu(Tr("Change"));
    Change->addAction(Tr("Value"), [this, ForSelected]() {
        CScanResult * First = FirstSelectedWatchListResult();
        if (First == nullptr)
        {
            return;
        }
        char Placeholder[1024];
        First->GetMemoryValueString(Placeholder, 1024);
        std::string Entered;
        if (DebugDialogs::SetValueText(this, Tr("Change value"), Tr("New value:"), Placeholder, Entered))
        {
            ForSelected([&](int Row, CScanResult * R) {
                // TODO (upstream): prompt for size change if string is too long
                R->SetMemoryValueFromString(Entered.c_str());
                m_Watch->item(Row, WatchList_Col_Value)->setText(QString::fromStdString(Entered));
            });
        }
    });
    Change->addAction(Tr("Description"), [this, ForSelected]() {
        CScanResult * First = FirstSelectedWatchListResult();
        if (First == nullptr)
        {
            return;
        }
        std::string Entered;
        if (DebugDialogs::SetValueText(this, Tr("Change description"), Tr("New description:"), First->GetDescription(), Entered))
        {
            ForSelected([&](int Row, CScanResult * R) {
                R->SetDescription(Entered.c_str());
                m_Watch->item(Row, WatchList_Col_Description)->setText(QString::fromStdString(Entered));
            });
        }
    });
    Change->addAction(Tr("Type"), [this, ForSelected]() {
        CScanResult * First = FirstSelectedWatchListResult();
        if (First == nullptr)
        {
            return;
        }
        DebugDialogs::ComboItem Items[sizeof(ModalChangeTypeItems) / sizeof(ModalChangeTypeItems[0])];
        for (size_t i = 0; i < sizeof(ModalChangeTypeItems) / sizeof(ModalChangeTypeItems[0]); i++)
        {
            Items[i] = {ModalChangeTypeItems[i].text, ModalChangeTypeItems[i].data};
        }
        uintptr_t Data;
        if (DebugDialogs::SetValueCombo(this, Tr("Change type"), Tr("New type:"), First->GetType(), Items, Data))
        {
            const ::ValueType Type = (::ValueType)Data;
            int Length = 0;
            if (Type == ValueType_string)
            {
                std::string Entered;
                if (DebugDialogs::SetValueText(this, Tr("String length"), Tr("New string length:"), stdstr_f("%d", First->GetStrLength()), Entered))
                {
                    Length = atoi(Entered.c_str());
                    if (Length <= 0)
                    {
                        Length = 1;
                    }
                }
            }
            // upstream only updates the labels here and never applies the type (the double click
            // path does); the type is applied to the selected entries
            ForSelected([&](int Row, CScanResult * R) {
                R->SetType(Type);
                if (R->IsStringType())
                {
                    R->SetStrLengthSafe(Length);
                    m_Watch->item(Row, WatchList_Col_Type)->setText(QString("char[%1]").arg(R->GetStrLength()));
                }
                else
                {
                    m_Watch->item(Row, WatchList_Col_Type)->setText(QString::fromLatin1(R->GetTypeName()));
                }
            });
        }
    });
    Change->addAction(Tr("Address"), [this, ForSelected]() {
        CScanResult * First = FirstSelectedWatchListResult();
        if (First == nullptr)
        {
            return;
        }
        std::string Entered;
        if (DebugDialogs::SetValueText(this, Tr("Change address"), Tr("New address:"), stdstr_f("0x%08X", First->m_Address), Entered))
        {
            const uint32_t NewAddr = strtoul(Entered.c_str(), nullptr, 0);
            const QString NewAddrStr = QString("0x%1").arg(Hex8(NewAddr));
            ForSelected([&](int Row, CScanResult * R) {
                R->SetAddressSafe(NewAddr);
                m_Watch->item(Row, WatchList_Col_Address)->setText(NewAddrStr);
            });
        }
    });
    Change->addAction(Tr("Address by..."), [this, ForSelected]() {
        CScanResult * First = FirstSelectedWatchListResult();
        if (First == nullptr)
        {
            return;
        }
        std::string Entered;
        if (DebugDialogs::SetValueText(this, Tr("Adjust address"), Tr("Address offset (+/-):"), "0", Entered))
        {
            const int Offset = atoi(Entered.c_str());
            ForSelected([&](int Row, CScanResult * R) {
                const uint32_t NewAddr = R->m_Address + Offset;
                if (R->SetAddressSafe(NewAddr))
                {
                    m_Watch->item(Row, WatchList_Col_Address)->setText(QString("0x%1").arg(Hex8(R->m_Address)));
                }
            });
        }
    });
    Checked(Menu.addAction(Tr("Lock value"), [this, ForSelected]() { ForSelected([this](int, CScanResult * R) { m_Debugger->Breakpoints()->ToggleMemLock(R->m_Address); }); }), HaveLock);
    Checked(Menu.addAction(Tr("Read breakpoint"), [this]() {
        if (CScanResult * R = FirstSelectedWatchListResult())
        {
            m_Debugger->Breakpoints()->RBPToggle(R->m_Address);
        }
    }),
            HaveReadBP);
    Checked(Menu.addAction(Tr("Write breakpoint"), [this]() {
        if (CScanResult * R = FirstSelectedWatchListResult())
        {
            m_Debugger->Breakpoints()->WBPToggle(R->m_Address);
        }
    }),
            HaveWriteBP);
    Menu.addSeparator();
    Checked(Menu.addAction(Tr("Hexadecimal"), [ForSelected]() {
        ForSelected([](int, CScanResult * R) { R->m_DisplayFormat = (R->m_DisplayFormat == DisplayDefault) ? DisplayHex : DisplayDefault; });
    }),
            Hex);
    Menu.addSeparator();
    Menu.addAction(Tr("View memory..."), [this]() {
        if (CScanResult * R = FirstSelectedWatchListResult())
        {
            m_Debugger->Debug_ShowMemoryLocation(R->m_Address, R->m_AddressType == AddressType_Virtual);
        }
    });
    Menu.addAction(Tr("Add Symbol..."), [this]() {
        CScanResult * R = FirstSelectedWatchListResult();
        if (R == nullptr)
        {
            return;
        }
        // TODO (upstream): fix magic numbers
        int SymType = 1;
        switch (R->GetType())
        {
        case ValueType_uint8: SymType = 2; break;
        case ValueType_uint16: SymType = 3; break;
        case ValueType_uint32: SymType = 4; break;
        case ValueType_uint64: SymType = 5; break;
        case ValueType_int8: SymType = 6; break;
        case ValueType_int16: SymType = 7; break;
        case ValueType_int32: SymType = 8; break;
        case ValueType_int64: SymType = 9; break;
        case ValueType_float: SymType = 10; break;
        case ValueType_double: SymType = 11; break;
        default: break;
        }
        DebugDialogs::AddSymbol(m_Debugger, this, R->GetVirtualAddress(), SymType);
    });
    Menu.addSeparator();
    QMenu * Copy = Menu.addMenu(Tr("Copy"));
    Copy->addAction(Tr("GameShark code"), [this]() {
        const QString Code = WatchGameSharkCode(SelectedRows(m_Watch));
        if (!Code.isEmpty())
        {
            QApplication::clipboard()->setText(Code);
        }
    });
    Copy->addAction(Tr("Address and description"), [this]() {
        stdstr Str = "";
        for (int Row : SelectedRows(m_Watch))
        {
            CScanResult * R = &m_WatchList[m_Watch->item(Row, 0)->data(Qt::UserRole).toInt()];
            Str += stdstr_f("%08X %s\n", R->m_Address, R->GetDescription());
        }
        if (Str.length() != 0)
        {
            // upstream drops the last character (its buffer is one byte short)
            QApplication::clipboard()->setText(QString::fromUtf8(Str.c_str(), (int)Str.length() - 1));
        }
    });
    Menu.addSeparator();
    Menu.addAction(Tr("Remove selected"), [this]() { RemoveSelectedWatchListItems(); });
    Menu.addAction(Tr("Remove all"), [this]() { ClearWatchList(); });
    Menu.exec(GlobalPos);
}

// OnWatchListPopupCopyGamesharkCode
QString MemorySearchView::WatchGameSharkCode(const std::vector<int> & Rows)
{
    stdstr GSCode = "";
    for (int Row : Rows)
    {
        CScanResult * Result = &m_WatchList[m_Watch->item(Row, 0)->data(Qt::UserRole).toInt()];
        const uint32_t VAddr = Result->GetVirtualAddress();
        if (Result->IsStringType())
        {
            const int Length = Result->GetStrLength();
            char Str[1024];
            Result->GetMemoryValueString(Str, 1024, true);
            const bool HaveOddLength = (Length & 1) != 0;
            const int EvenLength = Length & ~1;
            if (Length >= 2)
            {
                for (int j = 0; j < EvenLength; j += 2)
                {
                    GSCode += stdstr_f("%08X %02X%02X\n", (VAddr + j) | GS_TWOBYTE, (uint8_t)Str[j], (uint8_t)Str[j + 1]);
                }
            }
            if (HaveOddLength)
            {
                GSCode += stdstr_f("%08X 00%02X\n", (VAddr + Length - 1), (uint8_t)Str[Length - 1]);
            }
        }
        else
        {
            CMixed Value;
            Result->GetMemoryValue(&Value);
            switch (Result->GetType())
            {
            case ValueType_uint8:
            case ValueType_int8:
                GSCode += stdstr_f("%08X 00%02X\n", VAddr, Value.m_Value._uint8);
                break;
            case ValueType_uint16:
            case ValueType_int16:
                GSCode += stdstr_f("%08X %04X\n", VAddr | GS_TWOBYTE, Value.m_Value._uint16);
                break;
            case ValueType_uint32:
            case ValueType_int32:
            case ValueType_float:
                GSCode += stdstr_f("%08X %04X\n", (VAddr + 0) | GS_TWOBYTE, (uint16_t)(Value.m_Value._uint32 >> 16));
                GSCode += stdstr_f("%08X %04X\n", (VAddr + 2) | GS_TWOBYTE, (uint16_t)(Value.m_Value._uint32));
                break;
            case ValueType_uint64:
            case ValueType_int64:
            case ValueType_double:
                GSCode += stdstr_f("%08X %04X\n", (VAddr + 0) | GS_TWOBYTE, (uint16_t)(Value.m_Value._uint64 >> 48));
                GSCode += stdstr_f("%08X %04X\n", (VAddr + 2) | GS_TWOBYTE, (uint16_t)(Value.m_Value._uint64 >> 32));
                GSCode += stdstr_f("%08X %04X\n", (VAddr + 4) | GS_TWOBYTE, (uint16_t)(Value.m_Value._uint64 >> 16));
                GSCode += stdstr_f("%08X %04X\n", (VAddr + 6) | GS_TWOBYTE, (uint16_t)(Value.m_Value._uint64));
                break;
            default:
                g_Notify->BreakPoint(__FILE__, __LINE__);
                break;
            }
        }
    }
    if (GSCode.length() == 0)
    {
        return QString();
    }
    // upstream copies length-1 characters (drops the final newline)
    return QString::fromLatin1(GSCode.c_str(), (int)GSCode.length() - 1);
}

// The watch list of the game: <NativeSave>/<game>.wlst
void MemorySearchView::FlushWatchList()
{
    if (m_WatchList.size() == 0)
    {
        return;
    }
    CPath WlPath = GetWatchListPath();
    CFile WatchListFile;
    WatchListFile.Open(WlPath, CFileBase::modeCreate | CFile::modeWrite);
    if (!WatchListFile.IsOpen())
    {
        return;
    }
    for (size_t i = 0; i < m_WatchList.size(); i++)
    {
        CScanResult * Result = &m_WatchList[i];
        char ValueTypeText[32];
        const char * TypeName = Result->GetTypeName();
        switch (Result->GetType())
        {
        case ValueType_string:
        case ValueType_istring:
        case ValueType_unkstring:
            sprintf(ValueTypeText, "%s[%d]", TypeName, Result->GetStrLength());
            TypeName = ValueTypeText;
            break;
        default:
            break;
        }
        const char AddrType = (Result->m_AddressType == AddressType_Physical) ? 'p' : 'v';
        const char * DisplayFormatText = (Result->m_DisplayFormat == DisplayDefault) ? "def" : "hex";
        const stdstr Line = stdstr_f("%c,%08X,%s,%s,%s\n", AddrType, Result->m_Address, TypeName, DisplayFormatText, Result->GetDescription());
        WatchListFile.Write(Line.c_str(), (uint32_t)Line.length());
    }
    WatchListFile.Close();
}

void MemorySearchView::LoadWatchList()
{
    ClearWatchList();
    CPath WlPath = GetWatchListPath();
    CFile WatchListFile;
    WatchListFile.Open(WlPath, CFileBase::modeRead);
    if (!WatchListFile.IsOpen())
    {
        return;
    }
    const uint32_t Length = WatchListFile.GetLength();
    std::string Data(Length, '\0');
    WatchListFile.SeekToBegin();
    WatchListFile.Read(&Data[0], Length);
    WatchListFile.Close();

    // one entry per line: "<v|p>,<address>,<type>,<def|hex>,<description>"
    size_t Pos = 0;
    while (Pos < Data.size())
    {
        size_t LineEnd = Data.find('\n', Pos);
        if (LineEnd == std::string::npos)
        {
            break;
        }
        const std::string Line = Data.substr(Pos, LineEnd - Pos);
        Pos = LineEnd + 1;
        const size_t C1 = Line.find(',');
        const size_t C2 = C1 == std::string::npos ? C1 : Line.find(',', C1 + 1);
        const size_t C3 = C2 == std::string::npos ? C2 : Line.find(',', C2 + 1);
        const size_t C4 = C3 == std::string::npos ? C3 : Line.find(',', C3 + 1);
        if (C4 == std::string::npos)
        {
            break; // parse_error
        }
        const std::string AddrType = Line.substr(0, C1), Address = Line.substr(C1 + 1, C2 - C1 - 1), TypeText = Line.substr(C2 + 1, C3 - C2 - 1),
                          Format = Line.substr(C3 + 1, C4 - C3 - 1), Description = Line.substr(C4 + 1);
        CScanResult Result(AddressType_Virtual, DisplayDefault);
        if (AddrType == "v")
        {
            Result.m_AddressType = AddressType_Virtual;
        }
        else if (AddrType == "p")
        {
            Result.m_AddressType = AddressType_Physical;
        }
        else
        {
            break;
        }
        Result.m_Address = strtoul(Address.c_str(), nullptr, 16);
        int CharArrayLength = 0;
        const ::ValueType Type = CMixed::GetTypeFromString(TypeText.c_str(), &CharArrayLength);
        if (Type == ValueType_invalid)
        {
            break;
        }
        Result.SetType(Type);
        if (Result.IsStringType())
        {
            // g_MMU may be null here, so SetStrLengthSafe cannot be used (upstream TODO)
            Result.SetStrLength(CharArrayLength);
        }
        if (Format == "hex")
        {
            Result.m_DisplayFormat = DisplayHex;
        }
        else if (Format == "def")
        {
            Result.m_DisplayFormat = DisplayDefault;
        }
        else
        {
            break;
        }
        if (Description.length() > 0)
        {
            Result.SetDescription(Description.c_str());
        }
        m_WatchList.push_back(Result);
    }
    UpdateWatchList(true);
}

CPath MemorySearchView::GetWatchListPath()
{
    stdstr SaveDir = g_Settings->LoadStringVal(Directory_NativeSave);
    stdstr WlFileName = stdstr_f("%s.wlst", m_StrGame.c_str());
    CPath WlFilePath(SaveDir.c_str(), WlFileName.c_str());
    if (g_Settings->LoadBool(Setting_UniqueSaveDir))
    {
        stdstr UniqueSaveDir = g_Settings->LoadStringVal(Game_UniqueSaveDir);
        WlFilePath.AppendDirectory(UniqueSaveDir.c_str());
    }
    WlFilePath.NormalizePath(CPath(CPath::MODULE_DIRECTORY));
    if (!WlFilePath.DirectoryExists())
    {
        WlFilePath.DirectoryCreate();
    }
    return WlFilePath;
}
