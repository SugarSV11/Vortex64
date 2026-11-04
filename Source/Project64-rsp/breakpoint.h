#include "Rsp.h"
#ifdef _WIN32
#include <Windows.h>
#else
#include <stdint.h>
typedef uint32_t DWORD;
#endif

#define MaxBPoints 0x30

typedef struct
{
    unsigned int Location;
} BPOINT;

extern BPOINT BPoint[MaxBPoints];
extern int NoOfBpoints;

#ifdef _WIN32
void Add_BPoint(void);
void CreateBPPanel(void * hDlg, rectangle rcBox);
void HideBPPanel(void);
void PaintBPPanel(window_paint ps);
void ShowBPPanel(void);
void RefreshBpoints(void * hList);
void RemoveBpoint(void * hList, int index);
#endif
void RemoveAllBpoint(void);

int AddRSP_BPoint(DWORD Location, int Confirm);
int CheckForRSPBPoint(DWORD Location);
void RemoveRSPBreakPoint(DWORD Location);
