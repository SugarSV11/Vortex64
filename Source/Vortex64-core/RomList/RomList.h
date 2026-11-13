#pragma once
#include <Common/Platform.h> // PJ64_HAVE_7ZIP decides the layout of CRomList
#include <Common/IniFile.h>
#include <Common/StdString.h>
#include <Common/Thread.h>
#include <Common/md5.h>
#include <Common/path.h>
#include <mutex>
#include <vector>
#include <string>
#include <Vortex64-core/N64System/N64Types.h>

class CRomList
{
public:
    enum FILE_FORMAT
    {
        Format_Uncompressed,
        Format_Zip,
        Format_7zip,
    };

    struct ROM_INFO
    {
        char szFullFileName[300];
        FILE_FORMAT FileFormat;
        char Status[60];
        char FileName[200];
        char InternalName[22];
        char GoodName[200];
        char Name[200];
        char CartID[3];
        char PluginNotes[250];
        char CoreNotes[250];
        char UserNotes[250];
        char Developer[30];
        char ReleaseDate[30];
        char Genre[15];
        int32_t Players;
        uint32_t TextColor;
        int32_t SelColor;
        uint32_t SelTextColor;
        int32_t RomSize;
        uint8_t Media;
        uint8_t Country;
        uint32_t CRC1;
        uint32_t CRC2;
        CICChip CicChip;
        char ForceFeedback[15];
    };

    CRomList();
    virtual ~CRomList();

    void RefreshRomList(void);
    void LoadRomList(void);
    // Further directories scanned besides RomList_GameDir (the Vortex64 library keeps several); set
    // before RefreshRomList(). The cache and its hash cover them as well.
    void SetExtraGameDirectories(const std::vector<std::string> & Directories);

protected:
    typedef std::vector<ROM_INFO> ROMINFO_LIST;

    virtual void RomListReset(void)
    {
    }
    virtual void RomAddedToList(int32_t /*ListPos*/)
    {
    }
    virtual void RomListLoaded(void)
    {
    }

    MD5 RomListHash(strlist & FileList);
    void AddFileNameToList(strlist & FileList, const stdstr & Directory, CPath & File);
    ROMINFO_LIST m_RomInfo;
    bool m_StopRefresh;
    std::vector<std::string> m_ExtraGameDirs;
    // (m_ExtraGameDirs is set by the GUI and read by the refresh thread and by the directory watch)
    mutable std::mutex m_ExtraLock;
    std::vector<std::string> ExtraGameDirectories() const;

private:
    void AddRomToList(const char * RomLocation);
    void FillRomList(strlist & FileList, const char * Directory, const CPath & Root);
    bool FillRomInfo(ROM_INFO * pRomInfo);
    void FillRomExtensionInfo(ROM_INFO * pRomInfo);
    bool LoadDataFromRomFile(const char * FileName, uint8_t * Data, int32_t DataLen, int32_t * RomSize, FILE_FORMAT & FileFormat);
    void SaveRomList(strlist & FileList);
    void RefreshRomListThread(void);

    static void RefreshSettings(CRomList *);
    static void NotificationCB(const char * Status, CRomList * _this);
    static void RefreshRomListStatic(CRomList * _this);
    static void ByteSwapRomData(uint8_t * Data, int32_t DataLen);

    CPath m_GameDir;
    CIniFile * m_NotesIniFile;
    CIniFile * m_ExtIniFile;
    CIniFile * m_RomIniFile;
#ifdef PJ64_HAVE_7ZIP
    CIniFile * m_ZipIniFile;
#endif
    CThread m_RefreshThread;
    CIniFileBase::SectionList m_GameIdentifiers;

#define DISKSIZE_MAME 0x0435B0C0
#define DISKSIZE_SDK 0x03DEC800
};
