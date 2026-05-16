// license:GPLv3+

#pragma once

#include "typedefs3D.h"

#include "parts/Material.h"
#include "Texture.h"
#include "Sampler.h"
#include "RenderTarget.h"
#include "RenderState.h"
#include "IndexBuffer.h"
#include "VertexBuffer.h"
#include "MeshBuffer.h"
#include "TextureManager.h"
#include "RenderDeviceState.h"
#include "RenderFrame.h"
#include "RenderPass.h"
#include "Window.h"

#include <SDL3/SDL.h>

#include <thread>
#include <mutex>
#include <semaphore>

#define CHECKD3D(s) { s; }

void ReportFatalError(const HRESULT hr, const char *file, const int line);
void ReportError(const string& errorText, const HRESULT hr, const char *file, const int line);

class Shader;
class ModelViewProj;

class RenderDevice final
{
public:
   RenderDevice(VPX::Window* const wnd, const bool isStereo, const bool isAnaglyph, const bool isVR, const bool useNvidiaApi, const bool compressTextures, int nMSAASamples, VideoSyncMode& syncMode);
   ~RenderDevice();

   void AddWindow(VPX::Window* wnd);
   void RemoveWindow(VPX::Window* wnd);

   enum PrimitiveTypes
   {
      TRIANGLESTRIP,
      TRIANGLELIST,
      POINTLIST,
      LINELIST,
      LINESTRIP
   };

   ////////////////////////////////////////////////////////////////////////////////////////////////
   // (retained) RenderFrame API: Following calls will enqueue rendercommand to the renderframe.
   // Getters returns the state of the renderframe, not the of the renderdevice.
   RenderPass* GetCurrentPass() { return m_currentPass; }
   RenderTarget* GetCurrentRenderTarget() const { assert(m_currentPass != nullptr); return m_currentPass->m_rt; }
   void SetRenderTarget(const string& passName, RenderTarget* rt, const bool useRTContent = true, const bool forceNewPass = false);
   void AddRenderTargetDependency(RenderTarget* rt, const bool needDepth = false);
   void AddRenderTargetDependencyOnNextRenderCommand(RenderTarget* rt);
   void Clear(const DWORD flags, const DWORD colorARGB);
   void BlitRenderTarget(RenderTarget* source, RenderTarget* destination, const bool copyColor = true, const bool copyDepth = true,  
                         const int x1 = -1, const int y1 = -1, const int w1 = -1, const int h1 = -1,
                         const int x2 = -1, const int y2 = -1, const int w2 = -1, const int h2 = -1,
                         const int srcLayer = -1, const int dstLayer = -1);
   void SubmitVR(RenderTarget* source);
   void DrawMesh(Shader* shader, const bool isTranparentPass, const Vertex3Ds& center, const float depthBias, std::shared_ptr<MeshBuffer> mb, const PrimitiveTypes type, const uint32_t startIndex, const uint32_t indexCount);
   void DrawTexturedQuad(Shader* shader, const Vertex3D_TexelOnly* vertices, const bool isTransparent = false, const float depth = 0.f);
   void DrawTexturedQuad(Shader* shader, const Vertex3D_NoTex2* vertices, const bool isTransparent = false, const float depth = 0.f);
   void DrawFullscreenTexturedQuad(Shader* shader);
   void DrawGaussianBlur(RenderTarget* source, RenderTarget* tmp, RenderTarget* dest, float kernel_size, int singleLayer = -1);
   void AddBeginOfFrameCmd(const std::function<void()>& cmd) { m_renderFrame->AddBeginOfFrameCmd(cmd); }
   void AddEndOfFrameCmd(const std::function<void()>& cmd) { m_renderFrame->AddEndOfFrameCmd(cmd); }
   void LogNextFrame() { m_logNextFrame = true; }
   bool IsLogNextFrame() const { return m_logNextFrame; }
   void SubmitRenderFrame();
   void DiscardRenderFrame();

   // RenderState used in submitted render command
   void SetDefaultRenderState() { m_defaultRenderState = m_renderstate; }
   void ResetRenderState() { m_renderstate = m_defaultRenderState; }
   RenderState& GetRenderState() { return m_renderstate; }
   void SetRenderState(const RenderState::RenderStates p1, const RenderState::RenderStateValue p2);
   void SetRenderStateDepthBias(float bias);
   void CopyRenderStates(const bool copyTo, RenderState& state);
   void CopyRenderAndShaderStates(const bool copyTo, RenderDeviceState& state);
   void EnableAlphaBlend(const bool additiveBlending, const bool set_dest_blend = true, const bool set_blend_op = true);

   ////////////////////////////////////////////////////////////////////////////////////////////////
   // (live) RenderDevice state and operation API

   void Flip();
   void WaitForVSync(const bool asynchronous);
   float GetVisualLatency() const; // Average delay between when the frame is prepared and when it will be viewed by the player (including TV/display/headset latency)
   float GetPredictedDisplayDelay() const; // Delay between now (when called) and when the frame will be viewed by the player (including TV/display/headset latency)
   unsigned int GetTargetFrameLength() const; // Target frame length in microseconds

   RenderTarget* GetOutputBackBuffer() const { return m_outputWnd[0]->GetBackBuffer(); } // The screen render target (the only one which is not stereo when doing stereo rendering)

   bool DepthBufferReadBackAvailable() const;
   bool SupportLayeredRendering() const
   {
      return bgfx::getCaps()->supported & (BGFX_CAPS_INSTANCING | BGFX_CAPS_TEXTURE_2D_ARRAY | BGFX_CAPS_VIEWPORT_LAYER_ARRAY);
   }

   std::shared_ptr<MeshBuffer> GetQuadMeshBuffer() const { return m_quadMeshBuffer; }

   void SetClipPlane(const vec4& plane);

   // Active (live on GPU) render state
   void ApplyRenderStates();
   RenderState& GetActiveRenderState() { return m_current_renderstate; }

   void UploadTexture(ITexManCacheable* texture, const bool linearRGB);
   void SetSamplerState(int unit, SamplerFilter filter, SamplerAddressMode clamp_u, SamplerAddressMode clamp_v);
   std::shared_ptr<Sampler> m_nullTexture = nullptr;
   TextureManager m_texMan;
   const bool m_compressTextures;

   bool UseLowPrecision() const { return m_useLowPrecision; }

   unsigned int m_vsyncCount = 0;

   vector<std::shared_ptr<SharedIndexBuffer>> m_pendingSharedIndexBuffers;
   vector<std::shared_ptr<SharedVertexBuffer>> m_pendingSharedVertexBuffers;

   bool m_framePending = false;

   const int m_nEyes;
   Shader* m_uiShader = nullptr;
   Shader* m_basicShader = nullptr;
   Shader *m_DMDShader = nullptr;
   Shader *m_FBShader = nullptr;
   Shader *m_flasherShader = nullptr;
   Shader *m_lightShader = nullptr;
   Shader *m_stereoShader = nullptr;
   Shader *m_ballShader = nullptr;

   // performance counters
   unsigned int Perf_GetNumDrawCalls() const        { return m_frameDrawCalls; }
   unsigned int Perf_GetNumStateChanges() const     { return m_frameStateChanges; }
   unsigned int Perf_GetNumTextureChanges() const   { return m_frameTextureChanges; }
   unsigned int Perf_GetNumParameterChanges() const { return m_frameParameterChanges; }
   unsigned int Perf_GetNumTechniqueChanges() const { return m_frameTechniqueChanges; }
   unsigned int Perf_GetNumTextureUploads() const   { return m_frameTextureUpdates; }
   unsigned int Perf_GetNumLockCalls() const        { return m_frameLockCalls; }
   unsigned int m_curDrawCalls = 0, m_frameDrawCalls = 0;
   unsigned int m_curStateChanges = 0, m_frameStateChanges = 0;
   unsigned int m_curTextureChanges = 0, m_frameTextureChanges = 0;
   unsigned int m_curParameterChanges = 0, m_frameParameterChanges = 0;
   unsigned int m_curTechniqueChanges = 0, m_frameTechniqueChanges = 0;
   unsigned int m_curTextureUpdates = 0, m_frameTextureUpdates = 0;
   unsigned int m_curLockCalls = 0, m_frameLockCalls = 0;
   unsigned int m_curDrawnTriangles = 0, m_frameDrawnTriangles = 0;

   uint64_t m_lastPresentFrameTick = 0;

   // Swap chain always has at least one output window (OpenGL & DX9 only supports one, DX10+/Metal/Vulkan support multiple)
   vector<VPX::Window*> m_outputWnd;

   void CaptureScreenshot(const vector<VPX::Window*>& wnd, const vector<std::filesystem::path>& filename, const std::function<void(bool)>& callback, int frameDelay = 3);

   string m_GPU_name;
   string m_driver_name;

   bool m_noMovingBalls = false;

private:
   const bool m_isAnaglyph;
   const bool m_isVR;

   bool m_useLowPrecision = false; // OpenGL ES use low precision float and needs some clamping to avoid artifacts, but the clamping causes artefacts if applied with VR scene scaling on other backends.

   std::unique_ptr<RenderFrame> m_renderFrame = nullptr;
   RenderPass* m_currentPass = nullptr;
   RenderPass* m_nextRenderCommandDependency = nullptr;

   RenderState m_current_renderstate, m_renderstate, m_defaultRenderState;
   bool m_logNextFrame = false; // Output a log of next frame to main application log


   std::shared_ptr<MeshBuffer> m_quadMeshBuffer; // internal mesh buffer for rendering quads

   void UploadAndSetSMAATextures();
   std::shared_ptr<Sampler> m_SMAAsearchTexture = nullptr;
   std::shared_ptr<Sampler> m_SMAAareaTexture = nullptr;

   int m_screenshotFrameDelay = 0;
   bool m_screenshotSuccess = true;
   vector<VPX::Window*> m_screenshotWindow;
   vector<std::filesystem::path> m_screenshotFilename;
   std::function<void(bool)> m_screenshotCallback = [](bool) { };

   uint64_t m_presentTimestampReference = 0;

public:
   void NextView();
   void ResetActiveView();

   bgfx::ProgramHandle m_program = BGFX_INVALID_HANDLE; // Bound program for next draw submission
   bgfx::VertexLayout* m_pVertexTexelDeclaration = nullptr;
   bgfx::VertexLayout* m_pVertexNormalTexelDeclaration = nullptr;
   bgfx::ViewId m_activeViewId = 0;
   uint64_t m_bgfxState = 0;

   bool m_frameNoPresent = false; // Flag set when the next frame should be submitted without VBlank sync disabled
   std::binary_semaphore m_rendererInitialized { 0 }; // Semaphore to signal when the renderer is initialized
   std::binary_semaphore m_frameReadySem { 0 }; // Semaphore to signal when a frame is ready to be submitted
   std::mutex m_frameMutex; // Mutex to lock acces to retained render frame between logic thread and render thread

   std::vector<bgfx::ProgramHandle> m_mipmapPrograms;

   uint64_t m_lastGPUFrameLength = 0;

private:
   void SubmitAndFlipFrame(bool present);
   bgfx::TextureFormat::Enum SelectBackBufferFormat(const VPX::Window* wnd, bgfx::TextureFormat::Enum defaultFormat, bool isWCG) const;
   static colorFormat BGFXtoVPXTextureFormat(bgfx::TextureFormat::Enum format);
   static void RenderThread(RenderDevice* rd, bgfx::Init init);
   void BGFXDesktopRenderLoop(const bgfx::Init& init);

   uint32_t m_frameIndex = 0;

   uint32_t m_lastPresentFrameIdx = 0;
   float m_renderLatency = 0.f;

   bool m_renderDeviceAlive;
   std::thread m_renderThread;
   vector<std::shared_ptr<Sampler>> m_pendingTextureUploads;
   std::unique_ptr<ShaderState> m_uniformState = nullptr;

   class tBGFXCallback : public bgfx::CallbackI
   {
   public:
      tBGFXCallback(RenderDevice& rd) : bgfx::CallbackI(), m_rd(rd) { }
      ~tBGFXCallback() override { }
      void fatal(const char* _filePath, uint16_t _line, bgfx::Fatal::Enum _code, const char* _str) override;
      void traceVargs(const char* _filePath, uint16_t _line, const char* _format, va_list _argList) override;
      void profilerBegin(const char* /*_name*/, uint32_t /*_abgr*/, const char* /*_filePath*/, uint16_t /*_line*/) override { }
      void profilerBeginLiteral(const char* /*_name*/, uint32_t /*_abgr*/, const char* /*_filePath*/, uint16_t /*_line*/) override { }
      void profilerEnd() override { }
      uint32_t cacheReadSize(uint64_t /*_id*/) override { return 0; }
      bool cacheRead(uint64_t /*_id*/, void* /*_data*/, uint32_t /*_size*/) override { return false; }
      void cacheWrite(uint64_t /*_id*/, const void* /*_data*/, uint32_t /*_size*/) override { }
      void screenShot(const char* _filePath, uint32_t _width, uint32_t _height, uint32_t _pitch, bgfx::TextureFormat::Enum _format, const void* _data, uint32_t _size, bool _yflip) override;
      void captureBegin(uint32_t /*_width*/, uint32_t /*_height*/, uint32_t /*_pitch*/, bgfx::TextureFormat::Enum /*_format*/, bool /*_yflip*/) override { }
      void captureEnd() override { }
      void captureFrame(const void* /*_data*/, uint32_t /*_size*/) override { }

   private:
      RenderDevice& m_rd;
   } m_bgfxCallback;
};
