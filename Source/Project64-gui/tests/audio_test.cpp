// The audio plugin's real output, without a sound card: SDL's "disk" audio driver writes what the device would play to
// a file. A constant stereo signal is sent through the plugin's N64 interface (AI registers, RDRAM, AiDacrateChanged,
// AiLenChanged) the way the core does, and the file has to hold it at the volume of the settings (and silence when
// muted). The game is closed and opened again several times (the ring buffer is made ready under its lock, the device
// is opened and closed each time) and the plugin is shut down with data still queued.
//
//   SDL_AUDIODRIVER=disk SDL_DISKAUDIOFILE=<file> audio-test <expected volume percent | mute>
#include <Common/path.h>
#include <Project64-cli/LinuxNotification.h>
#include <Project64-core/AppInit.h>
#include <Project64-core/Notification.h>
#include <Project64-core/N64System/SystemGlobals.h>
#include <Project64-core/Plugins/Plugin.h>
#include <Project64-core/Settings.h>
#include <Project64-plugin-spec/Audio.h>
#include <dlfcn.h>
#include <unistd.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <vector>

class DummyRenderWindow : public RenderWindow
{
public:
    void * GetWindowHandle(void) const override { return nullptr; }
};

static int g_Failures = 0;
#define CHECK(cond)                                                            \
    do                                                                         \
    {                                                                          \
        if (!(cond))                                                           \
        {                                                                      \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);             \
            g_Failures++;                                                      \
        }                                                                      \
    } while (0)

static void NoCheckInterrupts(void) {}

int main(int argc, char ** argv)
{
    if (argc < 2 || getenv("SDL_DISKAUDIOFILE") == nullptr)
    {
        printf("usage: SDL_AUDIODRIVER=disk SDL_DISKAUDIOFILE=<file> %s <volume percent | mute>\n", argv[0]);
        return 2;
    }
    const bool Mute = strcmp(argv[1], "mute") == 0;
    const int Volume = Mute ? 0 : atoi(argv[1]);
    const char * File = getenv("SDL_DISKAUDIOFILE");

    static CLinuxNotification Notify;
    const std::string Base = CPath(CPath::MODULE_DIRECTORY).GetDirectory();
    if (!AppInit(&Notify, Base.c_str(), 1, argv))
    {
        return 2;
    }
    static DummyRenderWindow Window;
    g_Plugins->SetRenderWindows(&Window, &Window);
    g_Plugins->CreatePlugins();
    CHECK(g_Plugins->Audio() != nullptr);
    // the library the core loaded (its path is the plugin directory of the settings)
    std::string Path;
    {
        FILE * Maps = fopen("/proc/self/maps", "r");
        char Line[1024];
        while (Maps != nullptr && fgets(Line, sizeof(Line), Maps) != nullptr)
        {
            const char * Slash = strchr(Line, '/');
            if (Slash != nullptr && strstr(Slash, "libProject64-audio.so") != nullptr && strstr(Slash, "qtconfig") == nullptr)
            {
                Path = Slash;
                Path.erase(Path.find_last_not_of("\n") + 1);
                break;
            }
        }
        if (Maps != nullptr)
        {
            fclose(Maps);
        }
    }
    void * Lib = dlopen(Path.c_str(), RTLD_NOW | RTLD_NOLOAD);
    CHECK(Lib != nullptr);
    if (Lib == nullptr)
    {
        return 2;
    }
    auto InitiateAudio = reinterpret_cast<int32_t (*)(AUDIO_INFO)>(dlsym(Lib, "InitiateAudio"));
    auto RomOpen = reinterpret_cast<void (*)(void)>(dlsym(Lib, "RomOpen"));
    auto RomClosed = reinterpret_cast<void (*)(void)>(dlsym(Lib, "RomClosed"));
    auto DacrateChanged = reinterpret_cast<void (*)(int)>(dlsym(Lib, "AiDacrateChanged"));
    auto LenChanged = reinterpret_cast<void (*)(void)>(dlsym(Lib, "AiLenChanged"));
    auto Update = reinterpret_cast<void (*)(int32_t)>(dlsym(Lib, "AiUpdate"));
    auto ReadLength = reinterpret_cast<uint32_t (*)(void)>(dlsym(Lib, "AiReadLength"));
    CHECK(InitiateAudio && RomOpen && RomClosed && DacrateChanged && LenChanged && Update && ReadLength);
    if (!(InitiateAudio && RomOpen && RomClosed && DacrateChanged && LenChanged && Update && ReadLength))
    {
        return 2;
    }

    static uint8_t Header[0x40];
    static uint8_t Dmem[0x1000], Imem[0x1000];
    std::vector<uint8_t> Rdram(0x200000);
    uint32_t MiIntr = 0, DramAddr = 0, Len = 0, Control = 0, Status = 0, DacRate = 0, BitRate = 0;
    AUDIO_INFO Info = {};
    Info.Reserved = true;
    Info.HEADER = Header;
    Info.RDRAM = Rdram.data();
    Info.DMEM = Dmem;
    Info.IMEM = Imem;
    Info.MI_INTR_REG = &MiIntr;
    Info.AI_DRAM_ADDR_REG = &DramAddr;
    Info.AI_LEN_REG = &Len;
    Info.AI_CONTROL_REG = &Control;
    Info.AI_STATUS_REG = &Status;
    Info.AI_DACRATE_REG = &DacRate;
    Info.AI_BITRATE_REG = &BitRate;
    Info.CheckInterrupts = NoCheckInterrupts;

    // 4096 stereo frames: left +16000, right -16000 (an RDRAM word holds left in the high half, right in the low one)
    const int16_t Left = 16000, Right = -16000;
    for (size_t i = 0; i + 4 <= 0x4000; i += 4)
    {
        const uint32_t Word = ((uint32_t)(uint16_t)Left << 16) | (uint16_t)Right;
        memcpy(&Rdram[0x1000 + i], &Word, 4);
    }

    unlink(File);
    const int Cycles = 3;
    for (int Cycle = 0; Cycle < Cycles; Cycle++)
    {
        CHECK(InitiateAudio(Info));
        RomOpen();
        DacRate = 1520; // 32000 Hz
        DacrateChanged(0);
        Control = 1; // DMA on
        for (int Block = 0; Block < 8; Block++)
        {
            DramAddr = 0x1000;
            Len = 0x4000;
            LenChanged();
            const uint32_t Queued = ReadLength();
            CHECK(Queued <= 0x4000);
            Update(0);
            usleep(60000);
        }
        // the plugin is closed with data still queued: no crash, no hang
        RomClosed();
    }
    AppCleanup();

    FILE * F = fopen(File, "rb");
    CHECK(F != nullptr);
    if (F == nullptr)
    {
        return 1;
    }
    std::vector<int16_t> Samples;
    int16_t Buf[4096];
    size_t N;
    while ((N = fread(Buf, sizeof(int16_t), 4096, F)) > 0)
    {
        Samples.insert(Samples.end(), Buf, Buf + N);
    }
    fclose(F);
    // (the device converts the signal: a few frames at each start and end ring; the rest is the signal within one step)
    const int Want = 16000 * Volume / 100;
    size_t NonZero = 0, OnLevel = 0;
    for (size_t i = 0; i + 1 < Samples.size(); i += 2)
    {
        NonZero += Samples[i] != 0 || Samples[i + 1] != 0;
        OnLevel += abs(Samples[i] - Want) <= 2 && abs(Samples[i + 1] + Want) <= 2;
    }
    printf("output: %zu samples, %zu frames with sound, %zu at the expected level %d\n", Samples.size(), NonZero, OnLevel, Want);
    if (Mute)
    {
        CHECK(NonZero == 0);
    }
    else
    {
        CHECK(NonZero > 3000 * (size_t)Cycles);
        CHECK(OnLevel * 100 >= NonZero * 98);
    }
    printf("%s audio (%s)\n", g_Failures == 0 ? "PASS" : "FAILED", argv[1]);
    return g_Failures == 0 ? 0 : 1;
}
