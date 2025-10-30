# 📋 **Phase 0: NVRHI Integration - Implementation Checklist**

## 🎯 **Overview**

**Duration**: 3 weeks (15 working days)  
**Goal**: Integrate NVRHI and prove it works by rendering a blue screen  
**Deliverable**: Console command `r4_nvrhi_test` toggles blue screen

---

## 📅 **Week 1: NVRHI Library Integration**

### **Day 1: Add NVRHI Submodule (2-4 hours)**

#### **Tasks**

- [ ] **1.1**: Create feature branch
  ```bash
  git checkout -b feature/nvrhi-integration
  ```

- [ ] **1.2**: Add NVRHI submodule
  ```bash
  cd <repo-root>
  mkdir -p External
  git submodule add https://github.com/NVIDIAGameWorks/nvrhi.git External/nvrhi
  cd External/nvrhi
  git submodule update --init --recursive
  ```
  
- [ ] **1.3**: Verify NVRHI structure
  ```
  External/nvrhi/
  ├── include/
  │   └── nvrhi/
  │       ├── nvrhi.h
  │       ├── d3d11.h
  │       └── ...
  ├── src/
  │   ├── common/
  │   ├── d3d11/
  │   └── ...
  └── CMakeLists.txt
  ```
  - [ ] `include/nvrhi/nvrhi.h` exists
  - [ ] `include/nvrhi/d3d11.h` exists
  - [ ] `src/d3d11/` directory exists
  - [ ] Root `CMakeLists.txt` exists

- [ ] **1.4**: Commit submodule addition
  ```bash
  git add .gitmodules External/nvrhi
  git commit -m "Add NVRHI as submodule for graphics abstraction"
  git push -u origin feature/nvrhi-integration
  ```

#### **Success Criteria**
- [ ] `External/nvrhi` directory populated with source
- [ ] No errors during submodule initialization
- [ ] `.gitmodules` file references NVRHI

---

### **Day 2: CMake Integration (3-5 hours)**

#### **Tasks**

- [ ] **2.1**: Locate xrRenderPC_R4 CMakeLists.txt
  ```
  Path: Layers/xrRenderPC_R4/CMakeLists.txt
  (or wherever your R4 renderer CMake file is)
  ```

- [ ] **2.2**: Add NVRHI to CMakeLists.txt (before target definition)
  
  Add this near the top of the file:
  ```cmake
  # Add NVRHI library
  set(NVRHI_BUILD_SHARED OFF CACHE BOOL "Build NVRHI as shared library")
  add_subdirectory(${CMAKE_SOURCE_DIR}/External/nvrhi ${CMAKE_BINARY_DIR}/nvrhi)
  ```

- [ ] **2.3**: Link NVRHI to xrRenderPC_R4 target
  
  Find the `target_link_libraries(xrRenderPC_R4 ...)` section and add:
  ```cmake
  target_link_libraries(xrRenderPC_R4 PRIVATE
      # ... existing libraries ...
      nvrhi           # Core NVRHI
      nvrhi-d3d11     # D3D11 backend
  )
  ```

- [ ] **2.4**: Add NVRHI include directories
  ```cmake
  target_include_directories(xrRenderPC_R4 PRIVATE
      # ... existing includes ...
      ${CMAKE_SOURCE_DIR}/External/nvrhi/include
  )
  ```

- [ ] **2.5**: Configure CMake
  ```bash
  cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug
  ```
  - [ ] No configuration errors
  - [ ] NVRHI targets appear in output

- [ ] **2.6**: Build NVRHI (test build)
  ```bash
  cmake --build build --target nvrhi --config Debug
  cmake --build build --target nvrhi-d3d11 --config Debug
  ```
  - [ ] `nvrhi.lib` created
  - [ ] `nvrhi-d3d11.lib` created
  - [ ] No build errors

- [ ] **2.7**: Commit CMake changes
  ```bash
  git add Layers/xrRenderPC_R4/CMakeLists.txt
  git commit -m "Integrate NVRHI into xrRenderPC_R4 build system"
  ```

#### **Success Criteria**
- [ ] CMake configuration succeeds
- [ ] NVRHI libraries build without errors
- [ ] xrRenderPC_R4 can find NVRHI headers

---

### **Day 3: Create Directory Structure & Update stdafx.h (1-2 hours)**

#### **Tasks**

- [ ] **3.1**: Create NVRHI wrapper directory
  ```bash
  cd Layers/xrRenderPC_R4
  mkdir NVRHI
  ```

- [ ] **3.2**: Update stdafx.h to include NVRHI headers
  
  Open `Layers/xrRenderPC_R4/stdafx.h` and add near the top:
  ```cpp
  // NVRHI includes (suppress warnings from external library)
  #pragma warning(push, 0)
  #include <nvrhi/nvrhi.h>
  #include <nvrhi/d3d11.h>
  #pragma warning(pop)
  ```

- [ ] **3.3**: Test compile stdafx.h
  ```bash
  cmake --build build --target xrRenderPC_R4 --config Debug
  ```
  - [ ] No errors about missing NVRHI headers
  - [ ] Precompiled header rebuilds successfully

- [ ] **3.4**: Commit directory structure
  ```bash
  git add Layers/xrRenderPC_R4/NVRHI/.gitkeep
  git add Layers/xrRenderPC_R4/stdafx.h
  git commit -m "Create NVRHI wrapper directory and add headers to stdafx"
  ```

#### **Success Criteria**
- [ ] `NVRHI/` directory exists
- [ ] stdafx.h includes NVRHI headers
- [ ] Project compiles with NVRHI includes

---

### **Day 4-5: Create NVRHIDevice Wrapper (6-8 hours)**

#### **Tasks**

- [ ] **4.1**: Create NVRHIDevice.h

  Create file: `Layers/xrRenderPC_R4/NVRHI/NVRHIDevice.h`
  
  ```cpp
  #pragma once
  
  namespace xray::render::r4::nvrhi_wrapper {
  
  class NVRHIDevice {
  public:
      NVRHIDevice();
      ~NVRHIDevice();
      
      // Initialize by wrapping existing D3D11 device
      bool Initialize(ID3D11Device* d3d11Device, 
                     ID3D11DeviceContext* d3d11Context);
      
      void Shutdown();
      
      bool IsInitialized() const { return m_device != nullptr; }
      
      nvrhi::IDevice* GetDevice() const { return m_device.Get(); }
      nvrhi::ICommandList* GetCommandList() const { return m_commandList.Get(); }
      
      void ExecuteCommandList(nvrhi::ICommandList* commandList);
      void WaitForIdle();
      
  private:
      nvrhi::DeviceHandle m_device;
      nvrhi::CommandListHandle m_commandList;
      bool m_initialized = false;
      
      // Prevent copying
      NVRHIDevice(const NVRHIDevice&) = delete;
      NVRHIDevice& operator=(const NVRHIDevice&) = delete;
  };
  
  } // namespace xray::render::r4::nvrhi_wrapper
  ```

- [ ] **4.2**: Create NVRHIDevice.cpp

  Create file: `Layers/xrRenderPC_R4/NVRHI/NVRHIDevice.cpp`
  
  ```cpp
  #include "stdafx.h"
  #include "NVRHIDevice.h"
  
  namespace xray::render::r4::nvrhi_wrapper {
  
  NVRHIDevice::NVRHIDevice() = default;
  
  NVRHIDevice::~NVRHIDevice() {
      Shutdown();
  }
  
  bool NVRHIDevice::Initialize(ID3D11Device* d3d11Device, 
                                ID3D11DeviceContext* d3d11Context) {
      VERIFY(d3d11Device != nullptr);
      VERIFY(d3d11Context != nullptr);
      
      if (m_initialized) {
          Msg("! [NVRHI] Already initialized");
          return false;
      }
      
      try {
          Msg("~ [NVRHI] Initializing device wrapper...");
          
          // Create NVRHI device descriptor
          nvrhi::d3d11::DeviceDesc deviceDesc;
          deviceDesc.device = d3d11Device;
          deviceDesc.context = d3d11Context;
          
          // Wrap existing device
          m_device = nvrhi::d3d11::createDevice(deviceDesc);
          
          if (!m_device) {
              Msg("! [NVRHI] Failed to create device wrapper");
              return false;
          }
          
          // Create immediate execution command list
          nvrhi::CommandListParameters cmdListParams;
          cmdListParams.enableImmediateExecution = true;
          
          m_commandList = m_device->createCommandList(cmdListParams);
          
          if (!m_commandList) {
              Msg("! [NVRHI] Failed to create command list");
              m_device = nullptr;
              return false;
          }
          
          m_initialized = true;
          
          // Log device info
          const nvrhi::DeviceDesc& desc = m_device->getDeviceDesc();
          Msg("~ [NVRHI] Device initialized");
          Msg("~   Vendor: %s", desc.vendorName.c_str());
          Msg("~   Adapter: %s", desc.adapterName.c_str());
          
          return true;
      }
      catch (const std::exception& e) {
          Msg("! [NVRHI] Exception during initialization: %s", e.what());
          return false;
      }
  }
  
  void NVRHIDevice::Shutdown() {
      if (!m_initialized) {
          return;
      }
      
      Msg("~ [NVRHI] Shutting down device wrapper...");
      
      WaitForIdle();
      
      m_commandList = nullptr;
      m_device = nullptr;
      
      m_initialized = false;
      
      Msg("~ [NVRHI] Device wrapper shutdown complete");
  }
  
  void NVRHIDevice::ExecuteCommandList(nvrhi::ICommandList* commandList) {
      VERIFY(m_initialized);
      VERIFY(commandList != nullptr);
      
      m_device->executeCommandList(commandList);
  }
  
  void NVRHIDevice::WaitForIdle() {
      if (m_device) {
          m_device->waitForIdle();
      }
  }
  
  } // namespace xray::render::r4::nvrhi_wrapper
  ```

- [ ] **4.3**: Add files to CMakeLists.txt
  
  In `Layers/xrRenderPC_R4/CMakeLists.txt`, add:
  ```cmake
  target_sources(xrRenderPC_R4 PRIVATE
      # ... existing files ...
      
      # NVRHI wrapper
      NVRHI/NVRHIDevice.h
      NVRHI/NVRHIDevice.cpp
  )
  ```

- [ ] **4.4**: Test compile
  ```bash
  cmake --build build --target xrRenderPC_R4 --config Debug
  ```
  - [ ] NVRHIDevice.cpp compiles without errors
  - [ ] No linker errors

- [ ] **4.5**: Commit wrapper implementation
  ```bash
  git add Layers/xrRenderPC_R4/NVRHI/
  git add Layers/xrRenderPC_R4/CMakeLists.txt
  git commit -m "Implement NVRHIDevice wrapper for existing D3D11 device"
  ```

#### **Success Criteria**
- [ ] NVRHIDevice.h created with proper interface
- [ ] NVRHIDevice.cpp implements all methods
- [ ] Project compiles without errors
- [ ] NVRHI links properly

---

## 📅 **Week 2: Integration into CRender**

### **Day 6: Add NVRHIDevice Member to CRender (2-3 hours)**

#### **Tasks**

- [ ] **6.1**: Locate CRender class definition
  ```
  Typical locations:
  - Layers/xrRenderPC_R4/xrRender_R4.h
  - Layers/xrRender/Render.h
  ```

- [ ] **6.2**: Add include for NVRHIDevice
  
  In the header file where CRender is defined:
  ```cpp
  #include "NVRHI/NVRHIDevice.h"
  ```

- [ ] **6.3**: Add member variables to CRender class
  ```cpp
  class CRender : public IRenderer {
      // ... existing members ...
      
  private:
      // NVRHI integration
      xray::render::r4::nvrhi_wrapper::NVRHIDevice* m_nvrhiDevice = nullptr;
      bool m_nvrhiTestMode = false;
  };
  ```

- [ ] **6.4**: Forward declare if needed (to avoid include in header)
  
  Alternative approach if you want to avoid including in header:
  ```cpp
  // In header:
  namespace xray::render::r4::nvrhi_wrapper {
      class NVRHIDevice;
  }
  
  class CRender : public IRenderer {
  private:
      xray::render::r4::nvrhi_wrapper::NVRHIDevice* m_nvrhiDevice;
      bool m_nvrhiTestMode;
  };
  
  // In .cpp:
  #include "NVRHI/NVRHIDevice.h"
  ```

- [ ] **6.5**: Test compile
  ```bash
  cmake --build build --target xrRenderPC_R4 --config Debug
  ```
  - [ ] No errors about undefined NVRHIDevice

- [ ] **6.6**: Commit header changes
  ```bash
  git add Layers/xrRenderPC_R4/xrRender_R4.h  # Or wherever CRender is
  git commit -m "Add NVRHIDevice member to CRender class"
  ```

#### **Success Criteria**
- [ ] CRender has NVRHIDevice pointer member
- [ ] CRender has test mode flag
- [ ] Project compiles

---

### **Day 7: Initialize NVRHI in CRender::Create (3-4 hours)**

#### **Tasks**

- [ ] **7.1**: Locate CRender::Create() method
  ```
  Typical location: Layers/xrRenderPC_R4/xrRender_R4.cpp
  Look for method: CRender::Create() or CRender::Initialize()
  ```

- [ ] **7.2**: Find where CHW device is created
  
  Look for code like:
  ```cpp
  if (!HW.CreateDevice(m_hWnd, ...)) {
      return false;
  }
  ```
  - [ ] Found HW device creation
  - [ ] Verified HW.pDevice and HW.pContext are valid after creation

- [ ] **7.3**: Add NVRHI initialization after HW device creation
  
  After successful HW.CreateDevice():
  ```cpp
  // Initialize CHW (existing code)
  if (!HW.CreateDevice(m_hWnd, ...)) {
      return false;
  }
  
  // NEW: Initialize NVRHI wrapper
  m_nvrhiDevice = xr_new<xray::render::r4::nvrhi_wrapper::NVRHIDevice>();
  
  bool nvrhiSuccess = m_nvrhiDevice->Initialize(HW.pDevice, HW.pContext);
  
  if (!nvrhiSuccess) {
      Msg("! [CRender] NVRHI initialization failed - modern path disabled");
      xr_delete(m_nvrhiDevice);
      m_nvrhiDevice = nullptr;
      // Continue without NVRHI (graceful degradation)
  } else {
      Msg("~ [CRender] NVRHI initialized - modern path available");
  }
  ```

- [ ] **7.4**: Verify initialization order
  - [ ] CHW device created first
  - [ ] HW.pDevice and HW.pContext are valid
  - [ ] NVRHI initialized second
  - [ ] Error handling in place

- [ ] **7.5**: Test build and run
  ```bash
  cmake --build build --target xrRenderPC_R4 --config Debug
  
  # Launch game
  # Check log for NVRHI messages
  ```
  - [ ] Game launches
  - [ ] Log shows: `[NVRHI] Device initialized`
  - [ ] Log shows device vendor/adapter info
  - [ ] No crashes

- [ ] **7.6**: Commit initialization code
  ```bash
  git add Layers/xrRenderPC_R4/xrRender_R4.cpp
  git commit -m "Initialize NVRHI device wrapper in CRender::Create"
  ```

#### **Success Criteria**
- [ ] NVRHI initializes after CHW device
- [ ] Game launches without crashes
- [ ] Log confirms NVRHI initialization
- [ ] Graceful fallback if initialization fails

---

### **Day 8: Cleanup NVRHI in CRender::Destroy (1-2 hours)**

#### **Tasks**

- [ ] **8.1**: Locate CRender::Destroy() method

- [ ] **8.2**: Add NVRHI cleanup BEFORE CHW cleanup
  ```cpp
  void CRender::Destroy() {
      // NEW: Cleanup NVRHI first
      if (m_nvrhiDevice) {
          m_nvrhiDevice->Shutdown();
          xr_delete(m_nvrhiDevice);
          m_nvrhiDevice = nullptr;
      }
      
      // ... existing CHW cleanup ...
      HW.DestroyDevice();
      
      // ... rest of cleanup ...
  }
  ```

- [ ] **8.3**: Verify cleanup order
  - [ ] NVRHI cleaned up before CHW
  - [ ] Prevents use-after-free (NVRHI wraps CHW device)

- [ ] **8.4**: Test shutdown
  ```bash
  # Run game, then exit
  # Check log for clean shutdown
  ```
  - [ ] Log shows: `[NVRHI] Device wrapper shutdown complete`
  - [ ] No crashes on exit
  - [ ] No memory leaks (check with debugger)

- [ ] **8.5**: Commit cleanup code
  ```bash
  git add Layers/xrRenderPC_R4/xrRender_R4.cpp
  git commit -m "Add NVRHI cleanup in CRender::Destroy"
  ```

#### **Success Criteria**
- [ ] NVRHI shuts down cleanly
- [ ] No crashes on exit
- [ ] No memory leaks

---

### **Day 9-10: Console Command Implementation (4-6 hours)**

#### **Tasks**

- [ ] **9.1**: Locate console command registration code
  ```
  Typical files:
  - Layers/xrRenderPC_R4/xrRender_console.cpp
  - Layers/xrRender/r_constants.cpp
  ```

- [ ] **9.2**: Create console command class
  
  Add to console command file:
  ```cpp
  class CCC_NVRHITest : public IConsole_Command {
  public:
      virtual void Execute(LPCSTR args) override {
          if (!RImplementation.m_nvrhiDevice) {
              Msg("! [NVRHI] Not initialized");
              return;
          }
          
          // Toggle test mode
          RImplementation.m_nvrhiTestMode = !RImplementation.m_nvrhiTestMode;
          
          if (RImplementation.m_nvrhiTestMode) {
              Msg("~ [NVRHI] Test mode ENABLED - will render blue screen");
          } else {
              Msg("~ [NVRHI] Test mode DISABLED - normal rendering");
          }
      }
  };
  ```

- [ ] **9.3**: Register console command
  
  In console initialization function:
  ```cpp
  void CRender::RegisterConsoleCommands() {
      // ... existing commands ...
      
      Console->AddCommand(xr_new<CCC_NVRHITest>("r4_nvrhi_test"));
  }
  ```

- [ ] **9.4**: Test console command
  ```bash
  # Run game
  # Open console (~)
  # Type: r4_nvrhi_test
  ```
  - [ ] Command executes
  - [ ] Log shows toggle message
  - [ ] No crashes

- [ ] **9.5**: Commit console command
  ```bash
  git add Layers/xrRenderPC_R4/xrRender_console.cpp
  git commit -m "Add r4_nvrhi_test console command"
  ```

#### **Success Criteria**
- [ ] Console command registered
- [ ] Command toggles test mode
- [ ] Proper error handling if NVRHI not initialized

---

## 📅 **Week 3: Test Render Implementation**

### **Day 11-12: Implement Blue Screen Render (6-8 hours)**

#### **Tasks**

- [ ] **11.1**: Create test render function

  Add to CRender class (in .cpp file):
  ```cpp
  void CRender::TestNVRHI_Render() {
      VERIFY(m_nvrhiDevice && m_nvrhiDevice->IsInitialized());
      
      try {
          // Get NVRHI device and command list
          nvrhi::IDevice* device = m_nvrhiDevice->GetDevice();
          nvrhi::ICommandList* cmd = m_nvrhiDevice->GetCommandList();
          
          // Get current backbuffer from CHW
          ID3D11RenderTargetView* backbufferRTV = HW.pBaseRT;
          if (!backbufferRTV) {
              Msg("! [NVRHI Test] No backbuffer RTV");
              return;
          }
          
          ID3D11Resource* backbufferRes = nullptr;
          backbufferRTV->GetResource(&backbufferRes);
          
          if (!backbufferRes) {
              Msg("! [NVRHI Test] Failed to get backbuffer resource");
              return;
          }
          
          // Wrap backbuffer in NVRHI texture handle
          nvrhi::TextureDesc backbufferDesc;
          backbufferDesc.width = Device.dwWidth;
          backbufferDesc.height = Device.dwHeight;
          backbufferDesc.format = nvrhi::Format::RGBA8_UNORM;
          backbufferDesc.isRenderTarget = true;
          backbufferDesc.isUAV = false;
          backbufferDesc.debugName = "Backbuffer";
          backbufferDesc.dimension = nvrhi::TextureDimension::Texture2D;
          backbufferDesc.keepInitialState = true;
          backbufferDesc.initialState = nvrhi::ResourceStates::RenderTarget;
          
          nvrhi::TextureHandle backbuffer = device->createHandleForNativeTexture(
              nvrhi::ObjectTypes::D3D11_Resource,
              nvrhi::Object(backbufferRes),
              backbufferDesc
          );
          
          // Release the resource reference
          backbufferRes->Release();
          
          if (!backbuffer) {
              Msg("! [NVRHI Test] Failed to wrap backbuffer");
              return;
          }
          
          // Open command list
          cmd->open();
          
          // Clear to blue (R=0.1, G=0.2, B=0.4, A=1.0)
          nvrhi::Color clearColor(0.1f, 0.2f, 0.4f, 1.0f);
          cmd->clearTextureFloat(backbuffer, nvrhi::AllSubresources, clearColor);
          
          // Close command list
          cmd->close();
          
          // Execute
          m_nvrhiDevice->ExecuteCommandList(cmd);
          
      } catch (const std::exception& e) {
          Msg("! [NVRHI Test] Exception: %s", e.what());
      }
  }
  ```

- [ ] **11.2**: Add declaration to header
  
  In CRender class header:
  ```cpp
  class CRender : public IRenderer {
      // ... existing ...
      
  private:
      void TestNVRHI_Render();  // Test render function
  };
  ```

- [ ] **11.3**: Integrate into main render loop

  Locate `CRender::Render()` method and add at the beginning:
  ```cpp
  void CRender::Render() {
      // Check if NVRHI test mode is active
      if (m_nvrhiTestMode && m_nvrhiDevice) {
          // Render blue screen using NVRHI
          TestNVRHI_Render();
          
          // Present (existing code)
          HW.m_pSwapChain->Present(ps_r__VSync ? 1 : 0, 0);
          
          return;  // Skip normal rendering
      }
      
      // ... existing rendering code ...
  }
  ```

- [ ] **11.4**: Test compile
  ```bash
  cmake --build build --target xrRenderPC_R4 --config Debug
  ```
  - [ ] No compile errors
  - [ ] No linker errors

- [ ] **11.5**: Commit test render implementation
  ```bash
  git add Layers/xrRenderPC_R4/xrRender_R4.h
  git add Layers/xrRenderPC_R4/xrRender_R4.cpp
  git commit -m "Implement NVRHI blue screen test render"
  ```

#### **Success Criteria**
- [ ] TestNVRHI_Render() function created
- [ ] Integrated into main render loop
- [ ] Code compiles

---

### **Day 13: Testing & Debugging (4-6 hours)**

#### **Tasks**

- [ ] **13.1**: Basic functionality test
  ```
  1. Launch game
  2. Check log for NVRHI initialization
  3. Open console (~)
  4. Type: r4_nvrhi_test
  5. Verify blue screen appears
  6. Type: r4_nvrhi_test again
  7. Verify normal rendering resumes
  ```
  - [ ] Blue screen appears when test enabled
  - [ ] Normal rendering when test disabled
  - [ ] Can toggle multiple times
  - [ ] No crashes

- [ ] **13.2**: Enable D3D11 debug layer (if available)
  ```
  In Windows SDK Control Panel:
  - Enable D3D11 debug layer
  - Set break on D3D11 errors
  
  Or programmatically in device creation:
  UINT createDeviceFlags = D3D11_CREATE_DEVICE_DEBUG;
  ```
  - [ ] No D3D11 errors reported
  - [ ] No warnings about leaked resources

- [ ] **13.3**: Check for common issues

  **If black screen instead of blue:**
  - [ ] Check backbuffer format (try BGRA8_UNORM if RGBA8_UNORM fails)
  - [ ] Verify clear color values (0.1, 0.2, 0.4, 1.0)
  - [ ] Check resource state (should be RenderTarget)
  
  **If crashes:**
  - [ ] Verify HW.pBaseRT is valid
  - [ ] Check backbuffer resource pointer before use
  - [ ] Ensure command list opened before clear
  
  **If D3D11 warnings:**
  - [ ] Clear any bound render targets before NVRHI test
  - [ ] Unbind shader resources before test

- [ ] **13.4**: Test edge cases
  - [ ] Toggle test mode during gameplay
  - [ ] Alt-Tab while in test mode
  - [ ] Resize window in test mode
  - [ ] Exit game while in test mode

- [ ] **13.5**: Performance check
  
  Add timing to test render:
  ```cpp
  void CRender::TestNVRHI_Render() {
      auto start = std::chrono::high_resolution_clock::now();
      
      // ... rendering code ...
      
      auto end = std::chrono::high_resolution_clock::now();
      auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
      
      static float avgTime = 0.0f;
      avgTime = avgTime * 0.95f + duration.count() * 0.05f;
      
      static u32 frameCount = 0;
      if (++frameCount % 60 == 0) {
          Msg("~ [NVRHI] Clear time: %.2f us", avgTime);
      }
  }
  ```
  - [ ] Clear operation < 100 microseconds
  - [ ] No measurable overhead

#### **Success Criteria**
- [ ] Blue screen renders correctly
- [ ] Can toggle test mode reliably
- [ ] No crashes or errors
- [ ] No D3D11 debug layer warnings
- [ ] Performance acceptable

---

### **Day 14: Documentation & Cleanup (2-3 hours)**

#### **Tasks**

- [ ] **14.1**: Document console commands
  
  Create/update file: `docs/console_commands.md`
  ```markdown
  ## r4_nvrhi_test
  
  **Purpose**: Toggle NVRHI test mode (blue screen render)
  
  **Usage**: 
  ```
  r4_nvrhi_test
  ```
  
  **Behavior**:
  - Toggles between NVRHI test render (blue screen) and normal rendering
  - Useful for verifying NVRHI integration
  - No effect if NVRHI failed to initialize
  
  **Expected Result**: Blue screen when enabled, normal rendering when disabled
  ```

- [ ] **14.2**: Add inline code comments

  Review all new code and add comments explaining:
  - [ ] Why NVRHI is initialized after CHW
  - [ ] Why cleanup happens before CHW cleanup
  - [ ] Purpose of immediate execution mode
  - [ ] Format assumptions for backbuffer

- [ ] **14.3**: Create Week 1 summary document
  
  Create: `docs/phase0_week1_summary.md`
  ```markdown
  # Phase 0 Week 1 Summary
  
  ## Completed
  - NVRHI added as submodule
  - CMake integration complete
  - NVRHIDevice wrapper implemented
  - Integrated into CRender lifecycle
  - Console command for testing
  - Blue screen test render working
  
  ## Files Changed
  - External/nvrhi (new submodule)
  - Layers/xrRenderPC_R4/CMakeLists.txt
  - Layers/xrRenderPC_R4/stdafx.h
  - Layers/xrRenderPC_R4/NVRHI/NVRHIDevice.h (new)
  - Layers/xrRenderPC_R4/NVRHI/NVRHIDevice.cpp (new)
  - Layers/xrRenderPC_R4/xrRender_R4.h
  - Layers/xrRenderPC_R4/xrRender_R4.cpp
  - Layers/xrRenderPC_R4/xrRender_console.cpp
  
  ## Success Criteria Met
  ✅ Blue screen renders via NVRHI
  ✅ Can toggle between NVRHI and normal renderer
  ✅ No crashes
  ✅ No memory leaks
  ✅ No D3D11 errors
  
  ## Known Issues
  (List any known issues)
  
  ## Next Steps
  - Week 2: Resource wrapping
  - Week 3: Triangle render test
  ```

- [ ] **14.4**: Code review checklist
  - [ ] All new code follows X-Ray naming conventions
  - [ ] Error handling in place (VERIFY, try/catch)
  - [ ] Logging at key points (Msg)
  - [ ] No magic numbers (use named constants)
  - [ ] Memory management correct (xr_new/xr_delete)
  - [ ] Thread safety considered (if applicable)

- [ ] **14.5**: Create pull request (if using PR workflow)
  ```bash
  git push origin feature/nvrhi-integration
  
  # Create PR with description:
  # - What: NVRHI integration (Phase 0 Week 1)
  # - Why: Foundation for modern renderer
  # - How: Wraps existing D3D11 device
  # - Testing: Blue screen test via r4_nvrhi_test command
  ```

#### **Success Criteria**
- [ ] Code documented
- [ ] Summary document created
- [ ] Code review checklist complete
- [ ] Ready for Week 2

---

### **Day 15: Final Validation & Week 2 Prep (2-3 hours)**

#### **Tasks**

- [ ] **15.1**: Final test suite
  ```
  Full test sequence:
  1. Clean build
  2. Launch game
  3. Verify NVRHI init in log
  4. Toggle test mode 5 times
  5. Play normally for 5 minutes
  6. Toggle test mode again
  7. Exit game
  8. Check for leaks/errors in log
  ```
  - [ ] All tests pass
  - [ ] No crashes
  - [ ] Clean log

- [ ] **15.2**: Performance baseline
  
  Record baseline metrics:
  - [ ] NVRHI initialization time: _____ ms
  - [ ] Clear operation time: _____ us
  - [ ] Frame time impact: _____ ms
  - [ ] Memory usage: _____ MB

- [ ] **15.3**: Create Week 2 task list
  
  Preview Week 2 tasks:
  - [ ] Wrap more D3D11 resources (textures, buffers)
  - [ ] Test resource creation/destruction
  - [ ] Begin RenderContext interface design
  - [ ] Implement triangle render test

- [ ] **15.4**: Final commit
  ```bash
  git add docs/
  git commit -m "Add documentation and finalize Phase 0 Week 1"
  git push origin feature/nvrhi-integration
  ```

#### **Success Criteria**
- [ ] All Week 1 deliverables complete
- [ ] Blue screen test working reliably
- [ ] Documentation complete
- [ ] Ready to proceed to Week 2

---

## 📊 **Week 1 Deliverables Checklist**

### **Code**
- [ ] NVRHI submodule added and initialized
- [ ] CMake integration complete
- [ ] NVRHIDevice wrapper implemented
- [ ] Integrated into CRender lifecycle
- [ ] Console command registered
- [ ] Blue screen test render working

### **Testing**
- [ ] Blue screen renders correctly
- [ ] Can toggle test mode
- [ ] No crashes
- [ ] No memory leaks
- [ ] No D3D11 debug errors
- [ ] Performance acceptable (<100us for clear)

### **Documentation**
- [ ] Console commands documented
- [ ] Code comments added
- [ ] Week 1 summary created
- [ ] Known issues documented

### **Process**
- [ ] All code committed to git
- [ ] Feature branch up to date
- [ ] Code review ready (if using PRs)
- [ ] Week 2 tasks identified

---

## 🔧 **Common Issues & Solutions**

### **Issue 1: Black Screen Instead of Blue**

**Symptoms**: Test mode enabled but screen is black

**Debugging**:
```cpp
// Add before clear:
Msg("~ [NVRHI Test] Backbuffer: %dx%d, format: %d", 
    backbufferDesc.width, 
    backbufferDesc.height,
    (int)backbufferDesc.format);

// Add after clear:
Msg("~ [NVRHI Test] Clear executed");
```

**Solutions**:
- [ ] Try BGRA8_UNORM format instead of RGBA8_UNORM
- [ ] Verify clear color (should see blue at 0.1, 0.2, 0.4)
- [ ] Check Present() is called after clear
- [ ] Ensure command list is executed

### **Issue 2: Crash in createHandleForNativeTexture**

**Symptoms**: Crash when wrapping backbuffer

**Solutions**:
- [ ] Verify backbufferRes is not null before wrapping
- [ ] Check backbuffer dimensions are valid (> 0)
- [ ] Ensure format is supported by NVRHI
- [ ] Try different initial state (Common instead of RenderTarget)

### **Issue 3: D3D11 Debug Layer Warnings**

**Symptoms**: Warnings about resources still bound

**Solution**:
```cpp
// Before NVRHI test, clear all bindings:
ID3D11ShaderResourceView* nullSRVs[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT] = { nullptr };
HW.pContext->PSSetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, nullSRVs);
HW.pContext->VSSetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, nullSRVs);
HW.pContext->OMSetRenderTargets(0, nullptr, nullptr);
```

### **Issue 4: Linker Errors**

**Symptoms**: Unresolved external symbol nvrhi::*

**Solutions**:
- [ ] Verify nvrhi and nvrhi-d3d11 are in target_link_libraries
- [ ] Check NVRHI built successfully (look for .lib files)
- [ ] Try clean rebuild: `cmake --build build --target clean`
- [ ] Verify NVRHI submodule initialized recursively

---

## 📈 **Progress Tracking**

Use this section to track your progress:

**Week 1 Progress**: ___% complete

**Days Completed**:
- [ ] Day 1: Submodule addition
- [ ] Day 2: CMake integration
- [ ] Day 3: Directory structure
- [ ] Day 4-5: NVRHIDevice wrapper
- [ ] Day 6: Add to CRender
- [ ] Day 7: Initialize
- [ ] Day 8: Cleanup
- [ ] Day 9-10: Console command
- [ ] Day 11-12: Test render
- [ ] Day 13: Testing
- [ ] Day 14: Documentation
- [ ] Day 15: Final validation

**Blockers**: (List any blockers here)

**Notes**: (Any additional notes)

---

## ✅ **Week 1 Sign-off**

When all tasks complete:

- [ ] All code committed and pushed
- [ ] Blue screen test working reliably
- [ ] No known critical bugs
- [ ] Documentation complete
- [ ] Ready for Week 2

**Completed by**: ________________  
**Date**: ________________  
**Sign-off**: ________________

---

**Next**: Week 2 - Resource Wrapping & Triangle Render Test
