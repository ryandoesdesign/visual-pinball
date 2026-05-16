// license:GPLv3+

#include "core/stdafx.h"

#include "core/VPApp.h"

#include "vpversion.h"

#include "plugins/VPXPlugin.h"
#include "core/VPXPluginAPIImpl.h"

#ifdef CRASH_HANDLER
#include "utils/StackTrace.h"
#include "utils/CrashHandler.h"
#include "utils/BlackBox.h"
#endif

#include "ui/win/resource.h"
#include <initguid.h>

#define SET_CRT_DEBUG_FIELD(a) _CrtSetDbgFlag((a) | _CrtSetDbgFlag(_CRTDBG_REPORT_FLAG))


#include <locale>
#include <codecvt>

#ifdef __ANDROID__
#include <SDL3/SDL_main.h>
#endif

#include <SDL3_ttf/SDL_ttf.h>
#include <filesystem>
#include <libwinevbs/libwinevbs.h>

#include "parts/ball.h"
#include "parts/timer.h"
#include "parts/flipper.h"
#include "parts/plunger.h"
#include "parts/textbox.h"
#include "parts/surface.h"
#include "parts/dispreel.h"
#include "parts/lightseq.h"
#include "parts/bumper.h"
#include "parts/trigger.h"
#include "parts/light.h"
#include "parts/kicker.h"
#include "parts/decal.h"
#include "parts/primitive.h"
#include "parts/hittarget.h"
#include "parts/gate.h"
#include "parts/spinner.h"
#include "parts/ramp.h"
#include "parts/flasher.h"
#include "parts/rubber.h"
#include "parts/PartGroup.h"


#ifndef OVERRIDE
   #define OVERRIDE
#endif

#ifdef CRASH_HANDLER
extern "C" int __cdecl _purecall()
{
   ShowError("Pure Virtual Function Call");

   CONTEXT Context = {};
#ifdef _WIN64
   RtlCaptureContext(&Context);
#else
   Context.ContextFlags = CONTEXT_CONTROL;

   __asm
   {
   Label:
      mov[Context.Ebp], ebp;
      mov[Context.Esp], esp;
      mov eax, [Label];
      mov[Context.Eip], eax;
   }
#endif

   char callStack[2048] = {};
   rde::StackTrace::GetCallStack(&Context, true, callStack, sizeof(callStack) - 1);

   ShowError(callStack);

   return 0;
}
#endif



VPApp::VPApp()
   : m_logicalNumberOfProcessors(SDL_GetNumLogicalCPUCores())
{
   g_app = this;

   SetThreadName("Main"s);

   #ifdef CRASH_HANDLER
      rde::CrashHandler::Init();
   #endif

   IsOnWine(); // init static variable in there

   #ifdef _MSC_VER
      HINSTANCE instance = GetInstanceHandle();

      // disable auto-rotate on tablets
      #if (_WIN32_WINNT <= 0x0601)
         SetDisplayAutoRotationPreferences = (pSDARP)GetProcAddress(GetModuleHandle(TEXT("user32.dll")), "SetDisplayAutoRotationPreferences");
         if (SetDisplayAutoRotationPreferences)
            SetDisplayAutoRotationPreferences(static_cast<ORIENTATION_PREFERENCE>(ORIENTATION_PREFERENCE_LANDSCAPE | ORIENTATION_PREFERENCE_LANDSCAPE_FLIPPED));
      #else
         SetDisplayAutoRotationPreferences(static_cast<ORIENTATION_PREFERENCE>(ORIENTATION_PREFERENCE_LANDSCAPE | ORIENTATION_PREFERENCE_LANDSCAPE_FLIPPED));
      #endif

      #ifdef _ATL_FREE_THREADED
         const HRESULT hRes = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
      #else
         const HRESULT hRes = CoInitialize(nullptr);
      #endif
      _ASSERTE(SUCCEEDED(hRes));
      
      // load and register VP type library for COM integration
      m_module.Init(ObjectMap, instance, &LIBID_VPinballLib);
      {
         ITypeLib *ptl = nullptr;
         const wstring wFileName = GetModulePath<wstring>(instance);
         if (SUCCEEDED(LoadTypeLib(wFileName.c_str(), &ptl)))
         {
            // first try to register system-wide (if running as admin)
            HRESULT hr = RegisterTypeLib(ptl, wFileName.c_str(), nullptr);
            if (!SUCCEEDED(hr))
            {
               // if failed, register only for current user
               hr = RegisterTypeLibForUser(ptl, (OLECHAR*)wFileName.c_str(), nullptr);
               if (!SUCCEEDED(hr))
               {
                  ShowError("Could not register type library. Try running Visual Pinball as administrator.");
               }
            }
            ptl->Release();
         }
         else
         {
            ShowError("Could not load type library.");
         }
      }

      #ifdef _ATL_FREE_THREADED
         const HRESULT hRes2 = m_module.RegisterClassObjects(CLSCTX_LOCAL_SERVER, REGCLS_MULTIPLEUSE | REGCLS_SUSPENDED);
         _ASSERTE(SUCCEEDED(hRes));
         hRes2 = CoResumeClassObjects();
      #else
         const HRESULT hRes2 = m_module.RegisterClassObjects(CLSCTX_LOCAL_SERVER, REGCLS_MULTIPLEUSE);
      #endif
      _ASSERTE(SUCCEEDED(hRes2));

      INITCOMMONCONTROLSEX iccex;
      iccex.dwSize = sizeof(INITCOMMONCONTROLSEX);
      iccex.dwICC = ICC_COOL_CLASSES;
      InitCommonControlsEx(&iccex);
   #endif
   
   EditableRegistry::RegisterEditable<Ball>();
   EditableRegistry::RegisterEditable<Bumper>();
   EditableRegistry::RegisterEditable<Decal>();
   EditableRegistry::RegisterEditable<DispReel>();
   EditableRegistry::RegisterEditable<Flasher>();
   EditableRegistry::RegisterEditable<Flipper>();
   EditableRegistry::RegisterEditable<Gate>();
   EditableRegistry::RegisterEditable<Kicker>();
   EditableRegistry::RegisterEditable<Light>();
   EditableRegistry::RegisterEditable<LightSeq>();
   EditableRegistry::RegisterEditable<Plunger>();
   EditableRegistry::RegisterEditable<Primitive>();
   EditableRegistry::RegisterEditable<Ramp>();
   EditableRegistry::RegisterEditable<Rubber>();
   EditableRegistry::RegisterEditable<Spinner>();
   EditableRegistry::RegisterEditable<Surface>();
   EditableRegistry::RegisterEditable<Textbox>();
   EditableRegistry::RegisterEditable<Timer>();
   EditableRegistry::RegisterEditable<Trigger>();
   EditableRegistry::RegisterEditable<HitTarget>();
   EditableRegistry::RegisterEditable<PartGroup>();
}

VPApp::~VPApp()
{
      libwinevbs_shutdown();
   g_pvp = nullptr;
   g_app = nullptr;

   m_settings.Save();

   #ifdef _CRTDBG_MAP_ALLOC
      _CrtDumpMemoryLeaks();
   #endif
}

void VPApp::LimitMultiThreading()
{
   const int procCount = SDL_GetNumLogicalCPUCores();
   m_logicalNumberOfProcessors = max(min(procCount, 2), procCount / 4); // only use 1/4th the threads, but at least 2 (if there are 2)
}

int VPApp::GetLogicalNumberOfProcessors() const
{
   if (m_logicalNumberOfProcessors < 1)
   {
      PLOGE << "Invalid number of processor " << m_logicalNumberOfProcessors << ". Fallback to single processor.";
      return 1;
   }

   return m_logicalNumberOfProcessors;
}

void VPApp::InitInstance()
{
   std::filesystem::path iniFileName = m_commandLineCustomSettingsFileName;
   // Define settings location and load them
   if (iniFileName.empty()) // nothing passed via command line
   {
      std::filesystem::path defaultPath = m_fileLocator.GetAppPath(FileLocator::AppSubFolder::Preferences) / "VPinballX.ini"sv;
      std::filesystem::path appPath = m_fileLocator.GetAppPath(FileLocator::AppSubFolder::Root) / "VPinballX.ini"sv;
      if (FileExists(defaultPath))
         iniFileName = defaultPath;
      else if (FileExists(appPath))
         iniFileName = appPath;
      else
         iniFileName = defaultPath;
   }
   m_settings.SetIniPath(iniFileName);
   m_settings.Load(true);

   // The file layout must be defined before loading the settings file, so we apply the following rules:
   // - if we have a settings location commandline override, we load it and use the setting in it (to locate other files than the ini)
   // - if not, but we have a settings file in the default preference location, we use it and update the setting accordingly ('Table' layout mode)
   // - if not, but we have a settings file along the app executable, we use it and update the setting accordingly ('App' layout mode)
   // - if we don't have anything, then we use the default ('Table' layout mode)

   Logger::Init();

   libwinevbs_callbacks_t callbacks = {};
   callbacks.log = [](libwinevbs_log_level_t level, const char* format, va_list args) {
      va_list args_copy;
      va_copy(args_copy, args);
      int size = vsnprintf(nullptr, 0, format, args_copy);
      va_end(args_copy);
      if (size > 0) {
         char* const buffer = new char[size + 1];
         vsnprintf(buffer, size + 1, format, args);
         switch (level) {
         case LIBWINEVBS_LOG_DEBUG: PLOGD << buffer; break;
         case LIBWINEVBS_LOG_WARN:  PLOGW << buffer; break;
         case LIBWINEVBS_LOG_ERROR: PLOGE << buffer; break;
         default: PLOGI << buffer; break;
         }
         delete[] buffer;
      }
   };
   libwinevbs_init(&callbacks);

   Logger::SetupLogger(m_settings.GetEditor_EnableLog());
   PLOGI << "Starting VPX - " << VP_VERSION_STRING_FULL_LITERAL;
   PLOGI << "Settings file was loaded from " << m_settings.GetIniPath();
   PLOGI << "Number of logical CPU cores: " << GetLogicalNumberOfProcessors();
   PLOGI << "Application path: " << m_fileLocator.GetAppPath(FileLocator::AppSubFolder::Root);
   PLOGI << "Preference path: " << m_fileLocator.GetAppPath(FileLocator::AppSubFolder::Preferences);

   Settings::SetRecentDir_ImportDir_Default((m_fileLocator.GetAppPath(FileLocator::AppSubFolder::Tables) / ""sv).string());
   Settings::SetRecentDir_LoadDir_Default((m_fileLocator.GetAppPath(FileLocator::AppSubFolder::Tables) / ""sv).string());
   Settings::SetRecentDir_FontDir_Default((m_fileLocator.GetAppPath(FileLocator::AppSubFolder::Tables) / ""sv).string());
   Settings::SetRecentDir_PhysicsDir_Default((m_fileLocator.GetAppPath(FileLocator::AppSubFolder::Tables) / ""sv).string());
   Settings::SetRecentDir_ImageDir_Default((m_fileLocator.GetAppPath(FileLocator::AppSubFolder::Tables) / ""sv).string());
   Settings::SetRecentDir_MaterialDir_Default((m_fileLocator.GetAppPath(FileLocator::AppSubFolder::Tables) / ""sv).string());
   Settings::SetRecentDir_SoundDir_Default((m_fileLocator.GetAppPath(FileLocator::AppSubFolder::Tables) / ""sv).string());
   Settings::SetRecentDir_POVDir_Default((m_fileLocator.GetAppPath(FileLocator::AppSubFolder::Tables) / ""sv).string());

   m_securitylevel = g_app->m_settings.GetPlayer_SecurityLevel();
   if (m_securitylevel < eSecurityNone || m_securitylevel > eSecurityNoControls)
      m_securitylevel = eSecurityNoControls;

   m_settings.SetVersion_VPinball(string(VP_VERSION_STRING_DIGITS), false);
   m_settings.Save();
}

