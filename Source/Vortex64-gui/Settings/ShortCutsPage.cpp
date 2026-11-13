#include "ShortCutsPage.h"
#include "../Vortex/Strings.h"
#include "../VirtualKeys.h"

#include <Vortex64-core/Notification.h>
#include <Vortex64-core/Settings.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QGroupBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace
{
QString CleanTitle(const QString & Title)
{
    QString Result = Title;
    Result.remove('&');
    Result.remove("...");
    return Result;
}

// IDD_Key_Prompt: "Press a key to be used as an accelerator"
class KeyPromptDialog : public QDialog
{
public:
    explicit KeyPromptDialog(QWidget * Parent) :
        QDialog(Parent)
    {
        setWindowTitle(QtGS(ACCEL_DETECTKEY));
        QVBoxLayout * Layout = new QVBoxLayout(this);
        Layout->addWidget(new QLabel(Vortex::Tr("Pulsa una tecla para usarla como atajo", "Press a key to be used as an accelerator"), this));
        QPushButton * Cancel = new QPushButton(Vortex::Tr("Cancelar", "Cancel"), this);
        Layout->addWidget(Cancel);
        connect(Cancel, &QPushButton::clicked, this, &QDialog::reject);
    }

    int VirtualKey = 0;
    bool Ctrl = false, Alt = false, Shift = false;

protected:
    void keyPressEvent(QKeyEvent * Event) override
    {
        const int Key = Event->key();
        if (Key == Qt::Key_Shift || Key == Qt::Key_Control || Key == Qt::Key_Alt || Key == Qt::Key_Meta || Key == Qt::Key_AltGr)
        {
            return;
        }
        const int Virtual = VirtualKeyFromQtKey(Key);
        if (Virtual == 0)
        {
            return;
        }
        VirtualKey = Virtual;
        Ctrl = (Event->modifiers() & Qt::ControlModifier) != 0;
        Alt = (Event->modifiers() & Qt::AltModifier) != 0;
        Shift = (Event->modifiers() & Qt::ShiftModifier) != 0;
        accept();
    }
};
} // namespace

QtShortCutsPage::QtShortCutsPage(QWidget * parent) :
    QtSettingsPage(parent),
    m_EnableReset(false)
{
    QVBoxLayout * Layout = new QVBoxLayout(this);

    QHBoxLayout * StateRow = new QHBoxLayout;
    StateRow->addWidget(new QLabel(QtGS(ACCEL_CPUSTATE_TITLE), this));
    m_CpuState = new QComboBox(this);
    m_CpuState->addItem(QtGS(ACCEL_CPUSTATE_1), (int)CMenuShortCutKey::RUNNING_STATE_NOT_RUNNING);
    m_CpuState->addItem(QtGS(ACCEL_CPUSTATE_3), (int)CMenuShortCutKey::RUNNING_STATE_WINDOWED);
    m_CpuState->addItem(QtGS(ACCEL_CPUSTATE_4), (int)CMenuShortCutKey::RUNNING_STATE_FULLSCREEN);
    StateRow->addWidget(m_CpuState, 1);
    Layout->addLayout(StateRow);

    QHBoxLayout * Lists = new QHBoxLayout;
    QVBoxLayout * ItemColumn = new QVBoxLayout;
    ItemColumn->addWidget(new QLabel(QtGS(ACCEL_MENUITEM_TITLE), this));
    m_MenuItems = new QTreeWidget(this);
    m_MenuItems->setHeaderHidden(true);
    ItemColumn->addWidget(m_MenuItems);
    Lists->addLayout(ItemColumn, 2);
    QVBoxLayout * KeyColumn = new QVBoxLayout;
    KeyColumn->addWidget(new QLabel(QtGS(ACCEL_CURRENTKEYS_TITLE), this));
    m_CurrentKeys = new QListWidget(this);
    m_CurrentKeys->setSortingEnabled(true);
    KeyColumn->addWidget(m_CurrentKeys);
    QPushButton * Remove = new QPushButton(QtGS(ACCEL_REMOVE_BTN), this);
    KeyColumn->addWidget(Remove);
    Lists->addLayout(KeyColumn, 1);
    Layout->addLayout(Lists, 1);

    QGroupBox * NewKey = new QGroupBox(QtGS(ACCEL_SELKEY_TITLE), this);
    QGridLayout * NewKeyLayout = new QGridLayout(NewKey);
    m_VirtualKeyList = new QComboBox(NewKey);
    int Size = 0;
    VIRTUAL_KEY * VirtualKeys = CMenuShortCutKey::VirtualKeyList(Size);
    for (int i = 0; i < Size; i++)
    {
        m_VirtualKeyList->addItem(QString::fromWCharArray(VirtualKeys[i].Name), VirtualKeys[i].Key);
    }
    m_VirtualKeyList->setCurrentIndex(-1);
    QPushButton * Detect = new QPushButton(QtGS(ACCEL_DETECTKEY), NewKey);
    QPushButton * Assign = new QPushButton(QtGS(ACCEL_ASSIGN_BTN), NewKey);
    m_Ctrl = new QCheckBox("&Ctrl", NewKey);
    m_Alt = new QCheckBox("A&lt", NewKey);
    m_Shift = new QCheckBox("&Shift", NewKey);
    m_Assigned = new QLabel(NewKey);
    NewKeyLayout->addWidget(m_VirtualKeyList, 0, 0, 1, 2);
    NewKeyLayout->addWidget(Detect, 0, 2);
    NewKeyLayout->addWidget(Assign, 0, 3);
    NewKeyLayout->addWidget(m_Ctrl, 1, 0);
    NewKeyLayout->addWidget(m_Alt, 1, 1);
    NewKeyLayout->addWidget(m_Shift, 1, 2);
    NewKeyLayout->addWidget(new QLabel(QtGS(ACCEL_ASSIGNEDTO_TITLE), NewKey), 2, 0, 1, 2);
    NewKeyLayout->addWidget(m_Assigned, 2, 2, 1, 2);
    Layout->addWidget(NewKey);

    connect(m_CpuState, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() { OnCpuStateChanged(); });
    connect(m_MenuItems, &QTreeWidget::currentItemChanged, this, [this]() { OnMenuItemChanged(); });
    connect(Remove, &QPushButton::clicked, this, &QtShortCutsPage::OnRemoveClicked);
    connect(Detect, &QPushButton::clicked, this, &QtShortCutsPage::OnDetectKeyClicked);
    connect(Assign, &QPushButton::clicked, this, &QtShortCutsPage::OnAssignClicked);
    connect(m_Ctrl, &QCheckBox::clicked, this, &QtShortCutsPage::OnShortCutChanged);
    connect(m_Alt, &QCheckBox::clicked, this, &QtShortCutsPage::OnShortCutChanged);
    connect(m_Shift, &QCheckBox::clicked, this, &QtShortCutsPage::OnShortCutChanged);
    connect(m_VirtualKeyList, QOverload<int>::of(&QComboBox::activated), this, [this]() { OnShortCutChanged(); });

    OnCpuStateChanged();
    CheckResetEnable();
}

LanguageStringID QtShortCutsPage::PageTitle()
{
    return TAB_SHORTCUTS;
}

void QtShortCutsPage::UpdatePageSettings()
{
}

QtShortCutsPage::RUNNING_STATE QtShortCutsPage::CurrentRunningState() const
{
    return (RUNNING_STATE)m_CpuState->currentData().toInt();
}

void QtShortCutsPage::CheckResetEnable()
{
    for (const auto & Item : m_ShortCuts.GetShortCuts())
    {
        for (const CMenuShortCutKey & ShortCut : Item.second.GetAccelItems())
        {
            if (ShortCut.Inactive() || ShortCut.UserAdded())
            {
                m_EnableReset = true;
                return;
            }
        }
    }
    m_EnableReset = false;
}

// COptionsShortCutsPage::OnCpuStateChanged: the items available in this CPU
// state grouped by menu; items with user changes are bold.
void QtShortCutsPage::OnCpuStateChanged()
{
    const RUNNING_STATE RunningState = CurrentRunningState();
    m_MenuItems->clear();
    m_CurrentKeys->clear();
    std::map<int, QTreeWidgetItem *> Parents;
    for (const auto & Item : m_ShortCuts.GetShortCuts())
    {
        if (!Item.second.Avaliable(RunningState))
        {
            continue;
        }
        QTreeWidgetItem *& Parent = Parents[Item.second.Section()];
        if (Parent == nullptr)
        {
            Parent = new QTreeWidgetItem(m_MenuItems, QStringList(QtGS(Item.second.Section())));
        }
        QTreeWidgetItem * Entry = new QTreeWidgetItem(Parent, QStringList(CleanTitle(QtGS(Item.second.Title()))));
        Entry->setData(0, Qt::UserRole, QVariant::fromValue<quintptr>(reinterpret_cast<quintptr>(&Item.second)));
        for (const CMenuShortCutKey & ShortCut : Item.second.GetAccelItems())
        {
            if (!ShortCut.Inactive() && !ShortCut.UserAdded())
            {
                continue;
            }
            QFont Font = Entry->font(0);
            Font.setBold(true);
            Entry->setFont(0, Font);
            Parent->setFont(0, Font);
            break;
        }
    }
    m_MenuItems->expandAll();
}

void QtShortCutsPage::OnMenuItemChanged()
{
    RefreshShortCutOptions(m_MenuItems->currentItem());
}

void QtShortCutsPage::RefreshShortCutOptions(QTreeWidgetItem * Item)
{
    m_CurrentKeys->clear();
    if (Item == nullptr || Item->parent() == nullptr)
    {
        return;
    }
    CShortCutItem * ShortCut = reinterpret_cast<CShortCutItem *>(Item->data(0, Qt::UserRole).value<quintptr>());
    const RUNNING_STATE RunningState = CurrentRunningState();
    for (const CMenuShortCutKey & Key : ShortCut->GetAccelItems())
    {
        if (Key.Inactive() || !Key.Active(RunningState))
        {
            continue;
        }
        QListWidgetItem * Entry = new QListWidgetItem(QString::fromWCharArray(Key.Name().c_str()), m_CurrentKeys);
        Entry->setData(Qt::UserRole, QVariant::fromValue<quintptr>(reinterpret_cast<quintptr>(&Key)));
    }
}

void QtShortCutsPage::OnRemoveClicked()
{
    QTreeWidgetItem * Selected = m_MenuItems->currentItem();
    if (Selected == nullptr || Selected->parent() == nullptr || m_CurrentKeys->currentItem() == nullptr)
    {
        g_Notify->DisplayWarning(GS(MSG_NO_SEL_SHORTCUT));
        return;
    }
    CShortCutItem * ShortCut = reinterpret_cast<CShortCutItem *>(Selected->data(0, Qt::UserRole).value<quintptr>());
    CMenuShortCutKey * Key = reinterpret_cast<CMenuShortCutKey *>(m_CurrentKeys->currentItem()->data(Qt::UserRole).value<quintptr>());
    ShortCut->RemoveItem(Key);
    QFont Font = Selected->font(0);
    Font.setBold(true);
    Selected->setFont(0, Font);
    Selected->parent()->setFont(0, Font);
    m_EnableReset = true;
    RefreshShortCutOptions(Selected);
    Q_EMIT PageChanged();
}

void QtShortCutsPage::OnDetectKeyClicked()
{
    KeyPromptDialog Dialog(this);
    if (Dialog.exec() != QDialog::Accepted)
    {
        return;
    }
    const int Index = m_VirtualKeyList->findData(Dialog.VirtualKey);
    if (Index < 0)
    {
        return;
    }
    m_VirtualKeyList->setCurrentIndex(Index);
    m_Ctrl->setChecked(Dialog.Ctrl);
    m_Alt->setChecked(Dialog.Alt);
    m_Shift->setChecked(Dialog.Shift);
    OnShortCutChanged();
}

void QtShortCutsPage::OnAssignClicked()
{
    if (m_VirtualKeyList->currentIndex() < 0)
    {
        g_Notify->DisplayWarning(GS(MSG_NO_SHORTCUT_SEL));
        return;
    }
    const uint16_t Key = (uint16_t)m_VirtualKeyList->currentData().toInt();
    const bool Ctrl = m_Ctrl->isChecked(), Alt = m_Alt->isChecked(), Shift = m_Shift->isChecked();
    const RUNNING_STATE RunningState = CurrentRunningState();

    QTreeWidgetItem * Selected = m_MenuItems->currentItem();
    if (Selected == nullptr || Selected->parent() == nullptr)
    {
        g_Notify->DisplayWarning(GS(MSG_NO_MENUITEM_SEL));
        return;
    }
    CShortCutItem * ShortCut = reinterpret_cast<CShortCutItem *>(Selected->data(0, Qt::UserRole).value<quintptr>());
    if (m_ShortCuts.GetMenuItemName(Key, Ctrl, Alt, Shift, RunningState) != EMPTY_STRING)
    {
        g_Notify->DisplayWarning(GS(MSG_MENUITEM_ASSIGNED));
        return;
    }
    CMenuShortCutKey::ACCESS_MODE AccessMode = CMenuShortCutKey::ACCESS_NONE;
    switch (RunningState)
    {
    case CMenuShortCutKey::RUNNING_STATE_NOT_RUNNING: AccessMode = CMenuShortCutKey::ACCESS_GAME_NOT_RUNNING; break;
    case CMenuShortCutKey::RUNNING_STATE_WINDOWED: AccessMode = CMenuShortCutKey::ACCESS_GAME_RUNNING; break;
    case CMenuShortCutKey::RUNNING_STATE_FULLSCREEN: AccessMode = CMenuShortCutKey::ACCESS_GAME_RUNNING_FULLSCREEN; break;
    }
    ShortCut->AddShortCut(Key, Ctrl, Alt, Shift, AccessMode, true, false);
    QFont Font = Selected->font(0);
    Font.setBold(true);
    Selected->setFont(0, Font);
    Selected->parent()->setFont(0, Font);
    m_EnableReset = true;
    RefreshShortCutOptions(Selected);
    Q_EMIT PageChanged();
}

// Shows which menu item the chosen key combination is bound to
void QtShortCutsPage::OnShortCutChanged()
{
    if (m_VirtualKeyList->currentIndex() < 0)
    {
        return;
    }
    const uint16_t Key = (uint16_t)m_VirtualKeyList->currentData().toInt();
    stdstr Name = GS(m_ShortCuts.GetMenuItemName(Key, m_Ctrl->isChecked(), m_Alt->isChecked(), m_Shift->isChecked(), CurrentRunningState()));
    if (Name.length() > 0)
    {
        Name.resize(std::remove(Name.begin(), Name.end(), '&') - Name.begin());
    }
    else
    {
        Name = "None";
    }
    m_Assigned->setText(QString::fromUtf8(Name.c_str()));
}

void QtShortCutsPage::ApplySettings(bool /*UpdateScreen*/)
{
    m_ShortCuts.Save();
    UISettingsSaveBool(Info_ShortCutsChanged, true);
}

bool QtShortCutsPage::EnableReset()
{
    return m_EnableReset;
}

void QtShortCutsPage::ResetPage()
{
    m_EnableReset = false;
    m_ShortCuts.Load(true);
    OnCpuStateChanged();
    Q_EMIT PageChanged();
}
