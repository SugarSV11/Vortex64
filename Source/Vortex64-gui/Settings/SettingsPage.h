#pragma once

#include <Vortex64-core/Multilanguage.h>
#include <Vortex64-core/Settings/SettingsID.h>
#include <Vortex64-ui/Settings/UISettings.h>

#include <QString>
#include <QWidget>
#include <functional>
#include <map>
#include <string>
#include <vector>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;

// Qt counterpart of upstream's CSettingsPageImpl<> (Source/Vortex64-ui/
// UserInterface/Settings/SettingsPage.h). Every bound control follows the
// same rules as the WTL "Modified" controls: it is shown in bold while its
// SettingID differs from the default, edits only take effect on
// ApplySettings(), and resetting a control deletes the stored setting.
class QtSettingsPage : public QWidget
{
    Q_OBJECT

public:
    explicit QtSettingsPage(QWidget * parent = nullptr);
    ~QtSettingsPage() override;

    virtual LanguageStringID PageTitle() = 0;
    virtual void ApplySettings(bool UpdateScreen);
    virtual bool EnableReset();
    virtual void ResetPage();
    virtual void UpdatePageSettings();

    // The controls bound to a SettingID (the settings test goes through all of them)
    struct BoundControl
    {
        enum Kind
        {
            CheckBox,
            TextBox,
            ComboBox,
            ComboBoxTxt
        };
        SettingID Id;
        Kind Type;
        QWidget * Widget;
        bool IsString;
    };
    std::vector<BoundControl> BoundControls() const;

Q_SIGNALS:
    void PageChanged();

protected:
    struct Modified
    {
        QWidget * Widget = nullptr;
        QLabel * TextField = nullptr;
        bool Changed = false;
        bool Reset = false;
        bool IsString = false; // edit boxes: string or dword setting
    };

    // Confirm(checked) may veto the user's click (upstream: HleGfxChanged /
    // HleAudioChanged ask a yes/no question and restore the box on "No").
    void AddModCheckBox(QCheckBox * CheckBox, SettingID Type, std::function<bool(bool)> Confirm = nullptr);
    Modified * AddModTextBox(QLineEdit * EditBox, SettingID Type, bool IsString);
    // Items carry a dword (AddModComboBox) or a string (AddModComboBoxTxt)
    // as item data; add them with addItem(text, value) before UpdatePageSettings().
    Modified * AddModComboBox(QComboBox * ComboBox, SettingID Type);
    Modified * AddModComboBoxTxt(QComboBox * ComboBox, SettingID Type);

    static void SetChanged(Modified & Item, bool Changed);
    static void SetReset(Modified & Item, bool Reset);
    void MarkChanged(Modified & Item);

    std::map<SettingID, Modified> m_CheckBoxes;
    std::map<SettingID, Modified> m_TextBoxes;
    std::map<SettingID, Modified> m_ComboBoxes;
    std::map<SettingID, Modified> m_ComboBoxesTxt;
};

// Language string helper (upstream: GS()/wGS()).
QString QtGS(LanguageStringID ID);
