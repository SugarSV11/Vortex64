#include "SyncEvent.h"
#ifdef _WIN32
#include <Windows.h>
#else
#include <errno.h>
#include <pthread.h>
#include <time.h>
#endif

SyncEvent::SyncEvent(bool bManualReset)
{
#ifdef _WIN32
    m_Event = CreateEvent(nullptr, bManualReset, FALSE, nullptr);
#else
    m_signalled = false;
    m_manualReset = bManualReset;
    m_Event = new pthread_mutex_t;
    m_cond = new pthread_cond_t;
    pthread_mutex_init((pthread_mutex_t *)m_Event, nullptr);
    pthread_cond_init((pthread_cond_t *)m_cond, nullptr);
#endif
}

SyncEvent::~SyncEvent()
{
#ifdef _WIN32
    CloseHandle(m_Event);
#else
    pthread_mutex_destroy((pthread_mutex_t *)m_Event);
    pthread_cond_destroy((pthread_cond_t *)m_cond);
    delete (pthread_mutex_t *)m_Event;
    delete (pthread_cond_t *)m_cond;
#endif
}

void SyncEvent::Trigger()
{
#ifdef _WIN32
    SetEvent(m_Event);
#else
    pthread_mutex_lock((pthread_mutex_t *)m_Event);
    m_signalled = true;
    pthread_mutex_unlock((pthread_mutex_t *)m_Event);
    pthread_cond_signal((pthread_cond_t *)m_cond);
#endif
}

bool SyncEvent::IsTriggered(int32_t iWaitTime) const
{
#ifdef _WIN32
    return (WAIT_OBJECT_0 == WaitForSingleObject(m_Event, iWaitTime));
#else
    pthread_mutex_t * mutex = (pthread_mutex_t *)m_Event;
    pthread_cond_t * cond = (pthread_cond_t *)m_cond;
    pthread_mutex_lock(mutex);
    if (iWaitTime == 0 && !m_signalled)
    {
        pthread_mutex_unlock(mutex);
        return false;
    }
    if (iWaitTime == INFINITE_TIMEOUT)
    {
        while (!m_signalled)
        {
            pthread_cond_wait(cond, mutex);
        }
    }
    else if (!m_signalled)
    {
        struct timespec timeout;
        clock_gettime(CLOCK_REALTIME, &timeout);
        timeout.tv_sec += iWaitTime / 1000;
        timeout.tv_nsec += (iWaitTime % 1000) * 1000000;
        if (timeout.tv_nsec >= 1000000000)
        {
            timeout.tv_sec += 1;
            timeout.tv_nsec -= 1000000000;
        }
        while (!m_signalled)
        {
            int result = pthread_cond_timedwait(cond, mutex, &timeout);
            if (result == ETIMEDOUT)
            {
                pthread_mutex_unlock(mutex);
                return false;
            }
        }
    }
    // Windows semantics: an auto-reset event is reset by the wait that it releases
    if (!m_manualReset)
    {
        const_cast<SyncEvent *>(this)->m_signalled = false;
    }
    pthread_mutex_unlock(mutex);
    return true;
#endif
}

void SyncEvent::Reset()
{
#ifdef _WIN32
    ResetEvent(m_Event);
#else
    pthread_mutex_lock((pthread_mutex_t *)m_Event);
    m_signalled = false;
    pthread_mutex_unlock((pthread_mutex_t *)m_Event);
#endif
}

void * SyncEvent::GetHandle()
{
    return m_Event;
}
