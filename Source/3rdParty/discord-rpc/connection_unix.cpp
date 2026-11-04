// Linux counterpart of connection_win.cpp: the connection to the Discord client is the Unix
// domain socket "discord-ipc-N" (N = 0..9) that the client creates in the runtime directory.
#include "connection.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

int GetProcessId()
{
    return (int)getpid();
}

struct BaseConnectionUnix : public BaseConnection {
    int sock{-1};
};

static BaseConnectionUnix Connection;
static sockaddr_un PipeAddr{};
#ifdef MSG_NOSIGNAL
static int MsgFlags = MSG_NOSIGNAL;
#else
static int MsgFlags = 0;
#endif

/*static*/ BaseConnection* BaseConnection::Create()
{
    PipeAddr.sun_family = AF_UNIX;
    return &Connection;
}

/*static*/ void BaseConnection::Destroy(BaseConnection*& c)
{
    auto self = reinterpret_cast<BaseConnectionUnix*>(c);
    self->Close();
    c = nullptr;
}

// The directories in which a Discord client creates its socket: the runtime directory (also
// the subdirectories that the Flatpak and Snap packages of Discord use) and the temporary ones
static bool TryConnect(BaseConnectionUnix* self, const char* directory)
{
    for (int pipeNum = 0; pipeNum < 10; ++pipeNum) {
        snprintf(PipeAddr.sun_path, sizeof(PipeAddr.sun_path), "%s/discord-ipc-%d", directory, pipeNum);
        if (connect(self->sock, (const sockaddr*)&PipeAddr, sizeof(PipeAddr)) == 0) {
            return true;
        }
    }
    return false;
}

bool BaseConnection::Open()
{
    auto self = reinterpret_cast<BaseConnectionUnix*>(this);
    self->sock = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (self->sock == -1) {
        return false;
    }
    fcntl(self->sock, F_SETFL, O_NONBLOCK);

    const char* bases[] = {getenv("XDG_RUNTIME_DIR"), getenv("TMPDIR"), getenv("TMP"), getenv("TEMP"), "/tmp"};
    const char* packaged[] = {"", "/app/com.discordapp.Discord", "/app/com.discordapp.DiscordCanary", "/snap.discord"};
    for (const char* base : bases) {
        if (base == nullptr || base[0] == '\0') {
            continue;
        }
        for (const char* subdirectory : packaged) {
            char directory[sizeof(PipeAddr.sun_path)];
            snprintf(directory, sizeof(directory), "%s%s", base, subdirectory);
            if (TryConnect(self, directory)) {
                self->isOpen = true;
                return true;
            }
        }
    }
    self->Close();
    return false;
}

bool BaseConnection::Close()
{
    auto self = reinterpret_cast<BaseConnectionUnix*>(this);
    if (self->sock == -1) {
        return false;
    }
    close(self->sock);
    self->sock = -1;
    self->isOpen = false;
    return true;
}

bool BaseConnection::Write(const void* data, size_t length)
{
    auto self = reinterpret_cast<BaseConnectionUnix*>(this);
    if (self->sock == -1) {
        return false;
    }
    ssize_t sentBytes = send(self->sock, data, length, MsgFlags);
    if (sentBytes < 0) {
        Close();
    }
    return sentBytes == (ssize_t)length;
}

bool BaseConnection::Read(void* data, size_t length)
{
    auto self = reinterpret_cast<BaseConnectionUnix*>(this);
    if (self->sock == -1) {
        return false;
    }
    int res = (int)recv(self->sock, data, length, MsgFlags);
    if (res < 0) {
        if (errno == EAGAIN) {
            return false;
        }
        Close();
    }
    else if (res == 0) {
        Close();
    }
    return res == (int)length;
}
