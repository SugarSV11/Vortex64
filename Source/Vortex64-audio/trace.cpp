#include "trace.h"
#include <Settings/Settings.h>
#include <Common/path.h>
#include <Common/Log.h>
#include "AudioSettings.h"

static CTraceFileLog * g_LogFile = nullptr;

void SetupTrace(void)
{
    if (g_LogFile != nullptr)
    {
        return;
    }

#ifdef _DEBUG
    TraceSetMaxModule(MaxTraceModulePluginAudio, TraceInfo);
#else
    TraceSetMaxModule(MaxTraceModulePluginAudio, TraceError);
#endif
    TraceSetModuleName(TraceAudioInitShutdown, "AudioInitShutdown");
    TraceSetModuleName(TraceAudioInterface, "AudioInterface");
    TraceSetModuleName(TraceAudioDriver, "AudioDriver");
}

void StartTrace(void)
{
    const char * log_dir = g_settings ? g_settings->log_dir() : nullptr;
    if (log_dir == nullptr || log_dir[0] == '\0')
    {
        return;
    }

    CPath LogFilePath(log_dir, "Vortex64-audio.log");
    if (!LogFilePath.DirectoryExists())
    {
        LogFilePath.DirectoryCreate();
    }
    g_LogFile = new CTraceFileLog(LogFilePath, g_settings->FlushLogs(), CLog::Log_New, 500);
    TraceAddModule(g_LogFile);
}

void StopTrace(void)
{
    if (g_LogFile)
    {
        TraceRemoveModule(g_LogFile);
        delete g_LogFile;
        g_LogFile = nullptr;
    }
}
