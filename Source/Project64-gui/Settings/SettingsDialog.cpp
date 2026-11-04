#include "SettingsDialog.h"
#include <Project64-core/N64System/ConfigPause.h>
#include "../Vortex/Icons.h"
#include "../Vortex/Language.h"
#include "../Vortex/Strings.h"
#include "../Vortex/Theme.h"
#include "SettingsPages.h"

#include <algorithm>
#include "ShortCutsPage.h"
#include "VortexPages.h"

#include <Project64-core/N64System/N64System.h>
#include <Project64-core/N64System/SystemGlobals.h>
#include <Project64-core/Plugins/Plugin.h>
#include <Project64-core/Settings.h>

#include <QCheckBox>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QHBoxLayout>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QVBoxLayout>

using Vortex::Icon;
using Vortex::Tr;

SettingsDialog::SettingsDialog(bool GameSettingsOnly, QWidget * parent) :
    QDialog(parent),
    m_Nav(new QListWidget(this)),
    m_Stack(new QStackedWidget(this)),
    m_GameSettingsOnly(GameSettingsOnly)
{
    setObjectName("SettingsDialog");
    setWindowTitle(QStringLiteral("Vortex64 - ") + Tr("Ajustes", "Settings"));
    setModal(true);
    resize(900, 600);
    setMinimumSize(720, 480);

    m_Nav->setProperty("vx", "nav");
    m_Nav->setObjectName("VxSettingsNav");
    m_Nav->setFixedWidth(200);
    m_Nav->setIconSize(QSize(18, 18));
    m_Nav->setFocusPolicy(Qt::StrongFocus);
    m_Nav->setFrameShape(QFrame::NoFrame);
    m_Nav->setSpacing(1);

    m_Title = new QLabel(this);
    m_Title->setObjectName("VxSettingsTitle");
    Vortex::SetRole(m_Title, "title");

    m_ResetPage = new QPushButton(Tr("Restablecer página", "Reset page"), this);
    m_ResetAll = new QPushButton(Tr("Restablecer todo", "Reset all"), this);
    QPushButton * Ok = new QPushButton(Tr("Aceptar", "OK"), this);
    Ok->setObjectName("VxOk");
    QPushButton * Cancel = new QPushButton(Tr("Cancelar", "Cancel"), this);
    Cancel->setObjectName("VxCancel");
    m_Apply = new QPushButton(Tr("Aplicar", "Apply"), this);
    m_Apply->setObjectName("VxApply");
    Ok->setDefault(true);
    m_ResetPage->setObjectName("VxResetPage");
    m_ResetAll->setObjectName("VxResetAll");

    QHBoxLayout * Buttons = new QHBoxLayout;
    Buttons->setContentsMargins(16, 10, 16, 10);
    Buttons->setSpacing(8);
    Buttons->addWidget(m_ResetPage);
    Buttons->addWidget(m_ResetAll);
    Buttons->addStretch(1);
    Buttons->addWidget(Ok);
    Buttons->addWidget(Cancel);
    Buttons->addWidget(m_Apply);
    QWidget * Footer = new QWidget(this);
    Vortex::SetRole(Footer, "footer");
    Footer->setAttribute(Qt::WA_StyledBackground, true);
    Footer->setLayout(Buttons);

    QWidget * Left = new QWidget(this);
    Vortex::SetRole(Left, "sidebar");
    Left->setAttribute(Qt::WA_StyledBackground, true);
    QVBoxLayout * LeftLayout = new QVBoxLayout(Left);
    LeftLayout->setContentsMargins(8, 12, 8, 12);
    LeftLayout->addWidget(m_Nav);

    QVBoxLayout * Right = new QVBoxLayout;
    Right->setContentsMargins(24, 18, 12, 0);
    Right->setSpacing(10);
    Right->addWidget(m_Title);
    Right->addWidget(m_Stack, 1);

    QHBoxLayout * Body = new QHBoxLayout;
    Body->setSpacing(0);
    Body->addWidget(Left);
    Body->addLayout(Right, 1);

    QVBoxLayout * Layout = new QVBoxLayout(this);
    Layout->setContentsMargins(0, 0, 0, 0);
    Layout->setSpacing(0);
    Layout->addLayout(Body, 1);
    Layout->addWidget(Footer);

    if (m_GameSettingsOnly)
    {
        BuildGame();
    }
    else
    {
        BuildGlobal();
    }

    connect(m_Nav, &QListWidget::currentRowChanged, this, [this](int Row) {
        if (Row >= 0 && Row < (int)m_Categories.size())
        {
            m_Stack->setCurrentWidget(m_Categories[Row]->Body);
            m_Title->setText(m_Categories[Row]->Title);
            RefreshButtons();
        }
    });
    connect(&Vortex::LanguageNotifier::Instance(), &Vortex::LanguageNotifier::Changed, this, [this]() { m_LanguageChanged = true; });
    connect(Ok, &QPushButton::clicked, this, &SettingsDialog::OnOk);
    connect(Cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_Apply, &QPushButton::clicked, this, &SettingsDialog::OnApply);
    connect(m_ResetPage, &QPushButton::clicked, this, &SettingsDialog::OnResetPage);
    connect(m_ResetAll, &QPushButton::clicked, this, &SettingsDialog::OnResetAll);

    m_Nav->setCurrentRow(0);
    RefreshButtons();
}

SettingsDialog::~SettingsDialog()
{
    for (Category * C : m_Categories)
    {
        delete C;
    }
}

int SettingsDialog::exec()
{
    ConfigPause Pause; // (nested with the dialogs of the plugins that this one opens)
    return QDialog::exec();
}

SettingsDialog::Category & SettingsDialog::AddCategory(const QString & Id, const QString & Title, int IconId)
{
    Category * C = new Category;
    C->Id = Id;
    C->Title = Title;
    C->Item = new QListWidgetItem(Vortex::MakeIcon((Icon)IconId, 18), Title, m_Nav);
    C->Item->setData(Qt::UserRole, Id);
    // the row is exactly this tall (the item and its hover / selection are the same rectangle; the style sheet adds no
    // margin or minimum height that could make the painted area differ from the row): the text and the icon with air
    C->Item->setSizeHint(QSize(0, std::max(Vortex::T().ControlHeight + 2, m_Nav->fontMetrics().height() + 14)));
    QScrollArea * Scroll = new QScrollArea(m_Stack);
    Scroll->setObjectName("VxCategory_" + Id);
    Scroll->setWidgetResizable(true);
    Scroll->setFrameShape(QFrame::NoFrame);
    Scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    QWidget * Inner = new QWidget;
    Inner->setProperty("vx", "page");
    QVBoxLayout * InnerLayout = new QVBoxLayout(Inner);
    InnerLayout->setContentsMargins(0, 0, 14, 12);
    InnerLayout->setSpacing(18);
    InnerLayout->addStretch(1);
    Scroll->setWidget(Inner);
    C->Body = Scroll;
    m_Stack->addWidget(Scroll);
    m_Categories.push_back(C);
    return *C;
}

void SettingsDialog::AddWidget(Category & C, QWidget * Widget)
{
    QVBoxLayout * L = static_cast<QVBoxLayout *>(static_cast<QScrollArea *>(C.Body)->widget()->layout());
    L->insertWidget(L->count() - 1, Widget);
}

void SettingsDialog::AddPage(Category & C, QtSettingsPage * Page, const QString & SectionTitle)
{
    C.Pages.push_back(Page);
    m_AllPages.push_back(Page);
    connect(Page, &QtSettingsPage::PageChanged, this, &SettingsDialog::OnPageChanged);
    if (QtGeneralOptionsPage * General = qobject_cast<QtGeneralOptionsPage *>(Page))
    {
        connect(General, &QtGeneralOptionsPage::BasicModeChanged, this, [this](bool Hide) { UpdateAdvanced(!Hide); });
    }
    if (Page->layout() != nullptr)
    {
        Page->layout()->setContentsMargins(0, 0, 0, 0);
    }
    if (SectionTitle.isEmpty())
    {
        AddWidget(C, Page);
        return;
    }
    Vortex::Section * S = new Vortex::Section(SectionTitle);
    S->Body()->addWidget(Page);
    AddWidget(C, S);
}

// The categories of the global configuration; the order is that of the design
void SettingsDialog::BuildGlobal()
{
    // the pages first: some categories show a part of another one's page
    Vortex::LanguagePage * Language = new Vortex::LanguagePage(this);
    QtGeneralOptionsPage * General = new QtGeneralOptionsPage(this);
    Vortex::LibraryPage * Library = new Vortex::LibraryPage(this);
    QtDirectoriesPage * Directories = new QtDirectoriesPage(this);
    QtPluginPage * Plugins = new QtPluginPage(false, this);
    QtAdvancedOptionsPage * Advanced = new QtAdvancedOptionsPage(this);
    QtDefaultsOptionsPage * Defaults = new QtDefaultsOptionsPage(this);
    QtDiskDrivePage * Disk = new QtDiskDrivePage(this);
    QtShortCutsPage * ShortCuts = new QtShortCutsPage(this);

    {
        Category & C = AddCategory("general", Tr("General", "General"), (int)Icon::Settings);
        AddPage(C, Language);
        AddPage(C, General);
    }
    {
        Category & C = AddCategory("directories", Tr("Directorios", "Directories"), (int)Icon::Folder);
        AddPage(C, Library, Tr("Biblioteca de ROMs", "ROM library"));
        AddPage(C, Directories, Tr("Datos de Vortex64", "Vortex64 data"));
    }
    {
        Category & C = AddCategory("graphics", Tr("Gráficos", "Graphics"), (int)Icon::Graphics);
        Vortex::Section * S = new Vortex::Section(QString());
        S->Body()->addWidget(new Vortex::PluginRow(Plugins, PLUGIN_TYPE_VIDEO, Tr("Plugin de vídeo", "Video plugin"), S));
        if (Plugins->HleGfxBox() != nullptr)
        {
            S->Body()->addWidget(Vortex::MirrorCheck(Plugins->HleGfxBox(), S));
        }
        if (QCheckBox * Auto = Vortex::FindCheck(General, (SettingID)Setting_AutoFullscreen))
        {
            S->Body()->addWidget(Vortex::MirrorCheck(Auto, S));
        }
        if (QCheckBox * Exit = Vortex::FindCheck(Advanced, (SettingID)UserInterface_ExitFullscreenOnLoseFocus))
        {
            S->Body()->addWidget(Vortex::MirrorCheck(Exit, S));
        }
        if (QCheckBox * Fps = Vortex::FindCheck(Advanced, UserInterface_DisplayFrameRate))
        {
            S->Body()->addWidget(Vortex::MirrorCheck(Fps, S, Tr("Mostrar velocidad (FPS) en pantalla", "Show speed (FPS) on screen")));
        }
        AddWidget(C, S);
        AddPage(C, new Vortex::VSyncPage(this));
    }
    {
        Category & C = AddCategory("audio", Tr("Audio", "Audio"), (int)Icon::Audio);
        Vortex::Section * S = new Vortex::Section(QString());
        S->Body()->addWidget(new Vortex::PluginRow(Plugins, PLUGIN_TYPE_AUDIO, Tr("Plugin de audio", "Audio plugin"), S));
        if (Plugins->HleAudioBox() != nullptr)
        {
            S->Body()->addWidget(Vortex::MirrorCheck(Plugins->HleAudioBox(), S));
        }
        AddWidget(C, S);
    }
    {
        Category & C = AddCategory("input", Tr("Input", "Input"), (int)Icon::Gamepad);
        Vortex::Section * S = new Vortex::Section(QString());
        S->Body()->addWidget(new Vortex::PluginRow(Plugins, PLUGIN_TYPE_CONTROLLER, Tr("Plugin de entrada", "Input plugin"), S));
        AddWidget(C, S);
    }
    {
        Category & C = AddCategory("rsp", Tr("RSP", "RSP"), (int)Icon::Chip);
        Vortex::Section * S = new Vortex::Section(QString());
        S->Body()->addWidget(new Vortex::PluginRow(Plugins, PLUGIN_TYPE_RSP, Tr("Plugin RSP", "RSP plugin"), S));
        AddWidget(C, S);
    }
    {
        // the page of the Project64 port holds the choices; the category shows them as rows (like Graphics, Audio,
        // Input and RSP do for their kind)
        Category & C = AddCategory("plugins", Tr("Plugins", "Plugins"), (int)Icon::Plugin);
        AddPage(C, Plugins);
        Plugins->hide();
        Vortex::Section * S = new Vortex::Section(QString());
        S->Body()->addWidget(new Vortex::PluginRow(Plugins, PLUGIN_TYPE_VIDEO, Tr("Plugin de vídeo", "Video plugin"), S));
        S->Body()->addWidget(new Vortex::PluginRow(Plugins, PLUGIN_TYPE_AUDIO, Tr("Plugin de audio", "Audio plugin"), S));
        S->Body()->addWidget(new Vortex::PluginRow(Plugins, PLUGIN_TYPE_CONTROLLER, Tr("Plugin de entrada", "Input plugin"), S));
        S->Body()->addWidget(new Vortex::PluginRow(Plugins, PLUGIN_TYPE_RSP, Tr("Plugin RSP", "RSP plugin"), S));
        if (Plugins->HleGfxBox() != nullptr)
        {
            S->Body()->addWidget(Vortex::MirrorCheck(Plugins->HleGfxBox(), S));
        }
        if (Plugins->HleAudioBox() != nullptr)
        {
            S->Body()->addWidget(Vortex::MirrorCheck(Plugins->HleAudioBox(), S));
        }
        QLabel * DirText = new QLabel(Tr("Carpeta de plugins", "Plugin folder"), S);
        QHBoxLayout * DirRow = new QHBoxLayout;
        QLabel * Dir = new QLabel(QString::fromStdString(g_Settings->LoadStringVal(Directory_Plugin)), S);
        Dir->setObjectName("VxPluginDirectory");
        Vortex::SetRole(Dir, "muted");
        Dir->setTextInteractionFlags(Qt::TextSelectableByMouse);
        QPushButton * Change = new QPushButton(Tr("Cambiar…", "Change…"), S);
        Change->setObjectName("VxPluginDirectoryChange");
        connect(Change, &QPushButton::clicked, this, [this]() { ShowPage("directories"); });
        DirRow->addWidget(Dir, 1);
        DirRow->addWidget(Change);
        S->Body()->addWidget(DirText);
        S->Body()->addLayout(DirRow);
        AddWidget(C, S);
    }
    {
        Category & C = AddCategory("shortcuts", Tr("Atajos", "Shortcuts"), (int)Icon::Keyboard);
        AddPage(C, ShortCuts);
    }
    {
        Category & C = AddCategory("disk", QStringLiteral("64DD"), (int)Icon::Disk);
        AddPage(C, Disk);
    }
    {
        Category & C = AddCategory("advanced", Tr("Avanzado", "Advanced"), (int)Icon::Advanced);
        // (Vortex64 always starts the game it opens: the option of the classic "Start emulation" step is not shown)
        if (QCheckBox * AutoStart = Vortex::FindCheck(Advanced, Setting_AutoStart))
        {
            AutoStart->hide();
        }
        AddPage(C, Advanced, Tr("Opciones avanzadas", "Advanced options"));
        AddPage(C, Defaults, Tr("Valores por defecto de las ROM", "ROM defaults"));
    }
    UpdateAdvanced(!g_Settings->LoadBool(UserInterface_BasicMode));
}

// "Edit Game Settings": the section of the game that is loaded
void SettingsDialog::BuildGame()
{
    const std::string GameIni = g_Settings->LoadStringVal(Game_IniKey);
    const QString Name = QString::fromStdString(g_Settings->LoadStringVal(Rdb_GoodName));
    setWindowTitle(QStringLiteral("Vortex64 - ") + Tr("Ajustes de ", "Settings of ") + Name);
    if (GameIni.empty())
    {
        return;
    }
    {
        Category & C = AddCategory("game-general", Tr("General", "General"), (int)Icon::Settings);
        AddPage(C, new QtGameGeneralPage(this));
    }
    {
        Category & C = AddCategory("game-recompiler", Tr("Recompilador", "Recompiler"), (int)Icon::Chip);
        AddPage(C, new QtGameRecompilerPage(this));
    }
    {
        Category & C = AddCategory("game-plugins", Tr("Plugins", "Plugins"), (int)Icon::Plugin);
        AddPage(C, new QtPluginPage(true, this));
    }
    {
        Category & C = AddCategory("game-disk", QStringLiteral("64DD"), (int)Icon::Disk);
        AddPage(C, new QtGameDiskDrivePage(this));
    }
    if (g_Settings->LoadBool(Setting_RdbEditor))
    {
        Category & C = AddCategory("game-status", Tr("Estado", "Status"), (int)Icon::Info);
        AddPage(C, new QtGameStatusPage(this));
    }
}

// CSettingConfig::UpdateAdvanced: "Hide Advanced Settings" (BasicMode) hid the Advanced and Defaults pages. In Vortex64 the
// Advanced category is always there (the debugger, the 64DD and the defaults of the ROMs live in it); the option keeps
// hiding the advanced entries of the menus
void SettingsDialog::UpdateAdvanced(bool /*AdvancedMode*/)
{
    for (Category * C : m_Categories)
    {
        if (C->Id == "advanced")
        {
            C->Item->setHidden(false);
        }
    }
}

QStringList SettingsDialog::CategoryIds() const
{
    QStringList Ids;
    for (const Category * C : m_Categories)
    {
        if (!C->Item->isHidden())
        {
            Ids << C->Id;
        }
    }
    return Ids;
}

QString SettingsDialog::CurrentCategory() const
{
    const int Row = m_Nav->currentRow();
    return Row >= 0 && Row < (int)m_Categories.size() ? m_Categories[Row]->Id : QString();
}

void SettingsDialog::ShowPage(const QString & Name)
{
    for (int i = 0; i < (int)m_Categories.size(); i++)
    {
        if (m_Categories[i]->Id == Name && !m_Categories[i]->Item->isHidden())
        {
            m_Nav->setCurrentRow(i);
            return;
        }
    }
}

SettingsDialog::Category * SettingsDialog::Current()
{
    const int Row = m_Nav->currentRow();
    return Row >= 0 && Row < (int)m_Categories.size() ? m_Categories[Row] : nullptr;
}

void SettingsDialog::OnPageChanged()
{
    m_Changed = true;
    RefreshButtons();
    BoldChangedPages();
}

void SettingsDialog::RefreshButtons()
{
    Category * C = Current();
    m_Apply->setEnabled(m_Changed);
    bool CanReset = false;
    if (C != nullptr)
    {
        for (QtSettingsPage * P : C->Pages)
        {
            CanReset = CanReset || P->EnableReset();
        }
    }
    m_ResetPage->setEnabled(CanReset);
    bool Any = false;
    for (QtSettingsPage * P : m_AllPages)
    {
        Any = Any || P->EnableReset();
    }
    m_ResetAll->setEnabled(Any);
}

// Upstream shows a page title in bold while any control of the page differs from its default
void SettingsDialog::BoldChangedPages()
{
    for (Category * C : m_Categories)
    {
        bool Changed = false;
        for (QtSettingsPage * P : C->Pages)
        {
            Changed = Changed || P->EnableReset();
        }
        QFont Font = C->Item->font();
        Font.setBold(Changed);
        C->Item->setFont(Font);
    }
}

void SettingsDialog::OnApply()
{
    const bool BasicModeBefore = g_Settings->LoadBool(UserInterface_BasicMode);
    for (QtSettingsPage * Page : m_AllPages)
    {
        Page->ApplySettings(true);
    }
    const bool BasicModeAfter = g_Settings->LoadBool(UserInterface_BasicMode);
    if (BasicModeBefore != BasicModeAfter)
    {
        UpdateAdvanced(!BasicModeAfter);
    }
    m_Changed = false;
    RefreshButtons();
    BoldChangedPages();
    if (m_LanguageChanged)
    {
        m_Reopen = true;
        accept();
    }
}

QString SettingsDialog::CurrentPageId()
{
    Category * C = Current();
    return C != nullptr ? C->Id : QString();
}

void SettingsDialog::OnOk()
{
    for (QtSettingsPage * Page : m_AllPages)
    {
        Page->ApplySettings(false);
    }
    accept();
}

void SettingsDialog::OnResetPage()
{
    if (Category * C = Current())
    {
        for (QtSettingsPage * P : C->Pages)
        {
            P->ResetPage();
        }
    }
    RefreshButtons();
    BoldChangedPages();
}

void SettingsDialog::OnResetAll()
{
    for (QtSettingsPage * Page : m_AllPages)
    {
        Page->ResetPage();
    }
    RefreshButtons();
    BoldChangedPages();
}
