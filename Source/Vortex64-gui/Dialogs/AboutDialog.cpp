#include "AboutDialog.h"
#include "../Vortex/Icons.h"
#include "../Vortex/Strings.h"
#include "../Vortex/Theme.h"
#include "../Vortex/Version.h"

#include <Vortex64-core/Version.h>

#include <QApplication>
#include <QDesktopServices>
#include <QDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QTextBrowser>
#include <QUrl>
#include <QVBoxLayout>

using Vortex::Tr;

namespace
{
// The people the project owes its core to: the thanks of Project64 (CAboutDlg::OnInitDialog), unchanged
const char * const PreviousMembers = "Jabo, Smiff, Gent";
const char * const ThanksAlso = "Jahra!n, Witten, RadeonUser, Azimer, Shygoo, Frank, LuigiBlood, theboy181, Gonetz, BlueToonYoshi, Kimbjo, Melchior, retroben, AIO, krom";

struct Component
{
    const char * Name;
    const char * RoleSpanish;
    const char * RoleEnglish;
    const char * License;
};

// What Vortex64 is made of; the licences are those of the sources in the tree (Source/3rdParty/*/LICENSE)
const Component Components[] = {
    {"Project64", "Núcleo de emulación (CPU, JIT, memoria, RCP, ROMs, trucos, depurador)", "Emulation core (CPU, JIT, memory, RCP, ROMs, cheats, debugger)", "GPL-2.0"},
    {"Project64 Video / Glide64", "Plugin de vídeo", "Video plugin", "GPL-2.0"},
    {"GLideN64", "Plugin de vídeo (alto rendimiento)", "Video plugin (high performance)", "GPL-2.0"},
    {"Project64 Audio / Input / RSP", "Plugins de audio (SDL), entrada y RSP", "Audio (SDL), input and RSP plugins", "GPL-2.0"},
    {"Qt 6", "Interfaz gráfica (Qt Widgets)", "Graphical interface (Qt Widgets)", "LGPL-3.0"},
    {"SDL 2", "Audio, entrada y ventana del plugin de vídeo", "Audio, input and window of the video plugin", "zlib"},
    {"AsmJit", "Generación de código del recompilador", "Code generation of the recompiler", "zlib"},
    {"Duktape", "Motor de scripts JavaScript del depurador", "JavaScript scripting engine of the debugger", "MIT"},
    {"SoftFloat 3e", "Coma flotante del núcleo", "Floating point of the core", "BSD"},
    {"libpng / zlib / minizip", "Imágenes PNG, ZIP", "PNG images, ZIP", "libpng / zlib"},
    {"7-Zip (decodificador)", "ROMs en archivos .7z", "ROMs in .7z archives", "Public domain (LZMA SDK)"},
    {"Discord RPC", "Presencia enriquecida en Discord", "Rich presence in Discord", "MIT"},
};

QDialog * MakeDialog(QWidget * Parent, const QString & Title, const QSize & Size)
{
    QDialog * Dialog = new QDialog(Parent);
    Dialog->setWindowTitle(QStringLiteral("Vortex64 - ") + Title);
    Dialog->resize(Size);
    Dialog->setAttribute(Qt::WA_DeleteOnClose);
    return Dialog;
}

void ShowCredits(QWidget * Parent)
{
    QDialog * Dialog = MakeDialog(Parent, Tr("Créditos", "Credits"), QSize(560, 460));
    Dialog->setObjectName("VxCredits");
    QVBoxLayout * Layout = new QVBoxLayout(Dialog);
    QTextBrowser * Text = new QTextBrowser(Dialog);
    Text->setOpenExternalLinks(true);
    Text->setHtml(QString::fromUtf8(
                      "<h3>Vortex64</h3><p>%1</p>"
                      "<h3>Project64</h3><p>%2</p><p><b>%3</b><br>%4</p><p><b>%5</b><br>%6</p>"
                      "<h3>GLideN64</h3><p>%7</p>")
                      .arg(Tr("Interfaz de Vortex64 (biblioteca, ajustes, entrada, depurador) sobre el núcleo de Project64.", "Vortex64 frontend (library, settings, input, debugger) on top of the Project64 core.").toHtmlEscaped())
                      .arg(Tr("Vortex64 se basa en el emulador Project64 y en su equipo de desarrollo. Todo el mérito del núcleo es suyo.", "Vortex64 is based on the Project64 emulator and its developers. All the credit of the core is theirs.").toHtmlEscaped())
                      .arg(Tr("Agradecimientos a los antiguos miembros del núcleo:", "Special thanks to previous core members:").toHtmlEscaped())
                      .arg(QString::fromUtf8(PreviousMembers))
                      .arg(Tr("Gracias también a:", "Thanks also goes to:").toHtmlEscaped())
                      .arg(QString::fromUtf8(ThanksAlso))
                      .arg(Tr("Plugin de vídeo de Gonetz y colaboradores.", "Video plugin by Gonetz and contributors.").toHtmlEscaped()));
    QPushButton * Close = new QPushButton(Tr("Cerrar", "Close"), Dialog);
    Close->setObjectName("VxClose");
    QObject::connect(Close, &QPushButton::clicked, Dialog, &QDialog::accept);
    Layout->addWidget(Text, 1);
    Layout->addWidget(Close, 0, Qt::AlignRight);
    Dialog->exec();
}

void ShowComponents(QWidget * Parent)
{
    QDialog * Dialog = MakeDialog(Parent, Tr("Componentes y licencias", "Components and licenses"), QSize(760, 460));
    Dialog->setObjectName("VxComponents");
    QVBoxLayout * Layout = new QVBoxLayout(Dialog);
    QTableWidget * Table = new QTableWidget(Dialog);
    Table->setColumnCount(3);
    Table->setHorizontalHeaderLabels({Tr("Componente", "Component"), Tr("Uso", "Use"), Tr("Licencia", "License")});
    const int Count = int(sizeof(Components) / sizeof(Components[0]));
    Table->setRowCount(Count + 1);
    for (int i = 0; i < Count; i++)
    {
        Table->setItem(i, 0, new QTableWidgetItem(QString::fromUtf8(Components[i].Name)));
        Table->setItem(i, 1, new QTableWidgetItem(Tr(Components[i].RoleSpanish, Components[i].RoleEnglish)));
        Table->setItem(i, 2, new QTableWidgetItem(Tr(Components[i].License)));
    }
    // the versions that are known at run time
    Table->setItem(Count, 0, new QTableWidgetItem(QStringLiteral("Qt ") + QString::fromLatin1(qVersion())));
    Table->setItem(Count, 1, new QTableWidgetItem(Tr("Versión en uso", "Version in use")));
    Table->setItem(Count, 2, new QTableWidgetItem(QStringLiteral("Project64 ") + QStringLiteral(VER_FILE_VERSION_STR)));
    Table->verticalHeader()->hide();
    Table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    Table->setSelectionBehavior(QAbstractItemView::SelectRows);
    Table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    Table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    Table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    QLabel * Note = new QLabel(Tr("Vortex64 se distribuye bajo la GNU GPL versión 2 (consulta license.md); cada componente conserva su licencia.", "Vortex64 is distributed under the GNU GPL version 2 (see license.md); each component keeps its own license."), Dialog);
    Vortex::SetRole(Note, "muted");
    Note->setWordWrap(true);
    QPushButton * Close = new QPushButton(Tr("Cerrar", "Close"), Dialog);
    Close->setObjectName("VxClose");
    QObject::connect(Close, &QPushButton::clicked, Dialog, &QDialog::accept);
    Layout->addWidget(Table, 1);
    Layout->addWidget(Note);
    Layout->addWidget(Close, 0, Qt::AlignRight);
    Dialog->exec();
}
} // namespace

// The About window: the logo, the name, the version, what it is and the buttons for the credits and the components
void ShowAboutDialog(QWidget * Parent)
{
    QDialog Dialog(Parent);
    Dialog.setObjectName("VxAbout");
    Dialog.setWindowTitle(QStringLiteral("Vortex64 - ") + Tr("Acerca de", "About"));
    Dialog.setMinimumSize(520, 520);
    QVBoxLayout * Layout = new QVBoxLayout(&Dialog);
    Layout->setContentsMargins(32, 28, 32, 24);
    Layout->setSpacing(6);

    QLabel * Logo = new QLabel(&Dialog);
    Logo->setPixmap(Vortex::LogoPixmap(150, Vortex::T().TextPrimary, Dialog.devicePixelRatioF()));
    Logo->setAlignment(Qt::AlignCenter);
    Layout->addWidget(Logo);
    QLabel * Name = new QLabel(QStringLiteral("Vortex64"), &Dialog);
    Name->setObjectName("VxAboutName");
    QFont Font = Name->font();
    Font.setPixelSize(40);
    Font.setWeight(QFont::DemiBold);
    Name->setFont(Font);
    Name->setAlignment(Qt::AlignCenter);
    Layout->addWidget(Name);
    auto Line = [&](const QString & Text, bool Bold, const char * Id) {
        QLabel * Label = new QLabel(Text, &Dialog);
        Label->setObjectName(Id);
        Label->setAlignment(Qt::AlignCenter);
        Label->setWordWrap(true);
        if (Bold)
        {
            QFont F = Label->font();
            F.setBold(true);
            Label->setFont(F);
        }
        else
        {
            Vortex::SetRole(Label, "muted");
        }
        Layout->addWidget(Label);
    };
    Layout->addSpacing(8);
    // the product is presented as Vortex64; the attribution to Project64 and the other components stays in Credits and Components
    Line(Tr("Versión ", "Version ") + QStringLiteral(VORTEX_VERSION_STR), true, "VxAboutVersion");
    Line(QStringLiteral("Linux"), false, "VxAboutPlatform");
    Layout->addSpacing(4);
    Line(Tr("Emulador de Nintendo 64/64DD", "Nintendo 64/64DD emulator"), false, "VxAboutDescription");
    Line(Tr("Licencia: GPL-2.0", "License: GPL-2.0"), false, "VxAboutLicense");
    Layout->addStretch(1);

    QHBoxLayout * Buttons = new QHBoxLayout;
    Buttons->setSpacing(10);
    QPushButton * Credits = new QPushButton(Tr("Créditos", "Credits"), &Dialog);
    Credits->setObjectName("VxAboutCredits");
    QPushButton * Components = new QPushButton(Tr("Componentes", "Components"), &Dialog);
    Components->setObjectName("VxAboutComponents");
    QPushButton * Close = new QPushButton(Tr("Cerrar", "Close"), &Dialog);
    Close->setObjectName("VxAboutClose");
    Close->setDefault(true);
    Buttons->addWidget(Credits, 1);
    Buttons->addWidget(Components, 1);
    Buttons->addWidget(Close, 1);
    Layout->addLayout(Buttons);
    QObject::connect(Credits, &QPushButton::clicked, &Dialog, [&Dialog]() { ShowCredits(&Dialog); });
    QObject::connect(Components, &QPushButton::clicked, &Dialog, [&Dialog]() { ShowComponents(&Dialog); });
    QObject::connect(Close, &QPushButton::clicked, &Dialog, &QDialog::accept);
    Dialog.exec();
}
