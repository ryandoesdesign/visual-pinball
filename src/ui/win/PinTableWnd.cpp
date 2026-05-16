// license:GPLv3+

#include "core/stdafx.h"

#include "PinTableWnd.h"

#include "ui/win/codeview.h"
#include "ui/win/hitrectsur.h"
#include "ui/win/hitsur.h"
#include "ui/win/paintsur.h"
#include "ui/win/PinTableMDI.h"
#include "ui/win/sur.h"
#include "ui/win/WinEditor.h"
#include "ui/win/worker.h"

class SearchSelectDialog { };


PinTableWnd::PinTableWnd(WinEditor *vpxEditor, CComObject<PinTable> *table)
   : m_table(table) 
   , m_pcv(std::make_unique<CodeViewer>(table))
   , m_vpxEditor(vpxEditor)
{
   m_table->AddRef();
   m_table->m_tableEditor = this;
   m_pcv->Create(nullptr);
   SetDefaultView();
}

PinTableWnd::~PinTableWnd()
{
   m_table->m_tableEditor = nullptr;
   m_table->Release();
}

void PinTableWnd::SetCaption(const string &szCaption)
{
}

int PinTableWnd::ShowMessageBox(const char *text) const
{
   return 0;
}

void PinTableWnd::Redraw()
{
}

void PinTableWnd::SetDefaultView()
{
   FRect frect;
   GetViewRect(&frect);
   SetViewOffset(frect.Center());
   SetZoom(0.5f);
}

bool PinTableWnd::GetDisplayGrid() const { return m_table->m_winEditorGrid; }
void PinTableWnd::SetDisplayGrid(const bool display) { m_table->m_winEditorGrid = display; }
bool PinTableWnd::GetDisplayBackdrop() const { return m_table->m_winEditorBackdrop; }
void PinTableWnd::SetDisplayBackdrop(const bool backdrop) { m_table->m_winEditorBackdrop = backdrop; }
const Vertex2D &PinTableWnd::GetViewOffset() const { return m_table->m_winEditorViewOffset; }
void PinTableWnd::SetViewOffset(const Vertex2D &offset) { m_table->m_winEditorViewOffset = offset; }
float PinTableWnd::GetZoom() const { return m_table->m_winEditorZoom; }

void PinTableWnd::SetZoom(float zoom)
{
   m_table->m_winEditorZoom = zoom;
   SetMyScrollInfo();
}

void PinTableWnd::GetViewRect(FRect *const pfrect) const
{
   if (!m_vpxEditor->m_desktopBackdropView)
   {
      pfrect->left = m_table->m_left;
      pfrect->top = m_table->m_top;
      pfrect->right = m_table->m_right;
      pfrect->bottom = m_table->m_bottom;
   }
   else
   {
      pfrect->left = 0;
      pfrect->top = 0;
      pfrect->right = EDITOR_BG_WIDTH;
      pfrect->bottom = EDITOR_BG_HEIGHT;
   }
}

void PinTableWnd::SetMyScrollInfo()
{
}

void PinTableWnd::ExportBlueprint()
{
}



POINT PinTableWnd::GetScreenPoint() const
{
   return POINT();
}


ISelect *PinTableWnd::HitTest(const int x, const int y)
{
   return nullptr;
}


void PinTableWnd::FillCollectionContextMenu(CMenu &mainMenu, CMenu &colSubMenu, ISelect *psel)
{
}

void PinTableWnd::FillLayerContextMenu(CMenu &mainMenu, CMenu &layerSubMenu, ISelect *psel)
{
}



void PinTableWnd::BeginAutoSaveCounter()
{
}

void PinTableWnd::EndAutoSaveCounter()
{
}

void PinTableWnd::AutoSave()
{
}

void PinTableWnd::FVerifySaveToClose()
{
}

void PinTableWnd::OnPartChanged(IEditable *part)
{
}

void PinTableWnd::ShowSearchSelectDlg()
{
}
