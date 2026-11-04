// Regression tests of the Linux threading layer of Common (no GUI, no ROM):
//  - SyncEvent(false) (auto reset) is re-armed by the wait it releases,
//  - CThread::Terminate unwinds the thread (pthread_cancel raises __forced_unwind, which the
//    thread wrapper must let through instead of absorbing it with catch (...)).
#include <Common/SyncEvent.h>
#include <Common/Thread.h>
#include <Common/Trace.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>
#include <unistd.h>

static int g_Failures = 0;
#define CHECK(cond)                                                \
    do                                                             \
    {                                                              \
        if (!(cond))                                               \
        {                                                          \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            g_Failures++;                                          \
        }                                                          \
    } while (0)

static std::atomic<int> g_Unwound{0};
static std::atomic<bool> g_InLoop{false};

struct Sentinel
{
    ~Sentinel()
    {
        g_Unwound++;
    }
};

static void * CancellableThread(void *)
{
    Sentinel Guard;
    g_InLoop = true;
    try
    {
        for (;;)
        {
            usleep(1000); // a cancellation point
        }
    }
    catch (int)
    {
        // a handler that does not match: the forced unwind must pass through
    }
    return nullptr;
}

int main()
{
    TraceSetMaxModule(64, 0); // the trace calls of CThread read the module levels
    // auto reset event: one trigger releases one wait, the next wait blocks again
    {
        SyncEvent Event(false);
        CHECK(!Event.IsTriggered(0));
        Event.Trigger();
        CHECK(Event.IsTriggered(1000));
        CHECK(!Event.IsTriggered(100)); // re-armed: nothing is signalled any more
        CHECK(!Event.IsTriggered(0));
        Event.Trigger();
        CHECK(Event.IsTriggered(0));
        CHECK(!Event.IsTriggered(0));
        // a waiter that is blocked when the trigger comes is released once, and re-armed
        std::atomic<int> Released{0};
        std::thread Waiter([&]() {
            for (int i = 0; i < 2; i++)
            {
                if (Event.IsTriggered(SyncEvent::INFINITE_TIMEOUT))
                {
                    Released++;
                }
            }
        });
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        CHECK(Released == 0);
        Event.Trigger();
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        CHECK(Released == 1); // the second wait of the thread blocks
        Event.Trigger();
        Waiter.join();
        CHECK(Released == 2);
    }
    // manual reset event: stays signalled until Reset
    {
        SyncEvent Event(true);
        Event.Trigger();
        CHECK(Event.IsTriggered(0));
        CHECK(Event.IsTriggered(0));
        Event.Reset();
        CHECK(!Event.IsTriggered(0));
    }
    // Terminate unwinds a thread that is blocked in a loop, and returns
    {
        CThread Thread(CancellableThread);
        Thread.Start(nullptr);
        for (int i = 0; i < 200 && !g_InLoop; i++)
        {
            usleep(10000);
        }
        CHECK(g_InLoop);
        CHECK(Thread.isRunning());
        Thread.Terminate();
        CHECK(!Thread.isRunning());
        CHECK(g_Unwound == 1);
    }
    printf(g_Failures == 0 ? "PASS\n" : "FAILED (%d)\n", g_Failures);
    return g_Failures == 0 ? 0 : 1;
}
