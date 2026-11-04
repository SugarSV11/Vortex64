// windows/CommonAPIImpl_windows.cpp for Linux: the emulator gives the plugin an X11 window
// (GFX_INFO::hWnd) that the display window embeds its OpenGL context in, the paths follow the XDG
// base directories like the rest of the port (<config>/vortex64/GLideN64, ...)
#include <dlfcn.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <string>
#include <wchar.h>
#include <Graphics/OpenGLContext/windows/WindowsWGL.h>
#include <windows/GLideN64_windows.h>
#include "PluginAPI.h"
#include "RSP.h"

namespace {
void makeDirs(const std::string & _path)
{
	for (size_t pos = 1; pos != std::string::npos; pos = _path.find('/', pos + 1))
		mkdir(_path.substr(0, pos).c_str(), 0755);
	mkdir(_path.c_str(), 0755);
}

std::string xdg(const char * _variable, const char * _fallback)
{
	const char * dir = getenv(_variable);
	if (dir != nullptr && dir[0] == '/')
		return dir;
	const char * home = getenv("HOME");
	return std::string(home != nullptr ? home : "") + _fallback;
}

void toWide(const std::string & _s, wchar_t * _dest)
{
	const size_t n = mbstowcs(_dest, _s.c_str(), PLUGIN_PATH_SIZE - 1);
	_dest[n == (size_t)-1 ? 0 : n] = L'\0';
}

std::string pluginDir()
{
	Dl_info info;
	if (dladdr((void *)&pluginDir, &info) != 0 && info.dli_fname != nullptr) {
		std::string path = info.dli_fname;
		const size_t slash = path.find_last_of('/');
		if (slash != std::string::npos)
			return path.substr(0, slash);
	}
	return ".";
}

bool fileExists(const std::string & _path)
{
	struct stat st;
	return stat(_path.c_str(), &st) == 0;
}

void userPath(const std::string & _base, wchar_t * _strPath)
{
	if (_strPath == nullptr)
		return;
	const std::string dir = _base + "/vortex64/GLideN64";
	makeDirs(dir);
	toWide(dir, _strPath);
}
}

void setRenderWindow(void * _native); // DisplayWindow_linux.cpp

int PluginAPI::InitiateGFX(const GFX_INFO & _gfxInfo)
{
	_initiateGFX(_gfxInfo);

	WindowsWGL::setNativeWindow(_gfxInfo.hWnd);
	setRenderWindow(_gfxInfo.hWnd);
	hWnd = (HWND)(uintptr_t)_gfxInfo.hWnd;
	hStatusBar = (HWND)(uintptr_t)_gfxInfo.hStatusBar;
	hToolBar = 0;
	return TRUE;
}

// where the plugin is installed: the shared files (GLideN64.custom.ini, translations) are there
// or in ../share/vortex64/GLideN64 of an installation
void PluginAPI::FindPluginPath(wchar_t * _strPath)
{
	if (_strPath == nullptr)
		return;
	std::string dir = pluginDir();
	if (!fileExists(dir + "/GLideN64.custom.ini")) {
		// (share/vortex64 of an installation of Vortex64; share/project64 of an older one)
		for (const char * product : {"vortex64", "project64"}) {
			const std::string shared = dir + "/../share/" + product + "/GLideN64";
			if (fileExists(shared + "/GLideN64.custom.ini")) {
				dir = shared;
				break;
			}
		}
	}
	toWide(dir, _strPath);
}

// the settings (GLideN64.ini) and the custom textures of the user
void PluginAPI::GetUserDataPath(wchar_t * _strPath)
{
	userPath(xdg("XDG_DATA_HOME", "/.local/share"), _strPath);
}

// GLideN64.ini of the user (GetUserConfigPath of the Mupen64Plus build)
void GetUserConfigPath(wchar_t * _strPath)
{
	userPath(xdg("XDG_CONFIG_HOME", "/.config"), _strPath);
}

void PluginAPI::GetUserCachePath(wchar_t * _strPath)
{
	userPath(xdg("XDG_CACHE_HOME", "/.cache"), _strPath);
}
