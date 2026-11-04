#include "RomInformation.h"

#include <Common/File.h>
#include <Common/path.h>
#include <Common/StdString.h>
#include <Project64-core/Notification.h>
#include <Project64-core/Multilanguage.h>
#include <Project64-core/N64System/N64Disk.h>
#include <Project64-core/N64System/N64Rom.h>
#include <Project64-core/N64System/N64Types.h>
#include <Project64-core/N64System/SystemGlobals.h>

#include <QDialog>
#include <QFormLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QStringDecoder>
#include <QVBoxLayout>
#include <memory>

namespace
{
QString Gs(LanguageStringID ID)
{
    return QString::fromUtf8(GS(ID));
}

// Names in ROM headers are Shift-JIS (stdstr::CODEPAGE_932 upstream).
QString Sjis(const std::string & Text)
{
    QStringDecoder Decoder("Shift-JIS");
    if (Decoder.isValid())
    {
        return Decoder(QByteArray::fromStdString(Text));
    }
    return QString::fromLatin1(Text.c_str());
}

QString CountryName(uint8_t Country)
{
    switch (Country)
    {
    case Country_NTSC_BETA: return "Beta";
    case Country_Asian_NTSC: return "NTSC";
    case Country_Germany: return "Germany";
    case Country_NorthAmerica: return "America";
    case Country_French: return "France";
    case Country_Italian: return "Italy";
    case Country_Japan: return "Japan";
    case Country_Europe: return "Europe";
    case Country_Spanish: return "Spain";
    case Country_Australia: return "Australia";
    case Country_EuropeanX_PAL: return "PAL";
    case Country_EuropeanY_PAL: return "PAL";
    case Country_Unknown: return "None";
    default: return QString::asprintf(" Unknown %c (%02X)", Country, Country);
    }
}

struct Info
{
    QString RomName, FileName, Location, Md5, Size, CartId, Media, Country, Crc1, Crc2, Cic;
    bool HasMd5 = true;
    bool HasCic = true;
};

Info FromRom(CN64Rom & Rom)
{
    Info I;
    const uint8_t * RomHeader = Rom.GetRomAddress();
    CPath File(Rom.GetFileName());
    I.RomName = Sjis(Rom.GetRomName());
    I.FileName = QString::fromLocal8Bit(File.GetNameExtension().c_str());
    I.Location = QString::fromLocal8Bit(File.GetDirectory().c_str());
    I.Md5 = QString::fromStdString(Rom.GetRomMD5());
    I.Size = QString::asprintf("%.1f MBit", (float)Rom.GetRomSize() / 0x20000);
    I.CartId = QString::asprintf("%c%c", RomHeader[0x3F], RomHeader[0x3E]);
    switch (RomHeader[0x38])
    {
    case 'C': I.Media = "N64 Cartridge (Disk Compatible)"; break;
    case 'M': I.Media = "N64 Development Cartridge"; break;
    case 'N': I.Media = "N64 Cartridge"; break;
    case 'Z': I.Media = "Aleck64"; break;
    case 0: I.Media = "None"; break;
    default: I.Media = "(Unknown)"; break;
    }
    I.Country = CountryName(RomHeader[0x3D]);
    switch (Rom.CicChipID())
    {
    case CIC_NUS_8303:
    case CIC_NUS_8501:
    case CIC_NUS_8401:
    {
        uint16_t W[4];
        memcpy(&W[0], RomHeader + 0x608, 2);
        memcpy(&W[1], RomHeader + 0x60C, 2);
        memcpy(&W[2], RomHeader + 0x638, 2);
        memcpy(&W[3], RomHeader + 0x63C, 2);
        I.Crc1 = QString::asprintf("0x%08X", (W[0] << 16) | W[1]);
        I.Crc2 = QString::asprintf("0x%08X", (W[2] << 16) | W[3]);
        break;
    }
    default:
    {
        uint32_t C1, C2;
        memcpy(&C1, RomHeader + 0x10, 4);
        memcpy(&C2, RomHeader + 0x14, 4);
        I.Crc1 = QString::asprintf("0x%08X", C1);
        I.Crc2 = QString::asprintf("0x%08X", C2);
        break;
    }
    }
    switch (Rom.CicChipID())
    {
    case CIC_UNKNOWN: I.Cic = "Unknown"; break;
    case CIC_NUS_8303: I.Cic = "CIC-NUS-8303"; break;
    case CIC_NUS_5167: I.Cic = "CIC-NUS-5167"; break;
    case CIC_NUS_5101: I.Cic = "CIC-NUS-5101"; break;
    case CIC_NUS_8501: I.Cic = "CIC-NUS-8501"; break;
    case CIC_NUS_8401: I.Cic = "CIC-NUS-8401"; break;
    default: I.Cic = QString::asprintf("CIC-NUS-610%d", (int)Rom.CicChipID()); break;
    }
    return I;
}

Info FromDisk(CN64Disk & Disk)
{
    Info I;
    I.HasMd5 = false; // commented out upstream
    I.HasCic = false;
    const uint8_t * DiskHeader = Disk.GetDiskAddressID();
    CPath File(Disk.GetFileName());
    I.RomName = Sjis(Disk.GetRomName());
    I.FileName = QString::fromLocal8Bit(File.GetNameExtension().c_str());
    I.Location = QString::fromLocal8Bit(File.GetDirectory().c_str());
    I.Size = QString::asprintf("%.1f MBit", (float)Disk.GetDiskSize() / 0x20000);
    I.CartId = QString::asprintf("%c%c", DiskHeader[0x02], DiskHeader[0x01]);
    switch (DiskHeader[0x03])
    {
    case 'D': I.Media = "64DD Disk"; break;
    case 'E': I.Media = "64DD Disk (Expansion)"; break;
    case 0: I.Media = "None"; break;
    default: I.Media = "(Unknown)"; break;
    }
    I.Country = CountryName(DiskHeader[0x00]);
    const uint32_t Crc = Disk.CalculateCrc();
    I.Crc1 = QString::asprintf("0x%08X", Crc);
    I.Crc2 = QString::asprintf("0x%08X", ~Crc);
    return I;
}

void Show(QWidget * Parent, const Info & I)
{
    QDialog Dialog(Parent);
    Dialog.setObjectName("VxRomInfo");
    Dialog.setWindowTitle(Gs(INFO_TITLE));
    Dialog.setMinimumWidth(520);
    QVBoxLayout * Layout = new QVBoxLayout(&Dialog);
    Layout->setContentsMargins(20, 18, 20, 16);
    QFormLayout * Form = new QFormLayout;
    Form->setVerticalSpacing(8);
    auto Row = [&](LanguageStringID Label, const QString & Value) {
        QLineEdit * Edit = new QLineEdit(Value, &Dialog);
        Edit->setReadOnly(true);
        Form->addRow(Gs(Label), Edit);
    };
    Row(INFO_ROM_NAME_TEXT, I.RomName);
    Row(INFO_FILE_NAME_TEXT, I.FileName);
    Row(INFO_SIZE_TEXT, I.Size);
    Row(INFO_CART_ID_TEXT, I.CartId);
    Row(INFO_MEDIA_TEXT, I.Media);
    Row(INFO_COUNTRY_TEXT, I.Country);
    Row(INFO_CRC1_TEXT, I.Crc1);
    Row(INFO_CRC2_TEXT, I.Crc2);
    if (I.HasCic)
    {
        Row(INFO_CIC_CHIP_TEXT, I.Cic);
    }
    Row(INFO_LOCATION_TEXT, I.Location);
    if (I.HasMd5)
    {
        Row(INFO_MD5_TEXT, I.Md5);
    }
    Layout->addLayout(Form);
    QPushButton * Close = new QPushButton(Gs(BOTTOM_CLOSE), &Dialog);
    Close->setDefault(true);
    QObject::connect(Close, &QPushButton::clicked, &Dialog, &QDialog::accept);
    Layout->addWidget(Close, 0, Qt::AlignRight);
    Dialog.exec();
}
} // namespace

bool ShowRomInformation(QWidget * Parent, const QString & RomFile)
{
    if (RomFile.isEmpty())
    {
        return false;
    }
    const std::string File = RomFile.toStdString();
    const std::string Extension = CPath(File).GetExtension();
    if (Extension != "ndd" && Extension != "d64")
    {
        CN64Rom Rom;
        if (!Rom.LoadN64Image(File.c_str()))
        {
            return false;
        }
        Show(Parent, FromRom(Rom));
        return true;
    }
    CN64Disk Disk;
    if (!Disk.LoadDiskImage(File.c_str()))
    {
        return false;
    }
    Show(Parent, FromDisk(Disk));
    return true;
}

bool ShowRunningRomInformation(QWidget * Parent)
{
    if (g_Rom != nullptr)
    {
        Show(Parent, FromRom(*g_Rom));
        return true;
    }
    if (g_Disk != nullptr)
    {
        Show(Parent, FromDisk(*g_Disk));
        return true;
    }
    return false;
}
