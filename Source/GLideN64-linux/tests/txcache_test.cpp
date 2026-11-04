// The texture cache of the texture enhancement (GLideNHQ), under AddressSanitizer: textures of every size and format, a few checksums (so
// the same checksum comes again with another format), and a cache that is much smaller than the textures that go through it, so it evicts
// all the time. Built twice by CMake: with the cache of Vortex64 (must pass) and with the original of the submodule (must fail: use after
// free in TxMemoryCache::get), which shows that the test sees the fault.
//
//   txcache-test            (exit 0: no error)
#include <cstdint>
#include "GLideNHQ/TxFilter.h"
#include "GLideNHQ/TxFilterExport.h"
#include "GLideNHQ/TextureFilters_xbrz.h"
#include <Graphics/Parameters.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

// the plugin defines these with OpenGL; the filters only need their values
extern "C"
{
int osal_is_key_pressed(int, int) { return 0; }
void osal_keys_update_state() {}
}
namespace graphics
{
namespace colorFormat
{
ColorFormatParam RED_GREEN_BLUE(0x1907);
ColorFormatParam RGBA(0x1908);
}
namespace internalcolorFormat
{
InternalColorFormatParam NOCOLOR(0U);
InternalColorFormatParam RGB8(0x8051);
InternalColorFormatParam RGBA8(0x8058);
InternalColorFormatParam RGBA4(0x8056);
InternalColorFormatParam RGB5_A1(0x8057);
InternalColorFormatParam COLOR_INDEX8(0x80E5);
}
namespace datatype
{
DatatypeParam UNSIGNED_BYTE(0x1401);
DatatypeParam UNSIGNED_SHORT_5_6_5(0x8363);
DatatypeParam UNSIGNED_SHORT_5_5_5_1(0x8034);
DatatypeParam UNSIGNED_SHORT_4_4_4_4(0x8033);
}
}

static int Run(int Options, int LimitMB, int Iterations)
{
    char Dir[] = "/tmp/vortex-txcache-XXXXXX";
    if (mkdtemp(Dir) == nullptr)
    {
        return 1;
    }
    wchar_t Cache[256], Dump[256], Pack[256];
    swprintf(Cache, 256, L"%s/cache", Dir);
    swprintf(Dump, 256, L"%s/dump", Dir);
    swprintf(Pack, 256, L"%s/pack", Dir);
    if (!txfilter_init(8192, 8192, 32, Options, LimitMB * 1024 * 1024, Cache, Dump, Pack, L"TXCACHE-TEST", nullptr))
    {
        printf("FAIL: init\n");
        return 1;
    }
    uint32_t Seed = 1;
    auto Rnd = [&]() {
        Seed = Seed * 1664525u + 1013904223u;
        return Seed >> 8;
    };
    const int Dims[] = {4, 8, 16, 32, 64, 128};
    const uint32_t Formats[3] = {u32(graphics::internalcolorFormat::RGBA8), u32(graphics::internalcolorFormat::RGBA4), u32(graphics::internalcolorFormat::RGB5_A1)};
    int Filtered = 0;
    for (int i = 0; i < Iterations; i++)
    {
        const int W = Dims[Rnd() % 6], H = Dims[Rnd() % 6];
        const int Format = Rnd() % 3;
        std::vector<uint8_t> Source((size_t)W * H * (Format == 0 ? 4 : 2));
        for (uint8_t & B : Source)
        {
            B = (uint8_t)Rnd();
        }
        GHQTexInfo Info;
        memset(&Info, 0, sizeof Info);
        const uint64_t Crc = (uint64_t)(Rnd() % 400) + 1; // few: hits, and one checksum with several formats
        N64FormatSize FormatSize(Rnd() % 5, Rnd() % 4);
        if (txfilter_filter(Source.data(), W, H, (uint16_t)Formats[Format], Crc, FormatSize, &Info) && Info.data != nullptr)
        {
            Filtered++;
            volatile uint8_t Sum = 0;
            for (size_t k = 0; k < (size_t)Info.width * Info.height; k += 997)
            {
                Sum = Sum + Info.data[k];
            }
        }
    }
    txfilter_shutdown();
    return Filtered > Iterations / 2 ? 0 : 1;
}

// xBRZ with several threads: the texture comes out as the whole image scaled at once, with no line of different pixels at the cuts between the
// slices of the threads (each slice used to be taken as an image of its own)
static int CheckSlices()
{
    int Failures = 0;
    char Dir[] = "/tmp/vortex-txslices-XXXXXX";
    if (mkdtemp(Dir) == nullptr)
    {
        return 1;
    }
    wchar_t Cache[256], Dump[256], Pack[256];
    swprintf(Cache, 256, L"%s/cache", Dir);
    swprintf(Dump, 256, L"%s/dump", Dir);
    swprintf(Pack, 256, L"%s/pack", Dir);
    const struct
    {
        int Option, Factor;
    } Cases[] = {{0x800, 2}, {0x900, 3}, {0xa00, 4}, {0xb00, 5}, {0xc00, 6}};
    const int Sizes[][2] = {{64, 64}, {128, 32}, {32, 128}, {96, 80}, {40, 56}};
    for (const auto & Case : Cases)
    {
        // (no cache: every call filters)
        if (!txfilter_init(8192, 8192, 32, Case.Option, 0, Cache, Dump, Pack, L"TXSLICES-TEST", nullptr))
        {
            printf("FAIL: init\n");
            return 1;
        }
        int Different = 0, Textures = 0;
        for (const auto & Size : Sizes)
        {
            const int W = Size[0], H = Size[1];
            std::vector<uint32_t> Source((size_t)W * H);
            uint32_t Seed = (uint32_t)(W * 31 + H);
            const uint32_t Colours[3] = {0xFF3050A0u, 0xFFE0C040u, 0xFF40E080u};
            for (int y = 0; y < H; y++)
            {
                for (int x = 0; x < W; x++)
                {
                    Seed = Seed * 1664525u + 1013904223u;
                    Source[(size_t)y * W + x] = Colours[((x + y) / 5 + (Seed >> 29) % 2) % 3];
                }
            }
            std::vector<uint32_t> Expected((size_t)W * H * Case.Factor * Case.Factor);
            xbrz::scale(Case.Factor, Source.data(), Expected.data(), W, H, xbrz::ColorFormat::ABGR);
            GHQTexInfo Info;
            memset(&Info, 0, sizeof Info);
            if (!txfilter_filter((uint8_t *)Source.data(), W, H, (uint16_t)u32(graphics::internalcolorFormat::RGBA8), 0, N64FormatSize(2, 2), &Info) || Info.data == nullptr ||
                Info.width != (unsigned)(W * Case.Factor) || Info.height != (unsigned)(H * Case.Factor))
            {
                printf("FAIL: %dx%d at %dx did not come out %dx%d\n", W, H, Case.Factor, W * Case.Factor, H * Case.Factor);
                Failures++;
                continue;
            }
            Textures++;
            Different += memcmp(Info.data, Expected.data(), Expected.size() * 4) != 0 ? 1 : 0;
        }
        printf("%s %dxBRZ: %d textures, %d different from the whole image scaled at once\n", Different == 0 ? "ok" : "FAIL", Case.Factor, Textures, Different);
        Failures += Different;
        txfilter_shutdown();
    }
    return Failures;
}

int main()
{
    int Failures = CheckSlices();
    // 6xBRZ and 4xBRZ, the cache plain and compressed (as the plugin has it by default), three sizes of cache
    for (int Enhancement : {0xc00, 0xa00, 0x800})
    {
        for (int Compressed : {0, 1})
        {
            for (int LimitMB : {5, 20, 50})
            {
                const int Options = Enhancement | (Compressed ? 0x00400000 : 0);
                const int Rc = Run(Options, LimitMB, 5000);
                printf("%s enhancement %03x, %s cache, limit %d MB\n", Rc == 0 ? "ok" : "FAIL", Enhancement, Compressed ? "compressed" : "plain", LimitMB);
                Failures += Rc;
            }
        }
    }
    printf(Failures == 0 ? "PASS txcache\n" : "FAILED txcache\n");
    return Failures == 0 ? 0 : 1;
}
