// Qt6 counterpart of the Windows configuration property sheet in Config.cpp
// (COptionsSheet: CConfigBasicPage, CConfigEmuSettings, CDebugSettings and
// CConfigTextureEnhancement). Same controls, labels, tooltips (extracted from
// Config.cpp by Scripts/linux/gen_video_tooltips.py), CSettings accessors and
// Apply semantics; only the toolkit differs.
//
// It is built as a separate module (libProject64-video-qtconfig.so) that
// DllConfig() loads on demand, so the video plugin itself has no Qt
// dependency and keeps loading in frontends without Qt (the CLI).
#include <cstdlib>
#include "Gfx_1.3.h"
#include "Config.h"
#include "ScreenResolution.h"
#include "SettingsID.h"
#include "trace.h"
#include <Common/StdString.h>
#include <Settings/Settings.h>

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <algorithm>
#include <QVBoxLayout>
#include <map>
#include <string>
#include <vector>

namespace
{
const std::map<std::string, const char *> & Tooltips()
{
    static const std::map<std::string, const char *> Map = {
#include "ConfigTooltips.inc"
    };
    return Map;
}

void Tip(const char * Id, QWidget * Widget, QWidget * Label = nullptr)
{
    auto Found = Tooltips().find(Id);
    if (Found == Tooltips().end())
    {
        return;
    }
    Widget->setToolTip(QString::fromUtf8(Found->second));
    if (Label != nullptr)
    {
        Label->setToolTip(QString::fromUtf8(Found->second));
    }
}

QCheckBox * Check(QWidget * Parent, const char * Text, bool Checked, const char * TipId = nullptr)
{
    QCheckBox * C = new QCheckBox(QString::fromUtf8(Text), Parent);
    C->setChecked(Checked);
    if (TipId != nullptr)
    {
        Tip(TipId, C);
    }
    return C;
}

void AddItem(QComboBox * Combo, const char * Text, uint32_t Value)
{
    Combo->addItem(QString::fromUtf8(Text), QVariant::fromValue<uint>(Value));
}

// SetComboBoxIndex() of Config.cpp: select the entry whose data matches.
void SelectData(QComboBox * Combo, uint32_t Value)
{
    int Index = Combo->findData(QVariant::fromValue<uint>(Value));
    Combo->setCurrentIndex(Index >= 0 ? Index : 0);
}

uint32_t CurrentData(const QComboBox * Combo)
{
    return Combo->currentData().toUInt();
}

// One page of the sheet: knows how to apply its controls to g_settings.
class Page : public QWidget
{
public:
    explicit Page(QWidget * Parent) :
        QWidget(Parent)
    {
    }
    virtual void Apply() = 0;
};

// ---------------------------------------------------------------- Basic

class BasicPage : public Page
{
public:
    explicit BasicPage(QWidget * Parent) :
        Page(Parent)
    {
        QVBoxLayout * Layout = new QVBoxLayout(this);

        QGroupBox * Rendering = new QGroupBox("Rendering", this);
        QFormLayout * RenderForm = new QFormLayout(Rendering);
        m_WindowRes = new QComboBox(Rendering);
        for (uint32_t i = 0, n = GetScreenResolutionCount(); i < n; i++)
        {
            m_WindowRes->addItem(QString::fromUtf8(GetScreenResolutionName(i)), QVariant::fromValue<uint>(i));
        }
        SelectData(m_WindowRes, g_settings->ScreenRes());
        QLabel * WindowResText = new QLabel("Windowed Resolution", Rendering);
        Tip("IDC_CMB_WINDOW_RES", m_WindowRes, WindowResText);
        RenderForm->addRow(WindowResText, m_WindowRes);
        m_VSync = Check(Rendering, "Vertical sync", g_settings->vsync(), "IDC_CHK_VERTICAL_SYNC");
        RenderForm->addRow(m_VSync);
        if (getenv("VORTEX_VSYNC_MANAGED") != nullptr)
        {
            // Vortex64 decides (Settings > Graphics: Automatic, On, Off): this box would do nothing
            m_VSync->setEnabled(false);
            m_VSync->setToolTip(QString::fromUtf8("Vortex64: Ajustes → Gráficos / Settings → Graphics"));
        }
        Layout->addWidget(Rendering);

        QGroupBox * OpenGL = new QGroupBox("OpenGL settings", this);
        QFormLayout * GlForm = new QFormLayout(OpenGL);
        m_FsRes = new QComboBox(OpenGL);
        int32_t Size = 0;
        const char ** Res = getFullScreenResList(&Size);
        if (Res != nullptr && Size != 0)
        {
            for (int32_t r = 0; r < Size; r++)
            {
                m_FsRes->addItem(QString::fromUtf8(Res[r]));
            }
            m_FsRes->setCurrentIndex(g_settings->FullScreenRes() < (uint32_t)Size ? g_settings->FullScreenRes() : 0);
            // one entry: there is no display mode to choose (the frontend's full screen follows the screen)
            m_FsRes->setEnabled(Size > 1);
        }
        QLabel * FsResText = new QLabel("Full screen resolution:", OpenGL);
        Tip("IDC_CMB_FS_RESOLUTION", m_FsRes, FsResText);
        GlForm->addRow(FsResText, m_FsRes);
        m_Anisotropic = Check(OpenGL, "Anisotropic filtering", g_settings->wrpAnisotropic(), "IDC_CBXANISOTROPIC");
        GlForm->addRow(m_Anisotropic);
        m_AutoVram = Check(OpenGL, "Autodetect VRAM size", g_settings->wrpVRAM() == 0, "IDC_CHK_AUTODETECT_VRAM");
        m_Vram = new QSpinBox(OpenGL);
        m_Vram->setRange(0, 1 << 30);
        m_Vram->setSuffix(" Mb");
        QHBoxLayout * VramRow = new QHBoxLayout;
        VramRow->addWidget(m_AutoVram);
        VramRow->addWidget(m_Vram);
        GlForm->addRow(VramRow);
        Layout->addWidget(OpenGL);

        QGroupBox * Fb = new QGroupBox("Frame buffer emulation", this);
        QVBoxLayout * FbLayout = new QVBoxLayout(Fb);
        m_Fbo = Check(Fb, "Use frame buffer objects", g_settings->wrpFBO(), "IDC_CHK_USE_FRAME_BUFFER_OBJECT");
        FbLayout->addWidget(m_Fbo);
        Layout->addWidget(Fb);

        QGroupBox * Other = new QGroupBox("Other", this);
        QVBoxLayout * OtherLayout = new QVBoxLayout(Other);
        m_TexOptions = Check(Other, "Show texture enhancement options", g_settings->texenh_options());
        OtherLayout->addWidget(m_TexOptions);
        Layout->addWidget(Other);
        Layout->addStretch(1);

        connect(m_AutoVram, &QCheckBox::toggled, this, [this]() { AutoDetectChanged(); });
        AutoDetectChanged();
    }

    void Apply() override
    {
        g_settings->SetScreenRes(CurrentData(m_WindowRes));
        g_settings->SetVsync(m_VSync->isChecked());
        g_settings->SetTexenhOptions(m_TexOptions->isChecked());
        g_settings->SetFullScreenRes(m_FsRes->currentIndex());
        g_settings->SetWrpAnisotropic(m_Anisotropic->isChecked());
        g_settings->SetWrpVRAM(m_AutoVram->isChecked() ? 0 : m_Vram->value());
        g_settings->SetWrpFBO(m_Fbo->isChecked());
        if (g_settings->dirty())
        {
            g_settings->WriteSettings();
        }
    }

private:
    // CConfigBasicPage::AutoDetectChanged
    void AutoDetectChanged()
    {
        const bool Auto = m_AutoVram->isChecked();
        m_Vram->setEnabled(!Auto);
        if (!Auto)
        {
            m_Vram->setValue(g_settings->wrpVRAM() != 0 ? g_settings->wrpVRAM() : 32);
        }
    }

    QComboBox * m_WindowRes;
    QComboBox * m_FsRes;
    QCheckBox * m_VSync;
    QCheckBox * m_Anisotropic;
    QCheckBox * m_AutoVram;
    QSpinBox * m_Vram;
    QCheckBox * m_Fbo;
    QCheckBox * m_TexOptions;
};

// ------------------------------------------------------------ Emulation

class EmuPage : public Page
{
public:
    explicit EmuPage(QWidget * Parent) :
        Page(Parent)
    {
        QVBoxLayout * Layout = new QVBoxLayout(this);
        QLabel * Info = new QLabel(g_romopen ? "Current game emulation settings. Change with care!" : "Default emulation settings. Not recommended to change!", this);
        Layout->addWidget(Info);

        QGroupBox * General = new QGroupBox("General options", this);
        QFormLayout * Form = new QFormLayout(General);
        m_Filtering = Combo(General, Form, "Filtering mode:", "IDC_TXT_FILTERING_MODE");
        AddItem(m_Filtering, "Automatic", CSettings::Filter_Automatic);
        AddItem(m_Filtering, "Force Bilinear", CSettings::Filter_ForceBilinear);
        AddItem(m_Filtering, "Force Point-sampled", CSettings::Filter_ForcePointSampled);
        SelectData(m_Filtering, (uint32_t)g_settings->filtering());

        m_Swap = Combo(General, Form, "Buffer swapping method:", "IDC_TXT_BUFFER_SWAPPING");
        AddItem(m_Swap, "Old", CSettings::SwapMode_Old);
        AddItem(m_Swap, "New", CSettings::SwapMode_New);
        AddItem(m_Swap, "Hybrid", CSettings::SwapMode_Hybrid);
        SelectData(m_Swap, g_settings->swapmode());

        m_Lod = Combo(General, Form, "LOD calculation:", "IDC_TXT_LOD_CALC");
        AddItem(m_Lod, "off", CSettings::LOD_Off);
        AddItem(m_Lod, "fast", CSettings::LOD_Fast);
        AddItem(m_Lod, "precise", CSettings::LOD_Precise);
        SelectData(m_Lod, g_settings->lodmode());

        m_Aspect = Combo(General, Form, "Aspect ratio:", "IDC_TXT_ASPECT_RATIO");
        AddItem(m_Aspect, "4:3 (default)", CSettings::Aspect_4x3);
        AddItem(m_Aspect, "Force 16:9", CSettings::Aspect_16x9);
        AddItem(m_Aspect, "Stretch", CSettings::Aspect_Stretch);
        AddItem(m_Aspect, "Original", CSettings::Aspect_Original);
        SelectData(m_Aspect, (uint32_t)g_settings->aspectmode());

        m_Fog = Check(General, "Fog", g_settings->fog(), "IDC_CHK_FOG");
        m_BufferClear = Check(General, "Buffer clear on every frame", g_settings->buff_clear(), "IDC_CHK_BUFFER_CLEAR");
        m_FbEnable = Check(General, "Enable frame buffer emulation", g_settings->fb_emulation_enabled(), "IDC_CHK_FRAME_BUFFER_EMULATION");
        Form->addRow(m_Fog);
        Form->addRow(m_BufferClear);
        Form->addRow(m_FbEnable);
        Layout->addWidget(General);

        QGroupBox * Fb = new QGroupBox("Frame buffer emulation", this);
        QVBoxLayout * FbLayout = new QVBoxLayout(Fb);
        m_Hwfbe = Check(Fb, "Hardware frame buffer emulation", g_settings->fb_hwfbe_set(), "IDC_CHK_HARDWARE_FRAMEBUFFER");
        m_GetFbi = Check(Fb, "Get frame buffer info", g_settings->fb_get_info_enabled(), "IDC_CHK_GET_FRAMEBUFFER");
        m_ReadEvery = Check(Fb, "Read every frame (slow!)", g_settings->fb_ref_enabled(), "IDC_CHK_READ_EVERY_FRAME");
        m_AsTexture = Check(Fb, "Render N64 frame buffer as texture", g_settings->fb_read_back_to_screen_enabled(), "IDC_RENDER_FRAME_AS_TEXTURE");
        m_DetectWrite = Check(Fb, "Detect CPU write to the N64 frame buffer", g_settings->fb_cpu_write_hack_enabled(), "IDC_CHK_DETECT_CPU_WRITE");
        for (QCheckBox * C : {m_Hwfbe, m_GetFbi, m_ReadEvery, m_AsTexture, m_DetectWrite})
        {
            FbLayout->addWidget(C);
        }
        Layout->addWidget(Fb);

        QGroupBox * Depth = new QGroupBox("Depth buffer emulation", this);
        QVBoxLayout * DepthLayout = new QVBoxLayout(Depth);
        m_SoftDepth = Check(Depth, "Software depth buffer rendering", g_settings->fb_depth_render_enabled(), "IDC_SOFTWARE_DEPTH_BUFFER");
        DepthLayout->addWidget(m_SoftDepth);
        Layout->addWidget(Depth);
        Layout->addStretch(1);
    }

    void Apply() override
    {
        g_settings->SetFiltering((CSettings::Filtering_t)CurrentData(m_Filtering));
        g_settings->SetAspectmode((CSettings::AspectMode_t)CurrentData(m_Aspect));
        g_settings->SetSwapMode((CSettings::SwapMode_t)CurrentData(m_Swap));
        g_settings->SetFog(m_Fog->isChecked());
        g_settings->SetBuffClear(m_BufferClear->isChecked());
        g_settings->SetLODmode((CSettings::PixelLevelOfDetail_t)CurrentData(m_Lod));

        QCheckBox * FbButtons[] = {m_FbEnable, m_Hwfbe, m_ReadEvery, m_AsTexture, m_DetectWrite, m_GetFbi, m_SoftDepth};
        CSettings::fb_bits_t Bits[] = {
            CSettings::fb_emulation, CSettings::fb_hwfbe, CSettings::fb_ref,
            CSettings::fb_read_back_to_screen, CSettings::fb_cpu_write_hack,
            CSettings::fb_get_info, CSettings::fb_depth_render};
        uint32_t AddBits = 0, RemoveBits = 0;
        for (size_t i = 0; i < sizeof(FbButtons) / sizeof(FbButtons[0]); i++)
        {
            if (FbButtons[i]->isChecked())
            {
                AddBits |= Bits[i];
            }
            else
            {
                RemoveBits |= Bits[i];
            }
        }
        g_settings->UpdateFrameBufferBits(AddBits, RemoveBits);
        if (g_settings->dirty())
        {
            g_settings->WriteSettings();
        }
    }

private:
    static QComboBox * Combo(QWidget * Parent, QFormLayout * Form, const char * Text, const char * TipId)
    {
        QLabel * Label = new QLabel(QString::fromUtf8(Text), Parent);
        QComboBox * C = new QComboBox(Parent);
        Tip(TipId, C, Label);
        Form->addRow(Label, C);
        return C;
    }

    QComboBox *m_Filtering, *m_Swap, *m_Lod, *m_Aspect;
    QCheckBox *m_Fog, *m_BufferClear, *m_FbEnable, *m_Hwfbe, *m_GetFbi, *m_ReadEvery, *m_AsTexture, *m_DetectWrite, *m_SoftDepth;
};

// ---------------------------------------------------------------- Debug

class DebugPage : public Page
{
public:
    explicit DebugPage(QWidget * Parent) :
        Page(Parent)
    {
        QVBoxLayout * Layout = new QVBoxLayout(this);
        QGroupBox * Tracing = new QGroupBox("Tracing", this);
        QFormLayout * Form = new QFormLayout(Tracing);
        struct
        {
            const char * Text;
            uint16_t SettingId;
        } Rows[] = {
            {"Settings:", Set_Logging_Settings},
            {"Unknown:", Set_Logging_Unknown},
            {"Glide64:", Set_Logging_Glide64},
            {"Interface:", Set_Logging_Interface},
            {"Resolution:", Set_Logging_Resolution},
            {"Glitch:", Set_Logging_Glitch},
            {"RDP:", Set_Logging_VideoRDP},
            {"TLUT:", Set_Logging_TLUT},
            {"PNG:", Set_Logging_PNG},
            {"OGLWrapper:", Set_Logging_OGLWrapper},
            {"RDP Commands:", Set_Logging_RDPCommands},
        };
        for (const auto & Row : Rows)
        {
            QComboBox * C = new QComboBox(Tracing);
            AddItem(C, "Error", TraceError);
            AddItem(C, "Warning", TraceWarning);
            AddItem(C, "Notice", TraceNotice);
            AddItem(C, "Info", TraceInfo);
            AddItem(C, "Debug", TraceDebug);
            AddItem(C, "Verbose", TraceVerbose);
            SelectData(C, (uint32_t)GetSetting(Row.SettingId));
            Form->addRow(QString::fromUtf8(Row.Text), C);
            m_Combos.push_back({C, Row.SettingId});
        }
        Layout->addWidget(Tracing);
        Layout->addStretch(1);
    }

    void Apply() override
    {
        for (const auto & Entry : m_Combos)
        {
            SetSetting(Entry.second, CurrentData(Entry.first));
        }
    }

private:
    std::vector<std::pair<QComboBox *, uint16_t>> m_Combos;
};

// ---------------------------------------------------- Texture enhancement

class TexturePage : public Page
{
public:
    explicit TexturePage(QWidget * Parent) :
        Page(Parent)
    {
        QGridLayout * Grid = new QGridLayout(this);

        QGroupBox * Enh = new QGroupBox("Texture enhancement", this);
        QFormLayout * EnhForm = new QFormLayout(Enh);
        m_Filter = new QComboBox(Enh);
        AddItem(m_Filter, "None", CSettings::TextureFilter_None);
        AddItem(m_Filter, "Smooth filtering 1", CSettings::TextureFilter_SmoothFiltering);
        AddItem(m_Filter, "Smooth filtering 2", CSettings::TextureFilter_SmoothFiltering2);
        AddItem(m_Filter, "Smooth filtering 3", CSettings::TextureFilter_SmoothFiltering3);
        AddItem(m_Filter, "Smooth filtering 4", CSettings::TextureFilter_SmoothFiltering4);
        AddItem(m_Filter, "Sharp filtering 1", CSettings::TextureFilter_SharpFiltering1);
        AddItem(m_Filter, "Sharp filtering 2", CSettings::TextureFilter_SharpFiltering2);
        SelectData(m_Filter, g_settings->ghq_fltr());
        QLabel * FilterText = new QLabel("Filter", Enh);
        Tip("IDC_CMB_ENH_FILTER", m_Filter, FilterText);
        EnhForm->addRow(FilterText, m_Filter);

        m_Enhancement = new QComboBox(Enh);
        AddItem(m_Enhancement, "None", CSettings::TextureEnht_None);
        AddItem(m_Enhancement, "X2", CSettings::TextureEnht_X2);
        AddItem(m_Enhancement, "X2SAI", CSettings::TextureEnht_X2SAI);
        AddItem(m_Enhancement, "HQ2X", CSettings::TextureEnht_HQ2X);
        AddItem(m_Enhancement, "HQ2XS", CSettings::TextureEnht_HQ2XS);
        AddItem(m_Enhancement, "LQ2X", CSettings::TextureEnht_LQ2X);
        AddItem(m_Enhancement, "LQ2XS", CSettings::TextureEnht_LQ2XS);
        AddItem(m_Enhancement, "HQ4X", CSettings::TextureEnht_HQ4X);
        SelectData(m_Enhancement, g_settings->ghq_enht());
        QLabel * EnhText = new QLabel("Enhancement", Enh);
        Tip("IDC_CMB_ENHANCEMENT", m_Enhancement, EnhText);
        EnhForm->addRow(EnhText, m_Enhancement);

        m_CacheSize = new QSpinBox(Enh);
        m_CacheSize->setRange(0, 1 << 30);
        m_CacheSize->setSuffix(" mb");
        m_CacheSize->setValue(g_settings->ghq_cache_size());
        QLabel * CacheText = new QLabel("Texture cache", Enh);
        Tip("IDC_TXT_TEXTURE_CACHE", m_CacheSize, CacheText);
        EnhForm->addRow(CacheText, m_CacheSize);
        m_EnhCompressCache = Check(Enh, "Compress texture cache", g_settings->ghq_enht_gz(), "IDC_CHK_COMPRESS_CACHE");
        EnhForm->addRow(m_EnhCompressCache);

        QGroupBox * Perf = new QGroupBox("Performance tweaks", this);
        QVBoxLayout * PerfLayout = new QVBoxLayout(Perf);
        m_EnhCompression = Check(Perf, "Apply texture compression", g_settings->ghq_enht_cmpr(), "IDC_CHK_TEX_COMPRESSION");
        m_IgnoreBg = Check(Perf, "Ignore Backgrounds", g_settings->ghq_enht_nobg(), "IDC_CHK_IGNORE_BACKGROUND");
        PerfLayout->addWidget(m_EnhCompression);
        PerfLayout->addWidget(m_IgnoreBg);

        QGroupBox * Hires = new QGroupBox("Hi-resolution textures", this);
        QFormLayout * HiForm = new QFormLayout(Hires);
        m_HrsFormat = new QComboBox(Hires);
        AddItem(m_HrsFormat, "None", CSettings::HiResPackFormat_None);
        AddItem(m_HrsFormat, "Rice format", CSettings::HiResPackFormat_Riceformat);
        SelectData(m_HrsFormat, g_settings->ghq_hirs());
        QLabel * FormatText = new QLabel("Format", Hires);
        Tip("IDC_CMB_FORMAT_CHOICES", m_HrsFormat, FormatText);
        HiForm->addRow(FormatText, m_HrsFormat);
        m_AltCrc = Check(Hires, "Alternative CRC calculation", g_settings->ghq_hirs_altcrc(), "IDC_CHK_ALT_CRC");
        m_TexEdit = Check(Hires, "Texture dumping/editing mode", g_settings->ghq_hirs_dump(), "IDC_CHK_TEX_DUMP_EDIT");
        m_LetFly = Check(Hires, "Use alpha channel fully", g_settings->ghq_hirs_let_texartists_fly(), "IDC_CHK_USE_ALPHA_FULLY");
        m_HrsCompressCache = Check(Hires, "Compress texture cache", g_settings->ghq_hirs_gz(), "IDC_CHK_HRS_COMPRESS_CACHE");
        for (QCheckBox * C : {m_AltCrc, m_TexEdit, m_LetFly, m_HrsCompressCache})
        {
            HiForm->addRow(C);
        }
        if (g_settings->ghq_hirs_dump())
        {
            m_AltCrc->setEnabled(false);
        }

        QGroupBox * HiPerf = new QGroupBox("Performance tweaks", this);
        QVBoxLayout * HiPerfLayout = new QVBoxLayout(HiPerf);
        m_HrsCompression = Check(HiPerf, "Apply texture compression", g_settings->ghq_hirs_cmpr(), "IDC_CHK_HIRES_TEX_COMPRESSION");
        m_Force16 = Check(HiPerf, "Force 16bpp textures", g_settings->ghq_hirs_f16bpp(), "IDC_CHK_FORCE_16BPP_TEXT");
        m_Tile = Check(HiPerf, "Tile textures", g_settings->ghq_hirs_tile(), "IDC_CHK_TILE_TEX");
        for (QCheckBox * C : {m_HrsCompression, m_Force16, m_Tile})
        {
            HiPerfLayout->addWidget(C);
        }

        QGroupBox * Common = new QGroupBox("Common", this);
        QFormLayout * CommonForm = new QFormLayout(Common);
        m_Compression = new QComboBox(Common);
        AddItem(m_Compression, "S3TC", CSettings::TextureCompression_S3TC);
        AddItem(m_Compression, "FXT1", CSettings::TextureCompression_FXT1);
        SelectData(m_Compression, g_settings->ghq_cmpr());
        CommonForm->addRow("Texture compression method", m_Compression);
        m_SaveCache = Check(Common, "Save texture cache to hard disk", g_settings->ghq_cache_save(), "IDC_CHK_TEX_CACHE_HD");
        CommonForm->addRow(m_SaveCache);

        Grid->addWidget(Enh, 0, 0);
        Grid->addWidget(Perf, 1, 0);
        Grid->addWidget(Hires, 0, 1);
        Grid->addWidget(HiPerf, 1, 1);
        Grid->addWidget(Common, 2, 0, 1, 2);
    }

    void Apply() override
    {
        g_settings->SetGhqFltr((CSettings::TextureFilter_t)CurrentData(m_Filter));
        g_settings->SetGhqEnht((CSettings::TextureEnhancement_t)CurrentData(m_Enhancement));
        g_settings->SetGhqCacheSize(m_CacheSize->value());
        g_settings->SetGhqEnhtNobg(m_IgnoreBg->isChecked());
        g_settings->SetGhqEnhtCmpr(m_EnhCompression->isChecked());
        g_settings->SetGhqEnhtGz(m_EnhCompressCache->isChecked());
        g_settings->SetGhqHirs((CSettings::HiResPackFormat_t)CurrentData(m_HrsFormat));
        g_settings->SetGhqHirsTile(m_Tile->isChecked());
        g_settings->SetGhqHirsF16bpp(m_Force16->isChecked());
        g_settings->SetGhqHirsDump(m_TexEdit->isChecked());
        g_settings->SetGhqHirsAltcrc(m_AltCrc->isChecked());
        g_settings->SetGhqHirsCmpr(m_HrsCompression->isChecked());
        g_settings->SetGhqHirsGz(m_HrsCompressCache->isChecked());
        g_settings->SetGhqHirsLetTexartistsFly(m_LetFly->isChecked());
        g_settings->SetGhqCmpr((CSettings::TextureCompression_t)CurrentData(m_Compression));
        g_settings->SetGhqCacheSave(m_SaveCache->isChecked());
        if (g_settings->dirty())
        {
            g_settings->WriteSettings();
        }
    }

private:
    QComboBox *m_Filter, *m_Enhancement, *m_HrsFormat, *m_Compression;
    QSpinBox * m_CacheSize;
    QCheckBox *m_IgnoreBg, *m_EnhCompression, *m_HrsCompression, *m_EnhCompressCache, *m_Tile, *m_Force16, *m_TexEdit, *m_AltCrc,
        *m_HrsCompressCache, *m_LetFly, *m_SaveCache;
};

// ---------------------------------------------------------------- Sheet

// COptionsSheet: Basic always; Emulation with advanced options; Debug with the
// debugger; Texture enhancement while "Show texture enhancement options" is set.
class Sheet : public QDialog
{
public:
    explicit Sheet(QWidget * Parent) :
        QDialog(Parent),
        m_Tabs(new QTabWidget(this))
    {
        setWindowTitle("Glide64 settings");
        m_Basic = new BasicPage(this);
        m_Tabs->addTab(m_Basic, "Basic");
        m_Pages.push_back(m_Basic);
        if (g_settings->advanced_options())
        {
            AddPage(new EmuPage(this), "Emulation settings");
        }
        if (g_settings->debugger_enabled())
        {
            AddPage(new DebugPage(this), "Debug settings");
        }
        if (g_settings->texenh_options())
        {
            m_Texture = new TexturePage(this);
            AddPage(m_Texture, "Texture enhancement");
        }

        QDialogButtonBox * Buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply, this);
        connect(Buttons, &QDialogButtonBox::accepted, this, [this]() { ApplyAll(); accept(); });
        connect(Buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        connect(Buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, [this]() { ApplyAll(); });

        QVBoxLayout * Layout = new QVBoxLayout(this);
        Layout->addWidget(m_Tabs);
        Layout->addWidget(Buttons);
    }

private:
    void AddPage(Page * P, const char * Title)
    {
        m_Tabs->addTab(P, Title);
        m_Pages.push_back(P);
    }

    void ApplyAll()
    {
        for (Page * P : m_Pages)
        {
            P->Apply();
        }
        // "Show texture enhancement options": the page comes and goes with Apply (COptionsSheet)
        if (g_settings->texenh_options() && m_Texture == nullptr)
        {
            m_Texture = new TexturePage(this);
            AddPage(m_Texture, "Texture enhancement");
        }
        else if (!g_settings->texenh_options() && m_Texture != nullptr)
        {
            m_Tabs->removeTab(m_Tabs->indexOf(m_Texture));
            m_Pages.erase(std::remove(m_Pages.begin(), m_Pages.end(), static_cast<Page *>(m_Texture)), m_Pages.end());
            delete m_Texture;
            m_Texture = nullptr;
        }
    }

    QTabWidget * m_Tabs;
    BasicPage * m_Basic;
    TexturePage * m_Texture = nullptr;
    std::vector<Page *> m_Pages;
};
} // namespace

// Called by DllConfig() (Config.cpp) on the frontend's GUI thread.
extern "C" __attribute__((visibility("default"))) void PJ64VideoQtConfig(void * hParent)
{
    (void)hParent;
    if (QApplication::instance() == nullptr)
    {
        // Console frontend: there is no Qt event loop to show a dialog in.
        return;
    }
    Sheet Dialog(nullptr);
    Dialog.exec();
}
