#pragma once

// Linux stand-in for the part of <windows.h> that the script engine of the debugger
// (Project64/UserInterface/Debugger/Script*.cpp, ScriptAPI/*) uses: threads, events, the
// waitable timer of setInterval, and the Winsock names of the socket API (BSD sockets).
// Only the script targets have this directory on their include path; the implementation
// is ScriptShim.cpp.

#include <algorithm>
#include <arpa/inet.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdint.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

typedef uint32_t DWORD;
typedef int BOOL;
typedef uint16_t WORD;
typedef unsigned int UINT;
typedef void * HANDLE;
typedef void * LPVOID;
typedef void * HWND;
typedef char * LPSTR;
typedef const char * LPCSTR;
typedef uintptr_t WPARAM;
typedef intptr_t LPARAM;
typedef DWORD * LPDWORD;
typedef void * HMODULE;
typedef uintptr_t ULONG_PTR;
typedef unsigned long ULONG;

// the Windows headers define min/max; the script sources use them unqualified
using std::max;
using std::min;

typedef struct
{
    DWORD dwLowDateTime;
    DWORD dwHighDateTime;
} FILETIME;
typedef union
{
    struct
    {
        DWORD LowPart;
        DWORD HighPart;
    };
    uint64_t QuadPart;
} ULARGE_INTEGER;
typedef union
{
    struct
    {
        DWORD LowPart;
        int32_t HighPart;
    };
    int64_t QuadPart;
} LARGE_INTEGER;
void GetSystemTimeAsFileTime(FILETIME * FileTime);

// native modules (require() of a shared library)
inline HMODULE LoadLibraryA(const char * Path)
{
    return dlopen(Path, RTLD_NOW);
}
inline void * GetProcAddress(HMODULE Module, const char * Name)
{
    return dlsym(Module, Name);
}
inline BOOL FreeLibrary(HMODULE Module)
{
    return dlclose(Module) == 0;
}

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif
#ifndef MAX_PATH
#define MAX_PATH 260
#endif
#define WINAPI
#define INFINITE 0xFFFFFFFFu
#define INVALID_HANDLE_VALUE ((HANDLE)(intptr_t)-1)
#define WAIT_OBJECT_0 0
#define WAIT_TIMEOUT 0x102
#define WAIT_FAILED 0xFFFFFFFFu
#define MAKEWORD(a, b) ((WORD)(((uint8_t)(a)) | ((WORD)((uint8_t)(b))) << 8))

// Mouse messages the script system maps to mouse events (ScriptSystem::DoMouseEvent)
#define WM_LBUTTONDOWN 0x0201
#define WM_LBUTTONUP 0x0202
#define WM_RBUTTONDOWN 0x0204
#define WM_RBUTTONUP 0x0205
#define WM_MBUTTONDOWN 0x0207
#define WM_MBUTTONUP 0x0208

HANDLE CreateThread(void * SecurityAttributes, size_t StackSize, DWORD (*StartAddress)(void *), void * Parameter, DWORD CreationFlags, DWORD * ThreadId);
HANDLE CreateEvent(void * SecurityAttributes, BOOL ManualReset, BOOL InitialState, const char * Name);
BOOL SetEvent(HANDLE Event);
BOOL ResetEvent(HANDLE Event);
DWORD WaitForSingleObject(HANDLE Handle, DWORD Milliseconds);
DWORD WaitForMultipleObjects(DWORD Count, const HANDLE * Handles, BOOL WaitAll, DWORD Milliseconds);
BOOL CloseHandle(HANDLE Handle);
void Sleep(DWORD Milliseconds);
inline BOOL PathFileExistsA(const char * Path)
{
    return access(Path, F_OK) == 0;
}

// Waitable timer (setInterval / setTimeout of the scripts): a relative due time in 100 ns units
// (negative) and a period in milliseconds; auto reset
HANDLE CreateWaitableTimer(void * SecurityAttributes, BOOL ManualReset, const char * Name);
BOOL SetWaitableTimer(HANDLE Timer, const LARGE_INTEGER * DueTime, long Period, void * CompletionRoutine, void * Argument, BOOL Resume);

// Files: the directory listing pattern of the fs module is "<dir>\\*" (or "/*")
#define FILE_ATTRIBUTE_DIRECTORY 0x10
#define FILE_ATTRIBUTE_NORMAL 0x80
struct WIN32_FIND_DATAA
{
    DWORD dwFileAttributes;
    DWORD nFileSizeHigh;
    DWORD nFileSizeLow;
    char cFileName[MAX_PATH];
};
HANDLE FindFirstFileA(const char * Pattern, WIN32_FIND_DATAA * FindData);
BOOL FindNextFileA(HANDLE Find, WIN32_FIND_DATAA * FindData);
BOOL FindClose(HANDLE Find);
BOOL DeleteFileA(const char * Path);
BOOL CreateDirectoryA(const char * Path, void * SecurityAttributes);
BOOL RemoveDirectoryA(const char * Path);
#define _fileno fileno

// Processes and pipes (exec()); "cmd /c <command>" runs <command> with /bin/sh -c
struct SECURITY_ATTRIBUTES
{
    DWORD nLength;
    void * lpSecurityDescriptor;
    BOOL bInheritHandle;
};
#define STARTF_USESHOWWINDOW 0x1
#define STARTF_USESTDHANDLES 0x100
#define SW_HIDE 0
#define ZeroMemory(Destination, Length) memset((Destination), 0, (Length))
struct STARTUPINFOA
{
    DWORD cb;
    DWORD dwFlags;
    WORD wShowWindow;
    HANDLE hStdInput;
    HANDLE hStdOutput;
    HANDLE hStdError;
};
struct PROCESS_INFORMATION
{
    HANDLE hProcess;
    HANDLE hThread;
    DWORD dwProcessId;
    DWORD dwThreadId;
};
BOOL CreatePipe(HANDLE * ReadPipe, HANDLE * WritePipe, SECURITY_ATTRIBUTES * Attributes, DWORD Size);
BOOL ReadFile(HANDLE File, void * Buffer, DWORD NumberOfBytesToRead, DWORD * NumberOfBytesRead, void * Overlapped);
BOOL CreateProcessA(const char * ApplicationName, char * CommandLine, void * ProcessAttributes, void * ThreadAttributes, BOOL InheritHandles,
                    DWORD CreationFlags, void * Environment, const char * CurrentDirectory, STARTUPINFOA * StartupInfo, PROCESS_INFORMATION * ProcessInformation);
BOOL GetExitCodeProcess(HANDLE Process, DWORD * ExitCode);

// Threads of scripts that have not ended (the script worker threads and the script system's
// own), for the tests
int ScriptShimLiveThreads();
// Ends the processes that scripts started with exec(), which waits for them
void ScriptShimTerminateProcesses();

// The frontend receives what the script API sends to the main window / shows message boxes
struct ScriptShimHooks
{
    void (*PostMessage)(unsigned Message, uintptr_t WParam, intptr_t LParam);
    void (*MessageBox)(const char * Text, const char * Caption);
};
extern ScriptShimHooks g_ScriptShimHooks;
inline BOOL PostMessage(HWND, UINT Message, WPARAM WParam, LPARAM LParam)
{
    if (g_ScriptShimHooks.PostMessage != nullptr)
    {
        g_ScriptShimHooks.PostMessage(Message, WParam, LParam);
    }
    return TRUE;
}
#define MB_OK 0
inline int MessageBoxA(HWND, const char * Text, const char * Caption, UINT)
{
    if (g_ScriptShimHooks.MessageBox != nullptr)
    {
        g_ScriptShimHooks.MessageBox(Text, Caption);
    }
    return 1;
}

// Winsock names on BSD sockets
typedef int SOCKET;
// A socket is not inherited by the processes that scripts start (exec), which would keep a
// closed server's port open, and a send() to a closed connection is an error, not SIGPIPE
inline int ScriptShimSocket(int Family, int Type, int Protocol)
{
    return socket(Family, Type | SOCK_CLOEXEC, Protocol);
}
// (and a socket that accept() returns is non-blocking when the listening one is, as in Winsock)
inline int ScriptShimAccept(int Socket, struct sockaddr * Address, int * AddressLength)
{
    socklen_t Length = AddressLength != nullptr ? (socklen_t)*AddressLength : 0;
    const int Result = accept4(Socket, Address, AddressLength != nullptr ? &Length : nullptr, SOCK_CLOEXEC);
    if (Result >= 0 && (fcntl(Socket, F_GETFL, 0) & O_NONBLOCK) != 0)
    {
        fcntl(Result, F_SETFL, fcntl(Result, F_GETFL, 0) | O_NONBLOCK);
    }
    if (AddressLength != nullptr)
    {
        *AddressLength = (int)Length;
    }
    return Result;
}
#define socket(Family, Type, Protocol) ScriptShimSocket((Family), (Type), (Protocol))
#define accept(Socket, Address, AddressLength) ScriptShimAccept((Socket), (Address), (AddressLength))
#define send(Socket, Buffer, Length, Flags) send((Socket), (Buffer), (Length), (Flags) | MSG_NOSIGNAL)
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)
typedef struct sockaddr SOCKADDR;
typedef struct sockaddr_in SOCKADDR_IN;
typedef struct sockaddr_in6 SOCKADDR_IN6;
typedef struct timeval TIMEVAL;
#define SD_RECEIVE SHUT_RD
#define SD_SEND SHUT_WR
#define SD_BOTH SHUT_RDWR
#define WSAEWOULDBLOCK EWOULDBLOCK
#define WSAECONNRESET ECONNRESET
#define WSAEINPROGRESS EINPROGRESS
// Winsock ignores the first argument of select(); BSD sockets need the highest descriptor + 1, and
// select() of Linux writes the time left back into the timeout (Winsock leaves it alone: the
// workers reuse one timeval, which would become 0 and make them spin)
int ScriptShimSelect(fd_set * ReadFds, fd_set * WriteFds, fd_set * ExceptFds, struct timeval * Timeout);
// An fd_set holds FD_SETSIZE descriptors (Winsock's holds sockets, however high): a socket above
// that is not stored (writing it would be out of bounds) and the next select() fails instead
void ScriptShimFdSet(int Fd, fd_set * Set);
bool ScriptShimFdIsSet(int Fd, fd_set * Set);
#undef FD_SET
#undef FD_ISSET
#define FD_SET(Fd, Set) ScriptShimFdSet((Fd), (Set))
#define FD_ISSET(Fd, Set) ScriptShimFdIsSet((Fd), (Set))
#define select(NumFds, ReadFds, WriteFds, ExceptFds, Timeout) ScriptShimSelect((ReadFds), (WriteFds), (ExceptFds), (Timeout))
struct WSADATA
{
    int unused;
};
inline int WSAStartup(WORD, WSADATA *)
{
    return 0;
}
inline int WSACleanup()
{
    return 0;
}
inline int WSAGetLastError()
{
    return errno;
}
// Winsock takes int lengths, BSD sockets socklen_t
inline int getsockopt(SOCKET Socket, int Level, int OptionName, char * OptionValue, int * OptionLength)
{
    socklen_t Length = (socklen_t)*OptionLength;
    const int Result = ::getsockopt(Socket, Level, OptionName, OptionValue, &Length);
    *OptionLength = (int)Length;
    return Result;
}
inline int getsockname(SOCKET Socket, struct sockaddr * Name, int * NameLength)
{
    socklen_t Length = (socklen_t)*NameLength;
    const int Result = ::getsockname(Socket, Name, &Length);
    *NameLength = (int)Length;
    return Result;
}
inline int getpeername(SOCKET Socket, struct sockaddr * Name, int * NameLength)
{
    socklen_t Length = (socklen_t)*NameLength;
    const int Result = ::getpeername(Socket, Name, &Length);
    *NameLength = (int)Length;
    return Result;
}
struct WSAPROTOCOL_INFO
{
    int iAddressFamily; // SO_PROTOCOL_INFO is SO_DOMAIN: the address family
};
#define SO_PROTOCOL_INFO SO_DOMAIN
inline int closesocket(SOCKET Socket)
{
    return close(Socket);
}
inline int ioctlsocket(SOCKET Socket, long Command, unsigned long * Argument)
{
    if (Command == FIONBIO)
    {
        int Flags = fcntl(Socket, F_GETFL, 0);
        if (Flags < 0)
        {
            return -1;
        }
        return fcntl(Socket, F_SETFL, *Argument ? (Flags | O_NONBLOCK) : (Flags & ~O_NONBLOCK));
    }
    return ioctl(Socket, (unsigned long)Command, Argument);
}
