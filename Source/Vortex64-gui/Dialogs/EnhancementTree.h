#pragma once

#include <map>
#include <Vortex64-core/N64System/Enhancement/EnhancementList.h>

#include <QTreeWidget>

// The check box tree shared by the cheat and enhancement dialogs
// (CCheatList/CEnhancementUI: AddCodeLayers, ChangeChildrenStatus,
// CheckParentStatus). Names use '\' to nest entries; a parent shows the
// aggregate state of its children (partially checked when mixed).
class EnhancementTree : public QTreeWidget
{
    Q_OBJECT
public:
    explicit EnhancementTree(QWidget * Parent = nullptr);

    // Fills the tree from `List`; the list must outlive (and not be modified
    // during) the display, entries are referenced by pointer like upstream.
    void Refresh(CEnhancementList & List);
    void SetAllChecked(bool Checked);
    CEnhancement * EnhancementOf(QTreeWidgetItem * Item) const;
    // Applies the check to every leaf below `Item` (or the item itself)
    void ChangeChildrenStatus(QTreeWidgetItem * Item, bool Checked);

Q_SIGNALS:
    // A leaf became (in)active: the caller pushes the list to g_Enhancements.
    void ActiveChanged();
    // The user asked to choose the code extension of a leaf.
    void ChooseOption(QTreeWidgetItem * Item);

private Q_SLOTS:
    void OnItemChanged(QTreeWidgetItem * Item, int Column);

private:
    void AddCodeLayers(CEnhancement * Enhancement, const QString & Name, QTreeWidgetItem * Parent, bool Active);
    void CheckParentStatus(QTreeWidgetItem * Parent);
    bool m_Updating;
};
