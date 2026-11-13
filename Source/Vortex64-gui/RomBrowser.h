#pragma once

#include <Vortex64-core/Multilanguage.h>
#include <Vortex64-core/RomList/RomList.h>
#include <Vortex64-core/Settings/SettingsID.h>
#include <Vortex64-ui/Settings/UISettings.h>

#include <QFileSystemWatcher>
#include <QTableWidget>
#include <QTimer>
#include <atomic>
#include <string>
#include <vector>

// Qt counterpart of Project64/UserInterface/RomBrowser.{h,cpp}. The ROM list
// itself (scanning, RDB lookup, cache, zip/7z) is the core's CRomList, exactly
// as in upstream; this class only presents it: the same fields, default
// positions/widths, multi-key sort (RomBrowser_SortFieldIndex /
// RomBrowser_SortAscendingIndex) and column persistence (RomBrowser_PosIndex /
// RomBrowser_WidthIndex).

// Identical to upstream's ROMBROWSER_FIELDS (RomBrowser.h).
class ROMBROWSER_FIELDS
{
    std::string m_Name;
    uint32_t m_Pos, m_DefaultPos;
    int m_ID;
    uint32_t m_ColWidth;
    LanguageStringID m_LangID;
    bool m_PosChanged;

public:
    ROMBROWSER_FIELDS(const char * Name, int Pos, int ID, int ColWidth, LanguageStringID LangID, bool UseDefault) :
        m_Name(Name),
        m_Pos(Pos),
        m_DefaultPos(Pos),
        m_ID(ID),
        m_ColWidth(ColWidth),
        m_LangID(LangID),
        m_PosChanged(false)
    {
        if (!UseDefault)
        {
            m_PosChanged = UISettingsLoadDwordIndex(RomBrowser_PosIndex, m_ID, (uint32_t &)m_Pos);
            UISettingsLoadDwordIndex(RomBrowser_WidthIndex, m_ID, m_ColWidth);
        }
    }
    const char * Name(void) const { return m_Name.c_str(); }
    size_t Pos(void) const { return m_Pos; }
    bool PosChanged(void) const { return m_PosChanged; }
    int ID(void) const { return m_ID; }
    int ColWidth(void) const { return m_ColWidth; }
    LanguageStringID LangID(void) const { return m_LangID; }
    void SetColWidth(int ColWidth)
    {
        m_ColWidth = ColWidth;
        UISettingsSaveDwordIndex(RomBrowser_WidthIndex, m_ID, m_ColWidth);
    }
    void SetColPos(int Pos)
    {
        m_Pos = Pos;
        UISettingsSaveDwordIndex(RomBrowser_PosIndex, m_ID, Pos);
        m_PosChanged = true;
    }
    void ResetPos(void)
    {
        m_Pos = m_DefaultPos;
        UISettingsDeleteSettingIndex(RomBrowser_PosIndex, m_ID);
        m_PosChanged = false;
    }
};

typedef std::vector<ROMBROWSER_FIELDS> ROMBROWSER_FIELDS_LIST;

class QtRomBrowser : public QTableWidget, public CRomList
{
    Q_OBJECT

public:
    explicit QtRomBrowser(QWidget * parent = nullptr);
    ~QtRomBrowser() override;

    static void GetFieldInfo(ROMBROWSER_FIELDS_LIST & Fields, bool UseDefault = false);

    // CRomBrowser::RefreshRomList / LoadRomList are inherited from CRomList.
    void ReloadColumns();
    void HighLightLastRom();
    // CRomBrowser::ShowRomList: the browser is shown again: the last ROM is highlighted again
    // until the user clicks (m_AllowSelectionLastRom)
    void ShowingBrowser();
    QString SelectedRom() const;

    // The entries of the pop-up menu for the current selection
    void FillContextMenu(QMenu & Menu);

    // CRomBrowser::WatchThreadStart/WatchThreadStop: while the browser shows, the ROM directory is
    // watched and the list is refreshed when its files changed (RomDirNeedsRefresh), and once
    // when the watch starts
    void WatchStart();
    void WatchStop();
    // The list of ROM file names differs from the one the cache was made from
    bool RomDirNeedsRefresh();
    // The list as the core has it (for the Vortex library, which presents it in its own way). Only
    // valid in the GUI thread and between the callbacks (ListReset/ListLoaded).
    int RomCount() const
    {
        return (int)m_RomInfo.size();
    }
    const ROM_INFO & RomAt(int Index) const
    {
        return m_RomInfo[Index];
    }
    bool Refreshing() const
    {
        return m_Refreshing;
    }
    // A ROM was run: a refresh in progress is stopped (RomList_OpenRom)
    void StopRefresh()
    {
        m_StopRefresh = true;
    }
    // Test hook: a check that is on its way or waiting
    bool Checking() const
    {
        return m_Checking;
    }

Q_SIGNALS:
    // The list was cleared / a ROM was added (the count so far) / the list is complete
    void ListReset();
    void RomCountChanged(int Count);
    void ListLoaded();
    // Double click / "Play Game".
    void RomActivated(const QString & Path);
    void RomInfoRequested(const QString & Path);
    void EditGameSettingsRequested(const QString & Path);
    // The rest of the pop-up menu of CRomBrowser::RomList_PopupMenu
    void PlayWithDiskRequested(const QString & Path);
    void EditCheatsRequested(const QString & Path);
    void ChooseEnhancementRequested(const QString & Path);
    void RefreshRequested();
    void ChooseDirectoryRequested();

protected:
    // CRomList callbacks run on the refresh thread: they are forwarded to the
    // GUI thread, blocking until done like SendMessage does on Windows.
    void RomListReset(void) override;
    void RomAddedToList(int32_t ListPos) override;
    void RomListLoaded(void) override;

    void contextMenuEvent(QContextMenuEvent * Event) override;
    // NM_RETURN: Return plays the selected ROM; NM_CLICK on a ROM stops HighLightLastRom
    void keyPressEvent(QKeyEvent * Event) override;
    void mousePressEvent(QMouseEvent * Event) override;

private Q_SLOTS:
    void DoReset();
    void DoAdd(int ListPos);
    void DoLoaded();
    void OnHeaderClicked(int Column);
    void OnItemActivated(int Row);
    void OnSectionMoved();
    void OnSectionResized(int Column, int OldSize, int NewSize);

private:
    bool GetRomFileNames(strlist & FileList, const CPath & BaseDirectory, const std::string & Directory);
    void CheckDirectory();
    void WatchDirectories();

    struct CallbackScope;
    std::atomic<int> m_InCallback{0};
    std::atomic<bool> m_Destroying{false};

    QFileSystemWatcher m_Watcher;
    QTimer m_WatchTimer;
    bool m_Watching = false;
    std::atomic<bool> m_Checking{false};
    bool m_Refreshing = false;
    bool m_AllowSelectionLastRom = true;
    QString m_WatchedDirectory;
    bool m_CheckAgain = false;
    QThread * m_CheckThread = nullptr;

    enum
    {
        NoOfSortKeys = 3
    };
    enum
    {
        RB_FileName = 0,
        RB_InternalName = 1,
        RB_GoodName = 2,
        RB_Name = 3,
        RB_Status = 4,
        RB_RomSize = 5,
        RB_CoreNotes = 6,
        RB_PluginNotes = 7,
        RB_UserNotes = 8,
        RB_CartridgeID = 9,
        RB_Media = 10,
        RB_Country = 11,
        RB_Developer = 12,
        RB_Crc1 = 13,
        RB_Crc2 = 14,
        RB_CICChip = 15,
        RB_ReleaseDate = 16,
        RB_Genre = 17,
        RB_Players = 18,
        RB_ForceFeedback = 19,
        RB_FileFormat = 20,
    };

    static void ColumnsChangedCB(void * Data);
    static void AddField(ROMBROWSER_FIELDS_LIST & Fields, const char * Name, int32_t Pos, int32_t ID, int32_t Width, LanguageStringID LangID, bool UseDefault);
    QString FieldText(const ROM_INFO & Info, int FieldID) const;
    int CompareItems(int Rom1, int Rom2, int FieldIndex, bool Ascend) const;
    void SortList();
    void FillRow(int Row, int RomIndex);
    void RebuildRows();
    int RomIndexOfRow(int Row) const;

    ROMBROWSER_FIELDS_LIST m_Fields;
    std::vector<int> m_ColumnField; // column -> index into m_Fields
    std::string m_LastRom;
    std::vector<int> m_Order;       // ROM indexes in display order
    bool m_UpdatingColumns;
    static std::string m_UnknownGoodName;
};
