#include "EnhancementDialog.h"
#include <Project64-core/N64System/ConfigPause.h>
#include "../Vortex/Strings.h"
#include "EnhancementTree.h"

#include <Common/StdString.h>
#include <Project64-core/Multilanguage.h>
#include <Project64-core/N64System/Enhancement/Enhancements.h>
#include <Project64-core/N64System/N64System.h>
#include <Project64-core/N64System/SystemGlobals.h>

#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
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

// CEditPluginList
class EditPluginListDialog : public QDialog
{
public:
    EditPluginListDialog(CEnhancement::PluginList & List, QWidget * Parent) :
        QDialog(Parent),
        m_PluginList(List)
    {
        setWindowTitle(Tr("Edit Plugin List"));
        QVBoxLayout * Layout = new QVBoxLayout(this);
        QHBoxLayout * AddRow = new QHBoxLayout;
        m_Edit = new QLineEdit(this);
        QPushButton * Add = new QPushButton(Tr("Add"), this);
        AddRow->addWidget(m_Edit);
        AddRow->addWidget(Add);
        Layout->addLayout(AddRow);
        m_List = new QListWidget(this);
        m_List->setSortingEnabled(true);
        for (const std::string & Name : List)
        {
            m_List->addItem(Utf8(Name));
        }
        Layout->addWidget(m_List);
        QPushButton * Remove = new QPushButton(Tr("Remove"), this);
        Layout->addWidget(Remove, 0, Qt::AlignRight);
        QDialogButtonBox * Buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        Layout->addWidget(Buttons);
        connect(Add, &QPushButton::clicked, this, [this]() { OnAdd(); });
        connect(Remove, &QPushButton::clicked, this, [this]() { delete m_List->takeItem(m_List->currentRow()); });
        connect(Buttons, &QDialogButtonBox::accepted, this, [this]() { OnOk(); });
        connect(Buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    }

private:
    void OnAdd()
    {
        const QString Name = m_Edit->text().trimmed();
        if (Name.isEmpty())
        {
            return;
        }
        for (int i = 0; i < m_List->count(); i++)
        {
            if (m_List->item(i)->text() == Name)
            {
                return;
            }
        }
        m_Edit->clear();
        m_List->addItem(Name);
    }
    void OnOk()
    {
        CEnhancement::PluginList List;
        for (int i = 0; i < m_List->count(); i++)
        {
            if (!m_List->item(i)->text().isEmpty())
            {
                List.push_back(m_List->item(i)->text().toUtf8().constData());
            }
        }
        m_PluginList = List;
        accept();
    }

    CEnhancement::PluginList & m_PluginList;
    QLineEdit * m_Edit;
    QListWidget * m_List;
};

// CEditGS: the Game Shark code of an enhancement
class EditGameSharkDialog : public QDialog
{
public:
    EditGameSharkDialog(CEnhancement::CodeEntries & Entries, CEnhancement::PluginList & PluginList, QWidget * Parent) :
        QDialog(Parent),
        m_Entries(Entries),
        m_PluginList(PluginList)
    {
        setWindowTitle(Tr("Edit Gameshark"));
        QVBoxLayout * Layout = new QVBoxLayout(this);
        Layout->addWidget(new QLabel(Tr("Code:"), this));
        m_Codes = new QPlainTextEdit(this);
        stdstr Buffer;
        for (const CEnhancement::CodeEntry & Entry : Entries)
        {
            if (!Buffer.empty())
            {
                Buffer.append("\n");
            }
            Buffer += stdstr_f("%08X %s", Entry.Command, Entry.Value.c_str());
        }
        m_Codes->setPlainText(Utf8(Buffer));
        Layout->addWidget(m_Codes);
        QHBoxLayout * PluginRow = new QHBoxLayout;
        m_LimitPlugins = new QCheckBox(Tr("Limit on plugins"), this);
        m_LimitPlugins->setChecked(!PluginList.empty());
        QPushButton * EditPlugins = new QPushButton(Tr("Edit"), this);
        PluginRow->addWidget(m_LimitPlugins);
        PluginRow->addWidget(EditPlugins);
        Layout->addLayout(PluginRow);
        m_Buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        Layout->addWidget(m_Buttons);
        connect(m_Codes, &QPlainTextEdit::textChanged, this, [this]() {
            CEnhancement::CodeEntries Entries;
            m_Buttons->button(QDialogButtonBox::Ok)->setEnabled(CheatCode(Entries));
        });
        connect(m_LimitPlugins, &QCheckBox::clicked, this, [this]() { EditPluginList(); });
        connect(EditPlugins, &QPushButton::clicked, this, [this]() { EditPluginList(); });
        connect(m_Buttons, &QDialogButtonBox::accepted, this, [this]() {
            CEnhancement::CodeEntries Entries;
            if (CheatCode(Entries))
            {
                m_Entries = Entries;
                accept();
            }
        });
        connect(m_Buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    }

private:
    void EditPluginList()
    {
        EditPluginListDialog(m_PluginList, this).exec();
        m_LimitPlugins->setChecked(!m_PluginList.empty());
    }

    // CEditGS::CheatCode: every line must be "XXXXXXXX XXXX"
    bool CheatCode(CEnhancement::CodeEntries & Entries)
    {
        Entries.clear();
        for (const QString & Line : m_Codes->toPlainText().split('\n'))
        {
            if (Line.isEmpty())
            {
                continue;
            }
            const std::string Str = Line.toUpper().toUtf8().constData();
            char Format[128] = {0};
            for (size_t i = 0; i < sizeof(Format) - 1 && i < Str.size(); i++)
            {
                if (isxdigit((unsigned char)Str[i]))
                {
                    Format[i] = 'X';
                }
                else if (Str[i] == ' ')
                {
                    Format[i] = ' ';
                }
            }
            if (strcmp(Format, "XXXXXXXX XXXX") != 0)
            {
                return false;
            }
            CEnhancement::CodeEntry Entry;
            Entry.Command = strtoul(Str.c_str(), nullptr, 16);
            Entry.Value = strchr(Str.c_str(), ' ') + 1;
            Entries.push_back(Entry);
        }
        if (!Entries.empty())
        {
            CEnhancement Item("");
            Item.SetName("Test");
            Item.SetEntries(Entries);
            if (!Item.Valid() || Item.CodeOptionSize() != 0)
            {
                return false;
            }
        }
        return true;
    }

    CEnhancement::CodeEntries & m_Entries;
    CEnhancement::PluginList & m_PluginList;
    QPlainTextEdit * m_Codes;
    QCheckBox * m_LimitPlugins;
    QDialogButtonBox * m_Buttons;
};

// CEditEnhancement
class EditEnhancementDialog : public QDialog
{
public:
    EditEnhancementDialog(CEnhancementList & List, CEnhancement * Enhancement, QWidget * Parent) :
        QDialog(Parent),
        m_List(List),
        m_Edit(Enhancement)
    {
        setWindowTitle(Tr("Edit Enhancement"));
        if (Enhancement != nullptr)
        {
            m_Entries = Enhancement->GetEntries();
            m_PluginList = Enhancement->GetPluginList();
        }
        QVBoxLayout * Layout = new QVBoxLayout(this);
        m_Name = new QLineEdit(Enhancement != nullptr ? Utf8(Enhancement->GetName()) : QString(), this);
        m_Author = new QLineEdit(Enhancement != nullptr ? Utf8(Enhancement->GetAuthor()) : QString(), this);
        QHBoxLayout * NameRow = new QHBoxLayout;
        NameRow->addWidget(new QLabel(Tr("Name:"), this));
        NameRow->addWidget(m_Name);
        Layout->addLayout(NameRow);
        QHBoxLayout * AuthorRow = new QHBoxLayout;
        AuthorRow->addWidget(new QLabel(Tr("Author:"), this));
        AuthorRow->addWidget(m_Author);
        Layout->addLayout(AuthorRow);

        QHBoxLayout * GameSharkRow = new QHBoxLayout;
        m_GameShark = new QCheckBox(Tr("Game Shark Code"), this);
        m_GameShark->setChecked(!m_Entries.empty());
        QPushButton * EditGS = new QPushButton(Tr("Edit"), this);
        GameSharkRow->addWidget(m_GameShark);
        GameSharkRow->addWidget(EditGS);
        Layout->addLayout(GameSharkRow);

        QHBoxLayout * OverClockRow = new QHBoxLayout;
        m_OverClock = new QCheckBox(Tr("Overclock modifier:"), this);
        m_OverClock->setChecked(Enhancement != nullptr && Enhancement->OverClock());
        m_OverClockModifier = new QLineEdit(Enhancement != nullptr ? QString::number(Enhancement->OverClockModifier()) : QString("1"), this);
        m_OverClockModifier->setValidator(new QIntValidator(0, 1000000, this));
        m_OverClockModifier->setEnabled(m_OverClock->isChecked());
        OverClockRow->addWidget(m_OverClock);
        OverClockRow->addWidget(m_OverClockModifier);
        Layout->addLayout(OverClockRow);

        m_OnByDefault = new QCheckBox(Tr("On by default"), this);
        m_OnByDefault->setChecked(Enhancement != nullptr && Enhancement->GetOnByDefault());
        Layout->addWidget(m_OnByDefault);

        Layout->addWidget(new QLabel(Tr("Notes"), this));
        m_Notes = new QPlainTextEdit(Enhancement != nullptr ? Utf8(Enhancement->GetNote()) : QString(), this);
        m_Notes->setMaximumHeight(80);
        Layout->addWidget(m_Notes);

        QDialogButtonBox * Buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        Layout->addWidget(Buttons);

        connect(m_OverClock, &QCheckBox::toggled, m_OverClockModifier, &QWidget::setEnabled);
        connect(m_GameShark, &QCheckBox::clicked, this, [this]() { EditGameShark(); });
        connect(EditGS, &QPushButton::clicked, this, [this]() { EditGameShark(); });
        connect(Buttons, &QDialogButtonBox::accepted, this, [this]() { OnOk(); });
        connect(Buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    }

private:
    void EditGameShark()
    {
        EditGameSharkDialog(m_Entries, m_PluginList, this).exec();
        m_GameShark->setChecked(!m_Entries.empty());
    }

    void OnOk()
    {
        CEnhancement Temp(CEnhancement::EnhancementIdent);
        CEnhancement & Enhancement = m_Edit != nullptr ? *m_Edit : Temp;
        Enhancement.SetName(m_Name->text().toUtf8().constData());
        Enhancement.SetAuthor(m_Author->text().toUtf8().constData());
        Enhancement.SetOnByDefault(m_OnByDefault->isChecked());
        Enhancement.SetOverClock(m_OverClock->isChecked(), (uint32_t)m_OverClockModifier->text().toUInt());
        Enhancement.SetEntries(m_Entries);
        Enhancement.SetPluginList(m_PluginList);
        Enhancement.SetNote(m_Notes->toPlainText().toUtf8().constData());
        if (m_Edit == nullptr)
        {
            m_List.AddItem(Temp);
        }
        g_Enhancements->UpdateEnhancements(m_List);
        accept();
    }

    CEnhancementList & m_List;
    CEnhancement * m_Edit;
    CEnhancement::CodeEntries m_Entries;
    CEnhancement::PluginList m_PluginList;
    QLineEdit * m_Name;
    QLineEdit * m_Author;
    QCheckBox * m_GameShark;
    QCheckBox * m_OverClock;
    QLineEdit * m_OverClockModifier;
    QCheckBox * m_OnByDefault;
    QPlainTextEdit * m_Notes;
};

// CEnhancementUI
class EnhancementDialog : public QDialog
{
public:
    explicit EnhancementDialog(QWidget * Parent) :
        QDialog(Parent)
    {
        setObjectName("EnhancementDialog");
        setWindowTitle(Lang(ENHANCEMENT_TITLE));
        g_Enhancements->Load();
        m_Enhancements = g_Enhancements->Enhancements();

        QVBoxLayout * Layout = new QVBoxLayout(this);
        m_Tree = new EnhancementTree(this);
        m_Tree->setMinimumSize(360, 300);
        Layout->addWidget(m_Tree, 1);
        Layout->addWidget(new QLabel(Lang(CHEAT_NOTES_FRAME), this));
        m_Notes = new QPlainTextEdit(this);
        m_Notes->setReadOnly(true);
        m_Notes->setMaximumHeight(72);
        Layout->addWidget(m_Notes);
        QDialogButtonBox * Buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
        Layout->addWidget(Buttons);
        connect(Buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

        connect(m_Tree, &EnhancementTree::ActiveChanged, this, []() { g_Enhancements->UpdateCheats(); });
        connect(m_Tree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem * Item, int) {
            EditItem(Item);
        });
        connect(m_Tree, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem * Item, QTreeWidgetItem *) {
            m_Notes->setPlainText(QString());
            CEnhancement * Enhancement = m_Tree->EnhancementOf(Item);
            if (Enhancement != nullptr)
            {
                m_Notes->setPlainText(Utf8(Enhancement->GetNote()));
            }
        });
        m_Tree->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(m_Tree, &QWidget::customContextMenuRequested, this, [this](const QPoint & Pos) { OnContextMenu(Pos); });
        m_Tree->Refresh(m_Enhancements);
    }

private:
    void EditItem(QTreeWidgetItem * Item)
    {
        CEnhancement * Enhancement = m_Tree->EnhancementOf(Item);
        if (Enhancement == nullptr)
        {
            return;
        }
        EditEnhancementDialog(m_Enhancements, Enhancement, this).exec();
        m_Tree->Refresh(m_Enhancements);
    }

    // IDR_ENHANCEMENT_MENU: Add Enhancement..., Edit, -, Delete
    void OnContextMenu(const QPoint & Pos)
    {
        QTreeWidgetItem * Item = m_Tree->itemAt(Pos);
        QMenu Menu;
        QAction * Add = Menu.addAction(Tr("Add Enhancement..."));
        QAction * Edit = nullptr;
        QAction * Delete = nullptr;
        if (Item != nullptr)
        {
            Edit = Menu.addAction(Tr("Edit"));
            Menu.addSeparator();
            Delete = Menu.addAction(Tr("Delete"));
        }
        QAction * Chosen = Menu.exec(m_Tree->viewport()->mapToGlobal(Pos));
        if (Chosen == nullptr)
        {
            return;
        }
        if (Chosen == Add)
        {
            EditEnhancementDialog(m_Enhancements, nullptr, this).exec();
            m_Tree->Refresh(m_Enhancements);
        }
        else if (Chosen == Edit)
        {
            EditItem(Item);
        }
        else if (Chosen == Delete)
        {
            if (QMessageBox::question(this, Lang(MSG_DEL_TITLE), Lang(MSG_DEL_SURE), QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes)
            {
                return;
            }
            CEnhancement * Target = m_Tree->EnhancementOf(Item);
            m_Tree->SetAllChecked(false);
            for (CEnhancementList::iterator itr = m_Enhancements.begin(); itr != m_Enhancements.end(); itr++)
            {
                if (Target == &itr->second)
                {
                    m_Enhancements.erase(itr);
                    g_Enhancements->UpdateEnhancements(m_Enhancements);
                    break;
                }
            }
            m_Tree->Refresh(m_Enhancements);
        }
    }

    CEnhancementList m_Enhancements;
    EnhancementTree * m_Tree;
    QPlainTextEdit * m_Notes;
};
} // namespace

void ShowEnhancementDialog(QWidget * Parent)
{
    if (g_Enhancements == nullptr)
    {
        return;
    }
    ConfigPause Pause;
    EnhancementDialog Dialog(Parent);
    Dialog.exec();
}
