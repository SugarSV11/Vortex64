#pragma once

#include <QString>

class MainWindow;

namespace Vortex
{
// QA tool: when VORTEX_SHOTS=<directory> is set, the application runs the steps of VORTEX_SHOTS_STEPS
// (comma separated) once the window is shown, saves a PNG of each "shot" step and exits. Used by
// Scripts/linux/vortex-shots.sh to compare the screens with the design.
//   seed            history and favourites for the first games of the library
//   page:<id>       library | recent | favorites | disks
//   mode:<mode>     grid | list | compact (current page)
//   select:<n>      select the n-th game of the current page
//   search:<text>   type in the search box
//   shot:<name>     save <dir>/<name>.png of the window
//   size:<w>x<h>    resize the window
//   wait:<ms>
//   dialog:settings:<category>  open the settings window on a category (non modal); shotdlg:<name> saves it; closedlg
//   lang:<es|en|auto>   the application changes language (as in Settings > General); missing  prints the untranslated catalog texts
//   debug:<view>        opens a debugger window (commands, memory, search, dump, symbols, stack, stacktrace, tlb, exceptions, dmalog, cpulog, scripts)
//   report:<tag>        prints whether a game runs and the library shows (state <tag> running=1 library=0 window=WxH)
//   play:<rom>          opens that game; endgame  ends the running game (the menu of the running game)
//   hover:<item view object name>:<row>  moves the pointer over a row of the view of the active window
//   quit
void RunShotScriptFromEnvironment(MainWindow * Window);
} // namespace Vortex
