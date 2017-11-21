#include "Poco/NamedEvent.h"
#include "Poco/NumberParser.h"
#include "Poco/Event.h"
#include "Poco/Thread.h"

#include <iostream>
#include <iomanip>
#include <ctime>
#include <memory>
#include <Poco/NumberFormatter.h>
#include <Poco/Process.h>
#include <fstream>

#include <windows.h>
#include <tchar.h>

using namespace std;
using namespace Poco;

static NamedEvent s_terminate("terminate_wtf");
static Event s_terminated;

static int WAIT_SECONDS = 20;

static HWND windowHandle = nullptr;

static std::shared_ptr<void> messageThread;
static const string WINDOW_CLASS_NAME =  // NOLINT(runtime/string)
    Poco::ProcessImpl::terminationEventName(Poco::Process::id());

#define RET_FALSE_ON_QES_WITH_REASON_ES_NEVER_CALLED
#define RET_TRUE_ON_QES_AND_BLOCK_ON_ES_WITH_REASON

// #define KILL_HANDLER_THREAD

class tee
{
public:
    tee(ostream& out1, ostream& out2) : out1(out1), out2(out2) {}

    template<class T>
    tee& operator<<(const T& any)
    {
        out1 << any;
        out2 << any;
        out1.flush();
        out2.flush();
        return *this;
    }

private:
    ostream& out1;
    ostream& out2;
};

string LogFileName()
{
#ifdef KILL_HANDLER_THREAD
    string evName("C:\\work\\win_shut_console_with_window_kill_handler_");
#else
    string evName("C:\\work\\win_shut_console_with_window_");
#endif

#ifdef RET_FALSE_ON_QES_WITH_REASON_ES_NEVER_CALLED
    evName.append("false_qes");
#elif defined RET_TRUE_ON_QES_WITH_REASON_AND_BLOCK_ON_ES
    evName.append("true_qes_");
#endif

    NumberFormatter::append(evName, WAIT_SECONDS);
    evName.append("seconds_");
    NumberFormatter::appendHex(evName, Process::id());
    evName.append("_");
    NumberFormatter::append(evName, time(nullptr));
    return evName.append(".log");
}

static string LOG_FILE_NAME = LogFileName();

tee& LogIt()
{
    static fstream LOG_FILE(LOG_FILE_NAME, ios_base::out);
    static tee theOut(LOG_FILE, cout);

    auto now = time(nullptr);
    theOut << put_time(localtime(&now), "%F %T%z ");  // ISO 8601 format.
    return theOut;
}

void KillThisThread()
{
    // Detach Console:
    // FreeConsole();
    // Prevent closing:
    ExitThread(0);
}

string EventString(DWORD eventType)
{
    switch (eventType)
    {
        case CTRL_C_EVENT:
            return "CTRL_C_EVENT";
        case CTRL_BREAK_EVENT:
            return "CTRL_BREAK_EVENT";
        case CTRL_CLOSE_EVENT:
            return "CTRL_CLOSE_EVENT";
        case CTRL_LOGOFF_EVENT:
            return "CTRL_LOGOFF_EVENT";
        case CTRL_SHUTDOWN_EVENT:
            return "CTRL_SHUTDOWN_EVENT";
        default:
            return NumberFormatter::format(eventType);
    }
}

BOOL OnConsoleCtrlEvent(DWORD eventType)
{
    LogIt() << "Handler: Received control event " << EventString(eventType) << "\n";
    switch (eventType)
    {
        case CTRL_C_EVENT:
        case CTRL_BREAK_EVENT:
        case CTRL_CLOSE_EVENT:
        case CTRL_LOGOFF_EVENT:
        case CTRL_SHUTDOWN_EVENT:
            LogIt() << "Handler: Terminating on console control event " << EventString(eventType) << "\n";
            LogIt() << "Handler: Blocking for " << WAIT_SECONDS << " seconds.\n";
            s_terminate.set();
#ifdef KILL_HANDLER_THREAD
            KillThisThread();
#endif
            s_terminated.wait();
            LogIt() << "Handler: Terminated.\n";
            return TRUE;
        default:
            return FALSE;  // Pass on to other handlers
    }
}

const char* endSessionMessage[] = {
    "N/A",                                            // 0
    "The system is shutting down or restarting",      // 1
    "The application is being shut down forcefully",  // 2
    "The user is logging off"                         // 3
};

size_t ToMessageIndex(LPARAM logoffOption)
{
    // clang-format off
    return (logoffOption & ENDSESSION_CLOSEAPP) == ENDSESSION_CLOSEAPP ? 1 :
        (logoffOption & ENDSESSION_CRITICAL) == ENDSESSION_CRITICAL ? 2 :
        (logoffOption & ENDSESSION_LOGOFF) == ENDSESSION_LOGOFF ? 3 :
        0;
    // clang-format on
}

HWND CreateLucidProcessWindow(WNDPROC wndProcMethod)
{
    LPCSTR className = WINDOW_CLASS_NAME.c_str();

    HWND hwnd;
    WNDCLASS wc = {0};
    wc.lpfnWndProc = (WNDPROC)wndProcMethod;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.hIcon = nullptr;
    wc.lpszClassName = className;
    RegisterClass(&wc);

    hwnd = CreateWindowEx(
        0,                              // dwExStyle
        className,                      // lpClassName
        className,                      // lpWindowName
        WS_OVERLAPPEDWINDOW,            // dwStyle
        CW_USEDEFAULT,                  // x
        CW_USEDEFAULT,                  // y
        CW_USEDEFAULT,                  // nWidth
        CW_USEDEFAULT,                  // nHeight
        static_cast<HWND>(nullptr),     // hWndParent
        static_cast<HMENU>(nullptr),    // hMenu
        GetModuleHandle(nullptr),       // hInstance
        static_cast<LPVOID>(nullptr));  // lpParam

    if (!hwnd)
        LogIt() << "Could not create " << className << " window for message handling: " << GetLastError() << "\n";

    return hwnd;
}

DWORD WINAPI RunMessageQueueThread(LPVOID lpParam)
{
    MSG msg;
    WNDPROC wndProc = (WNDPROC)lpParam;
    HWND hwnd = CreateLucidProcessWindow(wndProc);
    windowHandle = hwnd;
    BOOL bRet;
    while ((bRet = GetMessage(&msg, hwnd, 0, 0)) != 0)
    {
        LogIt() << "GetMessage returned: " << bRet << "\n";
        if (bRet == -1)
        {
            LogIt() << "Exiting message loop because GetMessage returned error: " << GetLastError() << "\n";
            return -1;
        }
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    LogIt() << "Exiting message loop thread.\n";
    return 0;
}

string MsgString(UINT msg)
{
    switch (msg)
    {
        case WM_QUERYENDSESSION:
            return "WM_QUERYENDSESSION";
        case WM_ENDSESSION:
            return "WM_ENDSESSION";
        case WM_CLOSE:
            return "WM_CLOSE";
        case WM_DESTROY:
            return "WM_DESTROY";
        default:
            return NumberFormatter::format(msg);
    }
}

void DoTerminate(bool returnNow, UINT msg, LPARAM logOffOption)
{
    LogIt() << "Handler: Terminating on message " << MsgString(msg) << " with logoff '" << logOffOption << "'\n";

    if (!returnNow)
        LogIt() << "Handler: Blocking for " << WAIT_SECONDS << " seconds.\n";

    s_terminate.set();

    if (!returnNow)
        s_terminated.wait();

    LogIt() << "DoTerminate return.\n";
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM endSessionOption, LPARAM logoffOption)
{
    switch (msg)
    {
#ifdef RET_FALSE_ON_QES_WITH_REASON_ES_NEVER_CALLED
        case WM_QUERYENDSESSION:  // On purpose, we always terminate even on query in order to have time for cleanup.
        {
            LogIt() << "Return false on QueryEndSession & block in background. Reason: "
                    << endSessionMessage[ToMessageIndex(logoffOption)] << "\n";
            ShutdownBlockReasonCreate(hwnd, L"Saving temporary state.");
            DoTerminate(false, msg, logoffOption);
            Poco::Thread::sleep(1);
            return FALSE;
        }
        case WM_ENDSESSION:
        {
            LogIt() << "MUST HAVE NEVER BEEN CALLED ON EndSession. Reason: "
                    << endSessionMessage[ToMessageIndex(logoffOption)] << "\n";
            DoTerminate(false, msg, logoffOption);
            return TRUE;
        }
#elif defined RET_TRUE_ON_QES_WITH_REASON_AND_BLOCK_ON_ES
        case WM_QUERYENDSESSION:  // On purpose, we always terminate even on query in order to have time for cleanup.
        {
            LogIt() << "Return true on QueryEndSession & start terminating in background. Reason: "
                    << endSessionMessage[ToMessageIndex(logoffOption)] << "\n";
            ShutdownBlockReasonCreate(hwnd, L"Saving temporary state.");
            DoTerminate(true, msg, logoffOption);
            return TRUE;
        }
        case WM_ENDSESSION:
        {
            LogIt() << "Termination on EndSession. Block. Reason: " << endSessionMessage[ToMessageIndex(logoffOption)]
                    << "\n";
            DoTerminate(false, msg, logoffOption);
            return TRUE;
        }
#endif
        case WM_CLOSE:
        {
            LogIt() << "Termination on Close.\n";
            DoTerminate(false, msg, logoffOption);
            break;
        }

        // The contract-compliant behavior for WM_DESTROY is handled by DefWindowProc bellow
        case WM_DESTROY:
            LogIt() << "Called destroy.\n";
            ShutdownBlockReasonDestroy(hwnd);
        default:
            break;
    }
    /*  Observed unhandled messages so far:
    OnOpen
    36  - WM_GETMINMAXINFO
    129 - WM_NCCREATE
    131 - WM_NCCALCSIZE
    1   - WM_CREATE
    799 - WM_DWMNCRENDERINGCHANGED
    49361 - 0xC0D1 ?
    OnClose
    800 - WM_DWMCOLORIZATIONCOLORCHANGED
    537 - WM_DEVICECHANGE - when rebooting EC2 instance
    59  - 0X003B - at logOff/restart/shutdown in windows
    */
    LogIt() << "Received unhandled window message: " << MsgString(msg) << "\n";
    return DefWindowProc(hwnd, msg, endSessionOption, logoffOption);
}

void RegisterOnTerminateListener()
{
    if (!SetProcessShutdownParameters(0x4ff, 0))  //  greedy highest documented System reserved FirstShutdown
    {
        if (!SetProcessShutdownParameters(0x3ff, 0))  // highest notification range for applications
        {
            LogIt() << "Could not register process as high priority listener for shutdown events.\n";
        }
    }

    DWORD tid;
    HANDLE hInvisiblethread = CreateThread(nullptr, 0, RunMessageQueueThread, &WindowProc, 0, &tid);
    messageThread = shared_ptr<void>(hInvisiblethread, [](HANDLE hThread) { CloseHandle(hThread); });
}

int main(int argc, const char* argv[])
{
    LogIt() << "LOGGING TO FILE: " << LOG_FILE_NAME << "\n";

    if (SetProcessShutdownParameters(0x4ff, 0))
    {
        LogIt() << "Set max shutdown at system level\n";
        if (!SetProcessShutdownParameters(0x3ff, 0))
        {
            LogIt() << "Set max shutdown at app level\n";
        }
    }

    if (argc > 1)
    {
        WAIT_SECONDS = NumberParser().parse(argv[1]);
    }
    LogIt() << "Main: This process will wait for " << WAIT_SECONDS << " seconds on termination notification.\n";

    SetConsoleCtrlHandler(reinterpret_cast<PHANDLER_ROUTINE>(&OnConsoleCtrlEvent), TRUE);
    RegisterOnTerminateListener();

    s_terminate.wait();

    LogIt() << "Main: Notification received on main thread. Blocking to simulate cleanup.\n";

    for (int i = WAIT_SECONDS; i > 0; i--)
    {
        Thread::sleep(1000);
        LogIt() << "Main: Remaining " << i << " seconds.\n";
    }

    s_terminated.set();

    PostMessage(windowHandle, WM_CLOSE, 0, 0);
    WaitForSingleObject(messageThread.get(), INFINITE);
    return 0;
}
