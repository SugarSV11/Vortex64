#pragma once

#include "stdafx.h"

#include "DebugWindow.h"
#include "HexEditWidget.h"

#include <vector>

class CBreakpoints;
class NumberEdit;
class QCheckBox;
class QComboBox;
class QLabel;
class QScrollBar;
class QTabBar;

// Qt port of CDebugMemoryView (Debugger-ViewMemory.cpp): tabs of hex editors over the
// virtual (CPU) or physical (RCP) address space with breakpoints, locks, safe edit mode,
// symbol / CPU access colouring and the DMA / block status panes
class MemoryView : public DebugWindow
{
    Q_OBJECT
public:
    MemoryView(CDebuggerUI * Debugger);

    void ShowAddress(uint32_t Address, bool Virtual);

    // Test hooks
    HexEditWidget * Hex()
    {
        return m_Hex;
    }
    NumberEdit * AddressEdit()
    {
        return m_MemAddr;
    }
    QTabBar * Tabs()
    {
        return m_Tabs;
    }
    QComboBox * JumpCombo()
    {
        return m_CmbJump;
    }
    QCheckBox * VirtualCheck()
    {
        return m_Virtual;
    }
    QLabel * StatusPane(int Pane)
    {
        return m_Status[Pane];
    }
    bool SafeEditMode() const
    {
        return m_SafeEditMode;
    }
    void OpenNewTab(uint32_t Address, bool Virtual, int NumBytesPerGroup, bool Insert = false, bool OpenExisting = false);
    void CloseTab(int Item);
    void ContextMenuAt(uint32_t Address, const QPoint & GlobalPos, bool Exec = true);
    QString CopyBytesText(uint32_t StartAddress, uint32_t EndAddress, bool Hex, bool IncludeAddresses, bool RowAddresses);
    QString GameSharkText(uint32_t StartAddress, uint32_t EndAddress);
    void ApplySafeEditsNow()
    {
        ApplySafeEdits();
    }
    size_t SafeEditCount() const
    {
        return m_SafeEditQueue.size();
    }
    void ScrollAction(int Action);

    enum
    {
        MEMSB_HOTADDR,
        MEMSB_BLOCK,
        MEMSB_BLOCKLEN,
        MEMSB_DMAINFO,
        MEMSB_SAFEMODE,
        MEMSB_NUM_PANES
    };

private:
    enum EditType
    {
        SE_FILL
    };
    struct Edit
    {
        EditType type;
        uint32_t startAddress;
        uint32_t endAddress;
        uint8_t value;
    };
    struct JumpItem
    {
        uint32_t vaddr;
        uint32_t paddr;
        uint32_t size;
        const char * caption;
    };
    struct TabInfo
    {
        uint32_t address;
        bool isVirtual;
        int numBytesPerGroup;
    };
    static const JumpItem JumpItems[];
    static int GetJumpItemIndex(uint32_t Address, bool Virtual);

    bool GetByte(uint32_t Address, uint8_t * Value);
    bool SetByte(uint32_t Address, uint8_t Value);
    void SetupJumpMenu(bool Virtual);
    bool GetSafeEditValue(uint32_t Address, uint8_t * Value);
    void ApplySafeEdits();
    void CopyTextToClipboard(const QString & Text);
    void FillRange(uint32_t StartAddress, uint32_t EndAddress, uint8_t Value);
    void FollowPointer(bool ContextMenuAddress = true);
    void JumpToSelection();
    int AddTab(uint32_t Address, bool Virtual, int NumBytesPerGroup);
    int InsertTab(int Item, uint32_t Address, bool Virtual, int NumBytesPerGroup);
    void DeleteTab(int Index);
    void UpdateCurrentTab(uint32_t Address);
    void OpenDuplicateTab();
    void CloseCurrentTab();
    void TabSelChanged();
    void AddrChanged();
    void SetupHex();
    void GetByteInfo(uint32_t Address, size_t NumBytes, bool IgnoreDiff, HxByteInfo * OldBytes, HxByteInfo * NewBytes);
    void SelectionChanged();
    void HotAddrChanged();
    void BaseAddrChanged();
    void CtrlKeyPressed(int Key);
    void SetNibble(bool Insert, uint32_t Address, bool LoNibble, uint8_t Value);
    int PasteData(uint32_t Address, HxColumn Column);
    void StatusClicked(int Pane);
    void SetMemAddr(uint32_t Address);

    CBreakpoints * m_Breakpoints;
    HexEditWidget * m_Hex = nullptr;
    NumberEdit * m_MemAddr = nullptr;
    QCheckBox * m_Virtual = nullptr;
    QTabBar * m_Tabs = nullptr;
    QScrollBar * m_Scrollbar = nullptr;
    QComboBox * m_CmbJump = nullptr;
    QLabel * m_Status[MEMSB_NUM_PANES] = {};
    std::vector<TabInfo> m_TabData;
    int m_WriteTargetColorStride = 0;
    int m_ReadTargetColorStride = 0;
    int m_SymbolColorStride = 0;
    int m_SymbolColorPhase = 0;
    uint32_t m_ContextMenuAddress = 0;
    uint32_t m_HotAddress = 0;
    bool m_VirtualMemory = true;
    bool m_SafeEditMode = false;
    std::vector<Edit> m_SafeEditQueue;
    int m_TabRightClickItem = -1;
};
