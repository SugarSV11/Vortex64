#include "SyncEvent.h"
#include <errno.h>
#include <pthread.h>
#include <time.h>

SyncEvent::SyncEvent(bool bManualReset)
{
    m_signalled = false;
    m_manualReset = bManualReset;
    m_Event = new pthread_mutex_t;
    m_cond = new pthread_cond_t;
    pthread_mutex_init((pthread_mutex_t *)m_Event, nullptr);
    pthread_cond_init((pthread_cond_t *)m_cond, nullptr);
}

SyncEvent::~SyncEvent()
{
    pthread_mutex_destroy((pthread_mutex_t *)m_Event);
    pthread_cond_destroy((pthread_cond_t *)m_cond);
    delete (pthread_mutex_t *)m_Event;
    delete (pthread_cond_t *)m_cond;
}

void SyncEvent::Trigger()
{
    pthread_mutex_lock((pthread_mutex_t *)m_Event);
    m_signalled = true;
    pthread_mutex_unlock((pthread_mutex_t *)m_Event);
    pthread_cond_signal((pthread_cond_t *)m_cond);
}

bool SyncEvent::IsTriggered(int32_t iWaitTime) const
{
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
}

void SyncEvent::Reset()
{
    pthread_mutex_lock((pthread_mutex_t *)m_Event);
    m_signalled = false;
    pthread_mutex_unlock((pthread_mutex_t *)m_Event);
}

void * SyncEvent::GetHandle()
{
    return m_Event;
}
