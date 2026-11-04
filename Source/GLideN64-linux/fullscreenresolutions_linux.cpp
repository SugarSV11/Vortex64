// GLideNUI/fullscreenresolutions_windows.cpp for Linux: no modes (the full screen is the frontend's); the screens for the OSD
#include <QGuiApplication>
#include <QScreen>
#include <algorithm>
#include <vector>
#include "FullscreenResolutions.h"
#include "../Config.h"

// The full screen is the frontend's (see CMakeLists.txt): no list of modes. The dialog (M64P_GLIDENUI) hides the
// choice when both lists are empty and keeps the stored full screen size untouched.
void fillFullscreenResolutionsList(QStringList &, int & _resolutionIdx, QStringList &, int & _rateIdx)
{
	_resolutionIdx = 0;
	_rateIdx = 0;
}

void fillFullscreenRefreshRateList(int, QStringList &, int & _rateIdx)
{
	_rateIdx = 0;
}

void getFullscreenResolutions(int, unsigned int &, unsigned int &)
{
}

void getFullscreenRefreshRate(int, unsigned int &)
{
}

std::vector<DisplayInfo> getDisplayInfo()
{
	std::vector<DisplayInfo> displays;
	for (QScreen * screen : QGuiApplication::screens()) {
		DisplayInfo info;
		info.m_displayName = screen->name();
		info.m_deviceName = screen->name();
		info.m_leftBound = screen->geometry().left();
		displays.push_back(info);
	}
	return displays;
}
