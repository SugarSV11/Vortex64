#include "VortexPages.h"
#include "../Vortex/Store.h"
#include "../Vortex/Language.h"
#include "../Vortex/Strings.h"
#include "../Vortex/Theme.h"
#include "../PresentSync.h"
#include <QRegularExpression>
#include <QShowEvent>
#include <QSpinBox>
#include <QTimer>

#include <Project64-core/Multilanguage.h>
#include <Project64-core/Plugins/Plugin.h>
#include <Project64-core/Plugins/GFXPlugin.h>
#include <Project64-core/Plugins/AudioPlugin.h>
#include <Project64-core/Plugins/RSPPlugin.h>
#include <Project64-core/Plugins/ControllerPlugin.h>
#include <Project64-core/Settings.h>
#include <Project64-core/N64System/SystemGlobals.h>

#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

namespace Vortex
{
Section::Section(const QString & Title, QWidget * Parent) :
    QWidget(Parent)
{
    QVBoxLayout * Outer = new QVBoxLayout(this);
    Outer->setContentsMargins(0, 0, 0, 0);
    Outer->setSpacing(8);
    if (!Title.isEmpty())
    {
        QLabel * Label = new QLabel(Title, this);
        SetRole(Label, "heading");
        Outer->addWidget(Label);
    }
    m_Body = new QVBoxLayout;
    m_Body->setSpacing(8);
    Outer->addLayout(m_Body);
}

QCheckBox * FindCheck(QtSettingsPage * Page, SettingID Id)
{
    for (const auto & Control : Page->BoundControls())
    {
        if (Control.Id == Id && Control.Type == QtSettingsPage::BoundControl::CheckBox)
        {
            return qobject_cast<QCheckBox *>(Control.Widget);
        }
    }
    return nullptr;
}

QCheckBox * MirrorCheck(QCheckBox * Owner, QWidget * Parent, const QString & Text)
{
    QCheckBox * Mirror = new QCheckBox(Text.isEmpty() ? Owner->text() : Text, Parent);
    Mirror->setChecked(Owner->isChecked());
    Mirror->setEnabled(Owner->isEnabled());
    // the owner decides (it may ask a question and say no): the mirror follows what the owner ends up with
    QObject::connect(Mirror, &QCheckBox::clicked, Owner, [Owner, Mirror]() {
        Owner->click();
        Mirror->setChecked(Owner->isChecked());
    });
    QObject::connect(Owner, &QCheckBox::toggled, Mirror, [Mirror](bool On) {
        QSignalBlocker Block(Mirror);
        Mirror->setChecked(On);
    });
    return Mirror;
}

// ------------------------------------------------------------ Vertical synchronization

VSyncPage::VSyncPage(QWidget * Parent) :
    QtSettingsPage(Parent)
{
    QVBoxLayout * Layout = new QVBoxLayout(this);
    Layout->setContentsMargins(0, 0, 0, 0);
    Layout->setSpacing(6);
    // one grid: the labels and the controls of both rows line up
    QGridLayout * Grid = new QGridLayout;
    Grid->setHorizontalSpacing(12);
    Grid->setVerticalSpacing(8);
    QLabel * Text = new QLabel(Tr("Sincronización vertical", "Vertical synchronization"), this);
    m_Mode = new QComboBox(this);
    m_Mode->setObjectName("VxVSync");
    m_Mode->setMinimumWidth(260);
    m_Mode->addItem(Tr("Automática (recomendado)", "Automatic (recommended)"), QVariant::fromValue<uint>(PJ64_VSYNC_AUTO));
    m_Mode->addItem(Tr("Activada", "On"), QVariant::fromValue<uint>(PJ64_VSYNC_ON));
    m_Mode->addItem(Tr("Desactivada", "Off"), QVariant::fromValue<uint>(PJ64_VSYNC_OFF));
    m_Mode->setToolTip(Tr("Automática: la presentación se sincroniza con el monitor donde está la ventana (sin tearing) y la velocidad del juego no depende de sus Hz.\nActivada: siempre espera al monitor. Desactivada: nunca espera.", "Automatic: the presentation is synchronized with the monitor that holds the window (no tearing) and the speed of the game does not depend on its Hz.\nOn: always waits for the monitor. Off: never waits."));
    Grid->addWidget(Text, 0, 0);
    Grid->addWidget(m_Mode, 0, 1);
    // "Maximum FPS": limits how often a frame is presented, never the speed of the game (the plugins skip the presentation of a frame
    // that comes before its turn)
    QLabel * FpsText = new QLabel(Tr("FPS máximos", "Maximum FPS"), this);
    m_MaxFps = new QComboBox(this);
    m_MaxFps->setObjectName("VxMaxFps");
    m_MaxFps->setMinimumWidth(260);
    m_MaxFps->addItem(Tr("Automático", "Automatic"), 0);
    for (int Fps : {20, 30, 50, 60, 75, 90, 100, 120, 144, 165, 240})
    {
        m_MaxFps->addItem(QString("%1 FPS").arg(Fps), Fps);
    }
    m_MaxFps->addItem(Tr("Personalizado…", "Custom…"), -1);
    m_MaxFpsCustom = new QSpinBox(this);
    m_MaxFpsCustom->setObjectName("VxMaxFpsCustom");
    m_MaxFpsCustom->setRange(PresentSync::MinMaxFps, PresentSync::MaxMaxFps);
    m_MaxFpsCustom->setSuffix(" FPS");
    m_MaxFpsCustom->setKeyboardTracking(false);
    m_MaxFpsCustom->setValue(60);
    m_MaxFpsCustom->setToolTip(Tr("Entre 10 y 1000 FPS.", "Between 10 and 1000 FPS."));
    Grid->addWidget(FpsText, 1, 0);
    Grid->addWidget(m_MaxFps, 1, 1);
    Grid->addWidget(m_MaxFpsCustom, 1, 2);
    Grid->setColumnStretch(3, 1);
    Layout->addLayout(Grid);
    connect(m_MaxFps, QOverload<int>::of(&QComboBox::activated), this, [this]() {
        m_MaxFpsChanged = true;
        ShowMaxFpsEditor();
        Q_EMIT PageChanged();
    });
    connect(m_MaxFpsCustom, QOverload<int>::of(&QSpinBox::valueChanged), this, [this]() {
        m_MaxFpsChanged = true;
        Q_EMIT PageChanged();
    });
    // what is detected, not something to choose: it follows the monitor of the window
    m_Display = new QLabel(this);
    m_Display->setObjectName("VxCurrentDisplay");
    m_Display->setProperty("vx", "muted");
    Layout->addWidget(m_Display);
    AddModComboBox(m_Mode, (SettingID)UserInterface_VSyncMode);
    ShowMaxFpsEditor();
    m_Timer = new QTimer(this);
    m_Timer->setInterval(1000);
    connect(m_Timer, &QTimer::timeout, this, [this]() { ShowDisplay(); });
    UpdatePageSettings();
    ShowDisplay();
}

// the tooltip follows the choice; the box of the value is only there for "Custom..."
void VSyncPage::ShowMaxFpsEditor()
{
    const bool Auto = m_MaxFps->currentData().toInt() == 0;
    m_MaxFps->setToolTip(Auto ? Tr("Adapta la presentación a la pantalla y al contenido.", "Adapts presentation to the display and content.")
                              : Tr("Limita la frecuencia de presentación sin cambiar la velocidad de emulación.", "Limits presentation rate without changing emulation speed."));
    m_MaxFpsCustom->setVisible(m_MaxFps->currentData().toInt() < 0);
}

int VSyncPage::ChosenMaxFps() const
{
    const int Data = m_MaxFps->currentData().toInt();
    return Data < 0 ? PresentSync::ValidMaxFps(m_MaxFpsCustom->value()) : Data;
}

void VSyncPage::UpdatePageSettings()
{
    QtSettingsPage::UpdatePageSettings();
    if (m_MaxFps == nullptr)
    {
        return;
    }
    const int Value = PresentSync::ValidMaxFps(g_Settings->LoadDword(UserInterface_MaxFps));
    int Index = m_MaxFps->findData(Value);
    if (Index < 0)
    {
        Index = m_MaxFps->findData(-1);
        m_MaxFpsCustom->setValue(Value);
    }
    m_MaxFps->setCurrentIndex(Index);
    m_MaxFpsChanged = false;
    ShowMaxFpsEditor();
}

void VSyncPage::ApplySettings(bool UpdateScreen)
{
    // (first: the base class shows the settings again at the end, and that would drop the choice that is not saved yet)
    if (m_MaxFpsChanged)
    {
        g_Settings->SaveDword(UserInterface_MaxFps, (uint32_t)ChosenMaxFps());
        m_MaxFpsChanged = false;
    }
    QtSettingsPage::ApplySettings(UpdateScreen);
    if (PresentSync::Instance() != nullptr)
    {
        PresentSync::Instance()->Refresh(); // (the plugin has it at once; the timer would say it within a quarter of a second)
    }
}

LanguageStringID VSyncPage::PageTitle()
{
    return TAB_OPTIONS;
}

bool VSyncPage::EnableReset()
{
    return false;
}

void VSyncPage::ResetPage()
{
}

void VSyncPage::ShowDisplay()
{
    QWidget * Top = window();
    const DisplayInfo Info = PresentSync::Instance() != nullptr ? PresentSync::Instance()->Display() : PresentSync::DisplayOf(Top);
    m_Display->setText(Tr("Pantalla actual: ", "Current display: ") + Info.Text() + (Info.RefreshHz <= 0 ? Tr(" (frecuencia no detectada)", " (refresh rate not detected)") : QString()));
}

void VSyncPage::showEvent(QShowEvent * Event)
{
    QtSettingsPage::showEvent(Event);
    ShowDisplay();
    m_Timer->start();
}

// ------------------------------------------------------------ Language

LanguagePage::LanguagePage(QWidget * Parent) :
    QtSettingsPage(Parent)
{
    QVBoxLayout * Layout = new QVBoxLayout(this);
    Layout->setContentsMargins(0, 0, 0, 0);
    QHBoxLayout * Row = new QHBoxLayout;
    QLabel * Text = new QLabel(Tr("Idioma", "Language"), this);
    Text->setMinimumWidth(110);
    m_Language = new QComboBox(this);
    m_Language->setObjectName("VxLanguage");
    m_Language->setMinimumWidth(240);
    Row->addWidget(Text);
    Row->addWidget(m_Language);
    Row->addStretch(1);
    Layout->addLayout(Row);
    m_ConfirmExit = new QCheckBox(Tr("Confirmar al salir con un juego en marcha", "Confirm on exit while a game is running"), this);
    m_ConfirmExit->setObjectName("VxConfirmExit");
    Layout->addWidget(m_ConfirmExit);

    // exactly the languages that are translated: Spanish and English
    for (const QString & Code : SupportedLanguages())
    {
        m_Language->addItem(LanguageName(Code), Code);
    }
    connect(m_Language, QOverload<int>::of(&QComboBox::activated), this, [this]() {
        m_Changed = true;
        Q_EMIT PageChanged();
    });
    connect(m_ConfirmExit, &QCheckBox::clicked, this, [this]() {
        m_Changed = true;
        Q_EMIT PageChanged();
    });
    UpdatePageSettings();
}

LanguageStringID LanguagePage::PageTitle()
{
    return TAB_OPTIONS;
}

void LanguagePage::UpdatePageSettings()
{
    m_ShownLanguage = LanguageCode();
    const int Index = m_Language->findData(m_ShownLanguage);
    if (Index >= 0)
    {
        m_Language->setCurrentIndex(Index);
    }
    m_ShownConfirm = Store::Instance().Value("confirmExit", false).toBool();
    m_ConfirmExit->setChecked(m_ShownConfirm);
    m_Changed = false;
}

void LanguagePage::ApplySettings(bool UpdateScreen)
{
    if (m_Changed)
    {
        const QString Chosen = m_Language->currentData().toString();
        if (!Chosen.isEmpty() && Chosen != m_ShownLanguage)
        {
            // everything changes at once: the texts, the core's language file, the windows (MainWindow::OnLanguageChanged)
            ApplyLanguage(Chosen);
        }
        Store::Instance().SetValue("confirmExit", m_ConfirmExit->isChecked());
    }
    if (UpdateScreen)
    {
        UpdatePageSettings();
    }
    m_Changed = false;
}

bool LanguagePage::EnableReset()
{
    return false;
}

void LanguagePage::ResetPage()
{
}

// ------------------------------------------------------------- Library

LibraryPage::LibraryPage(QWidget * Parent) :
    QtSettingsPage(Parent)
{
    QVBoxLayout * Layout = new QVBoxLayout(this);
    Layout->setContentsMargins(0, 0, 0, 0);
    Layout->setSpacing(8);
    QLabel * RomText = new QLabel(Tr("ROMs", "ROMs"), this);
    QHBoxLayout * RomRow = new QHBoxLayout;
    m_RomDir = new QLineEdit(this);
    m_RomDir->setObjectName("VxRomDirectory");
    QPushButton * Browse = new QPushButton(Tr("Explorar", "Browse"), this);
    Browse->setObjectName("VxRomDirectoryBrowse");
    RomRow->addWidget(m_RomDir, 1);
    RomRow->addWidget(Browse);
    Layout->addWidget(RomText);
    Layout->addLayout(RomRow);

    m_Recursive = new QCheckBox(QtGS(RB_DIR_RECURSION), this);
    m_Recursive->setObjectName("VxRecursive");
    Layout->addWidget(m_Recursive);

    QLabel * ExtraText = new QLabel(Tr("Carpetas adicionales de la biblioteca", "Additional library folders"), this);
    m_Extra = new QListWidget(this);
    m_Extra->setObjectName("VxExtraDirectories");
    m_Extra->setMaximumHeight(110);
    QHBoxLayout * Buttons = new QHBoxLayout;
    QPushButton * Add = new QPushButton(Tr("Añadir carpeta…", "Add folder…"), this);
    Add->setObjectName("VxExtraAdd");
    m_Remove = new QPushButton(Tr("Quitar", "Remove"), this);
    m_Remove->setObjectName("VxExtraRemove");
    m_Remove->setEnabled(false);
    Buttons->addWidget(Add);
    Buttons->addWidget(m_Remove);
    Buttons->addStretch(1);
    Layout->addWidget(ExtraText);
    Layout->addWidget(m_Extra);
    Layout->addLayout(Buttons);

    connect(Browse, &QPushButton::clicked, this, [this]() {
        const QString Dir = QFileDialog::getExistingDirectory(this, Tr("Carpeta de ROMs", "ROM folder"), m_RomDir->text());
        if (!Dir.isEmpty())
        {
            m_RomDir->setText(Dir.endsWith('/') ? Dir : Dir + '/');
            m_Changed = true;
            Q_EMIT PageChanged();
        }
    });
    connect(m_RomDir, &QLineEdit::textEdited, this, [this]() {
        m_Changed = true;
        Q_EMIT PageChanged();
    });
    connect(m_Recursive, &QCheckBox::clicked, this, [this]() {
        m_Changed = true;
        Q_EMIT PageChanged();
    });
    connect(Add, &QPushButton::clicked, this, [this]() {
        const QString Dir = QFileDialog::getExistingDirectory(this, Tr("Añadir carpeta de ROMs", "Add ROM folder"), m_RomDir->text());
        if (!Dir.isEmpty())
        {
            const QString WithSlash = Dir.endsWith('/') ? Dir : Dir + '/';
            if (m_Extra->findItems(WithSlash, Qt::MatchFixedString).isEmpty() && WithSlash != m_RomDir->text())
            {
                m_Extra->addItem(WithSlash);
                m_Changed = true;
                Q_EMIT PageChanged();
            }
        }
    });
    connect(m_Remove, &QPushButton::clicked, this, [this]() {
        delete m_Extra->takeItem(m_Extra->currentRow());
        m_Changed = true;
        Q_EMIT PageChanged();
    });
    connect(m_Extra, &QListWidget::currentRowChanged, this, [this](int Row) { m_Remove->setEnabled(Row >= 0); });
    UpdatePageSettings();
}

LanguageStringID LibraryPage::PageTitle()
{
    return TAB_DIRECTORY;
}

void LibraryPage::UpdatePageSettings()
{
    m_RomDir->setText(QString::fromStdString(g_Settings->LoadStringVal(RomList_GameDir)));
    m_Recursive->setChecked(g_Settings->LoadBool(RomList_GameDirRecursive));
    m_Extra->clear();
    m_Extra->addItems(Store::Instance().ExtraDirectories());
    m_Changed = false;
}

void LibraryPage::ApplySettings(bool UpdateScreen)
{
    if (m_Changed)
    {
        QStringList Extra;
        for (int i = 0; i < m_Extra->count(); i++)
        {
            Extra << m_Extra->item(i)->text();
        }
        const bool Recursive = m_Recursive->isChecked();
        if (g_Settings->LoadBool(RomList_GameDirRecursive) != Recursive)
        {
            g_Settings->SaveBool(RomList_GameDirRecursive, Recursive);
        }
        if (QString::fromStdString(g_Settings->LoadStringVal(RomList_GameDir)) != m_RomDir->text())
        {
            g_Settings->SaveString(RomList_GameDir, m_RomDir->text().toStdString());
        }
        // (one signal for all of it: the library scans again)
        Store::Instance().SetExtraDirectories(Extra);
    }
    if (UpdateScreen)
    {
        UpdatePageSettings();
    }
    m_Changed = false;
}

bool LibraryPage::EnableReset()
{
    return false;
}

void LibraryPage::ResetPage()
{
}

// ---------------------------------------------------------- Plugin rows

// The name that a plugin reports ("Project64 Video Plugin: Dev-4.0.0.9999-Unknown") as the user reads it: without the placeholder version of a
// development build and without the word "plugin" that the rows already say; the file says which one it is.
QString PluginDisplayName(const QString & Reported)
{
    QString Name = Reported.trimmed();
    static const QRegularExpression DevVersion(QStringLiteral("[:\\s]+Dev-[0-9A-Za-z.\\-]*$"));
    Name.remove(DevVersion);
    static const QRegularExpression PluginWord(QStringLiteral("\\s+plugin$"), QRegularExpression::CaseInsensitiveOption);
    Name.remove(PluginWord);
    static const QRegularExpression PluginWordColon(QStringLiteral("\\s+plugin(?=:)"), QRegularExpression::CaseInsensitiveOption);
    Name.remove(PluginWordColon);
    QStringList Words = Name.split(' ', Qt::SkipEmptyParts);
    for (QString & Word : Words)
    {
        if (!Word.isEmpty() && Word[0].isLower() && !Word.contains('.') && !Word.contains(QRegularExpression(QStringLiteral("[0-9]"))))
        {
            Word[0] = Word[0].toUpper();
        }
    }
    return Words.join(' ');
}

PluginRow::PluginRow(QtPluginPage * Owner, uint16_t Type, const QString & Title, QWidget * Parent) :
    QWidget(Parent),
    m_Owner(Owner),
    m_Type(Type)
{
    setObjectName(QStringLiteral("VxPluginRow_%1").arg(Type));
    QGridLayout * Layout = new QGridLayout(this);
    Layout->setContentsMargins(0, 0, 0, 0);
    Layout->setHorizontalSpacing(8);
    Layout->setVerticalSpacing(4);
    QLabel * Label = new QLabel(Title, this);
    m_Combo = new QComboBox(this);
    m_Combo->setObjectName("VxPluginCombo");
    m_Configure = new QPushButton(Tr("Configurar…", "Configure…"), this);
    m_Configure->setObjectName("VxPluginConfigure");
    m_About = new QPushButton(Tr("Acerca de…", "About…"), this);
    m_About->setObjectName("VxPluginAbout");
    m_Info = new QLabel(this);
    SetRole(m_Info, "muted");
    m_Info->setWordWrap(true);
    Layout->addWidget(Label, 0, 0, 1, 3);
    Layout->addWidget(m_Combo, 1, 0);
    Layout->addWidget(m_Configure, 1, 1);
    Layout->addWidget(m_About, 1, 2);
    Layout->addWidget(m_Info, 2, 0, 1, 3);
    Layout->setColumnStretch(0, 1);

    QComboBox * Source = Owner->Combo(Type);
    if (Source != nullptr)
    {
        for (int i = 0; i < Source->count(); i++)
        {
            m_Combo->addItem(PluginDisplayName(Source->itemText(i)), Source->itemData(i));
        }
        connect(m_Combo, QOverload<int>::of(&QComboBox::activated), Owner, [Source](int Index) {
            Source->setCurrentIndex(Index);
            QMetaObject::invokeMethod(Source, "activated", Q_ARG(int, Index));
        });
        connect(Source, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() { Refresh(); });
    }
    connect(Owner, &QtPluginPage::PluginChoiceChanged, this, &PluginRow::Refresh);
    connect(m_Configure, &QPushButton::clicked, this, [this]() {
        if (g_Plugins != nullptr)
        {
            g_Plugins->ConfigPlugin(reinterpret_cast<void *>(window()->winId()), (PLUGIN_TYPE)m_Type);
        }
    });
    connect(m_About, &QPushButton::clicked, this, [this]() { m_Owner->ShowAboutFor(m_Type); });
    Refresh();
}

void PluginRow::Refresh()
{
    QComboBox * Source = m_Owner->Combo(m_Type);
    if (Source != nullptr)
    {
        QSignalBlocker Block(m_Combo);
        m_Combo->setCurrentIndex(Source->currentIndex());
    }
    const CPluginList::PLUGIN * Plugin = m_Owner->PluginFor(m_Type);
    if (Plugin != nullptr)
    {
        m_Info->setText(QString::fromStdString(Plugin->FileName));
    }
    else
    {
        m_Info->setText(Tr("Ningún plugin seleccionado", "No plugin selected"));
    }
    m_About->setEnabled(Plugin != nullptr && Plugin->AboutFunction);
    // the configuration belongs to the plugin that is loaded: a choice that was not applied yet has none
    bool CanConfigure = false;
    QString Why;
    if (g_Plugins == nullptr)
    {
        Why = Tr("Los plugins aún no están cargados.", "The plugins are not loaded yet.");
    }
    else if (m_Owner->PendingChange(m_Type))
    {
        Why = Tr("Aplica el cambio de plugin para configurarlo.", "Apply the plugin change to configure it.");
    }
    else
    {
        switch (m_Type)
        {
        case PLUGIN_TYPE_VIDEO: CanConfigure = g_Plugins->Gfx() != nullptr && g_Plugins->Gfx()->DllConfig != nullptr; break;
        case PLUGIN_TYPE_AUDIO: CanConfigure = g_Plugins->Audio() != nullptr && g_Plugins->Audio()->DllConfig != nullptr; break;
        case PLUGIN_TYPE_RSP: CanConfigure = g_Plugins->RSP() != nullptr && g_Plugins->RSP()->DllConfig != nullptr; break;
        case PLUGIN_TYPE_CONTROLLER: CanConfigure = g_Plugins->Control() != nullptr && g_Plugins->Control()->DllConfig != nullptr; break;
        }
        if (!CanConfigure)
        {
            Why = Tr("Este plugin no tiene ventana de configuración.", "This plugin has no configuration window.");
        }
    }
    m_Configure->setEnabled(CanConfigure);
    m_Configure->setToolTip(Why);
}
} // namespace Vortex
