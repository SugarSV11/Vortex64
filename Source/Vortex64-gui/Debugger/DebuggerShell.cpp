#include "DebuggerShell.h"
#include "DebugWindow.h"
#include "../Vortex/Store.h"
#include "../Vortex/Strings.h"

#include <QCloseEvent>
#include <QJsonArray>
#include <QJsonObject>
#include <QTabWidget>
#include <QTimer>

namespace
{
struct TabInfo
{
    const char * ClassName;
    const char * Spanish;
    const char * English;
};
// The order of the tabs
const TabInfo TabTable[] = {
    {"CommandsView", "CPU", "CPU"},
    {"MemoryView", "Memoria", "Memory"},
    {"MemorySearchView", "Buscar", "Search"},
    {"MemoryDumpView", "Volcado", "Dump"},
    {"SymbolsView", "Símbolos", "Symbols"},
    {"StackView", "Pila", "Stack"},
    {"StackTraceView", "Traza de pila", "Stack trace"},
    {"TlbView", "TLB", "TLB"},
    {"ExceptionBreakpointsView", "Excepciones", "Exceptions"},
    {"DmaLogView", "Log DMA", "DMA log"},
    {"CpuLogView", "Log CPU", "CPU log"},
    {"ScriptsView", "Scripts", "Scripts"},
};

int OrderOf(const QObject * View)
{
    const QString Name = QString::fromLatin1(View->metaObject()->className());
    for (int i = 0; i < int(sizeof(TabTable) / sizeof(TabTable[0])); i++)
    {
        if (Name == QLatin1String(TabTable[i].ClassName))
        {
            return i;
        }
    }
    return 1000;
}
} // namespace

DebuggerShell & DebuggerShell::Instance()
{
    static DebuggerShell * Shell = new DebuggerShell;
    return *Shell;
}

DebuggerShell::DebuggerShell() :
    QMainWindow(nullptr)
{
    setObjectName("VxDebugger");
    setWindowTitle(QStringLiteral("Vortex64 - Debugger"));
    m_Tabs = new QTabWidget(this);
    m_Tabs->setObjectName("VxDebuggerTabs");
    m_Tabs->setDocumentMode(true);
    m_Tabs->setUsesScrollButtons(true);
    m_Tabs->setTabsClosable(true);
    connect(m_Tabs, &QTabWidget::tabCloseRequested, this, [this](int Index) {
        if (DebugWindow * View = qobject_cast<DebugWindow *>(m_Tabs->widget(Index)))
        {
            Dismiss(View);
        }
    });
    setCentralWidget(m_Tabs);
    resize(1120, 720);
}

bool DebuggerShell::Hosts(const DebugWindow * View) const
{
    return m_Tabs->indexOf(const_cast<DebugWindow *>(View)) >= 0;
}

void DebuggerShell::Present(DebugWindow * View)
{
    if (!Hosts(View))
    {
        QString Label = View->windowTitle();
        const QString Name = QString::fromLatin1(View->metaObject()->className());
        for (const TabInfo & Info : TabTable)
        {
            if (Name == QLatin1String(Info.ClassName))
            {
                Label = Vortex::Tr(Info.Spanish, Info.English);
            }
        }
        View->HostInShell();
        int Position = m_Tabs->count();
        for (int i = 0; i < m_Tabs->count(); i++)
        {
            if (OrderOf(m_Tabs->widget(i)) > OrderOf(View))
            {
                Position = i;
                break;
            }
        }
        m_Tabs->insertTab(Position, View, Label);
    }
    if (!m_Placed)
    {
        m_Placed = true;
        const QJsonObject Saved = Vortex::Store::Instance().Value("debugger.geometry").toObject();
        if (Saved.contains("w"))
        {
            setGeometry(Saved.value("x").toInt(), Saved.value("y").toInt(), Saved.value("w").toInt(), Saved.value("h").toInt());
        }
    }
    m_Tabs->setCurrentWidget(View);
    View->show();
    show();
    if (isMinimized())
    {
        showNormal();
    }
    raise();
    activateWindow();
}

void DebuggerShell::Dismiss(DebugWindow * View)
{
    const int Index = m_Tabs->indexOf(View);
    if (Index >= 0)
    {
        m_Tabs->removeTab(Index);
        View->HostedAway();
    }
    if (m_Tabs->count() == 0)
    {
        hide();
    }
}

void DebuggerShell::closeEvent(QCloseEvent * Event)
{
    SaveGeometryLater();
    QMainWindow::closeEvent(Event);
}

void DebuggerShell::moveEvent(QMoveEvent * Event)
{
    QMainWindow::moveEvent(Event);
    SaveGeometryLater();
}

void DebuggerShell::resizeEvent(QResizeEvent * Event)
{
    QMainWindow::resizeEvent(Event);
    SaveGeometryLater();
}

void DebuggerShell::SaveGeometryLater()
{
    if (!m_Placed || !isVisible())
    {
        return;
    }
    QJsonObject Geometry;
    Geometry.insert("x", geometry().x());
    Geometry.insert("y", geometry().y());
    Geometry.insert("w", geometry().width());
    Geometry.insert("h", geometry().height());
    Vortex::Store::Instance().SetValue("debugger.geometry", Geometry);
}
