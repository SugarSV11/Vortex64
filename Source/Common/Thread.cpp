#include "Thread.h"
#include "Trace.h"
#include "TraceModulesCommon.h"
#ifdef _WIN32
#include <Windows.h>
#else
#include <cxxabi.h>
#include <pthread.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

CThread::CThread(CTHREAD_START_ROUTINE lpStartAddress) :
    m_StartAddress(lpStartAddress),
    m_lpThreadParameter(nullptr),
    m_thread(nullptr),
#ifndef _WIN32
    m_running(false),
#endif
    m_threadID(0)
{
    WriteTrace(TraceThread, TraceDebug, "Start");
    WriteTrace(TraceThread, TraceDebug, "Done");
}

CThread::~CThread()
{
    WriteTrace(TraceThread, TraceDebug, "Start");
    if (CThread::GetCurrentThreadId() == m_threadID)
    {
        WriteTrace(TraceThread, TraceError, "Deleting from thread!");
    }
    if (CThread::GetCurrentThreadId() != m_threadID && isRunning())
    {
        Terminate();
    }
#ifdef _WIN32
    CloseHandle(m_thread);
#else
    // Joins a thread that already finished (Terminate() joined a running one)
    Wait();
#endif
    WriteTrace(TraceThread, TraceDebug, "Done");
}

bool CThread::Start(void * lpThreadParameter)
{
    WriteTrace(TraceThread, TraceDebug, "Start");
    m_lpThreadParameter = lpThreadParameter;
#ifdef _WIN32
    m_thread = CreateThread(nullptr, 0, (LPTHREAD_START_ROUTINE)ThreadWrapper, this, 0, (LPDWORD)&m_threadID);
#else
    if (m_thread != nullptr)
    {
        if (isRunning())
        {
            WriteTrace(TraceThread, TraceError, "Thread is already running");
            return false;
        }
        Wait();
    }
    pthread_t * thread_id = new pthread_t;

    m_thread = (void *)thread_id;
    m_running = true;

    int res = pthread_create(thread_id, nullptr, (void * (*)(void *))ThreadWrapper, this);
    if (res != 0)
    {
        m_running = false;
        m_thread = nullptr;
        delete thread_id;
        WriteTrace(TraceThread, TraceError, "pthread_create failed: %d", res);
        return false;
    }
#endif
    WriteTrace(TraceThread, TraceDebug, "Done");
    return true;
}

void * CThread::ThreadWrapper(CThread * _this)
{
    WriteTrace(TraceThread, TraceDebug, "Start");
    _this->m_threadID = CThread::GetCurrentThreadId();
#ifndef _WIN32
    _this->m_running = true;
    WriteTrace(TraceThread, TraceDebug, "Thread is running");
#endif
    void * res = nullptr;
    try
    {
#if defined(__i386__) || defined(_M_IX86) || defined(__arm__) || defined(_M_ARM)
        res = (void *)_this->m_StartAddress(_this->m_lpThreadParameter);
#else
        res = (void *)((uint64_t)_this->m_StartAddress(_this->m_lpThreadParameter));
#endif
    }
#ifndef _WIN32
    catch (const __cxxabiv1::__forced_unwind &)
    {
        // pthread_cancel (Terminate) and pthread_exit (a fatal error of the thread) must be able
        // to unwind the thread; it is no longer running once it does
        _this->m_running = false;
        throw;
    }
#endif
    catch (...)
    {
        //WriteTrace(TraceUserInterface, TraceError, "Unhandled Exception ");
    }
#ifndef _WIN32
    _this->m_running = false;
    WriteTrace(TraceThread, TraceDebug, "Thread is finished");
#endif
#ifdef _WIN32
    CloseHandle(_this->m_thread);
    _this->m_thread = nullptr;
#endif
    // Linux: the pthread_t is released by Wait() after pthread_join()
    WriteTrace(TraceThread, TraceDebug, "Done");
    return res;
}

bool CThread::isRunning(void) const
{
    WriteTrace(TraceThread, TraceDebug, "Start");
    if (m_thread == nullptr)
    {
        WriteTrace(TraceThread, TraceDebug, "Done (res: false), m_thread is null");
        return false;
    }
#ifndef _WIN32
    WriteTrace(TraceThread, TraceDebug, "Done (res: %s)", m_running ? "true" : "false");
    return m_running;
#endif
#ifdef _WIN32
    DWORD ExitCode;
    if (GetExitCodeThread(m_thread, &ExitCode))
    {
        if (ExitCode == STILL_ACTIVE)
        {
            WriteTrace(TraceThread, TraceDebug, "Done (res: true)");
            return true;
        }
    }
    WriteTrace(TraceThread, TraceDebug, "Done (res: false)");
    return false;
#else
    return true;
#endif
}

void CThread::Terminate(void)
{
    WriteTrace(TraceThread, TraceDebug, "Start");
    if (isRunning())
    {
#ifdef _WIN32
        WriteTrace(TraceThread, TraceDebug, "Terminating thread");
        TerminateThread(m_thread, 0);
#else
        if (CThread::GetCurrentThreadId() != m_threadID)
        {
            WriteTrace(TraceThread, TraceDebug, "Cancelling thread");
            pthread_cancel(*(pthread_t *)m_thread);
            Wait();
        }
#endif
    }
    WriteTrace(TraceThread, TraceDebug, "Done");
}

#ifndef _WIN32
// Not in upstream: waits for the thread to finish (pthread_join) and releases it
void CThread::Wait(void)
{
    WriteTrace(TraceThread, TraceDebug, "Start");
    if (m_thread == nullptr || CThread::GetCurrentThreadId() == m_threadID)
    {
        WriteTrace(TraceThread, TraceDebug, "Done");
        return;
    }
    pthread_t * thread_id = (pthread_t *)m_thread;
    pthread_join(*thread_id, nullptr);
    delete thread_id;
    m_thread = nullptr;
    m_threadID = 0;
    m_running = false;
    WriteTrace(TraceThread, TraceDebug, "Done");
}
#endif

uint32_t CThread::GetCurrentThreadId(void)
{
#ifdef _WIN32
    return ::GetCurrentThreadId();
#elif defined(SYS_gettid) || defined(__NR_gettid)
    return syscall(__NR_gettid); // GLIBC has no implementation of gettid()
#else
    return gettid();
#endif
}
