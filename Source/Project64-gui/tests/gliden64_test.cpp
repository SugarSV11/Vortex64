// GLideN64 (Source/GLideN64-linux) as the video plugin of the real main window, with a real ROM.
// Needs an X server (Scripts/linux/run-gui-tests.sh starts Xvfb): the plugin makes its OpenGL
// context on the render widget.
//
//   gliden64-test <rom> [directory for the captures]
//
// The plugin is selected, the ROM runs, the plugin's own CaptureScreen writes the frame as a PNG
// (checked: it has a picture, not a cleared surface), the windowed resolution of the plugin's settings
// (800 x 600, written to its ini before the plugin loads) becomes the size of the render window and
// of the capture, the game is paused, saved and restored, the settings dialog opens and is closed,
// and the emulation ends; then the window is closed with the ROM running.
#include "../MainMenu.h"
#include "../MainWindow.h"
#include "../Settings/SettingsPage.h"

#include <Common/path.h>
#include <Project64-core/AppInit.h>
#include <Project64-core/N64System/N64System.h>
#include <Project64-core/N64System/SystemGlobals.h>
#include <Project64-core/Plugins/Plugin.h>
#include <Project64-core/Settings.h>
#include <Project64/Settings/UISettings.h>

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QMap>
#include <QVariant>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QMessageBox>
#include <QPushButton>
#include <QPixmap>
#include <QScreen>
#include <QSet>
#include <QThread>
#include <QTimer>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <unistd.h>

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

static int g_ConfigDialogs = 0;
static int g_SettingsDialogs = 0;
static int g_Glide64Dialogs = 0;


// GLIDEN64_TEST_CONFIGRT=1: the settings dialog of the plugin is opened twice. The first time a user changes every
// control that is enabled (check boxes inverted, lists on the next entry, numbers on another value) and presses OK;
// the second time every control has to show what was left in the first one (the settings were written and read again)
// 3: Cancel (Close) after changing controls leaves the saved settings alone; Restore Defaults brings the defaults the
// dialog showed the first time; Save keeps them; 4: reopened, the defaults are what is shown
static int g_RoundTrip = 0; // 0: off, 1: change + OK, 2: compare, 3: cancel/restore/save, 4: compare defaults
static QMap<QString, QVariant> g_RoundTripValues;
static QMap<QString, QVariant> g_Defaults, g_Restored;
static int g_RoundTripMismatch = 0;
static int g_ButtonMismatch = 0;

// the settings of the game (the ROM is running and the dialog saves into its own profile) hold the differences of the
// emulation only: upstream keeps the video back end, the OSD font and the hotkeys in the global file
static bool GlobalOnly(QWidget * W)
{
    const QString Name = W->objectName();
    return Name == "threadedVideoCheckBox" || Name == "fontSizeSpinBox";
}

static QString ControlKey(QWidget * W)
{
    QString Key = W->metaObject()->className();
    for (QWidget * P = W; P != nullptr && !P->objectName().isEmpty(); P = nullptr)
    {
        Key += ":" + P->objectName();
    }
    return Key;
}

static void Snapshot(QDialog * Dialog, QMap<QString, QVariant> & Into)
{
    for (QCheckBox * C : Dialog->findChildren<QCheckBox *>())
    {
        if (!C->objectName().isEmpty() && !GlobalOnly(C)) Into[ControlKey(C)] = C->isChecked();
    }
    for (QComboBox * C : Dialog->findChildren<QComboBox *>())
    {
        if (!C->objectName().isEmpty() && C->objectName() != "profilesComboBox") Into[ControlKey(C)] = C->currentText();
    }
    for (QSpinBox * C : Dialog->findChildren<QSpinBox *>())
    {
        if (!C->objectName().isEmpty() && !GlobalOnly(C)) Into[ControlKey(C)] = C->value();
    }
}

// the controls that differ from a snapshot (their keys)
static QStringList Differences(QDialog * Dialog, const QMap<QString, QVariant> & Expected)
{
    QMap<QString, QVariant> Now;
    Snapshot(Dialog, Now);
    QStringList Different;
    for (auto It = Expected.begin(); It != Expected.end(); ++It)
    {
        if (Now.contains(It.key()) && Now[It.key()] != It.value())
        {
            Different << It.key() + ": expected " + It.value().toString() + ", shows " + Now[It.key()].toString();
        }
    }
    return Different;
}

static void RoundTripControls(QDialog * Dialog)
{
    if (g_RoundTrip == 1)
    {
        // several passes: a change enables or disables other controls
        for (int Pass = 0; Pass < 2; Pass++)
        {
            for (QCheckBox * C : Dialog->findChildren<QCheckBox *>())
            {
                if (C->isEnabled() && !C->isHidden() && !C->objectName().isEmpty() && Pass == 0)
                {
                    C->setChecked(!C->isChecked());
                }
            }
            for (QComboBox * C : Dialog->findChildren<QComboBox *>())
            {
                if (C->isEnabled() && !C->objectName().isEmpty() && C->objectName() != "profilesComboBox" && C->count() > 1 && Pass == 0)
                {
                    C->setCurrentIndex((C->currentIndex() + 1) % C->count());
                }
            }
            for (QSpinBox * C : Dialog->findChildren<QSpinBox *>())
            {
                if (C->isEnabled() && !C->objectName().isEmpty() && C->maximum() > C->minimum() && Pass == 0)
                {
                    const int Span = qMin(C->maximum() - C->minimum(), 200);
                    const int Wanted = C->minimum() + Span / 4;
                    C->setValue(Wanted != C->value() ? Wanted : C->minimum() + Span / 2);
                }
            }
            QApplication::processEvents();
        }
        // what is left in the controls is what has to come back (a disabled control keeps its value too)
        for (QCheckBox * C : Dialog->findChildren<QCheckBox *>())
        {
            if (!C->objectName().isEmpty() && !GlobalOnly(C)) g_RoundTripValues[ControlKey(C)] = C->isChecked();
        }
        for (QComboBox * C : Dialog->findChildren<QComboBox *>())
        {
            if (!C->objectName().isEmpty() && C->objectName() != "profilesComboBox") g_RoundTripValues[ControlKey(C)] = C->currentText();
        }
        for (QSpinBox * C : Dialog->findChildren<QSpinBox *>())
        {
            if (!C->objectName().isEmpty() && !GlobalOnly(C)) g_RoundTripValues[ControlKey(C)] = C->value();
        }
        printf("config round trip: %d controls changed\n", (int)g_RoundTripValues.size());
        if (QDialogButtonBox * Box = Dialog->findChild<QDialogButtonBox *>())
        {
            Box->button(QDialogButtonBox::Ok)->click(); // what a user does: the OK button saves the settings
        }
        g_RoundTrip = 2;
    }
    else
    {
        int Compared = 0;
        auto Check = [&](QWidget * W, const QVariant & Now) {
            const QString Key = ControlKey(W);
            if (!g_RoundTripValues.contains(Key)) return;
            Compared++;
            if (g_RoundTripValues[Key] != Now)
            {
                printf("config round trip: %s was %s, shows %s\n", Key.toUtf8().constData(), g_RoundTripValues[Key].toString().toUtf8().constData(), Now.toString().toUtf8().constData());
                g_RoundTripMismatch++;
            }
        };
        for (QCheckBox * C : Dialog->findChildren<QCheckBox *>()) Check(C, C->isChecked());
        for (QComboBox * C : Dialog->findChildren<QComboBox *>()) Check(C, C->currentText());
        for (QSpinBox * C : Dialog->findChildren<QSpinBox *>()) Check(C, C->value());
        printf("config round trip: %d controls compared, %d different\n", Compared, g_RoundTripMismatch);
        // a user changes controls and closes the dialog with Close: nothing is saved
        for (QCheckBox * C : Dialog->findChildren<QCheckBox *>())
        {
            if (C->isEnabled() && !C->isHidden() && !C->objectName().isEmpty())
            {
                C->setChecked(!C->isChecked());
            }
        }
        g_RoundTrip = 3;
        Dialog->reject();
    }
}

static void ButtonControls(QDialog * Dialog)
{
    QDialogButtonBox * Box = Dialog->findChild<QDialogButtonBox *>();
    if (Box == nullptr)
    {
        g_ButtonMismatch++;
        Dialog->reject();
        return;
    }
    if (g_RoundTrip == 3)
    {
        // Close discarded the changes made before it
        QStringList Cancelled = Differences(Dialog, g_RoundTripValues);
        for (const QString & D : Cancelled) printf("buttons: after Close %s\n", D.toUtf8().constData());
        printf("buttons: Close kept the saved settings (%d different)\n", (int)Cancelled.size());
        g_ButtonMismatch += Cancelled.size();
        // Restore Defaults (with its confirmation) shows the defaults of the first opening
        QTimer::singleShot(300, []() {
            for (QWidget * Widget : QApplication::topLevelWidgets())
            {
                if (QMessageBox * Message = qobject_cast<QMessageBox *>(Widget))
                {
                    if (Message->isVisible() && Message->button(QMessageBox::RestoreDefaults) != nullptr)
                    {
                        Message->button(QMessageBox::RestoreDefaults)->click();
                    }
                }
            }
        });
        Box->button(QDialogButtonBox::RestoreDefaults)->click();
        // (upstream's resetToDefaults leaves the overscan margins alone: they keep what was loaded)
        Snapshot(Dialog, g_Restored);
        int Changed = 0;
        for (auto It = g_RoundTripValues.begin(); It != g_RoundTripValues.end(); ++It)
        {
            Changed += g_Restored.contains(It.key()) && g_Restored[It.key()] != It.value();
            if (getenv("GLIDEN64_TEST_VERBOSE") && g_Restored.contains(It.key()) && g_Restored[It.key()] == It.value())
                printf("buttons: unchanged by Restore Defaults: %s = %s (first opening %s)\n", It.key().toUtf8().constData(), It.value().toString().toUtf8().constData(), g_Defaults.value(It.key()).toString().toUtf8().constData());
        }
        QStringList Unlike = Differences(Dialog, g_Defaults);
        printf("buttons: Restore Defaults changed %d of %d controls (%d of them differ from the first opening: the test's own ini)\n", Changed, (int)g_RoundTripValues.size(), (int)Unlike.size());
        if (Changed < 20) g_ButtonMismatch++;
        Box->button(QDialogButtonBox::Save)->click(); // Save: written, the dialog stays
        Dialog->reject();
        g_RoundTrip = 4;
    }
    else
    {
        // (the factor box belongs to a radio button: the dialog keeps its old number until the button is chosen)
        g_Restored.remove("QSpinBox:resolutionFactorSpinBox");
        QStringList Saved = Differences(Dialog, g_Restored);
        for (const QString & D : Saved) printf("buttons: reopened %s\n", D.toUtf8().constData());
        printf("buttons: Save kept the restored defaults (%d different)\n", (int)Saved.size());
        g_ButtonMismatch += Saved.size();
        g_RoundTrip = 5;
        Dialog->reject();
    }
}

// message boxes are accepted like a user pressing OK; the plugin's settings dialog is cancelled
static void DismissDialogs()
{
    for (QWidget * Widget : QApplication::topLevelWidgets())
    {
        if (!Widget->isVisible())
        {
            continue;
        }
        if (QMessageBox * Box = qobject_cast<QMessageBox *>(Widget))
        {
            printf("dialog: %s\n", Box->text().left(60).toUtf8().constData());
            Box->accept();
        }
        else if (QDialog * Dialog = qobject_cast<QDialog *>(Widget))
        {
            if (strcmp(Dialog->metaObject()->className(), "ConfigDialog") == 0)
            {
                g_ConfigDialogs++;
                if (g_RoundTrip == 1 || g_RoundTrip == 2)
                {
                    if (g_RoundTrip == 1)
                    {
                        Snapshot(Dialog, g_Defaults); // (a profile of the game does not exist yet)
                    }
                    RoundTripControls(Dialog);
                }
                else if (g_RoundTrip == 3 || g_RoundTrip == 4)
                {
                    ButtonControls(Dialog);
                }
                else
                {
                    Dialog->reject();
                }
            }
            else if (Dialog->windowTitle().endsWith("Glide64 settings"))
            {
                // the Glide64 sheet opened while the game runs: a user changes the frame buffer option and presses OK
                g_Glide64Dialogs++;
                for (QCheckBox * Check : Dialog->findChildren<QCheckBox *>())
                {
                    if (Check->text() == "Anisotropic filtering" || Check->text() == "Fog")
                    {
                        Check->click();
                    }
                }
                if (QDialogButtonBox * Box = Dialog->findChild<QDialogButtonBox *>())
                {
                    Box->button(QDialogButtonBox::Ok)->click();
                }
            }
            else if (strcmp(Dialog->metaObject()->className(), "SettingsDialog") == 0)
            {
                // the settings of the application: a user changes one and presses OK
                g_SettingsDialogs++;
                for (QCheckBox * Check : Dialog->findChildren<QCheckBox *>())
                {
                    if (Check->text() == QtGS(OPTION_AUTO_FULLSCREEN))
                    {
                        Check->click();
                        break;
                    }
                }
                // the OK button applies the pages and closes the dialog
                for (QPushButton * Button : Dialog->findChildren<QPushButton *>())
                {
                    if (Button->objectName() == "VxOk" || Button->text() == QtGS(CHEAT_OK))
                    {
                        Button->click();
                        break;
                    }
                }
            }
        }
    }
}

static bool WaitFor(const std::function<bool()> & Condition, int TimeoutMs)
{
    QElapsedTimer Timer;
    Timer.start();
    while (Timer.elapsed() < TimeoutMs)
    {
        QApplication::processEvents(QEventLoop::AllEvents, 20);
        DismissDialogs();
        if (Condition())
        {
            return true;
        }
        QThread::msleep(10);
    }
    return Condition();
}

static void Pump(int Ms)
{
    WaitFor([]() { return false; }, Ms);
}

// Full screen on or off through the menu. The window manager of the test (Xvfb has none of its own) answers late now and then: a request
// that was not answered is made once more before it counts as a failure.
static bool SetFullScreen(MainWindow & Window, QtMainMenu * Menu, bool On)
{
    if (Window.isFullScreen() == On)
    {
        return true;
    }
    Menu->ProcessMessage(ID_OPTIONS_FULLSCREEN);
    if (WaitFor([&]() { return Window.isFullScreen() == On; }, 5000))
    {
        return true;
    }
    printf("fullscreen: no answer to the request, asking again\n");
    if (Window.isFullScreen() != On)
    {
        Menu->ProcessMessage(ID_OPTIONS_FULLSCREEN);
    }
    return WaitFor([&]() { return Window.isFullScreen() == On; }, 8000);
}

static bool Running()
{
    return g_Settings->LoadBool(GameRunning_CPU_Running);
}

static bool Paused()
{
    return g_Settings->LoadBool(GameRunning_CPU_Paused);
}

// Asks the plugin for a capture of the next frame and returns the PNG it writes
static QImage Capture(const QString & Dir, const char * Name)
{
    QDir(Dir).removeRecursively();
    QDir().mkpath(Dir);
    // (the directory with its slash at the end, as the application gives it)
    g_Plugins->Gfx()->CaptureScreen((Dir + "/").toLocal8Bit().constData());
    QStringList Files;
    WaitFor(
        [&]() {
            Files = QDir(Dir).entryList({"*.png"});
            return !Files.isEmpty();
        },
        10000);
    if (Files.isEmpty())
    {
        return QImage();
    }
    // the plugin may still be writing the file: wait until its size stays
    qint64 Size = -1;
    WaitFor(
        [&]() {
            const qint64 Now = QFileInfo(Dir + "/" + Files.first()).size();
            const bool Same = Now > 0 && Now == Size;
            Size = Now;
            return Same;
        },
        5000);
    QImage Image;
    WaitFor(
        [&]() {
            Image = QImage(Dir + "/" + Files.first());
            return !Image.isNull();
        },
        5000);
    printf("%s: %s %dx%d\n", Name, Files.first().toUtf8().constData(), Image.width(), Image.height());
    return Image;
}

static int Colors(const QImage & Image)
{
    QSet<QRgb> Set;
    for (int y = 0; y < Image.height(); y += 3)
    {
        for (int x = 0; x < Image.width(); x += 3)
        {
            Set.insert(Image.pixel(x, y));
        }
    }
    return Set.size();
}


// Where the picture sits in a capture of the screen: the box of what is not black
static QRect ContentBox(const QImage & Image)
{
    int Left = Image.width(), Top = Image.height(), Right = -1, Bottom = -1;
    for (int y = 0; y < Image.height(); y += 2)
    {
        for (int x = 0; x < Image.width(); x += 2)
        {
            const QRgb P = Image.pixel(x, y);
            if (qRed(P) + qGreen(P) + qBlue(P) > 24)
            {
                Left = qMin(Left, x);
                Right = qMax(Right, x);
                Top = qMin(Top, y);
                Bottom = qMax(Bottom, y);
            }
        }
    }
    return Right < 0 ? QRect() : QRect(Left, Top, Right - Left + 1, Bottom - Top + 1);
}

static void FullscreenCycles(MainWindow & Window, QtMainMenu * Menu, const QString & Dir, int Cycles, bool HasCapture)
{
    const QSize WindowedSize = Window.VideoWidget()->size();
    for (int Cycle = 0; Cycle < Cycles * 2; Cycle++)
    {
        const bool Full = Cycle % 2 == 1;
        if (Full)
        {
            CHECK(SetFullScreen(Window, Menu, true));
        }
        else if (Cycle > 0)
        {
            CHECK(SetFullScreen(Window, Menu, false));
        }
        Pump(2500);
        QScreen * Screen = Window.screen();
        const QSize Video = Window.VideoWidget()->size();
        const qreal Ratio = Window.VideoWidget()->devicePixelRatioF();
        const QImage Shot = HasCapture ? Capture(Dir + "/fs" + QString::number(Cycle), Full ? "fullscreen capture" : "windowed capture") : QImage();
        const QImage Desktop = Screen->grabWindow(0).toImage();
        const QRect Box = ContentBox(Desktop);
        printf("fullscreen cycle %d (%s): screen %dx%d dpr %.2f, window %dx%d, render widget %dx%d (device %dx%d), plugin picture %dx%d, content on screen %dx%d at %d,%d\n",
               Cycle, Full ? "full" : "windowed", Screen->size().width(), Screen->size().height(), Screen->devicePixelRatio(),
               Window.width(), Window.height(), Video.width(), Video.height(), qRound(Video.width() * Ratio), qRound(Video.height() * Ratio),
               Shot.width(), Shot.height(), Box.width(), Box.height(), Box.x(), Box.y());
        Desktop.save(Dir + "/desktop" + QString::number(Cycle) + ".png");
        // the game is shown whole inside its bars (4:3 with the margins of the game itself: not cut by a screen taller or wider
        // than the game, as a clip against the size of the game area instead of the screen did on a vertical monitor)
        CHECK(!Full || Box.isEmpty() || Box.height() * 100 >= Box.width() * 3 / 4 * 85);
        CHECK(Running());
        CHECK(!HasCapture || !Shot.isNull());
        if (!Full)
        {
            // back in a window: the size the plugin asked for
            CHECK(!Window.isFullScreen());
            CHECK(Window.VideoWidget()->size() == WindowedSize);
        }
        if (Full)
        {
            // the picture of the plugin is the render widget at its device size, and the render widget the screen
            CHECK(Window.VideoWidget()->size() == Screen->size());
            CHECK(Window.isFullScreen());
            // (the picture of GLideN64 is the whole widget; Glide64 gives the part of the game: its aspect ratio, in the widget)
            const QSize Widget(qRound(Video.width() * Ratio), qRound(Video.height() * Ratio));
            CHECK(!HasCapture || Shot.size() == Widget || (Shot.width() <= Widget.width() && Shot.height() <= Widget.height() && qAbs(Shot.width() * 3 - Shot.height() * 4) < 16 && (Shot.width() == Widget.width() || Shot.height() == Widget.height())));
        }
    }
}

// GLIDEN64_TEST_SESSION=<n>: n times what a user does in a session
static void Session(MainWindow & Window, QtMainMenu * Menu, const QString & Rom, const QString & Dir, int Times, bool IsGliden)
{
    // GLIDEN64_TEST_SKIP=pause,fullscreen,settings,plugin,states,resets: steps left out (to find which one matters)
    const QString Skip = qEnvironmentVariable("GLIDEN64_TEST_SKIP");
    auto Do = [&](const char * Step) { return !Skip.contains(Step); };
    for (int Round = 0; Round < Times; Round++)
    {
        printf("session %d: start\n", Round + 1);
        Window.OpenImage(Rom);
        CHECK(WaitFor(Running, 30000));
        Pump(2000);
        CHECK(Running());
        // pause, resume
        if (Do("pause"))
        {
            Menu->ProcessMessage(ID_SYSTEM_PAUSE);
            CHECK(WaitFor(Paused, 5000));
            Pump(500);
            Menu->ProcessMessage(ID_SYSTEM_PAUSE);
            CHECK(WaitFor([]() { return !Paused(); }, 5000));
        }
        // full screen and back
        if (Do("fullscreen"))
        {
            CHECK(SetFullScreen(Window, Menu, true));
            Pump(1500);
            CHECK(Running());
            CHECK(SetFullScreen(Window, Menu, false));
            Pump(1500);
        }
        // the settings of the application: open, change one, OK
        const bool Before = g_Settings->LoadBool((SettingID)Setting_AutoFullscreen);
        const int Opened = g_SettingsDialogs;
        if (Do("settings"))
        {
            Menu->ProcessMessage(ID_OPTIONS_SETTINGS);
            CHECK(g_SettingsDialogs == Opened + 1);
            CHECK(g_Settings->LoadBool((SettingID)Setting_AutoFullscreen) != Before);
            CHECK(Running());
        }
        // (the next round must not start in full screen)
        g_Settings->SaveBool((SettingID)Setting_AutoFullscreen, Before);
        // change the video plugin and back while the game runs
        if (IsGliden && Do("plugin"))
        {
            g_Settings->SaveString(Plugin_GFX_Current, "libProject64-video.so");
            Pump(3000);
            CHECK(Running());
            g_Settings->SaveString(Plugin_GFX_Current, "libGLideN64.so");
            Pump(3000);
            CHECK(Running());
            QImage Shot = Capture(Dir + "/session" + QString::number(Round), "session capture");
            if (Shot.isNull())
            {
                // late or never?
                printf("session %d: no capture after the plugin changes; plugin now: %s, setting: %s; waiting\n", Round + 1, g_Plugins->Gfx() != nullptr ? g_Plugins->Gfx()->PluginName() : "none", g_Settings->LoadStringVal(Plugin_GFX_Current).c_str());
                Pump(8000);
                Shot = Capture(Dir + "/session" + QString::number(Round) + "b", "session capture, second try");
                printf("session %d: second try %s (running %d)\n", Round + 1, Shot.isNull() ? "failed too" : "worked", (int)Running());
            }
            CHECK(!Shot.isNull());
        }
        // states: a slot each round, save, run on, load
        static const int Slots[] = {ID_CURRENT_SAVE_1, ID_CURRENT_SAVE_2, ID_CURRENT_SAVE_3};
        Menu->ProcessMessage(Slots[Round % 3]);
        Menu->ProcessMessage(ID_SYSTEM_SAVE);
        Pump(1500);
        Pump(1000);
        Menu->ProcessMessage(ID_SYSTEM_RESTORE);
        Pump(1500);
        CHECK(Running());
        // resets
        Menu->ProcessMessage(ID_SYSTEM_RESET_SOFT);
        Pump(1500);
        CHECK(Running());
        Menu->ProcessMessage(ID_SYSTEM_RESET_HARD);
        Pump(2000);
        CHECK(Running());
        // close the ROM
        Menu->ProcessMessage(ID_FILE_ENDEMULATION);
        CHECK(WaitFor([]() { return !Running(); }, 15000));
        Pump(500);
        printf("session %d: done\n", Round + 1);
    }
}

// GLIDEN64_TEST_CHAOS=<n>: n operations of a user who does not wait, in an order that a seed gives
static void Chaos(MainWindow & Window, QtMainMenu * Menu, const QString & Rom, int Steps, bool IsGliden)
{
    srand(getenv("GLIDEN64_TEST_SEED") != nullptr ? (unsigned)atoi(getenv("GLIDEN64_TEST_SEED")) : 12345u);
    Window.OpenImage(Rom);
    CHECK(WaitFor(Running, 30000));
    static const int Slots[] = {ID_CURRENT_SAVE_1, ID_CURRENT_SAVE_2, ID_CURRENT_SAVE_3};
    for (int i = 0; i < Steps; i++)
    {
        const int Op = rand() % 11;
        switch (Op)
        {
        case 0: Menu->ProcessMessage(ID_SYSTEM_PAUSE); break;
        case 1: Menu->ProcessMessage(ID_OPTIONS_FULLSCREEN); break;
        case 2: Menu->ProcessMessage(Slots[rand() % 3]); Menu->ProcessMessage(ID_SYSTEM_SAVE); break;
        case 3: Menu->ProcessMessage(ID_SYSTEM_RESTORE); break;
        case 4: Menu->ProcessMessage(ID_SYSTEM_RESET_SOFT); break;
        case 5: Menu->ProcessMessage(ID_SYSTEM_RESET_HARD); break;
        case 6:
            if (IsGliden)
            {
                g_Settings->SaveString(Plugin_GFX_Current, rand() % 2 ? "libProject64-video.so" : "libGLideN64.so");
            }
            break;
        case 7: Menu->ProcessMessage(ID_OPTIONS_SETTINGS); break;
        case 8: Menu->ProcessMessage(ID_SYSTEM_BITMAP); break;
        case 9: Menu->ProcessMessage(ID_OPTIONS_INCREASE_SPEED); break;
        default: Menu->ProcessMessage(ID_OPTIONS_DECREASE_SPEED); break;
        }
        Pump(rand() % 250);
        if (!Running())
        {
            // an operation ended the game (a plugin that could not start...): it is started again
            printf("chaos %d: the game is not running after op %d\n", i, Op);
            Window.OpenImage(Rom);
            CHECK(WaitFor(Running, 30000));
        }
    }
    // what is left: the game can be seen to run, and it ends. The window manager answers the full screen requests of a user who does
    // not wait at its own pace: the last one can still be on its way, and the state is read only when none is
    Pump(2500);
    if (Paused())
    {
        Menu->ProcessMessage(ID_SYSTEM_PAUSE);
        WaitFor([]() { return !Paused(); }, 5000);
    }
    if (Window.isFullScreen())
    {
        Menu->ProcessMessage(ID_OPTIONS_FULLSCREEN);
        WaitFor([&]() { return !Window.isFullScreen(); }, 5000);
    }
    Pump(3000);
    CHECK(Running());
    Menu->ProcessMessage(ID_FILE_ENDEMULATION);
    CHECK(WaitFor([]() { return !Running(); }, 30000));
    printf("chaos: %d operations, settings dialogs %d\n", Steps, g_SettingsDialogs);
}

int main(int argc, char ** argv)
{
    if (argc < 2)
    {
        printf("usage: %s <rom> [capture dir]\n", argv[0]);
        return 2;
    }
    const QString Rom = QString::fromLocal8Bit(argv[1]);
    const QString Dir = argc > 2 ? QString::fromLocal8Bit(argv[2]) : QDir::tempPath() + "/gliden64-test";
    setenv("SDL_VIDEODRIVER", "x11", 0);
    QApplication App(argc, argv);
    MainWindow Window;
    QTimer DismissTimer;
    QObject::connect(&DismissTimer, &QTimer::timeout, &DismissDialogs);
    DismissTimer.start(50);
    // the windowed resolution of the plugin: the ini of the user starts from the default one
    {
        const QString Config = qEnvironmentVariable("XDG_CONFIG_HOME", QDir::homePath() + "/.config") + "/vortex64/GLideN64";
        QDir().mkpath(Config);
        QFile Default(QCoreApplication::applicationDirPath() + "/GLideN64.ini");
        CHECK(Default.open(QIODevice::ReadOnly));
        QByteArray Ini = Default.readAll();
        Ini.replace("windowedWidth=640", "windowedWidth=800");
        Ini.replace("windowedHeight=480", "windowedHeight=600");
        // GLIDEN64_TEST_THREADED=1: the OpenGL commands run in the plugin's own thread
        if (qEnvironmentVariableIntValue("GLIDEN64_TEST_THREADED") != 0)
        {
            Ini.replace("[video]", "[video]\nthreadedVideo=1");
        }
        // GLIDEN64_TEST_ENHANCEMENT=<n>: the texture enhancement of the plugin (txEnhancementMode), for the matrix of the enhancements
        if (qEnvironmentVariableIsSet("GLIDEN64_TEST_ENHANCEMENT"))
        {
            Ini.replace("textureFilter\\txEnhancementMode=0", "textureFilter\\txEnhancementMode=" + qgetenv("GLIDEN64_TEST_ENHANCEMENT"));
        }
        QFile Out(Config + "/GLideN64.ini");
        CHECK(Out.open(QIODevice::WriteOnly | QIODevice::Truncate));
        Out.write(Ini);
    }
    const std::string Base = CPath(CPath::MODULE_DIRECTORY).GetDirectory();
    char * Args[] = {argv[0], nullptr};
    if (!AppInit(Window.GetNotify(), Base.c_str(), 1, Args))
    {
        return 2;
    }
    // GLIDEN64_TEST_PLUGIN=<library>: another video plugin (the checks of the picture and of the
    // full screen are the same; those of GLideN64 itself are not made)
    const std::string VideoPlugin = getenv("GLIDEN64_TEST_PLUGIN") != nullptr ? getenv("GLIDEN64_TEST_PLUGIN") : "libGLideN64.so";
    const bool IsGliden = VideoPlugin == "libGLideN64.so";
    // GLIDEN64_TEST_CAPTURE=1: the checks of the captures also for the other plugin (CaptureScreen of the plugin)
    const bool WithCapture = IsGliden || getenv("GLIDEN64_TEST_CAPTURE") != nullptr;
    // (the chaos opens the settings and changes this one at random: every run starts from the same state)
    g_Settings->SaveBool((SettingID)Setting_AutoFullscreen, false);
    g_Settings->SaveString(Plugin_GFX_Current, VideoPlugin);
    UISettingsSaveBool(Setting_AutoSleep, false);
    // GLIDEN64_TEST_JIT=1: the game runs on the recompiler
    const bool Jit = getenv("GLIDEN64_TEST_JIT") != nullptr;
    g_Settings->SaveBool(Setting_ForceInterpreterCPU, !Jit);
    if (Jit)
    {
        g_Settings->SaveDword(Game_CpuType, CPU_Recompiler);
    }
    Window.CreateLanguageMenus();
    Window.InitPlugins();
    Window.show();
    Pump(500);
    CHECK(g_Plugins->Gfx() != nullptr);
    if (g_Plugins->Gfx() == nullptr)
    {
        printf("FAILED\n");
        return 1;
    }
    printf("plugin: %s\n", g_Plugins->Gfx()->PluginName());
    CHECK(!IsGliden || strstr(g_Plugins->Gfx()->PluginName(), "GLideN64") != nullptr);
    QtMainMenu * Menu = Window.Menu();

    Window.OpenImage(Rom);
    CHECK(WaitFor(Running, 30000));
    // GLIDEN64_TEST_SHOTS=<seconds,seconds,...>: the pictures at those moments of the game (the intro and the demos of the ROM), nothing else
    if (qEnvironmentVariableIsSet("GLIDEN64_TEST_SHOTS"))
    {
        QElapsedTimer Clock;
        Clock.start();
        for (const QString & At : qEnvironmentVariable("GLIDEN64_TEST_SHOTS").split(','))
        {
            const qint64 Until = At.toInt() * 1000;
            while (Clock.elapsed() < Until)
            {
                Pump(100);
            }
            const QImage Shot = Capture(Dir + "/t" + At, "shot");
            CHECK(!Shot.isNull());
            if (!Shot.isNull())
            {
                Shot.save(Dir + "/shot-" + At + ".png");
            }
        }
        Menu->ProcessMessage(ID_FILE_ENDEMULATION);
        WaitFor([]() { return !Running(); }, 30000);
        Window.close();
        AppCleanup();
        printf("%s shots\n", g_Failures == 0 ? "PASS" : "FAILED");
        return g_Failures == 0 ? 0 : 1;
    }
    // Super Mario 64 shows its logo some seconds after the boot
    Pump(Jit ? 25000 : 12000);
    CHECK(g_BaseSystem != nullptr);
    CHECK(Running());

    const QImage First = WithCapture ? Capture(Dir + "/1", "capture") : QImage();
    CHECK(!WithCapture || !First.isNull());
    CHECK(!WithCapture || (First.width() >= 100 && First.height() >= 100));
    const int FirstColors = Colors(First);
    printf("colors: %d\n", FirstColors);
    CHECK(!IsGliden || FirstColors > 8);

    // the plugin asked for its windowed resolution: the render widget has it, the capture too
    printf("video widget: %dx%d\n", Window.VideoWidget()->width(), Window.VideoWidget()->height());
    CHECK(!IsGliden || WaitFor([&]() { return Window.VideoWidget()->size() == QSize(800, 600); }, 5000));
    Pump(1000);
    const QImage Second = IsGliden ? Capture(Dir + "/2", "capture at 800x600") : QImage();
    CHECK(!IsGliden || !Second.isNull());
    CHECK(!IsGliden || Second.size() == QSize(qRound(800 * Window.VideoWidget()->devicePixelRatioF()), qRound(600 * Window.VideoWidget()->devicePixelRatioF())));
    CHECK(!IsGliden || Colors(Second) > 8);

    Menu->ProcessMessage(ID_SYSTEM_PAUSE);
    CHECK(WaitFor(Paused, 5000));
    Menu->ProcessMessage(ID_SYSTEM_PAUSE);
    CHECK(WaitFor([]() { return !Paused(); }, 5000));

    Menu->ProcessMessage(ID_CURRENT_SAVE_3);
    Menu->ProcessMessage(ID_SYSTEM_SAVE);
    Pump(1500);
    Menu->ProcessMessage(ID_SYSTEM_RESET_SOFT);
    Pump(2500);
    CHECK(Running());
    Menu->ProcessMessage(ID_SYSTEM_RESTORE);
    Pump(2500);
    CHECK(Running());
    CHECK(!IsGliden || !Capture(Dir + "/3", "capture after restore").isNull());

    // GLIDEN64_TEST_FULLSCREEN=<n>: n times windowed -> full screen -> windowed, with what each state
    // looks like (the screen, the window, the render widget, the picture of the plugin)
    if (qEnvironmentVariableIntValue("GLIDEN64_TEST_FULLSCREEN") > 0)
    {
        FullscreenCycles(Window, Menu, Dir, qEnvironmentVariableIntValue("GLIDEN64_TEST_FULLSCREEN"), WithCapture);
    }

    // the settings dialog of the plugin (GLideNUI) opens and is cancelled
    if (IsGliden)
    {
        QTimer::singleShot(0, [&]() { g_Plugins->Gfx()->DllConfig(nullptr); });
        CHECK(WaitFor([]() { return g_ConfigDialogs > 0; }, 15000));
        Pump(1000);
        if (qEnvironmentVariableIntValue("GLIDEN64_TEST_CONFIGRT") > 0)
        {
            g_RoundTrip = 1;
            const int Before = g_ConfigDialogs;
            QTimer::singleShot(0, [&]() { g_Plugins->Gfx()->DllConfig(nullptr); });
            CHECK(WaitFor([&]() { return g_RoundTrip == 2; }, 15000));
            Pump(1500);
            QTimer::singleShot(0, [&]() { g_Plugins->Gfx()->DllConfig(nullptr); });
            CHECK(WaitFor([&]() { return g_RoundTrip == 3; }, 15000));
            Pump(1000);
            // Close, Restore Defaults, Save
            QTimer::singleShot(0, [&]() { g_Plugins->Gfx()->DllConfig(nullptr); });
            CHECK(WaitFor([&]() { return g_RoundTrip == 4; }, 20000));
            Pump(1500);
            QTimer::singleShot(0, [&]() { g_Plugins->Gfx()->DllConfig(nullptr); });
            CHECK(WaitFor([&]() { return g_RoundTrip == 5; }, 15000));
            Pump(1000);
            CHECK(g_ConfigDialogs == Before + 4);
            CHECK(g_RoundTripMismatch == 0);
            CHECK(g_ButtonMismatch == 0);
            CHECK(g_RoundTripValues.size() > 20);
            g_RoundTrip = 0;
        }
    }
    // Glide64's own sheet while the game runs: the renderer is made again after it (as upstream does around the dialog)
    // and the game goes on drawing
    if (!IsGliden && getenv("GLIDEN64_TEST_GLIDE64CFG") != nullptr)
    {
        for (int Round = 0; Round < 3; Round++)
        {
            QTimer::singleShot(0, [&]() { g_Plugins->Gfx()->DllConfig(nullptr); });
            CHECK(WaitFor([&]() { return g_Glide64Dialogs > Round; }, 15000));
            Pump(3000);
            CHECK(Running());
            const QImage After = Capture(Dir + "/glide64cfg" + QString::number(Round), "capture after the Glide64 sheet");
            printf("dialog: Glide64 sheet round %d, colors after: %d\n", Round, Colors(After));
            CHECK(Colors(After) > 8);
        }
    }
    CHECK(Running());

    // changing the video plugin while the game runs (upstream resets the plugins through
    // SysEvent_ChangePlugins): GLideN64 -> the other video plugin -> GLideN64
    if (getenv("GLIDEN64_TEST_NO_SWITCH") == nullptr && IsGliden)
    {
        g_Settings->SaveString(Plugin_GFX_Current, "libProject64-video.so");
        Pump(5000);
        CHECK(Running());
        CHECK(g_Plugins->Gfx() != nullptr && strstr(g_Plugins->Gfx()->PluginName(), "GLideN64") == nullptr);
        printf("plugin: %s\n", g_Plugins->Gfx() != nullptr ? g_Plugins->Gfx()->PluginName() : "none");
        g_Settings->SaveString(Plugin_GFX_Current, "libGLideN64.so");
        Pump(5000);
        CHECK(Running());
        CHECK(g_Plugins->Gfx() != nullptr && strstr(g_Plugins->Gfx()->PluginName(), "GLideN64") != nullptr);
        printf("plugin: %s\n", g_Plugins->Gfx() != nullptr ? g_Plugins->Gfx()->PluginName() : "none");
        CHECK(!Capture(Dir + "/4", "capture after the plugin changes").isNull());
    }

    Menu->ProcessMessage(ID_FILE_ENDEMULATION);
    CHECK(WaitFor([]() { return !Running(); }, 15000));
    Pump(500);

    if (qEnvironmentVariableIntValue("GLIDEN64_TEST_CHAOS") > 0)
    {
        Chaos(Window, Menu, Rom, qEnvironmentVariableIntValue("GLIDEN64_TEST_CHAOS"), IsGliden);
    }

    if (qEnvironmentVariableIntValue("GLIDEN64_TEST_SESSION") > 0)
    {
        Session(Window, Menu, Rom, Dir, qEnvironmentVariableIntValue("GLIDEN64_TEST_SESSION"), IsGliden);
    }

    // again, then close the window with the ROM running
    Window.OpenImage(Rom);
    CHECK(WaitFor(Running, 30000));
    Pump(3000);
    printf("closing window\n");
    Window.close();
    Pump(500);
    CHECK(!Running());
    printf("closed\n");
    AppCleanup();
    printf(g_Failures == 0 ? "PASS\n" : "FAILED\n");
    return g_Failures == 0 ? 0 : 1;
}
