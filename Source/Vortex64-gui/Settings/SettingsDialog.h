#pragma once

#include <QDialog>
#include <vector>

class QLabel;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QScrollArea;
class QStackedWidget;
class QtSettingsPage;
class QtPluginPage;
class QtGeneralOptionsPage;
class QVBoxLayout;

// The settings window of Vortex64: a list of categories on the left (General, Directorios, Gráficos, Audio, Input,
// RSP, Plugins, Atajos, 64DD, Avanzado) and the page of the category on the right, with Accept / Cancel / Apply.
// The pages are the ports of the Project64 pages (QtSettingsPage: the same SettingIDs, defaults and apply / reset
// rules) laid out by category. The emulation is paused while the dialog is open.
class SettingsDialog : public QDialog
{
    Q_OBJECT
public:
    // GameSettingsOnly: "Edit Game Settings" (CSettingConfig(true)) shows only the section of the loaded game.
    explicit SettingsDialog(bool GameSettingsOnly = false, QWidget * parent = nullptr);
    ~SettingsDialog() override;

    int exec() override;
    // Shows a category or a page by its name ("general", "directories", "graphics", "audio", "input", "rsp",
    // "plugins", "shortcuts", "disk", "advanced"); unknown names do nothing
    void ShowPage(const QString & Name);
    // The language was changed with Apply: the window closes and its owner opens it again, built with the new texts,
    // on the same page (a window is built once with its texts)
    bool ReopenRequested() const
    {
        return m_Reopen;
    }
    QString CurrentPageId();
    QStringList CategoryIds() const;
    QString CurrentCategory() const;
    const std::vector<QtSettingsPage *> & Pages() const
    {
        return m_AllPages;
    }

private Q_SLOTS:
    void OnPageChanged();
    void OnApply();
    void OnOk();
    void OnResetPage();
    void OnResetAll();

private:
    struct Category
    {
        QString Id;
        QString Title;
        QListWidgetItem * Item = nullptr;
        QWidget * Body = nullptr;
        std::vector<QtSettingsPage *> Pages; // the pages whose settings the category holds
    };
    Category & AddCategory(const QString & Id, const QString & Title, int IconId);
    // Appends a page (and registers it for Apply / Reset) to the body of the category
    void AddPage(Category & C, QtSettingsPage * Page, const QString & SectionTitle = QString());
    void AddWidget(Category & C, QWidget * Widget);
    void BuildGlobal();
    void BuildGame();
    void UpdateAdvanced(bool AdvancedMode);
    void RefreshButtons();
    void BoldChangedPages();
    Category * Current();

    QListWidget * m_Nav;
    QStackedWidget * m_Stack;
    QLabel * m_Title;
    QPushButton * m_ResetPage, * m_ResetAll, * m_Apply;
    std::vector<Category *> m_Categories;
    std::vector<QtSettingsPage *> m_AllPages;
    bool m_LanguageChanged = false;
    bool m_Reopen = false;
    bool m_GameSettingsOnly;
    bool m_Changed = false;
};
