// license:GPLv3+

#include "core/stdafx.h"

#include "PinTableMDI.h"


#include "ui/win/WinEditor.h"

static CComObject<PinTable>* CreatePinTable()
{
   CComObject<PinTable>* table;
   CComObject<PinTable>::CreateInstance(&table);
   return table; // Note that the ref count is zero so far
}

PinTableMDI::PinTableMDI(WinEditor* vpinball)
   : m_tableWnd(std::make_unique<PinTableWnd>(vpinball, CreatePinTable()))
   , m_vpxEditor(vpinball)
{
   m_tableWnd->SetMDITable(this);
}

PinTableMDI::~PinTableMDI()
{
   m_vpxEditor->CloseAllDialogs();
   m_tableWnd->FVerifySaveToClose();
   RemoveFromVectorSingle(m_vpxEditor->m_vtable, m_tableWnd.get());
}

bool PinTableMDI::CanClose() const
{
    if (m_tableWnd->m_table != nullptr && m_tableWnd->m_table->FDirty())
    {
        const string szText = LocalString(IDS_SAVE_CHANGES1).m_szbuffer /*"Do you want to save the changes you made to '"*/ + m_tableWnd->m_table->m_title + LocalString(IDS_SAVE_CHANGES2).m_szbuffer;
    }
    return true;
}

void PinTableMDI::PreCreate(CREATESTRUCT &cs)
{
    cs.x = 20;
    cs.y = 20;
    cs.cx = 400;
    cs.cy = 400;
    cs.style = WS_MAXIMIZE;
    cs.hwndParent = m_vpxEditor->GetHwnd();
    cs.lpszClass = _T("PinTable");
    cs.lpszName = _T("");
}

int PinTableMDI::OnCreate(CREATESTRUCT &cs)
{
    return 0;
}

void PinTableMDI::OnClose()
{
}

LRESULT PinTableMDI::OnMDIActivate(UINT msg, WPARAM wparam, LPARAM lparam)
{
   return 0L;
}

BOOL PinTableMDI::OnEraseBkgnd(CDC& dc)
{
   return TRUE;
}
