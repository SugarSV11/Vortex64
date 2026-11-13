#include "SdlInputBackend.h"
#include "Win32KeyboardScan.h"
#include "LinuxScancodes.h"
#include "CProject64Input.h"
#include <Common/StdString.h>
#include <cstddef>
#include <cmath>
#include <cstring>
#include <set>

// Match dinput.h device type low bytes used by existing scan routing.
static const uint32_t kDevTypeKeyboard = 0x13;
static const uint32_t kDevTypeMouse = 0x12;
static const uint32_t kDevTypeJoystick = 0x15;

void CSdlInput::MakeKeyboardMouseGuids(GUID * outKeyboard, GUID * outMouse)
{
    // Stable virtual GUIDs for SDL keyboard/mouse (not tied to DirectInput).
    static const GUID kb = { 0xA15C6E10, 0x0000, 0x4000, { 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01 } };
    static const GUID ms = { 0xA15C6E10, 0x0000, 0x4000, { 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02 } };
    *outKeyboard = kb;
    *outMouse = ms;
}

GUID CSdlInput::GuidFromSdlJoystick(SDL_JoystickID id)
{
    SDL_Joystick * j = SDL_JoystickFromInstanceID(id);
    if (j == nullptr)
    {
        return GUID{};
    }
    SDL_JoystickGUID g = SDL_JoystickGetGUID(j);
    GUID out{};
    static_assert(sizeof(g.data) == 16, "SDL GUID size");
    std::memcpy(&out, g.data, 16);
    return out;
}

CSdlInput::CSdlInput(HINSTANCE hinst) :
    m_hinst(hinst),
    m_hWnd(nullptr),
    m_PumpHwnd(nullptr),
    m_SdlInited(false),
    m_ConfigDialogOpen(false),
    m_ScanActive(false),
    m_RomOpen(false),
    m_MouseAccumX(0),
    m_MouseAccumY(0),
    m_MouseAccumZ(0)
{
    MakeKeyboardMouseGuids(&m_GuidKeyboard, &m_GuidMouse);
    if (SDL_InitSubSystem(SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) == 0)
    {
        m_SdlInited = true;
        SDL_GameControllerEventState(SDL_ENABLE);
        SDL_JoystickEventState(SDL_ENABLE);
        for (std::atomic<uint32_t> & Until : m_WheelUntil)
        {
            Until = 0;
        }
        for (std::atomic<uint8_t> & Key : m_ExtKeys)
        {
            Key = 0;
        }
        for (std::atomic<uint8_t> & Button : m_ExtButtons)
        {
            Button = 0;
        }
        SDL_AddEventWatch(WatchEvents, this);
        RefreshDeviceList();
    }
}

void CSdlInput::CloseJoystickDevices(void)
{
    for (DEVICE_MAP::iterator itr = m_Devices.begin(); itr != m_Devices.end(); ++itr)
    {
        DEVICE_ENTRY & d = itr->second;
        if (d.gamecontroller != nullptr)
        {
            SDL_GameControllerClose(d.gamecontroller);
            d.gamecontroller = nullptr;
            d.joystick = nullptr;
        }
        else if (d.joystick != nullptr)
        {
            SDL_JoystickClose(d.joystick);
            d.joystick = nullptr;
        }
    }
}

// Called by SDL when an event is added, in the thread that adds it, before anybody polls it: the wheel (it is a
// movement, not a state) and the connection of controllers are noted here
int SDLCALL CSdlInput::WatchEvents(void * Data, SDL_Event * Event)
{
    CSdlInput * This = static_cast<CSdlInput *>(Data);
    switch (Event->type)
    {
    case SDL_MOUSEWHEEL:
    {
        const uint32_t Until = SDL_GetTicks() + 90;
        // natural scrolling: SDL asks the application to negate the axes
        const int Sign = Event->wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1 : 1;
        const int X = Sign * Event->wheel.x, Y = Sign * Event->wheel.y;
        if (Y > 0) This->m_WheelUntil[0] = Until;
        if (Y < 0) This->m_WheelUntil[1] = Until;
        if (X < 0) This->m_WheelUntil[2] = Until;
        if (X > 0) This->m_WheelUntil[3] = Until;
        break;
    }
    case SDL_JOYDEVICEADDED:
    case SDL_JOYDEVICEREMOVED:
    case SDL_CONTROLLERDEVICEADDED:
    case SDL_CONTROLLERDEVICEREMOVED:
    case SDL_CONTROLLERDEVICEREMAPPED:
        This->m_HotPlugEvent = true;
        break;
    }
    return 0;
}

CSdlInput::~CSdlInput()
{
    DestroyPumpWindow();
    if (!m_SdlInited)
    {
        return;
    }
    SDL_DelEventWatch(WatchEvents, this);
    CloseJoystickDevices();
    m_Devices.clear();
    SDL_QuitSubSystem(SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS);
}

void CSdlInput::Initiate(CONTROL_INFO * ControlInfo)
{
    m_hWnd = (HWND)ControlInfo->hWnd;
    EnsurePumpWindow();
}

// On Linux there is no message-pump window: UpdateDeviceData() and
// ScanDevices() already call SDL_PumpEvents() directly (see ScanDevices'
// `m_PumpHwnd == nullptr` fast path, which is always true here), so the
// pump window itself is unnecessary and these are no-ops.
void CSdlInput::EnsurePumpWindow(void)
{
}

void CSdlInput::DestroyPumpWindow(void)
{
}

void CSdlInput::ApplyPumpTimerInterval(void)
{
}

void CSdlInput::NotifyConfigDialogOpen(bool open)
{
    m_ConfigDialogOpen = open;
    ApplyPumpTimerInterval();
}

void CSdlInput::NotifyScanActive(bool active)
{
    m_ScanActive = active;
    ApplyPumpTimerInterval();
}

void CSdlInput::NotifyRomOpen(bool open)
{
    m_RomOpen = open;
    ApplyPumpTimerInterval();
}

void CSdlInput::MapControllerDevice(N64CONTROLLER & Controller)
{
    BUTTON * Buttons[] =
    {
        &Controller.U_DPAD,
        &Controller.D_DPAD,
        &Controller.L_DPAD,
        &Controller.R_DPAD,
        &Controller.A_BUTTON,
        &Controller.B_BUTTON,
        &Controller.U_CBUTTON,
        &Controller.D_CBUTTON,
        &Controller.L_CBUTTON,
        &Controller.R_CBUTTON,
        &Controller.START_BUTTON,
        &Controller.Z_TRIG,
        &Controller.R_TRIG,
        &Controller.L_TRIG,
        &Controller.U_ANALOG,
        &Controller.D_ANALOG,
        &Controller.L_ANALOG,
        &Controller.R_ANALOG,
    };

    CGuard Guard(m_DeviceCS);
    for (size_t i = 0, n = sizeof(Buttons) / sizeof(Buttons[0]); i < n; i++)
    {
        DEVICE_MAP::iterator itr = m_Devices.find(Buttons[i]->DeviceGuid);
        if (itr != m_Devices.end())
        {
            Buttons[i]->Device = &itr->second;
        }
        else
        {
            Buttons[i]->Device = nullptr;
        }
    }
}

void CSdlInput::MapShortcutDevice(SHORTCUTS& Shortcuts)
{
    BUTTON* Buttons[] =
    {
        &Shortcuts.LOCKMOUSE,
    };

    CGuard Guard(m_DeviceCS);
    for (size_t i = 0, n = sizeof(Buttons) / sizeof(Buttons[0]); i < n; i++)
    {
        DEVICE_MAP::iterator itr = m_Devices.find(Buttons[i]->DeviceGuid);
        if (itr != m_Devices.end())
        {
            Buttons[i]->Device = &itr->second;
        }
        else
        {
            Buttons[i]->Device = nullptr;
        }
    }
}

void CSdlInput::RefreshDeviceList(void)
{
    CloseJoystickDevices();
    m_Devices.clear();

    DEVICE_ENTRY k{};
    k.dwDevType = kDevTypeKeyboard;
    k.joystick = nullptr;
    k.gamecontroller = nullptr;
    k.sdlJoystickIndex = -1;
    k.ProductName = "Keyboard";
    k.InstanceName = "Keyboard";
    std::memset(k.State.Keyboard, 0, sizeof(k.State.Keyboard));
    m_Devices.insert(DEVICE_MAP::value_type(m_GuidKeyboard, k));

    DEVICE_ENTRY m{};
    m.dwDevType = kDevTypeMouse;
    m.joystick = nullptr;
    m.gamecontroller = nullptr;
    m.sdlJoystickIndex = -1;
    m.ProductName = "Mouse";
    m.InstanceName = "Mouse";
    std::memset(&m.State.Mouse, 0, sizeof(m.State.Mouse));
    m_Devices.insert(DEVICE_MAP::value_type(m_GuidMouse, m));

    const int nj = SDL_NumJoysticks();
    for (int i = 0; i < nj; ++i)
    {
        char guidstr[64];
        SDL_JoystickGUID jg = SDL_JoystickGetDeviceGUID(i);
        SDL_GUIDToString(jg, guidstr, sizeof(guidstr));

        DEVICE_ENTRY d{};
        d.dwDevType = kDevTypeJoystick;
        d.sdlJoystickIndex = i;

        if (SDL_IsGameController(i))
        {
            d.gamecontroller = SDL_GameControllerOpen(i);
            if (d.gamecontroller == nullptr)
            {
                continue;
            }
            d.joystick = SDL_GameControllerGetJoystick(d.gamecontroller);
        }
        else
        {
            d.joystick = SDL_JoystickOpen(i);
            if (d.joystick == nullptr)
            {
                continue;
            }
        }

        const SDL_JoystickID jid = SDL_JoystickInstanceID(d.joystick);
        const GUID devGuid = GuidFromSdlJoystick(jid);
        d.InstanceName = SDL_JoystickName(d.joystick) ? SDL_JoystickName(d.joystick) : "Joystick";
        d.ProductName = d.InstanceName;

        std::memset(&d.State.Joy, 0, sizeof(d.State.Joy));
        m_Devices.insert(DEVICE_MAP::value_type(devGuid, d));
        if (g_InputPlugin != nullptr)
        {
            g_InputPlugin->DeviceAdded();
        }
    }
}

static int32_t ScaleSdlAxis(Sint16 v)
{
    const int32_t x = (int32_t)v + 32768;
    if (x < 0)
        return 0;
    if (x > 65535)
        return 65535;
    return x;
}

void CSdlInput::FillJoyState(const DEVICE_ENTRY & dev, DIJOYSTATE_COMPAT & j)
{
    std::memset(&j, 0, sizeof(j));

    if (dev.gamecontroller != nullptr)
    {
        SDL_GameController * gc = dev.gamecontroller;
        j.lX = ScaleSdlAxis(SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTX));
        j.lY = ScaleSdlAxis(SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTY));
        j.lZ = ScaleSdlAxis(SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_TRIGGERLEFT));
        j.lRx = ScaleSdlAxis(SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_RIGHTX));
        j.lRy = ScaleSdlAxis(SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_RIGHTY));
        j.lRz = ScaleSdlAxis(SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_TRIGGERRIGHT));
        j.rglSlider[0] = j.lZ;
        j.rglSlider[1] = j.lRz;

        const Uint8 hat = SDL_JoystickGetHat(dev.joystick, 0);
        if ((hat & SDL_HAT_UP) != 0)
            j.rgdwPOV[0] = 0;
        else if ((hat & SDL_HAT_RIGHT) != 0)
            j.rgdwPOV[0] = 9000;
        else if ((hat & SDL_HAT_DOWN) != 0)
            j.rgdwPOV[0] = 18000;
        else if ((hat & SDL_HAT_LEFT) != 0)
            j.rgdwPOV[0] = 27000;
        else
            j.rgdwPOV[0] = 0xFFFF;

        for (int b = 0; b < 32 && b < SDL_JoystickNumButtons(dev.joystick); ++b)
        {
            j.rgbButtons[b] = SDL_JoystickGetButton(dev.joystick, b) ? 0x80 : 0;
        }
        return;
    }

    if (dev.joystick == nullptr)
    {
        return;
    }

    const int nax = SDL_JoystickNumAxes(dev.joystick);
    if (nax > 0)
        j.lX = ScaleSdlAxis(SDL_JoystickGetAxis(dev.joystick, 0));
    if (nax > 1)
        j.lY = ScaleSdlAxis(SDL_JoystickGetAxis(dev.joystick, 1));
    if (nax > 2)
        j.lZ = ScaleSdlAxis(SDL_JoystickGetAxis(dev.joystick, 2));
    if (nax > 3)
        j.lRx = ScaleSdlAxis(SDL_JoystickGetAxis(dev.joystick, 3));
    if (nax > 4)
        j.lRy = ScaleSdlAxis(SDL_JoystickGetAxis(dev.joystick, 4));
    if (nax > 5)
        j.lRz = ScaleSdlAxis(SDL_JoystickGetAxis(dev.joystick, 5));

    const int nh = SDL_JoystickNumHats(dev.joystick);
    if (nh > 0)
    {
        const Uint8 hat = SDL_JoystickGetHat(dev.joystick, 0);
        if ((hat & SDL_HAT_UP) != 0)
            j.rgdwPOV[0] = 0;
        else if ((hat & SDL_HAT_RIGHT) != 0)
            j.rgdwPOV[0] = 9000;
        else if ((hat & SDL_HAT_DOWN) != 0)
            j.rgdwPOV[0] = 18000;
        else if ((hat & SDL_HAT_LEFT) != 0)
            j.rgdwPOV[0] = 27000;
        else
            j.rgdwPOV[0] = 0xFFFF;
    }

    for (int b = 0; b < 32 && b < SDL_JoystickNumButtons(dev.joystick); ++b)
    {
        j.rgbButtons[b] = SDL_JoystickGetButton(dev.joystick, b) ? 0x80 : 0;
    }
}

void CSdlInput::SyncJoystickState(DEVICE_ENTRY & dev)
{
    FillJoyState(dev, dev.State.Joy);
}

void CSdlInput::ExternalEvent(int Kind, int Code, int Value)
{
    switch (Kind)
    {
    case 0:
        if (Code >= 0 && Code < (int)(sizeof(linux_scancode_table) / sizeof(linux_scancode_table[0])))
        {
            const int Scancode = (int)linux_scancode_table[Code];
            if (Scancode > 0 && Scancode < (int)(sizeof(m_ExtKeys) / sizeof(m_ExtKeys[0])))
            {
                m_ExtKeys[Scancode] = Value != 0 ? 1 : 0;
            }
        }
        break;
    case 1:
        if (Code >= 0 && Code < 32)
        {
            m_ExtButtons[Code] = Value != 0 ? 1 : 0;
        }
        break;
    case 2:
        if (Code >= 0 && Code < 4)
        {
            m_WheelUntil[Code] = SDL_GetTicks() + 90;
        }
        break;
    case 3:
        m_ExtMotionX += Code;
        m_ExtMotionY += Value;
        break;
    case 4:
        for (std::atomic<uint8_t> & Key : m_ExtKeys)
        {
            Key = 0;
        }
        for (std::atomic<uint8_t> & Button : m_ExtButtons)
        {
            Button = 0;
        }
        break;
    }
}

void CSdlInput::UpdateDeviceData(void)
{
    CGuard Guard(m_DeviceCS);
    SDL_PumpEvents();
    if (m_HotPlugEvent.exchange(false))
    {
        // (the list is made again by the owner, which maps the buttons to the new devices: see TakeHotPlug)
        m_HotPlugPending = true;
    }

    DEVICE_MAP::iterator kit = m_Devices.find(m_GuidKeyboard);
    if (kit != m_Devices.end())
    {
        int numkeys = 0;
        const Uint8 * state = SDL_GetKeyboardState(&numkeys);
        const int n = (int)sizeof(kit->second.State.Keyboard);
        std::memset(kit->second.State.Keyboard, 0, n);
        const int copy = (numkeys < n) ? numkeys : n;
        for (int i = 0; i < copy; ++i)
        {
            kit->second.State.Keyboard[i] = state[i] ? 0xFF : 0;
        }
        // what the window of the application received (ExternalEvent)
        for (int i = 0; i < n && i < (int)(sizeof(m_ExtKeys) / sizeof(m_ExtKeys[0])); ++i)
        {
            if (m_ExtKeys[i] != 0)
            {
                kit->second.State.Keyboard[i] = 0xFF;
            }
        }
        Win32MergeKeyboardOrInto(kit->second.State.Keyboard, sizeof(kit->second.State.Keyboard));
    }

    DEVICE_MAP::iterator mit = m_Devices.find(m_GuidMouse);
    if (mit != m_Devices.end())
    {
        int x = 0, y = 0;
        const Uint32 btn = SDL_GetRelativeMouseState(&x, &y);
        x += m_ExtMotionX.exchange(0);
        y += m_ExtMotionY.exchange(0);
        mit->second.State.Mouse.lX = (LONG)(x * 10);
        mit->second.State.Mouse.lY = (LONG)(y * 10);
        mit->second.State.Mouse.lZ = 0;
        for (int b = 0; b < 32; ++b)
        {
            mit->second.State.Mouse.rgbButtons[b] = ((btn & (1u << b)) || m_ExtButtons[b] != 0) ? 0x80 : 0;
        }
        const uint32_t Now = SDL_GetTicks();
        for (int w = 0; w < 4; ++w)
        {
            mit->second.State.Mouse.rgbButtons[32 + w] = (int32_t)(m_WheelUntil[w] - Now) > 0 ? 0x80 : 0;
        }
    }

    for (DEVICE_MAP::iterator itr = m_Devices.begin(); itr != m_Devices.end(); ++itr)
    {
        if (itr->first == m_GuidKeyboard || itr->first == m_GuidMouse)
            continue;
        SyncJoystickState(itr->second);
    }
}

CSdlInput::ScanResult CSdlInput::ScanDevices(BUTTON & Button)
{
    if (m_PumpHwnd == nullptr)
    {
        SDL_PumpEvents();
    }

    ScanResult Result = SCAN_FAILED;

    CGuard Guard(m_DeviceCS);

    for (DEVICE_MAP::iterator itr = m_Devices.begin(); itr != m_Devices.end(); itr++)
    {
        DEVICE_ENTRY & device = itr->second;
        uint8_t DeviceType = (uint8_t)(device.dwDevType & 0xFF);
        if (DeviceType == kDevTypeKeyboard)
        {
            Result = ScanKeyboard(itr->first, device.State.Keyboard, Button);
        }
        else if (DeviceType != kDevTypeMouse)
        {
            Result = ScanGamePad(itr->first, device.State.Joy, Button);
        }

        if (Result != SCAN_FAILED)
        {
            return Result;
        }
    }

    for (DEVICE_MAP::iterator itr = m_Devices.begin(); itr != m_Devices.end(); itr++)
    {
        DEVICE_ENTRY & device = itr->second;
        uint8_t DeviceType = (uint8_t)(device.dwDevType & 0xFF);
        if (DeviceType == kDevTypeMouse)
        {
            Result = ScanMouse(itr->first, device.State.Mouse, Button);
        }
        if (Result != SCAN_FAILED)
        {
            return Result;
        }
    }
    return Result;
}

std::wstring CSdlInput::ButtonAssignment(BUTTON & Button)
{
    static const char * iGamepad[] =
    {
        "X-axis", "Y-axis", "Z-axis", "X-rotation", "Y-rotation", "Z-rotation",
        "Slider", "Slider", "PoV", "PoV", "PoV", "PoV", "Button"
    };
    static const char * AxeID[] = { " +", " -", " /\\", " >", " \\/", " <" };
    static const char* iMouse[] = { "X-axis", "Y-axis", "Z-axis", "Button" };

    if (Button.BtnType == BTNTYPE_JOYBUTTON)
    {
        return stdstr_f("Button %u", (unsigned)Button.Offset).ToUTF16();
    }
    if (Button.BtnType == BTNTYPE_JOYAXE)
    {
        stdstr_f Offset("%u", (unsigned)Button.Offset);
        if (Button.Offset < (sizeof(iGamepad) / sizeof(iGamepad[0])))
        {
            Offset = iGamepad[Button.Offset];
        }
        stdstr_f AxisId(" %u", Button.AxisID);
        if (Button.AxisID < (sizeof(AxeID) / sizeof(AxeID[0])))
        {
            AxisId = AxeID[Button.AxisID];
        }
        return stdstr_f("%s%s", Offset.c_str(), AxisId.c_str()).ToUTF16();
    }
    if (Button.BtnType == BTNTYPE_JOYPOV)
    {
        stdstr_f Offset("%u", (unsigned)Button.Offset);
        if (Button.Offset < (sizeof(iGamepad) / sizeof(iGamepad[0])))
        {
            Offset = iGamepad[Button.Offset];
        }
        stdstr_f AxisId(" %u", Button.AxisID);
        if ((Button.AxisID + 2) < (sizeof(AxeID) / sizeof(AxeID[0])))
        {
            AxisId = AxeID[Button.AxisID + 2];
        }
        return stdstr_f("%s%s", Offset.c_str(), AxisId.c_str()).ToUTF16();
    }
    if (Button.BtnType == BTNTYPE_KEYBUTTON)
    {
        const char * name = SDL_GetScancodeName((SDL_Scancode)Button.Offset);
        if (name && name[0])
        {
            return stdstr(name).ToUTF16();
        }
        return stdstr_f("Key %u", (unsigned)Button.Offset).ToUTF16();
    }
    if (Button.BtnType == BTNTYPE_MOUSEAXE)
    {
        stdstr_f Offset("%u", (unsigned)Button.Offset);
        if (Button.Offset < (sizeof(iMouse) / sizeof(iMouse[0])))
        {
            Offset = iMouse[Button.Offset];
        }
        stdstr_f AxisId(" %u", Button.AxisID);
        if (Button.AxisID < (sizeof(AxeID) / sizeof(AxeID[0])))
        {
            AxisId = AxeID[Button.AxisID];
        }
        return stdstr_f("%s%s", Offset.c_str(), AxisId.c_str()).ToUTF16();
    }
    if (Button.BtnType == BTNTYPE_MOUSEBUTTON)
    {
        static const char * Names[] = {"Mouse Left", "Mouse Middle", "Mouse Right", "Mouse 4 (Back)", "Mouse 5 (Forward)"};
        static const char * Wheel[] = {"Mouse Wheel Up", "Mouse Wheel Down", "Mouse Wheel Left", "Mouse Wheel Right"};
        if (Button.Offset < sizeof(Names) / sizeof(Names[0]))
        {
            return stdstr(Names[Button.Offset]).ToUTF16();
        }
        if (Button.Offset >= 32 && Button.Offset < 32 + sizeof(Wheel) / sizeof(Wheel[0]))
        {
            return stdstr(Wheel[Button.Offset - 32]).ToUTF16();
        }
        return stdstr_f("Mouse %u", (unsigned)Button.Offset + 1).ToUTF16();
    }
    if (Button.BtnType == BTNTYPE_UNASSIGNED)
    {
        return L"";
    }
    return L"Unknown";
}

std::wstring CSdlInput::ControllerDevices(const N64CONTROLLER & Controller)
{
    const BUTTON * Buttons[] =
    {
        &Controller.U_DPAD, &Controller.D_DPAD, &Controller.L_DPAD, &Controller.R_DPAD,
        &Controller.A_BUTTON, &Controller.B_BUTTON, &Controller.U_CBUTTON, &Controller.D_CBUTTON,
        &Controller.L_CBUTTON, &Controller.R_CBUTTON, &Controller.START_BUTTON, &Controller.Z_TRIG,
        &Controller.R_TRIG, &Controller.L_TRIG, &Controller.U_ANALOG, &Controller.D_ANALOG,
        &Controller.L_ANALOG, &Controller.R_ANALOG,
    };
    typedef std::set<GUID, GUIDComparer> GUID_LIST;
    GUID_LIST DeviceGuids;
    GUID EmptyGuid = { 0 };

    for (size_t i = 0, n = sizeof(Buttons) / sizeof(Buttons[0]); i < n; i++)
    {
        if (DeviceGuids.find(Buttons[i]->DeviceGuid) != DeviceGuids.end())
        {
            continue;
        }
        if (memcmp(&Buttons[i]->DeviceGuid, &EmptyGuid, sizeof(EmptyGuid)) == 0)
        {
            continue;
        }
        DeviceGuids.insert(Buttons[i]->DeviceGuid);
    }

    std::wstring DeviceList;
    CGuard Guard(m_DeviceCS);
    bool UnknownDevice = false;
    for (GUID_LIST::iterator itr = DeviceGuids.begin(); itr != DeviceGuids.end(); itr++)
    {
        DEVICE_MAP::iterator DeviceItr = m_Devices.find(*itr);
        if (DeviceItr == m_Devices.end())
        {
            UnknownDevice = true;
            continue;
        }
        if (!DeviceList.empty())
        {
            DeviceList += L", ";
        }
        DeviceList += stdstr(DeviceItr->second.ProductName).ToUTF16();
    }
    if (UnknownDevice)
    {
        if (!DeviceList.empty())
        {
            DeviceList += L", ";
        }
        DeviceList += L"Unknown Device";
    }
    return DeviceList;
}

bool CSdlInput::IsButtonPressed(BUTTON & Button)
{
    if (Button.Device == nullptr)
    {
        return false;
    }
    DEVICE_ENTRY & Device = *(DEVICE_ENTRY *)Button.Device;
    switch (Button.BtnType)
    {
    case BTNTYPE_KEYBUTTON:
        return Button.Offset < sizeof(Device.State.Keyboard) && (Device.State.Keyboard[Button.Offset] & 0x80) != 0;
    case BTNTYPE_JOYBUTTON:
        return Button.Offset < sizeof(Device.State.Joy.rgbButtons) && (Device.State.Joy.rgbButtons[Button.Offset] & 0x80) != 0;
    case BTNTYPE_MOUSEBUTTON:
        return Button.Offset < sizeof(Device.State.Mouse.rgbButtons) && (Device.State.Mouse.rgbButtons[Button.Offset] & 0x80) != 0;
    case BTNTYPE_JOYPOV:
        return JoyPadPovPressed((AI_POV)Button.AxisID, (int32_t)((uint32_t *)&Device.State.Joy)[Button.Offset]);
    case BTNTYPE_JOYSLIDER:
    case BTNTYPE_JOYAXE:
        return Button.AxisID ? ((uint32_t*)&Device.State.Joy)[Button.Offset] > (uint32_t)AXIS_BOTTOM_VALUE
                             : ((uint32_t *)&Device.State.Joy)[Button.Offset] < (uint32_t)AXIS_TOP_VALUE;
    case BTNTYPE_MOUSEAXE:
        return Button.AxisID ? ((uint32_t*)&Device.State.Mouse)[Button.Offset] > (uint32_t)AXIS_BOTTOM_VALUE
                             : ((uint32_t*)&Device.State.Mouse)[Button.Offset] < (uint32_t)AXIS_TOP_VALUE;
    }
    return false;
}

void CSdlInput::GetAxis(N64CONTROLLER & Controller, BUTTONS * Keys)
{
    enum { N64DIVIDER = 258 };
    Keys->X_AXIS = 0;
    Keys->Y_AXIS = 0;

    bool b_Value;
    long l_Value = 0;
    long lAxisValueX = 0;
    long lAxisValueY = 0;

    // (100 % would divide by zero below: the whole range cannot be a dead zone)
    uint8_t bPadDeadZone = Controller.DeadZone > 99 ? 99 : Controller.DeadZone;
    long lDeadZoneValue = bPadDeadZone * RANGE_RELATIVE / 100;
    float fDeadZoneRelation = (float)RANGE_RELATIVE / (float)(RANGE_RELATIVE - lDeadZoneValue);

    struct AxisBind
    {
        BUTTON & Button;
        bool Negative;
    }
    Buttons[] =
    {
        { Controller.R_ANALOG, false },
        { Controller.L_ANALOG, true },
        { Controller.D_ANALOG, true },
        { Controller.U_ANALOG, false },
    };

    for (size_t i = 0, n = sizeof(Buttons) / sizeof(Buttons[0]); i < n; i++)
    {
        bool fNegInput = Buttons[i].Negative;
        BUTTON & Btn = Buttons[i].Button;
        if (Btn.Device == nullptr)
        {
            continue;
        }
        DEVICE_ENTRY & Dev = *(DEVICE_ENTRY *)Btn.Device;
        LPLONG plRawState = (LPLONG)&Dev.State.Joy;
        LPLONG plRawStateMouse = (LPLONG)&Dev.State.Mouse;

        switch (Btn.BtnType)
        {
        case BTNTYPE_JOYSLIDER:
        case BTNTYPE_JOYAXE:
            l_Value = (plRawState[Btn.Offset] - MAX_AXIS_VALUE) * -1;
            if (Btn.AxisID == AI_AXE_NEGATIVE)
            {
                fNegInput = !fNegInput;
                b_Value = (l_Value < 0);
            }
            else
            {
                b_Value = (l_Value > 0);
            }
            break;
        case BTNTYPE_MOUSEAXE:
            l_Value = (plRawStateMouse[Btn.Offset]) * -1;
            l_Value *= Controller.Sensitivity * MOUSESCALEVALUE;
            if (Btn.AxisID == AI_AXE_NEGATIVE)
            {
                fNegInput = !fNegInput;
                b_Value = (l_Value < 0);
            }
            else
            {
                b_Value = (l_Value > 0);
            }
            break;
        case BTNTYPE_KEYBUTTON:
            b_Value = (Dev.State.Keyboard[Btn.Offset] & 0x80) != 0;
            if (b_Value)
            {
                l_Value = MAX_AXIS_VALUE;
            }
            break;
        case BTNTYPE_JOYBUTTON:
            b_Value = Btn.Offset < sizeof(Dev.State.Joy.rgbButtons) && (Dev.State.Joy.rgbButtons[Btn.Offset] & 0x80) != 0;
            if (b_Value)
            {
                l_Value = MAX_AXIS_VALUE;
            }
            break;
        case BTNTYPE_MOUSEBUTTON:
            b_Value = Btn.Offset < sizeof(Dev.State.Mouse.rgbButtons) && (Dev.State.Mouse.rgbButtons[Btn.Offset] & 0x80) != 0;
            if (b_Value)
            {
                l_Value = MAX_AXIS_VALUE;
            }
            break;
        case BTNTYPE_JOYPOV:
            b_Value = JoyPadPovPressed((AI_POV)Btn.AxisID, ((uint32_t *)&Dev.State.Joy)[Btn.Offset]);
            if (b_Value)
            {
                l_Value = MAX_AXIS_VALUE;
            }
            break;
        default:
            b_Value = false;
        }

        if (b_Value)
        {
            if (fNegInput)
            {
                l_Value = -l_Value;
            }
            if (i < 2)
            {
                lAxisValueX += l_Value;
            }
            else
            {
                lAxisValueY += l_Value;
            }
        }
    }

    long lAbsoluteX = (lAxisValueX > 0) ? lAxisValueX : -lAxisValueX;
    long lAbsoluteY = (lAxisValueY > 0) ? lAxisValueY : -lAxisValueY;

    if (lAbsoluteX * lAbsoluteX + lAbsoluteY * lAbsoluteY > lDeadZoneValue * lDeadZoneValue)
    {
        double dMagnitudeDiagonal = sqrt((double)lAbsoluteX * lAbsoluteX + (double)lAbsoluteY * lAbsoluteY);
        double dRel = ((dMagnitudeDiagonal - lDeadZoneValue) / dMagnitudeDiagonal * fDeadZoneRelation);
        lAxisValueX = (long)(lAxisValueX * dRel);
        lAxisValueY = (long)(lAxisValueY * dRel);
    }
    else
    {
        lAxisValueX = lAxisValueY = 0;
    }

    if (Controller.RealN64Range && (lAxisValueX || lAxisValueY))
    {
        long lRangeX = lAbsoluteX > lAbsoluteY ? MAX_AXIS_VALUE : MAX_AXIS_VALUE * lAbsoluteX / (lAbsoluteY ? lAbsoluteY : 1);
        long lRangeY = lAbsoluteX > lAbsoluteY ? MAX_AXIS_VALUE * lAbsoluteY / (lAbsoluteX ? lAbsoluteX : 1) : MAX_AXIS_VALUE;

        double dRangeDiagonal = sqrt((double)(lRangeX * lRangeX + lRangeY * lRangeY));
        double dRel = MAX_AXIS_VALUE / dRangeDiagonal;
        lAxisValueX = (long)(lAxisValueX * dRel);
        lAxisValueY = (long)(lAxisValueY * dRel);
    }
    if (lAxisValueX > MAX_AXIS_VALUE)
    {
        lAxisValueX = MAX_AXIS_VALUE;
    }
    if (lAxisValueX < MIN_AXIS_VALUE)
    {
        lAxisValueX = MIN_AXIS_VALUE;
    }
    if (lAxisValueY > MAX_AXIS_VALUE)
    {
        lAxisValueY = MAX_AXIS_VALUE;
    }
    if (lAxisValueY < MIN_AXIS_VALUE)
    {
        lAxisValueY = MIN_AXIS_VALUE;
    }
    Keys->X_AXIS = (int8_t)(lAxisValueX / N64DIVIDER);
    Keys->Y_AXIS = (int8_t)(lAxisValueY / N64DIVIDER);
}

void CSdlInput::PollHotPlug(void)
{
    CGuard Guard(m_DeviceCS);
    SDL_PumpEvents();
    if (m_HotPlugEvent.exchange(false))
    {
        m_HotPlugPending = true;
    }
}

bool CSdlInput::TakeHotPlug(void)
{
    CGuard Guard(m_DeviceCS);
    const bool Pending = m_HotPlugPending;
    m_HotPlugPending = false;
    return Pending;
}

void CSdlInput::DevicesChanged(void)
{
    CGuard Guard(m_DeviceCS); // (the polls and the scans read the list under this lock)
    RefreshDeviceList();
}

CSdlInput::ScanResult CSdlInput::ScanKeyboard(const GUID & DeviceGuid, uint8_t * KeyboardState, BUTTON & pButton)
{
    (void)DeviceGuid;
    int numkeys = 0;
    const Uint8 * cKeys = SDL_GetKeyboardState(&numkeys);
    uint8_t tmp[512];
    std::memset(tmp, 0, sizeof(tmp));
    const int lim = (numkeys < (int)sizeof(tmp)) ? numkeys : (int)sizeof(tmp);
    for (int i = 0; i < lim; ++i)
    {
        tmp[i] = cKeys[i] ? 0xFF : 0;
    }
    Win32MergeKeyboardOrInto(tmp, sizeof(tmp));

    for (size_t i = 0, n = sizeof(tmp) / sizeof(tmp[0]); i < n; i++)
    {
        if (KeyboardState[i] == tmp[i])
        {
            continue;
        }
        KeyboardState[i] = tmp[i];
        if ((tmp[i] & 0x80) == 0)
        {
            continue;
        }
        if (i == (size_t)SDL_SCANCODE_ESCAPE) // was DIK_ESCAPE
        {
            return SCAN_ESCAPE;
        }
        pButton.Offset = (uint16_t)i;
        pButton.AxisID = 0;
        pButton.BtnType = BTNTYPE_KEYBUTTON;
        pButton.DeviceGuid = m_GuidKeyboard;
        pButton.Device = nullptr;
        return SCAN_SUCCEED;
    }
    return SCAN_FAILED;
}

CSdlInput::ScanResult CSdlInput::ScanGamePad(const GUID & DeviceGuid, DIJOYSTATE_COMPAT & BaseState, BUTTON & pButton)
{
    DIJOYSTATE_COMPAT JoyState = {};
    DEVICE_MAP::iterator itr = m_Devices.find(DeviceGuid);
    if (itr == m_Devices.end())
    {
        return SCAN_FAILED;
    }
    FillJoyState(itr->second, JoyState);

    uint32_t JoyPad[][2] =
    {
        { (uint32_t)(offsetof(DIJOYSTATE_COMPAT, lX) / sizeof(uint32_t)), BTNTYPE_JOYAXE },
        { (uint32_t)(offsetof(DIJOYSTATE_COMPAT, lY) / sizeof(uint32_t)), BTNTYPE_JOYAXE },
        { (uint32_t)(offsetof(DIJOYSTATE_COMPAT, lZ) / sizeof(uint32_t)), BTNTYPE_JOYAXE },
        { (uint32_t)(offsetof(DIJOYSTATE_COMPAT, lRx) / sizeof(uint32_t)), BTNTYPE_JOYAXE },
        { (uint32_t)(offsetof(DIJOYSTATE_COMPAT, lRy) / sizeof(uint32_t)), BTNTYPE_JOYAXE },
        { (uint32_t)(offsetof(DIJOYSTATE_COMPAT, lRz) / sizeof(uint32_t)), BTNTYPE_JOYAXE },
        { (uint32_t)(offsetof(DIJOYSTATE_COMPAT, rglSlider[0]) / sizeof(uint32_t)), BTNTYPE_JOYSLIDER },
        { (uint32_t)(offsetof(DIJOYSTATE_COMPAT, rglSlider[1]) / sizeof(uint32_t)), BTNTYPE_JOYSLIDER },
        { (uint32_t)(offsetof(DIJOYSTATE_COMPAT, rgdwPOV) / sizeof(uint32_t)), BTNTYPE_JOYPOV },
        { (uint32_t)(offsetof(DIJOYSTATE_COMPAT, rgdwPOV) / sizeof(uint32_t)) + 1, BTNTYPE_JOYPOV },
        { (uint32_t)(offsetof(DIJOYSTATE_COMPAT, rgdwPOV) / sizeof(uint32_t)) + 2, BTNTYPE_JOYPOV },
        { (uint32_t)(offsetof(DIJOYSTATE_COMPAT, rgdwPOV) / sizeof(uint32_t)) + 3, BTNTYPE_JOYPOV }
    };

    uint8_t bAxeDirection = 0;
    int32_t foundJoyPad = -1;

    for (int32_t i = 0, n = (int32_t)(sizeof(JoyPad) / sizeof(JoyPad[0])); i < n; i++)
    {
        uint32_t lValue = ((uint32_t*)&JoyState)[JoyPad[i][0]];
        uint32_t BaseValue = ((uint32_t*)&BaseState)[JoyPad[i][0]];

        if ((JoyPad[i][1] == BTNTYPE_JOYAXE) || (JoyPad[i][1] == BTNTYPE_JOYSLIDER))
        {
            if ((lValue < (uint32_t)AXIS_TOP_VALUE && BaseValue < (uint32_t)AXIS_TOP_VALUE) ||
                (lValue > (uint32_t)AXIS_BOTTOM_VALUE && BaseValue > (uint32_t)AXIS_BOTTOM_VALUE))
            {
                continue;
            }
            ((uint32_t*)&(BaseState))[JoyPad[i][0]] = lValue;
            if (lValue < (uint32_t)AXIS_TOP_VALUE)
            {
                bAxeDirection = AI_AXE_POSITIVE;
                foundJoyPad = i;
                break;
            }
            else if (lValue > (uint32_t)AXIS_BOTTOM_VALUE)
            {
                bAxeDirection = AI_AXE_NEGATIVE;
                foundJoyPad = i;
                break;
            }
        }
        else
        {
            if (lValue == BaseValue)
            {
                continue;
            }
            ((uint32_t*)&(BaseState))[JoyPad[i][0]] = lValue;
        }
        if (JoyPad[i][1] == BTNTYPE_JOYPOV)
        {
            AI_POV pov[] = { AI_POV_UP, AI_POV_DOWN, AI_POV_LEFT, AI_POV_RIGHT };
            for (size_t p = 0; p < (sizeof(pov) / sizeof(pov[0])); p++)
            {
                if (JoyPadPovPressed(pov[p], (int32_t)lValue) && !JoyPadPovPressed(pov[p], (int32_t)BaseValue))
                {
                    bAxeDirection = (uint8_t)pov[p];
                    foundJoyPad = i;
                    break;
                }
            }
            if (foundJoyPad >= 0)
            {
                break;
            }
        }
    }

    if (foundJoyPad >= 0)
    {
        pButton.Offset = (uint16_t)JoyPad[foundJoyPad][0];
        pButton.AxisID = bAxeDirection;
        pButton.BtnType = (BtnType)JoyPad[foundJoyPad][1];
        pButton.DeviceGuid = DeviceGuid;
        pButton.Device = nullptr;
        return SCAN_SUCCEED;
    }

    for (uint16_t i = 0, n = (uint16_t)(sizeof(JoyState.rgbButtons) / sizeof(JoyState.rgbButtons[0])); i < n; i++)
    {
        if (BaseState.rgbButtons[i] == JoyState.rgbButtons[i])
        {
            continue;
        }
        BaseState.rgbButtons[i] = JoyState.rgbButtons[i];

        if ((JoyState.rgbButtons[i] & 0x80) == 0)
        {
            continue;
        }
        pButton.Offset = i;
        pButton.AxisID = 0;
        pButton.BtnType = BTNTYPE_JOYBUTTON;
        pButton.DeviceGuid = DeviceGuid;
        pButton.Device = nullptr;
        return SCAN_SUCCEED;
    }
    return SCAN_FAILED;
}

CSdlInput::ScanResult CSdlInput::ScanMouse(const GUID& DeviceGuid, DIMOUSESTATE2_COMPAT& BaseState, BUTTON& pButton)
{
    (void)DeviceGuid;
    DIMOUSESTATE2_COMPAT MouseState = {};
    int x = 0, y = 0;
    SDL_GetRelativeMouseState(&x, &y);
    MouseState.lX = (LONG)(x * 10);
    MouseState.lY = (LONG)(y * 10);
    MouseState.lZ = 0;
    const Uint32 btn = SDL_GetMouseState(nullptr, nullptr);
    for (int b = 0; b < 32; ++b)
    {
        MouseState.rgbButtons[b] = (btn & (1u << b)) ? 0x80 : 0;
    }

    uint32_t Mouse[][2] =
    {
        { (uint32_t)(offsetof(DIMOUSESTATE2_COMPAT, lX) / sizeof(uint32_t)), BTNTYPE_MOUSEAXE },
        { (uint32_t)(offsetof(DIMOUSESTATE2_COMPAT, lY) / sizeof(uint32_t)), BTNTYPE_MOUSEAXE },
        { (uint32_t)(offsetof(DIMOUSESTATE2_COMPAT, lZ) / sizeof(uint32_t)), BTNTYPE_MOUSEAXE }
    };

    uint8_t bAxeDirection = 0;
    int32_t foundJoyPad = -1;

    for (int32_t i = 0, n = (int32_t)(sizeof(Mouse) / sizeof(Mouse[0])); i < n; i++)
    {
        uint32_t lValue = ((uint32_t*)&MouseState)[Mouse[i][0]];
        uint32_t BaseValue = ((uint32_t*)&BaseState)[Mouse[i][0]];

        if (Mouse[i][1] == BTNTYPE_MOUSEAXE)
        {
            if ((lValue < (uint32_t)AXIS_TOP_VALUE && BaseValue < (uint32_t)AXIS_TOP_VALUE) ||
                (lValue > (uint32_t)AXIS_BOTTOM_VALUE && BaseValue > (uint32_t)AXIS_BOTTOM_VALUE))
            {
                continue;
            }
            ((uint32_t*)&(BaseState))[Mouse[i][0]] = lValue;
            if (lValue < (uint32_t)AXIS_TOP_VALUE)
            {
                bAxeDirection = AI_AXE_POSITIVE;
                foundJoyPad = i;
                break;
            }
            else if (lValue > (uint32_t)AXIS_BOTTOM_VALUE)
            {
                bAxeDirection = AI_AXE_NEGATIVE;
                foundJoyPad = i;
                break;
            }
        }
        else
        {
            if (lValue == BaseValue)
            {
                continue;
            }
            ((uint32_t*)&(BaseState))[Mouse[i][0]] = lValue;
        }
    }

    if (foundJoyPad >= 0)
    {
        pButton.Offset = (uint16_t)Mouse[foundJoyPad][0];
        pButton.AxisID = bAxeDirection;
        pButton.BtnType = (BtnType)Mouse[foundJoyPad][1];
        pButton.DeviceGuid = m_GuidMouse;
        pButton.Device = nullptr;
        return SCAN_SUCCEED;
    }

    for (uint8_t i = 0, n = (uint8_t)(sizeof(MouseState.rgbButtons) / sizeof(MouseState.rgbButtons[0])); i < n; i++)
    {
        if (BaseState.rgbButtons[i] == MouseState.rgbButtons[i])
        {
            continue;
        }
        BaseState.rgbButtons[i] = MouseState.rgbButtons[i];

        if ((MouseState.rgbButtons[i] & 0x80) == 0)
        {
            continue;
        }
        pButton.Offset = i;
        pButton.AxisID = 0;
        pButton.BtnType = BTNTYPE_MOUSEBUTTON;
        pButton.DeviceGuid = m_GuidMouse;
        pButton.Device = nullptr;
        return SCAN_SUCCEED;
    }
    return SCAN_FAILED;
}

bool CSdlInput::JoyPadPovPressed(AI_POV Pov, int32_t Angle)
{
    enum { POV_ANGLE_THRESH = 5675 };
    if (LOWORD(Angle) == 0xFFFF)
    {
        return false;
    }
    switch (Pov)
    {
    case AI_POV_UP:
        return ((Angle >= 36000 - POV_ANGLE_THRESH) || (Angle <= 0 + POV_ANGLE_THRESH));
    case AI_POV_RIGHT:
        return ((Angle >= 9000 - POV_ANGLE_THRESH) && (Angle <= 9000 + POV_ANGLE_THRESH));
    case AI_POV_DOWN:
        return ((Angle >= 18000 - POV_ANGLE_THRESH) && (Angle <= 18000 + POV_ANGLE_THRESH));
    case AI_POV_LEFT:
        return ((Angle >= 27000 - POV_ANGLE_THRESH) && (Angle <= 27000 + POV_ANGLE_THRESH));
    }
    return false;
}
