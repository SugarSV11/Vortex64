#pragma once

#include "LibraryModel.h"

#include <QWidget>

class QLabel;
class QPushButton;

namespace Vortex
{
// The strip below the games: the selected one with its data and the Start button
class DetailPanel : public QWidget
{
    Q_OBJECT
public:
    explicit DetailPanel(LibraryModel * Model, QWidget * Parent = nullptr);
    void SetRom(const QString & Path);
    QString Rom() const
    {
        return m_Path;
    }
    void Refresh();
    QPushButton * StartButton() const
    {
        return m_Start;
    }

Q_SIGNALS:
    void StartRequested(const QString & Path);

private:
    LibraryModel * m_Model;
    QString m_Path;
    QLabel * m_Thumb, * m_Name, * m_Region, * m_Size, * m_Format, * m_Last, * m_Played;
    QPushButton * m_Start;
};
} // namespace Vortex
