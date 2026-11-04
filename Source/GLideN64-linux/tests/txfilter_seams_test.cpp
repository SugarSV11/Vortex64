// Texture filters with several threads against the whole image. The texture enhancement cuts a texture in bands of rows for its threads; the
// result must be the one of the whole image, row for row, whatever the filter (every enhancement, smooth, sharp, deposterization and their
// combinations) and wherever the cuts fall. The reference is the same call with ONE thread (the whole image filtered at once, which is what
// the code of the filters does by itself); the candidate is the same call with 2, 3, 5 and 8 threads. The textures are made to show a seam:
// lines, diagonals, checkerboards, alpha, posterized gradients, single pixels and edges ON the rows where the bands are cut.
//
//   txfilter-seams-test            (exit 0: no difference)
//   txfilter-seams-original-test   (the filter of the submodule: must FAIL, shows that the test sees the seams)
#include <cstdint>
#include "GLideNHQ/TxFilter.h"
#include "GLideNHQ/TxFilterExport.h"
#include <Graphics/Parameters.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <chrono>

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

typedef std::vector<uint32_t> Image;

static uint32_t Rgba(int R, int G, int B, int A = 255) { return (uint32_t)R | ((uint32_t)G << 8) | ((uint32_t)B << 16) | ((uint32_t)A << 24); }

// W x H pixels of the pattern Kind, a different one in each (Seed varies the details)
static Image MakeImage(int Kind, int W, int H, uint32_t Seed)
{
    Image Img((size_t)W * H);
    auto Rnd = [&]() {
        Seed = Seed * 1664525u + 1013904223u;
        return Seed >> 8;
    };
    for (int y = 0; y < H; y++)
    {
        for (int x = 0; x < W; x++)
        {
            uint32_t P = 0;
            switch (Kind)
            {
            case 0: // diagonals of two colours (every angle crosses every cut)
                P = ((x + y) % 7 < 3) ? Rgba(250, 40, 40) : Rgba(30, 30, 220);
                break;
            case 1: // the other diagonal, thin, over a background
                P = ((x - y + 4096) % 5 == 0) ? Rgba(255, 255, 255) : Rgba(20, 90, 20);
                break;
            case 2: // checkerboard of single pixels with a patch of larger squares
                P = (((x ^ y) & 1) ? Rgba(240, 240, 0) : Rgba(0, 0, 0));
                if (x > W / 2 && y > H / 3)
                {
                    P = ((((x / 3) ^ (y / 3)) & 1) ? Rgba(0, 200, 200) : Rgba(120, 0, 120));
                }
                break;
            case 3: // alpha: stripes of different opacity over colour edges
                P = Rgba(x * 255 / (W > 1 ? W - 1 : 1), 120, 200 - (y % 5) * 30, ((y / 3) % 2) ? 255 : ((x % 4) * 80));
                break;
            case 4: // gradients quantized to 4 bits (what deposterization is for): bands that the filter smooths
                P = Rgba((x * 16 / W) * 17, (y * 16 / H) * 17, ((x + y) * 16 / (W + H)) * 17);
                break;
            case 5: // a horizontal line on every row that is a multiple of 4 and of 8, plus single pixels
                P = ((y % 4 == 0) ? ((y % 8 == 0) ? Rgba(255, 0, 0) : Rgba(0, 255, 0)) : Rgba(40, 40, 60));
                if ((x * 7 + y * 13) % 29 == 0)
                {
                    P = Rgba(255, 255, 0);
                }
                break;
            case 6: // two rows of an edge on both sides of the rows 4k-1/4k/4k+1 (where a cut with the halo of a filter would be)
                P = (((y + 1) / 4) % 2) ? Rgba(220, 220, 220) : Rgba(15, 15, 15);
                if (x % 9 == 4)
                {
                    P = Rgba(200, 30, 130);
                }
                break;
            default: // noise of few colours with soft edges (the worst case for xBRZ)
            {
                const uint32_t Colours[4] = {Rgba(0x30, 0x50, 0xA0), Rgba(0xE0, 0xC0, 0x40), Rgba(0x40, 0xE0, 0x80), Rgba(0xF0, 0xF0, 0xF0, 128)};
                P = Colours[((x + y) / 4 + (Rnd() >> 12) % 2) % 4];
                break;
            }
            }
            Img[(size_t)y * W + x] = P;
        }
    }
    return Img;
}

struct Out
{
    int W = 0, H = 0;
    Image Data;
    bool Ok = false;
};

struct Case
{
    int W, H;
    Image Source;
};

// every texture of Cases through one filter set up with Options and Threads (the number of threads is read when the filter starts)
static double FilterMs = 0; // time of the filtering alone in the last RunAll
static std::vector<Out> RunAll(const std::vector<Case> & Cases, int Options, int Threads, int MaxSize)
{
    std::vector<Out> Results(Cases.size());
    char Number[8];
    snprintf(Number, sizeof Number, "%d", Threads);
    setenv("VORTEX_TXFILTER_THREADS", Number, 1);
    char Dir[] = "/tmp/vortex-txseams-XXXXXX";
    if (mkdtemp(Dir) == nullptr)
    {
        return Results;
    }
    wchar_t Cache[256], Dump[256], Pack[256];
    swprintf(Cache, 256, L"%s/cache", Dir);
    swprintf(Dump, 256, L"%s/dump", Dir);
    swprintf(Pack, 256, L"%s/pack", Dir);
    // no cache: every call filters
    if (txfilter_init(MaxSize, MaxSize, 32, Options, 0, Cache, Dump, Pack, L"TXSEAMS-TEST", nullptr))
    {
        const auto Start = std::chrono::steady_clock::now();
        for (size_t i = 0; i < Cases.size(); i++)
        {
            Image Copy = Cases[i].Source;
            GHQTexInfo Info;
            memset(&Info, 0, sizeof Info);
            if (txfilter_filter((uint8_t *)Copy.data(), Cases[i].W, Cases[i].H, (uint16_t)u32(graphics::internalcolorFormat::RGBA8), 0, N64FormatSize(2, 2), &Info) &&
                Info.data != nullptr)
            {
                Results[i].W = (int)Info.width;
                Results[i].H = (int)Info.height;
                Results[i].Data.assign((const uint32_t *)Info.data, (const uint32_t *)Info.data + (size_t)Info.width * Info.height);
                Results[i].Ok = true;
            }
        }
        FilterMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - Start).count();
        txfilter_shutdown();
    }
    std::string Rm = std::string("rm -rf ") + Dir;
    if (system(Rm.c_str()) != 0)
    {
        // a leftover in /tmp is not a failure of the test
    }
    return Results;
}

// txfilter-seams-test --bench: time per texture with 1 thread and with the cores (not a test: it measures what the bands cost and gain)
static int Bench()
{
    const int Sizes[] = {16, 32, 64, 128, 256};
    const struct
    {
        int Option;
        const char * Name;
    } Modes[] = {{0xc00, "6xBRZ"}, {0xa00, "4xBRZ"}, {0x500, "HQ4x"}, {0x200, "2xSaI"}, {0xc00 | 0x1000, "6xBRZ+depo"}, {0x300, "HQ2x"}};
    for (const auto & Mode : Modes)
    {
        for (int Size : Sizes)
        {
            std::vector<Case> Cases;
            for (int i = 0; i < 20; i++)
            {
                Cases.push_back({Size, Size, MakeImage(7, Size, Size, (uint32_t)i)});
            }
            double Ms[2] = {0, 0};
            for (int T = 0; T < 2; T++)
            {
                RunAll(Cases, Mode.Option, T == 0 ? 1 : 8, 1024);
                Ms[T] = FilterMs / Cases.size();
            }
            printf("%-11s %3dx%-3d  1 thread %8.3f ms/texture   8 threads %8.3f ms/texture\n", Mode.Name, Size, Size, Ms[0], Ms[1]);
        }
    }
    return 0;
}

int main(int Argc, char ** Argv)
{
    if (Argc > 1 && strcmp(Argv[1], "--bench") == 0)
    {
        return Bench();
    }
    struct Enh
    {
        int Value;
        const char * Name;
    };
    const Enh Enhancements[] = {{0x000, "none"},   {0x100, "2x"},     {0x200, "2xSaI"},   {0x300, "HQ2x"},    {0x400, "LQ2x"},    {0x500, "HQ4x"},   {0x600, "HQ2xS"},
                                {0x700, "LQ2xS"},  {0x800, "2xBRZ"},  {0x900, "3xBRZ"},   {0xa00, "4xBRZ"},   {0xb00, "5xBRZ"},   {0xc00, "6xBRZ"}};
    struct Post
    {
        int Value;
        const char * Name;
    };
    const Post Posts[] = {{0, ""}, {0x01, "+smooth1"}, {0x02, "+smooth2"}, {0x03, "+smooth3"}, {0x04, "+smooth4"}, {0x10, "+sharp1"}, {0x20, "+sharp2"}};
    // sizes: cuts of the bands fall in different places (the bands are multiples of 4 rows of a number of threads that depends on the height)
    const int Sizes[][2] = {{64, 64}, {96, 80}, {32, 128}, {40, 56}, {37, 61}, {128, 32}, {16, 16}, {24, 100}, {160, 144}};
    const int ThreadCounts[] = {2, 3, 5, 8};
    std::vector<Case> Cases;
    for (const auto & Size : Sizes)
    {
        for (int Kind = 0; Kind < 8; Kind++)
        {
            Cases.push_back({Size[0], Size[1], MakeImage(Kind, Size[0], Size[1], (uint32_t)(Kind * 977 + Size[0] * 31 + Size[1]))});
        }
    }
    int Failures = 0, Total = 0;
    for (const Enh & E : Enhancements)
    {
        for (int Depo : {0, 1})
        {
            int Different = 0, Count = 0;
            std::string Where;
            for (const Post & P : Posts)
            {
                const int Options = E.Value | P.Value | (Depo ? 0x1000 : 0);
                if ((Options & 0xfff) == 0)
                {
                    continue;
                }
                const std::vector<Out> Reference = RunAll(Cases, Options, 1, 1024);
                for (int Threads : ThreadCounts)
                {
                    const std::vector<Out> Candidate = RunAll(Cases, Options, Threads, 1024);
                    for (size_t i = 0; i < Cases.size(); i++)
                    {
                        Count++;
                        const bool Same = Reference[i].Ok && Candidate[i].Ok && Reference[i].W == Candidate[i].W && Reference[i].H == Candidate[i].H &&
                                          Reference[i].Data == Candidate[i].Data;
                        if (!Same)
                        {
                            Different++;
                            if (Where.size() < 200)
                            {
                                char Buf[96];
                                snprintf(Buf, sizeof Buf, " [%s %dx%d pattern %d, %d threads%s]", P.Name[0] ? P.Name : "alone", Cases[i].W, Cases[i].H, (int)(i % 8), Threads,
                                         Reference[i].Ok && Candidate[i].Ok ? "" : ", NO OUTPUT");
                                Where += Buf;
                            }
                        }
                    }
                }
            }
            Total += Count;
            Failures += Different;
            printf("%s %-6s%s: %d comparisons, %d different from the whole image%s\n", Different == 0 ? "ok  " : "FAIL", E.Name, Depo ? " +depo" : "", Count, Different,
                   Different ? Where.c_str() : "");
            fflush(stdout);
        }
    }
    printf("%s txfilter seams (%d comparisons)\n", Failures == 0 ? "PASS" : "FAILED", Total);
    return Failures == 0 ? 0 : 1;
}
