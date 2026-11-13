#include "CriticalSection.h"
#include <pthread.h>

CriticalSection::CriticalSection()
{
    m_cs = new pthread_mutex_t;

    pthread_mutexattr_t mAttr;
    pthread_mutexattr_init(&mAttr);
    pthread_mutexattr_settype(&mAttr, PTHREAD_MUTEX_RECURSIVE_NP);
    pthread_mutex_init((pthread_mutex_t *)m_cs, &mAttr);
    pthread_mutexattr_destroy(&mAttr);
}

CriticalSection::~CriticalSection(void)
{
    pthread_mutex_destroy((pthread_mutex_t *)m_cs);
    delete (pthread_mutex_t *)m_cs;
}

/*
Enters a critical section of code.
Prevents other threads from accessing the section between the enter and leave sections simultaneously.
Note: It is good practice to try and keep the critical section code as little as possible, so that
other threads are not locked waiting for it.
*/

void CriticalSection::enter(void)
{
    pthread_mutex_lock((pthread_mutex_t *)m_cs);
}

/*
Leaves the critical section.
Allows threads access to the critical code section again.
Warning: Note that an exception occurring with a critical section may not result in the expected leave being
called.  To ensure that your critical section is exception safe, ensure that you wrap the critical
section in a try catch, and the catch calls the leave method.
*/

void CriticalSection::leave(void)
{
    pthread_mutex_unlock((pthread_mutex_t *)m_cs);
}
