#include "Theme.h"
#include "Icons.h"

#include <QApplication>
#include <QDialog>
#include <QMainWindow>
#include <QMessageBox>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QFile>
#include <QStyle>
#include <QStyleFactory>
#include <QWidget>
#include <QDir>
#include <QStandardPaths>

namespace Vortex
{
const Tokens & T()
{
    static const Tokens Value;
    return Value;
}

static QString C(const QColor & Color)
{
    return Color.name(QColor::HexRgb);
}

QFont MonoFont(int PointSize)
{
    QFont Font;
    Font.setFamilies(T().MonoFamily.split(", "));
    Font.setStyleHint(QFont::Monospace);
    Font.setFixedPitch(true);
    if (PointSize > 0)
    {
        Font.setPointSize(PointSize);
    }
    return Font;
}

void SetRole(QWidget * Widget, const char * Role)
{
    Widget->setProperty("vx", Role);
    Widget->style()->unpolish(Widget);
    Widget->style()->polish(Widget);
}

// QSS images must be files: the few glyphs a style sheet needs (the tick of a check box) are drawn into the cache directory
static QString GlyphFile(const char * Name, Icon Which, const QColor & Color, int Size)
{
    const QString Dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/vortex-glyphs");
    QDir().mkpath(Dir);
    const QString Path = Dir + QLatin1Char('/') + QLatin1String(Name) + QLatin1Char('-') + Color.name().mid(1) + QStringLiteral(".png");
    if (!QFile::exists(Path))
    {
        IconPixmap(Which, Color, Size, 2.0).save(Path, "PNG");
    }
    return Path;
}

static QString RadioGlyphFile(const QColor & Color)
{
    const QString Dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/vortex-glyphs");
    QDir().mkpath(Dir);
    const QString Path = Dir + QStringLiteral("/radio-") + Color.name().mid(1) + QStringLiteral(".png");
    if (!QFile::exists(Path))
    {
        QPixmap Pixmap(32, 32);
        Pixmap.fill(Qt::transparent);
        QPainter P(&Pixmap);
        P.setRenderHint(QPainter::Antialiasing);
        P.setPen(Qt::NoPen);
        P.setBrush(Color);
        P.drawEllipse(QPointF(16, 16), 7, 7);
        P.end();
        Pixmap.save(Path, "PNG");
    }
    return Path;
}

QString StyleSheet()
{
    const Tokens & K = T();
    QString S = QStringLiteral(R"CSS(
* { outline: 0; }
QWidget { color: @TextPrimary; selection-background-color: @Selected; selection-color: @TextPrimary; }
QMainWindow, QDialog, QWidget#VxRoot { background: @Background; }
QToolTip { background: @SurfaceElevated; color: @TextPrimary; border: 1px solid @Border; padding: 4px 6px; border-radius: @SmallRadiuspx; }
QLabel { background: transparent; }
QLabel[vx="title"] { font-size: 20px; font-weight: 600; }
QLabel[vx="heading"] { font-size: 15px; font-weight: 600; }
QLabel[vx="subtitle"], QLabel[vx="muted"] { color: @TextSecondary; }
QLabel[vx="mono"] { font-family: "@Mono"; }
QLabel:disabled { color: @TextDisabled; }

/* surfaces */
QWidget[vx="page"] { background: @Background; }
QWidget[vx="panel"], QFrame[vx="panel"] { background: @Surface; border: 1px solid @Border; border-radius: @CornerRadiuspx; }
QWidget[vx="sidebar"] { background: @Sidebar; border-right: 1px solid @Border; }
QWidget[vx="toolbar"] { background: @Background; }
QWidget[vx="detail"] { background: @Surface; border-top: 1px solid @Border; }
QWidget[vx="footer"] { background: @Sidebar; border-top: 1px solid @Border; }

/* sidebar navigation */
QPushButton[vx="nav"] { background: transparent; border: 0; border-radius: @SmallRadiuspx; text-align: left; padding: 0 12px; min-height: @ControlHeightpx; color: @TextPrimary; }
QPushButton[vx="nav"]:hover { background: @Hover; }
QPushButton[vx="nav"]:checked { background: @Selected; }
QPushButton[vx="nav"]:focus { border: 1px solid @Accent; }
QTabWidget#VxInputTabs::pane { border: 0; background: transparent; }
QFrame[vx="card"] { background: @Surface; border: 1px solid @Border; border-radius: @CornerRadiuspx; }
QLabel[vx="cardtitle"] { color: @TextSecondary; font-size: 11px; font-weight: 700; letter-spacing: 1px; background: transparent; border: 0; }
QWidget[vx="row"] { background: transparent; border-radius: @SmallRadiuspx; }
QWidget[vx="row"][hot="true"] { background: @Hover; }
QWidget[vx="row"][current="true"] { background: @Selected; }
QLabel[vx="rowlabel"] { background: transparent; border: 0; }
QPushButton[vx="binding"] { background: @SurfaceElevated; border: 1px solid @Border; border-radius: @SmallRadiuspx; min-height: 22px; padding: 0 10px; color: @TextPrimary; text-align: center; }
QPushButton[vx="binding"]:hover { border-color: @Accent; }
QPushButton[vx="binding"]:focus { border: 1px solid @Accent; }
QPushButton[vx="binding"][empty="true"] { color: @TextSecondary; }
QPushButton[vx="binding"][capturing="true"] { background: @Selected; border: 1px solid @Accent; color: @TextPrimary; font-weight: 600; }
QPushButton[vx="binding"]:disabled { color: @TextDisabled; border-color: @Border; }
QToolButton[vx="clear"] { background: transparent; border: 0; border-radius: 9px; min-width: 18px; max-width: 18px; min-height: 18px; max-height: 18px; padding: 0; color: @TextSecondary; font-size: 13px; }
QToolButton[vx="clear"]:hover { background: @Hover; color: @TextPrimary; }
QPushButton[vx="segment"] { background: @SurfaceElevated; border: 1px solid @Border; border-radius: @SmallRadiuspx; padding: 0 16px; min-height: @ControlHeightpx; color: @TextSecondary; }
QPushButton[vx="segment"]:hover { color: @TextPrimary; border-color: @Accent; }
QPushButton[vx="segment"]:checked { background: @Selected; border: 1px solid @Accent; color: @TextPrimary; font-weight: 600; }
QPushButton[vx="segment"]:disabled { color: @TextDisabled; }
QPushButton[vx="ghost"] { background: transparent; border: 0; color: @TextSecondary; padding: 0 8px; }
QPushButton[vx="ghost"]:hover { color: @TextPrimary; background: @Hover; }
QPushButton[vx="ghost"]:disabled { color: @TextDisabled; }

/* buttons */
QPushButton[vx="browse"] { padding: 0 6px; min-width: 40px; }
QPushButton, QToolButton { background: @SurfaceElevated; border: 1px solid @Border; border-radius: @SmallRadiuspx; padding: 0 14px; min-height: @ControlHeightpx; color: @TextPrimary; }
QPushButton:hover, QToolButton:hover { background: @Hover; }
QPushButton:pressed, QToolButton:pressed { background: @Selected; }
QPushButton:focus, QToolButton:focus { border: 1px solid @Accent; }
QPushButton:disabled, QToolButton:disabled { color: @TextDisabled; background: @Surface; border-color: @Border; }
QPushButton:default { background: @Selected; border: 1px solid #4d586b; }
QPushButton:default:hover { background: #46505f; }
QPushButton[vx="primary"] { background: #4a6089; border: 1px solid #6a82b0; color: #ffffff; font-weight: 700; font-size: 14px; padding: 0 24px; min-height: 40px; border-radius: @CornerRadiuspx; }
QPushButton[vx="primary"]:hover { background: #566e9c; }
QPushButton[vx="primary"]:pressed { background: #3d5078; }
QPushButton[vx="primary"]:disabled { background: @SurfaceElevated; border-color: @Border; color: @TextDisabled; }
QPushButton:pressed { padding-top: 1px; }
QPushButton[vx="danger"] { color: @Danger; }
QPushButton[vx="tool"] { padding: 0 6px; min-width: 24px; }
QTabBar::close-button { image: url(:/vortex/close.svg); subcontrol-position: right; }
QTabBar::close-button:hover { background: @Hover; border-radius: 4px; }
QToolButton[vx="view"] { min-height: 28px; min-width: 28px; padding: 0; background: transparent; border: 0; border-radius: @SmallRadiuspx; }
QToolButton[vx="view"]:hover { background: @Hover; }
QToolButton[vx="view"]:checked { background: @Selected; }
QToolButton[vx="star"] { min-height: 22px; min-width: 22px; padding: 0; background: transparent; border: 0; }
QToolButton[vx="icon"] { background: transparent; border: 0; padding: 0 6px; }
QToolButton[vx="icon"]:hover { background: @Hover; }

/* inputs */
QLineEdit, QSpinBox, QDoubleSpinBox, QPlainTextEdit, QTextEdit, QComboBox { background: @Surface; border: 1px solid @Border; border-radius: @SmallRadiuspx; padding: 0 8px; min-height: @ControlHeightpx; }
QTextEdit, QPlainTextEdit { padding: 6px 8px; }
QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus, QTextEdit:focus, QPlainTextEdit:focus { border: 1px solid @Accent; }
QLineEdit:disabled, QSpinBox:disabled, QComboBox:disabled { color: @TextDisabled; }
QLineEdit[vx="search"] { background: @Surface; border-radius: @CornerRadiuspx; padding-left: 4px; min-height: 36px; }
QComboBox { padding-right: 24px; }
QComboBox::drop-down { border: 0; width: 22px; }
QComboBox QAbstractItemView { background: @SurfaceElevated; border: 1px solid @Border; selection-background-color: @Selected; outline: 0; padding: 2px; }
QSpinBox::up-button, QSpinBox::down-button, QDoubleSpinBox::up-button, QDoubleSpinBox::down-button { width: 16px; border: 0; background: transparent; }

QCheckBox, QRadioButton { spacing: 8px; background: transparent; min-height: 24px; }
QCheckBox::indicator, QRadioButton::indicator { width: 16px; height: 16px; border: 1px solid #545b66; background: @Surface; }
QCheckBox::indicator { border-radius: 5px; }
QRadioButton::indicator { border-radius: 8px; }
QCheckBox::indicator:checked { background: @Accent; border-color: @Accent; image: url(:/vortex/check.svg); }
QRadioButton::indicator:checked { background: @Surface; border: 1px solid @Accent; image: url(:/vortex/radio.svg); }
QCheckBox::indicator:disabled, QRadioButton::indicator:disabled { background: @Background; border-color: @Border; }
QCheckBox:focus, QRadioButton:focus { color: @TextPrimary; }
QCheckBox:disabled, QRadioButton:disabled { color: @TextDisabled; }

QGroupBox { background: transparent; border: 1px solid @Border; border-radius: @CornerRadiuspx; margin-top: 14px; padding: 14px 10px 10px 10px; }
QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top left; left: 12px; padding: 0 6px; color: @TextSecondary; }

/* lists, trees, tables */
QAbstractItemView { background: @Surface; alternate-background-color: @SurfaceElevated; border: 1px solid @Border; border-radius: @SmallRadiuspx; gridline-color: @Border; outline: 0; }
QAbstractItemView::item { padding: 4px 6px; border: 0; }
QAbstractItemView::item:hover { background: @Hover; }
QAbstractItemView::item:selected { background: @Selected; color: @TextPrimary; }
QAbstractItemView:focus { border: 1px solid @Accent; }
QListView[vx="nav"], QListWidget[vx="nav"] { background: transparent; border: 0; }
QListWidget[vx="nav"]::item { padding: 0 10px; border-radius: @SmallRadiuspx; margin: 0; }
QListWidget[vx="nav"]::item:selected { background: @Selected; }
QListWidget[vx="nav"]::item:hover:!selected { background: @Hover; }
QHeaderView { background: transparent; border: 0; }
QHeaderView::section { background: @Surface; color: @TextSecondary; border: 0; border-bottom: 1px solid @Border; border-right: 1px solid @Border; padding: 5px 8px; font-weight: 500; }
QTableCornerButton::section { background: @Surface; border: 0; }

/* tabs */
QTabWidget::pane { border: 1px solid @Border; border-radius: @SmallRadiuspx; top: -1px; background: @Surface; }
QTabBar { background: transparent; }
QTabBar::tab { background: transparent; color: @TextSecondary; padding: 7px 16px; border: 0; border-bottom: 2px solid transparent; margin-right: 2px; }
QTabBar::tab:hover { color: @TextPrimary; }
QTabBar::tab:selected { color: @TextPrimary; border-bottom: 2px solid @Accent; background: @Surface; }
QTabBar::tab:focus { color: @TextPrimary; }

/* menus */
QMenuBar { background: @Background; color: @TextPrimary; border-bottom: 1px solid @Border; padding: 2px 4px; }
QMenuBar::item { background: transparent; padding: 5px 10px; border-radius: @SmallRadiuspx; }
QMenuBar::item:selected, QMenuBar::item:pressed { background: @Hover; }
QMenu { background: @SurfaceElevated; border: 1px solid @Border; border-radius: 6px; padding: 4px; }
QMenu::item { padding: 6px 28px 6px 24px; border-radius: 6px; }
QMenu::item:selected { background: @Selected; }
QMenu::item:disabled { color: @TextDisabled; }
QMenu::separator { height: 1px; background: @Border; margin: 4px 8px; }
QMenu::indicator { width: 14px; height: 14px; left: 6px; }
QStatusBar { background: @Background; color: @TextSecondary; border-top: 1px solid @Border; }
QStatusBar::item { border: 0; }
QStatusBar QLabel { color: @TextSecondary; }

/* scrollbars */
QScrollBar:vertical { background: transparent; width: 12px; margin: 2px; }
QScrollBar:horizontal { background: transparent; height: 12px; margin: 2px; }
QScrollBar::handle { background: #3b4048; border-radius: 5px; min-height: 28px; min-width: 28px; }
QScrollBar::handle:hover { background: #4a505a; }
QScrollBar::add-line, QScrollBar::sub-line, QScrollBar::add-page, QScrollBar::sub-page { background: transparent; border: 0; width: 0; height: 0; }
QAbstractScrollArea { background: transparent; }
QScrollArea { border: 0; background: transparent; }
QScrollArea > QWidget > QWidget { background: transparent; }

QSlider::groove:horizontal { height: 4px; background: @Border; border-radius: 2px; }
QSlider::sub-page:horizontal { background: @Accent; border-radius: 2px; }
QSlider::handle:horizontal { background: @TextPrimary; width: 14px; height: 14px; margin: -6px 0; border-radius: 7px; }
QProgressBar { background: @Surface; border: 1px solid @Border; border-radius: @SmallRadiuspx; text-align: center; min-height: 16px; }
QProgressBar::chunk { background: @Accent; border-radius: 4px; }
QSplitter::handle { background: @Border; }
QSplitter::handle:horizontal { width: 1px; }
QSplitter::handle:vertical { height: 1px; }
QMessageBox { background: @Background; }
QDialogButtonBox QPushButton { min-width: 88px; }
QFrame[frameShape="4"], QFrame[frameShape="5"] { color: @Border; }
)CSS");
    const QList<QPair<QString, QString>> Subs = {
        {"@SurfaceElevated", C(K.SurfaceElevated)}, {"@Surface", C(K.Surface)}, {"@Background", C(K.Background)}, {"@Sidebar", C(K.Sidebar)},
        {"@Hover", C(K.Hover)}, {"@Selected", C(K.Selected)}, {"@Border", C(K.Border)},
        {"@TextPrimary", C(K.TextPrimary)}, {"@TextSecondary", C(K.TextSecondary)}, {"@TextDisabled", C(K.TextDisabled)},
        {":/vortex/check.svg", GlyphFile("check", Icon::Check, QColor(0x10, 0x12, 0x15), 14)},
        {":/vortex/radio.svg", RadioGlyphFile(K.Accent)},
        {":/vortex/close.svg", GlyphFile("close", Icon::Close, K.TextSecondary, 12)},
        {"@Accent", C(K.Accent)}, {"@Danger", C(K.Danger)},
        {"@CornerRadius", QString::number(K.CornerRadius)}, {"@SmallRadius", QString::number(K.SmallRadius)},
        {"@ControlHeight", QString::number(K.ControlHeight)}, {"@Mono", K.MonoFamily.section(',', 0, 0)}};
    for (const auto & Sub : Subs)
    {
        S.replace(Sub.first, Sub.second);
    }
    return S;
}

namespace
{
// Every window of the application says whose it is: "Vortex64 - <title>" (the windows that built their own title keep it,
// those of the core's dialogs and of the plugins get the prefix)
class BrandFilter : public QObject
{
public:
    using QObject::QObject;
    bool eventFilter(QObject * Watched, QEvent * Event) override
    {
        if (Event->type() == QEvent::Show || Event->type() == QEvent::WindowTitleChange)
        {
            QWidget * Widget = qobject_cast<QWidget *>(Watched);
            if (Widget != nullptr && Widget->isWindow() && !m_Busy && (qobject_cast<QDialog *>(Widget) != nullptr || qobject_cast<QMainWindow *>(Widget) != nullptr) && qobject_cast<QMessageBox *>(Widget) == nullptr)
            {
                const QString Title = Widget->windowTitle();
                if (!Title.startsWith(QLatin1String("Vortex64")))
                {
                    m_Busy = true;
                    Widget->setWindowTitle(Title.isEmpty() ? QStringLiteral("Vortex64") : QStringLiteral("Vortex64 - ") + Title);
                    m_Busy = false;
                }
            }
        }
        return false;
    }

private:
    bool m_Busy = false;
};
} // namespace

void ApplyTheme(QApplication & App)
{
    App.installEventFilter(new BrandFilter(&App));
    const Tokens & K = T();
    if (QStyle * Fusion = QStyleFactory::create(QStringLiteral("Fusion")))
    {
        App.setStyle(Fusion);
    }
    QPalette P;
    P.setColor(QPalette::Window, K.Background);
    P.setColor(QPalette::WindowText, K.TextPrimary);
    P.setColor(QPalette::Base, K.Surface);
    P.setColor(QPalette::AlternateBase, K.SurfaceElevated);
    P.setColor(QPalette::Text, K.TextPrimary);
    P.setColor(QPalette::PlaceholderText, K.TextDisabled);
    P.setColor(QPalette::Button, K.SurfaceElevated);
    P.setColor(QPalette::ButtonText, K.TextPrimary);
    P.setColor(QPalette::BrightText, K.Danger);
    P.setColor(QPalette::ToolTipBase, K.SurfaceElevated);
    P.setColor(QPalette::ToolTipText, K.TextPrimary);
    P.setColor(QPalette::Highlight, K.Selected);
    P.setColor(QPalette::HighlightedText, K.TextPrimary);
    P.setColor(QPalette::Link, K.Accent);
    P.setColor(QPalette::Light, K.Hover);
    P.setColor(QPalette::Mid, K.Border);
    P.setColor(QPalette::Dark, K.Background);
    P.setColor(QPalette::Disabled, QPalette::WindowText, K.TextDisabled);
    P.setColor(QPalette::Disabled, QPalette::Text, K.TextDisabled);
    P.setColor(QPalette::Disabled, QPalette::ButtonText, K.TextDisabled);
    App.setPalette(P);
    App.setStyleSheet(StyleSheet());
}
} // namespace Vortex
