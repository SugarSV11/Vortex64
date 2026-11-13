// A gamepad that appears while the plugin runs (hot plug), is assigned in the Qt input dialog by pressing its buttons and
// moving its stick, drives the N64 pad (GetKeys), is unplugged (the pad is idle, no crash) and plugged again (the
// assignment still works). The gamepad is a virtual device of the kernel (uinput), so no real hardware is needed.
//
//   input-gamepad-test            (needs write access to /dev/uinput; exit 77 when it is not there: skipped)
#include "isolated_x_guard.h"
#include <Common/path.h>
#include <Vortex64-cli/LinuxNotification.h>
#include <Vortex64-core/AppInit.h>
#include <Vortex64-core/Notification.h>
#include <Vortex64-core/N64System/SystemGlobals.h>
#include <Vortex64-core/Plugins/ControllerPlugin.h>
#include <Vortex64-core/Plugins/Plugin.h>
#include <Vortex64-core/Settings.h>

#include <algorithm>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLineEdit>
#include "input_ui_access.h"
#include <QPushButton>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTimer>
#include <fcntl.h>
#include <linux/uinput.h>
#include <sys/ioctl.h>
#include <unistd.h>
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

static int g_Pad = -1;

static void Emit(int Type, int Code, int Value)
{
    input_event E = {};
    E.type = (unsigned short)Type;
    E.code = (unsigned short)Code;
    E.value = Value;
    if (write(g_Pad, &E, sizeof(E)) < 0) {}
}
static void Sync() { Emit(EV_SYN, SYN_REPORT, 0); }

static bool CreatePad()
{
    g_Pad = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (g_Pad < 0)
    {
        return false;
    }
    ioctl(g_Pad, UI_SET_EVBIT, EV_KEY);
    ioctl(g_Pad, UI_SET_EVBIT, EV_ABS);
    for (int Code : {BTN_SOUTH, BTN_EAST, BTN_NORTH, BTN_WEST, BTN_TL, BTN_TR, BTN_SELECT, BTN_START})
    {
        ioctl(g_Pad, UI_SET_KEYBIT, Code);
    }
    ioctl(g_Pad, UI_SET_ABSBIT, ABS_X);
    ioctl(g_Pad, UI_SET_ABSBIT, ABS_Y);
    uinput_user_dev Dev = {};
    snprintf(Dev.name, UINPUT_MAX_NAME_SIZE, "Vortex64 virtual gamepad");
    Dev.id.bustype = BUS_USB;
    Dev.id.vendor = 0x1209;
    Dev.id.product = 0x6464;
    Dev.id.version = 1;
    for (int Axis : {ABS_X, ABS_Y})
    {
        Dev.absmin[Axis] = -32768;
        Dev.absmax[Axis] = 32767;
    }
    if (write(g_Pad, &Dev, sizeof(Dev)) < 0 || ioctl(g_Pad, UI_DEV_CREATE) < 0)
    {
        close(g_Pad);
        g_Pad = -1;
        return false;
    }
    return true;
}

static void DestroyPad()
{
    if (g_Pad >= 0)
    {
        ioctl(g_Pad, UI_DEV_DESTROY);
        close(g_Pad);
        g_Pad = -1;
    }
}

static void Wait(int Ms)
{
    for (int i = 0; i < Ms / 10; i++)
    {
        QApplication::processEvents();
        usleep(10000);
    }
}

static uint32_t Read()
{
    BUTTONS Keys = {};
    g_Plugins->Control()->GetKeys(0, &Keys);
    return Keys.Value;
}

// waits until the pad is seen (hot plug) or the time is up
static bool WaitFor(bool (*Cond)(), int Ms)
{
    for (int i = 0; i < Ms / 20; i++)
    {
        Wait(20);
        if (Cond())
        {
            return true;
        }
    }
    return false;
}
static uint32_t g_Mask;
static bool Pressed() { return (Read() & 0xFFFF) == g_Mask; }
static bool AxisLeft() { const BUTTONS B = {Read()}; return B.X_AXIS != 0 && (Read() & 0xFFFF) == 0; }
static bool Idle() { return Read() == 0; }

static int g_Step = 0;
static QString g_Shown[3];

static void Drive()
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
        QTimer::singleShot(100, Drive);
        return;
    }
    QWidget * Page = Dialog->findChildren<QStackedWidget *>().first()->widget(0);
    QList<BindingRef> Displays = BindingDisplays(Page);
    QList<QPushButton *> Scans;
    for (QPushButton * B : Page->findChildren<QPushButton *>())
    {
        if (B->objectName() == "VxScan")
        {
            Scans.push_back(B);
        }
    }
    std::sort(Scans.begin(), Scans.end(), [](QPushButton * A, QPushButton * B) { return A->property("row").toInt() < B->property("row").toInt(); });
    if (Displays.size() != 18 || Scans.size() != 18)
    {
        CHECK(false);
        Dialog->reject();
        return;
    }
    if (g_Step == 0)
    {
        QComboBox * Type = Page->findChildren<QComboBox *>().at(0);
        Type->setCurrentIndex(Type->findData(1));
    }
    // rows: 6 = Analog Left, 12 = B, 14 = Start
    const int Rows[3] = {12, 14, 6};
    const int Index = g_Step / 3, Phase = g_Step % 3;
    if (Index < 3)
    {
        const int Row = Rows[Index];
        if (Phase == 0)
        {
            Scans.at(Row)->click();
            Wait(300); // the scan takes the state of the pad as the base
        }
        else if (Phase == 1)
        {
            if (Index == 0) { Emit(EV_KEY, BTN_SOUTH, 1); }
            if (Index == 1) { Emit(EV_KEY, BTN_EAST, 1); }
            if (Index == 2) { Emit(EV_ABS, ABS_X, -32768); }
            Sync();
            Wait(400);
        }
        else
        {
            g_Shown[Index] = Displays.at(Row)->text();
            if (Index == 0) { Emit(EV_KEY, BTN_SOUTH, 0); }
            if (Index == 1) { Emit(EV_KEY, BTN_EAST, 0); }
            if (Index == 2) { Emit(EV_ABS, ABS_X, 0); }
            Sync();
            Wait(300);
        }
        g_Step++;
        QTimer::singleShot(50, Drive);
        return;
    }
    Dialog->findChild<QPushButton *>("VxOk")->click();
}

int main(int argc, char ** argv)
{
    RequireIsolatedX();
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
    if (g_Plugins->Control() == nullptr || g_Plugins->Control()->GetKeys == nullptr)
    {
        printf("FAIL no controller plugin\n");
        return 2;
    }
    CHECK(g_Plugins->Control()->Initiate(nullptr, &Window));
    Wait(300);

    // the pad is plugged after the plugin runs: hot plug
    if (!CreatePad())
    {
        printf("SKIP no write access to /dev/uinput\n");
        return 77;
    }
    Wait(1500);

    QTimer::singleShot(200, Drive);
    g_Plugins->ConfigPlugin(nullptr, PLUGIN_TYPE_CONTROLLER);
    printf("assigned: '%s' '%s' '%s'\n", g_Shown[0].toUtf8().constData(), g_Shown[1].toUtf8().constData(), g_Shown[2].toUtf8().constData());
    CHECK(g_Shown[0].startsWith("Button"));
    CHECK(g_Shown[1].startsWith("Button"));
    CHECK(g_Shown[1] != g_Shown[0]);
    CHECK(g_Shown[2].contains("X-axis"));

    // the assignments drive the N64 pad
    for (int Round = 0; Round < 2; Round++)
    {
        CHECK(Idle() || WaitFor(Idle, 1000));
        Emit(EV_KEY, BTN_SOUTH, 1); Sync();
        g_Mask = 1u << 6;
        printf("%s round %d: gamepad button -> B\n", WaitFor(Pressed, 2000) ? "ok" : "BAD", Round);
        CHECK((Read() & 0xFFFF) == g_Mask);
        Emit(EV_KEY, BTN_SOUTH, 0); Sync();
        CHECK(WaitFor(Idle, 2000));
        Emit(EV_KEY, BTN_EAST, 1); Sync();
        g_Mask = 1u << 4;
        printf("%s round %d: gamepad button 2 -> Start\n", WaitFor(Pressed, 2000) ? "ok" : "BAD", Round);
        CHECK((Read() & 0xFFFF) == g_Mask);
        Emit(EV_KEY, BTN_EAST, 0); Sync();
        CHECK(WaitFor(Idle, 2000));
        Emit(EV_ABS, ABS_X, -32768); Sync();
        printf("%s round %d: stick left -> analog left\n", WaitFor(AxisLeft, 2000) ? "ok" : "BAD", Round);
        CHECK(AxisLeft());
        Emit(EV_ABS, ABS_X, 0); Sync();
        CHECK(WaitFor(Idle, 2000));
        if (Round == 0)
        {
            // unplug while a button is down: idle, no crash; the plug again keeps the assignment
            Emit(EV_KEY, BTN_SOUTH, 1); Sync();
            Wait(200);
            DestroyPad();
            CHECK(WaitFor(Idle, 3000));
            for (int i = 0; i < 20; i++) { Read(); Wait(20); }
            printf("ok unplugged: pad idle %08x\n", Read());
            CHECK(CreatePad());
            Wait(1500);
        }
    }
    DestroyPad();
    Wait(500);
    CHECK(Idle());
    g_Plugins->Control()->Close(nullptr);
    AppCleanup();
    printf("%s input-gamepad\n", g_Failures == 0 ? "PASS" : "FAILED");
    return g_Failures == 0 ? 0 : 1;
}
