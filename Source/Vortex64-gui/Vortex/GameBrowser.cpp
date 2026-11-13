#include "GameBrowser.h"
#include "Covers.h"
#include "Icons.h"
#include "Store.h"
#include "Strings.h"
#include "Theme.h"

#include <QAbstractItemView>
#include <cstdio>
#include <QAbstractProxyModel>
#include <QApplication>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollBar>
#include <QShortcut>
#include <QStackedLayout>
#include <QTimer>
#include <QVBoxLayout>

namespace Vortex
{
namespace
{
constexpr int Pad = 8;

QFont PixelFont(const QFont & Base, int Pixels, bool Bold = false)
{
    QFont Font = Base;
    Font.setPixelSize(Pixels);
    Font.setBold(Bold);
    return Font;
}

void PaintStar(QPainter * P, const QRect & R, bool On, bool Hot)
{
    const QColor Color = On ? T().Favorite : (Hot ? T().TextPrimary : T().TextDisabled);
    const qreal Ratio = P->device()->devicePixelRatioF();
    P->drawPixmap(R.topLeft(), IconPixmap(On ? Icon::Star : Icon::StarOutline, Color, R.width(), Ratio));
}
} // namespace

// ---------------------------------------------------------------- the card

QRect CardDelegate::CoverRect(const QRect & Card)
{
    const int Side = Card.width() - 2 * Pad;
    return QRect(Card.left() + Pad, Card.top() + Pad, Side, Side);
}

int CardDelegate::CardHeightFor(int Width)
{
    return Pad + (Width - 2 * Pad) + 6 + 34 + 2 + 20 + Pad;
}

QRect CardDelegate::StarRect(const QRect & Card)
{
    return QRect(Card.right() - Pad - 18, Card.bottom() - Pad - 20, 18, 18);
}

QSize CardDelegate::sizeHint(const QStyleOptionViewItem & Option, const QModelIndex &) const
{
    return QSize(Option.rect.width() > 0 ? Option.rect.width() : T().CardWidth, CardHeightFor(Option.rect.width() > 0 ? Option.rect.width() : T().CardWidth));
}

void CardDelegate::paint(QPainter * P, const QStyleOptionViewItem & Option, const QModelIndex & Index) const
{
    P->save();
    P->setRenderHint(QPainter::Antialiasing);
    const Tokens & K = T();
    const QRect Card = Option.rect.adjusted(3, 3, -3, -3);
    const bool Selected = Option.state & QStyle::State_Selected;
    const bool Hover = Option.state & QStyle::State_MouseOver;
    const bool Favorite = Index.data(LibraryModel::FavoriteRole).toBool();
    if (Selected || Hover)
    {
        P->setPen(Selected ? QPen(QColor(K.Accent.red(), K.Accent.green(), K.Accent.blue(), 120), 1) : Qt::NoPen);
        P->setBrush(Selected ? K.Selected : K.SurfaceElevated);
        P->drawRoundedRect(QRectF(Card).adjusted(0.5, 0.5, -0.5, -0.5), K.CornerRadius, K.CornerRadius);
    }
    if (Option.state & QStyle::State_HasFocus && Selected)
    {
        P->setPen(QPen(K.Accent, 1.5));
        P->setBrush(Qt::NoBrush);
        P->drawRoundedRect(QRectF(Card).adjusted(0.5, 0.5, -0.5, -0.5), K.CornerRadius, K.CornerRadius);
    }
    const QRect Cover = CoverRect(Card);
    const LibraryModel * Source = qobject_cast<const LibraryModel *>(qobject_cast<const QAbstractProxyModel *>(Index.model()) ? qobject_cast<const QAbstractProxyModel *>(Index.model())->sourceModel() : Index.model());
    const QModelIndex SourceIndex = qobject_cast<const QAbstractProxyModel *>(Index.model()) ? qobject_cast<const QAbstractProxyModel *>(Index.model())->mapToSource(Index) : Index;
    if (Source != nullptr)
    {
        const qreal Ratio = P->device()->devicePixelRatioF();
        const QPixmap Pix = Covers::Instance().Cover(Source->At(SourceIndex.row()), Cover.size(), Ratio);
        const QSizeF Logical = QSizeF(Pix.size()) / Pix.devicePixelRatio();
        const QPointF At(Cover.center().x() - Logical.width() / 2.0 + 0.5, Cover.center().y() - Logical.height() / 2.0 + 0.5);
        // a discreet shadow under the cover (three soft layers)
        P->setPen(Qt::NoPen);
        for (int Layer = 3; Layer >= 1; Layer--)
        {
            P->setBrush(QColor(0, 0, 0, Selected || Hover ? 26 : 20));
            P->drawRoundedRect(QRectF(At, Logical).adjusted(-Layer * 0.6, Layer * 0.6, Layer * 0.6, Layer * 1.6), 9, 9);
        }
        P->drawPixmap(At, Pix);
    }
    // title: up to two lines
    const QRect TitleRect(Card.left() + Pad, Cover.bottom() + 8, Card.width() - 2 * Pad, 34);
    P->setFont(PixelFont(Option.font, 12, true));
    P->setPen(K.TextPrimary);
    const QString Title = Index.data(LibraryModel::NameRole).toString();
    // the name without its region: it has its own line
    QString Shown = Title;
    const int Paren = Shown.lastIndexOf(QStringLiteral(" ("));
    if (Paren > 0 && Shown.endsWith(QLatin1Char(')')))
    {
        Shown = Shown.left(Paren);
    }
    QTextOption Wrap(Qt::AlignLeft | Qt::AlignTop);
    Wrap.setWrapMode(QTextOption::WordWrap);
    const QFontMetrics FM(P->font());
    QString Elided = Shown;
    if (FM.boundingRect(TitleRect, Qt::TextWordWrap, Shown).height() > TitleRect.height())
    {
        // two lines at most, the second elided
        const QStringList Words = Shown.split(' ');
        QString First;
        int Used = 0;
        for (; Used < Words.size(); Used++)
        {
            const QString Candidate = First.isEmpty() ? Words[Used] : First + ' ' + Words[Used];
            if (FM.horizontalAdvance(Candidate) > TitleRect.width())
            {
                break;
            }
            First = Candidate;
        }
        const QString Rest = Words.mid(Used).join(' ');
        Elided = First + '\n' + FM.elidedText(Rest, Qt::ElideRight, TitleRect.width());
    }
    P->drawText(TitleRect, Elided.contains('\n') ? int(Qt::AlignLeft | Qt::AlignTop) : int(Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap), Elided);
    // region
    const QRect Bottom(Card.left() + Pad, Card.bottom() - Pad - 20, Card.width() - 2 * Pad - 22, 20);
    const char Country = (char)Index.data(LibraryModel::CountryRole).toInt();
    const QRectF Flag(Bottom.left(), Bottom.center().y() - 5.5, 16, 11);
    PaintFlag(*P, Flag, Country);
    P->setFont(PixelFont(Option.font, 11));
    P->setPen(K.TextSecondary);
    P->drawText(QRect(Bottom.left() + 21, Bottom.top(), Bottom.width() - 21, Bottom.height()), Qt::AlignVCenter | Qt::AlignLeft,
                QStringLiteral("(%1)").arg(Index.data(LibraryModel::RegionRole).toString()));
    PaintStar(P, StarRect(Card), Favorite, Hover);
    P->restore();
}

bool CardDelegate::editorEvent(QEvent * Event, QAbstractItemModel * Model, const QStyleOptionViewItem & Option, const QModelIndex & Index)
{
    if (Event->type() == QEvent::MouseButtonRelease)
    {
        const QMouseEvent * Mouse = static_cast<QMouseEvent *>(Event);
        if (Mouse->button() == Qt::LeftButton && StarRect(Option.rect.adjusted(3, 3, -3, -3)).adjusted(-4, -4, 4, 4).contains(Mouse->position().toPoint()))
        {
            const QString Path = Index.data(LibraryModel::PathRole).toString();
            Store::Instance().SetFavorite(Path, !Store::Instance().IsFavorite(Path));
            return true;
        }
    }
    return QStyledItemDelegate::editorEvent(Event, Model, Option, Index);
}

// ---------------------------------------------------------------- the rows

RowDelegate::RowDelegate(QObject * Parent, bool Compact) :
    QStyledItemDelegate(Parent),
    m_Compact(Compact)
{
}

QSize RowDelegate::sizeHint(const QStyleOptionViewItem & Option, const QModelIndex &) const
{
    return QSize(Option.rect.width(), m_Compact ? 28 : 38);
}

void RowDelegate::paint(QPainter * P, const QStyleOptionViewItem & Option, const QModelIndex & Index) const
{
    const Tokens & K = T();
    P->save();
    P->setRenderHint(QPainter::Antialiasing);
    const bool Selected = Option.state & QStyle::State_Selected;
    const bool Hover = Option.state & QStyle::State_MouseOver;
    if (Selected)
    {
        P->fillRect(Option.rect, K.Selected);
    }
    else if (Hover)
    {
        P->fillRect(Option.rect, K.Hover);
    }
    else
    {
        P->setPen(QColor(K.Border.red(), K.Border.green(), K.Border.blue(), 110));
        P->drawLine(Option.rect.bottomLeft(), Option.rect.bottomRight());
    }
    const QRect R = Option.rect;
    switch (Index.column())
    {
    case LibraryModel::ColName:
    {
        const int Thumb = ThumbSize();
        const QAbstractProxyModel * Proxy = qobject_cast<const QAbstractProxyModel *>(Index.model());
        const LibraryModel * Source = qobject_cast<const LibraryModel *>(Proxy ? Proxy->sourceModel() : Index.model());
        const QModelIndex SourceIndex = Proxy ? Proxy->mapToSource(Index) : Index;
        const QRect ThumbRect(R.left() + 10, R.center().y() - Thumb / 2, Thumb, Thumb);
        if (Source != nullptr)
        {
            const QPixmap Pix = Covers::Instance().Cover(Source->At(SourceIndex.row()), ThumbRect.size(), P->device()->devicePixelRatioF());
            const QSizeF Logical = QSizeF(Pix.size()) / Pix.devicePixelRatio();
            P->drawPixmap(QPointF(ThumbRect.center().x() - Logical.width() / 2.0 + 0.5, ThumbRect.center().y() - Logical.height() / 2.0 + 0.5), Pix);
        }
        P->setFont(PixelFont(Option.font, 13, !m_Compact));
        P->setPen(K.TextPrimary);
        const QRect TextRect(ThumbRect.right() + 12, R.top(), R.right() - ThumbRect.right() - 16, R.height());
        P->drawText(TextRect, Qt::AlignVCenter | Qt::AlignLeft, QFontMetrics(P->font()).elidedText(Index.data(LibraryModel::NameRole).toString(), Qt::ElideRight, TextRect.width()));
        break;
    }
    case LibraryModel::ColRegion:
    {
        const char Country = (char)Index.data(LibraryModel::CountryRole).toInt();
        PaintFlag(*P, QRectF(R.left() + 8, R.center().y() - 5.5, 16, 11), Country);
        P->setFont(PixelFont(Option.font, 12));
        P->setPen(K.TextSecondary);
        P->drawText(QRect(R.left() + 30, R.top(), R.width() - 30, R.height()), Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("(%1)").arg(Index.data(LibraryModel::RegionRole).toString()));
        break;
    }
    case LibraryModel::ColFavorite:
        PaintStar(P, QRect(R.center().x() - 9, R.center().y() - 9, 18, 18), Index.data(LibraryModel::FavoriteRole).toBool(), Hover);
        break;
    default:
        P->setFont(PixelFont(Option.font, 12));
        P->setPen(K.TextSecondary);
        P->drawText(R.adjusted(8, 0, -6, 0), Qt::AlignVCenter | Qt::AlignLeft, QFontMetrics(P->font()).elidedText(Index.data(Qt::DisplayRole).toString(), Qt::ElideRight, R.width() - 14));
        break;
    }
    P->restore();
}

bool RowDelegate::editorEvent(QEvent * Event, QAbstractItemModel * Model, const QStyleOptionViewItem & Option, const QModelIndex & Index)
{
    if (Index.column() == LibraryModel::ColFavorite && Event->type() == QEvent::MouseButtonRelease)
    {
        const QMouseEvent * Mouse = static_cast<QMouseEvent *>(Event);
        if (Mouse->button() == Qt::LeftButton)
        {
            const QString Path = Index.data(LibraryModel::PathRole).toString();
            Store::Instance().SetFavorite(Path, !Store::Instance().IsFavorite(Path));
            return true;
        }
    }
    return QStyledItemDelegate::editorEvent(Event, Model, Option, Index);
}

// ---------------------------------------------------------------- the grid

GameGrid::GameGrid(QWidget * Parent) :
    QListView(Parent)
{
    setViewMode(QListView::IconMode);
    setResizeMode(QListView::Adjust);
    setMovement(QListView::Static);
    setFlow(QListView::LeftToRight);
    setWrapping(true);
    setUniformItemSizes(true);
    setSpacing(0);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setFrameShape(QFrame::NoFrame);
    setMouseTracking(true);
    setItemDelegate(new CardDelegate(this));
    setContextMenuPolicy(Qt::CustomContextMenu);
    viewport()->setAutoFillBackground(false);
    setStyleSheet(QStringLiteral("QListView { background: transparent; border: 0; }"));
}

void GameGrid::UpdateGrid()
{
    // (room for the scroll bar is kept so that the cards do not jump when it appears)
    const int Available = width() - 2 * frameWidth() - verticalScrollBar()->sizeHint().width() - 6;
    if (qEnvironmentVariableIsSet("VORTEX_DEBUG_GRID"))
    {
        fprintf(stderr, "grid: width=%d viewport=%d available=%d\n", width(), viewport()->width(), Available);
    }
    if (Available <= 0)
    {
        return;
    }
    const int MinWidth = T().CardWidth;
    const int Columns = qMax(1, Available / MinWidth);
    const int Width = Available / Columns;
    setGridSize(QSize(Width, CardDelegate::CardHeightFor(Width)));
    if (qEnvironmentVariableIsSet("VORTEX_DEBUG_GRID"))
    {
        QTimer::singleShot(600, this, [this, Width]() {
            fprintf(stderr, "grid: cell=%d viewport=%d item5=(%d,%d) item6x=%d\n", Width, viewport()->width(), visualRect(model()->index(4, 0)).x(), visualRect(model()->index(4, 0)).y(), visualRect(model()->index(5, 0)).x());
        });
    }
}

void GameGrid::resizeEvent(QResizeEvent * Event)
{
    QListView::resizeEvent(Event);
    UpdateGrid();
}

void GameGrid::mouseMoveEvent(QMouseEvent * Event)
{
    QListView::mouseMoveEvent(Event);
    const QModelIndex Index = indexAt(Event->position().toPoint());
    bool OnStar = false;
    if (Index.isValid())
    {
        const QRect Card = visualRect(Index).adjusted(3, 3, -3, -3);
        OnStar = CardDelegate::StarRect(Card).adjusted(-4, -4, 4, 4).contains(Event->position().toPoint());
    }
    viewport()->setCursor(OnStar ? Qt::PointingHandCursor : Qt::ArrowCursor);
}

// ---------------------------------------------------------------- the browser

GameBrowser::GameBrowser(LibraryProxy * Proxy, QWidget * Parent) :
    QWidget(Parent),
    m_Proxy(Proxy)
{
    m_Stack = new QStackedLayout(this);
    m_Stack->setContentsMargins(0, 0, 0, 0);

    m_Content = new QWidget(this);
    m_Views = new QStackedLayout(m_Content);
    m_Views->setContentsMargins(0, 0, 0, 0);
    m_Grid = new GameGrid(m_Content);
    m_Grid->setModel(Proxy);
    m_List = new QTreeView(m_Content);
    m_List->setModel(Proxy);
    m_List->setSelectionModel(m_Grid->selectionModel());
    m_RowDelegate = new RowDelegate(m_List);
    m_List->setItemDelegate(m_RowDelegate);
    m_List->setRootIsDecorated(false);
    m_List->setUniformRowHeights(true);
    m_List->setSortingEnabled(true);
    m_List->setAllColumnsShowFocus(true);
    m_List->setMouseTracking(true);
    m_List->setFrameShape(QFrame::NoFrame);
    m_List->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_List->setContextMenuPolicy(Qt::CustomContextMenu);
    m_List->setStyleSheet(QStringLiteral("QTreeView { background: transparent; border: 0; }"));
    QHeaderView * Header = m_List->header();
    Header->setStretchLastSection(false);
    Header->setSectionResizeMode(LibraryModel::ColName, QHeaderView::Stretch);
    Header->setSectionResizeMode(LibraryModel::ColRegion, QHeaderView::Fixed);
    Header->setSectionResizeMode(LibraryModel::ColSize, QHeaderView::Fixed);
    Header->setSectionResizeMode(LibraryModel::ColFormat, QHeaderView::Fixed);
    Header->setSectionResizeMode(LibraryModel::ColLast, QHeaderView::Fixed);
    Header->setSectionResizeMode(LibraryModel::ColPlayed, QHeaderView::Fixed);
    Header->setSectionResizeMode(LibraryModel::ColFavorite, QHeaderView::Fixed);
    Header->resizeSection(LibraryModel::ColRegion, 100);
    Header->resizeSection(LibraryModel::ColSize, 90);
    Header->resizeSection(LibraryModel::ColFormat, 90);
    Header->resizeSection(LibraryModel::ColLast, 170);
    Header->resizeSection(LibraryModel::ColPlayed, 120);
    Header->resizeSection(LibraryModel::ColFavorite, 44);
    Header->setSectionsClickable(true);
    Header->setHighlightSections(false);
    m_Views->addWidget(m_Grid);
    m_Views->addWidget(m_List);
    m_Stack->addWidget(m_Content);

    m_Empty = new QWidget(this);
    QVBoxLayout * EmptyLayout = new QVBoxLayout(m_Empty);
    EmptyLayout->addStretch(1);
    QLabel * Logo = new QLabel(m_Empty);
    Logo->setPixmap(LogoPixmap(64, T().TextDisabled, devicePixelRatioF()));
    Logo->setAlignment(Qt::AlignCenter);
    m_EmptyTitle = new QLabel(m_Empty);
    SetRole(m_EmptyTitle, "heading");
    m_EmptyTitle->setAlignment(Qt::AlignCenter);
    m_EmptySubtitle = new QLabel(m_Empty);
    SetRole(m_EmptySubtitle, "muted");
    m_EmptySubtitle->setAlignment(Qt::AlignCenter);
    m_EmptySubtitle->setWordWrap(true);
    m_EmptyButton = new QPushButton(m_Empty);
    EmptyLayout->addWidget(Logo);
    EmptyLayout->addWidget(m_EmptyTitle);
    EmptyLayout->addWidget(m_EmptySubtitle);
    EmptyLayout->addWidget(m_EmptyButton, 0, Qt::AlignCenter);
    EmptyLayout->addStretch(2);
    m_Stack->addWidget(m_Empty);
    connect(m_EmptyButton, &QPushButton::clicked, this, &GameBrowser::ActionRequested);

    Connect(m_Grid);
    Connect(m_List);
    connect(m_Proxy, &QAbstractItemModel::rowsInserted, this, &GameBrowser::UpdateEmpty);
    connect(m_Proxy, &QAbstractItemModel::rowsRemoved, this, &GameBrowser::UpdateEmpty);
    connect(m_Proxy, &QAbstractItemModel::modelReset, this, &GameBrowser::UpdateEmpty);
    connect(m_Proxy, &QAbstractItemModel::layoutChanged, this, &GameBrowser::UpdateEmpty);
    connect(m_Grid->selectionModel(), &QItemSelectionModel::currentChanged, this, [this](const QModelIndex & Current) { Q_EMIT Selected(PathAt(Current)); });
    connect(&Covers::Instance(), &Covers::CoverReady, this, [this]() {
        m_Grid->viewport()->update();
        m_List->viewport()->update();
    });
    SetMode(QStringLiteral("grid"));
    UpdateEmpty();
}

void GameBrowser::Connect(QAbstractItemView * View)
{
    // Ctrl+D (or F) toggles the favourite of the current game from the keyboard
    for (const QKeySequence & Keys : {QKeySequence(Qt::CTRL | Qt::Key_D), QKeySequence(Qt::Key_F)})
    {
        QShortcut * Toggle = new QShortcut(Keys, View);
        Toggle->setContext(Qt::WidgetShortcut);
        connect(Toggle, &QShortcut::activated, this, [this]() {
            const QString Path = CurrentPath();
            if (!Path.isEmpty())
            {
                Store::Instance().SetFavorite(Path, !Store::Instance().IsFavorite(Path));
            }
        });
    }
    // (activated is emitted by the double click and by Return: connecting doubleClicked as well started the game twice)
    connect(View, &QAbstractItemView::activated, this, [this](const QModelIndex & Index) {
        if (Index.column() != LibraryModel::ColFavorite)
        {
            Q_EMIT Activated(PathAt(Index));
        }
    });
    connect(View, &QWidget::customContextMenuRequested, this, [this, View](const QPoint & Pos) {
        const QModelIndex Index = View->indexAt(Pos);
        if (Index.isValid())
        {
            View->setCurrentIndex(Index.sibling(Index.row(), 0));
            Q_EMIT ContextMenuRequested(PathAt(Index), View->viewport()->mapToGlobal(Pos));
        }
    });
}

QString GameBrowser::PathAt(const QModelIndex & Index) const
{
    return Index.isValid() ? Index.data(LibraryModel::PathRole).toString() : QString();
}

QString GameBrowser::CurrentPath() const
{
    return PathAt(m_Grid->selectionModel()->currentIndex());
}

void GameBrowser::SelectPath(const QString & Path)
{
    for (int Row = 0; Row < m_Proxy->rowCount(); Row++)
    {
        const QModelIndex Index = m_Proxy->index(Row, 0);
        if (Index.data(LibraryModel::PathRole).toString() == Path)
        {
            m_Grid->selectionModel()->setCurrentIndex(Index, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
            m_Grid->scrollTo(Index);
            m_List->scrollTo(Index);
            return;
        }
    }
}

void GameBrowser::SelectFirstIfNone()
{
    if (!CurrentPath().isEmpty() || m_Proxy->rowCount() == 0)
    {
        return;
    }
    const QModelIndex First = m_Proxy->index(0, 0);
    m_Grid->selectionModel()->setCurrentIndex(First, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
}

void GameBrowser::SetMode(const QString & Mode)
{
    m_Mode = Mode;
    const bool Grid = Mode == QLatin1String("grid");
    m_Views->setCurrentWidget(Grid ? static_cast<QWidget *>(m_Grid) : m_List);
    if (!Grid)
    {
        m_RowDelegate->SetCompact(Mode == QLatin1String("compact"));
        m_List->doItemsLayout();
    }
    else
    {
        m_Grid->UpdateGrid();
    }
    const QModelIndex Current = m_Grid->selectionModel()->currentIndex();
    if (Current.isValid())
    {
        m_Grid->scrollTo(Current);
        m_List->scrollTo(Current);
    }
}

void GameBrowser::FocusView()
{
    m_Views->currentWidget()->setFocus();
}

bool GameBrowser::EmptyShown() const
{
    return m_Stack->currentWidget() == m_Empty;
}

void GameBrowser::SetEmpty(const QString & Title, const QString & Subtitle, const QString & Button)
{
    m_EmptyTitle->setText(Title);
    m_EmptySubtitle->setText(Subtitle);
    m_EmptyButton->setText(Button);
    m_EmptyButton->setVisible(!Button.isEmpty());
}

void GameBrowser::UpdateEmpty()
{
    const bool Empty = m_Proxy->rowCount() == 0;
    m_Stack->setCurrentWidget(Empty ? m_Empty : m_Content);
    if (Empty)
    {
        Q_EMIT Selected(QString());
    }
}
} // namespace Vortex
