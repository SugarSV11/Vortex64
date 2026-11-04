// Linux counterpart of discord_register_win.cpp. Registering the application with the desktop
// (a .desktop file and a URL scheme handler) is what lets a Discord invitation launch a game;
// Project64 only shows a presence and has no join or spectate, so nothing is registered.
#include "discord_register.h"

void Discord_Register(const char* /*applicationId*/, const char* /*command*/)
{
}

void Discord_RegisterSteamGame(const char* /*applicationId*/, const char* /*steamId*/)
{
}
