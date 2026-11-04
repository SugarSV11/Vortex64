#include "Shell.h"
#include "../RomBrowser.h"
#include "Covers.h"
#include "Icons.h"
#include "Store.h"
#include "Strings.h"
#include "Theme.h"

#include <Project64-core/Settings.h>
#include <Project64-core/Settings/SettingsID.h>

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QDesktopServices>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QPainter>
#include <QPushButton>
#include <QShortcut>
#include <QStackedWidget>
#include <QTextEdit>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

namespace Vortex
{
// The search box of the header: the magnifier is painted inside the field
class SearchEdit : public QLineEdit
{
public:
    explicit SearchEdit(QWidget * Parent) :
        QLineEdit(Parent)
    {
        setTextMargins(26, 0, 0, 0);
    }

protected:
    void paintEvent(QPaintEvent * Event) override
    {
        QLineEdit::paintEvent(Event);
        QPainter P(this);
        P.drawPixmap(QPoint(12, (height() - 16) / 2), IconPixmap(Icon::Search, T().TextSecondary, 16, devicePixelRatioF()));
    }
};

// ====================================================================== BrowsePage

BrowsePage::BrowsePage(const QString & Id, LibraryModel * Model, LibraryProxy::Scope Scope, QWidget * Parent) :
    QWidget(Parent),
    m_Id(Id),
    m_Model(Model)
{
    setObjectName("VxPage_" + Id);
    m_Proxy = new LibraryProxy(Scope, this);
    m_Proxy->setSourceModel(Model);
    if (Scope != LibraryProxy::Recents)
    {
        m_Proxy->SetSortKey(Store::Instance().SortKey(), Store::Instance().SortAscending());
    }
    m_Browser = new GameBrowser(m_Proxy, this);
    m_Detail = new DetailPanel(Model, this);
    if (Scope != LibraryProxy::Recents)
    {
        // (turning on the sorting of the list's header resets the proxy's sort to the header's column)
        m_Proxy->SetSortKey(Store::Instance().SortKey(), Store::Instance().SortAscending());
    }

    QVBoxLayout * Layout = new QVBoxLayout(this);
    Layout->setContentsMargins(0, 0, 0, 0);
    Layout->setSpacing(0);
    QWidget * Body = new QWidget(this);
    QVBoxLayout * BodyLayout = new QVBoxLayout(Body);
    BodyLayout->setContentsMargins(14, 0, 8, 8);
    BodyLayout->addWidget(m_Browser);
    Layout->addWidget(Body, 1);
    Layout->addWidget(m_Detail);

    connect(m_Browser, &GameBrowser::Selected, this, [this](const QString & Path) {
        m_Detail->SetRom(Path);
        if (!Path.isEmpty())
        {
            Store::Instance().SetLastSelected(Path);
        }
    });
    connect(m_Browser, &GameBrowser::Activated, this, &BrowsePage::StartRequested);
    connect(m_Detail, &DetailPanel::StartRequested, this, &BrowsePage::StartRequested);
    connect(m_Browser, &GameBrowser::ContextMenuRequested, this, &BrowsePage::ContextMenuRequested);
    connect(m_Browser, &GameBrowser::ActionRequested, this, &BrowsePage::AddDirectoryRequested);
    // The sort the user chooses in the header of the list is remembered (library and favourites)
    if (Scope != LibraryProxy::Recents)
    {
        connect(m_Browser->ListView()->header(), &QHeaderView::sortIndicatorChanged, this, [this](int Column, Qt::SortOrder Order) {
            static const char * const Keys[] = {"name", "region", "size", "name", "recent", "played", "name"};
            if (Column >= 0 && Column < 7)
            {
                Store::Instance().SetSort(Keys[Column], Order == Qt::AscendingOrder);
            }
        });
    }
    m_Browser->ListView()->header()->setSortIndicator(m_Proxy->sortColumn(), m_Proxy->sortOrder());
    SetMode(Store::Instance().Value("view." + Id, Scope == LibraryProxy::Recents ? "list" : "grid").toString());
    if (Scope == LibraryProxy::Recents)
    {
        // the table of the history: name, region, last time, play time
        m_Browser->ListView()->setColumnHidden(LibraryModel::ColSize, true);
        m_Browser->ListView()->setColumnHidden(LibraryModel::ColFormat, true);
    }
}

void BrowsePage::SetFilter(const QString & Text)
{
    m_FilterText = Text;
    m_Proxy->SetText(Text);
}

void BrowsePage::SetMode(const QString & Mode)
{
    m_Browser->SetMode(Mode);
    Store::Instance().SetValue("view." + m_Id, Mode);
}

void BrowsePage::RefreshEmptyText(bool HasDirectories, bool Scanning)
{
    if (!m_FilterText.isEmpty())
    {
        m_Browser->SetEmpty(Tr("Sin resultados", "No results"), Tr("Ningún juego coincide con «%1».", "No game matches “%1”.").arg(m_FilterText));
        return;
    }
    if (Scanning)
    {
        m_Browser->SetEmpty(Tr("Buscando juegos…", "Looking for games…"), Tr("Esto puede tardar unos segundos la primera vez.", "This can take a few seconds the first time."));
        return;
    }
    if (m_Id == "favorites")
    {
        m_Browser->SetEmpty(Tr("Aún no hay favoritos", "No favorites yet"), Tr("Marca un juego con la estrella ☆ para verlo aquí.", "Mark a game with the star ☆ to see it here."));
    }
    else if (m_Id == "recent")
    {
        m_Browser->SetEmpty(Tr("Aún no has jugado a nada", "You have not played anything yet"), Tr("Los juegos que inicies aparecerán aquí.", "The games you start will show up here."));
    }
    else if (!HasDirectories)
    {
        m_Browser->SetEmpty(Tr("Tu biblioteca está vacía", "Your library is empty"), Tr("Añade la carpeta donde guardas tus ROMs de N64.", "Add the folder where you keep your N64 ROMs."), Tr("Añadir carpeta de ROMs…", "Add ROM folder…"));
    }
    else
    {
        m_Browser->SetEmpty(Tr("No se encontraron juegos", "No games found"), Tr("Las carpetas configuradas no contienen ROMs de N64. Puedes añadir otra carpeta o volver a escanear.", "The configured folders hold no N64 ROMs. You can add another folder or rescan."), Tr("Añadir carpeta de ROMs…", "Add ROM folder…"));
    }
}

// ====================================================================== DisksPage

DisksPage::DisksPage(LibraryModel * Model, QWidget * Parent) :
    QWidget(Parent),
    m_Model(Model)
{
    setObjectName("VxPage_disks");
    m_Proxy = new LibraryProxy(LibraryProxy::Disks, this);
    m_Proxy->setSourceModel(Model);
    m_Proxy->SetSortKey("name", true);
    m_Browser = new GameBrowser(m_Proxy, this);
    m_Browser->SetMode("compact");
    // the list of disks has no header
    m_Browser->ListView()->setHeaderHidden(true);
    m_Browser->ListView()->setColumnHidden(LibraryModel::ColSize, true);
    m_Browser->ListView()->setColumnHidden(LibraryModel::ColFormat, true);
    m_Browser->ListView()->setColumnHidden(LibraryModel::ColLast, true);
    m_Browser->ListView()->setColumnHidden(LibraryModel::ColPlayed, true);
    m_Browser->ListView()->setColumnHidden(LibraryModel::ColFavorite, true);
    m_Browser->ListView()->header()->resizeSection(LibraryModel::ColRegion, 80);

    QHBoxLayout * Root = new QHBoxLayout(this);
    Root->setContentsMargins(14, 0, 14, 14);
    Root->setSpacing(14);

    QWidget * Left = new QWidget(this);
    SetRole(Left, "panel");
    Left->setAttribute(Qt::WA_StyledBackground, true);
    Left->setMinimumWidth(260);
    Left->setMaximumWidth(380);
    QVBoxLayout * LeftLayout = new QVBoxLayout(Left);
    LeftLayout->setContentsMargins(12, 12, 12, 12);
    QLabel * LeftTitle = new QLabel(Tr("Discos detectados", "Detected disks"), Left);
    SetRole(LeftTitle, "muted");
    LeftLayout->addWidget(LeftTitle);
    LeftLayout->addWidget(m_Browser, 1);

    QWidget * Right = new QWidget(this);
    SetRole(Right, "panel");
    Right->setAttribute(Qt::WA_StyledBackground, true);
    QVBoxLayout * RightLayout = new QVBoxLayout(Right);
    RightLayout->setContentsMargins(18, 14, 18, 14);
    RightLayout->setSpacing(10);
    QLabel * Title = new QLabel(Tr("Información del disco", "Disk information"), Right);
    SetRole(Title, "muted");
    RightLayout->addWidget(Title);

    QHBoxLayout * Info = new QHBoxLayout;
    Info->setSpacing(16);
    m_Cover = new QLabel(Right);
    m_Cover->setFixedSize(112, 112);
    QVBoxLayout * Text = new QVBoxLayout;
    Text->setSpacing(4);
    m_Name = new QLabel(Right);
    QFont Font = m_Name->font();
    Font.setPixelSize(17);
    Font.setWeight(QFont::DemiBold);
    m_Name->setFont(Font);
    m_Name->setWordWrap(true);
    m_Region = new QLabel(Right);
    m_Size = new QLabel(Right);
    m_Type = new QLabel(Right);
    m_State = new QLabel(Right);
    for (QLabel * L : {m_Region, m_Size, m_Type, m_State})
    {
        SetRole(L, "muted");
    }
    m_Name->setObjectName("VxDiskName");
    m_Region->setObjectName("VxDiskRegion");
    m_Size->setObjectName("VxDiskSize");
    m_Type->setObjectName("VxDiskType");
    m_State->setObjectName("VxDiskState");
    Text->addWidget(m_Name);
    Text->addWidget(m_Region);
    Text->addWidget(m_Size);
    Text->addWidget(m_Type);
    Text->addWidget(m_State);
    Text->addStretch(1);
    Info->addWidget(m_Cover, 0, Qt::AlignTop);
    Info->addLayout(Text, 1);
    RightLayout->addLayout(Info);

    QHBoxLayout * Buttons = new QHBoxLayout;
    m_Mount = new QPushButton(Tr("Montar disco", "Mount disk"), Right);
    m_Mount->setObjectName("VxDiskMount");
    m_Explore = new QPushButton(Tr("Explorar contenidos", "Browse contents"), Right);
    m_Explore->setObjectName("VxDiskExplore");
    m_Settings = new QPushButton(Tr("Ajustes de 64DD…", "64DD settings…"), Right);
    m_Settings->setObjectName("VxDiskSettings");
    Buttons->addWidget(m_Mount, 1);
    Buttons->addWidget(m_Explore, 1);
    Buttons->addWidget(m_Settings, 1);
    RightLayout->addLayout(Buttons);

    QLabel * NotesTitle = new QLabel(Tr("Notas", "Notes"), Right);
    SetRole(NotesTitle, "muted");
    RightLayout->addWidget(NotesTitle);
    m_Notes = new QTextEdit(Right);
    m_Notes->setReadOnly(true);
    m_Notes->setObjectName("VxDiskNotes");
    RightLayout->addWidget(m_Notes, 1);

    Root->addWidget(Left, 0);
    Root->addWidget(Right, 1);

    connect(m_Browser, &GameBrowser::Selected, this, &DisksPage::ShowDisk);
    connect(m_Browser, &GameBrowser::Activated, this, &DisksPage::MountRequested);
    connect(m_Browser, &GameBrowser::ContextMenuRequested, this, &DisksPage::ContextMenuRequested);
    connect(m_Mount, &QPushButton::clicked, this, [this]() {
        if (!m_Path.isEmpty())
        {
            Q_EMIT MountRequested(m_Path);
        }
    });
    connect(m_Explore, &QPushButton::clicked, this, [this]() {
        if (!m_Path.isEmpty())
        {
            Q_EMIT ExploreRequested(m_Path);
        }
    });
    connect(m_Settings, &QPushButton::clicked, this, &DisksPage::DiskSettingsRequested);
    connect(&Covers::Instance(), &Covers::CoverReady, this, [this](const QString & Rom) {
        if (Rom == m_Path)
        {
            ShowDisk(m_Path);
        }
    });
    m_Browser->SetEmpty(Tr("No hay discos 64DD", "No 64DD disks"), Tr("Los discos (.ndd, .d64) de tus carpetas de ROMs aparecerán aquí.", "The disks (.ndd, .d64) in your ROM folders will show up here."));
    ShowDisk(QString());
}

void DisksPage::SetFilter(const QString & Text)
{
    m_Proxy->SetText(Text);
}

QString DisksPage::SelectedDisk() const
{
    return m_Path;
}

void DisksPage::Refresh()
{
    ShowDisk(m_Browser->CurrentPath());
}

void DisksPage::ShowDisk(const QString & Path)
{
    m_Path = Path;
    const Entry * E = Path.isEmpty() ? nullptr : m_Model->Find(Path);
    m_Mount->setEnabled(E != nullptr);
    m_Explore->setEnabled(E != nullptr);
    if (E == nullptr)
    {
        m_Cover->setPixmap(Covers::Placeholder(QStringLiteral("64DD"), m_Cover->size(), devicePixelRatioF()));
        m_Name->setText(Tr("Ningún disco seleccionado", "No disk selected"));
        m_Region->clear();
        m_Size->clear();
        m_Type->clear();
        m_State->clear();
        m_Notes->clear();
        return;
    }
    m_Cover->setPixmap(Covers::Instance().Cover(*E, m_Cover->size(), devicePixelRatioF()));
    m_Name->setText(LibraryModel::DisplayName(*E));
    m_Region->setText(Tr("Región: ", "Region: ") + (RegionName(E->Country) == QStringLiteral("—") ? Tr("desconocida", "unknown") : RegionLong(E->Country) + QStringLiteral(" (") + RegionName(E->Country) + QLatin1Char(')')));
    m_Size->setText(Tr("Tamaño: ", "Size: ") + LibraryModel::SizeText(E->Size));
    m_Type->setText(Tr("Tipo: ", "Type: ") + (E->Media == 'E' ? QStringLiteral("64DD Disk (Expansion)") : QStringLiteral("64DD Disk")) + (E->Format.isEmpty() ? QString() : QStringLiteral("  ") + E->Format));
    // the IPL (64DD BIOS) is what a disk needs to run
    const bool Ipl = !g_Settings->LoadStringVal(File_DiskIPLPath).empty() || !g_Settings->LoadStringVal(File_DiskIPLUSAPath).empty() || !g_Settings->LoadStringVal(File_DiskIPLTOOLPath).empty();
    m_State->setText(Tr("BIOS 64DD (IPL): ", "64DD BIOS (IPL): ") + (Ipl ? Tr("configurada", "configured") : Tr("sin configurar", "not configured")));
    QStringList Notes;
    if (!E->Status.isEmpty() && !E->Status.startsWith('#'))
    {
        Notes << Tr("Estado en la base de datos: ", "Database status: ") + E->Status;
    }
    if (!E->Notes.isEmpty())
    {
        Notes << E->Notes;
    }
    if (!Ipl)
    {
        Notes << Tr("Requiere la BIOS del 64DD (IPL). Configúrala en Ajustes → Avanzado → 64DD.", "Requires the 64DD BIOS (IPL). Set it in Settings → Advanced → 64DD.");
    }
    m_Notes->setPlainText(Notes.join("\n\n"));
}

// ====================================================================== Shell

Shell::Shell(QtRomBrowser * Source, QWidget * Parent) :
    QWidget(Parent),
    m_Source(nullptr)
{
    setObjectName("VxRoot");
    setAttribute(Qt::WA_StyledBackground, true);
    m_Model = new LibraryModel(this);

    QHBoxLayout * Root = new QHBoxLayout(this);
    Root->setContentsMargins(0, 0, 0, 0);
    Root->setSpacing(0);
    m_Sidebar = new Sidebar(this);
    Root->addWidget(m_Sidebar);

    QWidget * Right = new QWidget(this);
    QVBoxLayout * RightLayout = new QVBoxLayout(Right);
    RightLayout->setContentsMargins(0, 0, 0, 0);
    RightLayout->setSpacing(0);

    // the header: search and view buttons
    QWidget * Header = new QWidget(Right);
    Header->setFixedHeight(T().HeaderHeight + 8);
    QHBoxLayout * HeaderLayout = new QHBoxLayout(Header);
    HeaderLayout->setContentsMargins(14, 10, 14, 6);
    HeaderLayout->setSpacing(10);
    m_Search = new SearchEdit(Header);
    m_Search->setObjectName("VxSearch");
    m_Search->setProperty("vx", "search");
    m_Search->setClearButtonEnabled(true);
    m_Search->setPlaceholderText(Tr("Buscar ROMs...", "Search ROMs..."));
    m_Search->setMaximumWidth(460);
    m_ScanLabel = new QLabel(Header);
    SetRole(m_ScanLabel, "muted");
    m_ScanLabel->setObjectName("VxScanLabel");
    m_ViewButtons = new QWidget(Header);
    QHBoxLayout * ViewLayout = new QHBoxLayout(m_ViewButtons);
    ViewLayout->setContentsMargins(0, 0, 0, 0);
    ViewLayout->setSpacing(2);
    auto MakeView = [&](const char * Name, Icon I, const QString & Tip) {
        QToolButton * B = new QToolButton(m_ViewButtons);
        B->setObjectName(Name);
        B->setProperty("vx", "view");
        B->setCheckable(true);
        B->setAutoExclusive(true);
        B->setIcon(MakeIcon(I, 18));
        B->setIconSize(QSize(18, 18));
        B->setToolTip(Tip);
        B->setCursor(Qt::PointingHandCursor);
        ViewLayout->addWidget(B);
        return B;
    };
    m_Grid = MakeView("VxViewGrid", Icon::ViewGrid, Tr("Cuadrícula", "Grid"));
    m_List = MakeView("VxViewList", Icon::ViewList, Tr("Lista", "List"));
    m_Compact = MakeView("VxViewCompact", Icon::ViewCompact, Tr("Compacta", "Compact"));
    // open a file that is not in the library, scan the library again
    auto MakeTool = [&](const char * Name, Icon I, const QString & Tip) {
        QToolButton * B = new QToolButton(Header);
        B->setObjectName(Name);
        B->setProperty("vx", "view");
        B->setIcon(MakeIcon(I, 18));
        B->setIconSize(QSize(18, 18));
        B->setToolTip(Tip);
        B->setCursor(Qt::PointingHandCursor);
        return B;
    };
    QToolButton * OpenRom = MakeTool("VxOpenRom", Icon::Folder, Tr("Abrir una ROM o un disco… (Ctrl+O)", "Open a ROM or a disk… (Ctrl+O)"));
    QToolButton * Rescan = MakeTool("VxRescan", Icon::Refresh, Tr("Volver a escanear la biblioteca (F5)", "Scan the library again (F5)"));
    connect(OpenRom, &QToolButton::clicked, this, &Shell::OpenRomRequested);
    connect(Rescan, &QToolButton::clicked, this, &Shell::RefreshRequested);
    HeaderLayout->addWidget(m_Search, 1);
    HeaderLayout->addWidget(m_ScanLabel);
    HeaderLayout->addWidget(Rescan);
    HeaderLayout->addWidget(OpenRom);
    HeaderLayout->addStretch(0);
    HeaderLayout->addWidget(m_ViewButtons);
    RightLayout->addWidget(Header);

    m_Pages = new QStackedWidget(Right);
    m_Library = new BrowsePage("library", m_Model, LibraryProxy::Games, m_Pages);
    m_Recent = new BrowsePage("recent", m_Model, LibraryProxy::Recents, m_Pages);
    m_Favorites = new BrowsePage("favorites", m_Model, LibraryProxy::Favorites, m_Pages);
    m_Disks = new DisksPage(m_Model, m_Pages);
    m_Pages->addWidget(m_Library);
    m_Pages->addWidget(m_Recent);
    m_Pages->addWidget(m_Favorites);
    m_Pages->addWidget(m_Disks);
    RightLayout->addWidget(m_Pages, 1);
    Root->addWidget(Right, 1);

    // sidebar
    connect(m_Sidebar, &Sidebar::PageRequested, this, &Shell::ShowPage);
    connect(m_Sidebar, &Sidebar::SettingsRequested, this, &Shell::SettingsRequested);
    connect(m_Sidebar, &Sidebar::DebuggerRequested, this, &Shell::DebuggerRequested);
    connect(m_Sidebar, &Sidebar::AboutRequested, this, &Shell::AboutRequested);

    // search and views
    connect(m_Search, &QLineEdit::textChanged, this, &Shell::SetFilter);
    connect(m_Grid, &QToolButton::clicked, this, [this]() { ApplyViewMode("grid"); });
    connect(m_List, &QToolButton::clicked, this, [this]() { ApplyViewMode("list"); });
    connect(m_Compact, &QToolButton::clicked, this, [this]() { ApplyViewMode("compact"); });

    for (BrowsePage * Page : {m_Library, m_Recent, m_Favorites})
    {
        connect(Page, &BrowsePage::StartRequested, this, &Shell::PlayRequested);
        connect(Page, &BrowsePage::ContextMenuRequested, this, [this, Page](const QString & Path, const QPoint & Pos) { BuildContextMenu(Path, Pos, Page == m_Recent); });
        connect(Page, &BrowsePage::AddDirectoryRequested, this, &Shell::AddDirectoryRequested);
    }
    connect(m_Disks, &DisksPage::MountRequested, this, &Shell::MountDiskRequested);
    connect(m_Disks, &DisksPage::ExploreRequested, this, [](const QString & Path) { QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(Path.section('?', 0, 0)).absolutePath())); });
    connect(m_Disks, &DisksPage::DiskSettingsRequested, this, &Shell::DiskSettingsRequested);
    connect(m_Disks, &DisksPage::ContextMenuRequested, this, [this](const QString & Path, const QPoint & Pos) { BuildContextMenu(Path, Pos, false); });

    connect(m_Model, &LibraryModel::ScanStateChanged, this, [this](bool Scanning, int Count) {
        m_ScanLabel->setText(Scanning ? Tr("Escaneando… %1 juegos", "Scanning… %1 games").arg(Count) : QString());
        UpdateEmptyTexts();
        if (!Scanning)
        {
            // the selection of the last session comes back once the list is there
            const QString Last = Store::Instance().LastSelected();
            if (!Last.isEmpty() && m_Library->SelectedRom().isEmpty() && m_Model->Find(Last))
            {
                SelectRom(Last);
            }
            for (BrowsePage * Page : {m_Library, m_Recent, m_Favorites})
            {
                Page->Browser()->SelectFirstIfNone();
            }
            m_Disks->Browser()->SelectFirstIfNone();
            m_Disks->Refresh();
        }
    });
    connect(&Store::Instance(), &Store::DirectoriesChanged, this, &Shell::UpdateEmptyTexts);
    connect(&Store::Instance(), &Store::HistoryChanged, this, [this]() { m_Recent->Proxy()->invalidate(); });
    connect(&Store::Instance(), &Store::FavoritesChanged, this, [this]() { m_Favorites->Proxy()->invalidate(); });

    // keyboard: Ctrl+F searches, F5 scans again
    QShortcut * Find = new QShortcut(QKeySequence::Find, this);
    Find->setContext(Qt::WidgetWithChildrenShortcut);
    connect(Find, &QShortcut::activated, this, [this]() {
        m_Search->setFocus();
        m_Search->selectAll();
    });
    QShortcut * Refresh = new QShortcut(QKeySequence(Qt::Key_F5), this);
    Refresh->setContext(Qt::WidgetWithChildrenShortcut);
    connect(Refresh, &QShortcut::activated, this, &Shell::RefreshRequested);

    if (Source != nullptr)
    {
        SetSource(Source);
    }
    m_Search->setText(Store::Instance().Filter());
    ShowPage(Store::Instance().Page());
    UpdateEmptyTexts();
}

void Shell::SetSource(QtRomBrowser * Source)
{
    m_Source = Source;
    m_Model->SetSource(Source);
    UpdateEmptyTexts();
}

BrowsePage * Shell::Page(const QString & Id) const
{
    if (Id == "library") return m_Library;
    if (Id == "recent") return m_Recent;
    if (Id == "favorites") return m_Favorites;
    return nullptr;
}

QString Shell::CurrentPage() const
{
    return m_Sidebar->Current();
}

void Shell::ShowPage(const QString & Page)
{
    QString Id = Page;
    QWidget * Widget = Id == "recent" ? static_cast<QWidget *>(m_Recent) : Id == "favorites" ? static_cast<QWidget *>(m_Favorites) : Id == "disks" ? static_cast<QWidget *>(m_Disks) : static_cast<QWidget *>(m_Library);
    if (Widget == m_Library)
    {
        Id = "library";
    }
    m_Pages->setCurrentWidget(Widget);
    if (BrowsePage * P = this->Page(Id))
    {
        P->Browser()->SelectFirstIfNone();
    }
    else
    {
        m_Disks->Browser()->SelectFirstIfNone();
    }
    m_Sidebar->SetCurrent(Id);
    Store::Instance().SetPage(Id);
    UpdateHeader();
    Q_EMIT PageChanged(Id);
}

void Shell::UpdateHeader()
{
    const QString Id = CurrentPage();
    const bool Browse = Id != "disks";
    m_ViewButtons->setVisible(Browse);
    if (BrowsePage * Page = this->Page(Id))
    {
        const QString Mode = Page->Mode();
        m_Grid->setChecked(Mode == "grid");
        m_List->setChecked(Mode == "list");
        m_Compact->setChecked(Mode == "compact");
    }
}

void Shell::ApplyViewMode(const QString & Mode)
{
    if (BrowsePage * Page = this->Page(CurrentPage()))
    {
        Page->SetMode(Mode);
    }
    UpdateHeader();
}

QToolButton * Shell::ViewButton(const QString & Mode) const
{
    return Mode == "grid" ? m_Grid : Mode == "list" ? m_List : m_Compact;
}

void Shell::SetFilter(const QString & Text)
{
    Store::Instance().SetFilter(Text);
    m_Library->SetFilter(Text);
    m_Recent->SetFilter(Text);
    m_Favorites->SetFilter(Text);
    m_Disks->SetFilter(Text);
    UpdateEmptyTexts();
}

void Shell::UpdateEmptyTexts()
{
    bool Directories = false;
    if (g_Settings != nullptr)
    {
        Directories = !g_Settings->LoadStringVal(RomList_GameDir).empty() || !Store::Instance().ExtraDirectories().isEmpty();
    }
    const bool Scanning = m_Model->Scanning();
    m_Library->RefreshEmptyText(Directories, Scanning);
    m_Recent->RefreshEmptyText(Directories, Scanning);
    m_Favorites->RefreshEmptyText(Directories, Scanning);
}

QString Shell::SelectedRom() const
{
    const QString Id = CurrentPage();
    if (Id == "disks")
    {
        return m_Disks->SelectedDisk();
    }
    if (BrowsePage * Page = this->Page(Id))
    {
        return Page->SelectedRom();
    }
    return QString();
}

void Shell::SelectRom(const QString & Path)
{
    for (BrowsePage * Page : {m_Library, m_Recent, m_Favorites})
    {
        Page->Browser()->SelectPath(Path);
    }
    m_Disks->Browser()->SelectPath(Path);
}

void Shell::keyPressEvent(QKeyEvent * Event)
{
    if (Event->key() == Qt::Key_Escape && m_Search->hasFocus() && !m_Search->text().isEmpty())
    {
        m_Search->clear();
        return;
    }
    QWidget::keyPressEvent(Event);
}

// The pop-up menu of CRomBrowser::RomList_PopupMenu with the entries of Vortex64
void Shell::BuildContextMenu(const QString & Path, const QPoint & GlobalPos, bool InRecents)
{
    const Entry * E = m_Model->Find(Path);
    if (E == nullptr || g_Settings == nullptr)
    {
        return;
    }
    const bool BasicMode = g_Settings->LoadBool(UserInterface_BasicMode);
    const bool CheatsRemembered = !BasicMode && g_Settings->LoadBool(Setting_RememberCheats);
    const bool Enhancement = !BasicMode && g_Settings->LoadBool(Setting_Enhancement);
    QMenu Menu(this);
    Menu.setObjectName("VxContextMenu");
    // (by value: a rescan can reset the model while the menu is open)
    const bool IsDisk = E->IsDisk;
    Menu.addAction(MakeIcon(Icon::Play, 16), IsDisk ? Tr("Montar y ejecutar", "Mount and run") : Tr("Iniciar", "Start"), this, [this, Path, IsDisk]() {
        if (IsDisk) Q_EMIT MountDiskRequested(Path); else Q_EMIT PlayRequested(Path);
    });
    if (!IsDisk)
    {
        Menu.addAction(MakeIcon(Icon::Disk, 16), Tr("Iniciar con disco 64DD…", "Start with a 64DD disk…"), this, [this, Path]() { Q_EMIT PlayWithDiskRequested(Path); });
    }
    Menu.addSeparator();
    const bool Fav = Store::Instance().IsFavorite(Path);
    Menu.addAction(MakeIcon(Fav ? Icon::StarOutline : Icon::Star, 16), Fav ? Tr("Quitar de favoritos", "Remove from favorites") : Tr("Añadir a favoritos", "Add to favorites"), this, [Path, Fav]() { Store::Instance().SetFavorite(Path, !Fav); });
    if (InRecents)
    {
        Menu.addAction(MakeIcon(Icon::Trash, 16), Tr("Quitar de recientes", "Remove from recent"), this, [Path]() { Store::Instance().ForgetRecent(Path); });
    }
    Menu.addSeparator();
    Menu.addAction(MakeIcon(Icon::Info, 16), Tr("Información de la ROM", "ROM information"), this, [this, Path]() { Q_EMIT RomInfoRequested(Path); });
    if (!BasicMode)
    {
        Menu.addAction(MakeIcon(Icon::Settings, 16), Tr("Ajustes del juego…", "Game settings…"), this, [this, Path]() { Q_EMIT EditSettingsRequested(Path); });
    }
    if (CheatsRemembered)
    {
        Menu.addAction(MakeIcon(Icon::Cheat, 16), Tr("Trucos…", "Cheats…"), this, [this, Path]() { Q_EMIT EditCheatsRequested(Path); });
    }
    if (Enhancement)
    {
        Menu.addAction(MakeIcon(Icon::Plugin, 16), Tr("Mejoras…", "Enhancements…"), this, [this, Path]() { Q_EMIT EditEnhancementsRequested(Path); });
    }
    Menu.addSeparator();
    Menu.addAction(MakeIcon(Icon::Folder, 16), Tr("Mostrar en la carpeta", "Show in folder"), this, [Path]() { QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(Path.section('?', 0, 0)).absolutePath())); });
    Menu.exec(GlobalPos);
}
} // namespace Vortex
