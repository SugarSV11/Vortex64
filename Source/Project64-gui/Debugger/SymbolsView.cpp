#include "stdafx.h"
#include "../Vortex/Strings.h"

#include "SymbolsView.h"
#include "DebugCommon.h"
#include "DebugDialogs.h"
#include "DebuggerUI.h"

#include <QGridLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTimer>

using Vortex::Tr;

static const DebugDialogs::ComboItem ModalChangeTypeItems[] = {
    {"code", SYM_CODE},
    {"data", SYM_DATA},
    {"uint8", SYM_U8},
    {"int8", SYM_S8},
    {"uint16", SYM_U16},
    {"int16", SYM_S16},
    {"uint32", SYM_U32},
    {"int32", SYM_S32},
    {"uint64", SYM_U64},
    {"int64", SYM_S64},
    {"float", SYM_FLOAT},
    {"double", SYM_DOUBLE},
    {"v2", SYM_VECTOR2},
    {"v3", SYM_VECTOR3},
    {"v4", SYM_VECTOR4},
    {nullptr, 0},
};

SymbolsView::SymbolsView(CDebuggerUI * Debugger) :
    DebugWindow(Debugger, DebuggerUI_SymbolsPos, true),
    m_SymbolTable(Debugger->SymbolTable())
{
    resize(313 * 1.6, 141 * 1.6);
    QGridLayout * Layout = new QGridLayout(this);
    m_List = new QTableWidget(0, Num_Columns, this);
    m_List->setHorizontalHeaderLabels({Tr("Address"), Tr("Type"), Tr("Name"), Tr("Value"), Tr("Description")});
    m_List->verticalHeader()->hide();
    m_List->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_List->setSelectionMode(QAbstractItemView::SingleSelection);
    m_List->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_List->horizontalHeader()->setStretchLastSection(true);
    m_List->setColumnWidth(0, 90);
    m_List->setColumnWidth(1, 60);
    m_List->setColumnWidth(2, 150);
    m_List->setColumnWidth(3, 120);
    Layout->addWidget(m_List, 0, 0, 1, 5);
    Layout->addWidget(new QLabel(Tr("Filter:"), this), 1, 0);
    m_Filter = new QLineEdit(this);
    Layout->addWidget(m_Filter, 1, 1);
    QPushButton * Remove = new QPushButton("-", this);
    QPushButton * Add = new QPushButton("+", this);
    Layout->addWidget(Remove, 1, 3);
    Layout->addWidget(Add, 1, 4);
    Layout->setColumnStretch(1, 1);

    connect(Add, &QPushButton::clicked, this, [this]() { DebugDialogs::AddSymbol(m_Debugger, this); });
    connect(Remove, &QPushButton::clicked, this, [this]() { RemoveSelected(); });
    connect(m_List, &QTableWidget::cellDoubleClicked, this, [this](int Row, int Column) { ListDoubleClicked(Row, Column); });
    connect(m_Filter, &QLineEdit::textChanged, this, [this](const QString & Text) {
        m_Filtering = !Text.isEmpty();
        Refresh();
    });

    m_Timer = new QTimer(this);
    connect(m_Timer, &QTimer::timeout, this, [this]() {
        if (isVisible())
        {
            Refresh();
        }
    });
    m_Timer->start(100);
    Refresh();
    LoadWindowPos();
}

SymbolsView::Item SymbolsView::MakeItem(CSymbol & Symbol)
{
    char Value[64];
    m_SymbolTable->GetValueString(Value, &Symbol);
    Item Result;
    Result.id = Symbol.m_Id;
    Result.address = QString("%1").arg(Symbol.m_Address, 8, 16, QChar('0')).toUpper();
    Result.type = QString::fromUtf8(Symbol.TypeName());
    Result.name = QString::fromUtf8(Symbol.m_Name);
    Result.value = QString::fromUtf8(Value);
    Result.description = QString::fromUtf8(Symbol.m_Description);
    return Result;
}

void SymbolsView::UpdateFiltered()
{
    m_Filtered.clear();
    const QString Text = m_Filter->text();
    CSymbol Symbol;
    size_t Index = 0;
    while (m_SymbolTable->GetSymbolByIndex(Index++, &Symbol))
    {
        if (QString::fromUtf8(Symbol.m_Name).contains(Text) || QString::fromUtf8(Symbol.m_Description).contains(Text))
        {
            m_Filtered.push_back(MakeItem(Symbol));
        }
    }
}

void SymbolsView::Refresh()
{
    if (g_Settings->LoadStringVal(Game_GameName) != "")
    {
        setWindowTitle(Tr("Symbols - %1").arg(QString::fromStdString(g_Settings->LoadStringVal(Game_GameName))));
    }
    else
    {
        setWindowTitle(Tr("Symbols"));
    }
    if (m_List == nullptr)
    {
        return;
    }
    std::vector<Item> Items;
    if (m_Filtering)
    {
        UpdateFiltered();
        Items = m_Filtered;
    }
    else
    {
        CSymbol Symbol;
        for (size_t Index = 0; m_SymbolTable->GetSymbolByIndex(Index, &Symbol); Index++)
        {
            Items.push_back(MakeItem(Symbol));
        }
    }
    // keep the selection across the periodic refresh
    const int Selected = m_List->currentRow();
    m_List->setUpdatesEnabled(false);
    m_List->setRowCount((int)Items.size());
    for (int Row = 0; Row < (int)Items.size(); Row++)
    {
        const QString Cells[Num_Columns] = {Items[Row].address, Items[Row].type, Items[Row].name, Items[Row].value, Items[Row].description};
        for (int Column = 0; Column < Num_Columns; Column++)
        {
            QTableWidgetItem * Cell = m_List->item(Row, Column);
            if (Cell == nullptr)
            {
                Cell = new QTableWidgetItem(Cells[Column]);
                m_List->setItem(Row, Column, Cell);
            }
            else if (Cell->text() != Cells[Column])
            {
                Cell->setText(Cells[Column]);
            }
        }
    }
    if (Selected >= 0 && Selected < (int)Items.size())
    {
        m_List->setCurrentCell(Selected, 0);
    }
    m_List->setUpdatesEnabled(true);
}

int SymbolsView::SymbolIdAt(int Row)
{
    if (m_Filtering)
    {
        if (Row >= (int)m_Filtered.size())
        {
            g_Notify->BreakPoint(__FILE__, __LINE__);
        }
        return m_Filtered[Row].id;
    }
    CSymbol Symbol;
    if (!m_SymbolTable->GetSymbolByIndex(Row, &Symbol))
    {
        g_Notify->BreakPoint(__FILE__, __LINE__);
    }
    return Symbol.m_Id;
}

void SymbolsView::RemoveSelected()
{
    const int Row = m_List->currentRow();
    if (Row < 0)
    {
        return;
    }
    const int Id = SymbolIdAt(Row);
    if (Id != -1)
    {
        m_SymbolTable->RemoveSymbolById(Id);
        m_SymbolTable->Save();
        Refresh();
    }
}

void SymbolsView::ListDoubleClicked(int Row, int Column)
{
    if (Row < 0)
    {
        return;
    }
    const int Id = SymbolIdAt(Row);
    CSymbol Symbol;
    if (!m_SymbolTable->GetSymbolById(Id, &Symbol))
    {
        return;
    }
    switch (Column)
    {
    case Col_Address:
        // Open it in memory viewer/commands viewer
        if (Symbol.m_Type == SYM_CODE)
        {
            m_Debugger->Debug_ShowCommandsLocation(Symbol.m_Address, true);
        }
        else
        {
            m_Debugger->Debug_ShowMemoryLocation(Symbol.m_Address, true);
        }
        break;
    case Col_Type:
    {
        uintptr_t Type;
        if (DebugDialogs::SetValueCombo(this, Tr("Change type"), Tr("New type:"), Symbol.m_Type, ModalChangeTypeItems, Type))
        {
            // TODO (upstream): Is there a better way?
            m_SymbolTable->RemoveSymbolById(Id);
            m_SymbolTable->AddSymbol((int)Type, Symbol.m_Address, Symbol.m_Name, Symbol.m_Description);
        }
        break;
    }
    case Col_Name:
    case Col_Description:
    case Col_Value:
    {
        std::string Entered;
        char Value[256];
        m_SymbolTable->GetValueString(Value, &Symbol);
        const QString Caption = Column == Col_Name ? Tr("Set name") : (Column == Col_Value ? Tr("Change value") : Tr("Set description"));
        const QString Label = Column == Col_Name ? Tr("New name:") : (Column == Col_Value ? Tr("New value:") : Tr("New description:"));
        const std::string Initial = Column == Col_Name ? Symbol.m_Name : (Column == Col_Value ? Value : Symbol.m_Description);
        if (DebugDialogs::SetValueText(this, Caption, Label, Initial, Entered))
        {
            ChangeCell(Row, Column, Entered);
        }
        break;
    }
    }
    m_SymbolTable->Save();
    Refresh();
}

// Applies the entered text of a Name/Value/Description cell
void SymbolsView::ChangeCell(int Row, int Column, const std::string & Entered)
{
    const int Id = SymbolIdAt(Row);
    CSymbol Symbol;
    if (!m_SymbolTable->GetSymbolById(Id, &Symbol))
    {
        return;
    }
    if (Column == Col_Name)
    {
        m_SymbolTable->RemoveSymbolById(Id);
        m_SymbolTable->AddSymbol(Symbol.m_Type, Symbol.m_Address, Entered.c_str(), Symbol.m_Description);
    }
    else if (Column == Col_Description)
    {
        m_SymbolTable->RemoveSymbolById(Id);
        m_SymbolTable->AddSymbol(Symbol.m_Type, Symbol.m_Address, Symbol.m_Name, Entered.c_str());
    }
    else if (Column == Col_Value)
    {
        StoreValue(Symbol, Entered);
    }
    m_SymbolTable->Save();
    Refresh();
}

// Writes a typed value to the symbol's address (the upstream switch)
void SymbolsView::StoreValue(CSymbol & Symbol, const std::string & Entered)
{
    // Vectors are entered as "x,y[,z[,w]]"
    auto Components = [&](int Count) {
        size_t Start = 0;
        for (int i = 0; i < Count; i++)
        {
            const size_t End = i + 1 < Count ? Entered.find(',', Start) : std::string::npos;
            if (i + 1 < Count && End == std::string::npos)
            {
                return; // upstream reads past the end here; refuse malformed input instead
            }
            const std::string Part = Entered.substr(Start, End == std::string::npos ? std::string::npos : End - Start);
            m_Debugger->DebugStore_VAddr<float>(Symbol.m_Address + sizeof(float) * i, (float)atof(Part.c_str()));
            Start = End + 1;
        }
    };
    const char * S = Entered.c_str();
    switch (Symbol.m_Type)
    {
    case SYM_U8: m_Debugger->DebugStore_VAddr<uint8_t>(Symbol.m_Address, (uint8_t)atoi(S)); break;
    case SYM_U16: m_Debugger->DebugStore_VAddr<uint16_t>(Symbol.m_Address, (uint16_t)atoi(S)); break;
    case SYM_U32: m_Debugger->DebugStore_VAddr<uint32_t>(Symbol.m_Address, atoi(S)); break;
    case SYM_U64: m_Debugger->DebugStore_VAddr<uint64_t>(Symbol.m_Address, atoll(S)); break;
    case SYM_S8: m_Debugger->DebugStore_VAddr<int8_t>(Symbol.m_Address, (int8_t)atoi(S)); break;
    case SYM_S16: m_Debugger->DebugStore_VAddr<int16_t>(Symbol.m_Address, (int16_t)atoi(S)); break;
    case SYM_S32: m_Debugger->DebugStore_VAddr<int>(Symbol.m_Address, atoi(S)); break;
    case SYM_S64: m_Debugger->DebugStore_VAddr<int64_t>(Symbol.m_Address, atoll(S)); break;
    case SYM_FLOAT: m_Debugger->DebugStore_VAddr<float>(Symbol.m_Address, (float)atof(S)); break;
    case SYM_DOUBLE: m_Debugger->DebugStore_VAddr<double>(Symbol.m_Address, atof(S)); break;
    case SYM_VECTOR2: Components(2); break;
    case SYM_VECTOR3: Components(3); break;
    case SYM_VECTOR4: Components(4); break;
    }
}
