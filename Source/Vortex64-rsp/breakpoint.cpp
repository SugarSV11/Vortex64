#include "breakpoint.h"
#include "Rsp.h"
#include <Vortex64-rsp-core/RSPInfo.h>
#include <stdio.h>

#define IDC_LOCATION_EDIT 105

BPOINT BPoint[MaxBPoints];
int NoOfBpoints;


int AddRSP_BPoint(DWORD Location, int Confirm)
{
    int count;

    if (NoOfBpoints == MaxBPoints)
    {
        DisplayError("Max amount of breakpoints set");
        return false;
    }

    for (count = 0; count < NoOfBpoints; count++)
    {
        if (BPoint[count].Location == Location)
        {
            DisplayError("You already have this breakpoint");
            return false;
        }
    }

    if (Confirm)
    {
        char Message[150];

        sprintf(Message, "Break when:\n\nRSP's program counter = 0x%03X\n\nIs this correct?",
                Location);
    }
    BPoint[NoOfBpoints].Location = Location;
    NoOfBpoints += 1;
    if (DebugInfo.UpdateBreakPoints)
    {
        DebugInfo.UpdateBreakPoints();
    }
    return true;
}

int CheckForRSPBPoint(DWORD Location)
{
    int count;

    for (count = 0; count < NoOfBpoints; count++)
    {
        if (BPoint[count].Location == Location)
        {
            return true;
        }
    }
    return false;
}



void RemoveAllBpoint(void)
{
    NoOfBpoints = 0;
}


void RemoveRSPBreakPoint(DWORD Location)
{
    int count, location = -1;

    for (count = 0; count < NoOfBpoints; count++)
    {
        if (BPoint[count].Location == Location)
        {
            location = count;
            count = NoOfBpoints;
        }
    }

    if (location >= 0)
    {
        for (count = location; count < NoOfBpoints - 1; count++)
        {
            BPoint[count].Location = BPoint[count + 1].Location;
        }
        NoOfBpoints -= 1;
        if (DebugInfo.UpdateBreakPoints)
        {
            DebugInfo.UpdateBreakPoints();
        }
    }
}
