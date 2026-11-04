#include "stdafx.h"
#include "../Vortex/Strings.h"

#include "ScriptsView.h"
#include "DebuggerUI.h"

#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QEvent>
#include <QFont>
#include <QFontDatabase>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QTableWidget>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

using Vortex::Tr;

namespace
{
QStringList ScriptFiles(const QString & Directory)
{
    QStringList Names = QDir(Directory).entryList({"*.js"}, QDir::Files | QDir::NoDotAndDotDot);
    Names.sort(Qt::CaseInsensitive);
    return Names;
}

QFont MonoFont(bool Bold)
{
    QFont Font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    Font.setBold(Bold);
    return Font;
}
} // namespace

// ---- autorun dialog ----

ScriptsAutorunDialog::ScriptsAutorunDialog(CDebuggerUI * Debugger, const QString & SelectedScript, QWidget * Parent) :
    QDialog(Parent),
    m_Debugger(Debugger),
    m_InitSelected(SelectedScript)
{
    setWindowTitle(Tr("Autorun scripts"));
    setModal(true);
    resize(323 * 1.6, 216 * 1.6);

    QGroupBox * Scripts = new QGroupBox(Tr("Scripts"), this);
    QGroupBox * Autorun = new QGroupBox(Tr("Run when Vortex64 starts"), this);
    m_ScriptList = new QListWidget(Scripts);
    m_AutorunList = new QListWidget(Autorun);
    m_Add = new QPushButton(Tr("Add"), Scripts);
    m_Remove = new QPushButton(Tr("Remove"), Autorun);
    QPushButton * Ok = new QPushButton("OK", this);
    Ok->setDefault(true);

    QVBoxLayout * ScriptsLayout = new QVBoxLayout(Scripts);
    ScriptsLayout->addWidget(m_ScriptList);
    ScriptsLayout->addWidget(m_Add, 0, Qt::AlignRight);
    QVBoxLayout * AutorunLayout = new QVBoxLayout(Autorun);
    AutorunLayout->addWidget(m_AutorunList);
    AutorunLayout->addWidget(m_Remove, 0, Qt::AlignRight);
    QGridLayout * Layout = new QGridLayout(this);
    Layout->addWidget(Scripts, 0, 0);
    Layout->addWidget(Autorun, 0, 1);
    Layout->addWidget(Ok, 1, 1, Qt::AlignRight);

    // Add is enabled for the list that has the focus (NM_SETFOCUS of the list views)
    m_Add->setEnabled(false);
    m_Remove->setEnabled(false);
    connect(m_ScriptList, &QListWidget::itemSelectionChanged, this, [this]() {
        m_Add->setEnabled(!m_ScriptList->selectedItems().isEmpty());
        m_Remove->setEnabled(false);
    });
    connect(m_AutorunList, &QListWidget::itemSelectionChanged, this, [this]() {
        m_Remove->setEnabled(!m_AutorunList->selectedItems().isEmpty());
        m_Add->setEnabled(false);
    });
    connect(m_Add, &QPushButton::clicked, this, &ScriptsAutorunDialog::AddSelected);
    connect(m_Remove, &QPushButton::clicked, this, &ScriptsAutorunDialog::RemoveSelected);
    connect(m_ScriptList, &QListWidget::itemDoubleClicked, this, [this]() { AddSelected(); });
    connect(m_AutorunList, &QListWidget::itemDoubleClicked, this, [this]() { RemoveSelected(); });
    connect(Ok, &QPushButton::clicked, this, &QDialog::accept);

    m_Debugger->ScriptAutorunLoad();
    RefreshLists();
    m_Watcher.addPath(QString::fromStdString(m_Debugger->ScriptsDir()));
    connect(&m_Watcher, &QFileSystemWatcher::directoryChanged, this, [this]() { RefreshLists(); });
}

void ScriptsAutorunDialog::RefreshLists()
{
    const std::set<std::string> Autorun = m_Debugger->ScriptAutorunList();
    auto Fill = [this](QListWidget * List, const QStringList & Names) {
        const int Row = List->currentRow();
        List->clear();
        List->addItems(Names);
        for (int i = 0; i < List->count(); i++)
        {
            if (List->item(i)->text() == m_InitSelected)
            {
                List->setCurrentRow(i);
                List->setFocus();
                m_InitSelected.clear();
                return;
            }
        }
        if (List->count() != 0 && Row != -1)
        {
            List->setCurrentRow(Row >= List->count() ? List->count() - 1 : Row);
        }
    };

    QStringList Available, Running;
    for (const QString & Name : ScriptFiles(QString::fromStdString(m_Debugger->ScriptsDir())))
    {
        if (Autorun.count(Name.toStdString()) == 0)
        {
            Available.push_back(Name);
        }
    }
    for (const std::string & Name : Autorun)
    {
        Running.push_back(QString::fromStdString(Name));
    }
    Running.sort(Qt::CaseInsensitive);
    Fill(m_ScriptList, Available);
    Fill(m_AutorunList, Running);
}

void ScriptsAutorunDialog::AddSelected()
{
    QListWidgetItem * Item = m_ScriptList->currentItem();
    if (Item == nullptr)
    {
        return;
    }
    std::set<std::string> Autorun = m_Debugger->ScriptAutorunList();
    Autorun.insert(Item->text().toStdString());
    m_Debugger->ScriptAutorunSave(Autorun);
    RefreshLists();
}

void ScriptsAutorunDialog::RemoveSelected()
{
    QListWidgetItem * Item = m_AutorunList->currentItem();
    if (Item == nullptr)
    {
        return;
    }
    std::set<std::string> Autorun = m_Debugger->ScriptAutorunList();
    Autorun.erase(Item->text().toStdString());
    m_Debugger->ScriptAutorunSave(Autorun);
    RefreshLists();
}

// ---- Scripts window ----

ScriptsView::ScriptsView(CDebuggerUI * Debugger) :
    DebugWindow(Debugger, DebuggerUI_ScriptsPos, true)
{
    setWindowTitle(Tr("Scripts"));
    resize(421 * 1.6, 216 * 1.6);

    QGroupBox * ScriptsGroup = new QGroupBox(Tr("Scripts"), this);
    QGroupBox * OutputGroup = new QGroupBox(Tr("Output"), this);

    // Status column ("-" stopped, "*" starting, ">" started) and the script name
    m_ScriptList = new QTableWidget(0, 2, ScriptsGroup);
    m_ScriptList->horizontalHeader()->hide();
    m_ScriptList->verticalHeader()->hide();
    m_ScriptList->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_ScriptList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_ScriptList->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_ScriptList->setShowGrid(false);
    m_ScriptList->setColumnWidth(0, 22);
    m_ScriptList->horizontalHeader()->setStretchLastSection(true);
    m_ScriptList->setContextMenuPolicy(Qt::CustomContextMenu);

    QPushButton * Directory = new QPushButton("...", ScriptsGroup);
    Directory->setToolTip(Tr("Open scripts directory in file explorer"));
    Directory->setMaximumWidth(40);
    m_Run = new QPushButton(Tr("Run"), ScriptsGroup);
    m_Run->setToolTip(Tr("Toggle selected script"));
    QHBoxLayout * ScriptButtons = new QHBoxLayout;
    ScriptButtons->addWidget(Directory);
    ScriptButtons->addStretch();
    ScriptButtons->addWidget(m_Run);
    QVBoxLayout * ScriptsLayout = new QVBoxLayout(ScriptsGroup);
    ScriptsLayout->addWidget(m_ScriptList);
    ScriptsLayout->addLayout(ScriptButtons);

    m_Output = new QPlainTextEdit(OutputGroup);
    m_Output->setReadOnly(true);
    m_Output->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_Output->setFont(MonoFont(false));
    m_Output->setStyleSheet("QPlainTextEdit { background-color: #222222; color: #EEEEEE; }");
    QPushButton * Copy = new QPushButton(Tr("Copy"), OutputGroup);
    Copy->setToolTip(Tr("Copy console output to the clipboard"));
    QPushButton * Clear = new QPushButton(Tr("Clear"), OutputGroup);
    Clear->setToolTip(Tr("Clear console output"));
    QHBoxLayout * OutputButtons = new QHBoxLayout;
    OutputButtons->addStretch();
    OutputButtons->addWidget(Copy);
    OutputButtons->addWidget(Clear);
    QVBoxLayout * OutputLayout = new QVBoxLayout(OutputGroup);
    OutputLayout->addWidget(m_Output);
    OutputLayout->addLayout(OutputButtons);

    QLabel * Prompt = new QLabel(">", this);
    Prompt->setFont(MonoFont(true));
    m_Input = new QLineEdit(this);
    m_Input->setFont(MonoFont(false));
    m_Input->setStyleSheet("QLineEdit { background-color: #222222; color: #EEEEEE; }");
    m_Input->setEnabled(false);
    m_Input->installEventFilter(this);
    QHBoxLayout * InputLayout = new QHBoxLayout;
    InputLayout->addWidget(Prompt);
    InputLayout->addWidget(m_Input);

    m_StatusLabel = new QLabel(this);
    m_StatusLabel->setFrameStyle(QFrame::Panel | QFrame::Sunken);

    QGridLayout * Layout = new QGridLayout(this);
    Layout->addWidget(ScriptsGroup, 0, 0);
    Layout->addWidget(OutputGroup, 0, 1);
    Layout->addLayout(InputLayout, 1, 0, 1, 2);
    Layout->addWidget(m_StatusLabel, 2, 0, 1, 2);
    Layout->setColumnStretch(0, 1);
    Layout->setColumnStretch(1, 3);

    connect(m_ScriptList, &QTableWidget::currentCellChanged, this, [this](int Row) {
        QTableWidgetItem * Item = m_ScriptList->item(Row, 1);
        if (Item != nullptr)
        {
            m_SelectedScript = Item->text();
            RefreshStatus();
        }
    });
    connect(m_ScriptList, &QTableWidget::cellDoubleClicked, this, [this]() { ToggleSelected(); });
    connect(m_ScriptList, &QTableWidget::customContextMenuRequested, this, &ScriptsView::ShowContextMenu);
    connect(Directory, &QPushButton::clicked, this, [this]() { QDesktopServices::openUrl(QUrl::fromLocalFile(ScriptsDir())); });
    connect(m_Run, &QPushButton::clicked, this, &ScriptsView::ToggleSelected);
    connect(Copy, &QPushButton::clicked, this, &ScriptsView::ConsoleCopy);
    connect(Clear, &QPushButton::clicked, this, [this]() { m_Debugger->ScriptConsoleClear(); });
    connect(m_Input, &QLineEdit::returnPressed, this, &ScriptsView::SubmitInput);

    // The window starts from the console of the script system (what was logged before it opened)
    m_Debugger->ScriptAutorunLoad();
    uint64_t Sequence = 0;
    const std::string Snapshot = m_Debugger->ScriptConsoleSnapshot(Sequence);
    m_BaseSequence = Sequence;
    m_Pending = Snapshot;
    std::string::size_type Pos = 0;
    while ((Pos = m_Pending.find("\r\n", Pos)) != std::string::npos)
    {
        m_Pending.erase(Pos, 1);
    }
    FlushConsole();

    RefreshList();

    m_FlushTimer = new QTimer(this);
    connect(m_FlushTimer, &QTimer::timeout, this, &ScriptsView::FlushConsole);
    m_FlushTimer->start(50);

    // The scripts directory is watched for scripts that appear and disappear
    m_Watcher.addPath(ScriptsDir());
    connect(&m_Watcher, &QFileSystemWatcher::directoryChanged, this, [this]() { RefreshList(); });

    LoadWindowPos();
}

ScriptsView::~ScriptsView()
{
}

QString ScriptsView::ScriptsDir() const
{
    return QString::fromStdString(m_Debugger->ScriptsDir());
}

void ScriptsView::ConsolePrint(uint64_t Sequence, const std::string & Text)
{
    if (Sequence <= m_BaseSequence)
    {
        return; // part of the snapshot the window started from
    }
    // the console of upstream uses CR LF
    std::string Line = Text;
    std::string::size_type Pos = 0;
    while ((Pos = Line.find("\r\n", Pos)) != std::string::npos)
    {
        Line.erase(Pos, 1);
    }
    m_Pending += Line;
}

void ScriptsView::ConsoleClear(uint64_t Sequence)
{
    if (Sequence <= m_BaseSequence)
    {
        return;
    }
    m_Pending.clear();
    m_ClearPending = true;
    // text logged before the clear that is still queued must not show up after it
    m_BaseSequence = Sequence;
}

// OnTimer(CONFLUSH_TIMER_ID): the text is appended, keeping the view at the end only when it
// was at the end
void ScriptsView::FlushConsole()
{
    if (m_ClearPending)
    {
        m_Output->clear();
        m_ClearPending = false;
    }
    if (m_Pending.empty())
    {
        return;
    }
    QScrollBar * Bar = m_Output->verticalScrollBar();
    const bool AtEnd = Bar->value() >= Bar->maximum();
    QTextCursor Cursor(m_Output->document());
    Cursor.movePosition(QTextCursor::End);
    Cursor.insertText(QString::fromStdString(m_Pending));
    m_Pending.clear();
    if (AtEnd)
    {
        Bar->setValue(Bar->maximum());
    }
}

bool ScriptsView::SelectScript(const QString & Name)
{
    for (int Row = 0; Row < m_ScriptList->rowCount(); Row++)
    {
        if (m_ScriptList->item(Row, 1)->text() == Name)
        {
            m_ScriptList->selectRow(Row);
            m_ScriptList->setCurrentCell(Row, 1);
            return true;
        }
    }
    return false;
}

// OnRefreshList
void ScriptsView::RefreshList()
{
    const QString Selected = m_SelectedScript;
    const int SelectedRow = m_ScriptList->currentRow();
    const std::set<std::string> Autorun = m_Debugger->ScriptAutorunList();
    const QStringList Names = ScriptFiles(ScriptsDir());

    m_ScriptList->setUpdatesEnabled(false);
    {
        // the selection changes while the rows are rebuilt
        QSignalBlocker Blocker(m_ScriptList);
        m_ScriptList->setRowCount(0);
        m_ScriptList->setRowCount(Names.size());
        for (int Row = 0; Row < Names.size(); Row++)
        {
            const std::string Name = Names[Row].toStdString();
            const CDebuggerUI::ScriptStatus Status = m_Debugger->GetScriptStatus(Name);
            QTableWidgetItem * StatusItem = new QTableWidgetItem(Status == CDebuggerUI::ScriptStarting ? "*" : Status == CDebuggerUI::ScriptStarted ? ">" : "-");
            QTableWidgetItem * NameItem = new QTableWidgetItem(Names[Row]);
            if (Autorun.count(Name) > 0)
            {
                NameItem->setForeground(QColor(0x00, 0x80, 0x00));
            }
            if (Status == CDebuggerUI::ScriptStarting)
            {
                StatusItem->setBackground(QColor(0xFF, 0xFF, 0xAA));
                NameItem->setBackground(QColor(0xFF, 0xFF, 0xAA));
            }
            else if (Status == CDebuggerUI::ScriptStarted)
            {
                StatusItem->setBackground(QColor(0xAA, 0xFF, 0xAA));
                NameItem->setBackground(QColor(0xAA, 0xFF, 0xAA));
            }
            if (Status != CDebuggerUI::ScriptStopped)
            {
                // the colors are light: the text must stay dark in a dark theme
                StatusItem->setForeground(Qt::black);
                if (Autorun.count(Name) == 0)
                {
                    NameItem->setForeground(Qt::black);
                }
            }
            m_ScriptList->setItem(Row, 0, StatusItem);
            m_ScriptList->setItem(Row, 1, NameItem);
        }
        int Row = Names.indexOf(Selected);
        if (Row < 0 && SelectedRow >= 0 && !Names.isEmpty())
        {
            Row = SelectedRow >= Names.size() ? Names.size() - 1 : SelectedRow;
        }
        if (Row >= 0)
        {
            m_ScriptList->setCurrentCell(Row, 1);
            m_ScriptList->selectRow(Row);
            m_SelectedScript = Names[Row];
        }
        else
        {
            m_SelectedScript.clear();
        }
    }
    m_ScriptList->setUpdatesEnabled(true);
    RefreshStatus();
}

void ScriptsView::RefreshStatus()
{
    if (m_SelectedScript.isEmpty())
    {
        m_StatusLabel->clear();
        m_Input->setEnabled(false);
        m_Run->setText(Tr("Run"));
        return;
    }
    const CDebuggerUI::ScriptStatus Status = m_Debugger->GetScriptStatus(m_SelectedScript.toStdString());
    QString Text = ScriptsDir() + "/" + m_SelectedScript;
    if (Status == CDebuggerUI::ScriptStarted)
    {
        Text += QLatin1Char(' ') + Tr("(Started)");
        if (!m_Input->isEnabled())
        {
            m_Input->setEnabled(true);
            m_Input->setFocus();
        }
    }
    else
    {
        if (Status == CDebuggerUI::ScriptStarting)
        {
            Text += QLatin1Char(' ') + Tr("(Starting)");
        }
        m_Input->setEnabled(false);
    }
    m_Run->setText(Status == CDebuggerUI::ScriptStopped ? Tr("Run") : Tr("Stop"));
    m_StatusLabel->setText(Text);
}

void ScriptsView::RunSelected()
{
    if (m_SelectedScript.isEmpty())
    {
        return;
    }
    m_Debugger->ScriptStart(m_SelectedScript.toStdString());
}

void ScriptsView::StopSelected()
{
    if (m_SelectedScript.isEmpty())
    {
        return;
    }
    m_Debugger->ScriptStop(m_SelectedScript.toStdString());
}

void ScriptsView::ToggleSelected()
{
    if (m_SelectedScript.isEmpty())
    {
        return;
    }
    if (m_Debugger->GetScriptStatus(m_SelectedScript.toStdString()) == CDebuggerUI::ScriptStopped)
    {
        RunSelected();
    }
    else
    {
        StopSelected();
    }
}

void ScriptsView::EditSelected()
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(ScriptsDir() + "/" + m_SelectedScript));
}

void ScriptsView::ConsoleCopy()
{
    QApplication::clipboard()->setText(m_Output->toPlainText());
}

// IDR_SCRIPT_POPUP
void ScriptsView::ShowContextMenu(const QPoint & Position)
{
    QTableWidgetItem * Item = m_ScriptList->itemAt(Position);
    if (Item == nullptr)
    {
        return;
    }
    m_ScriptList->setCurrentCell(Item->row(), 1);
    const CDebuggerUI::ScriptStatus Status = m_Debugger->GetScriptStatus(m_SelectedScript.toStdString());
    QMenu Menu(this);
    QAction * Run = Menu.addAction(Tr("Run"));
    QAction * Stop = Menu.addAction(Tr("Stop"));
    QAction * Edit = Menu.addAction(Tr("Edit"));
    Menu.addSeparator();
    QAction * Autorun = Menu.addAction(Tr("Autorun..."));
    Run->setEnabled(Status == CDebuggerUI::ScriptStopped);
    Stop->setEnabled(Status != CDebuggerUI::ScriptStopped);
    connect(Run, &QAction::triggered, this, &ScriptsView::RunSelected);
    connect(Stop, &QAction::triggered, this, &ScriptsView::StopSelected);
    connect(Edit, &QAction::triggered, this, &ScriptsView::EditSelected);
    connect(Autorun, &QAction::triggered, this, [this]() {
        ScriptsAutorunDialog(m_Debugger, m_SelectedScript, this).exec();
        RefreshList();
    });
    Menu.exec(m_ScriptList->viewport()->mapToGlobal(Position));
}

// OnInputSpecialKey: Return sends the code to the script, Up/Down walk the history
void ScriptsView::SubmitInput()
{
    const QString Code = m_Input->text();
    if (Code.isEmpty() || m_SelectedScript.isEmpty())
    {
        return;
    }
    m_Input->clear();
    m_Debugger->ScriptInput(m_SelectedScript.toStdString(), Code.toStdString());
    // a repeated entry moves to the end
    m_InputHistory.removeAll(Code);
    m_InputHistory.push_back(Code);
    m_InputHistoryIndex = m_InputHistory.size();
}

bool ScriptsView::eventFilter(QObject * Watched, QEvent * Event)
{
    if (Watched == m_Input && Event->type() == QEvent::KeyPress)
    {
        const int Key = static_cast<QKeyEvent *>(Event)->key();
        if (Key == Qt::Key_Up)
        {
            if (m_InputHistoryIndex > 0)
            {
                m_Input->setText(m_InputHistory[--m_InputHistoryIndex]);
                m_Input->end(false);
            }
            return true;
        }
        if (Key == Qt::Key_Down)
        {
            if (m_InputHistoryIndex < m_InputHistory.size())
            {
                m_InputHistoryIndex++;
            }
            if (m_InputHistoryIndex < m_InputHistory.size())
            {
                m_Input->setText(m_InputHistory[m_InputHistoryIndex]);
                m_Input->end(false);
            }
            else
            {
                m_Input->clear();
            }
            return true;
        }
    }
    return DebugWindow::eventFilter(Watched, Event);
}
