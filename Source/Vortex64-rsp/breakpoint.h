#include "Rsp.h"
#include <stdint.h>
typedef uint32_t DWORD;

#define MaxBPoints 0x30

typedef struct
{
    unsigned int Location;
} BPOINT;

extern BPOINT BPoint[MaxBPoints];
extern int NoOfBpoints;

void RemoveAllBpoint(void);

int AddRSP_BPoint(DWORD Location, int Confirm);
int CheckForRSPBPoint(DWORD Location);
void RemoveRSPBreakPoint(DWORD Location);
