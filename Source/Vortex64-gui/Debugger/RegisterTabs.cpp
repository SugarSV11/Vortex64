#include "stdafx.h"
#include "../Vortex/Strings.h"
#include "RegisterTabs.h"
#include "DebugCommon.h"
#include "DebuggerUI.h"

#include <Vortex64-core/N64System/Mips/R4300iInstruction.h>
#include <Vortex64-core/N64System/Mips/Register.h>
#include <Vortex64-core/N64System/SystemGlobals.h>
#include <Vortex64-core/Settings/DebugSettings.h>
#include <Vortex64-ui/UserInterface/Debugger/Breakpoints.h>

#include <QApplication>
#include <QClipboard>
#include <QFontDatabase>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QScrollArea>
#include <QTabBar>

namespace
{
// A register label: click toggles the read breakpoint, right click the write breakpoint
class RegisterLabel : public QLabel
{
public:
    std::function<void(bool)> Toggle; // argument: write breakpoint
    using QLabel::QLabel;

protected:
    void mousePressEvent(QMouseEvent * Event) override
    {
        if (Toggle)
        {
            Toggle(Event->button() == Qt::RightButton);
        }
    }
};

struct RegDef
{
    const char * Label;
    std::function<uint32_t()> Get;
    std::function<void(uint32_t)> Set;
};
struct TableDef
{
    const char * Name;
    std::vector<RegDef> Regs;
};

std::vector<TableDef> RcpTables()
{
    return {
#include "RegisterTables.inc"
    };
}

const char * const GprNames[32] = {"R0", "AT", "V0", "V1", "A0", "A1", "A2", "A3", "T0", "T1", "T2", "T3", "T4", "T5", "T6", "T7",
                                   "S0", "S1", "S2", "S3", "S4", "S5", "S6", "S7", "T8", "T9", "K0", "K1", "GP", "SP", "S8", "RA"};

const char * const ExceptionCodes[] = {
    "Interrupt", "TLB mod", "TLB load/fetch", "TLB store", "Address error (load/fetch)", "Address error (store)",
    "Bus error (instruction fetch)", "Bus error (data load/store)", "Syscall", "Breakpoint", "Reserved instruction",
    "Coprocessor unusable", "Arithmetic overflow", "Trap", "Virtual coherency (instruction)", "Floating-point",
    "? 16", "? 17", "? 18", "? 19", "? 20", "? 21", "? 22", "Watch", "? 24", "? 25", "? 26", "? 27", "? 28", "? 29", "? 30",
    "Virtual coherency (data)"};

// CEditReg64::ParseValue: "HHHHHHHH LLLLLLLL" or a single word
uint64_t ParseValue64(const QString & Text)
{
    const QByteArray Bytes = Text.toLatin1();
    char * End = nullptr;
    const uint32_t A = strtoul(Bytes.constData(), &End, 16);
    if (*End == ' ')
    {
        const uint32_t B = strtoul(End + 1, nullptr, 16);
        return ((uint64_t)A << 32) | B;
    }
    return (uint64_t)A;
}

QString Format64(uint64_t Value)
{
    return QString("%1 %2").arg((uint32_t)(Value >> 32), 8, 16, QChar('0')).arg((uint32_t)Value, 8, 16, QChar('0')).toUpper();
}

QString Format32(uint32_t Value)
{
    return QString("%1").arg(Value, 8, 16, QChar('0')).toUpper();
}
} // namespace

RegisterTabs::RegisterTabs(CDebuggerUI * Debugger, QWidget * Parent) :
    QTabWidget(Parent),
    m_Debugger(Debugger)
{
    setUsesScrollButtons(true);
    setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    // eight tabs have to fit the width of the debugger without the scroll buttons: less padding than the theme's 16 px
    tabBar()->setStyleSheet(QStringLiteral("QTabBar::tab { padding: 7px 5px; }"));
    BuildTabs();
    RefreshEdits();
}

void RegisterTabs::BuildTabs()
{
    // GPR: R0..R31 (64 bit) then HI and LO
    {
        Tab GprTab;
        GprTab.Name = "GPR";
        for (int i = 0; i < 32; i++)
        {
            Field F;
            F.Label = GprNames[i];
            F.Is64 = true;
            GprTab.Fields.push_back(F);
        }
        Field Hi;
        Hi.Label = "HI";
        Hi.Is64 = true;
        Field Lo;
        Lo.Label = "LO";
        Lo.Is64 = true;
        GprTab.Fields.push_back(Hi);
        GprTab.Fields.push_back(Lo);
        m_Tabs.push_back(GprTab);
    }
    // FPR: F0..F31 then FCSR
    {
        Tab FprTab;
        FprTab.Name = "FPR";
        for (int i = 0; i < 32; i++)
        {
            Field F;
            F.Label = QString("F%1").arg(i);
            F.Get = [i]() { return *(uint32_t *)g_Reg->m_FPR_S[i]; };
            F.Set = [i](uint32_t Value) { *(uint32_t *)g_Reg->m_FPR_S[i] = Value; };
            FprTab.Fields.push_back(F);
        }
        Field Fcsr;
        Fcsr.Label = "FCSR";
        Fcsr.Get = []() { return (uint32_t)g_Reg->m_FPCR[31]; };
        Fcsr.Set = [](uint32_t Value) { g_Reg->m_FPCR[31] = Value; };
        FprTab.Fields.push_back(Fcsr);
        m_Tabs.push_back(FprTab);
    }
    static const struct
    {
        const char * Name;
        const char * Address;
    } Addresses[] = {{"COP0", ""}, {"RDRAM", " (A3F00000)"}, {"SP", " (A4040000)"}, {"DPC", " (A4100000)"}, {"MI", " (A4300000)"},
                     {"VI", " (A4400000)"}, {"AI", " (A4500000)"}, {"PI", " (A4600000)"}, {"RI", " (A4700000)"}, {"SI", " (A4800000)"},
                     {"DD", " (A5000500)"}};
    for (const TableDef & Def : RcpTables())
    {
        Tab T;
        T.Name = Def.Name;
        for (const auto & A : Addresses)
        {
            if (T.Name == A.Name)
            {
                T.Address = A.Address;
            }
        }
        for (const RegDef & R : Def.Regs)
        {
            Field F;
            F.Label = R.Label;
            F.Get = R.Get;
            F.Set = R.Set;
            T.Fields.push_back(F);
        }
        m_Tabs.push_back(T);
    }
    for (size_t t = 0; t < m_Tabs.size(); t++)
    {
        Tab & T = m_Tabs[t];
        const int Columns = T.Name == "GPR" ? 2 : (T.Name == "FPR" ? 2 : 1);
        QWidget * Page = BuildGrid(T, Columns);
        addTab(Page, T.Name);
    }
}

QWidget * RegisterTabs::BuildGrid(Tab & TabData, int Columns)
{
    QWidget * Content = new QWidget;
    QGridLayout * Grid = new QGridLayout(Content);
    Grid->setSpacing(2);
    Grid->setContentsMargins(4, 4, 4, 4);
    const int TabIndex = (int)(&TabData - &m_Tabs[0]);
    const size_t Count = TabData.Fields.size();
    // GPR/FPR: the last entries (HI/LO, FCSR) go below the two columns of 16
    const bool Paired = Columns == 2;
    const size_t PerColumn = Paired ? 16 : Count;
    for (size_t i = 0; i < Count; i++)
    {
        Field & F = TabData.Fields[i];
        RegisterLabel * Label = new RegisterLabel(F.Label, Content);
        F.LabelWidget = Label;
        QLineEdit * Edit;
        if (F.Is64)
        {
            Edit = new QLineEdit(Content);
            Edit->setMaxLength(17);
            Edit->setValidator(new QRegularExpressionValidator(QRegularExpression("[0-9A-Fa-f ]*"), Edit));
            Edit->setMinimumWidth(150);
        }
        else
        {
            Edit = new QLineEdit(Content);
            Edit->setMaxLength(8);
            Edit->setValidator(new QRegularExpressionValidator(QRegularExpression("[0-9A-Fa-f]*"), Edit));
            Edit->setMinimumWidth(80);
        }
        F.Edit = Edit;
        int Row, Col;
        if (Paired && i < 32)
        {
            Row = (int)(i % PerColumn);
            Col = (int)(i / PerColumn) * 2;
        }
        else if (Paired)
        {
            Row = 16 + (int)(i - 32);
            Col = 0;
        }
        else
        {
            Row = (int)i;
            Col = 0;
        }
        Grid->addWidget(Label, Row, Col);
        Grid->addWidget(Edit, Row, Col + 1);
        connect(Edit, &QLineEdit::editingFinished, this, [this, TabIndex, i]() { FieldEdited(TabIndex, (int)i); });
        if (TabData.Name == "GPR")
        {
            const int FieldIndex = (int)i;
            Label->setCursor(Qt::PointingHandCursor);
            Label->Toggle = [this, FieldIndex](bool Write) { ToggleBreakpoint(FieldIndex, Write); };
        }
    }
    if (TabData.Name == "COP0")
    {
        TabData.Note = new QLabel(Content);
        Grid->addWidget(TabData.Note, (int)Count, 0, 1, 2);
    }
    Grid->setRowStretch(Grid->rowCount(), 1);
    QScrollArea * Scroll = new QScrollArea;
    Scroll->setWidgetResizable(true);
    Scroll->setWidget(Content);
    return Scroll;
}

void RegisterTabs::ToggleBreakpoint(int FieldIndex, bool Write)
{
    if (m_Debugger == nullptr)
    {
        return;
    }
    CBreakpoints * Breakpoints = m_Debugger->Breakpoints();
    if (FieldIndex == 32) // HI
    {
        Write ? Breakpoints->ToggleHIWriteBP() : Breakpoints->ToggleHIReadBP();
    }
    else if (FieldIndex == 33) // LO
    {
        Write ? Breakpoints->ToggleLOWriteBP() : Breakpoints->ToggleLOReadBP();
    }
    else if (FieldIndex > 0) // R0 is ignored
    {
        Write ? Breakpoints->ToggleGPRWriteBP(FieldIndex) : Breakpoints->ToggleGPRReadBP(FieldIndex);
    }
    RefreshColors();
}

void RegisterTabs::SetColorsEnabled(bool Enabled)
{
    m_ColorsEnabled = Enabled;
    RefreshColors();
}

// Label colours: register breakpoints; edit colours: what the instruction at PC reads/writes
void RegisterTabs::RefreshColors()
{
    const QString Read = "#c8c8ff", Write = "#ffc8c8", Both = "#dcaaff";
    Tab & Gpr = m_Tabs[0];
    for (size_t i = 0; i < Gpr.Fields.size(); i++)
    {
        Field & F = Gpr.Fields[i];
        if (F.LabelWidget == nullptr || F.Edit == nullptr)
        {
            continue;
        }
        bool HaveRead = false, HaveWrite = false;
        if (m_Debugger != nullptr && m_Debugger->Breakpoints() != nullptr)
        {
            CBreakpoints * B = m_Debugger->Breakpoints();
            if (i == 32)
            {
                HaveRead = B->HaveHIReadBP();
                HaveWrite = B->HaveHIWriteBP();
            }
            else if (i == 33)
            {
                HaveRead = B->HaveLOReadBP();
                HaveWrite = B->HaveLOWriteBP();
            }
            else
            {
                HaveRead = B->HaveGPRReadBP((int)i);
                HaveWrite = B->HaveGPRWriteBP((int)i);
            }
        }
        F.LabelWidget->setStyleSheet(HaveRead && HaveWrite ? QString("background:%1").arg(Both) : (HaveRead ? QString("background:%1").arg(Read) : (HaveWrite ? QString("background:%1").arg(Write) : QString())));

        bool OpReads = false, OpWrites = false;
        if (m_ColorsEnabled && g_Reg != nullptr && g_MMU != nullptr && m_Debugger != nullptr)
        {
            uint32_t Instruction = 0;
            m_Debugger->DebugLoad_VAddr((uint32_t)g_Reg->m_PROGRAM_COUNTER, Instruction);
            R4300iInstruction OpInfo(g_Reg->m_PROGRAM_COUNTER, Instruction);
            if (i == 33)
            {
                OpReads = OpInfo.ReadsLO();
                OpWrites = !OpReads && OpInfo.WritesLO();
            }
            else if (i == 32)
            {
                OpReads = OpInfo.ReadsHI();
                OpWrites = !OpReads && OpInfo.WritesHI();
            }
            else
            {
                uint32_t Read1, Read2, WriteReg = OpInfo.WritesGPR();
                OpInfo.ReadsGPR(Read1, Read2);
                OpReads = (uint32_t)i == Read1 || (uint32_t)i == Read2;
                OpWrites = (uint32_t)i == WriteReg;
            }
        }
        F.Edit->setStyleSheet(OpReads && OpWrites ? QString("background:%1").arg(Both) : (OpReads ? QString("background:%1").arg(Read) : (OpWrites ? QString("background:%1").arg(Write) : QString())));
    }
}

void RegisterTabs::RefreshEdits()
{
    m_Refreshing = true;
    for (size_t t = 0; t < m_Tabs.size(); t++)
    {
        Tab & T = m_Tabs[t];
        for (size_t i = 0; i < T.Fields.size(); i++)
        {
            Field & F = T.Fields[i];
            if (F.Edit == nullptr)
            {
                continue;
            }
            if (g_Reg == nullptr)
            {
                F.Edit->setText(F.Is64 ? Format64(0) : Format32(0));
                continue;
            }
            if (t == 0) // GPR tab
            {
                F.Edit->setText(Format64(i < 32 ? g_Reg->m_GPR[i].UDW : (i == 32 ? g_Reg->m_HI.UDW : g_Reg->m_LO.UDW)));
            }
            else
            {
                F.Edit->setText(Format32(F.Get()));
            }
        }
        if (T.Note != nullptr)
        {
            T.Note->setText(g_Reg != nullptr ? Vortex::Tr(ExceptionCodes[((uint32_t)g_Reg->CAUSE_REGISTER.Value >> 2) & 0x1F]) : QString());
        }
    }
    m_Refreshing = false;
    RefreshColors();
}

// RegisterChanged: only while stepping; 32 bit values are normalised to 8 hex digits
void RegisterTabs::FieldEdited(int TabIndex, int FieldIndex)
{
    if (m_Refreshing || g_Reg == nullptr || !g_DebugSettings.stepping)
    {
        return;
    }
    Field & F = m_Tabs[TabIndex].Fields[FieldIndex];
    if (TabIndex == 0)
    {
        const uint64_t Value = ParseValue64(F.Edit->text());
        if (FieldIndex < 32)
        {
            g_Reg->m_GPR[FieldIndex].UDW = Value;
        }
        else if (FieldIndex == 32)
        {
            g_Reg->m_HI.UDW = Value;
        }
        else
        {
            g_Reg->m_LO.UDW = Value;
        }
        F.Edit->setText(Format64(Value));
        return;
    }
    const uint32_t Value = (uint32_t)F.Edit->text().toULongLong(nullptr, 16);
    F.Edit->setText(Format32(Value));
    F.Set(Value);
}

// CopyTabRegisters(id): "NAME (addr)\r\nLABEL value..." (the text upstream puts on the clipboard)
QString RegisterTabs::TabText(int Index)
{
    Tab & T = m_Tabs[Index];
    QString Text = T.Name + T.Address;
    const size_t Regular = Index == 0 ? 32 : (Index == 1 ? 32 : T.Fields.size());
    const bool IsSp = T.Name == "SP";
    const size_t Limit = IsSp ? T.Fields.size() - 1 : Regular;
    for (size_t i = 0; i < Limit; i++)
    {
        Text += QString("\r\n%1 %2").arg(T.Fields[i].Label, T.Fields[i].Edit->text());
    }
    if (Index == 0)
    {
        Text += QString("\r\nHI %1").arg(T.Fields[32].Edit->text());
        Text += QString("\r\nLO %1").arg(T.Fields[33].Edit->text());
    }
    else if (Index == 1)
    {
        Text += QString("\r\nFCSR %1").arg(T.Fields[32].Edit->text());
    }
    else if (IsSp)
    {
        Text += QString("\r\nSP (A4080000)\r\n%1 %2").arg(T.Fields.back().Label, T.Fields.back().Edit->text());
    }
    Text += "\r\n";
    return Text;
}

void RegisterTabs::CopyTabRegisters()
{
    QApplication::clipboard()->setText(TabText(currentIndex()));
}

void RegisterTabs::CopyAllRegisters()
{
    QString Text;
    for (int i = 0; i < (int)m_Tabs.size(); i++)
    {
        if (!Text.isEmpty())
        {
            Text += "\r\n";
        }
        Text += TabText(i);
    }
    QApplication::clipboard()->setText(Text);
}
