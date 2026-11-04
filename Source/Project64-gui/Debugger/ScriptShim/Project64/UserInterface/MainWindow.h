#pragma once
// The script API (ScriptAPI_pj64.cpp) sends these to the main window; upstream's
// Project64/UserInterface/MainWindow.h is the WTL window
enum
{
    WM_JSAPI_ACTION = 0x0400 + 41
};

enum
{
    JSAPI_ACT_OPEN_ROM,
    JSAPI_ACT_CLOSE_ROM,
    JSAPI_ACT_RESET,
    JSAPI_ACT_PAUSE,
    JSAPI_ACT_RESUME
};
