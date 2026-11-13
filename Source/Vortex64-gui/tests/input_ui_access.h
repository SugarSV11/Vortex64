// How the tests see the rows of the Input dialog: each row is a capsule (QPushButton "VxScan", property "row" = the order of
// Setup) whose property "binding" holds the technical name of the assignment ("" when none) and "capturing" is true while it
// waits for an input. BindingRef reads like the field the tests used before the redesign: text() is the technical binding, or "..."
// while the row waits.
#pragma once
#include <QList>
#include <QPushButton>
#include <QString>
#include <QWidget>
#include <algorithm>

struct BindingRef
{
    QPushButton * W = nullptr;
    QString text() const
    {
        return W->property("capturing").toBool() ? QString("...") : W->property("binding").toString();
    }
    const BindingRef * operator->() const
    {
        return this;
    }
};

inline QList<BindingRef> BindingDisplays(QWidget * Page)
{
    QList<BindingRef> Result;
    for (QPushButton * B : Page->findChildren<QPushButton *>())
    {
        if (B->objectName() == "VxScan")
        {
            Result.push_back(BindingRef{B});
        }
    }
    std::sort(Result.begin(), Result.end(), [](const BindingRef & A, const BindingRef & B) { return A.W->property("row").toInt() < B.W->property("row").toInt(); });
    return Result;
}
