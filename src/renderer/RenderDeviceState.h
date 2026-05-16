// license:GPLv3+

#pragma once

#include "RenderState.h"

class RenderDevice;
class ShaderState;

class RenderDeviceState final
{
public:
   RenderDeviceState(RenderDevice* rd);
   ~RenderDeviceState();

   const RenderDevice* m_rd;
   ShaderState* const m_uiShaderState;
   ShaderState* const m_basicShaderState;
   ShaderState* const m_DMDShaderState;
   ShaderState* const m_FBShaderState;
   ShaderState* const m_flasherShaderState;
   ShaderState* const m_lightShaderState;
   ShaderState* const m_ballShaderState;
   ShaderState* const m_stereoShaderState;
   RenderState m_renderState;
};
