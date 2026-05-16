#include "core/stdafx.h"

#include "ScriptInterpreter.h"

#include "ScriptGlobalTable.h"
#include "core/vpversion.h"
#include "ui/win/WinEditor.h"


// The GUID used to identify the coclass of the VB Script engine {B54F3741-5B07-11cf-A4B0-00AA004A55E8}
DEFINE_GUID(CLSID_VBScript, 0xb54f3741, 0x5b07, 0x11cf, 0xa4, 0xb0, 0x0, 0xaa, 0x0, 0x4a, 0x55, 0xe8);
//DEFINE_GUID(IID_IActiveScriptParse32, 0xbb1a2ae2, 0xa4f9, 0x11cf, 0x8f, 0x20, 0x0, 0x80, 0x5f, 0x2c, 0xd0, 0x64);
//DEFINE_GUID(IID_IActiveScriptParse64,0xc7ef7658,0xe1ee,0x480e,0x97,0xea,0xd5,0x2c,0xb4,0xd7,0x6d,0x17);
//DEFINE_GUID(IID_IActiveScriptDebug, 0x51973C10, 0xCB0C, 0x11d0, 0xB5, 0xC9, 0x00, 0xA0, 0x24, 0x4A, 0x0E, 0x7A);

ScriptInterpreter::ScriptInterpreter()
{
   CComObject<DebuggerModule>::CreateInstance(&m_pdm);
   m_pdm->AddRef();

   // Note: For standalone, CoCreateInstance resolves to libwinevbs's implementation via the
   // filtered import library. Alternatively, VBScriptFactory_CreateInstance() from winevbs64.dll
   // could be called directly here to avoid any dependency on import library symbol ordering.
   const HRESULT vbScriptResult = CoCreateInstance(
      CLSID_VBScript, 0, CLSCTX_INPROC_SERVER | CLSCTX_INPROC_HANDLER | CLSCTX_LOCAL_SERVER, IID_IActiveScriptParse, (LPVOID *)&m_pScriptParse); //!! CLSCTX_INPROC_SERVER good enough?!
   if (vbScriptResult != S_OK)
      return;


   m_pScriptParse->QueryInterface(IID_IActiveScript, (LPVOID *)&m_pScript);
   m_pScriptParse->QueryInterface(IID_IActiveScriptDebug, (LPVOID *)&m_pScriptDebug);
   m_pScriptParse->InitNew();

}

ScriptInterpreter::~ScriptInterpreter()
{
   if (m_pScript)
   {
      // Cleanly wait for the script to end to allow Exit event, triggered just before closing, to be processed
      SCRIPTSTATE state;
      m_pScript->GetScriptState(&state);
      if (state != SCRIPTSTATE_CLOSED && state != SCRIPTSTATE_UNINITIALIZED)
      {
         m_pScript->Close();
         const uint32_t startWaitTick = msec();
         while ((msec() - startWaitTick < 5000) && (state != SCRIPTSTATE_CLOSED))
         {
            Sleep(16);
            m_pScript->GetScriptState(&state);
         }
         if (state != SCRIPTSTATE_CLOSED)
         {
            PLOGE << "Script did not terminate within 5s after request. Forcing close of interpreter #" << m_pScript;
            EXCEPINFO eiInterrupt = {};
            eiInterrupt.bstrDescription = MakeWideBSTR(LocalStringW(IDS_HANG).m_buffer);
            //eiInterrupt.scode = E_NOTIMPL;
            eiInterrupt.wCode = 2345;
            m_pScript->InterruptScriptThread(SCRIPTTHREADID_BASE /*SCRIPTTHREADID_ALL*/, &eiInterrupt, /*SCRIPTINTERRUPT_DEBUG*/ SCRIPTINTERRUPT_RAISEEXCEPTION);
            SysFreeString(eiInterrupt.bstrDescription);
         }
         else
         {
            PLOGI << "Script interpreter state is now closed. Releasing interpreter #" << m_pScript;
         }
      }
      SAFE_RELEASE_NO_RCC(m_pScript);
      SAFE_RELEASE_NO_RCC(m_pScriptParse);
      SAFE_RELEASE(m_pScriptDebug);
   }
   m_pdm->Release();
}

void ScriptInterpreter::Start(PinTable* table)
{
   if (m_pScript)
   {
      m_pScript->SetScriptSite(this);
      m_pScript->SetScriptState(SCRIPTSTATE_INITIALIZED);
      m_pScript->AddTypeLib(LIBID_VPinballLib, 1, 0, 0);
   }

   AddItem(table, false);
   AddItem((ScriptGlobalTable*) table->m_psgt, true);
   AddItem(m_pdm, false);
   for (int i = 0; i < table->m_vcollection.size(); i++)
      AddItem(&table->m_vcollection[i], false);
   for (auto editable : table->GetParts())
      if (editable->GetIScriptable())
         AddItem(editable->GetIScriptable(), false);
}

void ScriptInterpreter::Stop(PinTable *table, bool interruptDirectly)
{
   if (m_pScript == nullptr)
      return;

   SCRIPTSTATE state;
   m_pScript->GetScriptState(&state);
   if (state != SCRIPTSTATE_CLOSED && state != SCRIPTSTATE_UNINITIALIZED)
   {
      m_pScript->Close();
      if (!interruptDirectly)
      {
         const uint32_t startWaitTick = msec();
         while ((msec() - startWaitTick < 5000) && (state != SCRIPTSTATE_CLOSED))
         {
            Sleep(16);
            m_pScript->GetScriptState(&state);
         }
      }
      if (state != SCRIPTSTATE_CLOSED)
      {
         PLOGE << "Script did not terminate within 5s after request. Forcing close of interpreter #" << m_pScript;
         EXCEPINFO eiInterrupt = {};
         eiInterrupt.bstrDescription = MakeWideBSTR(LocalStringW(IDS_HANG).m_buffer);
         //eiInterrupt.scode = E_NOTIMPL;
         eiInterrupt.wCode = 2345;
         m_pScript->InterruptScriptThread(SCRIPTTHREADID_BASE /*SCRIPTTHREADID_ALL*/, &eiInterrupt, /*SCRIPTINTERRUPT_DEBUG*/ SCRIPTINTERRUPT_RAISEEXCEPTION);
         SysFreeString(eiInterrupt.bstrDescription);
      }
   }

   RemoveItem(table);
   RemoveItem((ScriptGlobalTable *)table->m_psgt);
   RemoveItem(m_pdm);
   for (int i = 0; i < table->m_vcollection.size(); i++)
      RemoveItem(&table->m_vcollection[i]);
   for (auto editable : table->GetParts())
      if (editable->GetIScriptable())
         RemoveItem(editable->GetIScriptable());
}

void ScriptInterpreter::AddItem(const wstring& name, IDispatch *dispatch, const bool global)
{
   if (auto it = m_scriptItemMap.find(name); it != m_scriptItemMap.end())
   {
      PLOGE << "Script item with name '" << MakeString(name) << "' already exists. Skipping addition of this item.";
      return;
   }

   dispatch->AddRef();
   {
      auto pcvd = std::make_unique<ScriptItem>();
      pcvd->m_wName = name;
      pcvd->m_pdisp = dispatch;
      pcvd->m_pdisp->QueryInterface(IID_IUnknown, (void **)&pcvd->m_punk);
      pcvd->m_punk->Release();
      pcvd->m_global = global;
      m_scriptItemMap[pcvd->m_wName] = std::move(pcvd);
   }

   int flags = SCRIPTITEM_ISSOURCE | SCRIPTITEM_ISVISIBLE;
   if (global)
      flags |= SCRIPTITEM_GLOBALMEMBERS;
   if (m_pScript != nullptr)
      m_pScript->AddNamedItem(name.c_str(), flags);
}

void ScriptInterpreter::RemoveItem(IScriptable *const piscript)
{
   piscript->GetIDispatch()->Release();
   m_scriptItemMap.erase(piscript->get_Name());
}

void ScriptInterpreter::Evaluate(const string &script, bool isDebugStatement)
{
   if (m_pScriptParse)
   {
      EXCEPINFO exception {};
      m_pScriptParse->ParseScriptText(MakeWString(script).c_str(), isDebugStatement ? L"Debug" : nullptr, nullptr, nullptr, isDebugStatement ? m_debugContextCookie : m_compileContextCookie, 0,
         isDebugStatement ? 0 : SCRIPTTEXT_ISVISIBLE, nullptr, &exception);
   }
   if (m_pScript)
      m_pScript->SetScriptState(SCRIPTSTATE_CONNECTED);
}

void ScriptInterpreter::GetScriptDispatch(IDispatch **ppdisp) const
{
   if (m_pScript)
      m_pScript->GetScriptDispatch(nullptr, ppdisp);
   else
      *ppdisp = nullptr;
}

void ScriptInterpreter::HandleScriptError(IActiveScriptError *pScriptError, IActiveScriptErrorDebug* pScriptDebugError)
{
   m_hasError = true;

   // Get stack trace
   vector<string> stackDump;

   DWORD dwCookie;
   ULONG nLine;
   LONG nChar;
   pScriptError->GetSourcePosition(&dwCookie, &nLine, &nChar);

   BSTR bstr = nullptr;
   pScriptError->GetSourceLineText(&bstr);
   SysFreeString(bstr);

   EXCEPINFO exception = {};
   pScriptError->GetExceptionInfo(&exception);
   const string description = exception.bstrDescription ? MakeString(exception.bstrDescription) : "Description unavailable"s;
   SysFreeString(exception.bstrDescription);
   SysFreeString(exception.bstrSource);
   SysFreeString(exception.bstrHelpFile);

   SCRIPTSTATE state;
   m_pScript->GetScriptState(&state);
   const bool isRuntimeError = (state == SCRIPTSTATE_CONNECTED);
   const bool isDebugConsole = (dwCookie == m_debugContextCookie);

   const ErrorType errorType = isDebugConsole ? ErrorType::DebugConsole : isRuntimeError ? ErrorType::Runtime : ErrorType::Compile;
   
   if (m_errorHandler)
      m_errorHandler(errorType, nLine + 1, nChar, description, stackDump);
}


///////////////////////////////////////////////////////////////////////////////////////////////////
// IActiveScriptSite interface

STDMETHODIMP ScriptInterpreter::GetLCID(LCID *plcid)
{
   //*plcid = 9; // Previous version would return 9 => What codepage is this ?
   *plcid = 1033; // English - United States
   return S_OK;
}

STDMETHODIMP ScriptInterpreter::GetItemInfo(LPCOLESTR pstrName, DWORD dwReturnMask, IUnknown **ppiunkItem, ITypeInfo **ppti)
{
   if (dwReturnMask & SCRIPTINFO_IUNKNOWN)
      *ppiunkItem = nullptr;
   if (dwReturnMask & SCRIPTINFO_ITYPEINFO)
      *ppti = nullptr;

   const auto it = m_scriptItemMap.find(std::wstring { pstrName });
   if (it == m_scriptItemMap.end())
      return E_FAIL;

   if (IUnknown * punk = it->second->m_punk; punk)
   {
      if (dwReturnMask & SCRIPTINFO_IUNKNOWN)
      {
         punk->AddRef();
         *ppiunkItem = punk;
      }

      if (dwReturnMask & SCRIPTINFO_ITYPEINFO)
      {
         IProvideClassInfo *pClassInfo;
         punk->QueryInterface(IID_IProvideClassInfo, (LPVOID *)&pClassInfo);
         if (pClassInfo)
         {
            pClassInfo->GetClassInfo(ppti);
            pClassInfo->Release();
         }
      }
   }

   return S_OK;
}

STDMETHODIMP ScriptInterpreter::GetDocVersionString(BSTR *pbstrVersion)
{
   static const wstring version = MakeWString(VP_VERSION_STRING_POINTS);
   *pbstrVersion = SysAllocStringLen(version.c_str(), static_cast<UINT>(version.length()));
   return S_OK;
}

// Called on compilation errors. Also called on runtime errors in we couldn't create a "process debug manager" (such
// as when running on wine), or if no debug application is available (where a "debug application" is something like
// VS 2010 Isolated Shell).
// See ScriptInterpreter::OnScriptErrorDebug for runtime errors, when a debug application is available
STDMETHODIMP ScriptInterpreter::OnScriptError(IActiveScriptError *pScriptError)
{
   HandleScriptError(pScriptError, nullptr);
   return S_OK;
}

STDMETHODIMP ScriptInterpreter::OnScriptTerminate(const VARIANT *pvr, const EXCEPINFO *pei) { return S_OK; }

STDMETHODIMP ScriptInterpreter::OnStateChange(SCRIPTSTATE ssScriptState) { return S_OK; }

STDMETHODIMP ScriptInterpreter::OnEnterScript() { return S_OK; }

STDMETHODIMP ScriptInterpreter::OnLeaveScript() { return S_OK; }


///////////////////////////////////////////////////////////////////////////////////////////////////
// IActiveScriptSiteWindow interface

STDMETHODIMP ScriptInterpreter::GetWindow(HWND *phwnd)
{
   // We are supposed to return the window to be used as a parent for modal dialog. Why not just nullptr ?
   return S_OK;
}

STDMETHODIMP ScriptInterpreter::EnableModeless(BOOL) { return S_OK; }


///////////////////////////////////////////////////////////////////////////////////////////////////
// IActiveScriptSiteDebug interface

STDMETHODIMP ScriptInterpreter::GetDocumentContextFromPosition(DWORD_PTR dwSourceContext, ULONG uCharacterOffset, ULONG uNumChars, IDebugDocumentContext **ppsc) { return E_NOTIMPL; }

STDMETHODIMP ScriptInterpreter::GetApplication(IDebugApplication **ppda)
{
   return S_OK;
}

STDMETHODIMP ScriptInterpreter::GetRootApplicationNode(IDebugApplicationNode **ppdanRoot)
{
   IDebugApplication *app;
   const HRESULT result = GetApplication(&app);
   if (SUCCEEDED(result))
      return app->GetRootNode(ppdanRoot);
   else
      return result;
}

// Called on runtime errors, if debugging is supported, and a debug application is available.
// See CodeViewer::OnScriptError for compilation errors, and also runtime errors when debugging isn't available.
STDMETHODIMP ScriptInterpreter::OnScriptErrorDebug(IActiveScriptErrorDebug *pScriptError, BOOL *pfEnterDebugger, BOOL *pfCallOnScriptErrorWhenContinuing)
{
   // TODO: Which debuggers even work with VBScript? It might be an idea to offer a "Debug" button (set pfEnterDebugger to
   //       true) if it can pop open some old version of visual studio to debug stuff.
   //
   //       VS 2010 Isolated Shell seems to work, but trying to enter debugging with it complains with an "invalid
   //       license" error. I haven't found anything else to work yet, not even regular VS 2010 (though, it might be
   //       that you need to manually set some registry keys to select the default debugger?)
   //
   //       HKEY_CLASSES_ROOT\CLSID\{834128A2-51F4-11D0-8F20-00805F2CD064}\LocalServer32 seems to be the registry key
   //       to select the default debugger.
   //       (https://stackoverflow.com/questions/2288043/how-do-i-debug-a-stand-alone-vbscript-script#comment36315883_2288064)
   *pfEnterDebugger = FALSE;
   *pfCallOnScriptErrorWhenContinuing = FALSE;

   HandleScriptError(reinterpret_cast<IActiveScriptError*>(pScriptError), pScriptError);

   return S_OK;
}


///////////////////////////////////////////////////////////////////////////////////////////////////
// Internet Security interface


HRESULT STDMETHODCALLTYPE ScriptInterpreter::GetSecurityId(BYTE *pbSecurityId, DWORD *pcbSecurityId, DWORD_PTR dwReserved) { return S_OK; }

HRESULT STDMETHODCALLTYPE ScriptInterpreter::ProcessUrlAction(
   DWORD dwAction, BYTE __RPC_FAR *pPolicy, DWORD cbPolicy, BYTE __RPC_FAR *pContext, DWORD cbContext, DWORD dwFlags, DWORD dwReserved)
{
   *pPolicy = (dwAction == URLACTION_ACTIVEX_RUN && (g_app->m_securitylevel < eSecurityNoControls)) ? URLPOLICY_ALLOW : URLPOLICY_DISALLOW;
   return S_OK;
}

DEFINE_GUID(GUID_CUSTOM_CONFIRMOBJECTSAFETY, 0x10200490, 0xfa38, 0x11d0, 0xac, 0x0e, 0x00, 0xa0, 0xc9, 0x0f, 0xff, 0xc0);

HRESULT STDMETHODCALLTYPE ScriptInterpreter::QueryCustomPolicy(
   REFGUID guidKey, BYTE __RPC_FAR *__RPC_FAR *ppPolicy, DWORD __RPC_FAR *pcbPolicy, BYTE __RPC_FAR *pContext, DWORD cbContext, DWORD dwReserved)
{

   return S_OK;
}

bool ScriptInterpreter::IsControlAlreadyOkayed(const CONFIRMSAFETY *pcs) const
{
   if (g_pplayer)
   {
      for (size_t i = 0; i < g_pplayer->m_controlclsidsafe.size(); ++i)
      {
         const CLSID *const pclsid = g_pplayer->m_controlclsidsafe[i];
         if (*pclsid == pcs->clsid)
            return true;
      }
   }

   return false;
}

void ScriptInterpreter::AddControlToOkayedList(const CONFIRMSAFETY *pcs) const
{
   if (g_pplayer)
   {
      CLSID *const pclsid = new CLSID();
      *pclsid = pcs->clsid;
      g_pplayer->m_controlclsidsafe.push_back(pclsid);
   }
}

bool ScriptInterpreter::IsControlMarkedSafe(const CONFIRMSAFETY *pcs)
{
   bool safe = false;

   return safe;
}

bool ScriptInterpreter::IsUserManuallyOkaysControl(const CONFIRMSAFETY *pcs) const
{
   return false;
}


///////////////////////////////////////////////////////////////////////////////////////////////////
// IServiceProvider interface

HRESULT STDMETHODCALLTYPE ScriptInterpreter::QueryService(REFGUID guidService, REFIID riid, void **ppv)
{
   return (riid == IID_IInternetHostSecurityManager) ? QueryInterface(riid /*IID_IInternetHostSecurityManager*/, ppv) : E_NOINTERFACE;
}


///////////////////////////////////////////////////////////////////////////////////////////////////
// Debug script object

STDMETHODIMP ScriptInterpreter::DebuggerModule::Print(VARIANT *pvar)
{
   // Disable logging in locked tables (there is no debugger in locked mode anyway)
   if (g_pplayer->m_ptable->IsLocked())
      return S_OK;

   if (!g_app->m_settings.GetEditor_EnableLog() || !g_app->m_settings.GetEditor_LogScriptOutput())
      return S_OK;

   if (V_VT(pvar) == VT_EMPTY || V_VT(pvar) == VT_NULL || V_VT(pvar) == VT_ERROR)
   {
      PLOGI << "Script.Print ''";
      return S_OK;
   }

   CComVariant varT;
   const HRESULT hr = VariantChangeType(&varT, pvar, 0, VT_BSTR);

   if (FAILED(hr))
   {
      const LocalString ls(IDS_DEBUGNOCONVERT);
      PLOGI << "Script.Print '" << ls.m_szbuffer << '\'';
      return S_OK;
   }

   PLOGI << "Script.Print '" << MakeString(V_BSTR(&varT)) << '\'';

   return S_OK;
}
