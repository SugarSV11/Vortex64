#pragma once

#include "stdafx.h"

#include "DebugWindow.h"

#include <vector>

class CSymbolTable;
class QLineEdit;
class QTableWidget;
class QTimer;

// Qt port of CDebugSymbols (Debugger-Symbols.cpp): the symbol list with the auto refresh
// (100 ms), filter, add/remove and the per-column double click actions
class SymbolsView : public DebugWindow
{
    Q_OBJECT
public:
    enum
    {
        Col_Address,
        Col_Type,
        Col_Name,
        Col_Value,
        Col_Description,
        Num_Columns
    };

    SymbolsView(CDebuggerUI * Debugger);

    void Refresh();

    // Test hooks
    QTableWidget * List()
    {
        return m_List;
    }
    QLineEdit * Filter()
    {
        return m_Filter;
    }
    void RemoveSelected();
    void ChangeCell(int Row, int Column, const std::string & Entered);

private:
    struct Item
    {
        int id;
        QString address, type, name, value, description;
    };
    Item MakeItem(class CSymbol & Symbol);
    void UpdateFiltered();
    void ListDoubleClicked(int Row, int Column);
    int SymbolIdAt(int Row);
    void StoreValue(class CSymbol & Symbol, const std::string & Entered);

    CSymbolTable * m_SymbolTable;
    QTableWidget * m_List = nullptr;
    QLineEdit * m_Filter = nullptr;
    QTimer * m_Timer = nullptr;
    bool m_Filtering = false;
    std::vector<Item> m_Filtered;
};
