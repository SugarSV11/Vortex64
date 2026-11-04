#pragma once

#include "DetailPanel.h"
#include "GameBrowser.h"
#include "LibraryModel.h"
#include "Sidebar.h"

#include <QWidget>

class QLineEdit;
class QStackedWidget;
class QToolButton;
class QLabel;
class QtRomBrowser;
class QTextEdit;
class QListView;

namespace Vortex
{
// A page of the library that shows games (Biblioteca, Recientes, Favoritos): the same model, a proxy with
// the scope of the page, the browser with its three modes and the detail strip
class BrowsePage : public QWidget
{
    Q_OBJECT
public:
    BrowsePage(const QString & Id, LibraryModel * Model, LibraryProxy::Scope Scope, QWidget * Parent = nullptr);
    QString Id() const
    {
        return m_Id;
    }
    GameBrowser * Browser() const
    {
        return m_Browser;
    }
    DetailPanel * Detail() const
    {
        return m_Detail;
    }
    LibraryProxy * Proxy() const
    {
        return m_Proxy;
    }
    void SetFilter(const QString & Text);
    void SetMode(const QString & Mode);
    QString Mode() const
    {
        return m_Browser->Mode();
    }
    void RefreshEmptyText(bool HasDirectories, bool Scanning);
    QString SelectedRom() const
    {
        return m_Browser->CurrentPath();
    }

Q_SIGNALS:
    void StartRequested(const QString & Path);
    void ContextMenuRequested(const QString & Path, const QPoint & GlobalPos);
    void AddDirectoryRequested();

private:
    QString m_Id;
    LibraryModel * m_Model;
    LibraryProxy * m_Proxy;
    GameBrowser * m_Browser;
    DetailPanel * m_Detail;
    QString m_FilterText;
};

// 64DD: the disks found, the data of the selected one, mount / explore and the notes
class DisksPage : public QWidget
{
    Q_OBJECT
public:
    DisksPage(LibraryModel * Model, QWidget * Parent = nullptr);
    void SetFilter(const QString & Text);
    QString SelectedDisk() const;
    LibraryProxy * Proxy() const
    {
        return m_Proxy;
    }
    GameBrowser * Browser() const
    {
        return m_Browser;
    }
    void Refresh();

Q_SIGNALS:
    void MountRequested(const QString & Path);
    void ExploreRequested(const QString & Path);
    void DiskSettingsRequested();
    void ContextMenuRequested(const QString & Path, const QPoint & GlobalPos);

private:
    void ShowDisk(const QString & Path);
    LibraryModel * m_Model;
    LibraryProxy * m_Proxy;
    GameBrowser * m_Browser;
    QLabel * m_Cover, * m_Name, * m_Region, * m_Size, * m_Type, * m_State;
    QPushButton * m_Mount, * m_Explore, * m_Settings;
    QTextEdit * m_Notes;
    QString m_Path;
};

// The central widget while no game runs
class Shell : public QWidget
{
    Q_OBJECT
public:
    explicit Shell(QtRomBrowser * Source, QWidget * Parent = nullptr);

    LibraryModel * Model() const
    {
        return m_Model;
    }
    Sidebar * SidebarWidget() const
    {
        return m_Sidebar;
    }
    void ShowPage(const QString & Page);
    QString CurrentPage() const;
    BrowsePage * Page(const QString & Id) const;
    DisksPage * Disks() const
    {
        return m_Disks;
    }
    QLineEdit * SearchBox() const
    {
        return m_Search;
    }
    QString SelectedRom() const;
    void SetSource(QtRomBrowser * Source);
    QToolButton * ViewButton(const QString & Mode) const;
    // The play button of the current page, for the tests
    void SelectRom(const QString & Path);

Q_SIGNALS:
    void PlayRequested(const QString & Path);
    void PlayWithDiskRequested(const QString & Path);
    void RomInfoRequested(const QString & Path);
    void EditSettingsRequested(const QString & Path);
    void EditCheatsRequested(const QString & Path);
    void EditEnhancementsRequested(const QString & Path);
    void MountDiskRequested(const QString & Path);
    void OpenRomRequested();
    void SettingsRequested();
    void DiskSettingsRequested();
    void DebuggerRequested();
    void AboutRequested();
    void RefreshRequested();
    void AddDirectoryRequested();
    void PageChanged(const QString & Page);

protected:
    void keyPressEvent(QKeyEvent * Event) override;

private:
    void BuildContextMenu(const QString & Path, const QPoint & GlobalPos, bool InRecents);
    void UpdateHeader();
    void UpdateEmptyTexts();
    void SetFilter(const QString & Text);
    void ApplyViewMode(const QString & Mode);

    LibraryModel * m_Model;
    Sidebar * m_Sidebar;
    QLineEdit * m_Search;
    QToolButton * m_Grid, * m_List, * m_Compact;
    QWidget * m_ViewButtons;
    QLabel * m_ScanLabel;
    QStackedWidget * m_Pages;
    BrowsePage * m_Library, * m_Recent, * m_Favorites;
    DisksPage * m_Disks;
    QtRomBrowser * m_Source;
};
} // namespace Vortex
