#pragma once

#include <QWidget>

class QPushButton;
class QButtonGroup;

namespace Vortex
{
// The navigation of the window: the logo, the pages of the library and, at the bottom, the settings, the
// debugger and the about screen (those open their own windows, so they are not "current")
class Sidebar : public QWidget
{
    Q_OBJECT
public:
    explicit Sidebar(QWidget * Parent = nullptr);
    void SetCurrent(const QString & Page);
    QString Current() const;
    QPushButton * Button(const QString & Page) const;
    void Retranslate();

Q_SIGNALS:
    void PageRequested(const QString & Page); // "library", "recent", "favorites", "disks"
    void SettingsRequested();
    void DebuggerRequested();
    void AboutRequested();

private:
    QPushButton * AddNav(const QString & Id, QWidget * Parent);
    QButtonGroup * m_Group;
    QPushButton * m_Library, * m_Recent, * m_Favorites, * m_Disks, * m_Settings, * m_Debugger, * m_About;
};
} // namespace Vortex
