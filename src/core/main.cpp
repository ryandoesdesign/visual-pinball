// license:GPLv3+

// Implementation of WinMain (Windows with UI) or main (Standalone)

#include "core/stdafx.h"

#include "vpversion.h"

#include "plugins/VPXPlugin.h"
#include "core/VPXPluginAPIImpl.h"

#include "core/AppCommands.h"
#include "core/player.h"
#include "core/extern.h"

#include "ui/win/resource.h"
#include <initguid.h>

#define SET_CRT_DEBUG_FIELD(a) _CrtSetDbgFlag((a) | _CrtSetDbgFlag(_CRTDBG_REPORT_FLAG))

#include <locale>
#include <codecvt>

#include <SDL3_ttf/SDL_ttf.h>
#include <filesystem>


#ifndef OVERRIDE
   #define OVERRIDE
#endif


namespace {
   // File-static so they survive across vpx_init() / vpx_run_command() / vpx_shutdown().
   // Swift's @main owns the program lifecycle and calls these at distinct moments;
   // VPApp + the parsed command must live across all three calls.
   std::unique_ptr<VPApp> s_app;
   std::unique_ptr<CommandLineProcessor> s_cmdLine;
}


extern "C" int vpx_init()
{
   Logger::Init();

   try
   {
      s_app = std::make_unique<VPApp>();
      s_cmdLine = std::make_unique<CommandLineProcessor>();
      s_cmdLine->ProcessCommandLine();
      s_app->InitInstance();

      SDL_SetHint(SDL_HINT_WINDOW_ALLOW_TOPMOST, "0");
      if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
      {
         PLOGE << "SDL_InitSubSystem(SDL_INIT_VIDEO) failed: " << SDL_GetError();
         return 1;
      }
      if (const char* const drv = SDL_GetCurrentVideoDriver())
      {
         PLOGI << "SDL video driver: " << drv;
      }
   }
   catch (const CException &e)
   {
      MessageBox(nullptr, e.GetText(), AtoT(e.what()), MB_ICONERROR);
      return -1;
   }

   return 0;
}


extern "C" int vpx_run_command()
{
   try
   {
      if (s_cmdLine && s_cmdLine->m_command)
         s_cmdLine->m_command->Execute();
   }
   catch (const CException &e)
   {
      MessageBox(nullptr, e.GetText(), AtoT(e.what()), MB_ICONERROR);
      return -1;
   }

   return 0;
}


extern "C" void vpx_shutdown()
{
   s_cmdLine.reset();
   s_app.reset();

   SDL_QuitSubSystem(SDL_INIT_VIDEO);

   TTF_Quit();

   SDL_Quit();

   PLOGI << "Closing VPX...\n\n";
}


// One iteration of the running game's main loop. Called from Swift's
// CADisplayLink on each vsync via the bridging header. No-ops if
// there's no Player yet (CADisplayLink may fire briefly before
// Player is constructed) or if the game is wrapping up.
extern "C" void vpx_tick()
{
   if (g_pplayer != nullptr)
      g_pplayer->Tick();
}


extern "C" int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE /*hPrevInstance*/, LPTSTR /*lpCmdLine*/, int /*nShowCmd*/)
{
   int rc = vpx_init();
   if (rc == 0)
      rc = vpx_run_command();
   vpx_shutdown();
   return rc;
}
