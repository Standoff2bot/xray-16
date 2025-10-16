// dx11DetailManager_Compute.cpp - DirectX 11 GPU-driven rendering implementation for detail objects
// Implements compute shader-based frustum culling and indirect drawing
//
#include "stdafx.h"
#include "Layers/xrRender/DetailManager_Compute.h"
#include "Layers/xrRender/DetailManager.h"
#include "dx11HW.h"

namespace xray::render::RENDER_NAMESPACE
{

// ===========================
// Constructor / Destructor
// ===========================

DetailComputeManager::DetailComputeManager()
    : m_instance_count(0)
    , m_max_instances(0)
    , m_index_count(768)  // Default fallback
    , m_initialized(false)
    , m_needs_upload(false)
{
    ZeroMemory(&m_gpu, sizeof(m_gpu));
    ZeroMemory(&m_stats, sizeof(m_stats));
}

DetailComputeManager::~DetailComputeManager()
{
    Shutdown();
}

// ===========================
// Initialization
// ===========================

void DetailComputeManager::Initialize(u32 max_instances)
{
    if (m_initialized)
    {
        Msg("! [DetailComputeManager] Already initialized!");
        return;
    }

    Msg("=== [DetailComputeManager] Initializing GPU-driven detail rendering ===");
    Msg("* [DetailComputeManager] Max instances: %u", max_instances);

    m_max_instances = max_instances;
    m_instance_staging.reserve(max_instances);

    // Create GPU resources
    CreateBuffers(max_instances);

    // Compile shaders
    CompileShaders();

    m_initialized = true;
    Msg("* [DetailComputeManager] Initialization complete");
}

void DetailComputeManager::Shutdown()
{
    if (!m_initialized)
        return;

    Msg("* [DetailComputeManager] Shutting down...");

    DestroyBuffers();

    m_instance_staging.clear();
    m_instance_count = 0;
    m_max_instances = 0;
    m_initialized = false;

    Msg("* [DetailComputeManager] Shutdown complete");
}

// ===========================
// Buffer Management
// ===========================

void DetailComputeManager::CreateBuffers(u32 max_instances)
{
    Msg("* [DetailComputeManager] Creating GPU buffers...");

#if defined(USE_DX11)
    auto* device = HW.pDevice;
    const u32 instance_buffer_size = max_instances * sizeof(DetailInstanceGPU);
    const u32 index_buffer_size = max_instances * sizeof(u32);

    // ===========================
    // Instance Buffer (SRV)
    // ===========================
    {
        D3D_BUFFER_DESC desc = {};
        desc.ByteWidth = instance_buffer_size;
        desc.Usage = D3D_USAGE_DEFAULT;
        desc.BindFlags = D3D_BIND_SHADER_RESOURCE;
        desc.CPUAccessFlags = 0;
        desc.MiscFlags = D3D_RESOURCE_MISC_BUFFER_STRUCTURED;
        desc.StructureByteStride = sizeof(DetailInstanceGPU);

        CHK_DX(device->CreateBuffer(&desc, nullptr, &m_gpu.instance_buffer));

        // Create SRV
        D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
        srv_desc.Format = DXGI_FORMAT_UNKNOWN;
        srv_desc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        srv_desc.Buffer.FirstElement = 0;
        srv_desc.Buffer.NumElements = max_instances;

        CHK_DX(device->CreateShaderResourceView(m_gpu.instance_buffer, &srv_desc, &m_gpu.instance_buffer_srv));

        Msg("    Instance buffer: %u bytes (%u instances)", instance_buffer_size, max_instances);
    }

    // ===========================
    // Visible Indices Buffers (UAV + SRV) - 3 lists for vis_id (still/wave1/wave2)
    // ===========================
    for (int i = 0; i < 3; ++i)
    {
        // Buffer for visible instance indices
        D3D_BUFFER_DESC desc = {};
        desc.ByteWidth = index_buffer_size;
        desc.Usage = D3D_USAGE_DEFAULT;
        desc.BindFlags = D3D_BIND_UNORDERED_ACCESS | D3D_BIND_SHADER_RESOURCE;
        desc.CPUAccessFlags = 0;
        desc.MiscFlags = D3D_RESOURCE_MISC_BUFFER_STRUCTURED;
        desc.StructureByteStride = sizeof(u32);

        CHK_DX(device->CreateBuffer(&desc, nullptr, &m_gpu.visible_indices[i]));

        // Create UAV
        D3D11_UNORDERED_ACCESS_VIEW_DESC uav_desc = {};
        uav_desc.Format = DXGI_FORMAT_UNKNOWN;
        uav_desc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        uav_desc.Buffer.FirstElement = 0;
        uav_desc.Buffer.NumElements = max_instances;
        uav_desc.Buffer.Flags = 0;

        CHK_DX(device->CreateUnorderedAccessView(m_gpu.visible_indices[i], &uav_desc, &m_gpu.visible_indices_uav[i]));

        // Create SRV
        D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
        srv_desc.Format = DXGI_FORMAT_UNKNOWN;
        srv_desc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        srv_desc.Buffer.FirstElement = 0;
        srv_desc.Buffer.NumElements = max_instances;

        CHK_DX(device->CreateShaderResourceView(m_gpu.visible_indices[i], &srv_desc, &m_gpu.visible_indices_srv[i]));
    }
    Msg("    Visible indices buffers (x3): %u bytes each", index_buffer_size);

    // ===========================
    // Counter Buffer (UAV) - Atomic counters for each vis_id
    // ===========================
    {
        D3D_BUFFER_DESC desc = {};
        desc.ByteWidth = 3 * sizeof(u32);  // 3 counters (still, wave1, wave2)
        desc.Usage = D3D_USAGE_DEFAULT;
        desc.BindFlags = D3D_BIND_UNORDERED_ACCESS;
        desc.CPUAccessFlags = 0;
        desc.MiscFlags = D3D_RESOURCE_MISC_BUFFER_STRUCTURED;
        desc.StructureByteStride = sizeof(u32);

        CHK_DX(device->CreateBuffer(&desc, nullptr, &m_gpu.counter_buffer));

        // Create UAV
        D3D11_UNORDERED_ACCESS_VIEW_DESC uav_desc = {};
        uav_desc.Format = DXGI_FORMAT_UNKNOWN;
        uav_desc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        uav_desc.Buffer.FirstElement = 0;
        uav_desc.Buffer.NumElements = 3;
        uav_desc.Buffer.Flags = 0;

        CHK_DX(device->CreateUnorderedAccessView(m_gpu.counter_buffer, &uav_desc, &m_gpu.counter_buffer_uav));

        Msg("    Counter buffer: %u bytes (3 counters)", 3 * sizeof(u32));
    }

    // ===========================
    // Indirect Draw Arguments (UAV) - 3 sets for each vis_id
    // NOTE: Cannot be BOTH structured and indirect args - use RAW buffer UAV instead
    // Compute shader will write to it as RWByteAddressBuffer
    // ===========================
    for (int i = 0; i < 3; ++i)
    {
        D3D_BUFFER_DESC desc = {};
        desc.ByteWidth = sizeof(IndirectDrawArgs);
        desc.Usage = D3D_USAGE_DEFAULT;
        desc.BindFlags = D3D_BIND_UNORDERED_ACCESS;
        desc.CPUAccessFlags = 0;
        desc.MiscFlags = D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS | D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
        desc.StructureByteStride = 0;  // Raw buffer

        CHK_DX(device->CreateBuffer(&desc, nullptr, &m_gpu.indirect_args[i]));

        // Create UAV as RAW buffer (ByteAddressBuffer in shader)
        D3D11_UNORDERED_ACCESS_VIEW_DESC uav_desc = {};
        uav_desc.Format = DXGI_FORMAT_R32_TYPELESS;  // Raw 32-bit access
        uav_desc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        uav_desc.Buffer.FirstElement = 0;
        uav_desc.Buffer.NumElements = sizeof(IndirectDrawArgs) / sizeof(u32);  // 5 uint32s
        uav_desc.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;  // RAW buffer flag

        CHK_DX(device->CreateUnorderedAccessView(m_gpu.indirect_args[i], &uav_desc, &m_gpu.indirect_args_uav[i]));
    }
    Msg("    Indirect args buffers (x3): %u bytes each (raw + indirect)", sizeof(IndirectDrawArgs));

    // ===========================
    // Constant Buffer
    // ===========================
    {
        D3D_BUFFER_DESC desc = {};
        desc.ByteWidth = sizeof(DetailCullParams);
        desc.Usage = D3D_USAGE_DYNAMIC;
        desc.BindFlags = D3D_BIND_CONSTANT_BUFFER;
        desc.CPUAccessFlags = D3D_CPU_ACCESS_WRITE;
        desc.MiscFlags = 0;

        CHK_DX(device->CreateBuffer(&desc, nullptr, &m_gpu.cull_params_cb));
        Msg("    Culling params CB: %u bytes", sizeof(DetailCullParams));
    }

    // ===========================
    // Staging Buffer for Counter Readback (stats only)
    // ===========================
    {
        D3D_BUFFER_DESC desc = {};
        desc.ByteWidth = 3 * sizeof(u32);
        desc.Usage = D3D_USAGE_STAGING;
        desc.BindFlags = 0;
        desc.CPUAccessFlags = D3D_CPU_ACCESS_READ;
        desc.MiscFlags = 0;

        ID3DBuffer* buffer = nullptr;
        CHK_DX(device->CreateBuffer(&desc, nullptr, &buffer));
        m_gpu.counter_readback = buffer;
    }

    // ===========================
    // Debug Buffer (UAV + Readback)
    // ===========================
    {
        // Debug output buffer (UAV)
        D3D_BUFFER_DESC desc = {};
        desc.ByteWidth = max_instances * 16;  // uint4 per instance (16 bytes)
        desc.Usage = D3D_USAGE_DEFAULT;
        desc.BindFlags = D3D_BIND_UNORDERED_ACCESS;
        desc.CPUAccessFlags = 0;
        desc.MiscFlags = D3D_RESOURCE_MISC_BUFFER_STRUCTURED;
        desc.StructureByteStride = 16;  // sizeof(uint4)

        CHK_DX(device->CreateBuffer(&desc, nullptr, &m_gpu.debug_buffer));

        // Create UAV
        D3D11_UNORDERED_ACCESS_VIEW_DESC uav_desc = {};
        uav_desc.Format = DXGI_FORMAT_UNKNOWN;
        uav_desc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        uav_desc.Buffer.FirstElement = 0;
        uav_desc.Buffer.NumElements = max_instances;
        uav_desc.Buffer.Flags = 0;

        CHK_DX(device->CreateUnorderedAccessView(m_gpu.debug_buffer, &uav_desc, &m_gpu.debug_buffer_uav));

        // Readback buffer
        desc.ByteWidth = max_instances * 16;
        desc.Usage = D3D_USAGE_STAGING;
        desc.BindFlags = 0;
        desc.CPUAccessFlags = D3D_CPU_ACCESS_READ;
        desc.MiscFlags = 0;

        ID3DBuffer* buffer = nullptr;
        CHK_DX(device->CreateBuffer(&desc, nullptr, &buffer));
        m_gpu.debug_readback = buffer;

        Msg("    Debug buffer: %u bytes (%u instances)", max_instances * 16, max_instances);
    }

    Msg("* [DetailComputeManager] GPU buffers created successfully");
#endif // USE_DX11
}

void DetailComputeManager::DestroyBuffers()
{
#if defined(USE_DX11)
    _RELEASE(m_gpu.instance_buffer);
    _RELEASE(m_gpu.instance_buffer_srv);

    for (int i = 0; i < 3; ++i)
    {
        _RELEASE(m_gpu.visible_indices[i]);
        _RELEASE(m_gpu.visible_indices_uav[i]);
        _RELEASE(m_gpu.visible_indices_srv[i]);
        _RELEASE(m_gpu.indirect_args[i]);
        _RELEASE(m_gpu.indirect_args_uav[i]);
    }

    _RELEASE(m_gpu.counter_buffer);
    _RELEASE(m_gpu.counter_buffer_uav);

    // Release host buffer handle (HostBufferHandle is just void*)
    if (m_gpu.counter_readback)
    {
        auto* buf = static_cast<ID3DBuffer*>(m_gpu.counter_readback);
        _RELEASE(buf);
        m_gpu.counter_readback = nullptr;
    }

    // Release debug buffers
    _RELEASE(m_gpu.debug_buffer);
    _RELEASE(m_gpu.debug_buffer_uav);
    if (m_gpu.debug_readback)
    {
        auto* buf = static_cast<ID3DBuffer*>(m_gpu.debug_readback);
        _RELEASE(buf);
        m_gpu.debug_readback = nullptr;
    }

    _RELEASE(m_gpu.cull_params_cb);
#endif // USE_DX11
}

// ===========================
// Shader Compilation
// ===========================

void DetailComputeManager::CompileShaders()
{
    Msg("* [DetailComputeManager] Compiling compute shaders...");

#if defined(USE_DX11)
    m_gpu.cull_shader.create("detail_cull");

    if (!m_gpu.cull_shader)
    {
        Msg("! [DetailComputeManager] FAILED to compile detail_cull.cs shader!");
        return;
    }

    Msg("* [DetailComputeManager] Compute shader compiled successfully");
#endif
}

// ===========================
// Instance Management
// ===========================

void DetailComputeManager::BeginInstanceUpdate()
{
    m_instance_staging.clear();
    m_instance_count = 0;
}

void DetailComputeManager::AddInstance(const DetailInstanceGPU& instance)
{
    if (m_instance_staging.size() >= m_max_instances)
    {
        Msg("! [DetailComputeManager] Max instances reached (%u)", m_max_instances);
        return;
    }

    m_instance_staging.push_back(instance);
    m_instance_count++;
}

void DetailComputeManager::EndInstanceUpdate()
{
    m_needs_upload = true;
    // Msg per frame removed to avoid spam
}

// ===========================
// GPU Upload
// ===========================

void DetailComputeManager::UploadInstances(CBackend& cmd_list)
{
    if (!m_needs_upload || m_instance_count == 0)
        return;

#if defined(USE_DX11)
    auto* context = HW.get_context(CHW::IMM_CTX_ID);

    // Update instance buffer on GPU
    D3D11_BOX box = {};
    box.left = 0;
    box.right = m_instance_count * sizeof(DetailInstanceGPU);
    box.top = 0;
    box.bottom = 1;
    box.front = 0;
    box.back = 1;

    context->UpdateSubresource(
        m_gpu.instance_buffer,
        0,
        &box,
        m_instance_staging.data(),
        0,
        0
    );

    m_needs_upload = false;
#endif // USE_DX11
}

// ===========================
// Compute Culling
// ===========================

extern ECORE_API float r_ssaDISCARD;
extern int ps_r__detail_radius;
extern float ps_r__ssaHZBvsTEX;

void DetailComputeManager::DispatchCulling(CBackend& cmd_list, const Fmatrix& view_proj)
{
    if (!m_initialized || m_instance_count == 0)
        return;

#if defined(USE_DX11)
    auto* context = HW.get_context(CHW::IMM_CTX_ID);

    // Upload instances if needed
    UploadInstances(cmd_list);

    // Build frustum from view-projection matrix
    FrustumGPU frustum = BuildFrustumGPU(view_proj);

    // Prepare culling parameters
    DetailCullParams params = {};
    params.camera_pos = Device.vCameraPosition;
    params.camera_dir = Device.vCameraDirection;
    params.fade_limit_sqr = psDeviceFlags.test(rsDrawDetails) ? (float(ps_r__detail_radius) * float(ps_r__detail_radius)) : 0.f;
    params.fade_start_sqr = params.fade_limit_sqr * 0.8f * 0.8f;  // Start fading at 80% of max distance
    params.r_ssa_discard = r_ssaDISCARD;
    params.r_ssa_cheap = ps_r__ssaHZBvsTEX;
    params.instance_count = m_instance_count;
    params.frame_number = Device.dwFrame;

    // Copy frustum planes to params
    for (int i = 0; i < 6; ++i)
    {
        params.frustum_planes[i] = frustum.planes[i];
    }

    // Upload constant buffer (map/unmap)
    D3D11_MAPPED_SUBRESOURCE mapped;
    CHK_DX(context->Map(m_gpu.cull_params_cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped));
    memcpy(mapped.pData, &params, sizeof(DetailCullParams));
    context->Unmap(m_gpu.cull_params_cb, 0);

    // Clear counters (write zeros)
    u32 zeros[3] = { 0, 0, 0 };
    context->UpdateSubresource(m_gpu.counter_buffer, 0, nullptr, zeros, 0, 0);

    // Initialize indirect draw arguments with correct index count
    IndirectDrawArgs init_args = {};
    init_args.index_count = m_index_count;  // Set by DetailManager after creating GPU geometry
    init_args.instance_count = 0;           // Will be set by compute shader
    init_args.start_index = 0;
    init_args.base_vertex = 0;
    init_args.start_instance = 0;

    // Upload initial values to all 3 indirect args buffers
    for (int i = 0; i < 3; ++i)
    {
        context->UpdateSubresource(m_gpu.indirect_args[i], 0, nullptr, &init_args, 0, 0);
    }

    // Bind compute shader
    context->CSSetShader(m_gpu.cull_shader->sh, nullptr, 0);

    // Bind constant buffer
    ID3DBuffer* cbs[] = { m_gpu.cull_params_cb };
    context->CSSetConstantBuffers(0, 1, cbs);

    // Bind input (instance buffer SRV)
    context->CSSetShaderResources(0, 1, &m_gpu.instance_buffer_srv);

    // Bind outputs (UAVs)
    // t0 = instance_buffer (SRV)
    // u0-u2 = visible_indices (UAVs)
    // u3 = counter_buffer (UAV)
    // u4-u6 = indirect_args (UAVs)
    // u7 = debug_buffer (UAV)
    ID3DUnorderedAccessView* uavs[] = {
        m_gpu.visible_indices_uav[0],  // u0 - still
        m_gpu.visible_indices_uav[1],  // u1 - wave1
        m_gpu.visible_indices_uav[2],  // u2 - wave2
        m_gpu.counter_buffer_uav,       // u3 - counters
        m_gpu.indirect_args_uav[0],     // u4 - args still
        m_gpu.indirect_args_uav[1],     // u5 - args wave1
        m_gpu.indirect_args_uav[2],     // u6 - args wave2
        m_gpu.debug_buffer_uav          // u7 - debug output
    };
    context->CSSetUnorderedAccessViews(0, 8, uavs, nullptr);

    // Dispatch compute shader
    constexpr u32 threads_per_group = 256;
    const u32 num_groups = (m_instance_count + threads_per_group - 1) / threads_per_group;

    context->Dispatch(num_groups, 1, 1);

    // Unbind resources
    ID3DShaderResourceView* null_srv[] = { nullptr };
    ID3DUnorderedAccessView* null_uav[] = { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
    context->CSSetShaderResources(0, 1, null_srv);
    context->CSSetUnorderedAccessViews(0, 8, null_uav, nullptr);

    // Update stats
    m_stats.total_instances = m_instance_count;
    m_stats.compute_dispatches++;

    // Read back counter values (for stats) - async, don't stall
    auto* readback_buffer = static_cast<ID3DBuffer*>(m_gpu.counter_readback);
    context->CopyResource(readback_buffer, m_gpu.counter_buffer);
#endif // USE_DX11
}

// ===========================
// Indirect Rendering
// ===========================

void DetailComputeManager::RenderIndirect(CBackend& cmd_list, u32 object_id, u32 vis_id, u32 lod_id)
{
    if (!m_initialized || m_instance_count == 0)
        return;

    if (vis_id >= 3)
    {
        Msg("! [DetailComputeManager] Invalid vis_id: %u", vis_id);
        return;
    }

#if defined(USE_DX11)
    auto* context = HW.get_context(CHW::IMM_CTX_ID);

    // Bind shader resources for GPU instanced rendering
    // t0 = visible_indices[vis_id] - maps SV_InstanceID to actual instance index
    // t1 = instance_buffer - all instance data (DetailInstanceGPU structures)
    ID3DShaderResourceView* srvs[] = {
        m_gpu.visible_indices_srv[vis_id],
        m_gpu.instance_buffer_srv
    };
    context->VSSetShaderResources(0, 2, srvs);

    // Debug: Log first indirect draw call per frame
    static u32 last_frame = 0;
    if (Device.dwFrame != last_frame && vis_id == 0)
    {
        // Read back indirect args to see what we're actually drawing
        ID3DBuffer* readback_buf = static_cast<ID3DBuffer*>(m_gpu.counter_readback);
        context->CopyResource(readback_buf, m_gpu.indirect_args[vis_id]);

        D3D11_MAPPED_SUBRESOURCE mapped;
        if (SUCCEEDED(context->Map(readback_buf, 0, D3D_MAP_READ, 0, &mapped)))
        {
            IndirectDrawArgs* args = static_cast<IndirectDrawArgs*>(mapped.pData);
            Msg("* [GPU Draw] vis_id=%u: index_count=%u, instance_count=%u",
                vis_id, args->index_count, args->instance_count);
            context->Unmap(readback_buf, 0);
        }
        last_frame = Device.dwFrame;
    }

    // Execute indirect draw
    // Note: The caller (DetailManager::Render) must set up:
    // - Geometry (VB/IB) via cmd_list.set_Geometry() - BASE grass blade geometry
    // - Shader element via cmd_list.set_Element() - must use lod_gpu vertex shader!
    // - Constants (wave, wind, etc.) via cmd_list.set_c()
    context->DrawIndexedInstancedIndirect(m_gpu.indirect_args[vis_id], 0);

    // Unbind SRVs
    ID3DShaderResourceView* null_srvs[] = { nullptr, nullptr };
    context->VSSetShaderResources(0, 2, null_srvs);

#endif // USE_DX11
}

// ===========================
// Statistics
// ===========================

void DetailComputeManager::ResetStats()
{
    ZeroMemory(&m_stats, sizeof(m_stats));
}

// ===========================
// Debug Functions
// ===========================

void DetailComputeManager::ReadDebugData()
{
    if (!m_initialized || m_instance_count == 0)
        return;

#if defined(USE_DX11)
    auto* context = HW.get_context(CHW::IMM_CTX_ID);

    // Copy debug buffer from GPU to staging
    auto* readback = static_cast<ID3DBuffer*>(m_gpu.debug_readback);
    context->CopyResource(readback, m_gpu.debug_buffer);

    // Map and read
    D3D11_MAPPED_SUBRESOURCE mapped;
    CHK_DX(context->Map(readback, 0, D3D_MAP_READ, 0, &mapped));

    struct DebugEntry
    {
        u32 instance_idx;
        u32 cull_reason;  // 0=visible, 1=distance, 2=frustum, 3=ssa
        u32 vis_id;
        u32 dist_sqr;
    };

    DebugEntry* data = static_cast<DebugEntry*>(mapped.pData);

    // Analyze culling results
    u32 visible_count = 0;
    u32 distance_culled = 0;
    u32 frustum_culled = 0;
    u32 ssa_culled = 0;
    u32 vis_counts[3] = {0, 0, 0};  // Per vis_id

    // Sample every Nth instance to avoid too much processing
    const u32 sample_stride = (m_instance_count > 1000) ? (m_instance_count / 1000) : 1;

    for (u32 i = 0; i < m_instance_count; i += sample_stride)
    {
        switch (data[i].cull_reason)
        {
        case 0: // Visible
            visible_count++;
            if (data[i].vis_id < 3)
                vis_counts[data[i].vis_id]++;
            break;
        case 1: distance_culled++; break;
        case 2: frustum_culled++; break;
        case 3: ssa_culled++; break;
        }
    }

    context->Unmap(readback, 0);

    // Scale back up if we sampled
    if (sample_stride > 1)
    {
        visible_count *= sample_stride;
        distance_culled *= sample_stride;
        frustum_culled *= sample_stride;
        ssa_culled *= sample_stride;
        vis_counts[0] *= sample_stride;
        vis_counts[1] *= sample_stride;
        vis_counts[2] *= sample_stride;
    }

    // Print statistics
    Msg("=== [GPU Culling Debug] Frame %d ===", Device.dwFrame);
    Msg("  Total instances:    %6u", m_instance_count);
    Msg("  Visible:            %6u (%5.1f%%)", visible_count, 100.0f * visible_count / m_instance_count);
    Msg("  Culled by distance: %6u (%5.1f%%)", distance_culled, 100.0f * distance_culled / m_instance_count);
    Msg("  Culled by frustum:  %6u (%5.1f%%)", frustum_culled, 100.0f * frustum_culled / m_instance_count);
    Msg("  Culled by SSA:      %6u (%5.1f%%)", ssa_culled, 100.0f * ssa_culled / m_instance_count);
    Msg("  Visible breakdown:");
    Msg("    Still (vis_id=0):  %6u", vis_counts[0]);
    Msg("    Wave1 (vis_id=1):  %6u", vis_counts[1]);
    Msg("    Wave2 (vis_id=2):  %6u", vis_counts[2]);

    // Read back counter values to verify
    auto* counter_readback = static_cast<ID3DBuffer*>(m_gpu.counter_readback);
    context->CopyResource(counter_readback, m_gpu.counter_buffer);

    CHK_DX(context->Map(counter_readback, 0, D3D_MAP_READ, 0, &mapped));
    u32* counters = static_cast<u32*>(mapped.pData);
    Msg("  GPU Counters (actual):");
    Msg("    Still:  %6u", counters[0]);
    Msg("    Wave1:  %6u", counters[1]);
    Msg("    Wave2:  %6u", counters[2]);
    context->Unmap(counter_readback, 0);

#endif // USE_DX11
}

// ===========================
// Utility Functions Implementation
// ===========================

DetailInstanceGPU ConvertToGPUInstance(
    const void* item_ptr,
    u32 object_id,
    const void* detail_ptr,
    int slot_x,
    int slot_z)
{
    const CDetailManager::SlotItem& item = *static_cast<const CDetailManager::SlotItem*>(item_ptr);
    const CDetail& detail_object = *static_cast<const CDetail*>(detail_ptr);

    DetailInstanceGPU gpu_inst = {};

    // Extract position from transform matrix
    gpu_inst.position.set(item.mRotY._41, item.mRotY._42, item.mRotY._43);
    gpu_inst.scale = item.scale;

    // Extract rotation (assuming rotation around Y axis)
    // atan2(m31, m33) gives Y rotation
    gpu_inst.rotation_y = atan2f(item.mRotY._31, item.mRotY._33);

    // Rendering data
    gpu_inst.c_hemi = item.c_hemi;
    gpu_inst.c_sun = item.c_sun;
    gpu_inst.object_id = object_id;
    gpu_inst.vis_id = item.vis_ID;

#if RENDER == R_R1
    gpu_inst.color_rgb = item.c_rgb;
#endif

    // Bounding data (from detail object)
    gpu_inst.bounds_min = detail_object.bv_bb.vMin;
    gpu_inst.bounds_max = detail_object.bv_bb.vMax;
    gpu_inst.bounds_radius = detail_object.bv_sphere.R;

    // Metadata
    gpu_inst.slot_x = slot_x;
    gpu_inst.slot_z = slot_z;
    gpu_inst.flags = 0;
    gpu_inst.fade_distance_sqr = item.distance;  // Already squared by CPU culling

    return gpu_inst;
}

} // namespace xray::render::RENDER_NAMESPACE
