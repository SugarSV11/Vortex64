#pragma once

#include <QString>
#include <QStringList>

namespace Vortex
{
// The plugins of Vortex64 (the video, audio, input and RSP plugins, their configuration windows and GLideN64) are built together with
// the application and have to be used together: the input plugin and the application agree on what the window sends to it, the video
// plugin on the memory of the game. The settings can point the plugin directory to another folder (the setting "Use Selected" of the
// Plugin Directory stays in the file of settings for ever), for example to the build of an older version, and then the application
// runs with plugins that do not have what it expects and nothing says so. This lists the plugins of the selected folder that are not the
// ones that come with the application (same file name, other content); plugins that exist only in one of the folders (third-party ones)
// are not compared.
QStringList PluginsDifferentFromOwn(const QString & SelectedDirectory, const QString & OwnDirectory);
} // namespace Vortex
