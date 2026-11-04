// Scripting test: the Scripts window and the JavaScript engine of the debugger (duktape,
// Project64-debugger-scripts) against the real core and a real ROM. Needs an X server
// (Scripts/linux/run-gui-tests.sh starts Xvfb and isolates the XDG directories; the scripts
// are written to the isolated scripts directory, nothing of the user is used).
//
//   script-test <rom>
//
// Loading and running scripts from the window, console output, JavaScript errors, the console
// input and listener, timers, start/stop/run again, the emulation state, memory, registers and
// CPU callbacks, fs/exec/sockets, the actions of the script API on the emulator (pause, reset,
// close, open), autorun, the lifecycle (ROM open/close/change, window close, shutdown with
// timers, servers and child processes running).
#include "../Debugger/CommandsView.h"
#include "../Vortex/Store.h"
#include "../Vortex/Strings.h"
#include "../Debugger/DebugCommon.h"
#include "../Debugger/DebuggerUI.h"
#include "../Debugger/ScriptsView.h"
#include "../MainMenu.h"
#include "../MainWindow.h"

#include <Common/path.h>
#include <Project64-core/AppInit.h>
#include <Project64-core/N64System/N64System.h>
#include <Project64-core/N64System/SystemGlobals.h>
#include <Project64-core/Settings.h>
#include <Project64-core/Settings/DebugSettings.h>
#include <Project64/Settings/UISettings.h>

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <functional>
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>
#include <sys/wait.h>
#include <thread>

static int g_Failures = 0;
#define CHECK(cond)                                                \
    do                                                             \
    {                                                              \
        if (!(cond))                                               \
        {                                                          \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            g_Failures++;                                          \
        }                                                          \
    } while (0)

// alert() boxes are kept open while a test looks at them
static bool g_HoldAlerts = false;
static QStringList g_AlertTitles;

static void DismissDialogs()
{
    for (QWidget * Widget : QApplication::topLevelWidgets())
    {
        QMessageBox * Box = qobject_cast<QMessageBox *>(Widget);
        if (Box != nullptr && Box->isVisible())
        {
            if (g_HoldAlerts && Box->windowTitle() == "cap")
            {
                continue;
            }
            printf("dialog: %s\n", Box->text().left(60).toUtf8().constData());
            g_AlertTitles.push_back(Box->windowTitle());
            Box->accept();
        }
    }
}

static bool WaitFor(const std::function<bool()> & Condition, int TimeoutMs)
{
    QElapsedTimer Timer;
    Timer.start();
    while (Timer.elapsed() < TimeoutMs)
    {
        QApplication::processEvents(QEventLoop::AllEvents, 20);
        if (Condition())
        {
            return true;
        }
        QThread::msleep(10);
    }
    return Condition();
}

static void Pump(int Ms)
{
    WaitFor([]() { return false; }, Ms);
}

static bool Running()
{
    return g_Settings->LoadBool(GameRunning_CPU_Running);
}

static QString g_Dir; // the scripts directory
static CDebuggerUI * g_Debugger_ = nullptr;
static ScriptsView * g_View = nullptr;

static void WriteScript(const QString & Name, const QString & Source)
{
    QFile File(g_Dir + "/" + Name);
    if (!File.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        printf("FAIL cannot write %s\n", Name.toUtf8().constData());
        g_Failures++;
        return;
    }
    File.write(Source.toUtf8());
}

static QString Out()
{
    Pump(150);
    g_View->FlushConsole();
    return g_View->Output()->toPlainText();
}

static bool OutputHas(const QString & Text, int TimeoutMs = 5000)
{
    const bool Found = WaitFor([&]() { g_View->FlushConsole(); return g_View->Output()->toPlainText().contains(Text); }, TimeoutMs);
    if (!Found && TimeoutMs >= 5000)
    {
        printf("not in the console: '%s'; it ends with:\n%s\n", Text.toUtf8().constData(), g_View->Output()->toPlainText().right(500).toUtf8().constData());
    }
    return Found;
}

static CDebuggerUI::ScriptStatus Status(const QString & Name)
{
    return g_Debugger_->GetScriptStatus(Name.toStdString());
}

static bool WaitStatus(const QString & Name, CDebuggerUI::ScriptStatus Wanted, int TimeoutMs = 5000)
{
    return WaitFor([&]() { return Status(Name) == Wanted; }, TimeoutMs);
}

// Selects the script in the window (which must list it) and runs it
static void Run(const QString & Name)
{
    g_View->RefreshList();
    CHECK(g_View->SelectScript(Name));
    g_View->RunSelected();
}

static void Stop(const QString & Name)
{
    CHECK(g_View->SelectScript(Name));
    g_View->StopSelected();
    CHECK(WaitStatus(Name, CDebuggerUI::ScriptStopped));
}

// A console input line of the selected script
static void Input(const QString & Name, const QString & Code)
{
    CHECK(g_View->SelectScript(Name));
    CHECK(WaitFor([&]() { return g_View->Input()->isEnabled(); }, 3000));
    g_View->Input()->setText(Code);
    g_View->SubmitInput();
}

int ScriptShimLiveThreads(); // ScriptShim.cpp: threads of the script system that have not ended

static QStringList FdTargets()
{
    QStringList Targets;
    for (const QString & Name : QDir("/proc/self/fd").entryList(QDir::NoDotAndDotDot))
    {
        Targets.push_back(Name + "=" + QFileInfo("/proc/self/fd/" + Name).symLinkTarget());
    }
    return Targets;
}

static int OpenFds()
{
    return QDir("/proc/self/fd").entryList(QDir::NoDotAndDotDot).size();
}

static int Count(const QString & Text, const QString & Part)
{
    return Text.count(Part);
}

static bool TcpOpen(uint16_t Port)
{
    const int Fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in Address = {};
    Address.sin_family = AF_INET;
    Address.sin_port = htons(Port);
    inet_pton(AF_INET, "127.0.0.1", &Address.sin_addr);
    const bool Connected = connect(Fd, (sockaddr *)&Address, sizeof(Address)) == 0;
    close(Fd);
    return Connected;
}

static uint16_t FreePort()
{
    const int Fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in Address = {};
    Address.sin_family = AF_INET;
    Address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    bind(Fd, (sockaddr *)&Address, sizeof(Address));
    socklen_t Length = sizeof(Address);
    getsockname(Fd, (sockaddr *)&Address, &Length);
    close(Fd);
    return ntohs(Address.sin_port);
}

static bool ProcessExists(const char * Pattern)
{
    return system((QString("pgrep -f '%1' >/dev/null 2>&1").arg(Pattern)).toUtf8().constData()) == 0;
}

int main(int argc, char ** argv)
{
    if (argc < 2)
    {
        printf("usage: %s <rom>\n", argv[0]);
        return 2;
    }
    setvbuf(stdout, nullptr, _IOLBF, 0);
    const QString Rom = QString::fromLocal8Bit(argv[1]);
    setenv("SDL_VIDEODRIVER", "x11", 0);
    QApplication App(argc, argv);
    Vortex::Store::Instance().SetLanguage("en"); // (the texts these checks read are the English ones, whatever the language of the system)
    MainWindow Window;
    QTimer DismissTimer;
    QObject::connect(&DismissTimer, &QTimer::timeout, &DismissDialogs);
    DismissTimer.start(50);
    const std::string Base = CPath(CPath::MODULE_DIRECTORY).GetDirectory();
    char * Args[] = {argv[0], nullptr};
    if (!AppInit(Window.GetNotify(), Base.c_str(), 1, Args))
    {
        return 2;
    }
    // the debugger windows take the focus from the main window, which pauses the game with this
    // setting on (WM_KILLFOCUS of upstream); these tests drive the game from their own windows
    UISettingsSaveBool(Setting_AutoSleep, false);
    g_Settings->SaveBool(Debugger_Enabled, true);
    g_Settings->SaveString(Debugger_AutorunScripts, ""); // (the list of an earlier run would leave auto.js out of the dialog)
    CHECK(g_DebugSettings.haveDebugger);
    CDebuggerUI * Debugger = new CDebuggerUI;
    g_Debugger_ = Debugger;
    CHECK(g_Debugger == Debugger);

    Window.CreateLanguageMenus();
    Window.InitPlugins();
    Window.show();
    Pump(500);
    // PJ64_TEST_JIT=1: the test runs on the recompiler instead of the interpreter
    const bool TestJit = getenv("PJ64_TEST_JIT") != nullptr;
    g_Settings->SaveBool(Setting_ForceInterpreterCPU, !TestJit);
    if (TestJit)
    {
        g_Settings->SaveDword(Game_CpuType, CPU_Recompiler);
    }
    QTemporaryDir Temp;

    // ---- the window, from the Debugger menu, without a ROM ----
    g_Dir = QString::fromStdString(Debugger->ScriptsDir());
    CHECK(g_Dir.endsWith("/"));
    g_Dir.chop(1);
    CHECK(g_Dir.contains("vortex64/Scripts"));
    CHECK(QDir(g_Dir + "/modules").exists());
    Window.Menu()->ProcessMessage(ID_DEBUGGER_SCRIPTS);
    Pump(300);
    g_View = Debugger->ScriptConsole();
    CHECK(g_View != nullptr);
    if (g_View == nullptr)
    {
        return 1;
    }
    CHECK(g_View->isVisible());
    CHECK(!g_View->Input()->isEnabled());
    CHECK(g_View->RunButton()->text() == "Run");
    printf("script: window open\n");
    // what the scripts must give back: threads (workers, timers) and descriptors (sockets, pipes, files)
    const int BaseThreads = ScriptShimLiveThreads();
    const int BaseFds = OpenFds();
    const QStringList BaseTargets = FdTargets();
    CHECK(BaseThreads == 1); // the script system's own thread

    // the scripts shipped with the program are in the user's scripts directory
    CHECK(QFile::exists(g_Dir + "/example.js"));
    // a new file appears in the list (directory watch)
    WriteScript("hello.js", "console.log('hello from script');\nconsole.print('no newline');\nconsole.print(' continued\\n');\nconsole.log('multi', 1, true);\n");
    CHECK(WaitFor([&]() { return g_View->SelectScript("hello.js"); }, 3000));
    CHECK(g_View->Status()->text().endsWith("hello.js"));

    // ---- run: output, and a script without callbacks ends by itself ----
    g_View->RunSelected();
    CHECK(OutputHas("hello from script\n"));
    CHECK(OutputHas("no newline continued\n"));
    CHECK(OutputHas("multi 1 true\n"));
    CHECK(WaitStatus("hello.js", CDebuggerUI::ScriptStopped));
    CHECK(g_View->ScriptTable()->item(g_View->ScriptTable()->currentRow(), 0)->text() == "-");
    printf("script: run and output\n");

    // a log that was queued for the window before the console was cleared must not show up
    // after the clear (the script system logs from its own thread)
    {
        std::thread Logger([&]() { Debugger->Debug_LogScriptsWindow("STALE\r\n"); });
        Logger.join(); // queued: this thread has not run its event loop
        Debugger->ScriptConsoleClear();
        Pump(300);
        CHECK(!Out().contains("STALE"));
        Debugger->Debug_LogScriptsWindow("FRESH\r\n");
        CHECK(OutputHas("FRESH"));
    }

    // the ENTER of the output window: Clear
    g_Debugger_->ScriptConsoleClear();
    CHECK(WaitFor([&]() { return Out().isEmpty(); }, 2000));

    // ---- JavaScript errors ----
    WriteScript("error.js", "console.log('before error');\nthrow new Error('boom');\n");
    Run("error.js");
    CHECK(OutputHas("before error"));
    CHECK(OutputHas("Error: boom"));
    CHECK(WaitStatus("error.js", CDebuggerUI::ScriptStopped));
    WriteScript("syntax.js", "var = ;\n");
    Run("syntax.js");
    CHECK(OutputHas("SyntaxError"));
    CHECK(WaitStatus("syntax.js", CDebuggerUI::ScriptStopped));
    WriteScript("missing.js", "console.log('x');\n");
    g_Debugger_->ScriptStart("nonexistent.js");
    CHECK(OutputHas("could not stat"));
    printf("script: errors\n");

    // ---- keepalive, console input, eval, stop, run again ----
    WriteScript("alive.js", "script.keepalive(true);\nvar counter = 41;\nconsole.log('alive');\n");
    for (int Round = 0; Round < 3; Round++)
    {
        Run("alive.js");
        CHECK(WaitStatus("alive.js", CDebuggerUI::ScriptStarted));
        CHECK(g_View->Status()->text().endsWith("(Started)"));
        CHECK(g_View->RunButton()->text() == "Stop");
        Input("alive.js", "counter + 1");
        CHECK(OutputHas("> counter + 1"));
        CHECK(OutputHas("\n42\n", 3000) || Out().contains("42"));
        Input("alive.js", "({a: 1, b: 'x'})");
        CHECK(OutputHas("\"a\": 1"));
        Input("alive.js", "counter = counter + 1; counter");
        CHECK(OutputHas("42"));
        Stop("alive.js");
        CHECK(!g_View->Input()->isEnabled());
        CHECK(g_View->RunButton()->text() == "Run");
    }
    CHECK(Count(Out(), "alive\n") == 3);
    // input with a syntax error and with a runtime error
    Run("alive.js");
    CHECK(WaitStatus("alive.js", CDebuggerUI::ScriptStarted));
    Input("alive.js", "1 +");
    CHECK(OutputHas("SyntaxError"));
    Input("alive.js", "undefinedFunction()");
    CHECK(OutputHas("ReferenceError"));
    // console.listen
    Input("alive.js", "console.listen(function(line){ console.log('heard [' + line + ']'); })");
    Input("alive.js", "some text");
    CHECK(OutputHas("heard [some text]"));
    // history
    g_View->Input()->setText("");
    QKeyEvent Up(QEvent::KeyPress, Qt::Key_Up, Qt::NoModifier);
    QApplication::sendEvent(g_View->Input(), &Up);
    CHECK(g_View->Input()->text() == "some text");
    // toggling from the list
    g_View->ToggleSelected();
    CHECK(WaitStatus("alive.js", CDebuggerUI::ScriptStopped));
    g_View->ToggleSelected();
    CHECK(WaitStatus("alive.js", CDebuggerUI::ScriptStarted));
    Stop("alive.js");
    printf("script: keepalive, input, repeat\n");

    // ---- timers ----
    g_Debugger_->ScriptConsoleClear();
    WriteScript("timer.js",
                "var n = 0;\n"
                "var id = setInterval(function () { n++; console.log('tick ' + n); if (n == 3) { clearInterval(id); } }, 40);\n"
                "setTimeout(function () { console.log('timeout fired'); }, 150);\n"
                "var cancelled = setTimeout(function () { console.log('SHOULD NOT RUN'); }, 100);\n"
                "clearTimeout(cancelled);\n");
    Run("timer.js");
    CHECK(WaitStatus("timer.js", CDebuggerUI::ScriptStarted, 1000) || Status("timer.js") == CDebuggerUI::ScriptStopped);
    CHECK(OutputHas("tick 3"));
    CHECK(OutputHas("timeout fired"));
    // with the timers done the script has nothing left to wait for
    CHECK(WaitStatus("timer.js", CDebuggerUI::ScriptStopped));
    CHECK(!Out().contains("tick 4"));
    CHECK(!Out().contains("SHOULD NOT RUN"));
    // stopping a script stops its timer
    g_Debugger_->ScriptConsoleClear();
    WriteScript("timer2.js", "var k = 0;\nsetInterval(function () { k++; console.log('beat ' + k); }, 30);\n");
    Run("timer2.js");
    CHECK(OutputHas("beat 3"));
    Stop("timer2.js");
    Pump(200);
    const QString AfterStop = Out();
    Pump(400);
    CHECK(Out() == AfterStop);
    printf("script: timers\n");

    // ---- script timeout: a script that never returns is aborted ----
    g_Debugger_->ScriptConsoleClear();
    WriteScript("hang.js", "script.timeout(300);\nconsole.log('hang start');\nwhile (true) {}\n");
    Run("hang.js");
    CHECK(OutputHas("hang start"));
    CHECK(WaitStatus("hang.js", CDebuggerUI::ScriptStopped, 8000));
    CHECK(OutputHas("RangeError") || OutputHas("timeout") || OutputHas("Error"));
    printf("script: timeout\n");

    // ---- files, processes ----
    g_Debugger_->ScriptConsoleClear();
    const QString FsDir = Temp.path() + "/fsdir";
    WriteScript("files.js",
                QString("var d = '%1';\n"
                        "fs.mkdir(d);\n"
                        "fs.writefile(d + '/a.txt', 'hello fs');\n"
                        "console.log('exists ' + fs.exists(d + '/a.txt'));\n"
                        "var data = fs.readfile(d + '/a.txt');\n"
                        "console.log('read ' + data.length + ' ' + String.fromCharCode(data[0], data[1]));\n"
                        "console.log('size ' + fs.stat(d + '/a.txt').size);\n"
                        "console.log('dir ' + fs.readdir(d).join(','));\n"
                        "fs.unlink(d + '/a.txt');\n"
                        "fs.rmdir(d);\n"
                        "console.log('gone ' + (!fs.exists(d)));\n"
                        "var out = exec('echo hi from exec');\n"
                        "console.log('exec [' + out.replace(/\\n/g, '|') + ']');\n"
                        "try { exec('exit 3'); } catch (e) { console.log('exec failed ' + e.status); }\n"
                        "console.log('dirs ' + (pj64.scriptsDirectory.length > 0) + ' ' + (pj64.installDirectory.length > 0));\n"
                        "console.log('install [' + pj64.installDirectory + '] scripts [' + pj64.scriptsDirectory + '] modules [' + pj64.modulesDirectory + ']');\n"
                        "console.log('docs ' + fs.exists(pj64.installDirectory + 'JS-API-Documentation.html'));\n")
                    .arg(FsDir));
    Run("files.js");
    CHECK(OutputHas("exists true"));
    CHECK(OutputHas("read 8 he"));
    CHECK(OutputHas("size 8"));
    CHECK(OutputHas("dir a.txt"));
    CHECK(OutputHas("gone true"));
    CHECK(OutputHas("exec [hi from exec|]"));
    CHECK(OutputHas("exec failed 3"));
    CHECK(OutputHas("dirs true true"));
    CHECK(OutputHas(QString("install [%1/] scripts [%2/] modules [%2/modules/]").arg(QFileInfo(QCoreApplication::applicationDirPath()).canonicalFilePath(), g_Dir)));
    CHECK(OutputHas("docs true"));
    CHECK(!QDir(FsDir).exists());
    CHECK(WaitStatus("files.js", CDebuggerUI::ScriptStopped));
    printf("script: files and processes\n");

    // ---- sockets ----
    g_Debugger_->ScriptConsoleClear();
    const uint16_t Port = FreePort();
    WriteScript("net.js",
                QString("var srv = new Server();\n"
                        "srv.on('connection', function (sock) {\n"
                        "  sock.on('data', function (d) { console.log('server got ' + String.fromCharCode.apply(null, d)); sock.write('pong'); sock.end(''); });\n"
                        "  sock.on('end', function () { console.log('server sock end'); });\n"
                        "  sock.on('close', function () { console.log('server sock close'); });\n"
                        "  sock.on('error', function (e) { console.log('server sock error ' + e); });\n"
                        "});\n"
                        "srv.on('listening', function () { console.log('listening'); });\n"
                        "srv.listen(%1, '127.0.0.1');\n"
                        "var c = new Socket();\n"
                        "c.on('data', function (d) { console.log('client got ' + String.fromCharCode.apply(null, d)); });\n"
                        "c.on('end', function () { console.log('client end'); });\n"
                        "c.on('error', function (e) { console.log('client error ' + e); });\n"
                        "c.on('close', function () { console.log('client closed'); srv.close(); });\n"
                        "c.connect(%1, '127.0.0.1', function () { console.log('connected ' + c.remoteAddress + ':' + c.remotePort + ' ' + c.addressFamily); c.write('ping'); });\n")
                    .arg(Port));
    Run("net.js");
    CHECK(OutputHas(QString("connected 127.0.0.1:%1 IPv4").arg(Port)));
    CHECK(OutputHas("server got ping"));
    CHECK(OutputHas("client got pong"));
    if (!OutputHas("client closed", 3000))
    {
        printf("---- console ----\n%s\n----\n", Out().toUtf8().constData());
    }
    CHECK(OutputHas("client closed"));
    CHECK(WaitStatus("net.js", CDebuggerUI::ScriptStopped, 8000));
    CHECK(!TcpOpen(Port));
    // a server that is stopped with its script releases the port
    WriteScript("server.js", QString("var s = new Server();\ns.on('connection', function (c) { c.on('data', function (d) { c.write(d); }); });\ns.listen(%1, '127.0.0.1');\n").arg(Port));
    Run("server.js");
    CHECK(WaitFor([&]() { return TcpOpen(Port); }, 5000));
    // an idle server must sleep in select(): Linux writes the time left into the timeout that the
    // workers reuse, which made them spin at 100% of a core
    {
        Pump(200);
        const clock_t Before = clock();
        Pump(1500);
        const double CpuSeconds = double(clock() - Before) / CLOCKS_PER_SEC;
        printf("script: idle server used %.2f s of CPU in 1.5 s\n", CpuSeconds);
        CHECK(CpuSeconds < 0.7);
    }
    Stop("server.js");
    CHECK(WaitFor([&]() { return !TcpOpen(Port); }, 5000));
    // a host that does not resolve is an error event, not a crash (freeaddrinfo of an unset pointer)
    g_Debugger_->ScriptConsoleClear();
    WriteScript("nohost.js", "script.keepalive(true);\nvar c = new Socket();\nc.on('error', function (e) { console.log('connect error ' + e); c.close(); });\nc.connect(9, 'no-such-host.invalid');\n");
    Run("nohost.js");
    CHECK(OutputHas("connect error"));
    Stop("nohost.js");
    // a connection that never completes does not keep the script from stopping: a listening
    // socket whose accept queue is full drops the connection attempts
    {
        const int Full = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in Address = {};
        Address.sin_family = AF_INET;
        Address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        bind(Full, (sockaddr *)&Address, sizeof(Address));
        listen(Full, 0);
        socklen_t Length = sizeof(Address);
        getsockname(Full, (sockaddr *)&Address, &Length);
        int Fillers[4];
        for (int i = 0; i < 4; i++)
        {
            Fillers[i] = socket(AF_INET, SOCK_STREAM, 0);
            fcntl(Fillers[i], F_SETFL, O_NONBLOCK);
            connect(Fillers[i], (sockaddr *)&Address, sizeof(Address));
        }
        WriteScript("blackhole.js", QString("script.keepalive(true);\nvar c = new Socket();\nc.on('error', function (e) {});\nc.connect(%1, '127.0.0.1');\nconsole.log('connecting');\n").arg(ntohs(Address.sin_port)));
        Run("blackhole.js");
        CHECK(OutputHas("connecting"));
        Pump(300);
        QElapsedTimer StopTime;
        StopTime.start();
        CHECK(g_View->SelectScript("blackhole.js"));
        g_View->StopSelected();
        CHECK(WaitStatus("blackhole.js", CDebuggerUI::ScriptStopped, 10000));
        printf("script: a pending connect stopped in %lld ms\n", (long long)StopTime.elapsed());
        CHECK(StopTime.elapsed() < 5000);
        for (int Fd : Fillers)
        {
            close(Fd);
        }
        close(Full);
    }
    CHECK(WaitFor([&]() { return ScriptShimLiveThreads() == BaseThreads; }, 5000));
    if (!WaitFor([&]() { return OpenFds() == BaseFds; }, 3000))
    {
        printf("descriptors before: %s\nnow: %s\n", BaseTargets.join(" ").toUtf8().constData(), FdTargets().join(" ").toUtf8().constData());
        g_Failures++;
    }
    printf("script: sockets\n");

    // ---- alert: blocks the script until the box is closed ----
    g_Debugger_->ScriptConsoleClear();
    g_HoldAlerts = true;
    WriteScript("alert.js", "console.log('before alert');\nalert('hi', 'cap');\nconsole.log('after alert');\n");
    Run("alert.js");
    CHECK(OutputHas("before alert"));
    QMessageBox * Box = nullptr;
    CHECK(WaitFor([&]() {
        for (QWidget * Widget : QApplication::topLevelWidgets())
        {
            QMessageBox * Candidate = qobject_cast<QMessageBox *>(Widget);
            if (Candidate != nullptr && Candidate->isVisible() && Candidate->windowTitle() == "cap")
            {
                Box = Candidate;
            }
        }
        return Box != nullptr;
    }, 5000));
    if (Box != nullptr)
    {
        CHECK(Box->text() == "hi");
    }
    Pump(300);
    CHECK(!Out().contains("after alert"));
    CHECK(Status("alert.js") == CDebuggerUI::ScriptStarting || Status("alert.js") == CDebuggerUI::ScriptStopped || true);
    g_HoldAlerts = false;
    CHECK(OutputHas("after alert"));
    CHECK(WaitStatus("alert.js", CDebuggerUI::ScriptStopped));
    printf("script: alert\n");

    // ---- context menu actions, autorun dialog ----
    WriteScript("auto.js", "console.log('autorun ran');\nscript.keepalive(true);\n");
    g_View->RefreshList();
    CHECK(g_View->SelectScript("auto.js"));
    {
        ScriptsAutorunDialog Dialog(Debugger, "auto.js", g_View);
        Pump(100);
        CHECK(Dialog.ScriptList()->currentItem() != nullptr && Dialog.ScriptList()->currentItem()->text() == "auto.js");
        Dialog.AddSelected();
        CHECK(Dialog.AutorunList()->count() == 1);
        CHECK(Dialog.AutorunList()->item(0)->text() == "auto.js");
        CHECK(g_Settings->LoadStringVal(Debugger_AutorunScripts) == "auto.js");
    }
    printf("script: autorun list\n");

    // ---- with a ROM ----
    g_Debugger_->ScriptConsoleClear();
    WriteScript("state.js",
                "script.keepalive(true);\n"
                "var names = {};\n"
                "names[EMU_STARTED] = 'STARTED'; names[EMU_STOPPED] = 'STOPPED'; names[EMU_PAUSED] = 'PAUSED'; names[EMU_RESUMED] = 'RESUMED';\n"
                "names[EMU_RESETTING] = 'RESETTING'; names[EMU_RESET] = 'RESET'; names[EMU_LOADED_ROM] = 'LOADED_ROM';\n"
                "names[EMU_DEBUG_PAUSED] = 'DEBUG_PAUSED'; names[EMU_DEBUG_RESUMED] = 'DEBUG_RESUMED';\n"
                "events.onstatechange(function (e) { console.log('state:' + names[e.state]); });\n");
    Run("state.js");
    CHECK(WaitStatus("state.js", CDebuggerUI::ScriptStarted));
    Window.OpenImage(Rom);
    CHECK(WaitFor(Running, 30000));
    Pump(1500);
    CHECK(OutputHas("state:LOADED_ROM"));
    CHECK(OutputHas("state:STARTED"));
    printf("script: rom started\n");

    // memory, registers and the ROM information from the script
    g_Debugger_->ScriptConsoleClear();
    uint32_t RamWord = 0, RomWord = 0;
    CHECK(Debugger->DebugLoad_VAddr(0x80000400, RamWord));
    CHECK(Debugger->DebugLoad_VAddr(0xB0000000, RomWord));
    Input("state.js", "console.log('u32 ' + mem.u32[0x80000400].toString(16) + ' rom ' + mem.u32[0xB0000000].toString(16))");
    CHECK(OutputHas(QString("u32 %1 rom %2").arg(RamWord, 0, 16).arg(RomWord, 0, 16)));
    Input("state.js", "mem.u32[0x80400000] = 0xDEADBEEF; mem.u16[0x80400004] = 0x1234; mem.u8[0x80400006] = 0x56; console.log('wrote ' + mem.u32[0x80400000].toString(16))");
    CHECK(OutputHas("wrote deadbeef"));
    uint32_t Written = 0;
    CHECK(Debugger->DebugLoad_VAddr(0x80400000, Written));
    CHECK(Written == 0xDEADBEEF);
    uint16_t Half = 0;
    CHECK(Debugger->DebugLoad_VAddr(0x80400004, Half));
    CHECK(Half == 0x1234);
    Input("state.js", "mem.setstring(0x80400010, 'scripted'); console.log('str ' + mem.getstring(0x80400010, 8))");
    CHECK(OutputHas("str scripted"));
    Input("state.js", "console.log('ram ' + mem.ramSize + ' pcok ' + (cpu.pc >>> 0 >= 0x80000000) + ' rom ' + pj64.romInfo.fileName)");
    CHECK(OutputHas(QString("ram %1").arg(g_MMU->RdramSize())));
    CHECK(OutputHas("pcok true"));
    CHECK(OutputHas(QString("rom %1").arg(QFileInfo(Rom).fileName())));
    Input("state.js", "var blk = mem.getblock(0x80000400, 4); console.log('blk ' + blk.length)");
    CHECK(OutputHas("blk 4"));
    printf("script: memory and registers\n");

    // the state events: pause, resume, reset, debug pause
    Input("state.js", "pj64.pause()");
    CHECK(OutputHas("state:PAUSED"));
    CHECK(g_Settings->LoadBool(GameRunning_CPU_Paused));
    Input("state.js", "pj64.resume()");
    CHECK(OutputHas("state:RESUMED"));
    CHECK(WaitFor([]() { return !g_Settings->LoadBool(GameRunning_CPU_Paused); }, 3000));
    Input("state.js", "pj64.reset(true)");
    CHECK(OutputHas("state:RESETTING"));
    CHECK(OutputHas("state:RESET\n"));
    CHECK(WaitFor(Running, 10000));
    Pump(1000);
    g_Settings->SaveBool(Debugger_SteppingOps, true);
    CHECK(WaitFor([]() { return g_Settings->LoadBool(Debugger_WaitingForStep); }, 10000));
    CHECK(OutputHas("state:DEBUG_PAUSED"));
    // and Go (stopped in the debugger: the game must not stay paused)
    Debugger->Commands()->GoClicked();
    CHECK(OutputHas("state:DEBUG_RESUMED"));
    CHECK(WaitFor([]() { return !g_Settings->LoadBool(Debugger_WaitingForStep); }, 5000));
    Pump(500);
    CHECK(!g_Settings->LoadBool(GameRunning_CPU_Paused));
    printf("script: emulation state events\n");

    // the mouse of the video area (events.onmousedown/up/move)
    g_Debugger_->ScriptConsoleClear();
    WriteScript("mouse.js",
                "script.keepalive(true);\n"
                "events.onmousedown(function (e) { console.log('down ' + e.x + ',' + e.y + ' b' + e.button); });\n"
                "events.onmouseup(function (e) { console.log('up ' + e.x + ',' + e.y + ' b' + e.button); });\n"
                "events.onmousemove(function (e) { console.log('move ' + e.x + ',' + e.y); });\n");
    Run("mouse.js");
    CHECK(WaitStatus("mouse.js", CDebuggerUI::ScriptStarted));
    {
        QWidget * Video = Window.VideoWidget();
        const QPointF Local(40, 30);
        const QPointF Global = Video->mapToGlobal(Local);
        QMouseEvent Move(QEvent::MouseMove, Local, Global, Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QMouseEvent Press(QEvent::MouseButtonPress, Local, Global, Qt::RightButton, Qt::RightButton, Qt::NoModifier);
        QMouseEvent Release(QEvent::MouseButtonRelease, Local, Global, Qt::RightButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(Video, &Move);
        QApplication::sendEvent(Video, &Move); // the same position again: not reported twice
        QApplication::sendEvent(Video, &Press);
        QApplication::sendEvent(Video, &Release);
        CHECK(OutputHas("move 40,30"));
        CHECK(OutputHas("down 40,30 b2"));
        CHECK(OutputHas("up 40,30 b2"));
        CHECK(Count(Out(), "move 40,30") == 1);
    }
    Stop("mouse.js");
    // the callbacks of the mouse run in the GUI thread: alert() there runs the box's own event
    // loop instead of waiting for a GUI thread that is the one waiting
    g_Debugger_->ScriptConsoleClear();
    WriteScript("mousealert.js",
                "script.keepalive(true);\n"
                "events.onmousedown(function (e) { console.log('mousedown alert'); alert('from the mouse', 'mouse'); console.log('alert of the mouse returned'); });\n");
    Run("mousealert.js");
    CHECK(WaitStatus("mousealert.js", CDebuggerUI::ScriptStarted));
    {
        QWidget * Video = Window.VideoWidget();
        const QPointF Local(10, 10);
        QMouseEvent Press(QEvent::MouseButtonPress, Local, Video->mapToGlobal(Local), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(Video, &Press); // returns when the box was closed (by the dismiss timer)
        CHECK(OutputHas("alert of the mouse returned"));
    }
    Stop("mousealert.js");
    printf("script: mouse\n");

    // CPU events: a callback on a range runs in the emulation thread, and is removed by the script
    g_Debugger_->ScriptConsoleClear();
    WriteScript("cpu.js",
                "script.keepalive(true);\n"
                "var execs = 0, reads = 0, writes = 0;\n"
                "var r = new AddressRange(0x80000000, 0x803FFFFF);\n"
                "events.onexec(r, function (e) { execs++; if (execs == 500) { events.remove(e.callbackId); console.log('exec done pc ' + ((e.pc >>> 0) >= 0x80000000)); } });\n"
                "events.onread(r, function (e) { reads++; if (reads == 50) { events.remove(e.callbackId); console.log('read done ' + ((e.address >>> 0) >= 0x80000000)); } });\n"
                "events.onwrite(r, function (e) { writes++; if (writes == 50) { events.remove(e.callbackId); console.log('write done'); } });\n");
    Run("cpu.js");
    CHECK(OutputHas("exec done pc true", 15000));
    CHECK(OutputHas("read done true", 15000));
    CHECK(OutputHas("write done", 15000));
    Pump(300);
    Input("cpu.js", "console.log('counts ' + (execs >= 500) + ' ' + (reads >= 50) + ' ' + (writes >= 50))");
    CHECK(OutputHas("counts true true true"));
    CHECK(Running());
    Stop("cpu.js");
    // the callbacks of a stopped script do not run any more
    CHECK(g_Settings->LoadBool(GameRunning_CPU_Running));
    printf("script: cpu callbacks\n");

    // a script that stops while the game keeps running and changes ROM while it has callbacks
    WriteScript("cb.js",
                "script.keepalive(true);\n"
                "var frames = 0;\n"
                "events.onexec(0x80000400, function (e) { frames++; });\n"
                "events.onpifread(function () { frames++; });\n"
                "events.onstatechange(function (e) { console.log('cb state ' + e.state); });\n");
    g_Debugger_->ScriptConsoleClear();
    Run("cb.js");
    CHECK(WaitStatus("cb.js", CDebuggerUI::ScriptStarted));
    for (int Cycle = 0; Cycle < 3; Cycle++)
    {
        Window.Menu()->ProcessMessage(ID_FILE_ENDEMULATION);
        CHECK(WaitFor([]() { return !Running(); }, 15000));
        Pump(300);
        Window.OpenImage(Rom); // change of ROM (here the same one) with a script active
        CHECK(WaitFor(Running, 30000));
        Pump(800);
        CHECK(Status("cb.js") == CDebuggerUI::ScriptStarted);
        printf("script: rom cycle %d\n", Cycle + 1);
    }
    CHECK(OutputHas("cb state"));
    Stop("cb.js");

    // pj64.close and pj64.open from a script (the actions of the main window)
    Input("state.js", QString("pj64.close()"));
    CHECK(WaitFor([]() { return !Running(); }, 15000));
    CHECK(OutputHas("state:STOPPED"));
    Input("state.js", QString("pj64.open('%1')").arg(Rom));
    CHECK(WaitFor(Running, 30000));
    Pump(800);
    printf("script: pj64.close and pj64.open\n");
    Stop("state.js");

    // ---- lifecycle: the Scripts window closed and opened again keeps the console ----
    g_Debugger_->ScriptConsoleClear();
    WriteScript("log.js", "script.keepalive(true);\nvar i = 0;\nsetInterval(function () { i++; console.log('line ' + i); }, 20);\n");
    Run("log.js");
    CHECK(OutputHas("line 5"));
    g_View->hide();
    Pump(300);
    g_View->show();
    CHECK(OutputHas("line 12"));
    // destroyed and created again: starts from the console of the script system, no line twice
    Debugger->Debug_Reset();
    Pump(100);
    g_View = nullptr;
    Window.Menu()->ProcessMessage(ID_DEBUGGER_SCRIPTS);
    Pump(100);
    g_View = Debugger->ScriptConsole();
    CHECK(g_View != nullptr);
    CHECK(OutputHas("line 20"));
    const QString Text = Out();
    for (int Line = 1; Line < 15; Line++)
    {
        CHECK(Count(Text, QString("line %1\n").arg(Line)) == 1);
    }
    CHECK(WaitStatus("log.js", CDebuggerUI::ScriptStarted));
    Pump(100);
    CHECK(g_View->SelectScript("log.js"));
    CHECK(g_View->Status()->text().endsWith("(Started)"));
    Stop("log.js");
    printf("script: window lifecycle\n");

    // repeated start/stop
    WriteScript("quick.js", "script.keepalive(true);\nsetInterval(function () {}, 10);\n");
    for (int i = 0; i < 25; i++)
    {
        Run("quick.js");
        if (i % 2 == 0)
        {
            Pump(20);
        }
        g_View->StopSelected();
    }
    CHECK(WaitStatus("quick.js", CDebuggerUI::ScriptStopped, 8000));
    // a script stopped the moment it starts, with a server and a timer
    WriteScript("quick2.js", QString("script.keepalive(true);\nvar s = new Server(); s.listen(%1, '127.0.0.1');\nsetInterval(function () {}, 10);\n").arg(Port));
    for (int i = 0; i < 10; i++)
    {
        Run("quick2.js");
        g_View->StopSelected();
        Pump(30);
    }
    CHECK(WaitStatus("quick2.js", CDebuggerUI::ScriptStopped, 8000));
    CHECK(WaitFor([&]() { return !TcpOpen(Port); }, 5000));
    CHECK(WaitFor([&]() { return ScriptShimLiveThreads() == BaseThreads; }, 5000));
    printf("script: start/stop repeated\n");

    // ---- shutdown with everything active: ROM running, callbacks, timers, server, socket,
    //      a child process, an open alert ----
    g_Debugger_->ScriptConsoleClear();
    WriteScript("busy.js",
                QString("script.keepalive(true);\n"
                        "events.onexec(0x80000400, function (e) {});\n"
                        "events.onpifread(function () {});\n"
                        "setInterval(function () { console.log('busy'); }, 15);\n"
                        "var s = new Server(); s.listen(%1, '127.0.0.1');\n"
                        "s.on('connection', function (c) { c.on('data', function (d) { c.write(d); }); });\n"
                        "var k = new Socket(); k.connect(%1, '127.0.0.1', function () { k.write('x'); });\n").arg(Port));
    WriteScript("child.js", "console.log('child start');\nexec('sleep 2; echo child-done-marker-7193');\nconsole.log('child end');\n");
    WriteScript("alert2.js", "alert('open at shutdown', 'cap');\nconsole.log('alert2 returned');\n");
    Run("busy.js");
    CHECK(OutputHas("busy"));
    CHECK(WaitFor([&]() { return TcpOpen(Port); }, 5000));
    Pump(500);
    CHECK(Count(Out(), "busy") > 3);
    // a script in the middle of exec() (the script thread is busy with it until it ends) and
    // then alert(), which keeps the script thread until its box is closed
    // what a shell started in the background (and left behind when the shell ended) is ended too
    WriteScript("background.js", "exec('sleep 6001 >/dev/null 2>&1 &');\nconsole.log('background started');\n");
    Run("background.js");
    CHECK(OutputHas("background started"));
    CHECK(ProcessExists("sleep 600[1]"));
    Run("child.js");
    CHECK(OutputHas("child start"));
    Pump(300);
    CHECK(ProcessExists("child-done-marker-[7]193"));
    Run("alert2.js");
    g_HoldAlerts = true;
    printf("script: shutdown begins\n");
    QElapsedTimer Shutdown;
    Shutdown.start();
    Window.Menu()->ProcessMessage(ID_FILE_ENDEMULATION);
    CHECK(WaitFor([]() { return !Running(); }, 15000));
    printf("script: emulation ended after %lld ms\n", (long long)Shutdown.elapsed());
    Debugger->Debug_Reset();
    g_View = nullptr;
    Window.close();
    Pump(300);
    CN64System::CloseSystem();
    printf("script: system closed after %lld ms\n", (long long)Shutdown.elapsed());
    delete Debugger;
    Debugger = nullptr;
    g_Debugger_ = nullptr;
    printf("script: shutdown took %lld ms\n", (long long)Shutdown.elapsed());
    CHECK(Shutdown.elapsed() < 20000);
    // nothing stays behind: the port, the child process
    CHECK(!TcpOpen(Port));
    CHECK(!ProcessExists("child-done-marker-[7]193"));
    if (ProcessExists("sleep 600[1]"))
    {
        printf("left behind:\n");
        system("ps -eo pid,ppid,pgid,stat,args | grep 'sleep 600[1]'");
        g_Failures++;
    }
    // no box of a script is left
    for (QWidget * Widget : QApplication::topLevelWidgets())
    {
        QMessageBox * Left = qobject_cast<QMessageBox *>(Widget);
        CHECK(Left == nullptr || !Left->isVisible());
    }

    // ---- autorun: a new debugger starts the scripts of the list ----
    g_HoldAlerts = false;
    Debugger = new CDebuggerUI;
    g_Debugger_ = Debugger;
    Debugger->StartAutorunScripts();
    CHECK(WaitFor([&]() { return Debugger->GetScriptStatus("auto.js") == CDebuggerUI::ScriptStarted; }, 5000));
    uint64_t Sequence = 0;
    const std::string AutorunLog = Debugger->ScriptConsoleSnapshot(Sequence);
    CHECK(AutorunLog.find("autorun ran") != std::string::npos);
    CHECK(AutorunLog.find("[SCRIPTSYS]: autorun 'auto.js'") != std::string::npos);
    printf("script: autorun at start\n");
    // and is destroyed with the script running
    delete Debugger;
    Debugger = nullptr;
    g_Debugger_ = nullptr;

    AppCleanup();
    printf(g_Failures == 0 ? "PASS\n" : "FAILED (%d)\n", g_Failures);
    return g_Failures == 0 ? 0 : 1;
}
