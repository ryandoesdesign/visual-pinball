// license:GPLv3+

// implementation of the VPinball class.

#include "core/stdafx.h"
#include "WinEditor.h"

#include <filesystem>


#include "core/vpversion.h"
#include "parts/bumper.h"
#include "parts/decal.h"
#include "parts/dispreel.h"
#include "parts/flasher.h"
#include "parts/flipper.h"
#include "parts/gate.h"
#include "parts/hittarget.h"
#include "parts/kicker.h"
#include "parts/light.h"
#include "parts/lightseq.h"
#include "parts/PartGroup.h"
#include "parts/plunger.h"
#include "parts/primitive.h"
#include "parts/ramp.h"
#include "parts/rubber.h"
#include "parts/spinner.h"
#include "parts/surface.h"
#include "parts/textbox.h"
#include "parts/timer.h"
#include "parts/trigger.h"
#include "ui/VPXFileFeedback.h"
#include "ui/win/codeview.h"
#include "ui/win/PinTableMDI.h"
#include "ui/win/resource.h"
#include "ui/win/worker.h"


#ifdef __LIBVPINBALL__
#include "lib/src/VPinballLib.h"
#endif


#if defined(IMSPANISH)
#define TOOLBAR_WIDTH 152
#elif defined(IMGERMAN)
#define TOOLBAR_WIDTH 152
#else
#define TOOLBAR_WIDTH 102 //98 //102
#endif

#define SCROLL_WIDTH GetSystemMetrics(SM_CXVSCROLL)

#define DOCKER_REGISTRY_KEY     "Visual Pinball\\VP10\\Editor"

#define RECENT_FIRST_MENU_IDM   5000           // ID of the first recent file list filename
#define OPEN_MDI_TABLE_IDM      IDW_FIRSTCHILD // ID of the first open table
#define LAST_MDI_TABLE_IDM      IDW_CHILD9

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////
// menu locations
enum {
   FILEMENU = 0,
   EDITMENU,
   VIEWMENU,
   INSERTMENU,
   TABLEMENU,
   LAYERMENU,
   PREFMENU,
   WINDOWMENU,
   HELPMENU,
   NUM_MENUS
};

/*
TBButton:
typedef struct {
int       iBitmap;
int       idCommand;
BYTE      fsState;
BYTE      fsStyle;
DWORD_PTR dwData;
INT_PTR   iString;
} TBBUTTON, *PTBBUTTON, *LPTBBUTTON;
*/

INT_PTR CALLBACK FontManagerProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK SecurityOptionsProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);

typedef struct _tagSORTDATA
{
    HWND hwndList;
    int subItemIndex;
    int sortUpDown;
}SORTDATA;

SORTDATA SortData;

WinEditor::WinEditor(HINSTANCE appInstance)
   : m_instance(appInstance)
{
   m_closing = false;
   m_unloadingTable = false;
   m_cref = 0;				//inits Reference Count for IUnknown Interface. Every com Object must 
   //implement this and StdMethods QueryInterface, AddRef and Release

   m_mouseCursorPosition.x = 0.0f;
   m_mouseCursorPosition.y = 0.0f;

   m_NextTableID = 1;

   m_workerthread = nullptr;//Workerthread - only for hanging scripts and autosave - will be created later

   m_ToolCur = IDC_SELECT;

   m_hbmInPlayMode = nullptr;


   wintimer_init();
}

//deletes clipboard
//Releases Resources for Script editor
WinEditor::~WinEditor()
{
   SetClipboard(nullptr);
}


//Post Work to the worker Thread
//Creates Worker-Thread if not present
//See Worker::VPWorkerThreadStart for infos
//workid int for the type of message (COMPLETE_AUTOSAVE | HANG_SNOOP_START | HANG_SNOOP_STOP)
//Second Parameter for message (AutoSavePackage (see worker.h) if COMPLETE_AUTOSAVE, otherwise nullptr)
//returns Handle to Event that get ack. If event is finished (unsure)
HANDLE WinEditor::PostWorkToWorkerThread(int workid, LPARAM lParam)
{
   return nullptr;
}

void WinEditor::SetAutoSaveMinutes(const int minutes)
{
   m_autosaveTime = (minutes <= 0) ? -1 : minutes * (60 * 1000); // convert to milliseconds
}

//Post Work to the worker Thread
//Creates Worker-Thread if not present
//See Worker::VPWorkerThreadStart for infos
//workid int for the type of message (COMPLETE_AUTOSAVE | HANG_SNOOP_START | HANG_SNOOP_STOP)
//Second Parameter for message (AutoSavePackage (see worker.h) if COMPLETE_AUTOSAVE, otherwise nullptr)
//returns Handle to Event that get ack. If event is finished (unsure)
void WinEditor::InitTools()
{
   m_ToolCur = IDC_SELECT;
}

// Load editor behavior options from the settings
void WinEditor::LoadEditorSetupFromSettings()
{
   m_alwaysDrawDragPoints = g_app->m_settings.GetEditor_ShowDragPoints();
   m_alwaysDrawLightCenters = g_app->m_settings.GetEditor_DrawLightCenters();
   m_gridSize = g_app->m_settings.GetEditor_GridSize();

   const bool autoSave = g_app->m_settings.GetEditor_AutoSaveOn();
   if (autoSave)
   {
      m_autosaveTime = g_app->m_settings.GetEditor_AutoSaveTime();
      SetAutoSaveMinutes(m_autosaveTime);
   }
   else
      m_autosaveTime = -1;

   m_elemSelectColor = g_app->m_settings.GetEditor_ElementSelectColor();
   m_elemSelectLockedColor = g_app->m_settings.GetEditor_ElementSelectLockedColor();
   m_backgroundColor = g_app->m_settings.GetEditor_BackGroundColor();
   m_fillColor = g_app->m_settings.GetEditor_FillColor();

   m_recentTableList.clear();
   // get the list of the last n loaded tables
   for (int i = 0; i < LAST_OPENED_TABLE_COUNT; i++)
   {
      string szTableName = g_app->m_settings.GetRecentDir_TableFileName(i);
      if (!szTableName.empty())
         m_recentTableList.push_back(std::move(szTableName));
   }

   m_convertToUnit = g_app->m_settings.GetEditor_Units();
}

void WinEditor::SetClipboard(vector<IStream*> * const pvstm)
{
   for (size_t i = 0; i < m_vstmclipboard.size(); i++)
      m_vstmclipboard[i]->Release();
   m_vstmclipboard.clear();

   if (pvstm)
      for (size_t i = 0; i < pvstm->size(); i++)
         m_vstmclipboard.push_back((*pvstm)[i]);
}

void WinEditor::SetCursorCur(HINSTANCE hInstance, LPCTSTR lpCursorName)
{
}

void WinEditor::SetActionCur(const string& szaction)
{
}

void WinEditor::SetStatusBarElementInfo(const string& info)
{
}

bool WinEditor::OpenFileDialog(const string& initDir, vector<string>& filename, const char* const fileFilter, const char* const defaultExt, const DWORD flags, const string& windowTitle) //!! use this all over the place and move to some standard header
{
   return false;
}

bool WinEditor::SaveFileDialog(const string& initDir, vector<string>& filename, const char* const fileFilter, const char* const defaultExt, const DWORD flags, const string& windowTitle) //!! use this all over the place and move to some standard header
{
   return false;
}

CDockProperty *WinEditor::GetPropertiesDocker()
{
   return m_dockProperties;
}

CDockToolbar *WinEditor::GetToolbarDocker()
{
   return m_dockToolbar;
}

void WinEditor::ResetAllDockers()
{
}

CDockNotes* WinEditor::GetDefaultNotesDocker()
{
   return m_dockNotes;
}

CDockNotes* WinEditor::GetNotesDocker()
{
   return m_dockNotes;
}

CDockLayers *WinEditor::GetLayersDocker()
{
   return m_dockLayers;
}

void WinEditor::CreateDocker()
{
}

void WinEditor::SetPosCur(float x, float y)
{
}

void WinEditor::SetObjectPosCur(float x, float y)
{
}

void WinEditor::ClearObjectPosCur()
{
}

float WinEditor::ConvertToUnit(const float value) const
{
   switch (m_convertToUnit)
   {
      case 0:
        return vpUnitsToInches(value);
      case 1:
        return vpUnitsToMillimeters(value);
      case 2:
        return value;
   }
   return 0;
}

void WinEditor::SetPropSel(VectorProtected<ISelect> &pvsel)
{
}

void WinEditor::RenameEditable(IEditable *editable, const string &name)
{
   const string oldName = MakeString(editable->GetIScriptable()->m_wzName);
   editable->SetName(MakeWString(name));

}

CMenu WinEditor::GetMainMenu(int id)
{
   return CMenu();
}


bool WinEditor::ParseCommand(const size_t code, const bool notify)
{
   return false;
}

void WinEditor::ToggleToolbar()
{
}

void WinEditor::DoPlay(const int playMode)
{
   if (g_pplayer)
      return; // Can't play twice

   PinTableWnd *const tableEditor = GetActiveTableEditor();
   CComObject<PinTable> *const table = GetActiveTable(); //!! no const here because of CopyForPlay call below that changes some data implicitly inside
   if (tableEditor == nullptr || table == nullptr)
   {
      m_table_played_via_SelectTableOnStart = false;
      return;
   }

   PLOGI << "Starting Play mode [table: " << table->m_tableName << ", play mode: " << playMode << ']';

   // Create the player on a (shallow) copy of the table, that will be animated by the script, animations, ...
   PinTable *live_table = table->CopyForPlay();
   if (live_table == nullptr)
   {
      m_table_played_via_SelectTableOnStart = false;
      return;
   }
   switch (playMode)
   {
   case 0: new Player(live_table, Player::PlayMode::Play); break;
   case 1: new Player(live_table, Player::PlayMode::EditPOV); break;
   case 2: new Player(live_table, Player::PlayMode::LiveEdit); break;
   default: assert(false); break;
   }

   if (g_pplayer == nullptr)
   {
      m_table_played_via_SelectTableOnStart = false;
      return;
   }


   // Switch to Player's main loop (needed to avoid interference between editor's Window Msg loop and player's specific msg loop, also Player has a fairly specific msg loop)
   g_pplayer->GameLoop();
   delete g_pplayer;
   assert(g_pplayer == nullptr);

   // The table settings may have been edited during play (camera, rendering, ...), so copy them back to the editor table's settings
   table->m_settings.Load(live_table->m_settings);
   table->m_settings.SetModified(live_table->m_settings.IsModified());
   live_table->Release();


   // If the table was played via the "Select Table on Start" option, then close the table and propose to load another one
   if (m_table_played_via_SelectTableOnStart)
   {
      CloseTable(tableEditor);
      m_table_played_via_SelectTableOnStart = LoadFile(false);
      if (m_table_played_via_SelectTableOnStart)
         DoPlay(0);
   }
}

bool WinEditor::LoadFile(const bool updateEditor, VPXFileFeedback* feedback)
{
   const string& szInitialDir = g_app->m_settings.GetRecentDir_LoadDir();

   vector<string> filename;
   if (!OpenFileDialog(szInitialDir, filename, "Visual Pinball Tables (*.vpx)\0*.vpx\0Old Visual Pinball Tables(*.vpt)\0*.vpt\0", "vpx", 0,
          !updateEditor ? "Select a Table to Play or press Cancel to enter Editor-Mode"s : string()))
      return false;

   const size_t index = filename[0].find_last_of(PATH_SEPARATOR_CHAR);
   if (index != string::npos)
      g_app->m_settings.SetRecentDir_LoadDir(filename[0].substr(0, index), false);

   LoadFileName(filename[0], updateEditor, feedback);

   return true;
}

void WinEditor::LoadFileName(const string& filename, const bool updateEditor, VPXFileFeedback* feedback)
{
   if (m_vtable.size() == MAX_OPEN_TABLES)
   {
      ShowError("Maximum amount of tables already loaded and open.");
      return;
   }

   if (!FileExists(filename))
   {
      ShowError("File not found \"" + filename + '"');
      return;
   }

   CloseAllDialogs();

   PinTableMDI * const mdiTable = new PinTableMDI(this);
   PinTableWnd *const ppt = mdiTable->GetTableWnd();
   const HRESULT hr = feedback != nullptr ? ppt->m_table->LoadGameFromFilename(filename, *feedback) : ppt->m_table->LoadGameFromFilename(filename);

   const bool hashing_error = (hr == APPX_E_BLOCK_HASH_INVALID || hr == APPX_E_CORRUPT_CONTENT);
   if (hashing_error)
      ShowError(LocalString(IDS_CORRUPTFILE).m_szbuffer);

   if (!SUCCEEDED(hr) && !hashing_error)
   {
      ShowError("This file does not exist, or is corrupt and failed to load.");

      delete mdiTable;
   }
   else
   {
      m_vtable.push_back(ppt);

      m_ptableActive = ppt->m_table;

      AddMDIChild(mdiTable);

      // make sure the load directory is the active directory
      const std::filesystem::path tablePath = PathFromFilename(filename);
      SetCurrentDirectory(tablePath.string().c_str());

      PLOGI << "UI Post Load Start";

      g_app->m_settings.SetRecentDir_LoadDir(tablePath.string(), false);
      UpdateRecentFileList(filename);

      ppt->m_table->AddMultiSel(ppt->m_table, false, true, false);
      if (updateEditor)
      {
      }

      PLOGI << "UI Post Load End";
   }
}

PinTableWnd* WinEditor::GetActiveTableEditor()
{
   if (const auto mdiTable = (PinTableMDI *)GetActiveMDIChild(); mdiTable && !m_unloadingTable)
      return mdiTable->GetTableWnd();
   return nullptr;
}

CComObject<PinTable>* WinEditor::GetActiveTable()
{
   if (const auto mdiTable = (PinTableMDI *)GetActiveMDIChild(); mdiTable && !m_unloadingTable && mdiTable->GetTableWnd())
      return mdiTable->GetTableWnd()->m_table;
   return nullptr;
}

bool WinEditor::CanClose()
{
   while (!m_vtable.empty())
   {
      if (!m_vtable[0]->GetMDITable()->CanClose())
         return false;

      CloseTable(m_vtable[0]);
   }

   return true;
}

void WinEditor::CloseTable(PinTableWnd * ppt)
{
    PinTableMDI* mdiTable = ppt->GetMDITable();
    if (mdiTable)
        RemoveMDIChild(mdiTable);

    RemoveFromVectorSingle(m_vtable, ppt);
}

void WinEditor::SetEnableMenuItems()
{
}

void WinEditor::UpdateRecentFileList(const std::filesystem::path &filename)
{
   const size_t old_count = m_recentTableList.size();

   // if the loaded file name is a valid one then add it to the top of the list
   if (!filename.empty())
   {
      vector<string> newList;
      newList.push_back(filename.string());

      for (const string &tableName : m_recentTableList)
      {
         if (tableName != newList[0]) // does this file name already exist in the list?
            newList.push_back(tableName);
      }

      m_recentTableList.clear();

      int i = 0;
      for (const string &tableName : newList)
      {
         m_recentTableList.push_back(tableName);
         // write entry to the registry
         g_app->m_settings.SetRecentDir_TableFileName(i, tableName, false);

         if (++i == LAST_OPENED_TABLE_COUNT)
            break;
      }
   }

   // update the file menu to contain the last n recent loaded files
   // must be at least 1 recent file in the list
   if (!m_recentTableList.empty())
   {
   }
}

void WinEditor::PreCreate(CREATESTRUCT& cs)
{
   // do the base class stuff
   CWnd::PreCreate(cs);
   const int screenwidth = GetSystemMetrics(SM_CXSCREEN);  // width of primary monitor
   const int screenheight = GetSystemMetrics(SM_CYSCREEN); // height of primary monitor

   const int x = (screenwidth - MAIN_WINDOW_WIDTH) / 2;
   const int y = (screenheight - MAIN_WINDOW_HEIGHT) / 2;
   constexpr int width = MAIN_WINDOW_WIDTH;
   constexpr int height = MAIN_WINDOW_HEIGHT;

   cs.x = x; // set initial window placement
   cs.y = y;
   cs.cx = width;
   cs.cy = height;
   // specify a title bar and border with a window-menu on the title bar
   cs.style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_SIZEBOX | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
   cs.dwExStyle = WS_EX_CLIENTEDGE
              | WS_EX_CONTROLPARENT   // TAB key navigation
      //      | WS_EX_CONTEXTHELP     // doesn't work if WS_MINIMIZEBOX
                                      // or WS_MAXIMIZEBOX is specified
      ;
}

void WinEditor::PreRegisterClass(WNDCLASS& wc)
{
   wc.style = CS_DBLCLKS; //CS_NOCLOSE | CS_OWNDC;
   wc.lpszClassName = _T("VPinball");
   wc.lpszMenuName = _T("IDR_APPMENU");
}

void WinEditor::OnClose()
{
   // Reject close if player was not closed before
   if (g_pplayer)
      return;

   CComObject<PinTable> * const ptable = GetActiveTable();
   m_closing = true;

   while (ShowCursor(FALSE) >= 0);
   while (ShowCursor(TRUE) < 0);

   if (ptable)
      while (ptable->m_savingActive)
         Sleep(THREADS_PAUSE);

}

void WinEditor::OnDestroy()
{
}

void WinEditor::ShowSubDialog(CDialog &dlg, const bool show)
{
}


int WinEditor::OnCreate(CREATESTRUCT& cs)
{
   return 0;
}

LRESULT WinEditor::OnPaint(UINT msg, WPARAM wparam, LPARAM lparam)
{
   return 0;
}

// Called when window is initially updated to be able to perform initial setup of the window and its children
void WinEditor::OnInitialUpdate()
{
   PLOGI << "OnInitialUpdate";

   LoadEditorSetupFromSettings();


   UpdateRecentFileList(string()); // update the recent loaded file list

   //   InitTools();
   //   SetForegroundWindow();
   SetEnableMenuItems();
}

BOOL WinEditor::OnCommand(WPARAM wparam, LPARAM lparam)
{
   return TRUE;
}


LRESULT WinEditor::WndProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
   return 0;
}

LRESULT WinEditor::OnMDIActivated(UINT msg, WPARAM wparam, LPARAM lparam)
{
   return 0;
}

LRESULT WinEditor::OnMDIDestroyed(UINT msg, WPARAM wparam, LPARAM lparam)
{
   return 0;
}


int CALLBACK MyCompProc(LPARAM lSortParam1, LPARAM lSortParam2, LPARAM lSortOption)
{
   return 0;
}

int CALLBACK MyCompProcIntValues(LPARAM lSortParam1, LPARAM lSortParam2, LPARAM lSortOption)
{
   return 0;
}

int CALLBACK MyCompProcMemValues(LPARAM lSortParam1, LPARAM lSortParam2, LPARAM lSortOption)
{
   const SORTDATA * const lpsd = (SORTDATA *)lSortOption;
   const Texture * const t1 = (Texture *)lSortParam1;
   const Texture * const t2 = (Texture *)lSortParam2;
   const size_t t1_size = t1->GetEstimatedGPUSize();
   const size_t t2_size = t2->GetEstimatedGPUSize();
   if (lpsd->sortUpDown == 1)
      return (int)(t1_size - t2_size);
   else
      return (int)(t2_size - t1_size);
}

static constexpr int rgDlgIDFromSecurityLevel[] = { IDC_ACTIVEX0, IDC_ACTIVEX1, IDC_ACTIVEX2, IDC_ACTIVEX3, IDC_ACTIVEX4 };

INT_PTR CALLBACK SecurityOptionsProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{

   return FALSE;
}

INT_PTR CALLBACK FontManagerProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{

   return FALSE;
}


void WinEditor::ShowDrawingOrderDialog(bool select)
{
}

void WinEditor::CloseAllDialogs()
{
}

void WinEditor::ToggleBackglassView()
{
   const bool show = !m_desktopBackdropView;
   m_desktopBackdropView = show;

   for (const auto ptT : m_vtable)
   {
      ptT->SetDefaultView();
      ptT->m_table->SetDirtyDraw();
   }

   CComObject<PinTable> * const ptCur = GetActiveTable();
   if (ptCur)
      // Set selection to something in the new view (unless hiding table elements)
      ptCur->AddMultiSel((ISelect *)ptCur, false, true, false);

   ToggleToolbar();
}

void WinEditor::ToggleScriptEditor()
{
}

void WinEditor::ShowSearchSelect()
{
}

void WinEditor::SetDefaultPhysics()
{
   CComObject<PinTable> * const ptCur = GetActiveTable();
   if (ptCur)
   {
      const int answ = MessageBox(LocalString(IDS_DEFAULTPHYSICS).m_szbuffer, "Continue?", MB_YESNO | MB_ICONWARNING);
      if (answ == IDYES)
      {
         ptCur->BeginUndo();
         for (int i = 0; i < ptCur->m_vmultisel.size(); i++)
            ptCur->m_vmultisel[i].SetDefaultPhysics(true);
         ptCur->EndUndo();
      }
   }
}

void WinEditor::SetViewSolidOutline(size_t viewId)
{
}

void WinEditor::ShowGridView()
{
}

void WinEditor::ShowBackdropView()
{
}

void WinEditor::AddControlPoint()
{
   const auto ptCur = GetActiveTableEditor();
   if (ptCur == nullptr)
      return;

   if (!ptCur->m_table->m_vmultisel.empty())
   {
      ISelect * const psel = ptCur->m_table->m_vmultisel.ElementAt(0);
      if (psel != nullptr)
      {
         const POINT pt = ptCur->GetScreenPoint();
         switch (psel->GetItemType())
         {
         case eItemRamp:
         {
            Ramp * const pRamp = (Ramp *)psel;
            pRamp->AddPoint(pt.x, pt.y, false);
            break;
         }
         case eItemLight:
         {
            Light * const pLight = (Light *)psel;
            pLight->AddPoint(pt.x, pt.y, false);
            break;
         }
         case eItemSurface:
         {
            Surface * const pSurf = (Surface *)psel;
            pSurf->AddPoint(pt.x, pt.y, false);
            break;
         }
         case eItemRubber:
         {
            Rubber * const pRub = (Rubber *)psel;
            pRub->AddPoint(pt.x, pt.y, false);
            break;
         }
         default:
            break;
         }
      } //if (psel != nullptr)
   }
}

void WinEditor::AddSmoothControlPoint()
{
   const auto ptCur = GetActiveTableEditor();
   if (ptCur == nullptr)
      return;

   if (!ptCur->m_table->m_vmultisel.empty())
   {
      ISelect *const psel = ptCur->m_table->m_vmultisel.ElementAt(0);
      if (psel != nullptr)
      {
         const POINT pt = ptCur->GetScreenPoint();
         switch (psel->GetItemType())
         {
         case eItemRamp:
         {
            Ramp * const pRamp = (Ramp *)psel;
            pRamp->AddPoint(pt.x, pt.y, true);
            break;
         }
         case eItemLight:
         {
            Light * const pLight = (Light *)psel;
            pLight->AddPoint(pt.x, pt.y, true);
            break;
         }
         case eItemSurface:
         {
            Surface * const pSurf = (Surface *)psel;
            pSurf->AddPoint(pt.x, pt.y, true);
            break;
         }
         case eItemRubber:
         {
            Rubber * const pRub = (Rubber *)psel;
            pRub->AddPoint(pt.x, pt.y, true);
            break;
         }
         default:
            break;
         }
      }
   }
}

void WinEditor::SaveTable(const bool saveAs)
{
}

void WinEditor::OpenNewTable(size_t tableId)
{
   if (m_vtable.size() == MAX_OPEN_TABLES)
   {
      ShowError("Maximum amount of tables already loaded and open.");
      return;
   }

}

void WinEditor::ProcessDeleteElement()
{
   CComObject<PinTable> * const ptCur = GetActiveTable();
   if (ptCur)
      ptCur->OnDelete();
}

void WinEditor::OpenRecentFile(const size_t menuId)
{
   // get the index into the recent list menu
   const size_t Index = menuId - RECENT_FIRST_MENU_IDM;
   // copy it into a temporary string so it can be correctly processed
   LoadFileName(m_recentTableList[Index], true);
}

void WinEditor::CopyPasteElement(const CopyPasteModes mode)
{
   const auto ptCur = GetActiveTableEditor();
   if (ptCur && !ptCur->m_table->IsLocked())
   {
      const POINT ptCursor = ptCur->GetScreenPoint();
      switch (mode)
      {
      case COPY:
      {
         ptCur->m_table->Copy(ptCursor.x, ptCursor.y);
         break;
      }
      case PASTE:
      {
         ptCur->m_table->Paste(false, ptCursor.x, ptCursor.y);
         break;
      }
      case PASTE_AT:
      {
         ptCur->m_table->Paste(true, ptCursor.x, ptCursor.y);
         break;
      }
      default:
         break;
      }
   }
}
