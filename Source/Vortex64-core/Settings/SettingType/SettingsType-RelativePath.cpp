#include "stdafx.h"

#include "SettingsType-RelativePath.h"

CSettingTypeRelativePath::CSettingTypeRelativePath(const char * Directory, const char * FileName) :
    m_Directory(Directory),
    m_FileName(FileName)
{
    BuildPath();
    g_Settings->RegisterChangeCB(Cmd_BaseDirectory, this, RefreshSettings);
}

CSettingTypeRelativePath::~CSettingTypeRelativePath(void)
{
    g_Settings->UnregisterChangeCB(Cmd_BaseDirectory, this, RefreshSettings);
}

bool CSettingTypeRelativePath::Load(uint32_t /*Index*/, std::string & value) const
{
    value = m_FullPath;
    return true;
}

// Return the default values
void CSettingTypeRelativePath::LoadDefault(uint32_t /*Index*/, bool & /*Value*/) const
{
    g_Notify->BreakPoint(__FILE__, __LINE__);
}

void CSettingTypeRelativePath::LoadDefault(uint32_t /*Index*/, uint32_t & /*Value*/) const
{
    g_Notify->BreakPoint(__FILE__, __LINE__);
}

void CSettingTypeRelativePath::LoadDefault(uint32_t /*Index*/, std::string & /*Value*/) const
{
    g_Notify->BreakPoint(__FILE__, __LINE__);
}

void CSettingTypeRelativePath::Save(uint32_t /*Index*/, bool /*Value*/)
{
    g_Notify->BreakPoint(__FILE__, __LINE__);
}

void CSettingTypeRelativePath::Save(uint32_t /*Index*/, uint32_t /*Value*/)
{
    g_Notify->BreakPoint(__FILE__, __LINE__);
}

void CSettingTypeRelativePath::Save(uint32_t /*Index*/, const std::string & Value)
{
    m_Directory = "";
    m_FileName = Value;
    BuildPath();
}

void CSettingTypeRelativePath::Save(uint32_t /*Index*/, const char * Value)
{
    m_Directory = "";
    m_FileName = Value;
    BuildPath();
}

void CSettingTypeRelativePath::Delete(uint32_t /*Index*/)
{
    g_Notify->BreakPoint(__FILE__, __LINE__);
}

void CSettingTypeRelativePath::BuildPath(void)
{
    // XDG Base Directory Specification: pure user data (saves, screenshots,
    // custom textures, logs) lives under ~/.local/share, not next to the
    // installed binary. "Config" is handled per-file rather than by
    // directory name: that same bucket also holds Project64.rdb/Video.rdb/
    // Audio.rdb - the read-only ucode-CRC/game-compat databases the
    // top-level CMakeLists.txt's copy-config-rdb step ships relative to the
    // binary (see LINUX_PORT.md; getting this wrong is exactly what made no
    // ROM's microcode ever get recognized before that fix) - so only
    // the user-writable files (Project64.cfg, the keyboard shortcuts
    // Project64.sc3 and the ROM list caches) redirect to $XDG_CONFIG_HOME; the .rdb files fall through to
    // the relative-to-binary path below unchanged. "Plugin"/"SyncPlugin"
    // also deliberately stay relative to Cmd_BaseDirectory: they're shipped
    // binary artifacts (.so files), not user data, same reasoning as the
    // RDBs.
    if (m_Directory == "Save" || m_Directory == "Screenshots" || m_Directory == "Textures" || m_Directory == "Logs")
    {
        CPath FullPath(CPath::XdgDataHome().c_str(), "");
        FullPath.AppendDirectory("vortex64");
        FullPath.AppendDirectory(m_Directory.c_str());
        FullPath.SetNameExtension(m_FileName.c_str());
        m_FullPath = (const char *)FullPath;
        return;
    }
    if (m_Directory == "Config\\Cheats-User" || m_Directory == "Config\\Enhancements-User")
    {
        // Codes the user writes in the cheat/enhancement dialogs: user data
        CPath FullPath(CPath::XdgDataHome().c_str(), "");
        FullPath.AppendDirectory("vortex64");
        FullPath.AppendDirectory(m_Directory.c_str() + strlen("Config\\"));
        FullPath.SetNameExtension(m_FileName.c_str());
        m_FullPath = (const char *)FullPath;
        return;
    }
    if (m_Directory == "Config" && (m_FileName == "Project64.cfg" || m_FileName == "Project64.sc3" || m_FileName == "Project64.cache3" || m_FileName == "Project64.zcache"))
    {
        CPath FullPath(CPath::XdgConfigHome().c_str(), "");
        FullPath.AppendDirectory("vortex64");
        FullPath.SetNameExtension(m_FileName.c_str());
        m_FullPath = (const char *)FullPath;
        return;
    }
    CPath FullPath(g_Settings->LoadStringVal(Cmd_BaseDirectory).c_str(), "");
    FullPath.AppendDirectory(m_Directory.c_str());
    FullPath.SetNameExtension(m_FileName.c_str());
    m_FullPath = (const char *)FullPath;
}

void CSettingTypeRelativePath::RefreshSettings(void * _this)
{
    ((CSettingTypeRelativePath *)_this)->BuildPath();
}
