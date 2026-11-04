#pragma once

#include "stdafx.h"

#include "DebugWindow.h"

#include <Project64/UserInterface/Debugger/MemoryScanner.h>

#include <QLineEdit>
#include <string>
#include <vector>

class NumberEdit;
class QCheckBox;
class QComboBox;
class QGroupBox;
class QPushButton;
class QScrollBar;
class QSplitter;
class QTableWidget;
class QTimer;

// CEditMixed counterpart: a line edit whose text is read as a typed value (decimal/hex,
// integers, floats) or as a string
class MixedEdit : public QLineEdit
{
    Q_OBJECT
public:
    explicit MixedEdit(QWidget * Parent = nullptr);

    DisplayFormat GetDisplayFormat() const
    {
        return m_DisplayFormat;
    }
    void SetDisplayFormat(DisplayFormat Format)
    {
        m_DisplayFormat = Format;
    }
    void SetType(ValueType Type)
    {
        m_Type = Type;
    }

    bool GetValue(uint8_t & Value);
    bool GetValue(int8_t & Value);
    bool GetValue(uint16_t & Value);
    bool GetValue(int16_t & Value);
    bool GetValue(uint32_t & Value);
    bool GetValue(int32_t & Value);
    bool GetValue(uint64_t & Value);
    bool GetValue(int64_t & Value);
    bool GetValue(float & Value);
    bool GetValue(double & Value);
    bool GetValueString(const wchar_t *& Value, int & Length);
    bool GetValueHexString(const wchar_t *& Value, int & Length);

private:
    ValueType m_Type = ValueType_uint64;
    DisplayFormat m_DisplayFormat = DisplayDefault;
    std::wstring m_String;
};

// Qt port of CDebugMemorySearch (Debugger-MemorySearch.cpp): the memory scanner with the
// results list, the persistent watch list (<game>.wlst) and their context menus
class MemorySearchView : public DebugWindow
{
    Q_OBJECT
public:
    enum
    {
        WatchList_Col_Lock,
        WatchList_Col_BP,
        WatchList_Col_Address,
        WatchList_Col_Description,
        WatchList_Col_Type,
        WatchList_Col_Value,
        WatchList_Num_Columns
    };
    enum
    {
        Results_Col_Address,
        Results_Col_Value,
        Results_Col_Previous
    };

    MemorySearchView(CDebuggerUI * Debugger);
    ~MemorySearchView();

    // Called when the game changes/resets (the watch list is per game)
    void GameReset();

    // Test hooks
    MixedEdit * SearchValue()
    {
        return m_SearchValue;
    }
    QComboBox * SearchType()
    {
        return m_SearchTypeOptions;
    }
    QComboBox * ValueType()
    {
        return m_ValueTypeOptions;
    }
    QCheckBox * Hex()
    {
        return m_HexCheckbox;
    }
    QCheckBox * Unsigned()
    {
        return m_UnsignedCheckbox;
    }
    QCheckBox * Physical()
    {
        return m_PhysicalCheckbox;
    }
    NumberEdit * AddrStart()
    {
        return m_AddrStart;
    }
    NumberEdit * AddrEnd()
    {
        return m_AddrEnd;
    }
    QPushButton * SearchButton()
    {
        return m_SearchButton;
    }
    QPushButton * ResetButton()
    {
        return m_ResetButton;
    }
    QTableWidget * Results()
    {
        return m_Results;
    }
    QTableWidget * Watch()
    {
        return m_Watch;
    }
    QGroupBox * ResultsGroup()
    {
        return m_ResultsGroup;
    }
    CMemoryScanner & Scanner()
    {
        return m_MemoryScanner;
    }
    std::vector<CScanResult> & WatchListData()
    {
        return m_WatchList;
    }
    void DoSearch();
    void DoReset();
    void AddResultToWatchList(int ResultIndex);
    void UpdateWatchList(bool UpdateScrollbar = false);
    void FlushWatchList();
    void ReloadWatchList();
    void SetScanType(int SearchTypeValue);
    void SetValueTypeByData(int ValueTypeValue);
    QString WatchGameSharkCode(const std::vector<int> & Rows);

protected:
    void resizeEvent(class QResizeEvent * Event) override;
    void showEvent(class QShowEvent * Event) override;
    bool eventFilter(QObject * Watched, QEvent * Event) override;

private:
    void Search();
    void UpdateOptions();
    void UpdateResultsList(bool UpdateScrollbar = false, bool ResetScrollPos = true);
    void RefreshResultsListValues();
    void RefreshWatchListValues();
    void ScanTypeChanged();
    void ValueTypeChanged();
    void ResultsContextMenu(const QPoint & GlobalPos);
    void WatchContextMenu(const QPoint & GlobalPos);
    void WatchDoubleClicked(int Row, int Column);
    void LoadWatchList();
    void ClearWatchList();
    void RemoveWatchListItem(int Index);
    void RemoveSelectedWatchListItems();
    std::vector<int> SelectedRows(QTableWidget * Table) const;
    CScanResult * FirstSelectedScanResult();
    CScanResult * FirstSelectedWatchListResult();
    CPath GetWatchListPath();
    void SetComboBoxSelByData(QComboBox * Combo, int Data);
    int NumVisibleRows(QTableWidget * Table) const;
    void PauseCPU();
    void ResumeCPU();

    static const struct ModalItem
    {
        const char * text;
        uintptr_t data;
    } ModalChangeTypeItems[];

    bool m_JalSelected = false;
    bool m_JalHexWasChecked = false;
    bool m_JalUnsignedWasChecked = false;
    stdstr m_StrGame;
    MixedEdit * m_SearchValue = nullptr;
    NumberEdit * m_AddrStart = nullptr;
    NumberEdit * m_AddrEnd = nullptr;
    QComboBox * m_SearchTypeOptions = nullptr;
    QComboBox * m_ValueTypeOptions = nullptr;
    QTableWidget * m_Results = nullptr;
    QTableWidget * m_Watch = nullptr;
    QScrollBar * m_ResultsScrollbar = nullptr;
    QScrollBar * m_WatchScrollbar = nullptr;
    QCheckBox * m_PhysicalCheckbox = nullptr;
    QCheckBox * m_HexCheckbox = nullptr;
    QCheckBox * m_UnsignedCheckbox = nullptr;
    QCheckBox * m_IgnoreCaseCheckbox = nullptr;
    QCheckBox * m_UnkEncodingCheckbox = nullptr;
    QPushButton * m_SearchButton = nullptr;
    QPushButton * m_ResetButton = nullptr;
    QPushButton * m_RdramButton = nullptr;
    QPushButton * m_RomButton = nullptr;
    QPushButton * m_SpmemButton = nullptr;
    QGroupBox * m_ResultsGroup = nullptr;
    QSplitter * m_Splitter = nullptr;
    QTimer * m_Timer = nullptr;
    CMemoryScanner m_MemoryScanner;
    std::vector<CScanResult> m_WatchList;
};
