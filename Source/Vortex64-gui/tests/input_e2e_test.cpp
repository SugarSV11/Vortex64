// End to end: what the input dialog stored (input-capture-test write: 17 inputs, one per control of Player 1) really
// drives the N64 controller. A window of SDL has the focus of an X server (Xvfb), the physical keys and mouse buttons
// are pressed through the XTEST extension (the same events a keyboard and a mouse make) and the plugin is asked, as
// the emulator does, for the state of the pad with GetKeys.
//
//   input-e2e-test
#include "isolated_x_guard.h"
#include <Common/path.h>
#include <Vortex64-cli/LinuxNotification.h>
#include <Vortex64-core/AppInit.h>
#include <Vortex64-core/Notification.h>
#include <Vortex64-core/N64System/SystemGlobals.h>
#include <Vortex64-core/Plugins/ControllerPlugin.h>
#include <Vortex64-core/Plugins/Plugin.h>
#include <Vortex64-core/Settings.h>
#include <SDL2/SDL.h>
#include <X11/Xlib.h>
#include <unistd.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" int XTestFakeKeyEvent(Display *, unsigned int, int, unsigned long);
extern "C" int XTestFakeButtonEvent(Display *, unsigned int, int, unsigned long);
extern "C" int XTestFakeMotionEvent(Display *, int, int, int, unsigned long);

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

static Display * g_Display;

static void Pump(int Ms)
{
    for (int i = 0; i < Ms / 5; i++)
    {
        SDL_PumpEvents();
        usleep(5000);
    }
}

static uint32_t Read()
{
    BUTTONS Keys = {};
    g_Plugins->Control()->GetKeys(0, &Keys);
    return Keys.Value;
}

enum Kind { KEY, BUTTON, WHEEL };
// one input per control, the order of the rows of the dialog (Pad U D L R, Analog U D L R, C U D L R, B, A, Start, Z, L, R)
struct Case
{
    const char * Control;
    Kind Type;
    unsigned Code;     // X keycode, X button
    uint32_t Mask;     // the bits that must come on (0: an axis)
    int Axis;          // 1: X, 2: Y (analog), 0: none
};
static const Case Cases[] = {
    // row 0 was cleared by the capture test: nothing may drive Up of the pad
    {"Down (Left Shift)", KEY, 50, 1u << 2, 0},
    {"Left (Right Ctrl)", KEY, 105, 1u << 1, 0},
    {"Right (Escape)", KEY, 9, 1u << 0, 0},
    {"Analog Up (Return)", KEY, 36, 0, 2},
    {"Analog Down (Space)", KEY, 65, 0, 2},
    {"Analog Left (Tab)", KEY, 23, 0, 1},
    {"Analog Right (Up)", KEY, 111, 0, 1},
    {"C Up (Keypad 1)", KEY, 87, 1u << 11, 0},
    {"C Down (F8)", KEY, 74, 1u << 10, 0},
    {"C Left (Mouse left)", BUTTON, 1, 1u << 9, 0},
    {"C Right (Mouse middle)", BUTTON, 2, 1u << 8, 0},
    {"B (Mouse right)", BUTTON, 3, 1u << 6, 0},
    {"A (Mouse Back)", BUTTON, 8, 1u << 7, 0},
    {"Start (Wheel up)", WHEEL, 4, 1u << 4, 0},
    {"Z (Wheel down)", WHEEL, 5, 1u << 5, 0},
    {"L (Wheel left)", WHEEL, 6, 1u << 13, 0},
    {"R (Super)", KEY, 133, 1u << 12, 0},
};

// the second set (input-capture-test write B): every row assigned, the modifier pairs, arrows, letters, numbers, keypad
// and the rest of the mouse
static const Case CasesB[] = {
    {"Up (Right Shift)", KEY, 62, 1u << 3, 0},
    {"Down (Left Ctrl)", KEY, 37, 1u << 2, 0},
    {"Left (Left Alt)", KEY, 64, 1u << 1, 0},
    {"Right (Right Alt)", KEY, 108, 1u << 0, 0},
    {"Analog Up (Down)", KEY, 116, 0, 2},
    {"Analog Down (Left)", KEY, 113, 0, 2},
    {"Analog Left (Right)", KEY, 114, 0, 1},
    {"Analog Right (Z)", KEY, 52, 0, 1},
    {"C Up (5)", KEY, 14, 1u << 11, 0},
    {"C Down (Keypad 0)", KEY, 90, 1u << 10, 0},
    {"C Left (Mouse Forward)", BUTTON, 9, 1u << 9, 0},
    {"C Right (Mouse 6)", BUTTON, 10, 1u << 8, 0},
    {"B (Wheel right)", WHEEL, 7, 1u << 6, 0},
    {"A (Right Super)", KEY, 134, 1u << 7, 0},
    {"Start (Keypad Enter)", KEY, 104, 1u << 4, 0},
    {"Z (Q)", KEY, 24, 1u << 5, 0},
    {"L (Left Shift)", KEY, 50, 1u << 13, 0},
    {"R (Right Ctrl)", KEY, 105, 1u << 12, 0},
};

int main(int argc, char ** argv)
{
    RequireIsolatedX();
    const bool SetB = argc > 1 && strcmp(argv[1], "B") == 0;
    const Case * Set = SetB ? CasesB : Cases;
    const size_t SetSize = SetB ? sizeof(CasesB) / sizeof(CasesB[0]) : sizeof(Cases) / sizeof(Cases[0]);
    g_Display = XOpenDisplay(nullptr);
    if (g_Display == nullptr)
    {
        printf("no display\n");
        return 2;
    }
    if (SDL_Init(SDL_INIT_VIDEO) != 0)
    {
        printf("SDL: %s\n", SDL_GetError());
        return 2;
    }
    SDL_Window * Window = SDL_CreateWindow("input-e2e", 100, 100, 300, 200, SDL_WINDOW_SHOWN);
    CHECK(Window != nullptr);
    Pump(500);
    SDL_RaiseWindow(Window);
    SDL_SetWindowInputFocus(Window);
    Pump(500);
    int Wx = 0, Wy = 0;
    SDL_GetWindowPosition(Window, &Wx, &Wy);
    XTestFakeMotionEvent(g_Display, -1, Wx + 150, Wy + 100, 0);
    XFlush(g_Display);
    Pump(300);

    static CLinuxNotification Notify;
    const std::string Base = CPath(CPath::MODULE_DIRECTORY).GetDirectory();
    if (!AppInit(&Notify, Base.c_str(), 1, argv))
    {
        return 2;
    }
    static DummyRenderWindow Render;
    g_Plugins->SetRenderWindows(&Render, &Render);
    g_Plugins->CreatePlugins();
    CHECK(g_Plugins->Control() != nullptr && g_Plugins->Control()->GetKeys != nullptr);
    if (g_Plugins->Control() == nullptr)
    {
        return 2;
    }
    CHECK(g_Plugins->Control()->Initiate(nullptr, &Render));
    Pump(200);

    CHECK(Read() == 0);
    int Done = 0;
    for (size_t Index = 0; Index < SetSize; Index++)
    {
        const Case & C = Set[Index];
        auto Press = [&](bool Down) {
            if (C.Type == KEY)
            {
                XTestFakeKeyEvent(g_Display, C.Code, Down, 0);
            }
            else
            {
                XTestFakeButtonEvent(g_Display, C.Code, Down, 0);
            }
            XFlush(g_Display);
        };
        Press(true);
        uint32_t State = 0;
        for (int i = 0; i < 10; i++) // the wheel is a pulse of some milliseconds: read while it lasts
        {
            Pump(10);
            State = Read();
            if (State != 0)
            {
                break;
            }
        }
        if (C.Type == WHEEL)
        {
            Press(false); // (a wheel click is a press and a release)
        }
        else
        {
            Press(false);
        }
        const BUTTONS B = {State};
        bool Ok;
        if (C.Axis == 0)
        {
            Ok = (State & 0xFFFF) == C.Mask && B.X_AXIS == 0 && B.Y_AXIS == 0;
        }
        else if (C.Axis == 1)
        {
            Ok = (State & 0xFFFF) == 0 && B.X_AXIS != 0 && B.Y_AXIS == 0;
        }
        else
        {
            Ok = (State & 0xFFFF) == 0 && B.Y_AXIS != 0 && B.X_AXIS == 0;
        }
        printf("%s %s: state %08x\n", Ok ? "ok" : "BAD", C.Control, State);
        if (!Ok)
        {
            g_Failures++;
        }
        else
        {
            Done++;
        }
        // released: the pad is idle again
        Pump(150);
        const uint32_t Idle = Read();
        if (Idle != 0)
        {
            printf("BAD %s: not released, state %08x\n", C.Control, Idle);
            g_Failures++;
        }
    }
    if (!SetB)
    {
        // the cleared row does nothing: Escape was Right, nothing is Up
        XTestFakeKeyEvent(g_Display, 111, 1, 0); // (the Up arrow is only the right analog: no digital Up)
        XFlush(g_Display);
        Pump(50);
        CHECK((Read() & (1u << 3)) == 0);
        XTestFakeKeyEvent(g_Display, 111, 0, 0);
        XFlush(g_Display);
        Pump(50);
    }

    AppCleanup();
    SDL_DestroyWindow(Window);
    SDL_Quit();
    printf("%s e2e%s: %d of %d inputs reach the N64 pad\n", g_Failures == 0 ? "PASS" : "FAILED", SetB ? " B" : "", Done, (int)SetSize);
    return g_Failures == 0 ? 0 : 1;
}
