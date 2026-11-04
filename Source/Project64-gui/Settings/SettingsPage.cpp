#include "SettingsPage.h"

#include <Project64-core/Settings.h>
#include <Project64-core/Settings/SettingsID.h>

#include <QCheckBox>
#include <QComboBox>
#include <QFont>
#include <QLabel>
#include <QLineEdit>

QString QtGS(LanguageStringID ID)
{
    return QString::fromUtf8(GS(ID));
}

QtSettingsPage::QtSettingsPage(QWidget * parent) :
    QWidget(parent)
{
}

QtSettingsPage::~QtSettingsPage() = default;

static void SetBold(QWidget * Widget, bool Bold)
{
    if (Widget == nullptr)
    {
        return;
    }
    QFont Font = Widget->font();
    Font.setBold(Bold);
    Widget->setFont(Font);
}

void QtSettingsPage::SetChanged(Modified & Item, bool Changed)
{
    Item.Changed = Changed;
    if (Changed)
    {
        Item.Reset = false;
    }
    SetBold(Item.Widget, Changed);
    SetBold(Item.TextField, Changed);
}

void QtSettingsPage::SetReset(Modified & Item, bool Reset)
{
    Item.Reset = Reset;
    if (Reset)
    {
        SetChanged(Item, false);
        Item.Reset = true;
    }
}

void QtSettingsPage::MarkChanged(Modified & Item)
{
    SetChanged(Item, true);
    Q_EMIT PageChanged();
}

void QtSettingsPage::AddModCheckBox(QCheckBox * CheckBox, SettingID Type, std::function<bool(bool)> Confirm)
{
    if (m_CheckBoxes.find(Type) != m_CheckBoxes.end())
    {
        return;
    }
    Modified & Item = m_CheckBoxes[Type];
    Item.Widget = CheckBox;
    connect(CheckBox, &QCheckBox::clicked, this, [this, Type, CheckBox, Confirm](bool Checked) {
        if (Confirm && !Confirm(Checked))
        {
            CheckBox->setChecked(!Checked);
            return;
        }
        MarkChanged(m_CheckBoxes[Type]);
    });
}

QtSettingsPage::Modified * QtSettingsPage::AddModTextBox(QLineEdit * EditBox, SettingID Type, bool IsString)
{
    if (m_TextBoxes.find(Type) != m_TextBoxes.end())
    {
        return nullptr;
    }
    Modified & Item = m_TextBoxes[Type];
    Item.Widget = EditBox;
    Item.IsString = IsString;
    // textEdited is only emitted for user edits, which replaces the
    // m_UpdatingTxt guard of the WTL implementation.
    connect(EditBox, &QLineEdit::textEdited, this, [this, Type]() { MarkChanged(m_TextBoxes[Type]); });
    return &Item;
}

QtSettingsPage::Modified * QtSettingsPage::AddModComboBox(QComboBox * ComboBox, SettingID Type)
{
    auto Found = m_ComboBoxes.find(Type);
    if (Found != m_ComboBoxes.end())
    {
        return &Found->second;
    }
    Modified & Item = m_ComboBoxes[Type];
    Item.Widget = ComboBox;
    connect(ComboBox, QOverload<int>::of(&QComboBox::activated), this, [this, Type]() { MarkChanged(m_ComboBoxes[Type]); });
    return &Item;
}

QtSettingsPage::Modified * QtSettingsPage::AddModComboBoxTxt(QComboBox * ComboBox, SettingID Type)
{
    auto Found = m_ComboBoxesTxt.find(Type);
    if (Found != m_ComboBoxesTxt.end())
    {
        return &Found->second;
    }
    Modified & Item = m_ComboBoxesTxt[Type];
    Item.Widget = ComboBox;
    connect(ComboBox, QOverload<int>::of(&QComboBox::activated), this, [this, Type]() { MarkChanged(m_ComboBoxesTxt[Type]); });
    return &Item;
}

std::vector<QtSettingsPage::BoundControl> QtSettingsPage::BoundControls() const
{
    std::vector<BoundControl> Controls;
    for (const auto & Entry : m_CheckBoxes)
    {
        Controls.push_back({Entry.first, BoundControl::CheckBox, Entry.second.Widget, false});
    }
    for (const auto & Entry : m_TextBoxes)
    {
        Controls.push_back({Entry.first, BoundControl::TextBox, Entry.second.Widget, Entry.second.IsString});
    }
    for (const auto & Entry : m_ComboBoxes)
    {
        Controls.push_back({Entry.first, BoundControl::ComboBox, Entry.second.Widget, false});
    }
    for (const auto & Entry : m_ComboBoxesTxt)
    {
        Controls.push_back({Entry.first, BoundControl::ComboBoxTxt, Entry.second.Widget, true});
    }
    return Controls;
}

void QtSettingsPage::UpdatePageSettings()
{
    for (auto & Entry : m_CheckBoxes)
    {
        bool Value = false;
        SetChanged(Entry.second, g_Settings->LoadBool(Entry.first, Value));
        static_cast<QCheckBox *>(Entry.second.Widget)->setChecked(Value);
    }
    for (auto & Entry : m_ComboBoxesTxt)
    {
        std::string Value;
        SetChanged(Entry.second, g_Settings->LoadStringVal(Entry.first, Value));
        QComboBox * ComboBox = static_cast<QComboBox *>(Entry.second.Widget);
        int Index = ComboBox->findData(QString::fromStdString(Value), Qt::UserRole, Qt::MatchFixedString);
        if (Index >= 0)
        {
            ComboBox->setCurrentIndex(Index);
        }
    }
    for (auto & Entry : m_ComboBoxes)
    {
        uint32_t Value = 0;
        SetChanged(Entry.second, g_Settings->LoadDword(Entry.first, Value));
        QComboBox * ComboBox = static_cast<QComboBox *>(Entry.second.Widget);
        int Index = ComboBox->findData(QVariant::fromValue<uint>(Value));
        if (Index >= 0)
        {
            ComboBox->setCurrentIndex(Index);
        }
    }
    for (auto & Entry : m_TextBoxes)
    {
        QLineEdit * EditBox = static_cast<QLineEdit *>(Entry.second.Widget);
        if (Entry.second.IsString)
        {
            std::string Value;
            SetChanged(Entry.second, g_Settings->LoadStringVal(Entry.first, Value));
            EditBox->setText(QString::fromStdString(Value));
        }
        else
        {
            uint32_t Value = 0;
            SetChanged(Entry.second, g_Settings->LoadDword(Entry.first, Value));
            EditBox->setText(QString::number(Value));
        }
    }
}

void QtSettingsPage::ApplySettings(bool UpdateScreen)
{
    for (auto & Entry : m_TextBoxes)
    {
        Modified & Item = Entry.second;
        QLineEdit * EditBox = static_cast<QLineEdit *>(Item.Widget);
        if (Item.Changed)
        {
            if (Item.IsString)
            {
                g_Settings->SaveString(Entry.first, EditBox->text().toStdString());
            }
            else
            {
                g_Settings->SaveDword(Entry.first, EditBox->text().toUInt());
            }
        }
        if (Item.Reset)
        {
            g_Settings->DeleteSetting(Entry.first);
            Item.Reset = false;
        }
    }
    for (auto & Entry : m_CheckBoxes)
    {
        Modified & Item = Entry.second;
        QCheckBox * CheckBox = static_cast<QCheckBox *>(Item.Widget);
        if (Item.Changed)
        {
            bool Value = CheckBox->isChecked();
            if (Value != g_Settings->LoadBool(Entry.first))
            {
                g_Settings->SaveBool(Entry.first, Value);
            }
        }
        if (Item.Reset)
        {
            g_Settings->DeleteSetting(Entry.first);
            Item.Reset = false;
        }
    }
    for (auto & Entry : m_ComboBoxesTxt)
    {
        Modified & Item = Entry.second;
        QComboBox * ComboBox = static_cast<QComboBox *>(Item.Widget);
        if (Item.Changed && ComboBox->currentIndex() >= 0)
        {
            g_Settings->SaveString(Entry.first, ComboBox->currentData().toString().toStdString());
        }
        if (Item.Reset)
        {
            g_Settings->DeleteSetting(Entry.first);
            Item.Reset = false;
        }
    }
    for (auto & Entry : m_ComboBoxes)
    {
        Modified & Item = Entry.second;
        QComboBox * ComboBox = static_cast<QComboBox *>(Item.Widget);
        if (Item.Changed && ComboBox->currentIndex() >= 0)
        {
            g_Settings->SaveDword(Entry.first, ComboBox->currentData().toUInt());
        }
        if (Item.Reset)
        {
            g_Settings->DeleteSetting(Entry.first);
            Item.Reset = false;
        }
    }

    if (UpdateScreen)
    {
        UpdatePageSettings();
    }
}

bool QtSettingsPage::EnableReset()
{
    for (const auto * List : {&m_TextBoxes, &m_CheckBoxes, &m_ComboBoxesTxt, &m_ComboBoxes})
    {
        for (const auto & Entry : *List)
        {
            if (Entry.second.Changed)
            {
                return true;
            }
        }
    }
    return false;
}

void QtSettingsPage::ResetPage()
{
    bool Changed = false;

    for (auto & Entry : m_TextBoxes)
    {
        Modified & Item = Entry.second;
        if (!Item.Changed)
        {
            continue;
        }
        QLineEdit * EditBox = static_cast<QLineEdit *>(Item.Widget);
        if (Item.IsString)
        {
            EditBox->setText(QString::fromStdString(g_Settings->LoadDefaultString(Entry.first)));
        }
        else
        {
            EditBox->setText(QString::number(g_Settings->LoadDefaultDword(Entry.first)));
        }
        SetReset(Item, true);
        Changed = true;
    }
    for (auto & Entry : m_CheckBoxes)
    {
        Modified & Item = Entry.second;
        if (!Item.Changed)
        {
            continue;
        }
        SetReset(Item, true);
        static_cast<QCheckBox *>(Item.Widget)->setChecked(g_Settings->LoadDefaultBool(Entry.first));
        Changed = true;
    }
    for (auto & Entry : m_ComboBoxesTxt)
    {
        Modified & Item = Entry.second;
        if (!Item.Changed)
        {
            continue;
        }
        SetReset(Item, true);
        QComboBox * ComboBox = static_cast<QComboBox *>(Item.Widget);
        int Index = ComboBox->findData(QString::fromStdString(g_Settings->LoadDefaultString(Entry.first)), Qt::UserRole, Qt::MatchFixedString);
        if (Index >= 0)
        {
            ComboBox->setCurrentIndex(Index);
            Changed = true;
        }
    }
    for (auto & Entry : m_ComboBoxes)
    {
        Modified & Item = Entry.second;
        if (!Item.Changed)
        {
            continue;
        }
        SetReset(Item, true);
        QComboBox * ComboBox = static_cast<QComboBox *>(Item.Widget);
        int Index = ComboBox->findData(QVariant::fromValue<uint>(g_Settings->LoadDefaultDword(Entry.first)));
        if (Index >= 0)
        {
            ComboBox->setCurrentIndex(Index);
            Changed = true;
        }
    }

    if (Changed)
    {
        Q_EMIT PageChanged();
    }
}
