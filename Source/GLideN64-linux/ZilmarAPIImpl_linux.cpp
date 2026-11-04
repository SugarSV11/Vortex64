// windows/ZilmarAPIImpl_windows.cpp for Linux. The about dialog of GLideNUI creates a
// QApplication of its own (the emulator of Windows has no Qt); the GUI of this port has one, so
// the dialog runs in it.
#include <QApplication>
#include <chrono>
#include <GLideNUI/AboutDialog.h>
#include "PluginAPI.h"
#include "GLideN64.h"
#include "GLideNUI/GLideNUI.h"
#include "Config.h"
#include <DisplayWindow.h>
#include <Revision.h>

void PluginAPI::DllAbout(void * /*_hParent*/)
{
	if (QCoreApplication::instance() != nullptr) {
		AboutDialog dialog(nullptr, Qt::WindowTitleHint | Qt::WindowSystemMenuHint | Qt::WindowCloseButtonHint);
		dialog.exec();
		return;
	}
	Config_LoadConfig();
	wchar_t strIniFolderPath[PLUGIN_PATH_SIZE];
	api().FindPluginPath(strIniFolderPath);
	RunAbout(strIniFolderPath);
}

void PluginAPI::CaptureScreen(const char * const _Directory)
{
	dwnd().setCaptureScreen(_Directory);
}

void PluginAPI::DllConfig(void * /*_hParent*/)
{
	Config_DoConfig(nullptr);
}

void PluginAPI::GetDllInfo(PLUGIN_INFO * PluginInfo)
{
	PluginInfo->Version = 0x105;
	PluginInfo->Type = PLUGIN_TYPE_VIDEO;
	sprintf(PluginInfo->Name, "%s", pluginNameWithRevision);
}

void PluginAPI::DrawStatus(const char * lpString, int32_t RightAlign)
{
	// the message at the left or right of the top for a short while, with the OSD position
	// restored afterwards
	int prevPos = config.onScreenDisplay.pos;
	config.onScreenDisplay.pos = RightAlign ? Config::posTopRight : Config::posTopLeft;
	dwnd().getDrawer().showMessage(lpString, std::chrono::milliseconds(2000));
	config.onScreenDisplay.pos = prevPos;
}
