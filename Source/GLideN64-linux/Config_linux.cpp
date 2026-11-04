// GLideNUI/Config_GLideNUI.cpp for Linux. The same (open the dialog, load the settings and the
// settings of the game), plus the OSD font: the default of the plugin is /usr/share/fonts/truetype/
// freefont/FreeSans.ttf, a Debian path, so the font is looked for in the font directories of the
// system and its full path is used (TextDrawer takes a name that is an existing path as it is).
#include <dirent.h>
#include <string.h>
#include <string>
#include <sys/stat.h>
#include <vector>

#include "N64.h"
#include "Config.h"
#include "RSP.h"
#include "PluginAPI.h"
#include "GLideNUI/GLideNUI.h"
#include <DisplayWindow.h>
#include <QApplication>
#include <QCheckBox>
#include <QEvent>
#include <QObject>


void GetUserConfigPath(wchar_t * _strPath); // CommonAPIImpl_linux.cpp

Config config;

namespace {
bool findFont(const std::string & _dir, const std::string & _name, int _depth, std::string & _found)
{
	DIR * dir = opendir(_dir.c_str());
	if (dir == nullptr)
		return false;
	bool found = false;
	while (!found) {
		const dirent * entry = readdir(dir);
		if (entry == nullptr)
			break;
		if (entry->d_name[0] == '.')
			continue;
		const std::string path = _dir + "/" + entry->d_name;
		struct stat st;
		if (stat(path.c_str(), &st) != 0)
			continue;
		if (S_ISDIR(st.st_mode)) {
			if (_depth > 0)
				found = findFont(path, _name, _depth - 1, _found);
		} else if (_name == entry->d_name) {
			_found = path;
			found = true;
		}
	}
	closedir(dir);
	return found;
}

// the font of the OSD: the one named in the settings if it is installed, else a common one
void resolveFont()
{
	static std::string resolved;
	static std::string requested;
	if (!resolved.empty() && requested == config.font.name) {
		config.font.name = resolved;
		return;
	}
	struct stat st;
	if (stat(config.font.name.c_str(), &st) == 0)
		return; // a full path
	requested = config.font.name;
	std::vector<std::string> names = { config.font.name, "FreeSans.ttf", "DejaVuSans.ttf",
		"LiberationSans-Regular.ttf", "NotoSans-Regular.ttf", "Cantarell-Regular.otf", "Ubuntu-R.ttf" };
	const char * roots[] = { "/usr/share/fonts", "/usr/local/share/fonts", "/usr/share/X11/fonts" };
	for (const auto & name : names)
		for (const char * root : roots) {
			std::string path;
			if (findFont(root, name, 4, path)) {
				resolved = path;
				config.font.name = path;
				return;
			}
		}
}
}

namespace {
// Vortex64 decides the synchronization of the presentation (Settings > Graphics: Automatic, On, Off): the box "Vertical sync" of this
// dialog would do nothing, so while the dialog is open it is shown disabled (the dialog is the upstream's, it is not edited)
class ManagedVSyncFilter : public QObject
{
public:
	bool eventFilter(QObject * _obj, QEvent * _event) override
	{
		if (_event->type() == QEvent::Show || _event->type() == QEvent::Polish) {
			QCheckBox * box = qobject_cast<QCheckBox *>(_obj);
			if (box != nullptr && box->objectName() == QLatin1String("vSyncCheckBox") && box->isEnabled()) {
				box->setEnabled(false);
				box->setToolTip(QString::fromUtf8("Vortex64: Ajustes \u2192 Gr\u00e1ficos / Settings \u2192 Graphics"));
			}
		}
		return false;
	}
};
}

void Config_DoConfig(void* parent)
{
	if (ConfigOpen)
		return;

	wchar_t strIniFolderPath[PLUGIN_PATH_SIZE];
	wchar_t strSharedIniFolderPath[PLUGIN_PATH_SIZE];

#ifdef M64P_GLIDENUI
	api().GetSharedDataPath(strSharedIniFolderPath);
	api().GetUserConfigPath(strIniFolderPath);
#else
	api().FindPluginPath(strSharedIniFolderPath);
	GetUserConfigPath(strIniFolderPath);
#endif // M64P_GLIDENUI

	ConfigOpen = true;
	const u32 maxMsaa = dwnd().maxMSAALevel();
	const u32 maxAnisotropy = dwnd().maxAnisotropy();
	ManagedVSyncFilter vsyncFilter;
	const bool managed = qEnvironmentVariableIsSet("VORTEX_VSYNC_MANAGED") && QCoreApplication::instance() != nullptr;
	if (managed)
		QCoreApplication::instance()->installEventFilter(&vsyncFilter);
	const bool bRestart = RunConfig(parent, strIniFolderPath, strSharedIniFolderPath, api().isRomOpen() ? RSP.romname : nullptr, maxMsaa, maxAnisotropy);
	if (managed)
		QCoreApplication::instance()->removeEventFilter(&vsyncFilter);
	if (config.generalEmulation.enableCustomSettings != 0)
		LoadCustomRomSettings(strIniFolderPath, strSharedIniFolderPath, RSP.romname);
	config.validate();
	if (bRestart)
		dwnd().restart();
	ConfigOpen = false;
}

void Config_LoadConfig()
{
	wchar_t strIniFolderPath[PLUGIN_PATH_SIZE];
	wchar_t strSharedIniFolderPath[PLUGIN_PATH_SIZE];

#ifdef M64P_GLIDENUI
	api().GetSharedDataPath(strSharedIniFolderPath);
	api().GetUserConfigPath(strIniFolderPath);
#else
	api().FindPluginPath(strSharedIniFolderPath);
	GetUserConfigPath(strIniFolderPath);
#endif // M64P_GLIDENUI

	LoadConfig(strIniFolderPath, strSharedIniFolderPath);
	if (config.generalEmulation.enableCustomSettings != 0)
		LoadCustomRomSettings(strIniFolderPath, strSharedIniFolderPath, RSP.romname);
	resolveFont();
	config.validate();
}
