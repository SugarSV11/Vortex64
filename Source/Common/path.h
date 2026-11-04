#pragma once
#include <stdint.h>
#include <string>

class CPath
{
    // Enums
public:
    enum DIR_CURRENT_DIRECTORY
    {
        CURRENT_DIRECTORY = 1
    };
    enum DIR_MODULE_DIRECTORY
    {
        MODULE_DIRECTORY = 2
    };
    enum DIR_MODULE_FILE
    {
        MODULE_FILE = 3
    };

    enum
    {
        FIND_ATTRIBUTE_ALLFILES = 0xFFFF, // Search include all files
        FIND_ATTRIBUTE_FILES = 0x0000,    // File can be read or written to without restriction
        FIND_ATTRIBUTE_SUBDIR = 0x0010,   // Subdirectories
    };

    // Attributes
private:
    std::string m_strPath;
    static void * m_hInst;
#ifdef _WIN32
    void * m_hFindFile;
#else
    void * m_OpenedDir;
    std::string m_FindWildcard;
#endif
    uint32_t m_dwFindFileAttributes;

public:
    // Methods

    // Construction / destruction
    CPath();
    CPath(const CPath & rPath);
    CPath(const char * lpszPath);
    CPath(const char * lpszPath, const char * NameExten);
    CPath(const std::string & strPath);
    CPath(const std::string & strPath, const char * NameExten);
    CPath(const std::string & strPath, const std::string & NameExten);

    CPath(DIR_CURRENT_DIRECTORY sdt, const char * NameExten = nullptr);
    CPath(DIR_MODULE_DIRECTORY sdt, const char * NameExten = nullptr);
    CPath(DIR_MODULE_FILE sdt);
    virtual ~CPath();

    // Operators
    CPath & operator=(const CPath & rPath);
    CPath & operator=(const char * lpszPath);
    CPath & operator=(const std::string & strPath);
    bool operator==(const CPath & rPath) const;
    bool operator!=(const CPath & rPath) const;
    operator const char *() const;
    operator const std::string &()
    {
        return m_strPath;
    }

    // Get path components
#ifdef _WIN32
    void GetDriveDirectory(std::string & rDriveDirectory) const;
    std::string GetDriveDirectory(void) const;
#endif
    void GetDirectory(std::string & rDirectory) const;
    std::string GetDirectory(void) const;
    void GetName(std::string & rName) const;
    std::string GetName(void) const;
    void GetNameExtension(std::string & rNameExtension) const;
    std::string GetNameExtension(void) const;
    void GetExtension(std::string & rExtension) const;
    std::string GetExtension(void) const;
    void GetLastDirectory(std::string & rDirectory) const;
    std::string GetLastDirectory(void) const;
    void GetFullyQualified(std::string & rFullyQualified) const;
#ifdef _WIN32
    void GetComponents(std::string * pDrive = nullptr, std::string * pDirectory = nullptr, std::string * pName = nullptr, std::string * pExtension = nullptr) const;
#else
    void GetComponents(std::string * pDirectory = nullptr, std::string * pName = nullptr, std::string * pExtension = nullptr) const;
#endif
    // Get other state
    bool IsEmpty() const
    {
        return m_strPath.empty();
    }
    bool IsRelative() const;

    // Set path components
#ifdef _WIN32
    void SetDrive(char chDrive);
    void SetDriveDirectory(const char * lpszDriveDirectory);
#endif
    void SetDirectory(const char * lpszDirectory, bool bEnsureAbsolute = false);
    void SetName(const char * lpszName);
    void SetName(int iName);
    void SetNameExtension(const char * lpszNameExtension);
    void SetExtension(const char * lpszExtension);
    void SetExtension(int iExtension);
    void AppendDirectory(const char * lpszSubDirectory);
    void UpDirectory(std::string * pLastDirectory = nullptr);
#ifdef _WIN32
    void SetComponents(const char * lpszDrive, const char * lpszDirectory, const char * lpszName, const char * lpszExtension);
#else
    void SetComponents(const char * lpszDirectory, const char * lpszName, const char * lpszExtension);
#endif
    // Set whole path
    void Empty()
    {
        m_strPath.erase();
    }
    void CurrentDirectory();
    void Module();
    void Module(void * hInstance);
    void ModuleDirectory();
    void ModuleDirectory(void * hInstance);

    // Directory information
    bool IsDirectory() const;
    bool DirectoryExists() const;

    // File information
    bool IsFile() const
    {
        return !IsDirectory();
    }
    bool Exists() const;
#ifdef _WIN32
    bool SelectFile(void * hwndOwner, const char * InitialDir, const char * FileFilter, bool FileMustExist);
#endif

    // Directory operations
    bool DirectoryCreate(bool bCreateIntermediates = true);
    bool ChangeDirectory();
    CPath & NormalizePath(CPath BaseDir);

    // File operations
    bool Delete(bool bEvenIfReadOnly = true) const;
    bool CopyTo(const char * lpcszTargetFile, bool bOverwrite = true);
    bool MoveTo(const char * lpcszTargetFile, bool bOverwrite = true);

    // Finders
    bool FindFirst(uint32_t dwAttributes = 0);
    bool FindNext();

    // Helpers
    static void SethInst(void * hInst);
    static void * GethInst();

    // XDG Base Directory Specification (freedesktop.org) helpers, used on
    // Linux so user config/data (settings, saves, screenshots, textures,
    // logs) live under ~/.config and ~/.local/share instead of next to the
    // installed binary - unlike Directory_Plugin, which stays relative to
    // the executable on purpose (plugins are shipped binary artifacts, not
    // user data). Meaningless on Windows and never called there; harmless
    // either way since they only read environment variables.
    static std::string XdgConfigHome();
    static std::string XdgDataHome();

    // The folders of the user of Vortex64: $XDG_CONFIG_HOME/vortex64 and $XDG_DATA_HOME/vortex64. The product before it was called Project64
    // and kept them in .../project64 (the Legacy ones). A new installation only has its own; an existing user keeps the old folders untouched
    // and MigrateLegacyUserData() copies what is in them (once, never over a file that already exists in the new folder).
    static std::string UserConfigDir();
    static std::string UserDataDir();
    static std::string LegacyConfigDir();
    static std::string LegacyDataDir();
    // Copies the settings, shortcuts, saves, states, screenshots, cheats, scripts and the settings of the plugins from the Project64 folders
    // to the Vortex64 ones. Nothing of the old folders is changed or removed. The textures (they can be very large) are not copied: the
    // new folder gets a link to the old one when it has none. Marks the work with ".migrated-from-project64" in the new folders, so it is
    // done once; a copy that fails is tried again at the next start (it resumes: what exists is skipped). Returns the number of files copied;
    // Report (optional) says what was done.
    static int MigrateLegacyUserData(std::string * Report = nullptr);

private:
    // Setup and cleanup
    inline void Init();
    inline void Exit();

    bool AttributesMatch(uint32_t dwTargetAttributes, uint32_t dwFileAttributes);

    void cleanPathString(std::string & rDirectory) const;
    void StripLeadingChar(std::string & rText, char chLeading) const;
    void StripLeadingBackslash(std::string & Directory) const;
    void StripTrailingChar(std::string & rText, char chTrailing) const;
    void StripTrailingBackslash(std::string & rDirectory) const;
    void EnsureTrailingBackslash(std::string & Directory) const;
    void EnsureLeadingBackslash(std::string & Directory) const;
#ifndef _WIN32
    bool wildcmp(const char * wild, const char * string);
#endif
};
