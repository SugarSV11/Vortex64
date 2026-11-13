#pragma once

#include <QString>

class QWidget;

// Qt counterpart of RomInformation (Project64/UserInterface/RomInformation.cpp):
// loads the image (or uses the running system's) and shows the same fields with
// the same conversions in a dialog.
//
//   ShowRomInformation(parent, path)   loads the file like RomInformation(const char*)
//   ShowRunningRomInformation(parent)  uses g_Rom / g_Disk like RomInformation(CN64Rom*)
bool ShowRomInformation(QWidget * Parent, const QString & RomFile);
bool ShowRunningRomInformation(QWidget * Parent);
