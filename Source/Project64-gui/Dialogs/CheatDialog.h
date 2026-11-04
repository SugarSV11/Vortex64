#pragma once

class QWidget;

// Qt counterpart of CCheatsUI (Project64/UserInterface/CheatUI.cpp): cheat tree
// with tri-state check boxes, notes, code-extension chooser and (outside basic
// mode) the add/edit panel. Works on g_Enhancements like upstream.
void ShowCheatDialog(QWidget * Parent);
