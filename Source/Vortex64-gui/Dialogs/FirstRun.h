#pragma once

class MainWindow;

// First run (no language loaded in the core yet): there is no wizard and nothing is asked. The ordinary main window opens at once
// with the defaults saved: Spanish, GLideN64 as the video plugin (when it is installed) and an empty library. Everything stays
// changeable from the Settings window. Does nothing when the language was already set, so later runs are untouched.
void ApplyFirstRunDefaults(MainWindow * Window);
