// license:GPLv3+

#include "core/stdafx.h"

#include "ui/win/sur.h"
#include "ui/win/WinEditor.h"

ISelect::ISelect()
   : m_vpinball(g_pvp)
{
}

void ISelect::SetDirtyDraw()
{
   if (GetPTable())
      GetPTable()->SetDirtyDraw();
}

void ISelect::SetObjectPos()
{
   m_vpinball->ClearObjectPosCur();
}

void ISelect::RenderBlueprint(Sur *psur, const bool solid) { UIRenderPass2(psur); }

void ISelect::OnLButtonDown(int x, int y)
{
   m_dragging = true;
   m_markedForUndo = false; // So we will be marked when and if we are dragged

   GetPTable()->SetMouseCapture();

   SetObjectPos();
}

void ISelect::OnLButtonUp(int x, int y)
{
   m_dragging = false;


   if (m_markedForUndo)
   {
      m_markedForUndo = false;
      STOPUNDOSELECT
   }
}

void ISelect::DoCommand(int icmd, int x, int y)
{
}

void ISelect::SetSelectFormat(Sur *psur)
{
   const COLORREF color = IsUILocked() ? m_vpinball->m_elemSelectLockedColor
                                       : m_vpinball->m_elemSelectColor;//GetSysColor(COLOR_HIGHLIGHT);

   psur->SetBorderColor(color, false, 4);
   psur->SetLineColor(color, false, 4);
}

void ISelect::SetMultiSelectFormat(Sur *psur)
{
   const COLORREF color = IsUILocked() ? m_vpinball->m_elemSelectLockedColor :
                                         m_vpinball->m_elemSelectColor;//GetSysColor(COLOR_HIGHLIGHT);

   psur->SetBorderColor(color, false, 3);
   psur->SetLineColor(color, false, 3);
}

void ISelect::SetLockedFormat(Sur *psur)
{
   psur->SetBorderColor(m_vpinball->m_elemSelectLockedColor, false, 1);
   psur->SetLineColor(m_vpinball->m_elemSelectLockedColor, false, 1);
}

void ISelect::FlipY(const Vertex2D& pvCenter)
{
   GetIEditable()->MarkForUndo(); // Start/EndUndo cycle is around the loop that calls this

   Vertex2D vCenter = GetCenter();
   const float delta = vCenter.y - pvCenter.y;
   vCenter.y -= delta * 2.f;
   PutCenter(vCenter);
}

void ISelect::FlipX(const Vertex2D& pvCenter)
{
   GetIEditable()->MarkForUndo(); // Start/EndUndo cycle is around the loop that calls this

   Vertex2D vCenter = GetCenter();
   const float delta = vCenter.x - pvCenter.x;
   vCenter.x -= delta * 2.f;
   PutCenter(vCenter);
}

void ISelect::Rotate(const float ang, const Vertex2D& pvCenter, const bool useElementCenter)
{
   GetIEditable()->MarkForUndo(); // Start/EndUndo cycle is around the loop that calls this

   Vertex2D vCenter = GetCenter();

   const float sn = sinf(ANGTORAD(ang));
   const float cs = cosf(ANGTORAD(ang));

   const float dx = vCenter.x - pvCenter.x;
   const float dy = vCenter.y - pvCenter.y;

   vCenter.x = pvCenter.x + cs*dx - sn*dy;
   vCenter.y = pvCenter.y + cs*dy + sn*dx;
   PutCenter(vCenter);
}

void ISelect::Scale(const float scalex, const float scaley, const Vertex2D& pvCenter, const bool useElementCenter)
{
   GetIEditable()->MarkForUndo(); // Start/EndUndo cycle is around the loop that calls this

   Vertex2D vCenter = GetCenter();

   const float dx = vCenter.x - pvCenter.x;
   const float dy = vCenter.y - pvCenter.y;

   vCenter.x = pvCenter.x + dx*scalex;
   vCenter.y = pvCenter.y + dy*scaley;
   PutCenter(vCenter);
}

void ISelect::Translate(const Vertex2D &pvOffset)
{
   GetIEditable()->MarkForUndo(); // Start/EndUndo cycle is around the loop that calls this

   Vertex2D vCenter = GetCenter();

   vCenter.x += pvOffset.x;
   vCenter.y += pvOffset.y;
   PutCenter(vCenter);
}

HRESULT ISelect::GetTypeName(BSTR *pVal) const
{
   wstring buf = GetTypeNameForType(GetItemType());
   *pVal = SysAllocStringLen(buf.c_str(), static_cast<UINT>(buf.length()));
   return S_OK;
}

wstring ISelect::GetTypeNameForType(const ItemTypeEnum type) const
{
   UINT strID;
   switch (type)
   {
   case eItemTable:        strID = IDS_TABLE; break;
   case eItemLightCenter:  strID = IDS_TB_LIGHT; break;
   case eItemDragPoint:    strID = IDS_CONTROLPOINT; break;
   //case eItemLightSeqCenter: strID = IDS_TB_LIGHTSEQ; break;
   default:
      strID = EditableRegistry::GetTypeNameStringID(type); break;
   }

   return LocalStringW(strID).m_buffer;
}

void ISelect::UpdateStatusBarInfo()
{
   if (m_vpinball)
      m_vpinball->SetStatusBarUnitInfo(string(), false);
}

bool ISelect::IsVisible(IEditable *editable) const
{
   return IsUIVisible()
      && (editable == nullptr || editable->GetPartGroup() == nullptr || editable->GetPartGroup()->GetISelect() == nullptr
         || editable->GetPartGroup()->GetISelect()->IsVisible(editable->GetPartGroup()));
}
