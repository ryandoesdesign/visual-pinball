// license:GPLv3+

#include "core/stdafx.h"
#include "VRDevice.h"
#include "core/vpversion.h"

#include "parts/primitive.h"

// MSVC Concurrency Viewer support
// This requires to add the MSVC Concurrency SDK to the project
//#define MSVC_CONCURRENCY_VIEWER
#ifdef MSVC_CONCURRENCY_VIEWER
#include <cvmarkersobj.h>
using namespace Concurrency::diagnostic;
extern marker_series series;
#endif







VRDevice::VRDevice(const Settings& settings)
{
      // Scene offset (vertical rotation and horizontal shift)
      m_orientation = settings.GetPlayerVR_Orientation();
      m_tablePos.x = settings.GetPlayerVR_TableX();
      m_tablePos.y = settings.GetPlayerVR_TableY();
      // Offset of the playfield from the room ground is defined as an offset from the lockbar, minus bottom glass height and custom adjustment
      // (Note that for OpenVR offset is defined from the ground)
      m_tablePos.z = settings.GetPlayerVR_TableZ();


}

VRDevice::~VRDevice()
{
}


void VRDevice::OffsetTable(float dx, float dy, float dz)
{
   m_tablePos.x = clamp(m_tablePos.x + dx, -100.0f, 100.0f);
   m_tablePos.y = clamp(m_tablePos.y + dy, -100.0f, 100.0f);
   m_tablePos.z = clamp(m_tablePos.z + dz, -100.0f, 100.0f);
   m_worldDirty = true;
}



void VRDevice::UpdateVRPosition(PartGroupData::SpaceReference spaceRef, ModelViewProj& mvp)
{
   using enum PartGroupData::SpaceReference;

}

void VRDevice::RecenterTable()
{
}

void VRDevice::SaveVRSettings(Settings& settings) const
{
   settings.SetPlayerVR_Orientation(m_orientation, false);
   settings.SetPlayerVR_TableX(m_tablePos.x, false);
   settings.SetPlayerVR_TableY(m_tablePos.y, false);
   settings.SetPlayerVR_TableZ(m_tablePos.z, false);
}
