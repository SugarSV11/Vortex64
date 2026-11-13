#pragma once

#include "stdafx.h"

#include "DebugWindow.h"

#include <Vortex64-ui/UserInterface/Debugger/OpInfo.h>

#include <QTableWidget>
#include <vector>

class CBreakpoints;
class CDebuggerUI;
class NumberEdit;
class QLineEdit;
class QListWidget;
class QPushButton;
class QScrollBar;
class RegisterTabs;
class SyncEvent;
class CommandsView;

// CCommandList: the command list with the branch arrow column drawn over it
class CommandList : public QTableWidget
{
    Q_OBJECT
public:
    enum
    {
        COL_ARROWS,
        COL_ADDRESS,
        COL_COMMAND,
        COL_PARAMETERS,
        COL_SYMBOL
    };
    struct BranchArrow
    {
        int startPos;
        int endPos;
        int startMargin;
        int endMargin;
        int margin;
    };

    CommandList(QWidget * Parent);
    int RowHeight() const;
    std::vector<BranchArrow> Arrows;

protected:
    void paintEvent(QPaintEvent * Event) override;
};

// Qt port of CDebugCommandsView (Debugger-Commands.cpp)
class CommandsView : public DebugWindow
{
    Q_OBJECT
public:
    CommandsView(CDebuggerUI * Debugger, SyncEvent & StepEvent);
    ~CommandsView();

    void ShowAddress(uint32_t Address, bool Top, bool UserInput = false);
    void CopyCommands(uint32_t Address, uint32_t Count);
    void ShowPIRegTab();
    void Reset();

    // Test hooks
    bool IsOpEdited(uint32_t Address) const;
    void EditOp(uint32_t Address, uint32_t Op, bool Refresh = true);
    void RestoreAllOps();
    uint32_t StartAddress() const
    {
        return m_StartAddress;
    }
    CommandList * List()
    {
        return m_CommandList;
    }
    QListWidget * BreakpointList()
    {
        return m_BreakpointList;
    }
    RegisterTabs * Registers()
    {
        return m_RegisterTabs;
    }
    QPushButton * StepButton()
    {
        return m_StepButton;
    }
    QPushButton * GoButton()
    {
        return m_GoButton;
    }
    void StepClicked();
    void GoClicked();
    void SkipClicked();
    void StepOverClicked();
    void ToggleBreakpointAt(uint32_t Address);
    void AssembleAndEdit(uint32_t Address, const QString & Line);

protected:
    bool eventFilter(QObject * Watched, QEvent * Event) override;
    void wheelEvent(QWheelEvent * Event) override;
    void showEvent(QShowEvent * Event) override;
    void resizeEvent(QResizeEvent * Event) override;
    void changeEvent(QEvent * Event) override;

private:
    struct EditedOp
    {
        uint32_t address;
        uint32_t originalOp;
    };

    static void StaticSteppingOpsChanged(CommandsView * This);
    static void StaticWaitingForStepChanged(CommandsView * This);
    static void StaticGameCpuRunningChanged(CommandsView * This);

    void BuildUi();
    void RecompilerCheck();
    void CPUSkip();
    void CPUResume();
    void CPUStepOver();
    void SteppingOpsChanged();
    void WaitingForStepChanged();
    void EnableStepButtons(bool Enable);
    void RefreshBreakpointList();
    void RemoveSelectedBreakpoints();
    void HistoryPushState();
    void ToggleHistoryButtons();
    void AddBranchArrow(int StartPos, int EndPos);
    void RedrawCommands();
    void ShowContextMenu(int Row, const QPoint & GlobalPos);
    void BeginOpEdit(uint32_t Address);
    void EndOpEdit();
    void OpEditReturn();
    void OpEditMultiline(QString Text);
    void RestoreOp(uint32_t Address);
    void GoToAddress(uint32_t Address);
    bool HandleOpEditKey(int Key);

    CBreakpoints * m_Breakpoints;
    SyncEvent & m_StepEvent;
    std::vector<uint32_t> m_History;
    int m_HistoryIndex = -1;

    uint32_t m_StartAddress = 0x80000000;
    NumberEdit * m_AddressEdit = nullptr;
    NumberEdit * m_PCEdit = nullptr;
    CommandList * m_CommandList = nullptr;
    int m_CommandListRows = 39;
    QListWidget * m_BreakpointList = nullptr;
    QScrollBar * m_Scrollbar = nullptr;
    RegisterTabs * m_RegisterTabs = nullptr;
    QPushButton * m_BackButton = nullptr;
    QPushButton * m_ForwardButton = nullptr;
    QPushButton * m_ViewPCButton = nullptr;
    QPushButton * m_StepButton = nullptr;
    QPushButton * m_StepOverButton = nullptr;
    QPushButton * m_SkipButton = nullptr;
    QPushButton * m_GoButton = nullptr;
    QLineEdit * m_OpEdit = nullptr;
    bool m_Editing = false;
    bool m_Attached = false;

    uint32_t m_SelectedAddress = 0;
    uint32_t m_FollowAddress = 0;
    std::vector<EditedOp> m_EditedOps;
    std::vector<bool> m_AnnotatedLines;
};
