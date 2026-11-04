#include "CProject64Input.h"
#include "LinuxScancodes.h"
#include "InputSettings.h"
#include "InputConfigUI.h"
#include <cstring>

CProject64Input * g_InputPlugin = nullptr;

CProject64Input::CProject64Input(HINSTANCE hinst) :
    m_hinst(hinst),
    m_Scanning(false),
    m_DisplayCtrlId(0),
    m_iFirstController(-1),
    m_MouseLock(false)
{
    memset(m_Controllers, 0, sizeof(m_Controllers));
}

CProject64Input::~CProject64Input()
{
}

// The devices are listed again and every button is mapped to its device again (the mappings point into the list:
// a device that is gone leaves them empty). The caller has m_CS.
void CProject64Input::RemapDevices(void)
{
    m_Input->DevicesChanged();
    for (uint32_t i = 0, n = sizeof(m_Controllers) / sizeof(m_Controllers[0]); i < n; i++)
    {
        m_Input->MapControllerDevice(m_Controllers[i]);
    }
    m_Input->MapControllerDevice(m_N64Mouse);
    m_Input->MapShortcutDevice(m_Shortcuts);
}

void CProject64Input::DevicesChanged(void)
{
    CGuard guard(m_CS);
    if (m_Input.get() != nullptr)
    {
        RemapDevices();
    }
}

void CProject64Input::DeviceAdded(void)
{
#ifdef _WIN32
    ConfigUIDeviceAdded();
#endif
}

void CProject64Input::InitiateControllers(CONTROL_INFO * ControlInfo)
{
    CGuard guard(m_CS);
    m_ControlInfo = *ControlInfo;
    if (m_Input.get() == nullptr)
    {
        m_Input.reset(new CSdlInput(m_hinst));
    }
    m_Input->Initiate(ControlInfo);
    m_iFirstController = -1;
    for (uint32_t i = 0, n = sizeof(m_Controllers) / sizeof(m_Controllers[0]); i < n; i++)
    {
        g_Settings->LoadController(i, m_ControlInfo.Controls[i], m_Controllers[i]);
        m_Input->MapControllerDevice(m_Controllers[i]);
        if (m_ControlInfo.Controls[i].Present != PRESENT_NONE && m_iFirstController < 0)
        {
            m_iFirstController = i;
        }
    }

    g_Settings->GetControllerMouse(m_N64Mouse);
    m_Input->MapControllerDevice(m_N64Mouse);

    g_Settings->LoadShortcuts(m_Shortcuts);
    m_Input->MapShortcutDevice(m_Shortcuts);
}

bool CProject64Input::IsButtonDown(BUTTON & Button, bool UpdateDevices)
{
    CGuard guard(m_CS);
    if (m_Input.get() == nullptr)
    {
        return false;
    }
    if (UpdateDevices)
    {
        m_Input->UpdateDeviceData();
    }
    return m_Input->IsButtonPressed(Button);
}

void CProject64Input::GetKeys(int32_t Control, BUTTONS * Keys)
{
    if (Control >= sizeof(m_Controllers) / sizeof(m_Controllers[0]))
    {
        return;
    }
    CGuard guard(m_CS);
    if (m_Input.get() == nullptr)
    {
        return; // not initiated yet (InitiateControllers is the first call of the emulator)
    }
    if (Control == m_iFirstController)
    {
        m_Input->UpdateDeviceData();
        if (m_Input->TakeHotPlug())
        {
            RemapDevices();
        }
        CheckShortcuts();
    }
    if (m_ControlInfo.Controls[Control].Present == PRESENT_MOUSE)
    {
        //Mouse
        if (m_MouseLock)
        {
            LockCursor();
            m_N64Mouse.Sensitivity = m_Controllers[Control].Sensitivity;
            Keys->R_DPAD = m_Input->IsButtonPressed(m_N64Mouse.R_DPAD);
            Keys->L_DPAD = m_Input->IsButtonPressed(m_N64Mouse.L_DPAD);
            Keys->D_DPAD = m_Input->IsButtonPressed(m_N64Mouse.D_DPAD);
            Keys->U_DPAD = m_Input->IsButtonPressed(m_N64Mouse.U_DPAD);
            Keys->START_BUTTON = m_Input->IsButtonPressed(m_N64Mouse.START_BUTTON);
            Keys->Z_TRIG = m_Input->IsButtonPressed(m_N64Mouse.Z_TRIG);
            Keys->B_BUTTON = m_Input->IsButtonPressed(m_N64Mouse.B_BUTTON);
            Keys->A_BUTTON = m_Input->IsButtonPressed(m_N64Mouse.A_BUTTON);
            Keys->R_CBUTTON = m_Input->IsButtonPressed(m_N64Mouse.R_CBUTTON);
            Keys->L_CBUTTON = m_Input->IsButtonPressed(m_N64Mouse.L_CBUTTON);
            Keys->D_CBUTTON = m_Input->IsButtonPressed(m_N64Mouse.D_CBUTTON);
            Keys->U_CBUTTON = m_Input->IsButtonPressed(m_N64Mouse.U_CBUTTON);
            Keys->R_TRIG = m_Input->IsButtonPressed(m_N64Mouse.R_TRIG);
            Keys->L_TRIG = m_Input->IsButtonPressed(m_N64Mouse.L_TRIG);
            m_Input->GetAxis(m_N64Mouse, Keys);
        }
    }
    else
    {
        //Controller
        N64CONTROLLER& Controller = m_Controllers[Control];
        Keys->R_DPAD = m_Input->IsButtonPressed(Controller.R_DPAD);
        Keys->L_DPAD = m_Input->IsButtonPressed(Controller.L_DPAD);
        Keys->D_DPAD = m_Input->IsButtonPressed(Controller.D_DPAD);
        Keys->U_DPAD = m_Input->IsButtonPressed(Controller.U_DPAD);
        Keys->START_BUTTON = m_Input->IsButtonPressed(Controller.START_BUTTON);
        Keys->Z_TRIG = m_Input->IsButtonPressed(Controller.Z_TRIG);
        Keys->B_BUTTON = m_Input->IsButtonPressed(Controller.B_BUTTON);
        Keys->A_BUTTON = m_Input->IsButtonPressed(Controller.A_BUTTON);
        Keys->R_CBUTTON = m_Input->IsButtonPressed(Controller.R_CBUTTON);
        Keys->L_CBUTTON = m_Input->IsButtonPressed(Controller.L_CBUTTON);
        Keys->D_CBUTTON = m_Input->IsButtonPressed(Controller.D_CBUTTON);
        Keys->U_CBUTTON = m_Input->IsButtonPressed(Controller.U_CBUTTON);
        Keys->R_TRIG = m_Input->IsButtonPressed(Controller.R_TRIG);
        Keys->L_TRIG = m_Input->IsButtonPressed(Controller.L_TRIG);
        m_Input->GetAxis(Controller, Keys);
    }
    
}

void CProject64Input::StartScanDevices(int32_t DisplayCtrlId)
{
    CGuard guard(m_CS);
    m_Scanning = true;
    m_DisplayCtrlId = DisplayCtrlId;
    if (m_Input.get() != nullptr)
    {
        m_Input->NotifyScanActive(true);
        m_Input->UpdateDeviceData();
    }
}

void CProject64Input::EndScanDevices(void)
{
    m_Scanning = false;
    m_DisplayCtrlId = 0;
    if (m_Input.get() != nullptr)
    {
        m_Input->NotifyScanActive(false);
    }
}

void CProject64Input::NotifyConfigDialogOpen(bool open)
{
    if (m_Input.get() != nullptr)
    {
        m_Input->NotifyConfigDialogOpen(open);
    }
}

void CProject64Input::NotifyRomOpen(bool open)
{
    if (m_Input.get() != nullptr)
    {
        m_Input->NotifyRomOpen(open);
    }
}

CSdlInput::ScanResult CProject64Input::ScanDevices(BUTTON & Button)
{
    CSdlInput::ScanResult Result = CSdlInput::SCAN_FAILED;
    if (m_Input.get() != nullptr)
    {
        // a controller plugged in (or out) while the dialog is open: GetKeys does not run when no game does
        CGuard guard(m_CS);
        m_Input->PollHotPlug();
        if (m_Input->TakeHotPlug())
        {
            RemapDevices();
        }
        Result = m_Input->ScanDevices(Button);
    }
    return Result;
}

bool CProject64Input::IsKeyAssigned(int32_t EvdevCode)
{
    if (EvdevCode < 0 || EvdevCode >= (int32_t)(sizeof(linux_scancode_table) / sizeof(linux_scancode_table[0])))
    {
        return false;
    }
    const uint32_t Scancode = (uint32_t)linux_scancode_table[EvdevCode];
    if (Scancode == 0)
    {
        return false;
    }
    CGuard guard(m_CS);
    for (N64CONTROLLER & Controller : m_Controllers)
    {
        BUTTON * Buttons[] = {&Controller.U_DPAD, &Controller.D_DPAD, &Controller.L_DPAD, &Controller.R_DPAD, &Controller.A_BUTTON, &Controller.B_BUTTON,
                              &Controller.U_CBUTTON, &Controller.D_CBUTTON, &Controller.L_CBUTTON, &Controller.R_CBUTTON, &Controller.START_BUTTON,
                              &Controller.Z_TRIG, &Controller.R_TRIG, &Controller.L_TRIG, &Controller.U_ANALOG, &Controller.D_ANALOG, &Controller.L_ANALOG,
                              &Controller.R_ANALOG};
        for (BUTTON * Button : Buttons)
        {
            if (Button->BtnType == BTNTYPE_KEYBUTTON && Button->Offset == Scancode)
            {
                return true;
            }
        }
    }
    return false;
}

const GUID & CProject64Input::KeyboardGuid() const
{
    return m_Input->KeyboardGuid();
}

const GUID & CProject64Input::MouseGuid() const
{
    return m_Input->MouseGuid();
}

std::wstring CProject64Input::ButtonAssignment(BUTTON & Button)
{
    if (m_Input.get() != nullptr)
    {
        return m_Input->ButtonAssignment(Button);
    }
    return L"Unknown";
}

std::wstring CProject64Input::ControllerDevices(const N64CONTROLLER & Controller)
{
    if (m_Input.get() != nullptr)
    {
        return m_Input->ControllerDevices(Controller);
    }
    return L"";
}

bool CProject64Input::SaveController(uint32_t ControlIndex)
{
    CGuard guard(m_CS);

    if (ControlIndex >= sizeof(m_Controllers) / sizeof(m_Controllers[0]))
    {
        return false;
    }
    g_Settings->SaveController(ControlIndex, m_ControlInfo.Controls[ControlIndex], m_Controllers[ControlIndex]);
    m_Input->MapControllerDevice(m_Controllers[ControlIndex]);
    return true;
}

bool CProject64Input::ResetController(uint32_t ControlIndex, CONTROL & ControlInfo, N64CONTROLLER & Controller)
{
    g_Settings->ResetController(ControlIndex, ControlInfo, Controller);
    m_Input->MapControllerDevice(Controller);
    return true;
}

void CProject64Input::CheckShortcuts()
{
    bool isPressed = m_Input->IsButtonPressed(m_Shortcuts.LOCKMOUSE);
    if ((isPressed == true) && (m_Shortcuts.LOCKMOUSE_PRESSED == false))
    {
        LockMouseSwitch();
    }
    m_Shortcuts.LOCKMOUSE_PRESSED = isPressed;
}

bool CProject64Input::SaveShortcuts()
{
    CGuard guard(m_CS);

    g_Settings->SaveShortcuts(m_Shortcuts);
    m_Input->MapShortcutDevice(m_Shortcuts);
    return true;
}

bool CProject64Input::ResetShortcuts(SHORTCUTS& Shortcuts)
{
    g_Settings->ResetShortcuts(Shortcuts);
    m_Input->MapShortcutDevice(Shortcuts);
    return true;
}

void CProject64Input::LockMouse()
{
    if (IsMouseUsed() == false) return UnlockMouse();
    if (m_MouseLock == true) return;
#ifdef _WIN32
    PostMessage((HWND)m_ControlInfo.hWnd, WM_HIDE_CUROSR, false, 0);
#else
    SDL_SetRelativeMouseMode(SDL_TRUE);
#endif
    m_MouseLock = true;
}

void CProject64Input::UnlockMouse()
{
    if (m_MouseLock == false) return;
#ifdef _WIN32
    PostMessage((HWND)m_ControlInfo.hWnd, WM_HIDE_CUROSR, true, 0);
#else
    SDL_SetRelativeMouseMode(SDL_FALSE);
#endif
    m_MouseLock = false;
}

void CProject64Input::LockMouseSwitch()
{
    if (m_MouseLock == true)
    {
        UnlockMouse();
    }
    else
    {
        LockMouse();
    }
}

bool CProject64Input::IsMouseUsed()
{
    for (uint32_t i = 0, n = sizeof(m_Controllers) / sizeof(m_Controllers[0]); i < n; i++)
    {
        if (m_ControlInfo.Controls[i].Present == PRESENT_MOUSE)
        {
            return true;
        }
    }
    return false;
}

void CProject64Input::LockCursor()
{
#ifdef _WIN32
    RECT rect;
    GetWindowRect((HWND)m_ControlInfo.hWnd, &rect);
    SetCursorPos((rect.left + rect.right) / 2, (rect.top + rect.bottom) / 2);
#endif
    // On Linux, LockMouse() already enabled SDL relative-mouse mode, which
    // confines/hides the cursor and reports deltas without needing to
    // re-center it each frame.
}
