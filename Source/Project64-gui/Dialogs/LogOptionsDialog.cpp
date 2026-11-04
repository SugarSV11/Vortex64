#include "LogOptionsDialog.h"
#include "../Vortex/Strings.h"

#include <Project64-core/Settings.h>
#include <Project64-core/Settings/LoggingSettings.h>

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QTabWidget>
#include <QVBoxLayout>

using Vortex::Tr;

LogOptionsDialog::LogOptionsDialog(QWidget * Parent) :
    QDialog(Parent)
{
    setWindowTitle(Tr("Log Options"));
    QVBoxLayout * Layout = new QVBoxLayout(this);
    QTabWidget * Tabs = new QTabWidget(this);
    Layout->addWidget(Tabs);

    auto Page = [&](const QString & Title) {
        QWidget * Widget = new QWidget(Tabs);
        QVBoxLayout * PageLayout = new QVBoxLayout(Widget);
        QGroupBox * Group = new QGroupBox(Widget);
        PageLayout->addWidget(Group);
        QVBoxLayout * GroupLayout = new QVBoxLayout(Group);
        GroupLayout->setSpacing(8);
        Tabs->addTab(Widget, Title);
        return Group;
    };
    const LogSettings & Log = g_LogSettings;

    // IDD_Logging_Registers
    QGroupBox * Registers = Page(Tr("Registers"));
    AddCheck(Registers, Tr("Include RDRAM Registers accesses"), Log.logRdRamRegisters, Logging_LogRDRamRegisters);
    AddCheck(Registers, Tr("Include SP Registers accesses"), Log.logSpRegisters, Logging_LogSPRegisters);
    AddCheck(Registers, Tr("Include DP Command Registers accesses"), Log.logDpcRegisters, Logging_LogDPCRegisters);
    AddCheck(Registers, Tr("Include DP Span Registers accesses"), Log.logDpsRegisters, Logging_LogDPSRegisters);
    AddCheck(Registers, Tr("Include MIPS Interface (MI) Registers accesses"), Log.logMipsInterface, Logging_LogMIPSInterface);
    AddCheck(Registers, Tr("Include Video Interface (VI) Registers accesses"), Log.logVideoInterface, Logging_LogVideoInterface);
    AddCheck(Registers, Tr("Include Audio Interface (AI) Registers accesses"), Log.logAudioInterface, Logging_LogAudioInterface);
    AddCheck(Registers, Tr("Include Peripheral Interface (PI) Registers accesses"), Log.logPerInterface, Logging_LogPerInterface);
    AddCheck(Registers, Tr("Include RDRAM Interface (RI) Registers accesses"), Log.logRdramInterface, Logging_LogRDRAMInterface);
    AddCheck(Registers, Tr("Include Serial Interface (SI) Registers accesses"), Log.logSerialInterface, Logging_LogSerialInterface);

    // IDD_Logging_PifRam (the settings of the "direct writing" / "DMAed to" / "direct reads" /
    // "DMAed from" boxes are the ones that upstream's LogPifProc pairs with them)
    QGroupBox * PifRam = Page(Tr("Pif Ram"));
    AddCheck(PifRam, Tr("Include Serial Interface DMA tansfers"), Log.logPrDmaOperations, Logging_LogPRDMAOperations);
    AddCheck(PifRam, Tr("Include direct writing to Pif Ram"), Log.logPrDirectMemLoads, Logging_LogPRDirectMemLoads);
    AddCheck(PifRam, Tr("Include data DMAed to Pif Ram"), Log.logPrDmaMemLoads, Logging_LogPRDMAMemLoads);
    AddCheck(PifRam, Tr("Include direct reads to Pif Ram"), Log.logPrDirectMemStores, Logging_LogPRDirectMemStores);
    AddCheck(PifRam, Tr("Include data DMAed from Pif Ram"), Log.logPrDmaMemStores, Logging_LogPRDMAMemStores);
    AddCheck(PifRam, Tr("Include data for controller pak"), Log.logControllerPak, Logging_LogControllerPak);

    // IDD_Logging_General
    QGroupBox * General = Page(Tr("General"));
    AddCheck(General, Tr("Include changes of CP0 registers"), Log.logCp0changes, Logging_LogCP0changes);
    AddCheck(General, Tr("Include Reading from CP0 registers"), Log.logCp0reads, Logging_LogCP0reads);
    AddCheck(General, Tr("Include cache operations"), Log.logCache, Logging_LogCache);
    AddCheck(General, Tr("Include Exceptions"), Log.logExceptions, Logging_LogExceptions);
    QCheckBox * NoInterrupts = AddCheck(General, Tr("Do not log interrupts"), Log.logNoInterrupts, Logging_NoInterrupts);
    NoInterrupts->setStyleSheet("QCheckBox { margin-left: 16px; }"); // indented under Include Exceptions
    AddCheck(General, Tr("Include TLB modifications"), Log.logTlb, Logging_LogTLB);
    AddCheck(General, Tr("Include Read from Rom Header"), Log.logRomHeader, Logging_LogRomHeader);
    AddCheck(General, Tr("Include unkown Memory accesses"), Log.logUnknown, Logging_LogUnknown);

    QDialogButtonBox * Buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    Layout->addWidget(Buttons);
    connect(Buttons, &QDialogButtonBox::accepted, this, [this]() {
        Apply();
        accept();
    });
    connect(Buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

QCheckBox * LogOptionsDialog::AddCheck(QWidget * Page, const QString & Text, bool Checked, int Setting)
{
    QCheckBox * Box = new QCheckBox(Text, Page);
    Box->setChecked(Checked);
    Page->layout()->addWidget(Box);
    m_Entries.push_back({Box, Setting});
    return Box;
}

void LogOptionsDialog::Apply()
{
    for (const Entry & Item : m_Entries)
    {
        g_Settings->SaveBool((SettingID)Item.Setting, Item.Box->isChecked());
    }
}

QCheckBox * LogOptionsDialog::Find(const QString & Text) const
{
    for (const Entry & Item : m_Entries)
    {
        if (Item.Box->text() == Text)
        {
            return Item.Box;
        }
    }
    return nullptr;
}

void ShowLogOptionsDialog(QWidget * Parent)
{
    LogOptionsDialog(Parent).exec();
}
