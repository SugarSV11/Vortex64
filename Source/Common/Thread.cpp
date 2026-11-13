#include "Thread.h"
#include "Trace.h"
#include "TraceModulesCommon.h"
#include <cxxabi.h>
#include <pthread.h>
#include <sys/syscall.h>
#include <unistd.h>

CThread::CThread(CTHREAD_START_ROUTINE lpStartAddress) :
    m_StartAddress(lpStartAddress),
    m_lpThreadParameter(nullptr),
    m_thread(nullptr),
    m_running(false),
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
    // Joins a thread that already finished (Terminate() joined a running one)
    Wait();
    WriteTrace(TraceThread, TraceDebug, "Done");
}

bool CThread::Start(void * lpThreadParameter)
{
    WriteTrace(TraceThread, TraceDebug, "Start");
    m_lpThreadParameter = lpThreadParameter;
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
    WriteTrace(TraceThread, TraceDebug, "Done");
    return true;
}

void * CThread::ThreadWrapper(CThread * _this)
{
    WriteTrace(TraceThread, TraceDebug, "Start");
    _this->m_threadID = CThread::GetCurrentThreadId();
    _this->m_running = true;
    WriteTrace(TraceThread, TraceDebug, "Thread is running");
    void * res = nullptr;
    try
    {
#if defined(__i386__) || defined(_M_IX86) || defined(__arm__) || defined(_M_ARM)
        res = (void *)_this->m_StartAddress(_this->m_lpThreadParameter);
#else
        res = (void *)((uint64_t)_this->m_StartAddress(_this->m_lpThreadParameter));
#endif
    }
    catch (const __cxxabiv1::__forced_unwind &)
    {
        // pthread_cancel (Terminate) and pthread_exit (a fatal error of the thread) must be able
        // to unwind the thread; it is no longer running once it does
        _this->m_running = false;
        throw;
    }
    catch (...)
    {
        //WriteTrace(TraceUserInterface, TraceError, "Unhandled Exception ");
    }
    _this->m_running = false;
    WriteTrace(TraceThread, TraceDebug, "Thread is finished");
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
    WriteTrace(TraceThread, TraceDebug, "Done (res: %s)", m_running ? "true" : "false");
    return m_running;
    return true;
}

void CThread::Terminate(void)
{
    WriteTrace(TraceThread, TraceDebug, "Start");
    if (isRunning())
    {
        if (CThread::GetCurrentThreadId() != m_threadID)
        {
            WriteTrace(TraceThread, TraceDebug, "Cancelling thread");
            pthread_cancel(*(pthread_t *)m_thread);
            Wait();
        }
    }
    WriteTrace(TraceThread, TraceDebug, "Done");
}

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

uint32_t CThread::GetCurrentThreadId(void)
{
#if   defined(SYS_gettid) || defined(__NR_gettid)
    return syscall(__NR_gettid); // GLIBC has no implementation of gettid()
#else
    return gettid();
#endif
}
