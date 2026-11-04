#include "EnhancementTree.h"

#include <QString>

EnhancementTree::EnhancementTree(QWidget * Parent) :
    QTreeWidget(Parent),
    m_Updating(false)
{
    setHeaderHidden(true);
    setSelectionMode(QAbstractItemView::SingleSelection);
    connect(this, &QTreeWidget::itemChanged, this, &EnhancementTree::OnItemChanged);
}

CEnhancement * EnhancementTree::EnhancementOf(QTreeWidgetItem * Item) const
{
    if (Item == nullptr || Item->childCount() != 0)
    {
        return nullptr;
    }
    return reinterpret_cast<CEnhancement *>(Item->data(0, Qt::UserRole).value<quintptr>());
}

void EnhancementTree::Refresh(CEnhancementList & List)
{
    m_Updating = true;
    clear();
    for (CEnhancementList::iterator itr = List.begin(); itr != List.end(); itr++)
    {
        const std::string & Name = itr->second.GetNameAndExtension();
        if (Name.empty())
        {
            continue;
        }
        AddCodeLayers(&itr->second, QString::fromUtf8(Name.c_str()), nullptr, itr->second.Active());
    }
    sortItems(0, Qt::AscendingOrder);
    m_Updating = false;
}

void EnhancementTree::AddCodeLayers(CEnhancement * Enhancement, const QString & Name, QTreeWidgetItem * Parent, bool Active)
{
    const int Separator = Name.indexOf('\\');
    const QString Text = Separator >= 0 ? Name.left(Separator) : Name;
    const QString Rest = Separator >= 0 ? Name.mid(Separator + 1) : QString();

    const int Count = Parent != nullptr ? Parent->childCount() : topLevelItemCount();
    for (int i = 0; i < Count; i++)
    {
        QTreeWidgetItem * Child = Parent != nullptr ? Parent->child(i) : topLevelItem(i);
        if (Child->text(0) != Text)
        {
            continue;
        }
        const Qt::CheckState State = Child->checkState(0);
        if ((Active && State == Qt::Unchecked) || (!Active && State == Qt::Checked))
        {
            Child->setCheckState(0, Qt::PartiallyChecked);
        }
        AddCodeLayers(Enhancement, Rest, Child, Active);
        return;
    }

    QTreeWidgetItem * Item = Parent != nullptr ? new QTreeWidgetItem(Parent) : new QTreeWidgetItem(this);
    Item->setText(0, Text);
    Item->setData(0, Qt::UserRole, QVariant::fromValue<quintptr>(reinterpret_cast<quintptr>(Enhancement)));
    Item->setFlags(Item->flags() | Qt::ItemIsUserCheckable);
    Item->setCheckState(0, Active ? Qt::Checked : Qt::Unchecked);
    if (Rest.isEmpty())
    {
        return;
    }
    AddCodeLayers(Enhancement, Rest, Item, Active);
}

void EnhancementTree::ChangeChildrenStatus(QTreeWidgetItem * Item, bool Checked)
{
    const bool Root = Item == nullptr;
    const int Count = Root ? topLevelItemCount() : Item->childCount();
    if (Count == 0)
    {
        if (Root)
        {
            return;
        }
        CEnhancement * Enhancement = EnhancementOf(Item);
        if (Enhancement == nullptr)
        {
            return;
        }
        if (Checked && Enhancement->CodeOptionSize() != 0 && Enhancement->GetOptions().size() > 0 && !Enhancement->OptionSelected())
        {
            return;
        }
        Item->setCheckState(0, Checked ? Qt::Checked : Qt::Unchecked);
        Enhancement->SetActive(Checked);
        return;
    }
    Qt::CheckState State = Qt::PartiallyChecked;
    bool First = true;
    for (int i = 0; i < Count; i++)
    {
        QTreeWidgetItem * Child = Root ? topLevelItem(i) : Item->child(i);
        const Qt::CheckState ChildState = Child->checkState(0);
        if ((ChildState != Qt::Checked || !Checked) && (ChildState != Qt::Unchecked || Checked))
        {
            ChangeChildrenStatus(Child, Checked);
        }
        const Qt::CheckState After = Child->checkState(0);
        if (First)
        {
            State = After;
            First = false;
        }
        else if (State != After)
        {
            State = Qt::PartiallyChecked;
        }
    }
    if (!Root && !First)
    {
        Item->setCheckState(0, State);
    }
}

void EnhancementTree::CheckParentStatus(QTreeWidgetItem * Parent)
{
    if (Parent == nullptr)
    {
        return;
    }
    const Qt::CheckState Initial = Parent->checkState(0);
    Qt::CheckState Current = Parent->child(0)->checkState(0);
    for (int i = 1; i < Parent->childCount(); i++)
    {
        if (Parent->child(i)->checkState(0) != Current)
        {
            Current = Qt::PartiallyChecked;
            break;
        }
    }
    Parent->setCheckState(0, Current);
    if (Initial != Current)
    {
        CheckParentStatus(Parent->parent());
    }
}

void EnhancementTree::SetAllChecked(bool Checked)
{
    m_Updating = true;
    ChangeChildrenStatus(nullptr, Checked);
    m_Updating = false;
}

// A user click toggled a check box (CCheatList::OnTreeClicked).
void EnhancementTree::OnItemChanged(QTreeWidgetItem * Item, int Column)
{
    if (m_Updating || Column != 0)
    {
        return;
    }
    const bool Checked = Item->checkState(0) == Qt::Checked;
    m_Updating = true;
    if (Item->childCount() == 0)
    {
        CEnhancement * Enhancement = EnhancementOf(Item);
        if (Enhancement != nullptr && Checked && Enhancement->CodeOptionSize() != 0 && Enhancement->GetOptions().size() > 0 && !Enhancement->OptionSelected())
        {
            // A code with extensions needs a value before it can be switched on
            Item->setCheckState(0, Qt::Unchecked);
            m_Updating = false;
            Q_EMIT ChooseOption(Item);
            return;
        }
    }
    ChangeChildrenStatus(Item, Checked);
    CheckParentStatus(Item->parent());
    m_Updating = false;
    Q_EMIT ActiveChanged();
}
