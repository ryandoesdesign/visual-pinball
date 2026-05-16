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

#if defined(__STANDALONE__) && ((defined(__linux__) && !defined(__ANDROID__)) || defined(__MINGW32__))
#include <csignal>

void OnSignalHandler(int signum)
{
   PLOGI.printf("Exiting from signal: %d", signum);
   exit(-9999);
}
#endif

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

#if defined(__STANDALONE__) && ((defined(__linux__) && !defined(__ANDROID__)) || defined(__MINGW32__))
extern int g_argc;
extern const char **g_argv;
int main(int argc, const char** argv) {
#ifdef __MINGW32__
   signal(SIGINT, OnSignalHandler);
#else
   struct sigaction sigIntHandler;
   sigIntHandler.sa_handler = OnSignalHandler;
   sigemptyset(&sigIntHandler.sa_mask);
   sigIntHandler.sa_flags = 0;
   sigaction(SIGINT, &sigIntHandler, nullptr);
#endif

   g_argc = argc;
   g_argv = argv;
   return WinMain(NULL, NULL, NULL, 0);
}
#endif
