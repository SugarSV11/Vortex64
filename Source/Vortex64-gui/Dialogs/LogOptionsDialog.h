#pragma once

#include <QDialog>
#include <vector>

class QCheckBox;

// Qt counterpart of EnterLogOptions (Project64/UserInterface/LoggingUI.cpp): the property sheet
// "Log Options" with the pages Registers, Pif Ram and General. The check boxes start from the
// log settings (g_LogSettings) and are written to the Logging_* settings when OK is pressed.
class LogOptionsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit LogOptionsDialog(QWidget * Parent = nullptr);

    // What OK does
    void Apply();

    // Test hook: the check box with this text
    QCheckBox * Find(const QString & Text) const;

private:
    QCheckBox * AddCheck(QWidget * Page, const QString & Text, bool Checked, int Setting);

    struct Entry
    {
        QCheckBox * Box;
        int Setting;
    };
    std::vector<Entry> m_Entries;
};

void ShowLogOptionsDialog(QWidget * Parent);
