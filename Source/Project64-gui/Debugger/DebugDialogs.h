#pragma once

#include <stdint.h>
#include <string>

class CDebuggerUI;
class QWidget;

// Qt counterparts of CAddBreakpointDlg, CCopyCommandsDlg and CAddSymbolDlg
// (Debugger-AddBreakpoint/CopyCommands/AddSymbol.cpp). Each is modal and returns when closed.
namespace DebugDialogs
{
void AddBreakpoint(CDebuggerUI * Debugger, QWidget * Parent);
// InitAddress/InitCount: pass -1 (as int64_t) when not given
void CopyCommands(CDebuggerUI * Debugger, QWidget * Parent, int64_t InitAddress = -1, int64_t InitCount = -1);
void AddSymbol(CDebuggerUI * Debugger, QWidget * Parent, int64_t InitAddress = -1, int InitType = -1);

// CSetValueDlg counterpart (Debugger-MemorySearch.h): a prompt with a text box, or with a
// combo box of (text, data) items. Return false when cancelled.
struct ComboItem
{
    const char * text;
    uintptr_t data;
};
bool SetValueText(QWidget * Parent, const QString & Caption, const QString & Label, const std::string & InitialText, std::string & Entered);
bool SetValueCombo(QWidget * Parent, const QString & Caption, const QString & Label, uintptr_t InitialData, const ComboItem * Items, uintptr_t & EnteredData);
} // namespace DebugDialogs
