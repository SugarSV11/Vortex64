#pragma once
#include <stdint.h>

class HighResTimeStamp
{
public:
    HighResTimeStamp();
    HighResTimeStamp & SetToNow(void);
    uint64_t GetMicroSeconds(void);
    void SetMicroSeconds(uint64_t MicroSeconds);

private:
    uint64_t m_time;
};
