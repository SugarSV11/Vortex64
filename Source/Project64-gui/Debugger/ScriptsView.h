#pragma once

#include "stdafx.h"

#include "DebugWindow.h"

#include <QDialog>
#include <QFileSystemWatcher>
#include <QString>
#include <QStringList>
#include <set>
#include <string>

class QGroupBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QTableWidget;
class QTimer;

// Autorun scripts dialog (CScriptsAutorunDlg): the scripts that run when Project64 starts
class ScriptsAutorunDialog : public QDialog
{
    Q_OBJECT
public:
    ScriptsAutorunDialog(CDebuggerUI * Debugger, const QString & SelectedScript, QWidget * Parent = nullptr);

    // Test hooks
    QListWidget * ScriptList() const
    {
        return m_ScriptList;
    }
    QListWidget * AutorunList() const
    {
        return m_AutorunList;
    }
    void AddSelected();
    void RemoveSelected();

private:
    void RefreshLists();

    CDebuggerUI * m_Debugger;
    QString m_InitSelected;
    QListWidget * m_ScriptList;
    QListWidget * m_AutorunList;
    QPushButton * m_Add;
    QPushButton * m_Remove;
    QFileSystemWatcher m_Watcher;
};

// The Scripts window (CDebugScripts): the script list, run/stop, the console output, the
// console input of the selected script and the autorun list. It talks to the script system
// of the debugger (CScriptSystem) only through CDebuggerUI.
class ScriptsView : public DebugWindow
{
    Q_OBJECT
public:
    explicit ScriptsView(CDebuggerUI * Debugger);
    ~ScriptsView() override;

    // GUI thread; Sequence orders the text against the console snapshot the window started from
    void ConsolePrint(uint64_t Sequence, const std::string & Text);
    void ConsoleClear(uint64_t Sequence);
    void RefreshList();

    // Test hooks
    QTableWidget * ScriptTable() const
    {
        return m_ScriptList;
    }
    QPlainTextEdit * Output() const
    {
        return m_Output;
    }
    QLineEdit * Input() const
    {
        return m_Input;
    }
    QPushButton * RunButton() const
    {
        return m_Run;
    }
    QLabel * Status() const
    {
        return m_StatusLabel;
    }
    QString SelectedScript() const
    {
        return m_SelectedScript;
    }
    bool SelectScript(const QString & Name);
    void RunSelected();
    void StopSelected();
    void ToggleSelected();
    void SubmitInput();
    void FlushConsole();

protected:
    bool eventFilter(QObject * Watched, QEvent * Event) override;

private:
    void RefreshStatus();
    void ShowContextMenu(const QPoint & Position);
    void EditSelected();
    void ConsoleCopy();
    QString ScriptsDir() const;

    QTableWidget * m_ScriptList;
    QPlainTextEdit * m_Output;
    QLineEdit * m_Input;
    QPushButton * m_Run;
    QLabel * m_StatusLabel;
    QTimer * m_FlushTimer;
    QFileSystemWatcher m_Watcher;

    QString m_SelectedScript;
    QStringList m_InputHistory;
    int m_InputHistoryIndex = 0;
    std::string m_Pending;
    uint64_t m_BaseSequence = 0;
    bool m_ClearPending = false;
};
