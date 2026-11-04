#include "stdafx.h"

#ifndef _WIN32
#include <Common/Util.h>
#endif
#include <Project64-core/N64System/Mips/MemoryVirtualMem.h>
#include <Project64-core/N64System/Mips/Register.h>
#include <Project64-core/N64System/N64Disk.h>
#include <Project64-core/N64System/N64Rom.h>
#include <Project64-core/N64System/N64System.h>
#include <Project64-core/N64System/SystemGlobals.h>
#include <Project64-core/Plugins/AudioPlugin.h>
#include <Project64-plugin-spec/Audio.h>
#ifdef _WIN32
#include <Windows.h>
#else
#include <pthread.h>
#endif

CAudioPlugin::CAudioPlugin() :
    AiLenChanged(nullptr),
    AiReadLength(nullptr),
    ProcessAList(nullptr),
#ifdef _WIN32
    m_hAudioThread(nullptr),
#else
    m_AudioThread(nullptr),
    m_StopAudioThread(false),
#endif
    AiUpdate(nullptr),
    AiDacrateChanged(nullptr)
{
}

CAudioPlugin::~CAudioPlugin()
{
#ifndef _WIN32
    StopAudioThread();
#endif
    Close(nullptr);
    UnloadPlugin();
}

#ifndef _WIN32
// Linux: the audio thread is a CThread that is asked to stop and joined
// (upstream Windows terminates it with TerminateThread)
void CAudioPlugin::StopAudioThread(void)
{
    if (m_AudioThread != nullptr)
    {
        WriteTrace(TraceAudioPlugin, TraceDebug, "Stop audio thread");
        m_StopAudioThread = true;
        m_AudioThread->Wait();
        delete m_AudioThread;
        m_AudioThread = nullptr;
    }
}

void CAudioPlugin::Close(RenderWindow * Render)
{
    StopAudioThread();
    CPlugin::Close(Render);
}
#endif

bool CAudioPlugin::LoadFunctions(void)
{
    g_Settings->SaveBool(Setting_SyncViaAudioEnabled, false);

    // Find entries for functions in DLL
    void(CALL * InitiateAudio)(void);
    LoadFunction(InitiateAudio);
    LoadFunction(AiDacrateChanged);
    LoadFunction(AiLenChanged);
    LoadFunction(AiReadLength);
    LoadFunction(AiUpdate);
    LoadFunction(ProcessAList);

    // Make sure DLL has all needed functions
    if (AiDacrateChanged == nullptr)
    {
        UnloadPlugin();
        return false;
    }
    if (AiLenChanged == nullptr)
    {
        UnloadPlugin();
        return false;
    }
    if (AiReadLength == nullptr)
    {
        UnloadPlugin();
        return false;
    }
    if (InitiateAudio == nullptr)
    {
        UnloadPlugin();
        return false;
    }
    if (ProcessAList == nullptr)
    {
        UnloadPlugin();
        return false;
    }

    if (m_PluginInfo.Version >= 0x0102)
    {
        if (PluginOpened == nullptr)
        {
            UnloadPlugin();
            return false;
        }
    }
    return true;
}

bool CAudioPlugin::Initiate(CN64System * System, RenderWindow * Window)
{
#ifndef _WIN32
    StopAudioThread();
#endif

    // Get function from DLL
    int32_t(CALL * InitiateAudio)(AUDIO_INFO Audio_Info);
    LoadFunction(InitiateAudio);
    if (InitiateAudio == nullptr)
    {
        return false;
    }

    AUDIO_INFO Info = {0};

#ifdef _WIN32
    Info.hWnd = Window ? Window->GetWindowHandle() : nullptr;
    Info.hinst = Window ? Window->GetModuleInstance() : nullptr;
#else
    Info.hWnd = nullptr;
    Info.hinst = nullptr;
#endif
    Info.Reserved = true;
    Info.CheckInterrupts = DummyCheckInterrupts;

    // We are initializing the plugin before any ROM is loaded so we do not have any correct
    // parameters here, just needed to we can config the DLL
    if (System == nullptr)
    {
        static uint8_t Buffer[100];
        static uint32_t Value = 0;

        Info.HEADER = Buffer;
        Info.RDRAM = Buffer;
        Info.DMEM = Buffer;
        Info.IMEM = Buffer;
        Info.MI_INTR_REG = &Value;
        Info.AI_DRAM_ADDR_REG = &Value;
        Info.AI_LEN_REG = &Value;
        Info.AI_CONTROL_REG = &Value;
        Info.AI_STATUS_REG = &Value;
        Info.AI_DACRATE_REG = &Value;
        Info.AI_BITRATE_REG = &Value;
    }
    // Send initialization information to the DLL
    else
    {
        CMipsMemoryVM & MMU = System->m_MMU_VM;
        CRegisters & Reg = System->m_Reg;

        if (g_Rom->IsLoadedRomDDIPL() && g_Disk != nullptr)
            Info.HEADER = g_Disk->GetDiskHeader();
        else
            Info.HEADER = g_Rom->GetRomAddress();
        Info.RDRAM = MMU.Rdram();
        Info.DMEM = MMU.Dmem();
        Info.IMEM = MMU.Imem();
        Info.MI_INTR_REG = &Reg.m_AudioIntrReg;
        Info.AI_DRAM_ADDR_REG = &Reg.AI_DRAM_ADDR_REG;
        Info.AI_LEN_REG = &Reg.AI_LEN_REG;
        Info.AI_CONTROL_REG = &Reg.AI_CONTROL_REG;
        Info.AI_STATUS_REG = &Reg.AI_STATUS_REG;
        Info.AI_DACRATE_REG = &Reg.AI_DACRATE_REG;
        Info.AI_BITRATE_REG = &Reg.AI_BITRATE_REG;
    }
    m_Initialized = InitiateAudio(Info) != 0;

    if (System != nullptr)
    {
        if (AiUpdate)
        {
#ifdef _WIN32
            if (m_hAudioThread)
            {
                WriteTrace(TraceAudioPlugin, TraceDebug, "Terminate Audio Thread");
                TerminateThread(m_hAudioThread, 0);
            }
            DWORD ThreadID;
            m_hAudioThread = CreateThread(nullptr, 0, (LPTHREAD_START_ROUTINE)AudioThread, (LPVOID)this, 0, &ThreadID);
#else
            m_StopAudioThread = false;
            m_AudioThread = new CThread((CThread::CTHREAD_START_ROUTINE)AudioThread);
            if (!m_AudioThread->Start(this))
            {
                delete m_AudioThread;
                m_AudioThread = nullptr;
            }
#endif
        }

        if (System->m_Reg.AI_DACRATE_REG != 0)
        {
            DacrateChanged(g_GameSettings.systemType);
        }
    }
    return m_Initialized;
}

void CAudioPlugin::UnloadPluginDetails(void)
{
#ifdef _WIN32
    if (m_hAudioThread)
    {
        WriteTrace(TraceAudioPlugin, TraceDebug, "Terminate audio thread");
        TerminateThread(m_hAudioThread, 0);
        m_hAudioThread = nullptr;
    }
#else
    StopAudioThread();
#endif
    AiDacrateChanged = nullptr;
    AiLenChanged = nullptr;
    AiReadLength = nullptr;
    AiUpdate = nullptr;
    ProcessAList = nullptr;
}

void CAudioPlugin::DacrateChanged(SYSTEM_TYPE Type)
{
    if (!Initialized())
    {
        return;
    }
    WriteTrace(TraceAudioPlugin, TraceDebug, "SystemType: %s", Type == SYSTEM_NTSC ? "SYSTEM_NTSC" : "SYSTEM_PAL");

    //uint32_t Frequency = g_Reg->AI_DACRATE_REG * 30;
    //uint32_t CountsPerSecond = (g_Reg->VI_V_SYNC_REG != 0 ? (g_Reg->VI_V_SYNC_REG + 1) * g_Settings->LoadDword(Game_ViRefreshRate) : 500000) * 60;
    AiDacrateChanged(Type);
}

#ifdef _WIN32
void CAudioPlugin::AudioThread(CAudioPlugin * _this)
{
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    if (g_Settings->LoadBool(Setting_CN64TimeCritical))
    {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
    }
    for (;;)
    {
        _this->AiUpdate(true);
    }
}
#else
void * CAudioPlugin::AudioThread(void * param)
{
    CAudioPlugin * _this = (CAudioPlugin *)param;
    if (_this == nullptr)
    {
        return nullptr;
    }

    struct sched_param schedParam;
    schedParam.sched_priority = sched_get_priority_max(SCHED_FIFO);
    pthread_setschedparam(pthread_self(), SCHED_FIFO, &schedParam);
    while (!_this->m_StopAudioThread)
    {
        // The SDL driver has no event to block on, so poll instead of Wait
        _this->AiUpdate(false);
        pjutil::Sleep(1);
    }
    return nullptr;
}
#endif
