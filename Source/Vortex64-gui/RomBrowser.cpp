#include "RomBrowser.h"

#include <Vortex64-core/Settings.h>
#include <Common/md5.h>
#include <Common/File.h>

#include <QAction>
#include <QDirIterator>
#include <QDir>
#include <QBrush>
#include <QColor>
#include <QCoreApplication>
#include <QContextMenuEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QFileInfo>
#include <QHeaderView>
#include <QMenu>
#include <QMetaObject>
#include <QStringDecoder>
#include <QThread>
#include <algorithm>
#include <cstring>
#include <strings.h>

std::string QtRomBrowser::m_UnknownGoodName;

namespace
{
// CRomList calls back from its refresh thread (the caller waits, like
// SendMessage on Windows); when it is already the GUI thread a queued call
// would deadlock.
Qt::ConnectionType CallType(const QObject * Object)
{
    return QThread::currentThread() == Object->thread() ? Qt::DirectConnection : Qt::BlockingQueuedConnection;
}

QString Gs(LanguageStringID ID)
{
    return QString::fromUtf8(GS(ID));
}

int Compare(const char * A, const char * B)
{
    return strcasecmp(A, B);
}
} // namespace

QtRomBrowser::QtRomBrowser(QWidget * parent) :
    QTableWidget(parent),
    m_UpdatingColumns(false)
{
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    setShowGrid(false);
    verticalHeader()->hide();
    horizontalHeader()->setSectionsMovable(true);
    horizontalHeader()->setSectionsClickable(true);
    horizontalHeader()->setStretchLastSection(false);

    connect(horizontalHeader(), &QHeaderView::sectionClicked, this, &QtRomBrowser::OnHeaderClicked);
    connect(horizontalHeader(), &QHeaderView::sectionMoved, this, [this]() { OnSectionMoved(); });
    connect(horizontalHeader(), &QHeaderView::sectionResized, this, &QtRomBrowser::OnSectionResized);
    connect(this, &QTableWidget::cellDoubleClicked, this, [this](int Row, int) { OnItemActivated(Row); });

    ReloadColumns();
    // The "ROM selection" settings page flips this flag after changing the
    // fields (COptionsGameBrowserPage::ApplySettings).
    g_Settings->RegisterChangeCB((SettingID)RomBrowser_ColoumnsChanged, this, ColumnsChangedCB);

    // a change of the directory is acted on once it has stopped changing (a copy in progress)
    m_WatchTimer.setSingleShot(true);
    m_WatchTimer.setInterval(500);
    connect(&m_WatchTimer, &QTimer::timeout, this, &QtRomBrowser::CheckDirectory);
    connect(&m_Watcher, &QFileSystemWatcher::directoryChanged, this, [this]() {
        WatchDirectories(); // new subdirectories
        m_WatchTimer.start();
    });
}

void QtRomBrowser::ColumnsChangedCB(void * Data)
{
    static_cast<QtRomBrowser *>(Data)->ReloadColumns();
}

QtRomBrowser::~QtRomBrowser()
{
    m_Destroying = true;
    m_StopRefresh = true;
    // a callback of the refresh thread that is already waiting for this thread is served
    for (int i = 0; m_InCallback > 0 && i < 400; i++)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
    WatchStop();
    if (m_CheckThread != nullptr)
    {
        m_CheckThread->wait();
        delete m_CheckThread;
        m_CheckThread = nullptr;
    }
    if (g_Settings != nullptr)
    {
        g_Settings->UnregisterChangeCB((SettingID)RomBrowser_ColoumnsChanged, this, ColumnsChangedCB);
    }
    // The refresh thread calls back into this object; CRomList stops it in its
    // own destructor, but only after this class' members are gone.
    m_StopRefresh = true;
}

// ---------------------------------------------------------------- Fields

void QtRomBrowser::AddField(ROMBROWSER_FIELDS_LIST & Fields, const char * Name, int32_t Pos, int32_t ID, int32_t Width, LanguageStringID LangID, bool UseDefault)
{
    Fields.push_back(ROMBROWSER_FIELDS(Name, Pos, ID, (int)(Width * DPIScale()), LangID, UseDefault));
}

// Same fields, default positions and widths as CRomBrowser::GetFieldInfo.
void QtRomBrowser::GetFieldInfo(ROMBROWSER_FIELDS_LIST & Fields, bool UseDefault)
{
    Fields.clear();

    AddField(Fields, "File Name", -1, RB_FileName, 218, RB_FILENAME, UseDefault);
    AddField(Fields, "Internal Name", -1, RB_InternalName, 200, RB_INTERNALNAME, UseDefault);
    AddField(Fields, "Good Name", -1, RB_GoodName, 218, RB_GOODNAME, UseDefault);
    AddField(Fields, "Name", 0, RB_Name, 417, RB_NAME, UseDefault);
    AddField(Fields, "Status", 1, RB_Status, 206, RB_STATUS, UseDefault);
    AddField(Fields, "Rom Size", -1, RB_RomSize, 100, RB_ROMSIZE, UseDefault);
    AddField(Fields, "Notes (Core)", -1, RB_CoreNotes, 120, RB_NOTES_CORE, UseDefault);
    AddField(Fields, "Notes (default plugins)", -1, RB_PluginNotes, 188, RB_NOTES_PLUGIN, UseDefault);
    AddField(Fields, "Notes (User)", -1, RB_UserNotes, 100, RB_NOTES_USER, UseDefault);
    AddField(Fields, "Cartridge ID", -1, RB_CartridgeID, 100, RB_CART_ID, UseDefault);
    AddField(Fields, "Media", -1, RB_Media, 100, RB_MEDIA, UseDefault);
    AddField(Fields, "Country", -1, RB_Country, 100, RB_COUNTRY, UseDefault);
    AddField(Fields, "Developer", -1, RB_Developer, 100, RB_DEVELOPER, UseDefault);
    AddField(Fields, "CRC1", -1, RB_Crc1, 100, RB_CRC1, UseDefault);
    AddField(Fields, "CRC2", -1, RB_Crc2, 100, RB_CRC2, UseDefault);
    AddField(Fields, "CIC Chip", -1, RB_CICChip, 100, RB_CICCHIP, UseDefault);
    AddField(Fields, "Release Date", -1, RB_ReleaseDate, 100, RB_RELEASE_DATE, UseDefault);
    AddField(Fields, "Genre", -1, RB_Genre, 100, RB_GENRE, UseDefault);
    AddField(Fields, "Players", -1, RB_Players, 100, RB_PLAYERS, UseDefault);
    AddField(Fields, "Force Feedback", -1, RB_ForceFeedback, 100, RB_FORCE_FEEDBACK, UseDefault);
    AddField(Fields, "File Format", -1, RB_FileFormat, 100, RB_FILE_FORMAT, UseDefault);
}

// CRomBrowser::ResetRomBrowserColomuns: one column per field with a position
// >= 0, ordered by position.
void QtRomBrowser::ReloadColumns()
{
    m_UpdatingColumns = true;
    GetFieldInfo(m_Fields);
    m_ColumnField.clear();
    for (size_t Pos = 0; Pos < m_Fields.size(); Pos++)
    {
        for (size_t i = 0; i < m_Fields.size(); i++)
        {
            if (m_Fields[i].Pos() == Pos)
            {
                m_ColumnField.push_back((int)i);
                break;
            }
        }
    }
    setColumnCount((int)m_ColumnField.size());
    for (int Column = 0; Column < (int)m_ColumnField.size(); Column++)
    {
        const ROMBROWSER_FIELDS & Field = m_Fields[m_ColumnField[Column]];
        setHorizontalHeaderItem(Column, new QTableWidgetItem(Gs(Field.LangID())));
        setColumnWidth(Column, Field.ColWidth());
    }
    m_UpdatingColumns = false;
    RebuildRows();
}

QString QtRomBrowser::FieldText(const ROM_INFO & Info, int FieldID) const
{
    auto Text = [&](const char * S) {
        if (strcmp(S, "#340#") == 0)
        {
            return Gs(RB_NOT_GOOD_FILE);
        }
        if (strcmp(S, "#321#") == 0)
        {
            return QString::fromUtf8(Info.FileName);
        }
        return QString::fromUtf8(S);
    };
    switch (FieldID)
    {
    case RB_FileName: return QString::fromLocal8Bit(Info.FileName);
    case RB_InternalName:
    {
        QStringDecoder Decoder("Shift-JIS");
        if (Decoder.isValid())
        {
            return Decoder(QByteArray(Info.InternalName));
        }
        return QString::fromLatin1(Info.InternalName);
    }
    case RB_GoodName: return Text(Info.GoodName);
    case RB_Name: return Text(Info.Name);
    case RB_CoreNotes: return Text(Info.CoreNotes);
    case RB_PluginNotes: return Text(Info.PluginNotes);
    case RB_Status: return Text(Info.Status);
    case RB_RomSize: return QString::asprintf("%.1f MBit", (float)Info.RomSize / 0x20000);
    case RB_CartridgeID: return Text(Info.CartID);
    case RB_Media:
        switch (Info.Media)
        {
        case 'C': return "N64 Cartridge (Disk Compatible)";
        case 'D': return "64DD Disk";
        case 'E': return "64DD Disk (Expansion)";
        case 'M': return "N64 Development Cartridge";
        case 'N': return "N64 Cartridge";
        case 'Z': return "Aleck64";
        case 0: return "None";
        default: return QString::asprintf("(Unknown %c (%X))", Info.Media, Info.Media);
        }
    case RB_Country:
        switch (Info.Country)
        {
        case '7': return "Beta";
        case 'A': return "NTSC";
        case 'D': return "Germany";
        case 'E': return "America";
        case 'F': return "France";
        case 'J': return "Japan";
        case 'I': return "Italy";
        case 'P': return "Europe";
        case 'S': return "Spain";
        case 'U': return "Australia";
        case 'X': return "PAL";
        case 'Y': return "PAL";
        case 0: return "None";
        default: return QString::asprintf("Unknown %c (%02X)", Info.Country, Info.Country);
        }
    case RB_Crc1: return QString::asprintf("0x%08X", Info.CRC1);
    case RB_Crc2: return QString::asprintf("0x%08X", Info.CRC2);
    case RB_CICChip:
        if (Info.CicChip < 0)
        {
            return "Unknown CIC Chip";
        }
        if (Info.CicChip == CIC_NUS_8303)
        {
            return "CIC-NUS-8303";
        }
        if (Info.CicChip == CIC_NUS_5167)
        {
            return "CIC-NUS-5167";
        }
        if (Info.CicChip == CIC_NUS_8501)
        {
            return "CIC-NUS-8501";
        }
        if (Info.CicChip == CIC_NUS_8401)
        {
            return "CIC-NUS-8401";
        }
        if (Info.CicChip == CIC_NUS_5101)
        {
            return "CIC-NUS-5101";
        }
        return QString::asprintf("CIC-NUS-610%d", Info.CicChip);
    case RB_UserNotes: return Text(Info.UserNotes);
    case RB_Developer: return Text(Info.Developer);
    case RB_ReleaseDate: return Text(Info.ReleaseDate);
    case RB_Genre: return Text(Info.Genre);
    case RB_Players: return QString::number(Info.Players);
    case RB_ForceFeedback: return Text(Info.ForceFeedback);
    case RB_FileFormat:
        switch (Info.FileFormat)
        {
        case Format_Uncompressed: return "Uncompressed";
        case Format_Zip: return "Zip";
        case Format_7zip: return "7zip";
        default: return QString::asprintf("Unknown (%X)", Info.FileFormat);
        }
    default: return " ";
    }
}

// ------------------------------------------------------------------ Sort

// CRomBrowser::RomList_CompareItems (returns <0, 0, >0 for the given order).
int QtRomBrowser::CompareItems(int Rom1, int Rom2, int FieldIndex, bool Ascend) const
{
    if (Rom1 < 0 || Rom1 >= (int)m_RomInfo.size() || Rom2 < 0 || Rom2 >= (int)m_RomInfo.size())
    {
        return 0;
    }
    const ROM_INFO * Info1 = &m_RomInfo[Ascend ? Rom1 : Rom2];
    const ROM_INFO * Info2 = &m_RomInfo[Ascend ? Rom2 : Rom1];
    const int Key = FieldIndex;

    const char *GoodName1 = nullptr, *GoodName2 = nullptr;
    if (Key == RB_GoodName)
    {
        GoodName1 = strcmp("#340#", Info1->GoodName) != 0 ? Info1->GoodName : m_UnknownGoodName.c_str();
        GoodName2 = strcmp("#340#", Info2->GoodName) != 0 ? Info2->GoodName : m_UnknownGoodName.c_str();
    }
    const char *Name1 = nullptr, *Name2 = nullptr;
    if (Key == RB_Name)
    {
        Name1 = strcmp("#321#", Info1->Name) != 0 ? Info1->GoodName : Info1->FileName;
        Name2 = strcmp("#321#", Info2->Name) != 0 ? Info2->GoodName : Info2->FileName;
    }

    switch (Key)
    {
    case RB_FileName: return Compare(Info1->FileName, Info2->FileName);
    case RB_InternalName: return Compare(Info1->InternalName, Info2->InternalName);
    case RB_GoodName: return Compare(GoodName1, GoodName2);
    case RB_Name: return Compare(Name1, Name2);
    case RB_Status: return Compare(Info1->Status, Info2->Status);
    case RB_RomSize: return Info1->RomSize - Info2->RomSize;
    case RB_CoreNotes: return Compare(Info1->CoreNotes, Info2->CoreNotes);
    case RB_PluginNotes: return Compare(Info1->PluginNotes, Info2->PluginNotes);
    case RB_UserNotes: return Compare(Info1->UserNotes, Info2->UserNotes);
    case RB_CartridgeID: return Compare(Info1->CartID, Info2->CartID);
    case RB_Media: return (int)Info1->Media - (int)Info2->Media;
    case RB_Country: return (int)Info1->Country - (int)Info2->Country;
    case RB_Developer: return Compare(Info1->Developer, Info2->Developer);
    case RB_Crc1: return (int)Info1->CRC1 - (int)Info2->CRC1;
    case RB_Crc2: return (int)Info1->CRC2 - (int)Info2->CRC2;
    case RB_CICChip: return (int)Info1->CicChip - (int)Info2->CicChip;
    case RB_ReleaseDate: return Compare(Info1->ReleaseDate, Info2->ReleaseDate);
    case RB_Players: return Info1->Players - Info2->Players;
    case RB_ForceFeedback: return Compare(Info1->ForceFeedback, Info2->ForceFeedback);
    case RB_Genre: return Compare(Info1->Genre, Info2->Genre);
    case RB_FileFormat: return (int)Info1->FileFormat - (int)Info2->FileFormat;
    }
    return 0;
}

// CRomBrowser::RomList_SortList: applies the sort keys from the last (least
// significant) to the first, each as a stable sort.
void QtRomBrowser::SortList()
{
    m_UnknownGoodName = Gs(RB_NOT_GOOD_FILE).toStdString();
    m_Order.resize(m_RomInfo.size());
    for (size_t i = 0; i < m_Order.size(); i++)
    {
        m_Order[i] = (int)i;
    }
    for (int Count = NoOfSortKeys; Count >= 0; Count--)
    {
        const std::string SortFieldName = UISettingsLoadStringIndex(RomBrowser_SortFieldIndex, Count);
        size_t Index;
        for (Index = 0; Index < m_Fields.size(); Index++)
        {
            if (strcasecmp(m_Fields[Index].Name(), SortFieldName.c_str()) == 0)
            {
                break;
            }
        }
        if (Index >= m_Fields.size())
        {
            continue;
        }
        const int FieldID = m_Fields[Index].ID();
        const bool Ascend = UISettingsLoadBoolIndex(RomBrowser_SortAscendingIndex, Count) != 0;
        std::stable_sort(m_Order.begin(), m_Order.end(), [&](int A, int B) { return CompareItems(A, B, FieldID, Ascend) < 0; });
    }
}

// ------------------------------------------------------------------ Rows

void QtRomBrowser::FillRow(int Row, int RomIndex)
{
    const ROM_INFO & Info = m_RomInfo[RomIndex];
    // ROM_INFO colours are Win32 COLORREF (0x00BBGGRR).
    auto ToColor = [](uint32_t Ref) { return QColor(Ref & 0xFF, (Ref >> 8) & 0xFF, (Ref >> 16) & 0xFF); };
    for (int Column = 0; Column < (int)m_ColumnField.size(); Column++)
    {
        QTableWidgetItem * Item = new QTableWidgetItem(FieldText(Info, m_Fields[m_ColumnField[Column]].ID()));
        Item->setData(Qt::UserRole, RomIndex);
        Item->setForeground(QBrush(ToColor(Info.TextColor)));
        if (Info.SelColor >= 0)
        {
            (void)Info.SelColor; // selection colours: the platform style is used
        }
        setItem(Row, Column, Item);
    }
}

int QtRomBrowser::RomIndexOfRow(int Row) const
{
    QTableWidgetItem * Item = this->item(Row, 0);
    return Item != nullptr ? Item->data(Qt::UserRole).toInt() : -1;
}

void QtRomBrowser::RebuildRows()
{
    SortList();
    const std::string Selected = SelectedRom().toStdString();
    setUpdatesEnabled(false);
    clearContents();
    setRowCount((int)m_Order.size());
    for (int Row = 0; Row < (int)m_Order.size(); Row++)
    {
        FillRow(Row, m_Order[Row]);
    }
    setUpdatesEnabled(true);
    if (!Selected.empty())
    {
        m_LastRom = Selected;
    }
    HighLightLastRom();
}

void QtRomBrowser::ShowingBrowser()
{
    m_AllowSelectionLastRom = true;
    HighLightLastRom();
}

void QtRomBrowser::keyPressEvent(QKeyEvent * Event)
{
    if ((Event->key() == Qt::Key_Return || Event->key() == Qt::Key_Enter) && currentRow() >= 0 && !SelectedRom().isEmpty())
    {
        OnItemActivated(currentRow());
        return;
    }
    QTableWidget::keyPressEvent(Event);
}

void QtRomBrowser::mousePressEvent(QMouseEvent * Event)
{
    QTableWidget::mousePressEvent(Event);
    if (!selectedRanges().isEmpty())
    {
        m_AllowSelectionLastRom = false;
    }
}

void QtRomBrowser::HighLightLastRom()
{
    if (!m_AllowSelectionLastRom)
    {
        return;
    }
    const std::string Last = m_LastRom.empty() ? UISettingsLoadStringIndex(File_RecentGameFileIndex, 0) : m_LastRom;
    for (int Row = 0; Row < rowCount(); Row++)
    {
        const int RomIndex = RomIndexOfRow(Row);
        if (RomIndex >= 0 && strcasecmp(m_RomInfo[RomIndex].szFullFileName, Last.c_str()) == 0)
        {
            selectRow(Row);
            scrollToItem(item(Row, 0));
            return;
        }
    }
}

QString QtRomBrowser::SelectedRom() const
{
    const QList<QTableWidgetSelectionRange> Ranges = selectedRanges();
    if (Ranges.isEmpty())
    {
        return QString();
    }
    const int RomIndex = RomIndexOfRow(Ranges.first().topRow());
    if (RomIndex < 0 || RomIndex >= (int)m_RomInfo.size())
    {
        return QString();
    }
    return QString::fromLocal8Bit(m_RomInfo[RomIndex].szFullFileName);
}

// ------------------------------------------------------ CRomList callbacks

// The refresh thread waits for the GUI thread in each of these. The destructor (GUI thread) must not
// wait for that thread while it waits for the GUI thread: after m_Destroying the callbacks do not
// wait, and the destructor serves the ones that already do.
struct QtRomBrowser::CallbackScope
{
    explicit CallbackScope(QtRomBrowser * Browser) :
        m_Browser(Browser)
    {
        m_Browser->m_InCallback++;
    }
    ~CallbackScope()
    {
        m_Browser->m_InCallback--;
    }
    bool Stopping() const
    {
        return m_Browser->m_Destroying;
    }
    QtRomBrowser * m_Browser;
};

void QtRomBrowser::RomListReset(void)
{
    CallbackScope Scope(this);
    if (!Scope.Stopping())
    {
        QMetaObject::invokeMethod(this, "DoReset", CallType(this));
    }
}

void QtRomBrowser::RomAddedToList(int32_t ListPos)
{
    CallbackScope Scope(this);
    if (!Scope.Stopping())
    {
        QMetaObject::invokeMethod(this, "DoAdd", CallType(this), Q_ARG(int, ListPos));
    }
}

void QtRomBrowser::RomListLoaded(void)
{
    CallbackScope Scope(this);
    if (!Scope.Stopping())
    {
        QMetaObject::invokeMethod(this, "DoLoaded", CallType(this));
    }
}

void QtRomBrowser::DoReset()
{
    m_Refreshing = true;
    // CRomBrowser::RomListReset: the watch follows RomList_GameDir
    if (m_Watching && m_WatchedDirectory != QString::fromStdString(g_Settings->LoadStringVal(RomList_GameDir)))
    {
        WatchDirectories();
    }
    clearContents();
    setRowCount(0);
    m_Order.clear();
    m_LastRom = UISettingsLoadStringIndex(File_RecentGameFileIndex, 0);
    Q_EMIT ListReset();
}

void QtRomBrowser::DoAdd(int ListPos)
{
    const int Row = rowCount();
    insertRow(Row);
    m_Order.push_back(ListPos);
    FillRow(Row, ListPos);
    Q_EMIT RomCountChanged(ListPos + 1);
}

void QtRomBrowser::DoLoaded()
{
    RebuildRows();
    // The cache is written just after this; what changed in the directory meanwhile is found by a
    // check some time later (upstream's watch thread has the same race)
    m_Refreshing = false;
    if (m_Watching)
    {
        m_WatchTimer.start();
    }
    Q_EMIT ListLoaded();
}

// ------------------------------------------------------------------ Input

// CRomBrowser::RomList_ColoumnSortList
void QtRomBrowser::OnHeaderClicked(int Column)
{
    // sectionClicked gives the logical section: the fields are kept by it (the order on screen is the user's)
    const int VisualColumn = Column;
    if (VisualColumn < 0 || VisualColumn >= (int)m_ColumnField.size())
    {
        return;
    }
    const ROMBROWSER_FIELDS & Field = m_Fields[m_ColumnField[VisualColumn]];
    if (strcasecmp(UISettingsLoadStringIndex(RomBrowser_SortFieldIndex, 0).c_str(), Field.Name()) == 0)
    {
        UISettingsSaveBoolIndex(RomBrowser_SortAscendingIndex, 0, !UISettingsLoadBoolIndex(RomBrowser_SortAscendingIndex, 0));
    }
    else
    {
        for (int Count = NoOfSortKeys; Count > 0; Count--)
        {
            UISettingsSaveStringIndex(RomBrowser_SortFieldIndex, Count, UISettingsLoadStringIndex(RomBrowser_SortFieldIndex, Count - 1));
            UISettingsSaveBoolIndex(RomBrowser_SortAscendingIndex, Count, UISettingsLoadBoolIndex(RomBrowser_SortAscendingIndex, Count - 1));
        }
        UISettingsSaveStringIndex(RomBrowser_SortFieldIndex, 0, Field.Name());
        UISettingsSaveBoolIndex(RomBrowser_SortAscendingIndex, 0, true);
    }
    RebuildRows();
}

void QtRomBrowser::OnItemActivated(int Row)
{
    const int RomIndex = RomIndexOfRow(Row);
    if (RomIndex >= 0 && RomIndex < (int)m_RomInfo.size())
    {
        m_StopRefresh = true; // RomList_OpenRom: a refresh in progress is stopped
        Q_EMIT RomActivated(QString::fromLocal8Bit(m_RomInfo[RomIndex].szFullFileName));
    }
}

// SaveRomListColoumnInfo: remember the width of each column.
void QtRomBrowser::OnSectionResized(int Column, int /*OldSize*/, int NewSize)
{
    if (m_UpdatingColumns || Column < 0 || Column >= (int)m_ColumnField.size())
    {
        return;
    }
    ROMBROWSER_FIELDS & Field = m_Fields[m_ColumnField[Column]];
    if (Field.ColWidth() != NewSize)
    {
        Field.SetColWidth(NewSize);
    }
}

// Column drag and drop: store the new positions.
void QtRomBrowser::OnSectionMoved()
{
    if (m_UpdatingColumns)
    {
        return;
    }
    QHeaderView * Header = horizontalHeader();
    for (int Visual = 0; Visual < (int)m_ColumnField.size(); Visual++)
    {
        const int Logical = Header->logicalIndex(Visual);
        m_Fields[m_ColumnField[Logical]].SetColPos(Visual);
    }
}

// CRomBrowser::RomList_PopupMenu: the menu of IDR_POPUP, with its entries removed the way
// upstream removes them (nothing selected, a disk image, basic mode, cheats and enhancements
// not enabled). The "GFX Plugin" entry is the menu that the video plugin gives
// (GetRomBrowserMenu), which the Linux plugin does not have.
void QtRomBrowser::contextMenuEvent(QContextMenuEvent * Event)
{
    QMenu Menu(this);
    FillContextMenu(Menu);
    Menu.exec(Event->globalPos());
}

void QtRomBrowser::FillContextMenu(QMenu & Menu)
{
    const QString Rom = SelectedRom();
    const bool Selected = !Rom.isEmpty();
    const bool BasicMode = g_Settings->LoadBool(UserInterface_BasicMode);
    const bool CheatsRemembered = !BasicMode && g_Settings->LoadBool(Setting_RememberCheats);
    const bool Enhancement = !BasicMode && g_Settings->LoadBool(Setting_Enhancement);
    const QString Extension = QFileInfo(Rom).suffix().toLower();
    const bool Disk = Extension == "ndd" || Extension == "d64";

    if (Selected)
    {
        Menu.addAction(Gs(POPUP_PLAY), this, [this, Rom]() { Q_EMIT RomActivated(Rom); });
        if (!Disk)
        {
            Menu.addAction(Gs(POPUP_PLAYDISK), this, [this, Rom]() { Q_EMIT PlayWithDiskRequested(Rom); });
        }
        Menu.addSeparator();
    }
    Menu.addAction(Gs(MENU_REFRESH), this, [this]() { Q_EMIT RefreshRequested(); });
    Menu.addAction(Gs(MENU_CHOOSE_ROM), this, [this]() { Q_EMIT ChooseDirectoryRequested(); });
    if (Selected)
    {
        Menu.addSeparator();
        Menu.addAction(Gs(POPUP_INFO), this, [this, Rom]() { Q_EMIT RomInfoRequested(Rom); });
        if (!(BasicMode && !CheatsRemembered))
        {
            Menu.addSeparator();
        }
        if (!BasicMode)
        {
            Menu.addAction(Gs(POPUP_SETTINGS), this, [this, Rom]() { Q_EMIT EditGameSettingsRequested(Rom); });
        }
        if (CheatsRemembered)
        {
            Menu.addAction(Gs(POPUP_CHEATS), this, [this, Rom]() { Q_EMIT EditCheatsRequested(Rom); });
        }
        if (Enhancement)
        {
            Menu.addAction(Gs(POPUP_ENHANCEMENTS), this, [this, Rom]() { Q_EMIT ChooseEnhancementRequested(Rom); });
        }
    }
}

// ---------------------------------------------------------------- Directory watch

// CRomBrowser::GetRomFileNames: the names that the list is made from (and whose hash the cache
// holds), with the subdirectories when RomList_GameDirRecursive is on
bool QtRomBrowser::GetRomFileNames(strlist & FileList, const CPath & BaseDirectory, const std::string & Directory)
{
    if (!BaseDirectory.DirectoryExists())
    {
        return false;
    }
    CPath SearchPath(BaseDirectory, "*.*");
    SearchPath.AppendDirectory(Directory.c_str());
    if (!SearchPath.FindFirst(CPath::FIND_ATTRIBUTE_ALLFILES))
    {
        return false;
    }
    do
    {
        if (m_StopRefresh)
        {
            return false;
        }
        if (SearchPath.IsDirectory())
        {
            if (g_Settings->LoadBool(RomList_GameDirRecursive))
            {
                CPath CurrentDir(Directory);
                CurrentDir.AppendDirectory(SearchPath.GetLastDirectory().c_str());
                GetRomFileNames(FileList, BaseDirectory, CurrentDir);
            }
        }
        else
        {
            AddFileNameToList(FileList, Directory, SearchPath);
        }
    } while (SearchPath.FindNext());
    return true;
}

// CRomBrowser::RomDirNeedsRefresh: the MD5 that starts the cache file against the one of the
// names that the directory has now
bool QtRomBrowser::RomDirNeedsRefresh()
{
    CPath FileName = CPath(g_Settings->LoadStringVal(RomList_RomListCache)).NormalizePath(CPath::MODULE_DIRECTORY);
    if (!FileName.Exists())
    {
        return true; // no cache: make it
    }
    CFile File(FileName, CFileBase::modeRead);
    if (!File.IsOpen())
    {
        return false; // could not validate, assume it is fine
    }
    unsigned char CurrentFileMD5[16];
    File.Read(&CurrentFileMD5, sizeof(CurrentFileMD5));
    File.Close();

    strlist FileNames;
    bool Found = GetRomFileNames(FileNames, CPath(g_Settings->LoadStringVal(RomList_GameDir)), std::string());
    for (const std::string & Extra : ExtraGameDirectories())
    {
        Found = GetRomFileNames(FileNames, CPath(Extra.c_str()), std::string()) || Found;
    }
    if (!Found)
    {
        return false;
    }
    FileNames.sort();
    MD5 NewMd5 = RomListHash(FileNames);
    return memcmp(NewMd5.raw_digest(), CurrentFileMD5, sizeof(CurrentFileMD5)) != 0;
}

void QtRomBrowser::WatchDirectories()
{
    const QStringList Old = m_Watcher.directories();
    if (!Old.isEmpty())
    {
        m_Watcher.removePaths(Old);
    }
    const QString Base = QString::fromStdString(g_Settings->LoadStringVal(RomList_GameDir));
    m_WatchedDirectory = Base;
    QStringList Roots{Base};
    for (const std::string & Extra : ExtraGameDirectories())
    {
        Roots << QString::fromStdString(Extra);
    }
    QStringList Directories;
    for (const QString & Root : Roots)
    {
        if (Root.isEmpty() || !QDir(Root).exists())
        {
            continue;
        }
        Directories << Root;
        if (g_Settings->LoadBool(RomList_GameDirRecursive))
        {
            QDirIterator Iterator(Root, QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
            while (Iterator.hasNext())
            {
                Directories << Iterator.next();
            }
        }
    }
    m_Watcher.addPaths(Directories);
}

void QtRomBrowser::WatchStart()
{
    m_Watching = true;
    WatchDirectories();
    CheckDirectory();
}

void QtRomBrowser::WatchStop()
{
    m_Watching = false;
    m_WatchTimer.stop();
    const QStringList Directories = m_Watcher.directories();
    if (!Directories.isEmpty())
    {
        m_Watcher.removePaths(Directories);
    }
}

// The names are read in a thread of their own (a slow directory must not stall the window); the
// list is refreshed in the GUI thread when they differ
void QtRomBrowser::CheckDirectory()
{
    if (!m_Watching)
    {
        return;
    }
    if (m_Checking || m_Refreshing)
    {
        m_CheckAgain = true;
        return;
    }
    if (m_CheckThread != nullptr)
    {
        m_CheckThread->wait();
        delete m_CheckThread;
        m_CheckThread = nullptr;
    }
    m_Checking = true;
    m_CheckThread = QThread::create([this]() {
        const bool NeedsRefresh = RomDirNeedsRefresh();
        QMetaObject::invokeMethod(this, [this, NeedsRefresh]() {
            m_Checking = false;
            if (m_Watching && NeedsRefresh)
            {
                // CRomList ignores the request while its refresh thread is still running: the
                // check is repeated until what the directory has is what the list was made from
                RefreshRomList();
                m_WatchTimer.start();
            }
            if (m_CheckAgain)
            {
                m_CheckAgain = false;
                CheckDirectory();
            }
        }, Qt::QueuedConnection);
    });
    m_CheckThread->start();
}
