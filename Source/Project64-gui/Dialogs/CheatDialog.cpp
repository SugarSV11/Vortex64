#include "CheatDialog.h"
#include "../Vortex/Strings.h"
#include "EnhancementTree.h"

#include <Common/StdString.h>
#include <Project64-core/Multilanguage.h>
#include <Project64-core/N64System/Enhancement/Enhancements.h>
#include <Project64-core/N64System/ConfigPause.h>
#include <Project64-core/N64System/N64System.h>
#include <Project64-core/N64System/SystemGlobals.h>
#include <Project64-core/Notification.h>
#include <Project64-core/Settings.h>
#include <Project64-core/Settings/SettingsID.h>

#include <QDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>

using Vortex::Tr;

namespace
{
QString Lang(LanguageStringID ID)
{
    return QString::fromUtf8(GS(ID));
}

QString Utf8(const std::string & Text)
{
    return QString::fromUtf8(Text.c_str());
}

// CEnhancementCodeEx: pick the value of a code with extensions
class CodeExtensionDialog : public QDialog
{
public:
    CodeExtensionDialog(CEnhancement * Enhancement, QWidget * Parent) :
        QDialog(Parent),
        m_Enhancement(Enhancement)
    {
        setWindowTitle(Lang(CHEAT_CODE_EXT_TITLE));
        QVBoxLayout * Layout = new QVBoxLayout(this);
        Layout->addWidget(new QLabel(Lang(CHEAT_CODE_EXT_TXT), this));
        Layout->addWidget(new QLabel(Utf8(Enhancement->GetName()), this));
        m_List = new QListWidget(this);
        const CEnhancement::CodeOptions & Options = Enhancement->GetOptions();
        for (size_t i = 0; i < Options.size(); i++)
        {
            const stdstr_f Text(Enhancement->CodeOptionSize() == 2 ? "$%02X %s" : "$%04X %s", Options[i].Value, Options[i].Name.c_str());
            QListWidgetItem * Item = new QListWidgetItem(Utf8(Text), m_List);
            Item->setData(Qt::UserRole, Options[i].Value);
            if (Enhancement->OptionSelected() && Enhancement->SelectedOption() == Options[i].Value)
            {
                m_List->setCurrentItem(Item);
            }
        }
        Layout->addWidget(m_List);
        QHBoxLayout * Buttons = new QHBoxLayout;
        QPushButton * Ok = new QPushButton(Lang(CHEAT_OK), this);
        QPushButton * Cancel = new QPushButton(Lang(CHEAT_CANCEL), this);
        Ok->setDefault(true);
        Buttons->addStretch();
        Buttons->addWidget(Ok);
        Buttons->addWidget(Cancel);
        Layout->addLayout(Buttons);
        connect(Ok, &QPushButton::clicked, this, &CodeExtensionDialog::Accept);
        connect(Cancel, &QPushButton::clicked, this, &QDialog::reject);
        connect(m_List, &QListWidget::itemDoubleClicked, this, &CodeExtensionDialog::Accept);
    }

private:
    void Accept()
    {
        int Row = m_List->currentRow();
        if (Row < 0)
        {
            Row = 0;
        }
        if (m_List->count() > 0)
        {
            m_Enhancement->SetSelectedOption((uint16_t)m_List->item(Row)->data(Qt::UserRole).toUInt());
        }
        if (g_Enhancements != nullptr)
        {
            g_Enhancements->UpdateCheats();
        }
        accept();
    }

    CEnhancement * m_Enhancement;
    QListWidget * m_List;
};

// CCheatsUI + CCheatList + CEditCheat
class CheatDialog : public QDialog
{
public:
    explicit CheatDialog(QWidget * Parent) :
        QDialog(Parent),
        m_EditEnhancement(nullptr)
    {
        setObjectName("CheatDialog");
        g_Enhancements->Load();
        m_Cheats = g_Enhancements->Cheats();
        const bool BasicMode = g_Settings->LoadBool(UserInterface_BasicMode);
        setWindowTitle(Lang(CHEAT_TITLE));

        QHBoxLayout * Main = new QHBoxLayout(this);
        QVBoxLayout * ListSide = new QVBoxLayout;
        m_Tree = new EnhancementTree(this);
        m_Tree->setMinimumSize(320, 300);
        ListSide->addWidget(m_Tree, 1);
        QPushButton * Unmark = new QPushButton(Lang(CHEAT_MARK_NONE), this);
        ListSide->addWidget(Unmark, 0, Qt::AlignRight);
        ListSide->addWidget(new QLabel(Lang(CHEAT_NOTES_FRAME), this));
        m_Notes = new QPlainTextEdit(this);
        m_Notes->setReadOnly(true);
        m_Notes->setMaximumHeight(72);
        ListSide->addWidget(m_Notes);
        Main->addLayout(ListSide);

        if (!BasicMode)
        {
            // The ">" button of upstream shows/hides the edit panel
            m_Expand = new QToolButton(this);
            m_Expand->setArrowType(Qt::RightArrow);
            m_Expand->setCheckable(true);
            m_Expand->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
            Main->addWidget(m_Expand);
            BuildEditPanel();
            m_EditPanel->setVisible(false);
            Main->addWidget(m_EditPanel);
            connect(m_Expand, &QToolButton::toggled, this, [this](bool Expanded) {
                m_Expand->setArrowType(Expanded ? Qt::LeftArrow : Qt::RightArrow);
                m_EditPanel->setVisible(Expanded);
                adjustSize();
            });
        }

        connect(Unmark, &QPushButton::clicked, this, [this]() {
            m_Tree->SetAllChecked(false);
            g_Enhancements->UpdateCheats();
        });
        connect(m_Tree, &EnhancementTree::ActiveChanged, this, []() { g_Enhancements->UpdateCheats(); });
        connect(m_Tree, &EnhancementTree::ChooseOption, this, [this](QTreeWidgetItem * Item) { ChooseOption(Item); });
        connect(m_Tree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem * Item, int) { ChooseOption(Item); });
        connect(m_Tree, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem * Item, QTreeWidgetItem *) { OnSelectionChanged(Item); });
        if (!BasicMode)
        {
            m_Tree->setContextMenuPolicy(Qt::CustomContextMenu);
            connect(m_Tree, &QWidget::customContextMenuRequested, this, [this](const QPoint & Pos) { OnContextMenu(Pos); });
        }
        m_Tree->Refresh(m_Cheats);
    }

    void done(int Result) override
    {
        if (m_EditPanel != nullptr && ValuesChanged())
        {
            return;
        }
        QDialog::done(Result);
    }

private:
    void BuildEditPanel()
    {
        m_EditPanel = new QWidget(this);
        QVBoxLayout * Layout = new QVBoxLayout(m_EditPanel);
        m_Name = new QLineEdit(m_EditPanel);
        m_Author = new QLineEdit(m_EditPanel);
        QHBoxLayout * NameRow = new QHBoxLayout;
        NameRow->addWidget(new QLabel(Lang(CHEAT_ADDCHEAT_NAME), m_EditPanel));
        NameRow->addWidget(m_Name);
        Layout->addLayout(NameRow);
        QHBoxLayout * AuthorRow = new QHBoxLayout;
        AuthorRow->addWidget(new QLabel(Tr("Author"), m_EditPanel));
        AuthorRow->addWidget(m_Author);
        Layout->addLayout(AuthorRow);

        QHBoxLayout * Columns = new QHBoxLayout;
        QVBoxLayout * CodeColumn = new QVBoxLayout;
        CodeColumn->addWidget(new QLabel(Lang(CHEAT_ADDCHEAT_CODE), m_EditPanel));
        CodeColumn->addWidget(new QLabel(Lang(CHEAT_ADDCHEAT_CODEDES), m_EditPanel));
        m_Codes = new QPlainTextEdit(m_EditPanel);
        CodeColumn->addWidget(m_Codes);
        QVBoxLayout * OptionColumn = new QVBoxLayout;
        m_OptionsLabel = new QLabel(Lang(CHEAT_ADDCHEAT_OPT), m_EditPanel);
        m_OptionsFormat = new QLabel(Lang(CHEAT_ADDCHEAT_OPTDES), m_EditPanel);
        m_Options = new QPlainTextEdit(m_EditPanel);
        OptionColumn->addWidget(m_OptionsLabel);
        OptionColumn->addWidget(m_OptionsFormat);
        OptionColumn->addWidget(m_Options);
        Columns->addLayout(CodeColumn);
        Columns->addLayout(OptionColumn);
        Layout->addLayout(Columns, 1);

        Layout->addWidget(new QLabel(Lang(CHEAT_ADDCHEAT_NOTES), m_EditPanel));
        m_EditNotesEdit = new QPlainTextEdit(m_EditPanel);
        m_EditNotesEdit->setMaximumHeight(64);
        Layout->addWidget(m_EditNotesEdit);

        QHBoxLayout * Buttons = new QHBoxLayout;
        QPushButton * NewCheat = new QPushButton(Lang(CHEAT_ADDCHEAT_NEW), m_EditPanel);
        m_Add = new QPushButton(Lang(CHEAT_ADDCHEAT_ADD), m_EditPanel);
        Buttons->addWidget(NewCheat);
        Buttons->addWidget(m_Add);
        Layout->addLayout(Buttons);

        SetOptionControlsEnabled(false);
        m_Add->setEnabled(false);
        RecordCurrentValues();

        connect(m_Add, &QPushButton::clicked, this, [this]() { OnAddCheat(); });
        connect(NewCheat, &QPushButton::clicked, this, [this]() { OnNewCheat(); });
        connect(m_Name, &QLineEdit::textChanged, this, [this]() { DetailsChanged(); });
        connect(m_Codes, &QPlainTextEdit::textChanged, this, [this]() { DetailsChanged(); });
        connect(m_Options, &QPlainTextEdit::textChanged, this, [this]() { DetailsChanged(); });
    }

    void SetOptionControlsEnabled(bool Enabled)
    {
        m_OptionsLabel->setEnabled(Enabled);
        m_OptionsFormat->setEnabled(Enabled);
        m_Options->setEnabled(Enabled);
    }

    // CCheatList::OnChangeCodeExtension
    void ChooseOption(QTreeWidgetItem * Item)
    {
        CEnhancement * Enhancement = m_Tree->EnhancementOf(Item);
        if (Enhancement == nullptr || Enhancement->CodeOptionSize() == 0 || Enhancement->GetOptions().empty())
        {
            return;
        }
        CodeExtensionDialog Dialog(Enhancement, this);
        Dialog.exec();
        std::string Name = Enhancement->GetNameAndExtension();
        const size_t Slash = Name.rfind('\\');
        if (Slash != std::string::npos)
        {
            Name = Name.substr(Slash + 1);
        }
        Item->setText(0, Utf8(Name));
    }

    // CCheatList::OnTreeSelChanged
    void OnSelectionChanged(QTreeWidgetItem * Item)
    {
        m_Notes->setPlainText(QString());
        CEnhancement * Enhancement = m_Tree->EnhancementOf(Item);
        if (Enhancement == nullptr)
        {
            return;
        }
        m_Notes->setPlainText(Utf8(Enhancement->GetNote()));
        if (m_EditPanel != nullptr)
        {
            EditCheat(Enhancement);
        }
    }

    // CCheatList::OnTreeRClicked / OnPopupDelete
    void OnContextMenu(const QPoint & Pos)
    {
        QTreeWidgetItem * Item = m_Tree->itemAt(Pos);
        if (Item == nullptr || Item->childCount() != 0)
        {
            return;
        }
        QMenu Menu;
        QAction * Delete = Menu.addAction(Lang(CHEAT_DELETE));
        if (Menu.exec(m_Tree->viewport()->mapToGlobal(Pos)) != Delete)
        {
            return;
        }
        if (QMessageBox::question(this, Lang(MSG_DEL_TITLE), Lang(MSG_DEL_SURE), QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes)
        {
            return;
        }
        CEnhancement * Target = m_Tree->EnhancementOf(Item);
        m_Tree->SetAllChecked(false);
        for (CEnhancementList::iterator itr = m_Cheats.begin(); itr != m_Cheats.end(); itr++)
        {
            if (Target != &itr->second)
            {
                continue;
            }
            if (m_EditEnhancement == Target)
            {
                m_EditEnhancement = nullptr;
            }
            m_Cheats.erase(itr);
            g_Enhancements->UpdateCheats(m_Cheats);
            break;
        }
        m_Tree->Refresh(m_Cheats);
    }

    // CEditCheat::OnEditCheat
    void EditCheat(CEnhancement * Enhancement)
    {
        if (ValuesChanged())
        {
            return;
        }
        m_EditEnhancement = Enhancement;
        m_Name->setText(Utf8(Enhancement->GetName()));
        m_Author->setText(Utf8(Enhancement->GetAuthor()));
        stdstr Buffer;
        for (const CEnhancement::CodeEntry & Entry : Enhancement->GetEntries())
        {
            if (!Buffer.empty())
            {
                Buffer.append("\n");
            }
            Buffer += stdstr_f("%08X %s", Entry.Command, Entry.Value.c_str());
        }
        m_Codes->setPlainText(Utf8(Buffer));
        Buffer.clear();
        for (const CEnhancement::CodeOption & Option : Enhancement->GetOptions())
        {
            if (!Buffer.empty())
            {
                Buffer.append("\n");
            }
            Buffer += stdstr_f(Enhancement->CodeOptionSize() == 2 ? "%02X %s" : "%04X %s", Option.Value, Option.Name.c_str());
        }
        m_Options->setPlainText(Utf8(Buffer));
        m_EditNotesEdit->setPlainText(Utf8(Enhancement->GetNote()));
        m_Add->setText(Lang(CHEAT_EDITCHEAT_UPDATE));
        DetailsChanged();
        RecordCurrentValues();
    }

    // CEditCheat::ReadEnhancement
    bool ReadEnhancement(CEnhancement & Enhancement)
    {
        CEnhancement Test(CEnhancement::CheatIdent);
        Test.SetName(m_Name->text().toUtf8().constData());
        Test.SetNote(m_EditNotesEdit->toPlainText().toUtf8().constData());

        CEnhancement::CodeEntries Entries;
        for (const QString & Line : m_Codes->toPlainText().split('\n'))
        {
            if (Line.isEmpty())
            {
                continue;
            }
            const std::string Str = Line.toUpper().toUtf8().constData();
            CEnhancement::CodeEntry Entry;
            Entry.Command = strtoul(Str.c_str(), nullptr, 16);
            const char * ReadPos = strchr(Str.c_str(), ' ');
            if (ReadPos == nullptr)
            {
                return false;
            }
            Entry.Value = ReadPos + 1;
            Entries.push_back(Entry);
        }
        Test.SetEntries(Entries);

        CEnhancement::CodeOptions Options;
        for (const QString & Line : m_Options->toPlainText().split('\n'))
        {
            if (Line.isEmpty())
            {
                continue;
            }
            const std::string Str = Line.toUtf8().constData();
            CEnhancement::CodeOption Option;
            Option.Value = (uint16_t)strtoul(Str.c_str(), nullptr, 16);
            if (Str.length() > Test.CodeOptionSize() && Str[Test.CodeOptionSize()] != ' ')
            {
                return false;
            }
            const char * ReadPos = strchr(Str.c_str(), ' ');
            if (ReadPos == nullptr)
            {
                return false;
            }
            Option.Name = ReadPos + 1;
            Options.push_back(Option);
        }
        Test.SetOptions(Options);
        if (!Test.Valid())
        {
            return false;
        }
        Enhancement = Test;
        return true;
    }

    // CEditCheat::DetailsChanged
    void DetailsChanged()
    {
        CEnhancement Enhancement(CEnhancement::CheatIdent);
        if (!ReadEnhancement(Enhancement))
        {
            m_Add->setEnabled(false);
            return;
        }
        const bool HasOptions = Enhancement.CodeOptionSize() > 0;
        SetOptionControlsEnabled(HasOptions);
        m_Add->setEnabled(Enhancement.CodeOptionSize() == 0 || Enhancement.GetOptions().size() > 0);
    }

    // CEditCheat::OnAddCheat
    void OnAddCheat()
    {
        const std::string NewCheatName = m_Name->text().toUtf8().constData();
        for (CEnhancementList::const_iterator itr = m_Cheats.begin(); itr != m_Cheats.end(); itr++)
        {
            const CEnhancement & Existing = itr->second;
            if (_stricmp(Existing.GetName().c_str(), NewCheatName.c_str()) != 0 || m_EditEnhancement == &Existing)
            {
                continue;
            }
            g_Notify->DisplayWarning(GS(MSG_CHEAT_NAME_IN_USE));
            m_Name->setFocus();
            return;
        }

        CEnhancement Enhancement(CEnhancement::CheatIdent);
        if (!ReadEnhancement(Enhancement))
        {
            return;
        }
        if (m_EditEnhancement != nullptr)
        {
            m_EditEnhancement->SetName(NewCheatName.c_str());
            m_EditEnhancement->SetAuthor(m_Author->text().toUtf8().constData());
            m_EditEnhancement->SetEntries(Enhancement.GetEntries());
            m_EditEnhancement->SetOptions(Enhancement.GetOptions());
            m_EditEnhancement->SetNote(m_EditNotesEdit->toPlainText().toUtf8().constData());
        }
        else
        {
            m_Cheats.AddItem(Enhancement);
        }
        g_Enhancements->UpdateCheats(m_Cheats);
        RecordCurrentValues();
        m_EditEnhancement = nullptr;
        m_Tree->Refresh(m_Cheats);
    }

    // CEditCheat::OnNewCheat
    void OnNewCheat()
    {
        if (ValuesChanged())
        {
            return;
        }
        m_EditEnhancement = nullptr;
        m_Name->clear();
        m_Author->clear();
        m_Codes->clear();
        m_Options->clear();
        m_EditNotesEdit->clear();
        m_Add->setText(Lang(CHEAT_ADDCHEAT_ADD));
        m_Add->setEnabled(false);
        SetOptionControlsEnabled(false);
        RecordCurrentValues();
    }

    void RecordCurrentValues()
    {
        m_EditName = m_Name->text();
        m_EditCode = m_Codes->toPlainText();
        m_EditOptions = m_Options->toPlainText();
        m_EditNotes = m_EditNotesEdit->toPlainText();
    }

    // CEditCheat::ValuesChanged: true when the user cancelled or the entry is invalid
    bool ValuesChanged()
    {
        if (m_EditPanel == nullptr)
        {
            return false;
        }
        if (m_EditName == m_Name->text() && m_EditCode == m_Codes->toPlainText() && m_EditOptions == m_Options->toPlainText() && m_EditNotes == m_EditNotesEdit->toPlainText())
        {
            return false;
        }
        const QMessageBox::StandardButton Result = QMessageBox::question(this, Lang(CHEAT_CHANGED_TITLE), Lang(CHEAT_CHANGED_MSG),
                                                                          QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
        if (Result == QMessageBox::Cancel)
        {
            return true;
        }
        if (Result == QMessageBox::Yes)
        {
            CEnhancement Enhancement(CEnhancement::CheatIdent);
            if (ReadEnhancement(Enhancement) && (Enhancement.CodeOptionSize() == 0 || Enhancement.GetOptions().size() > 0))
            {
                OnAddCheat();
            }
            else
            {
                QMessageBox::critical(this, Lang(MSG_CHEAT_INVALID_TITLE), Lang(MSG_CHEAT_INVALID_MSG));
                return true;
            }
        }
        RecordCurrentValues();
        return false;
    }

    CEnhancementList m_Cheats;
    EnhancementTree * m_Tree;
    QPlainTextEdit * m_Notes;
    QToolButton * m_Expand = nullptr;
    QWidget * m_EditPanel = nullptr;
    QLineEdit * m_Name = nullptr;
    QLineEdit * m_Author = nullptr;
    QPlainTextEdit * m_Codes = nullptr;
    QPlainTextEdit * m_Options = nullptr;
    QPlainTextEdit * m_EditNotesEdit = nullptr;
    QLabel * m_OptionsLabel = nullptr;
    QLabel * m_OptionsFormat = nullptr;
    QPushButton * m_Add = nullptr;
    CEnhancement * m_EditEnhancement;
    QString m_EditName, m_EditCode, m_EditOptions, m_EditNotes;
};
} // namespace

// CMainGui::DisplayCheatsUI: the emulation is paused while the dialog is open
void ShowCheatDialog(QWidget * Parent)
{
    if (g_Enhancements == nullptr)
    {
        return;
    }
    ConfigPause Pause;
    CheatDialog Dialog(Parent);
    Dialog.exec();
}
