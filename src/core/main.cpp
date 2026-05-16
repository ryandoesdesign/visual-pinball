// license:GPLv3+

// Implementation of WinMain (Windows with UI) or main (Standalone)

#include "core/stdafx.h"

#include "vpversion.h"

#include "plugins/VPXPlugin.h"
#include "core/VPXPluginAPIImpl.h"

#include "core/AppCommands.h"

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





extern "C" int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE /*hPrevInstance*/, LPTSTR /*lpCmdLine*/, int /*nShowCmd*/)
{

   Logger::Init();


   int retval = 0;
   try
   {

      VPApp theApp;
      CommandLineProcessor cmdLine;
      cmdLine.ProcessCommandLine();
      theApp.InitInstance();

      SDL_SetHint(SDL_HINT_WINDOW_ALLOW_TOPMOST, "0");
      if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
      {
         PLOGE << "SDL_InitSubSystem(SDL_INIT_VIDEO) failed: " << SDL_GetError();
         // FIXME this is not correct as we may be running something else than the player (extract vbs, ...)
         exit(1);
      }
      if (const char* const drv = SDL_GetCurrentVideoDriver()) {
         PLOGI << "SDL video driver: " << drv;
      }

      // Run the application
      if (cmdLine.m_command)
         cmdLine.m_command->Execute();
   }

   // catch all CException types
   catch (const CException &e)
   {
      // Display the exception and quit
      MessageBox(nullptr, e.GetText(), AtoT(e.what()), MB_ICONERROR);

      retval = -1;
   }

   SDL_QuitSubSystem(SDL_INIT_VIDEO);

      TTF_Quit();


   SDL_Quit();

   PLOGI << "Closing VPX...\n\n";
   return retval;
}
