#pragma once

#include "LibraryModel.h"

#include <QListView>
#include <QStyledItemDelegate>
#include <QTreeView>
#include <QWidget>

class QLabel;
class QPushButton;
class QStackedLayout;

namespace Vortex
{
// The card of a game in the grid: cover, title, region with its flag and the favourite star
class CardDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter * Painter, const QStyleOptionViewItem & Option, const QModelIndex & Index) const override;
    QSize sizeHint(const QStyleOptionViewItem & Option, const QModelIndex & Index) const override;
    bool editorEvent(QEvent * Event, QAbstractItemModel * Model, const QStyleOptionViewItem & Option, const QModelIndex & Index) override;
    static QRect StarRect(const QRect & Card);
    static QRect CoverRect(const QRect & Card);
    static int CardHeightFor(int Width);
};

// The row of a game in the list / compact views
class RowDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    explicit RowDelegate(QObject * Parent = nullptr, bool Compact = false);
    void SetCompact(bool Compact)
    {
        m_Compact = Compact;
    }
    void paint(QPainter * Painter, const QStyleOptionViewItem & Option, const QModelIndex & Index) const override;
    QSize sizeHint(const QStyleOptionViewItem & Option, const QModelIndex & Index) const override;
    bool editorEvent(QEvent * Event, QAbstractItemModel * Model, const QStyleOptionViewItem & Option, const QModelIndex & Index) override;
    int ThumbSize() const
    {
        return m_Compact ? 20 : 28;
    }

private:
    bool m_Compact;
};

class GameGrid : public QListView
{
    Q_OBJECT
public:
    explicit GameGrid(QWidget * Parent = nullptr);
    void UpdateGrid();

protected:
    void resizeEvent(QResizeEvent * Event) override;
    void mouseMoveEvent(QMouseEvent * Event) override;
};

// Presents a LibraryProxy as a grid of cards, a list or a compact list; the selection is shared
class GameBrowser : public QWidget
{
    Q_OBJECT
public:
    explicit GameBrowser(LibraryProxy * Proxy, QWidget * Parent = nullptr);

    void SetMode(const QString & Mode); // "grid", "list", "compact"
    QString Mode() const
    {
        return m_Mode;
    }
    QString CurrentPath() const;
    void SelectPath(const QString & Path);
    void SetEmpty(const QString & Title, const QString & Subtitle, const QString & Button = QString());
    QTreeView * ListView() const
    {
        return m_List;
    }
    GameGrid * GridView() const
    {
        return m_Grid;
    }
    LibraryProxy * Proxy() const
    {
        return m_Proxy;
    }
    // The visible view takes the keyboard focus
    void FocusView();
    bool EmptyShown() const;
    void SelectFirstIfNone();

Q_SIGNALS:
    void Selected(const QString & Path);   // "" when nothing is selected
    void Activated(const QString & Path);  // double click / Return
    void ContextMenuRequested(const QString & Path, const QPoint & GlobalPos);
    void ActionRequested();                // the button of the empty state

private:
    void Connect(QAbstractItemView * View);
    void UpdateEmpty();
    QString PathAt(const QModelIndex & Index) const;

    LibraryProxy * m_Proxy;
    QStackedLayout * m_Stack;
    QWidget * m_Content;
    QStackedLayout * m_Views;
    GameGrid * m_Grid;
    QTreeView * m_List;
    RowDelegate * m_RowDelegate;
    QWidget * m_Empty;
    QLabel * m_EmptyTitle;
    QLabel * m_EmptySubtitle;
    QPushButton * m_EmptyButton;
    QString m_Mode = QStringLiteral("grid");
};
} // namespace Vortex
