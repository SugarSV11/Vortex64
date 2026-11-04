#include "stdafx.h"
#include "../Vortex/Strings.h"
#include "DebugDialogs.h"
#include "DebugCommon.h"
#include "DebuggerUI.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>

using Vortex::Tr;

namespace DebugDialogs
{
void AddBreakpoint(CDebuggerUI * Debugger, QWidget * Parent)
{
    QDialog Dialog(Parent);
    Dialog.setWindowTitle(Tr("Add breakpoint"));
    QGridLayout * Layout = new QGridLayout(&Dialog);
    NumberEdit * Address = new NumberEdit(&Dialog);
    QCheckBox * Read = new QCheckBox(Tr("Read"), &Dialog);
    QCheckBox * Write = new QCheckBox(Tr("Write"), &Dialog);
    QCheckBox * Execute = new QCheckBox(Tr("Execute"), &Dialog);
    QPushButton * Ok = new QPushButton("OK", &Dialog);
    Ok->setDefault(true);
    Layout->addWidget(new QLabel(Tr("Address"), &Dialog), 0, 0);
    Layout->addWidget(Address, 1, 0, 3, 1, Qt::AlignTop);
    Layout->addWidget(Read, 0, 1);
    Layout->addWidget(Write, 1, 1);
    Layout->addWidget(Execute, 2, 1);
    Layout->addWidget(Ok, 3, 1);
    QObject::connect(Ok, &QPushButton::clicked, &Dialog, [&]() {
        // IDOK: stoul(text, 16) -> the empty string is treated as 0 here (upstream throws)
        const uint32_t Value = Address->Value();
        CBreakpoints * Breakpoints = Debugger->Breakpoints();
        if (Read->isChecked())
        {
            Breakpoints->RBPAdd(Value);
        }
        if (Write->isChecked())
        {
            Breakpoints->WBPAdd(Value);
        }
        if (Execute->isChecked())
        {
            Breakpoints->AddExecution(Value);
        }
        Dialog.accept();
    });
    Address->setFocus();
    Dialog.exec();
}

void CopyCommands(CDebuggerUI * Debugger, QWidget * Parent, int64_t InitAddress, int64_t InitCount)
{
    QDialog Dialog(Parent);
    Dialog.setWindowTitle(Tr("Copy Commands"));
    QGridLayout * Layout = new QGridLayout(&Dialog);
    NumberEdit * Address = new NumberEdit(&Dialog, NumberEdit::DisplayHex);
    NumberEdit * Count = new NumberEdit(&Dialog, NumberEdit::DisplayDec, 10);
    QPushButton * Ok = new QPushButton("OK", &Dialog);
    Ok->setDefault(true);
    Layout->addWidget(new QLabel(Tr("Start Address"), &Dialog), 0, 0);
    Layout->addWidget(new QLabel(Tr("Count"), &Dialog), 0, 1);
    Layout->addWidget(Address, 1, 0);
    Layout->addWidget(Count, 1, 1);
    Layout->addWidget(Ok, 2, 1);
    Address->setFocus();
    if (InitAddress >= 0)
    {
        Address->SetValue((uint32_t)InitAddress, true);
        Count->setFocus();
    }
    Count->SetValue(InitCount >= 0 ? (uint32_t)InitCount : 1, false);
    QObject::connect(Ok, &QPushButton::clicked, &Dialog, [&]() {
        if (!Address->HasText())
        {
            QMessageBox::critical(&Dialog, Tr("Error"), Tr("Address required"));
            return;
        }
        if (!Count->HasText())
        {
            QMessageBox::critical(&Dialog, Tr("Error"), Tr("Count required"));
            return;
        }
        Debugger->CopyCommands(Address->Value(), Count->Value());
        Dialog.accept();
    });
    Dialog.exec();
}

void AddSymbol(CDebuggerUI * Debugger, QWidget * Parent, int64_t InitAddress, int InitType)
{
    QDialog Dialog(Parent);
    Dialog.setWindowTitle(Tr("Add Symbol"));
    QGridLayout * Layout = new QGridLayout(&Dialog);
    NumberEdit * Address = new NumberEdit(&Dialog, NumberEdit::DisplayHex);
    QComboBox * Type = new QComboBox(&Dialog);
    QLineEdit * Name = new QLineEdit(&Dialog);
    QLineEdit * Description = new QLineEdit(&Dialog);
    QPushButton * Ok = new QPushButton("OK", &Dialog);
    Ok->setDefault(true);
    for (int i = 0;; i++)
    {
        const char * TypeName = CSymbolTable::m_SymbolTypes[i].name;
        if (TypeName == nullptr)
        {
            break;
        }
        Type->addItem(TypeName);
    }
    Layout->addWidget(new QLabel(Tr("Address"), &Dialog), 0, 0);
    Layout->addWidget(new QLabel(Tr("Type"), &Dialog), 0, 1);
    Layout->addWidget(new QLabel(Tr("Name"), &Dialog), 0, 2);
    Layout->addWidget(Address, 1, 0);
    Layout->addWidget(Type, 1, 1);
    Layout->addWidget(Name, 1, 2);
    Layout->addWidget(new QLabel(Tr("Description"), &Dialog), 2, 0, 1, 3);
    Layout->addWidget(Description, 3, 0, 1, 3);
    Layout->addWidget(Ok, 4, 2);
    Address->setFocus();
    if (InitAddress >= 0)
    {
        Address->SetValue((uint32_t)InitAddress, true);
        Type->setFocus();
    }
    if (InitType >= 0)
    {
        Type->setCurrentIndex(InitType);
        Name->setFocus();
    }
    else
    {
        Type->setCurrentIndex(SYM_DATA);
    }
    QObject::connect(Ok, &QPushButton::clicked, &Dialog, [&]() {
        if (!Address->HasText())
        {
            QMessageBox::critical(&Dialog, Tr("Error"), Tr("Address required"));
            return;
        }
        if (Name->text().isEmpty() && Description->text().isEmpty())
        {
            QMessageBox::critical(&Dialog, Tr("Error"), Tr("Name and/or description required"));
            return;
        }
        Debugger->SymbolTable()->AddSymbol(Type->currentIndex(), Address->Value(), Name->text().toUtf8().constData(), Description->text().toUtf8().constData());
        Debugger->SymbolTable()->Save();
        Debugger->Debug_RefreshSymbolsWindow();
        Dialog.accept();
    });
    Dialog.exec();
}

bool SetValueText(QWidget * Parent, const QString & Caption, const QString & Label, const std::string & InitialText, std::string & Entered)
{
    QDialog Dialog(Parent);
    Dialog.setWindowTitle(Caption);
    QGridLayout * Layout = new QGridLayout(&Dialog);
    Layout->addWidget(new QLabel(Label, &Dialog), 0, 0);
    QLineEdit * Edit = new QLineEdit(QString::fromStdString(InitialText), &Dialog);
    Edit->setMinimumWidth(240);
    Layout->addWidget(Edit, 1, 0);
    QDialogButtonBox * Buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &Dialog);
    Layout->addWidget(Buttons, 2, 0);
    QObject::connect(Buttons, &QDialogButtonBox::accepted, &Dialog, &QDialog::accept);
    QObject::connect(Buttons, &QDialogButtonBox::rejected, &Dialog, &QDialog::reject);
    Edit->selectAll();
    if (Dialog.exec() != QDialog::Accepted)
    {
        return false;
    }
    Entered = Edit->text().toStdString();
    return true;
}

bool SetValueCombo(QWidget * Parent, const QString & Caption, const QString & Label, uintptr_t InitialData, const ComboItem * Items, uintptr_t & EnteredData)
{
    QDialog Dialog(Parent);
    Dialog.setWindowTitle(Caption);
    QGridLayout * Layout = new QGridLayout(&Dialog);
    Layout->addWidget(new QLabel(Label, &Dialog), 0, 0);
    QComboBox * Combo = new QComboBox(&Dialog);
    for (int i = 0; Items[i].text != nullptr; i++)
    {
        Combo->addItem(Tr(Items[i].text), QVariant::fromValue<qulonglong>(Items[i].data));
        if (Items[i].data == InitialData)
        {
            Combo->setCurrentIndex(i);
        }
    }
    Layout->addWidget(Combo, 1, 0);
    QDialogButtonBox * Buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &Dialog);
    Layout->addWidget(Buttons, 2, 0);
    QObject::connect(Buttons, &QDialogButtonBox::accepted, &Dialog, &QDialog::accept);
    QObject::connect(Buttons, &QDialogButtonBox::rejected, &Dialog, &QDialog::reject);
    if (Dialog.exec() != QDialog::Accepted)
    {
        return false;
    }
    EnteredData = (uintptr_t)Combo->currentData().toULongLong();
    return true;
}
} // namespace DebugDialogs
