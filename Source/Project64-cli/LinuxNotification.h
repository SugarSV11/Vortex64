#pragma once

#include <Project64-core/Notification.h>

// Minimal CNotification for a console frontend: every message goes to
// stdout/stderr instead of a Win32 message box or status bar. This is the
// only thing a frontend strictly has to implement to satisfy AppInit(); a
// GUI frontend (Qt6, planned) implements the same interface with real
// dialogs instead.
class CLinuxNotification : public CNotification
{
public:
    void DisplayError(const char * Message) const override;
    void DisplayError(LanguageStringID StringID) const override;

    void FatalError(const char * Message) const override;
    void FatalError(LanguageStringID StringID) const override;

    void DisplayWarning(const char * Message) const override;
    void DisplayWarning(LanguageStringID StringID) const override;

    void DisplayMessage(int DisplayTime, const char * Message) const override;
    void DisplayMessage(int DisplayTime, LanguageStringID StringID) const override;
    void DisplayMessage2(const char * Message) const override;

    bool AskYesNoQuestion(const char * Question) const override;

    void BreakPoint(const char * FileName, int32_t LineNumber) override;

    void AppInitDone(void) override;
    bool ProcessGuiMessages(void) const override;
    void ChangeFullScreen(void) const override;
};
