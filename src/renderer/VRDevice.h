// license:GPLv3+

#pragma once


class VRDevice final
{
public:
   VRDevice(const Settings& settings);
   ~VRDevice();

   unsigned int GetEyeWidth() const { return m_eyeWidth; }
   unsigned int GetEyeHeight() const { return m_eyeHeight; }
   
   float GetLockbarWidth() const { return m_lockbarWidth; }
   void SetLockbarWidth(float width) { m_lockbarWidth = width; m_worldDirty = true; }
   float GetLockbarHeight() const { return m_lockbarHeight; }
   void SetLockbarHeight(float height) { m_lockbarHeight = height; m_worldDirty = true; }

   void OffsetTable(float dx, float dy, float dz);
   void RecenterTable();
   float GetSceneOrientation() const { return m_orientation; }
   const Vertex3Ds& GetSceneOffset() const { return m_tablePos; }
   void SetSceneOrientation(float orientation) { m_orientation = orientation; m_worldDirty = true; }
   void SetSceneOffset(const Vertex3Ds& pos) { m_tablePos = pos; m_worldDirty = true; }
   void SaveVRSettings(Settings& settings) const;

   void UpdateVRPosition(PartGroupData::SpaceReference spaceRef, ModelViewProj& mvp);

   float GetPredictedDisplayTimestamp() const { return m_predictedDisplayTimestamp; }

private:
   unsigned int m_eyeWidth = 1080;
   unsigned int m_eyeHeight = 1020;

   float m_scale = 1.0f;
   float m_lockbarWidth = 57.0f; // Real world width of the lockbar in cm
   float m_lockbarHeight = 85.0f; // Real world height (from ground) of the lockbar in cm
   float m_orientation = 0.0f;
   Vertex3Ds m_tablePos;
   float m_slope = 0.0f;

   float m_predictedDisplayTimestamp = 0.f;

   bool m_worldDirty = true;
   struct Viewpoint
   {
      Matrix3D m_toWorld; // Matrix to transform from this viewpoint to world coordinates
      Matrix3D m_view[2];
   };
   Viewpoint m_pfWorld;
   Viewpoint m_cabWorld;
   Viewpoint m_feetWorld;
   Viewpoint m_roomWorld;
   Matrix3D m_roomProj[2];
   Matrix3D m_sceneProj[2];


};
