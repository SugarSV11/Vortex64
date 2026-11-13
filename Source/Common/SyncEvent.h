#pragma once
#include <stdint.h>

class SyncEvent
{
public:
    enum
    {
        INFINITE_TIMEOUT = 0xFFFFFFFF
    };

    SyncEvent(bool bManualReset = true);
    ~SyncEvent(void);

    void Trigger(void);
    bool IsTriggered(int32_t iWaitTime = 0) const;
    void Reset();
    void * GetHandle();

protected:
    SyncEvent(const SyncEvent &);
    SyncEvent & operator=(const SyncEvent &);

    void * m_Event;
    void * m_cond;
    bool m_signalled;
    bool m_manualReset;
};
