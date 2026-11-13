#include "VirtualKeys.h"

#include <Qt>

// The Win32 accelerator table stores virtual key codes (Project64.sc3 keeps
// them), so they are translated to Qt keys where the shortcut is installed.
int QtKeyFromVirtualKey(int VirtualKey)
{
    if ((VirtualKey >= 'A' && VirtualKey <= 'Z') || (VirtualKey >= '0' && VirtualKey <= '9'))
    {
        return VirtualKey;
    }
    if (VirtualKey >= 0x70 && VirtualKey <= 0x87)
    {
        return Qt::Key_F1 + (VirtualKey - 0x70);
    }
    if (VirtualKey >= 0x60 && VirtualKey <= 0x69)
    {
        return Qt::Key_0 + (VirtualKey - 0x60);
    }
    switch (VirtualKey)
    {
    case 0x08: return Qt::Key_Backspace;
    case 0x09: return Qt::Key_Tab;
    case 0x0D: return Qt::Key_Return;
    case 0x13: return Qt::Key_Pause;
    case 0x1B: return Qt::Key_Escape;
    case 0x20: return Qt::Key_Space;
    case 0x21: return Qt::Key_PageUp;
    case 0x22: return Qt::Key_PageDown;
    case 0x23: return Qt::Key_End;
    case 0x24: return Qt::Key_Home;
    case 0x25: return Qt::Key_Left;
    case 0x26: return Qt::Key_Up;
    case 0x27: return Qt::Key_Right;
    case 0x28: return Qt::Key_Down;
    case 0x2C: return Qt::Key_Print;
    case 0x2D: return Qt::Key_Insert;
    case 0x2E: return Qt::Key_Delete;
    case 0x6A: return Qt::Key_Asterisk;
    case 0x6B: return Qt::Key_Plus;
    case 0x6D: return Qt::Key_Minus;
    case 0x6E: return Qt::Key_Period;
    case 0x6F: return Qt::Key_Slash;
    case 0x90: return Qt::Key_NumLock;
    case 0x91: return Qt::Key_ScrollLock;
    case 0xBA: return Qt::Key_Semicolon;
    case 0xBB: return Qt::Key_Equal; // VK_OEM_PLUS is the =/+ key
    case 0xBC: return Qt::Key_Comma;
    case 0xBD: return Qt::Key_Minus;
    case 0xBE: return Qt::Key_Period;
    case 0xBF: return Qt::Key_Slash;
    case 0xC0: return Qt::Key_QuoteLeft;
    case 0xDB: return Qt::Key_BracketLeft;
    case 0xDC: return Qt::Key_Backslash;
    case 0xDD: return Qt::Key_BracketRight;
    case 0xDE: return Qt::Key_Apostrophe;
    }
    return 0;
}

int VirtualKeyFromQtKey(int QtKey)
{
    // Every virtual key code that maps to a Qt key, the reverse lookup of the
    // function above (the first code wins for keys with several).
    for (int VirtualKey = 1; VirtualKey < 0x100; VirtualKey++)
    {
        if (QtKeyFromVirtualKey(VirtualKey) == QtKey)
        {
            return VirtualKey;
        }
    }
    return 0;
}
