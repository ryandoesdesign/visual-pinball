// license:GPLv3+

#pragma once

#include "parts/pintable.h"

class PinTableMDI;

class PinTableWnd : public CWnd
{
public:
   explicit PinTableWnd(WinEditor *vpxEditor, CComObject<PinTable> *table);
   ~PinTableWnd();

   void SetMDITable(PinTableMDI *const table) { m_mdiTable = table; }
   PinTableMDI *GetMDITable() const { return m_mdiTable; }

   ISelect *HitTest(const int x, const int y);

   void SetCaption(const string &caption);
   int ShowMessageBox(const char *text) const;

   void FillCollectionContextMenu(CMenu &mainMenu, CMenu &colSubMenu, ISelect *psel);
   void FillLayerContextMenu(CMenu &mainMenu, CMenu &layerSubMenu, ISelect *psel);

   void Redraw();
   void SetDefaultView();
   void GetViewRect(FRect *pfrect) const;
   void SetMyScrollInfo();
   POINT GetScreenPoint() const;
   void ExportBlueprint();
   bool GetDisplayGrid() const;
   void SetDisplayGrid(const bool display);
   bool GetDisplayBackdrop() const;
   void SetDisplayBackdrop(const bool backdrop);
   const Vertex2D &GetViewOffset() const;
   void SetViewOffset(const Vertex2D &offset);
   float GetZoom() const;
   void SetZoom(float zoom);

   void FVerifySaveToClose();
   void BeginAutoSaveCounter();
   void EndAutoSaveCounter();
   void AutoSave();

   void ShowSearchSelectDlg();

   void OnPartChanged(IEditable *part);

   CComObject<PinTable> *const m_table;
   
   std::unique_ptr<class CodeViewer> m_pcv;

   ViewSetupID m_currentBackglassMode = ViewSetupID::BG_DESKTOP; // POV shown in the UI (not persisted)

protected:

private:

   WinEditor *const m_vpxEditor;
   PinTableMDI *m_mdiTable = nullptr;

   std::unique_ptr<class SearchSelectDialog> m_searchSelectDlg;

   bool m_moving = false;
   short2 m_oldMousePos;

   vector<HANDLE> m_vAsyncHandles;

   bool m_dirtyDraw = true; // Whether our background bitmap is up to date
   HBITMAP m_hbmOffScreen = nullptr; // Buffer for drawing the editor window

private:
   POINT m_ptLast {}; // Last point when dragging

};
