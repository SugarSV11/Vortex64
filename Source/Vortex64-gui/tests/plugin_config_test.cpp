// Regression test for the Qt configuration dialog of the video plugin
// (Source/Vortex64-video/ConfigQt.cpp, reached through CPlugins::ConfigPlugin
// -> DllConfig -> libVortex64-video-qtconfig.so). Runs offscreen with an
// isolated XDG config/data home:
//
//   plugin-config-test video|audio write   toggles "Vertical sync" (video) or
//                                          "Mute" (audio) in the dialog, OK
//   plugin-config-test video|audio read    reopens the dialog and checks the
//                                          change persisted
#include <Common/path.h>
#include <Vortex64-cli/LinuxNotification.h>
#include <Vortex64-core/AppInit.h>
#include <Vortex64-core/Notification.h>
#include <Vortex64-core/N64System/SystemGlobals.h>
#include <Vortex64-core/Plugins/AudioPlugin.h>
#include <Vortex64-core/Plugins/ControllerPlugin.h>
#include <Vortex64-core/Plugins/GFXPlugin.h>
#include <Vortex64-core/Plugins/Plugin.h>
#include <Vortex64-core/Settings.h>

#include <algorithm>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QKeyEvent>
#include <QLineEdit>
#include "input_ui_access.h"
#include <QSlider>
#include <QSpinBox>
#include <QFile>
#include <QStackedWidget>
#include <QTabWidget>
#include <QPushButton>
#include <QTimer>
#include <cstdio>
#include <cstring>

class DummyRenderWindow : public RenderWindow
{
public:
    void * GetWindowHandle(void) const override { return nullptr; }
};

static int g_Failures = 0;
#define CHECK(cond)                                                            \
    do                                                                         \
    {                                                                          \
        if (!(cond))                                                           \
        {                                                                      \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);             \
            g_Failures++;                                                      \
        }                                                                      \
    } while (0)

static bool g_ExpectChecked = false;
static const char * g_CheckText = "Vertical sync";
static bool g_Toggle = false;
static bool g_Seen = false;
static bool g_Prep = false; // video: a first pass that only switches on the page of the texture enhancement


// Video: every control of every page is changed in "write" (check boxes inverted, lists on the next entry, numbers
// on another value) and its final state saved next to the settings; "read" reopens the dialog and compares each
static QStringList VideoControlState(QDialog * Dialog)
{
    QStringList State;
    int Index = 0;
    for (QCheckBox * C : Dialog->findChildren<QCheckBox *>())
    {
        State << QString("check %1 %2").arg(Index++).arg(C->isChecked());
    }
    Index = 0;
    for (QComboBox * C : Dialog->findChildren<QComboBox *>())
    {
        State << QString("combo %1 %2").arg(Index++).arg(C->currentText());
    }
    Index = 0;
    for (QSpinBox * C : Dialog->findChildren<QSpinBox *>())
    {
        State << QString("spin %1 %2").arg(Index++).arg(C->value());
    }
    return State;
}

static void VideoChangeAll(QDialog * Dialog)
{
    for (QCheckBox * C : Dialog->findChildren<QCheckBox *>())
    {
        // (this one adds and removes a page of the dialog)
        if (C->isEnabled() && C->text() != "Show texture enhancement options" && C->text() != "Vertical sync")
        {
            C->setChecked(!C->isChecked());
        }
    }
    for (QComboBox * C : Dialog->findChildren<QComboBox *>())
    {
        if (C->isEnabled() && C->count() > 1)
        {
            C->setCurrentIndex((C->currentIndex() + 1) % C->count());
        }
    }
    for (QSpinBox * C : Dialog->findChildren<QSpinBox *>())
    {
        if (C->isEnabled())
        {
            const int Wanted = C->minimum() + (C->maximum() - C->minimum()) / 3;
            C->setValue(Wanted != C->value() ? Wanted : C->minimum());
        }
    }
    QApplication::processEvents();
}

// Runs inside the dialog's event loop.
static void DriveDialog()
{
    for (QWidget * W : QApplication::topLevelWidgets())
    {
        QDialog * Dialog = qobject_cast<QDialog *>(W);
        if (Dialog == nullptr || !Dialog->isVisible())
        {
            continue;
        }
        QCheckBox * VSync = nullptr;
        for (QCheckBox * C : Dialog->findChildren<QCheckBox *>())
        {
            if (C->text() == g_CheckText)
            {
                VSync = C;
            }
        }
        if (g_Prep)
        {
            for (QCheckBox * C : Dialog->findChildren<QCheckBox *>())
            {
                if (C->text() == "Show texture enhancement options" && !C->isChecked())
                {
                    C->click();
                }
            }
            Dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
            return;
        }
        CHECK(VSync != nullptr);
        if (VSync != nullptr)
        {
            g_Seen = true;
            if (g_Toggle)
            {
                VSync->click();
                g_ExpectChecked = VSync->isChecked();
            }
            else
            {
                CHECK(VSync->isChecked() == g_ExpectChecked);
            }
        }
        if (strcmp(g_CheckText, "Vertical sync") == 0 && getenv("XDG_CONFIG_HOME") != nullptr)
        {
            const QString File = QString(getenv("XDG_CONFIG_HOME")) + "/video-controls-state";
            if (g_Toggle)
            {
                VideoChangeAll(Dialog);
                QFile Out(File);
                if (Out.open(QIODevice::WriteOnly))
                {
                    Out.write(VideoControlState(Dialog).join("\n").toUtf8());
                }
            }
            else
            {
                QFile In(File);
                CHECK(In.open(QIODevice::ReadOnly));
                const QStringList Want = QString::fromUtf8(In.readAll()).split("\n");
                const QStringList Now = VideoControlState(Dialog);
                CHECK(Want.size() == Now.size() && Want.size() > 30);
                for (int i = 0; i < qMin(Want.size(), Now.size()); i++)
                {
                    if (Want[i] != Now[i])
                    {
                        printf("video control: wrote '%s', shows '%s'\n", Want[i].toUtf8().constData(), Now[i].toUtf8().constData());
                        g_Failures++;
                    }
                }
                printf("video controls compared: %d\n", (int)Now.size());
            }
        }
        QDialogButtonBox * Buttons = Dialog->findChild<QDialogButtonBox *>();
        Buttons->button(g_Toggle ? QDialogButtonBox::Ok : QDialogButtonBox::Cancel)->click();
        return;
    }
    QTimer::singleShot(100, DriveDialog);
}

// Input plugin dialog (Vortex64-input/ConfigQt.cpp): "write" scans a key for
// Player 1 "Digital Pad > Up" (F8, sent as a Qt key event like a real key
// press) and moves the range slider, then OK; "read" checks both came back.
static QString g_InputKeyText;
static int g_InputStep = 0;
static QString g_InputExpectText;
static void DriveInputDialog()
{
    QDialog * Dialog = nullptr;
    for (QWidget * W : QApplication::topLevelWidgets())
    {
        QDialog * D = qobject_cast<QDialog *>(W);
        if (D != nullptr && D->isVisible() && D->objectName() == "InputDialog")
        {
            Dialog = D;
        }
    }
    if (Dialog == nullptr)
    {
        QTimer::singleShot(100, DriveInputDialog);
        return;
    }
    g_Seen = true;
    // Player 1's page (findChildren over the whole dialog does not list the tabs in order)
    QStackedWidget * Controls = Dialog->findChildren<QStackedWidget *>().first();
    CHECK(Controls != nullptr && Controls->count() == 4);
    QWidget * Page = Controls != nullptr ? Controls->widget(0) : Dialog;
    QList<QSlider *> Sliders = Page->findChildren<QSlider *>();
    QList<BindingRef> Displays = BindingDisplays(Page);
    QList<QPushButton *> Buttons = Page->findChildren<QPushButton *>();
    CHECK(Sliders.size() == 3 && Displays.size() == 18);
    if (Sliders.size() != 3 || Displays.size() != 18)
    {
        Dialog->reject();
        return;
    }
    QSlider * Range = Sliders.at(0);
    BindingRef DPadUp = Displays.at(0);
    if (!g_Toggle)
    {
        CHECK(Range->value() == 77);
        CHECK(DPadUp->text() == g_InputExpectText);
        Dialog->reject();
        return;
    }
    if (g_InputStep == 0)
    {
        // No ROM is running, so Player 1 starts as "None" with its controls disabled
        QComboBox * Type = Page->findChildren<QComboBox *>().at(0);
        CHECK(Type->count() == 3);
        Type->setCurrentIndex(Type->findData(1));
        // Click the small scan button of the first row (fixed width 20, empty text)
        for (QPushButton * B : Buttons)
        {
            if (B->objectName() == "VxScan" && B->property("row").toInt() == 0 && B->isEnabled())
            {
                B->click();
                break;
            }
        }
        g_InputStep = 1;
        QTimer::singleShot(150, DriveInputDialog);
        return;
    }
    if (g_InputStep == 1)
    {
        CHECK(DPadUp->text() == "...");
        QKeyEvent Press(QEvent::KeyPress, Qt::Key_F8, Qt::NoModifier);
        QApplication::sendEvent(Dialog, &Press);
        g_InputStep = 2;
        QTimer::singleShot(400, DriveInputDialog);
        return;
    }
    // Scan finished: the row shows the key, then set the range and press OK
    g_InputKeyText = DPadUp->text();
    CHECK(!g_InputKeyText.isEmpty() && g_InputKeyText != "..." );
    Range->setValue(77);
    Dialog->findChild<QPushButton *>("VxOk")->click();
}

int main(int argc, char ** argv)
{
    if (argc < 3 || (strcmp(argv[1], "video") != 0 && strcmp(argv[1], "audio") != 0 && strcmp(argv[1], "input") != 0) ||
        (strcmp(argv[2], "write") != 0 && strcmp(argv[2], "read") != 0 && strcmp(argv[2], "prep") != 0))
    {
        printf("usage: %s video|audio|input write|read\n", argv[0]);
        return 2;
    }
    const bool Audio = strcmp(argv[1], "audio") == 0;
    const bool Input = strcmp(argv[1], "input") == 0;
    g_CheckText = Audio ? "Mute" : "Vertical sync";
    const PLUGIN_TYPE Type = Input ? PLUGIN_TYPE_CONTROLLER : (Audio ? PLUGIN_TYPE_AUDIO : PLUGIN_TYPE_VIDEO);
    const std::string StateFile = std::string(getenv("XDG_CONFIG_HOME") ? getenv("XDG_CONFIG_HOME") : "/tmp") + "/plugin-config-state-" + argv[1];
    QApplication App(argc, argv);
    static CLinuxNotification Notify;
    const std::string Base = CPath(CPath::MODULE_DIRECTORY).GetDirectory();
    if (!AppInit(&Notify, Base.c_str(), 1, argv))
    {
        return 2;
    }
    static DummyRenderWindow Window;
    g_Plugins->SetRenderWindows(&Window, &Window);
    g_Plugins->CreatePlugins();
    if (Input)
    {
        CHECK(g_Plugins->Control() != nullptr);
        CHECK(g_Plugins->Control() != nullptr && g_Plugins->Control()->DllConfig != nullptr);
    }
    else if (Audio)
    {
        CHECK(g_Plugins->Audio() != nullptr);
        CHECK(g_Plugins->Audio() != nullptr && g_Plugins->Audio()->DllConfig != nullptr);
    }
    else
    {
        CHECK(g_Plugins->Gfx() != nullptr);
        CHECK(g_Plugins->Gfx() != nullptr && g_Plugins->Gfx()->DllConfig != nullptr);
    }

    // "write": the first run's expected value is the opposite of the default,
    // which the dialog itself reports; "read" recomputes it from the ini.
    g_Toggle = strcmp(argv[2], "write") == 0;
    if (!g_Toggle && strcmp(argv[2], "prep") != 0)
    {
        // The write run stored the value it left in the dialog.
        FILE * F = fopen(StateFile.c_str(), "r");
        int V = -1;
        CHECK(F != nullptr && fscanf(F, "%d", &V) == 1);
        if (F != nullptr)
        {
            fclose(F);
        }
        g_ExpectChecked = V == 1;
        if (Input)
        {
            F = fopen((StateFile + "-text").c_str(), "r");
            char Text[128] = {0};
            CHECK(F != nullptr && fgets(Text, sizeof(Text), F) != nullptr);
            if (F != nullptr)
            {
                fclose(F);
            }
            g_InputExpectText = QString::fromUtf8(Text).trimmed();
        }
    }
    if (strcmp(argv[2], "prep") == 0)
    {
        // video, first run: advanced mode, the debugger and the page of the texture enhancement are switched on
        // (the plugin reads them when it starts, so the next runs show every page of the dialog)
        g_Settings->SaveBool(UserInterface_BasicMode, false);
        g_Settings->SaveBool(Debugger_Enabled, true);
        g_Prep = true;
        QTimer::singleShot(200, DriveDialog);
        g_Plugins->ConfigPlugin(nullptr, Type);
        AppCleanup();
        printf(g_Failures == 0 ? "PASS %s prep\n" : "FAILED %s prep\n", argv[1]);
        return g_Failures == 0 ? 0 : 1;
    }
    QTimer::singleShot(200, Input ? DriveInputDialog : DriveDialog);
    g_Plugins->ConfigPlugin(nullptr, Type);
    CHECK(g_Seen);
    if (g_Toggle)
    {
        FILE * F = fopen(StateFile.c_str(), "w");
        if (F != nullptr)
        {
            fprintf(F, "%d\n", g_ExpectChecked ? 1 : 0);
            fclose(F);
        }
        if (Input)
        {
            F = fopen((StateFile + "-text").c_str(), "w");
            if (F != nullptr)
            {
                fprintf(F, "%s\n", g_InputKeyText.toUtf8().constData());
                fclose(F);
            }
        }
    }

    AppCleanup();
    printf(g_Failures == 0 ? "PASS %s %s\n" : "FAILED %s %s\n", argv[1], argv[2]);
    return g_Failures == 0 ? 0 : 1;
}
