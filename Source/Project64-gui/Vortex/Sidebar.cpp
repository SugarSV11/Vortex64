#include "Sidebar.h"
#include "Icons.h"
#include "Strings.h"
#include "Theme.h"

#include <QButtonGroup>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace Vortex
{
Sidebar::Sidebar(QWidget * Parent) :
    QWidget(Parent)
{
    setObjectName("VxSidebar");
    SetRole(this, "sidebar");
    setAttribute(Qt::WA_StyledBackground, true);
    setFixedWidth(T().SidebarWidth);

    QVBoxLayout * Layout = new QVBoxLayout(this);
    Layout->setContentsMargins(12, 12, 12, 12);
    Layout->setSpacing(4);

    QWidget * Brand = new QWidget(this);
    Brand->setFixedHeight(T().HeaderHeight - 8);
    QHBoxLayout * BrandLayout = new QHBoxLayout(Brand);
    BrandLayout->setContentsMargins(6, 0, 0, 0);
    BrandLayout->setSpacing(10);
    QLabel * Logo = new QLabel(Brand);
    Logo->setPixmap(LogoPixmap(34, T().TextPrimary, devicePixelRatioF()));
    Logo->setFixedSize(34, 34);
    QLabel * Name = new QLabel(QStringLiteral("Vortex64"), Brand);
    Name->setObjectName("VxBrand");
    QFont Font = Name->font();
    Font.setPixelSize(19);
    Font.setWeight(QFont::DemiBold);
    Name->setFont(Font);
    BrandLayout->addWidget(Logo);
    BrandLayout->addWidget(Name, 1);
    Layout->addWidget(Brand);
    Layout->addSpacing(10);

    m_Group = new QButtonGroup(this);
    m_Group->setExclusive(true);
    m_Library = AddNav("library", this);
    m_Recent = AddNav("recent", this);
    m_Favorites = AddNav("favorites", this);
    m_Disks = AddNav("disks", this);
    for (QPushButton * B : {m_Library, m_Recent, m_Favorites, m_Disks})
    {
        Layout->addWidget(B);
    }
    Layout->addStretch(1);
    m_Settings = AddNav("settings", this);
    m_Debugger = AddNav("debugger", this);
    m_About = AddNav("about", this);
    for (QPushButton * B : {m_Settings, m_Debugger, m_About})
    {
        B->setCheckable(false);
        Layout->addWidget(B);
    }
    Retranslate();

    connect(m_Library, &QPushButton::clicked, this, [this]() { Q_EMIT PageRequested("library"); });
    connect(m_Recent, &QPushButton::clicked, this, [this]() { Q_EMIT PageRequested("recent"); });
    connect(m_Favorites, &QPushButton::clicked, this, [this]() { Q_EMIT PageRequested("favorites"); });
    connect(m_Disks, &QPushButton::clicked, this, [this]() { Q_EMIT PageRequested("disks"); });
    connect(m_Settings, &QPushButton::clicked, this, &Sidebar::SettingsRequested);
    connect(m_Debugger, &QPushButton::clicked, this, &Sidebar::DebuggerRequested);
    connect(m_About, &QPushButton::clicked, this, &Sidebar::AboutRequested);
    m_Library->setChecked(true);
}

QPushButton * Sidebar::AddNav(const QString & Id, QWidget * Parent)
{
    QPushButton * Button = new QPushButton(Parent);
    Button->setObjectName("VxNav_" + Id);
    Button->setProperty("vx", "nav");
    Button->setCheckable(true);
    Button->setIconSize(QSize(18, 18));
    Button->setCursor(Qt::PointingHandCursor);
    Button->setFocusPolicy(Qt::StrongFocus);
    m_Group->addButton(Button);
    return Button;
}

void Sidebar::Retranslate()
{
    m_Library->setText(Tr("  Biblioteca", "  Library"));
    m_Library->setIcon(MakeIcon(Icon::Library, 18));
    m_Recent->setText(Tr("  Recientes", "  Recent"));
    m_Recent->setIcon(MakeIcon(Icon::Recent, 18));
    m_Favorites->setText(Tr("  Favoritos", "  Favorites"));
    m_Favorites->setIcon(MakeIcon(Icon::Favorites, 18));
    m_Disks->setText(QStringLiteral("  64DD"));
    m_Disks->setIcon(MakeIcon(Icon::Disk, 18));
    m_Settings->setText(Tr("  Ajustes", "  Settings"));
    m_Settings->setIcon(MakeIcon(Icon::Settings, 18));
    m_Debugger->setText(QStringLiteral("  Debugger"));
    m_Debugger->setIcon(MakeIcon(Icon::Debugger, 18));
    m_About->setText(Tr("  Acerca de", "  About"));
    m_About->setIcon(MakeIcon(Icon::About, 18));
}

QPushButton * Sidebar::Button(const QString & Page) const
{
    if (Page == "library") return m_Library;
    if (Page == "recent") return m_Recent;
    if (Page == "favorites") return m_Favorites;
    if (Page == "disks") return m_Disks;
    if (Page == "settings") return m_Settings;
    if (Page == "debugger") return m_Debugger;
    if (Page == "about") return m_About;
    return nullptr;
}

void Sidebar::SetCurrent(const QString & Page)
{
    if (QPushButton * B = Button(Page))
    {
        B->setChecked(true);
    }
}

QString Sidebar::Current() const
{
    for (const QString & Id : {QStringLiteral("library"), QStringLiteral("recent"), QStringLiteral("favorites"), QStringLiteral("disks")})
    {
        if (Button(Id)->isChecked())
        {
            return Id;
        }
    }
    return QStringLiteral("library");
}
} // namespace Vortex
