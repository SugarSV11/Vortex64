#pragma once
#include <stdint.h>
#include <atomic>

class CThread
{
public:
    typedef void * (*CTHREAD_START_ROUTINE)(void *);
    CThread(CTHREAD_START_ROUTINE lpStartAddress);
    ~CThread();

    bool Start(void * lpThreadParameter);

    inline uint32_t ThreadID(void) const
    {
        return m_threadID;
    }

    bool isRunning(void) const;
    void Terminate(void);
    void Wait(void);
    static uint32_t GetCurrentThreadId(void);

private:
    CThread(void);
    CThread(const CThread &);
    CThread & operator=(const CThread &);

    static void * ThreadWrapper(CThread * _this);

    CTHREAD_START_ROUTINE m_StartAddress;
    void * m_lpThreadParameter;
    void * m_thread;
    std::atomic<uint32_t> m_threadID; // written by the thread itself, read by whoever ends it
    std::atomic<bool> m_running;
};
