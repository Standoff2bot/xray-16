# 📋 **Phase 1: Core Systems - Granular Implementation Checklist**

## 🎯 **Overview**

**Duration**: 9 weeks (45 working days)  
**Goal**: Build foundational systems for modern renderer  

**Three Major Components**:
1. **RenderContext** (Weeks 4-7): Command recording API
2. **FrameGraph** (Weeks 8-10): Dependency graph system
3. **Resource Manager Foundation** (Weeks 11-12): Modern resource management

**Final Deliverable**: Render a textured triangle using FrameGraph + RenderContext + ResourceManager

---

## 📅 **Week 4-7: RenderContext Implementation**

### **Week 4 Overview**

**Goal**: Create basic RenderContext interface and state management  
**Deliverable**: Draw solid color triangle

---

### **Day 16-17: RenderContext Interface Design (4-6 hours)**

#### **Tasks**

- [ ] **16.1**: Create RenderContext directory structure
  ```bash
  cd Layers/xrRenderPC_R4
  mkdir -p RenderContext
  ```

- [ ] **16.2**: Design RenderContext.h interface

  Create: `Layers/xrRenderPC_R4/RenderContext/RenderContext.h`
  
  ```cpp
  #pragma once
  
  namespace xray::render::ng {
  
  // Forward declarations
  class ResourceManager;
  
  // Handle types (will be defined later)
  struct PipelineStateHandle {
      u32 index = 0;
      bool IsValid() const { return index != 0; }
  };
  
  struct BufferHandle {
      u32 index = 0;
      bool IsValid() const { return index != 0; }
  };
  
  struct TextureHandle {
      u32 index = 0;
      bool IsValid() const { return index != 0; }
  };
  
  // Render pass description
  struct RenderPassDesc {
      nvrhi::TextureHandle renderTargets[8] = {};
      u32 numRenderTargets = 0;
      nvrhi::TextureHandle depthStencil = nullptr;
      
      struct ClearValue {
          float color[4] = {0, 0, 0, 0};
          float depth = 1.0f;
          u8 stencil = 0;
      } clearValue;
      
      bool clearColor = false;
      bool clearDepth = false;
      bool clearStencil = false;
  };
  
  // Viewport
  struct Viewport {
      float x = 0;
      float y = 0;
      float width = 0;
      float height = 0;
      float minDepth = 0.0f;
      float maxDepth = 1.0f;
  };
  
  // Scissor rect
  struct Rect {
      i32 x = 0;
      i32 y = 0;
      u32 width = 0;
      u32 height = 0;
  };
  
  /**
   * Modern command recording API
   * 
   * Design principles:
   * - Explicit state management
   * - Minimal overhead
   * - Clear, readable API
   * - Future-proof for DX12/Vulkan
   */
  class RenderContext {
  public:
      RenderContext(nvrhi::IDevice* device, nvrhi::ICommandList* commandList);
      ~RenderContext();
      
      // ═══════════════════════════════════════════════════════
      //  RENDER PASS MANAGEMENT
      // ═══════════════════════════════════════════════════════
      
      void BeginRenderPass(const RenderPassDesc& desc);
      void EndRenderPass();
      
      // ═══════════════════════════════════════════════════════
      //  PIPELINE STATE
      // ═══════════════════════════════════════════════════════
      
      void SetPipeline(PipelineStateHandle pso);
      void SetPipeline(nvrhi::IGraphicsPipeline* pipeline);  // Direct NVRHI (temporary)
      
      // ═══════════════════════════════════════════════════════
      //  VIEWPORT & SCISSOR
      // ═══════════════════════════════════════════════════════
      
      void SetViewport(const Viewport& viewport);
      void SetViewport(float x, float y, float width, float height);
      void SetScissor(const Rect& scissor);
      
      // ═══════════════════════════════════════════════════════
      //  VERTEX & INDEX BUFFERS
      // ═══════════════════════════════════════════════════════
      
      void SetVertexBuffer(u32 slot, BufferHandle buffer, u64 offset = 0);
      void SetVertexBuffer(u32 slot, nvrhi::IBuffer* buffer, u64 offset = 0);  // Direct
      
      void SetIndexBuffer(BufferHandle buffer, nvrhi::Format format, u64 offset = 0);
      void SetIndexBuffer(nvrhi::IBuffer* buffer, nvrhi::Format format, u64 offset = 0);
      
      // ═══════════════════════════════════════════════════════
      //  TEXTURES & SAMPLERS
      // ═══════════════════════════════════════════════════════
      
      void SetTexture(u32 slot, TextureHandle texture);
      void SetTexture(u32 slot, nvrhi::ITexture* texture);  // Direct
      
      void SetSampler(u32 slot, nvrhi::ISampler* sampler);
      
      // ═══════════════════════════════════════════════════════
      //  CONSTANT BUFFERS
      // ═══════════════════════════════════════════════════════
      
      void SetConstantBuffer(u32 slot, BufferHandle buffer);
      void SetConstantBuffer(u32 slot, nvrhi::IBuffer* buffer);  // Direct
      
      // ═══════════════════════════════════════════════════════
      //  DRAW CALLS
      // ═══════════════════════════════════════════════════════
      
      void Draw(u32 vertexCount, u32 startVertex = 0);
      void DrawIndexed(u32 indexCount, u32 startIndex = 0, i32 baseVertex = 0);
      void DrawInstanced(u32 vertexCount, u32 instanceCount, 
                        u32 startVertex = 0, u32 startInstance = 0);
      void DrawIndexedInstanced(u32 indexCount, u32 instanceCount,
                               u32 startIndex = 0, i32 baseVertex = 0,
                               u32 startInstance = 0);
      
      // ═══════════════════════════════════════════════════════
      //  CLEAR OPERATIONS
      // ═══════════════════════════════════════════════════════
      
      void ClearRenderTarget(nvrhi::ITexture* rt, const float color[4]);
      void ClearDepthStencil(nvrhi::ITexture* ds, float depth, u8 stencil);
      
      // ═══════════════════════════════════════════════════════
      //  STATE QUERY
      // ═══════════════════════════════════════════════════════
      
      bool IsInRenderPass() const { return m_inRenderPass; }
      
      // ═══════════════════════════════════════════════════════
      //  INTERNAL
      // ═══════════════════════════════════════════════════════
      
      nvrhi::ICommandList* GetCommandList() const { return m_commandList; }
      
  private:
      nvrhi::IDevice* m_device;
      nvrhi::ICommandList* m_commandList;
      ResourceManager* m_resourceManager = nullptr;  // Set later
      
      // State tracking
      bool m_inRenderPass = false;
      RenderPassDesc m_currentRenderPass;
      
      // Prevent copying
      RenderContext(const RenderContext&) = delete;
      RenderContext& operator=(const RenderContext&) = delete;
  };
  
  } // namespace xray::render::ng
  ```

- [ ] **16.3**: Review interface design
  - [ ] All common operations covered
  - [ ] Clear ownership semantics
  - [ ] Consistent naming conventions
  - [ ] Easy to use
  - [ ] Future-proof

- [ ] **16.4**: Commit interface
  ```bash
  git add Layers/xrRenderPC_R4/RenderContext/RenderContext.h
  git commit -m "Add RenderContext interface design"
  ```

#### **Success Criteria**
- [ ] RenderContext.h created with clean API
- [ ] All major rendering operations covered
- [ ] Code compiles (header only, no implementation yet)

---

### **Day 18-19: RenderContext Implementation - Core (6-8 hours)**

#### **Tasks**

- [ ] **18.1**: Create RenderContext.cpp

  Create: `Layers/xrRenderPC_R4/RenderContext/RenderContext.cpp`
  
  ```cpp
  #include "stdafx.h"
  #include "RenderContext.h"
  
  namespace xray::render::ng {
  
  RenderContext::RenderContext(nvrhi::IDevice* device, 
                               nvrhi::ICommandList* commandList)
      : m_device(device)
      , m_commandList(commandList)
  {
      VERIFY(m_device != nullptr);
      VERIFY(m_commandList != nullptr);
      
      Msg("~ [RenderContext] Created");
  }
  
  RenderContext::~RenderContext() {
      if (m_inRenderPass) {
          Msg("! [RenderContext] Destroying while in render pass - forcing end");
          EndRenderPass();
      }
      
      Msg("~ [RenderContext] Destroyed");
  }
  
  // ═══════════════════════════════════════════════════════
  //  RENDER PASS MANAGEMENT
  // ═══════════════════════════════════════════════════════
  
  void RenderContext::BeginRenderPass(const RenderPassDesc& desc) {
      VERIFY(!m_inRenderPass && "Already in render pass!");
      
      // Store current render pass
      m_currentRenderPass = desc;
      
      // Build NVRHI framebuffer descriptor
      nvrhi::FramebufferDesc fbDesc;
      
      for (u32 i = 0; i < desc.numRenderTargets; i++) {
          fbDesc.addColorAttachment(desc.renderTargets[i]);
      }
      
      if (desc.depthStencil) {
          fbDesc.setDepthAttachment(desc.depthStencil);
      }
      
      // Create framebuffer (NVRHI caches these)
      nvrhi::FramebufferHandle framebuffer = m_device->createFramebuffer(fbDesc);
      
      if (!framebuffer) {
          Msg("! [RenderContext] Failed to create framebuffer");
          return;
      }
      
      // Begin render pass in command list
      nvrhi::RenderState renderState;
      // Will set viewport, pipeline state, etc. later
      
      m_commandList->beginMarker("RenderPass");  // Debug marker
      m_commandList->setFramebuffer(framebuffer);
      
      // Clear if requested
      if (desc.clearColor) {
          for (u32 i = 0; i < desc.numRenderTargets; i++) {
              m_commandList->clearTextureFloat(
                  desc.renderTargets[i],
                  nvrhi::AllSubresources,
                  nvrhi::Color(desc.clearValue.color)
              );
          }
      }
      
      if (desc.clearDepth || desc.clearStencil) {
          if (desc.depthStencil) {
              m_commandList->clearDepthStencilTexture(
                  desc.depthStencil,
                  nvrhi::AllSubresources,
                  desc.clearDepth,
                  desc.clearValue.depth,
                  desc.clearStencil,
                  desc.clearValue.stencil
              );
          }
      }
      
      m_inRenderPass = true;
  }
  
  void RenderContext::EndRenderPass() {
      VERIFY(m_inRenderPass && "Not in render pass!");
      
      m_commandList->endMarker();  // End debug marker
      
      m_inRenderPass = false;
  }
  
  // ═══════════════════════════════════════════════════════
  //  PIPELINE STATE
  // ═══════════════════════════════════════════════════════
  
  void RenderContext::SetPipeline(nvrhi::IGraphicsPipeline* pipeline) {
      VERIFY(m_inRenderPass && "Must be in render pass!");
      VERIFY(pipeline != nullptr);
      
      nvrhi::GraphicsState state;
      state.pipeline = pipeline;
      state.framebuffer = m_commandList->getCurrentFramebuffer();
      
      m_commandList->setGraphicsState(state);
  }
  
  void RenderContext::SetPipeline(PipelineStateHandle pso) {
      // TODO: Implement once we have pipeline state cache
      VERIFY(false && "Pipeline handle support not yet implemented");
  }
  
  // ═══════════════════════════════════════════════════════
  //  VIEWPORT & SCISSOR
  // ═══════════════════════════════════════════════════════
  
  void RenderContext::SetViewport(const Viewport& viewport) {
      VERIFY(m_inRenderPass && "Must be in render pass!");
      
      nvrhi::Viewport nvrhiViewport;
      nvrhiViewport.minX = viewport.x;
      nvrhiViewport.maxX = viewport.x + viewport.width;
      nvrhiViewport.minY = viewport.y;
      nvrhiViewport.maxY = viewport.y + viewport.height;
      nvrhiViewport.minZ = viewport.minDepth;
      nvrhiViewport.maxZ = viewport.maxDepth;
      
      m_commandList->setViewport(nvrhiViewport);
  }
  
  void RenderContext::SetViewport(float x, float y, float width, float height) {
      Viewport vp;
      vp.x = x;
      vp.y = y;
      vp.width = width;
      vp.height = height;
      SetViewport(vp);
  }
  
  void RenderContext::SetScissor(const Rect& scissor) {
      VERIFY(m_inRenderPass && "Must be in render pass!");
      
      nvrhi::Rect nvrhiRect;
      nvrhiRect.minX = scissor.x;
      nvrhiRect.minY = scissor.y;
      nvrhiRect.maxX = scissor.x + scissor.width;
      nvrhiRect.maxY = scissor.y + scissor.height;
      
      m_commandList->setScissorRect(nvrhiRect);
  }
  
  // ═══════════════════════════════════════════════════════
  //  VERTEX & INDEX BUFFERS
  // ═══════════════════════════════════════════════════════
  
  void RenderContext::SetVertexBuffer(u32 slot, nvrhi::IBuffer* buffer, u64 offset) {
      VERIFY(m_inRenderPass && "Must be in render pass!");
      VERIFY(buffer != nullptr);
      
      nvrhi::VertexBufferBinding binding;
      binding.buffer = buffer;
      binding.slot = slot;
      binding.offset = offset;
      
      m_commandList->setVertexBuffer(slot, buffer, offset);
  }
  
  void RenderContext::SetVertexBuffer(u32 slot, BufferHandle buffer, u64 offset) {
      // TODO: Implement once we have buffer manager
      VERIFY(false && "Buffer handle support not yet implemented");
  }
  
  void RenderContext::SetIndexBuffer(nvrhi::IBuffer* buffer, 
                                     nvrhi::Format format, 
                                     u64 offset) {
      VERIFY(m_inRenderPass && "Must be in render pass!");
      VERIFY(buffer != nullptr);
      
      m_commandList->setIndexBuffer(buffer, format, offset);
  }
  
  void RenderContext::SetIndexBuffer(BufferHandle buffer, 
                                     nvrhi::Format format, 
                                     u64 offset) {
      // TODO: Implement once we have buffer manager
      VERIFY(false && "Buffer handle support not yet implemented");
  }
  
  // ═══════════════════════════════════════════════════════
  //  TEXTURES & SAMPLERS
  // ═══════════════════════════════════════════════════════
  
  void RenderContext::SetTexture(u32 slot, nvrhi::ITexture* texture) {
      VERIFY(m_inRenderPass && "Must be in render pass!");
      
      // TODO: Bind via descriptor sets once we have binding system
      // For now, just store and apply before draw
  }
  
  void RenderContext::SetTexture(u32 slot, TextureHandle texture) {
      // TODO: Implement once we have texture manager
      VERIFY(false && "Texture handle support not yet implemented");
  }
  
  void RenderContext::SetSampler(u32 slot, nvrhi::ISampler* sampler) {
      VERIFY(m_inRenderPass && "Must be in render pass!");
      
      // TODO: Bind via descriptor sets
  }
  
  // ═══════════════════════════════════════════════════════
  //  CONSTANT BUFFERS
  // ═══════════════════════════════════════════════════════
  
  void RenderContext::SetConstantBuffer(u32 slot, nvrhi::IBuffer* buffer) {
      VERIFY(m_inRenderPass && "Must be in render pass!");
      
      // TODO: Bind via descriptor sets
  }
  
  void RenderContext::SetConstantBuffer(u32 slot, BufferHandle buffer) {
      // TODO: Implement once we have buffer manager
      VERIFY(false && "Buffer handle support not yet implemented");
  }
  
  // ═══════════════════════════════════════════════════════
  //  DRAW CALLS
  // ═══════════════════════════════════════════════════════
  
  void RenderContext::Draw(u32 vertexCount, u32 startVertex) {
      VERIFY(m_inRenderPass && "Must be in render pass!");
      
      nvrhi::DrawArguments args;
      args.vertexCount = vertexCount;
      args.startVertexLocation = startVertex;
      
      m_commandList->draw(args);
  }
  
  void RenderContext::DrawIndexed(u32 indexCount, u32 startIndex, i32 baseVertex) {
      VERIFY(m_inRenderPass && "Must be in render pass!");
      
      nvrhi::DrawArguments args;
      args.vertexCount = indexCount;  // Actually index count for indexed draws
      args.startIndexLocation = startIndex;
      args.startVertexLocation = baseVertex;
      
      m_commandList->drawIndexed(args);
  }
  
  void RenderContext::DrawInstanced(u32 vertexCount, u32 instanceCount,
                                    u32 startVertex, u32 startInstance) {
      VERIFY(m_inRenderPass && "Must be in render pass!");
      
      nvrhi::DrawArguments args;
      args.vertexCount = vertexCount;
      args.instanceCount = instanceCount;
      args.startVertexLocation = startVertex;
      args.startInstanceLocation = startInstance;
      
      m_commandList->draw(args);
  }
  
  void RenderContext::DrawIndexedInstanced(u32 indexCount, u32 instanceCount,
                                           u32 startIndex, i32 baseVertex,
                                           u32 startInstance) {
      VERIFY(m_inRenderPass && "Must be in render pass!");
      
      nvrhi::DrawArguments args;
      args.vertexCount = indexCount;
      args.instanceCount = instanceCount;
      args.startIndexLocation = startIndex;
      args.startVertexLocation = baseVertex;
      args.startInstanceLocation = startInstance;
      
      m_commandList->drawIndexed(args);
  }
  
  // ═══════════════════════════════════════════════════════
  //  CLEAR OPERATIONS
  // ═══════════════════════════════════════════════════════
  
  void RenderContext::ClearRenderTarget(nvrhi::ITexture* rt, const float color[4]) {
      VERIFY(rt != nullptr);
      
      nvrhi::Color clearColor(color[0], color[1], color[2], color[3]);
      m_commandList->clearTextureFloat(rt, nvrhi::AllSubresources, clearColor);
  }
  
  void RenderContext::ClearDepthStencil(nvrhi::ITexture* ds, float depth, u8 stencil) {
      VERIFY(ds != nullptr);
      
      m_commandList->clearDepthStencilTexture(
          ds, 
          nvrhi::AllSubresources,
          true,  // Clear depth
          depth,
          true,  // Clear stencil
          stencil
      );
  }
  
  } // namespace xray::render::ng
  ```

- [ ] **18.2**: Add to CMakeLists.txt
  ```cmake
  target_sources(xrRenderPC_R4 PRIVATE
      # ... existing ...
      
      # RenderContext
      RenderContext/RenderContext.h
      RenderContext/RenderContext.cpp
  )
  ```

- [ ] **18.3**: Test compile
  ```bash
  cmake --build build --target xrRenderPC_R4 --config Debug
  ```
  - [ ] RenderContext.cpp compiles without errors
  - [ ] No linker errors

- [ ] **18.4**: Commit implementation
  ```bash
  git add Layers/xrRenderPC_R4/RenderContext/
  git add Layers/xrRenderPC_R4/CMakeLists.txt
  git commit -m "Implement RenderContext core functionality"
  ```

#### **Success Criteria**
- [ ] RenderContext.cpp implemented
- [ ] All basic operations work
- [ ] Compiles without errors
- [ ] State validation in place (VERIFY checks)

---

### **Day 20-21: Triangle Test with RenderContext (8-10 hours)**

#### **Tasks**

- [ ] **20.1**: Create simple vertex shader

  Create: `gamedata/shaders/r3/test_triangle.vs`
  
  ```hlsl
  // Simple pass-through vertex shader for testing
  
  struct VSInput {
      float3 position : POSITION;
      float4 color : COLOR;
  };
  
  struct VSOutput {
      float4 position : SV_Position;
      float4 color : COLOR;
  };
  
  VSOutput main(VSInput input) {
      VSOutput output;
      output.position = float4(input.position, 1.0);
      output.color = input.color;
      return output;
  }
  ```

- [ ] **20.2**: Create simple pixel shader

  Create: `gamedata/shaders/r3/test_triangle.ps`
  
  ```hlsl
  // Simple pass-through pixel shader for testing
  
  struct PSInput {
      float4 position : SV_Position;
      float4 color : COLOR;
  };
  
  float4 main(PSInput input) : SV_Target {
      return input.color;
  }
  ```

- [ ] **20.3**: Create triangle test function in CRender

  Add to CRender class:
  
  ```cpp
  // In xrRender_R4.h:
  class CRender : public IRenderer {
      // ... existing ...
      
  private:
      void TestRenderContext_Triangle();  // Triangle test
      
      // Test resources
      nvrhi::BufferHandle m_testVertexBuffer;
      nvrhi::BufferHandle m_testIndexBuffer;
      nvrhi::GraphicsPipelineHandle m_testPipeline;
      nvrhi::ShaderHandle m_testVS;
      nvrhi::ShaderHandle m_testPS;
      xray::render::ng::RenderContext* m_renderContext = nullptr;
  };
  
  // In xrRender_R4.cpp:
  void CRender::TestRenderContext_Triangle() {
      VERIFY(m_nvrhiDevice && m_nvrhiDevice->IsInitialized());
      
      using namespace xray::render::ng;
      
      nvrhi::IDevice* device = m_nvrhiDevice->GetDevice();
      nvrhi::ICommandList* cmd = m_nvrhiDevice->GetCommandList();
      
      // Create resources once
      if (!m_testVertexBuffer) {
          // Define triangle vertices (colored)
          struct Vertex {
              float pos[3];
              float color[4];
          };
          
          Vertex vertices[] = {
              // Position (x, y, z)     // Color (r, g, b, a)
              {{ 0.0f,  0.5f, 0.0f},   {1.0f, 0.0f, 0.0f, 1.0f}}, // Top (red)
              {{ 0.5f, -0.5f, 0.0f},   {0.0f, 1.0f, 0.0f, 1.0f}}, // Right (green)
              {{-0.5f, -0.5f, 0.0f},   {0.0f, 0.0f, 1.0f, 1.0f}}  // Left (blue)
          };
          
          // Create vertex buffer
          nvrhi::BufferDesc vbDesc;
          vbDesc.byteSize = sizeof(vertices);
          vbDesc.isVertexBuffer = true;
          vbDesc.debugName = "TestTriangle_VB";
          vbDesc.initialState = nvrhi::ResourceStates::VertexBuffer;
          
          m_testVertexBuffer = device->createBuffer(vbDesc);
          
          // Upload data
          cmd->beginMarker("UploadTriangleVB");
          cmd->writeBuffer(m_testVertexBuffer, vertices, sizeof(vertices));
          cmd->endMarker();
          
          Msg("~ [TestRenderContext] Created vertex buffer");
      }
      
      if (!m_testIndexBuffer) {
          // Define triangle indices
          u16 indices[] = {0, 1, 2};
          
          // Create index buffer
          nvrhi::BufferDesc ibDesc;
          ibDesc.byteSize = sizeof(indices);
          ibDesc.isIndexBuffer = true;
          ibDesc.debugName = "TestTriangle_IB";
          ibDesc.initialState = nvrhi::ResourceStates::IndexBuffer;
          
          m_testIndexBuffer = device->createBuffer(ibDesc);
          
          // Upload data
          cmd->beginMarker("UploadTriangleIB");
          cmd->writeBuffer(m_testIndexBuffer, indices, sizeof(indices));
          cmd->endMarker();
          
          Msg("~ [TestRenderContext] Created index buffer");
      }
      
      if (!m_testVS || !m_testPS) {
          // Compile shaders
          // For now, compile from file (we'll improve this in shader phase)
          std::vector<u8> vsBlob = CompileShaderFromFile(
              "gamedata/shaders/r3/test_triangle.vs", "main", "vs_5_0"
          );
          
          std::vector<u8> psBlob = CompileShaderFromFile(
              "gamedata/shaders/r3/test_triangle.ps", "main", "ps_5_0"
          );
          
          // Create shader objects
          nvrhi::ShaderDesc vsDesc;
          vsDesc.shaderType = nvrhi::ShaderType::Vertex;
          vsDesc.debugName = "TestTriangle_VS";
          
          m_testVS = device->createShader(vsDesc, vsBlob.data(), vsBlob.size());
          
          nvrhi::ShaderDesc psDesc;
          psDesc.shaderType = nvrhi::ShaderType::Pixel;
          psDesc.debugName = "TestTriangle_PS";
          
          m_testPS = device->createShader(psDesc, psBlob.data(), psBlob.size());
          
          Msg("~ [TestRenderContext] Compiled shaders");
      }
      
      if (!m_testPipeline) {
          // Create graphics pipeline
          nvrhi::GraphicsPipelineDesc pipelineDesc;
          
          // Shaders
          pipelineDesc.VS = m_testVS;
          pipelineDesc.PS = m_testPS;
          
          // Input layout
          nvrhi::VertexAttributeDesc attributes[] = {
              {
                  "POSITION",               // Semantic name
                  nvrhi::Format::RGB32_FLOAT, // Format
                  0,                        // Binding
                  0,                        // Offset
                  false                     // Per-instance
              },
              {
                  "COLOR",
                  nvrhi::Format::RGBA32_FLOAT,
                  0,
                  12,  // Offset after position (3 * 4 bytes)
                  false
              }
          };
          
          pipelineDesc.inputLayout = attributes;
          
          // Render state
          pipelineDesc.primType = nvrhi::PrimitiveType::TriangleList;
          
          pipelineDesc.renderState.depthStencilState.depthTestEnable = false;
          pipelineDesc.renderState.depthStencilState.depthWriteEnable = false;
          
          pipelineDesc.renderState.rasterState.fillMode = nvrhi::RasterFillMode::Solid;
          pipelineDesc.renderState.rasterState.cullMode = nvrhi::RasterCullMode::None;
          
          pipelineDesc.renderState.blendState.targets[0].enableBlend();
          
          m_testPipeline = device->createGraphicsPipeline(pipelineDesc, nullptr);
          
          Msg("~ [TestRenderContext] Created pipeline");
      }
      
      // Create RenderContext if needed
      if (!m_renderContext) {
          m_renderContext = xr_new<RenderContext>(device, cmd);
      }
      
      // === RENDER USING RENDERCONTEXT ===
      
      try {
          // Open command list
          cmd->open();
          
          // Get backbuffer
          ID3D11Resource* backbufferRes = nullptr;
          HW.pBaseRT->GetResource(&backbufferRes);
          
          nvrhi::TextureDesc backbufferDesc;
          backbufferDesc.width = Device.dwWidth;
          backbufferDesc.height = Device.dwHeight;
          backbufferDesc.format = nvrhi::Format::RGBA8_UNORM;
          backbufferDesc.isRenderTarget = true;
          backbufferDesc.debugName = "Backbuffer";
          
          nvrhi::TextureHandle backbuffer = device->createHandleForNativeTexture(
              nvrhi::ObjectTypes::D3D11_Resource,
              nvrhi::Object(backbufferRes),
              backbufferDesc
          );
          
          backbufferRes->Release();
          
          // Begin render pass
          RenderPassDesc passDesc;
          passDesc.renderTargets[0] = backbuffer;
          passDesc.numRenderTargets = 1;
          passDesc.clearColor = true;
          passDesc.clearValue.color[0] = 0.2f;  // Dark gray background
          passDesc.clearValue.color[1] = 0.2f;
          passDesc.clearValue.color[2] = 0.2f;
          passDesc.clearValue.color[3] = 1.0f;
          
          m_renderContext->BeginRenderPass(passDesc);
          
          // Set viewport
          m_renderContext->SetViewport(0, 0, 
                                      (float)Device.dwWidth, 
                                      (float)Device.dwHeight);
          
          // Set pipeline
          m_renderContext->SetPipeline(m_testPipeline.Get());
          
          // Set vertex buffer
          m_renderContext->SetVertexBuffer(0, m_testVertexBuffer.Get(), 0);
          
          // Set index buffer
          m_renderContext->SetIndexBuffer(m_testIndexBuffer.Get(), 
                                         nvrhi::Format::R16_UINT, 0);
          
          // Draw triangle!
          m_renderContext->DrawIndexed(3, 0, 0);
          
          // End render pass
          m_renderContext->EndRenderPass();
          
          // Close command list
          cmd->close();
          
          // Execute
          m_nvrhiDevice->ExecuteCommandList(cmd);
          
      } catch (const std::exception& e) {
          Msg("! [TestRenderContext] Exception: %s", e.what());
      }
  }
  ```

- [ ] **20.4**: Integrate into render loop

  In `CRender::Render()`:
  ```cpp
  void CRender::Render() {
      // Check test modes
      if (m_nvrhiTestMode && m_nvrhiDevice) {
          TestNVRHI_Render();  // Blue screen (Phase 0)
          HW.m_pSwapChain->Present(ps_r__VSync ? 1 : 0, 0);
          return;
      }
      
      // NEW: RenderContext triangle test
      if (m_renderContextTestMode && m_nvrhiDevice) {
          TestRenderContext_Triangle();
          HW.m_pSwapChain->Present(ps_r__VSync ? 1 : 0, 0);
          return;
      }
      
      // ... existing rendering ...
  }
  ```

- [ ] **20.5**: Add console command

  ```cpp
  class CCC_RenderContextTest : public IConsole_Command {
  public:
      virtual void Execute(LPCSTR args) override {
          if (!RImplementation.m_nvrhiDevice) {
              Msg("! [RenderContext] NVRHI not initialized");
              return;
          }
          
          RImplementation.m_renderContextTestMode = 
              !RImplementation.m_renderContextTestMode;
          
          if (RImplementation.m_renderContextTestMode) {
              Msg("~ [RenderContext] Test mode ENABLED - colored triangle");
          } else {
              Msg("~ [RenderContext] Test mode DISABLED");
          }
      }
  };
  
  // Register:
  Console->AddCommand(xr_new<CCC_RenderContextTest>("r4_rendercontext_test"));
  ```

- [ ] **20.6**: Test
  ```
  1. Launch game
  2. Open console
  3. Type: r4_rendercontext_test
  4. Expected: Colored triangle appears
  ```
  - [ ] Triangle renders with smooth color gradient
  - [ ] No crashes
  - [ ] Can toggle test mode

- [ ] **20.7**: Commit triangle test
  ```bash
  git add gamedata/shaders/r3/test_triangle.*
  git add Layers/xrRenderPC_R4/xrRender_R4.*
  git add Layers/xrRenderPC_R4/xrRender_console.cpp
  git commit -m "Add triangle test for RenderContext"
  ```

#### **Success Criteria**
- [ ] Colored triangle renders using RenderContext
- [ ] Console command works (`r4_rendercontext_test`)
- [ ] Clean API usage demonstrated
- [ ] No crashes or errors

---

### **Week 4 Summary**

**Completed**:
- [ ] RenderContext interface designed
- [ ] Core implementation complete
- [ ] Triangle test working

**Deliverable**: ✅ Draw colored triangle using RenderContext

---

## 📅 **Week 5-6: RenderContext Refinement**

### **Week 5 Overview**

**Goal**: Add descriptor sets, binding system, state caching  
**Deliverable**: Textured triangle

---

### **Day 22-23: Descriptor Set System (6-8 hours)**

#### **Tasks**

- [ ] **22.1**: Design binding layout system

  Add to RenderContext.h:
  
  ```cpp
  // Binding layout describes what resources a shader expects
  struct BindingLayoutDesc {
      struct Binding {
          u32 slot;
          nvrhi::ResourceType type;  // Texture, Buffer, Sampler, etc.
          nvrhi::ShaderType stages;  // Which shader stages use this
      };
      
      xr_vector<Binding> bindings;
      
      const char* debugName = nullptr;
  };
  
  class RenderContext {
      // ... existing ...
      
      // Create binding layout (cached by NVRHI)
      nvrhi::BindingLayoutHandle CreateBindingLayout(const BindingLayoutDesc& desc);
      
      // Create binding set from layout
      nvrhi::BindingSetHandle CreateBindingSet(
          nvrhi::BindingLayoutHandle layout,
          const nvrhi::BindingSetDesc& desc
      );
      
      // Bind descriptor set
      void SetBindingSet(u32 index, nvrhi::BindingSetHandle bindingSet);
  };
  ```

- [ ] **22.2**: Implement descriptor binding

  Add to RenderContext.cpp:
  
  ```cpp
  nvrhi::BindingLayoutHandle RenderContext::CreateBindingLayout(
      const BindingLayoutDesc& desc) {
      
      nvrhi::BindingLayoutDesc nvrhiDesc;
      nvrhiDesc.visibility = nvrhi::ShaderType::All;  // Or specific stages
      nvrhiDesc.registerSpace = 0;
      
      for (const auto& binding : desc.bindings) {
          nvrhi::BindingLayoutItem item;
          item.slot = binding.slot;
          item.type = binding.type;
          
          nvrhiDesc.bindings.push_back(item);
      }
      
      return m_device->createBindingLayout(nvrhiDesc);
  }
  
  nvrhi::BindingSetHandle RenderContext::CreateBindingSet(
      nvrhi::BindingLayoutHandle layout,
      const nvrhi::BindingSetDesc& desc) {
      
      return m_device->createBindingSet(desc, layout);
  }
  
  void RenderContext::SetBindingSet(u32 index, 
                                    nvrhi::BindingSetHandle bindingSet) {
      VERIFY(m_inRenderPass && "Must be in render pass!");
      
      nvrhi::GraphicsState state = m_commandList->getCurrentGraphicsState();
      state.bindings[index] = bindingSet;
      
      m_commandList->setGraphicsState(state);
  }
  ```

- [ ] **22.3**: Test compile
  ```bash
  cmake --build build --target xrRenderPC_R4 --config Debug
  ```

- [ ] **22.4**: Commit descriptor system
  ```bash
  git add Layers/xrRenderPC_R4/RenderContext/
  git commit -m "Add descriptor set binding system to RenderContext"
  ```

#### **Success Criteria**
- [ ] Binding layout system implemented
- [ ] Can create and bind descriptor sets
- [ ] Compiles without errors

---

### **Day 24-25: Textured Triangle Test (8-10 hours)**

#### **Tasks**

- [ ] **24.1**: Create test texture

  Generate simple test texture in code:
  
  ```cpp
  nvrhi::TextureHandle CreateTestTexture(nvrhi::IDevice* device) {
      // Create 64x64 checkerboard texture
      const u32 size = 64;
      u32 pixels[size * size];
      
      for (u32 y = 0; y < size; y++) {
          for (u32 x = 0; x < size; x++) {
              bool checker = ((x / 8) + (y / 8)) % 2 == 0;
              pixels[y * size + x] = checker ? 0xFFFFFFFF : 0xFF0000FF;  // White or red
          }
      }
      
      nvrhi::TextureDesc desc;
      desc.width = size;
      desc.height = size;
      desc.mipLevels = 1;
      desc.format = nvrhi::Format::RGBA8_UNORM;
      desc.debugName = "TestCheckerboard";
      desc.initialState = nvrhi::ResourceStates::ShaderResource;
      
      nvrhi::TextureHandle texture = device->createTexture(desc);
      
      // Upload data
      nvrhi::ICommandList* cmd = ...; // Get command list
      cmd->writeTexture(texture, 0, 0, pixels, size * 4);
      
      return texture;
  }
  ```

- [ ] **24.2**: Update vertex shader for UVs

  Update `test_triangle.vs`:
  
  ```hlsl
  struct VSInput {
      float3 position : POSITION;
      float4 color : COLOR;
      float2 texcoord : TEXCOORD;  // NEW
  };
  
  struct VSOutput {
      float4 position : SV_Position;
      float4 color : COLOR;
      float2 texcoord : TEXCOORD;  // NEW
  };
  
  VSOutput main(VSInput input) {
      VSOutput output;
      output.position = float4(input.position, 1.0);
      output.color = input.color;
      output.texcoord = input.texcoord;  // Pass through
      return output;
  }
  ```

- [ ] **24.3**: Update pixel shader to sample texture

  Update `test_triangle.ps`:
  
  ```hlsl
  Texture2D<float4> t_texture : register(t0);
  SamplerState s_sampler : register(s0);
  
  struct PSInput {
      float4 position : SV_Position;
      float4 color : COLOR;
      float2 texcoord : TEXCOORD;
  };
  
  float4 main(PSInput input) : SV_Target {
      float4 texColor = t_texture.Sample(s_sampler, input.texcoord);
      return input.color * texColor;  // Modulate vertex color with texture
  }
  ```

- [ ] **24.4**: Update triangle vertices to include UVs

  ```cpp
  struct Vertex {
      float pos[3];
      float color[4];
      float texcoord[2];  // NEW
  };
  
  Vertex vertices[] = {
      // Position              // Color           // TexCoord
      {{ 0.0f,  0.5f, 0.0f},  {1,1,1,1},         {0.5f, 0.0f}}, // Top
      {{ 0.5f, -0.5f, 0.0f},  {1,1,1,1},         {1.0f, 1.0f}}, // Right
      {{-0.5f, -0.5f, 0.0f},  {1,1,1,1},         {0.0f, 1.0f}}  // Left
  };
  ```

- [ ] **24.5**: Create binding set for texture + sampler

  ```cpp
  // Create sampler
  nvrhi::SamplerDesc samplerDesc;
  samplerDesc.minFilter = true;  // Linear
  samplerDesc.magFilter = true;
  nvrhi::SamplerHandle sampler = device->createSampler(samplerDesc);
  
  // Create binding layout
  BindingLayoutDesc layoutDesc;
  layoutDesc.bindings = {
      { 0, nvrhi::ResourceType::Texture_SRV, nvrhi::ShaderType::Pixel },
      { 0, nvrhi::ResourceType::Sampler, nvrhi::ShaderType::Pixel }
  };
  
  nvrhi::BindingLayoutHandle layout = m_renderContext->CreateBindingLayout(layoutDesc);
  
  // Create binding set
  nvrhi::BindingSetDesc bindingDesc;
  bindingDesc.bindings = {
      nvrhi::BindingSetItem::Texture_SRV(0, testTexture),
      nvrhi::BindingSetItem::Sampler(0, sampler)
  };
  
  nvrhi::BindingSetHandle bindingSet = 
      m_renderContext->CreateBindingSet(layout, bindingDesc);
  
  // Bind before draw
  m_renderContext->SetBindingSet(0, bindingSet);
  ```

- [ ] **24.6**: Test textured triangle
  ```
  1. Launch game
  2. Type: r4_rendercontext_test
  3. Expected: Triangle with checkerboard texture
  ```
  - [ ] Texture renders correctly
  - [ ] No crashes
  - [ ] Clean visual result

- [ ] **24.7**: Commit textured triangle
  ```bash
  git add gamedata/shaders/r3/test_triangle.*
  git add Layers/xrRenderPC_R4/xrRender_R4.cpp
  git commit -m "Add textured triangle test"
  ```

#### **Success Criteria**
- [ ] Textured triangle renders
- [ ] Descriptor binding system works
- [ ] Texture sampling functional

---

### **Day 26-27: State Caching (6-8 hours)**

#### **Tasks**

- [ ] **26.1**: Add state tracking to RenderContext

  ```cpp
  class RenderContext {
      // ... existing ...
      
  private:
      // State cache
      struct StateCache {
          nvrhi::IGraphicsPipeline* currentPipeline = nullptr;
          nvrhi::IBuffer* vertexBuffers[8] = {};
          u64 vertexBufferOffsets[8] = {};
          nvrhi::IBuffer* indexBuffer = nullptr;
          nvrhi::Format indexBufferFormat = nvrhi::Format::UNKNOWN;
          nvrhi::IBindingSet* bindingSets[4] = {};
          Viewport viewport;
          Rect scissor;
          bool viewportSet = false;
          bool scissorSet = false;
      } m_stateCache;
      
      // State management
      void InvalidateStateCache();
      bool IsPipelineDirty() const;
      bool IsVertexBufferDirty(u32 slot) const;
      // ... etc
  };
  ```

- [ ] **26.2**: Implement state caching logic

  ```cpp
  void RenderContext::SetPipeline(nvrhi::IGraphicsPipeline* pipeline) {
      VERIFY(m_inRenderPass && "Must be in render pass!");
      VERIFY(pipeline != nullptr);
      
      // Check if already bound
      if (m_stateCache.currentPipeline == pipeline) {
          return;  // Skip redundant state change
      }
      
      // Update cache
      m_stateCache.currentPipeline = pipeline;
      
      // Apply to command list
      nvrhi::GraphicsState state;
      state.pipeline = pipeline;
      state.framebuffer = m_commandList->getCurrentFramebuffer();
      m_commandList->setGraphicsState(state);
  }
  
  void RenderContext::SetVertexBuffer(u32 slot, nvrhi::IBuffer* buffer, u64 offset) {
      VERIFY(m_inRenderPass && "Must be in render pass!");
      VERIFY(buffer != nullptr);
      
      // Check if already bound
      if (m_stateCache.vertexBuffers[slot] == buffer &&
          m_stateCache.vertexBufferOffsets[slot] == offset) {
          return;  // Skip redundant state change
      }
      
      // Update cache
      m_stateCache.vertexBuffers[slot] = buffer;
      m_stateCache.vertexBufferOffsets[slot] = offset;
      
      // Apply to command list
      m_commandList->setVertexBuffer(slot, buffer, offset);
  }
  
  void RenderContext::InvalidateStateCache() {
      memset(&m_stateCache, 0, sizeof(m_stateCache));
  }
  ```

- [ ] **26.3**: Add statistics tracking

  ```cpp
  class RenderContext {
  public:
      struct Stats {
          u32 numDrawCalls = 0;
          u32 numTriangles = 0;
          u32 numStateChanges = 0;
          u32 numRedundantStateCalls = 0;  // Calls avoided by caching
      };
      
      const Stats& GetStats() const { return m_stats; }
      void ResetStats() { memset(&m_stats, 0, sizeof(m_stats)); }
      
  private:
      Stats m_stats;
  };
  
  // Update in draw calls:
  void RenderContext::DrawIndexed(u32 indexCount, u32 startIndex, i32 baseVertex) {
      // ... existing ...
      
      m_stats.numDrawCalls++;
      m_stats.numTriangles += indexCount / 3;  // Assuming triangle list
  }
  ```

- [ ] **26.4**: Test state caching
  - [ ] Redundant state calls skipped
  - [ ] Performance improves with caching

- [ ] **26.5**: Commit state caching
  ```bash
  git add Layers/xrRenderPC_R4/RenderContext/
  git commit -m "Add state caching and statistics to RenderContext"
  ```

#### **Success Criteria**
- [ ] State caching implemented
- [ ] Redundant state changes avoided
- [ ] Statistics tracking working

---

## 📊 **Week 4-7 Deliverables Checklist**

### **Code**
- [ ] RenderContext interface designed
- [ ] Core functionality implemented
- [ ] Descriptor binding system working
- [ ] State caching implemented
- [ ] Triangle test rendering
- [ ] Textured triangle rendering

### **Testing**
- [ ] Colored triangle renders correctly
- [ ] Textured triangle renders correctly
- [ ] Console commands work
- [ ] No crashes or errors
- [ ] State caching reduces redundant calls

### **Documentation**
- [ ] API documented with comments
- [ ] Usage examples in test code
- [ ] Week 4-7 summary created

---

## 🎯 **Weeks 8-10: FrameGraph Implementation**

*(Continue with FrameGraph detailed breakdown...)*

---

**Would you like me to continue with the complete 9-week breakdown (Weeks 8-12 covering FrameGraph and Resource Manager Foundation)?**

This is approximately 1/3 of Phase 1. The full document would be ~3x this length with all 9 weeks detailed.
