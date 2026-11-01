// dllmain.cpp : Defines the entry point for the DLL application.
#include <Windows.h>
#include <iostream>
#include "game.h"
#include "hooks.h"
#include "vr.h"
#include "sdk.h"

#ifdef L4D2VR_DIAGNOSTICS
#include <fstream>
#include <cstdint>

// Writes everything to the log file (b) and, if there is one, also to a second stream buffer
// (a, the console).
class TeeBuf : public std::streambuf
{
public:
    TeeBuf(std::streambuf *a, std::streambuf *b) : m_A(a), m_B(b) {}

    void SetFirst(std::streambuf *a) { m_A = a; }

protected:
    int overflow(int c) override
    {
        if (c == EOF)
            return !EOF;
        const int r1 = m_A ? m_A->sputc((char)c) : c;
        const int r2 = m_B->sputc((char)c);
        return (r1 == EOF || r2 == EOF) ? EOF : c;
    }

    int sync() override
    {
        return ((!m_A || m_A->pubsync() == 0) && m_B->pubsync() == 0) ? 0 : -1;
    }

private:
    std::streambuf *m_A;
    std::streambuf *m_B;
};

static const char *ModuleOf(const void *address, uintptr_t &offset)
{
    static char name[MAX_PATH];
    HMODULE module = nullptr;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (LPCSTR)address, &module) ||
        !GetModuleFileNameA(module, name, MAX_PATH))
        return nullptr;
    offset = (uintptr_t)address - (uintptr_t)module;
    const char *slash = strrchr(name, '\\');
    return slash ? slash + 1 : name;
}

// The engine installs its own unhandled exception filter, so use a vectored handler to see the
// crash first. Exceptions that the code handles itself get logged too; the last one is the crash.
static LONG CALLBACK CrashLogger(EXCEPTION_POINTERS *ep)
{
    const DWORD code = ep->ExceptionRecord->ExceptionCode;
    if (code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_ILLEGAL_INSTRUCTION &&
        code != EXCEPTION_PRIV_INSTRUCTION && code != EXCEPTION_INT_DIVIDE_BY_ZERO &&
        code != EXCEPTION_STACK_OVERFLOW && code != EXCEPTION_IN_PAGE_ERROR)
        return EXCEPTION_CONTINUE_SEARCH;

    const void *address = ep->ExceptionRecord->ExceptionAddress;
    uintptr_t offset = 0;
    const char *module = ModuleOf(address, offset);
    std::cout << std::hex << "[CRASH] code=0x" << code << " at 0x" << (uintptr_t)address << " "
              << (module ? module : "?") << "+0x" << offset;
    if (code == EXCEPTION_ACCESS_VIOLATION && ep->ExceptionRecord->NumberParameters >= 2)
        std::cout << " (" << (ep->ExceptionRecord->ExceptionInformation[0] == 0 ? "read" : "write")
                  << " of 0x" << ep->ExceptionRecord->ExceptionInformation[1] << ")";
    const CONTEXT *c = ep->ContextRecord;
    std::cout << "\n[CRASH] eax=" << c->Eax << " ebx=" << c->Ebx << " ecx=" << c->Ecx << " edx=" << c->Edx
              << " esi=" << c->Esi << " edi=" << c->Edi << " ebp=" << c->Ebp << " esp=" << c->Esp << "\n";

    // Poor man's backtrace: stack words that point into executable module memory.
    const uintptr_t *stack = (const uintptr_t *)c->Esp;
    int found = 0;
    for (int i = 0; i < 512 && found < 24; ++i)
    {
        uintptr_t word;
        MEMORY_BASIC_INFORMATION mbi;
        if (!ReadProcessMemory(GetCurrentProcess(), stack + i, &word, sizeof(word), nullptr) ||
            !VirtualQuery((LPCVOID)word, &mbi, sizeof(mbi)) ||
            !(mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)))
            continue;
        uintptr_t off = 0;
        if (const char *m = ModuleOf((const void *)word, off))
        {
            std::cout << "[CRASH]   esp+0x" << i * 4 << ": " << m << "+0x" << off << "\n";
            ++found;
        }
    }
    std::cout << std::dec << std::flush;
    return EXCEPTION_CONTINUE_SEARCH;
}
#endif

DWORD WINAPI InitL4D2VR(HMODULE hModule)
{
// Release if buggy, so we'll be releasing the debug binary
#ifdef L4D2VR_DIAGNOSTICS
    // Everything written to std::cout goes to l4d2vr_debug.log, flushed after every write, so it
    // survives a crash. A console window is only opened if the environment variable
    // L4D2VR_CONSOLE is set: the window takes the focus from the game, and with Steam Link that
    // made SteamVR open its dashboard right when the game menu appeared.
    static std::ofstream logFile("l4d2vr_debug.log", std::ios::trunc);
    static TeeBuf teeBuf(nullptr, logFile.rdbuf());
    if (GetEnvironmentVariableA("L4D2VR_CONSOLE", nullptr, 0) > 0)
    {
        AllocConsole();
        FILE *fp;
        freopen_s(&fp, "CONOUT$", "w", stdout);
        teeBuf.SetFirst(std::cout.rdbuf());
    }
    std::cout.rdbuf(&teeBuf);
    std::cout << std::unitbuf;
    AddVectoredExceptionHandler(1, CrashLogger);
#endif

    // Make sure -insecure is used
    LPWSTR *szArglist;
    int nArgs;
    szArglist = CommandLineToArgvW(GetCommandLineW(), &nArgs);
    bool insecureEnabled = false;
    for (int i = 0; i < nArgs; ++i)
    {
        if (wcscmp(szArglist[i], L"-insecure") == 0)
            insecureEnabled = true;
    }
    LocalFree(szArglist);

    if (!insecureEnabled)
        ExitProcess(0);

    g_Game = new Game();

    return 0;
}



BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
                     )
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
            CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)InitL4D2VR, hModule, 0, NULL);
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}


