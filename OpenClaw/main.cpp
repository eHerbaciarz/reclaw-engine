//#define SDL_MAIN_HANDLED
//#pragma comment(linker, "/SUBSYSTEM:windows /ENTRY:mainCRTStartup")

#include <SDL2/SDL.h>
#include "Engine/GameApp/MainLoop.h"
#include "ClawGameApp.h"


ClawGameApp testApp;

int main(int argc, char* argv[]);

// Android: SDL's Java activity loads libmain.so and calls SDL_main, which
// <SDL2/SDL_main.h> (included through SDL.h) makes of the main() below.

#if defined(__PS3__) || defined(__PSL1GHT__)
#include <sys/thread.h>
extern "C" void ReclawBootMark(const char* step);

// PS3: the main thread's stack is small (64 KB) and setting a bigger one
// with SYS_PROCESS_PARAM froze the console, so the game gets a thread of its
// own with 1 MB.
namespace
{
    int g_Argc; char** g_Argv; int g_Result;
    void GameThread(void*)
    {
        ReclawBootMark("2b game thread");
        g_Result = RunGameEngine(g_Argc, g_Argv);
        sysThreadExit(0);
    }
}
#endif

int main(int argc, char* argv[])
{
#if defined(__PS3__) || defined(__PSL1GHT__)
    ReclawBootMark("2 main");
    g_Argc = argc; g_Argv = argv;
    sys_ppu_thread_t thread;
    char name[] = "reclaw";
    if (sysThreadCreate(&thread, GameThread, nullptr, 1000, 0x100000, THREAD_JOINABLE, name) == 0)
    {
        u64 ret = 0;
        sysThreadJoin(thread, &ret);
        return g_Result;
    }
    ReclawBootMark("2b thread could not be created, running on the main thread");
#endif
#ifdef _CRTDBG_MAP_ALLOC
    _CrtSetReportMode( _CRT_WARN, _CRTDBG_MODE_FILE );
    _CrtSetReportFile( _CRT_WARN, _CRTDBG_FILE_STDOUT );
    _CrtSetReportMode( _CRT_ERROR, _CRTDBG_MODE_FILE );
    _CrtSetReportFile( _CRT_ERROR, _CRTDBG_FILE_STDOUT );
//    _CrtSetReportMode( _CRT_ASSERT, _CRTDBG_MODE_FILE );
//    _CrtSetReportFile( _CRT_ASSERT, _CRTDBG_FILE_STDOUT );
    _CrtSetDbgFlag ( _CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF );
#endif
    return RunGameEngine(argc, argv);
}
