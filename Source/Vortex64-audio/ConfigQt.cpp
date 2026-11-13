// Qt6 counterpart of the Windows configuration property sheet in ConfigUI.cpp
// (CAudioUI: CGeneralSettings, CGameSettings, CLogSettings): same controls,
// same AudioSettings accessors and the same pages shown under the same
// conditions. Built as libVortex64-audio-qtconfig.so and loaded on demand by
// DllConfig() so the plugin has no Qt dependency.
#include <stdint.h>
#include "AudioSettings.h"
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
#include <QGroupBox>
#include <QHBoxLayout>
#include <QPushButton>
#include <QSlider>
#include <QTabWidget>
#include <QVBoxLayout>
#include <vector>

extern bool g_romopen;

namespace
{
class Page : public QWidget
{
public:
    explicit Page(QWidget * Parent) :
        QWidget(Parent)
    {
    }
    virtual void Apply() = 0;
};

class GeneralPage : public Page
{
public:
    explicit GeneralPage(QWidget * Parent) :
        Page(Parent)
    {
        QHBoxLayout * Layout = new QHBoxLayout(this);
        QGroupBox * Group = new QGroupBox("Volume", this);
        QVBoxLayout * Box = new QVBoxLayout(Group);
        m_Volume = new QSlider(Qt::Vertical, Group);
        // Same orientation as the trackbar: position 0 (top) is full volume.
        m_Volume->setRange(0, 100);
        m_Volume->setTickInterval(20);
        m_Volume->setTickPosition(QSlider::TicksBothSides);
        m_Volume->setInvertedAppearance(true);
        m_Volume->setValue(100 - g_settings->GetVolume());
        m_Mute = new QCheckBox("Mute", Group);
        m_Mute->setChecked(!g_settings->AudioEnabled());
        Box->addWidget(m_Volume, 1);
        Box->addWidget(m_Mute);
        Layout->addWidget(Group);
        Layout->addStretch(1);
    }
    void Apply() override
    {
        g_settings->SetAudioEnabled(!m_Mute->isChecked());
        g_settings->SetVolume(100 - m_Volume->value());
        FlushSettings();
    }

private:
    QSlider * m_Volume;
    QCheckBox * m_Mute;
};

class GamePage : public Page
{
public:
    explicit GamePage(QWidget * Parent) :
        Page(Parent)
    {
        QVBoxLayout * Layout = new QVBoxLayout(this);
        QFormLayout * Form = new QFormLayout;
        m_Buffer = new QSlider(Qt::Horizontal, this);
        m_Buffer->setRange(1, 7);
        m_Buffer->setTickInterval(1);
        m_Buffer->setTickPosition(QSlider::TicksBelow);
        m_Buffer->setValue(g_settings->GetBuffer());
        Form->addRow("Buffer", m_Buffer);
        Layout->addLayout(Form);
        Layout->addStretch(1);
    }
    void Apply() override
    {
        g_settings->SetBuffer(m_Buffer->value());
        FlushSettings();
    }

private:
    QSlider * m_Buffer;
};

class LogPage : public Page
{
public:
    explicit LogPage(QWidget * Parent) :
        Page(Parent)
    {
        QVBoxLayout * Layout = new QVBoxLayout(this);
        QFormLayout * Form = new QFormLayout;
        struct
        {
            const char * Text;
            uint16_t SettingId;
        } Rows[] = {
            {"Thread:", Set_Logging_Thread},
            {"Path:", Set_Logging_Path},
            {"Init/Shutdown:", Set_Logging_InitShutdown},
            {"Interface:", Set_Logging_Interface},
            {"Audio driver:", Set_Logging_Driver},
        };
        for (const auto & Row : Rows)
        {
            QComboBox * C = new QComboBox(this);
            const struct
            {
                const char * Text;
                uint32_t Value;
            } Levels[] = {{"Error", TraceError}, {"Warning", TraceWarning}, {"Notice", TraceNotice}, {"Info", TraceInfo}, {"Debug", TraceDebug}, {"Verbose", TraceVerbose}};
            for (const auto & L : Levels)
            {
                C->addItem(L.Text, QVariant::fromValue<uint>(L.Value));
            }
            int Index = C->findData(QVariant::fromValue<uint>((uint32_t)GetSetting(Row.SettingId)));
            C->setCurrentIndex(Index >= 0 ? Index : 0);
            Form->addRow(Row.Text, C);
            m_Combos.push_back({C, Row.SettingId});
        }
        Layout->addLayout(Form);
        Layout->addStretch(1);
    }
    void Apply() override
    {
        for (const auto & Entry : m_Combos)
        {
            SetSetting(Entry.second, Entry.first->currentData().toUInt());
        }
        FlushSettings();
    }

private:
    std::vector<std::pair<QComboBox *, uint16_t>> m_Combos;
};

// CAudioUI: General always, Game with a ROM open, Log with the debugger.
class Sheet : public QDialog
{
public:
    Sheet() :
        QDialog(nullptr),
        m_Tabs(new QTabWidget(this))
    {
        setWindowTitle("Settings");
        AddPage(new GeneralPage(this), "General");
        if (g_romopen)
        {
            AddPage(new GamePage(this), "Game");
        }
        if (g_settings->debugger_enabled())
        {
            AddPage(new LogPage(this), "Log");
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
    }
    QTabWidget * m_Tabs;
    std::vector<Page *> m_Pages;
};
} // namespace

extern "C" __attribute__((visibility("default"))) void PJ64AudioQtConfig(void * hParent)
{
    (void)hParent;
    if (QApplication::instance() == nullptr)
    {
        return;
    }
    Sheet Dialog;
    Dialog.exec();
}
