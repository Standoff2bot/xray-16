// dx11DetailManager_Compute.cpp - DirectX 11 GPU-driven rendering implementation for detail objects
// Implements compute shader-based frustum culling and indirect drawing
//
#include "stdafx.h"
#include "Layers/xrRender/DetailManager_Compute.h"
#include "Layers/xrRender/DetailManager.h"
#include "Layers/xrRender/GPUGrassPlacement.h"

#include <array>
#include <cmath>
#include <iterator>
#include "Layers/xrRender/GPUGrassData.h"
#include "dx11HW.h"

using xray::render::RENDER_NAMESPACE::gpu_grass::PlacementSeed;
using xray::render::RENDER_NAMESPACE::gpu_grass::kHeightQuantOffset;
using xray::render::RENDER_NAMESPACE::gpu_grass::kHeightQuantStep;

namespace xray::render::RENDER_NAMESPACE
{

namespace
{
struct PlacementSeedGPUData
{
    u32 slot_x;
    u32 slot_z;
    u32 base_seed;
    u32 object_mask_low;
    u32 object_mask_high;
    float density_scale;
    float c_hemi;
    float c_sun;
    float pad0;
};

struct SlotReferenceGPUData
{
    u32 slot_index;
    u32 slot_local_x;
    u32 slot_local_z;
    u32 pad;
};

struct SlotHeightGPUData
{
    float min_height;
    float max_height;
};
} // namespace


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
    // Instance Buffer (SRV + UAV)
    // ===========================
    {
        D3D_BUFFER_DESC desc = {};
        desc.ByteWidth = instance_buffer_size;
        desc.Usage = D3D_USAGE_DEFAULT;
        desc.BindFlags = D3D_BIND_SHADER_RESOURCE | D3D_BIND_UNORDERED_ACCESS;
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

        // Create UAV
        D3D11_UNORDERED_ACCESS_VIEW_DESC uav_desc = {};
        uav_desc.Format = DXGI_FORMAT_UNKNOWN;
        uav_desc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        uav_desc.Buffer.FirstElement = 0;
        uav_desc.Buffer.NumElements = max_instances;
        uav_desc.Buffer.Flags = 0;

        CHK_DX(device->CreateUnorderedAccessView(m_gpu.instance_buffer, &uav_desc, &m_gpu.instance_buffer_uav));

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
    // Instance Counter Buffer (UAV)
    // ===========================
    {
        D3D_BUFFER_DESC desc = {};
        desc.ByteWidth = sizeof(u32);
        desc.Usage = D3D_USAGE_DEFAULT;
        desc.BindFlags = D3D_BIND_UNORDERED_ACCESS | D3D_BIND_SHADER_RESOURCE;
        desc.CPUAccessFlags = 0;
        desc.MiscFlags = D3D_RESOURCE_MISC_BUFFER_STRUCTURED;
        desc.StructureByteStride = sizeof(u32);

        CHK_DX(device->CreateBuffer(&desc, nullptr, &m_gpu.instance_counter_buffer));

        D3D11_UNORDERED_ACCESS_VIEW_DESC uav_desc = {};
        uav_desc.Format = DXGI_FORMAT_UNKNOWN;
        uav_desc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        uav_desc.Buffer.FirstElement = 0;
        uav_desc.Buffer.NumElements = 1;
        uav_desc.Buffer.Flags = 0;

        CHK_DX(device->CreateUnorderedAccessView(m_gpu.instance_counter_buffer, &uav_desc, &m_gpu.instance_counter_uav));

        D3D_BUFFER_DESC readback_desc = desc;
        readback_desc.BindFlags = 0;
        readback_desc.CPUAccessFlags = D3D_CPU_ACCESS_READ;
        readback_desc.Usage = D3D_USAGE_STAGING;
        readback_desc.MiscFlags = 0;

        ID3DBuffer* readback = nullptr;
        CHK_DX(device->CreateBuffer(&readback_desc, nullptr, &readback));
        m_gpu.instance_counter_readback = readback;

        Msg("    Instance counter buffer: %u bytes", sizeof(u32));
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

    {
        D3D_BUFFER_DESC desc = {};
        desc.ByteWidth = sizeof(PlacementParamsGPU);
        desc.Usage = D3D_USAGE_DYNAMIC;
        desc.BindFlags = D3D_BIND_CONSTANT_BUFFER;
        desc.CPUAccessFlags = D3D_CPU_ACCESS_WRITE;
        desc.MiscFlags = 0;

        CHK_DX(device->CreateBuffer(&desc, nullptr, &m_gpu.placement_params_cb));
        Msg("    Placement params CB: %u bytes", sizeof(PlacementParamsGPU));
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
    // Staging Buffer for Indirect Args Readback (debug only)
    // ===========================
    {
        D3D_BUFFER_DESC desc = {};
        desc.ByteWidth = sizeof(IndirectDrawArgs);
        desc.Usage = D3D_USAGE_STAGING;
        desc.BindFlags = 0;
        desc.CPUAccessFlags = D3D_CPU_ACCESS_READ;
        desc.MiscFlags = 0;

        ID3DBuffer* buffer = nullptr;
        CHK_DX(device->CreateBuffer(&desc, nullptr, &buffer));
        m_gpu.indirect_args_readback = buffer;

        Msg("    Indirect args readback: %u bytes", sizeof(IndirectDrawArgs));
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
    _RELEASE(m_gpu.instance_buffer_uav);

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
    _RELEASE(m_gpu.instance_counter_buffer);
    _RELEASE(m_gpu.instance_counter_uav);

    if (m_gpu.counter_readback)
    {
        auto* buf = static_cast<ID3DBuffer*>(m_gpu.counter_readback);
        _RELEASE(buf);
        m_gpu.counter_readback = nullptr;
    }
    if (m_gpu.indirect_args_readback)
    {
        auto* buf = static_cast<ID3DBuffer*>(m_gpu.indirect_args_readback);
        _RELEASE(buf);
        m_gpu.indirect_args_readback = nullptr;
    }
    _RELEASE(m_gpu.instance_counter_readback);

    _RELEASE(m_gpu.debug_buffer);
    _RELEASE(m_gpu.debug_buffer_uav);
    if (m_gpu.debug_readback)
    {
        auto* buf = static_cast<ID3DBuffer*>(m_gpu.debug_readback);
        _RELEASE(buf);
        m_gpu.debug_readback = nullptr;
    }

    _RELEASE(m_gpu.detail_object_buffer);
    _RELEASE(m_gpu.detail_object_srv);

    _RELEASE(m_gpu.cull_params_cb);
    _RELEASE(m_gpu.placement_params_cb);
    m_gpu.placement_shader.destroy();
    m_gpu.cull_shader.destroy();
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
    m_gpu.placement_shader.create("detail_place");

    if (!m_gpu.cull_shader)
    {
        Msg("! [DetailComputeManager] FAILED to compile detail_cull.cs shader!");
        return;
    }

    if (!m_gpu.placement_shader)
    {
        Msg("! [DetailComputeManager] FAILED to compile detail_place.cs shader!");
        return;
    }

    Msg("* [DetailComputeManager] Compute shaders compiled successfully");
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
    auto* context = HW.get_context(cmd_list.context_id);

    // PERFORMANCE WARNING: This uploads ALL instances to GPU every frame!
    // With many instances, this can cause massive CPU→GPU bandwidth saturation
    u32 upload_size_bytes = m_instance_count * sizeof(DetailInstanceGPU);
    float upload_size_mb = upload_size_bytes / (1024.0f * 1024.0f);

    // Log every 60 frames to track upload size
    static u32 upload_log_counter = 0;
    if ((++upload_log_counter % 60) == 0)
    {
        Msg("! [DetailComputeManager] Uploading %u instances (%.2f MB) to GPU",
            m_instance_count, upload_size_mb);
    }

    // Update instance buffer on GPU
    D3D11_BOX box = {};
    box.left = 0;
    box.right = upload_size_bytes;
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

void DetailComputeManager::ResetInstanceAllocator(CBackend& cmd_list)
{
#if defined(USE_DX11)
    auto* context = HW.get_context(cmd_list.context_id);
    const UINT zeros[4] = {0, 0, 0, 0};

    if (m_gpu.instance_counter_uav)
        context->ClearUnorderedAccessViewUint(m_gpu.instance_counter_uav, zeros);

    if (m_gpu.counter_buffer_uav)
        context->ClearUnorderedAccessViewUint(m_gpu.counter_buffer_uav, zeros);

    for (int i = 0; i < 3; ++i)
    {
        if (m_gpu.visible_indices_uav[i])
            context->ClearUnorderedAccessViewUint(m_gpu.visible_indices_uav[i], zeros);
        if (m_gpu.indirect_args_uav[i])
            context->ClearUnorderedAccessViewUint(m_gpu.indirect_args_uav[i], zeros);
    }

    m_instance_count = 0;
    m_stats.total_instances = 0;
    m_stats.compute_dispatches = 0;
    m_needs_upload = false;
#endif
}

void DetailComputeManager::ProcessPlacementTiles(CBackend& cmd_list, const xr_vector<gpu_grass::TileResourceSlice>& tiles)
{
#if defined(USE_DX11)
    if (!m_gpu.placement_shader || !m_gpu.detail_object_srv)
        return;

    const float density = ps_current_detail_density > EPS ? ps_current_detail_density : DETAIL_SLOT_SIZE;
    const u32 sample_dim = static_cast<u32>(std::ceil(DETAIL_SLOT_SIZE / density)) + 1u;
    const u32 samples_per_slot = sample_dim * sample_dim;
    const u32 threads_per_group = 64;

    for (const auto& tile : tiles)
    {
        if (tile.seed_count == 0)
            continue;
        DispatchPlacement(cmd_list, tile);
        const u32 total_samples = tile.seed_count * samples_per_slot;
        const u32 dispatch_x = (total_samples + threads_per_group - 1u) / threads_per_group;
        m_stats.compute_dispatches += dispatch_x;
    }

    m_needs_upload = false;
#else
    XR_UNUSED(cmd_list);
    XR_UNUSED(tiles);
#endif
}

void DetailComputeManager::FinalizePlacement(CBackend& cmd_list)
{
#if defined(USE_DX11)
    m_instance_count = ReadInstanceCounter(cmd_list);
    m_stats.total_instances = m_instance_count;
    m_needs_upload = false;
#else
    XR_UNUSED(cmd_list);
#endif
}

void DetailComputeManager::UploadDetailObjects(const xr_vector<DetailObjectGPU>& details)
{
#if defined(USE_DX11)
    UploadDetailObjectsInternal(details);
#else
    XR_UNUSED(details);
#endif
}

void DetailComputeManager::UploadDetailObjectsInternal(const xr_vector<DetailObjectGPU>& details)
{
#if defined(USE_DX11)
    auto* device = HW.pDevice;

    _RELEASE(m_gpu.detail_object_srv);
    _RELEASE(m_gpu.detail_object_buffer);

    if (details.empty())
        return;

    D3D_BUFFER_DESC desc = {};
    desc.ByteWidth = static_cast<u32>(details.size() * sizeof(DetailObjectGPU));
    desc.Usage = D3D_USAGE_IMMUTABLE;
    desc.BindFlags = D3D_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = 0;
    desc.MiscFlags = D3D_RESOURCE_MISC_BUFFER_STRUCTURED;
    desc.StructureByteStride = sizeof(DetailObjectGPU);

    D3D11_SUBRESOURCE_DATA init_data = {};
    init_data.pSysMem = details.data();

    CHK_DX(device->CreateBuffer(&desc, &init_data, &m_gpu.detail_object_buffer));

    D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
    srv_desc.Format = DXGI_FORMAT_UNKNOWN;
    srv_desc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    srv_desc.Buffer.FirstElement = 0;
    srv_desc.Buffer.NumElements = static_cast<u32>(details.size());

    CHK_DX(device->CreateShaderResourceView(m_gpu.detail_object_buffer, &srv_desc, &m_gpu.detail_object_srv));

    Msg("* [DetailComputeManager] Uploaded %zu detail object descriptors", details.size());
#else
    XR_UNUSED(details);
#endif
}

u32 DetailComputeManager::ReadInstanceCounter(CBackend& cmd_list)
{
#if defined(USE_DX11)
    if (!m_gpu.instance_counter_buffer || !m_gpu.instance_counter_readback)
        return 0;

    auto* context = HW.get_context(cmd_list.context_id);
    context->CopyResource(m_gpu.instance_counter_readback, m_gpu.instance_counter_buffer);

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    CHK_DX(context->Map(m_gpu.instance_counter_readback, 0, D3D11_MAP_READ, 0, &mapped));
    u32 count = *reinterpret_cast<const u32*>(mapped.pData);
    context->Unmap(m_gpu.instance_counter_readback, 0);

    return count;
#else
    XR_UNUSED(cmd_list);
    return 0;
#endif
}

void DetailComputeManager::DispatchPlacement(CBackend& cmd_list, const gpu_grass::TileResourceSlice& tile)
{
#if defined(USE_DX11)
    auto* device = HW.pDevice;
    auto* context = HW.get_context(cmd_list.context_id);

    xr_vector<PlacementSeedGPUData> seeds_gpu;
    seeds_gpu.reserve(tile.seed_count);
    for (u32 i = 0; i < tile.seed_count; ++i)
    {
        const PlacementSeed& seed = tile.seeds[i];
        PlacementSeedGPUData gpu_seed = {};
        gpu_seed.slot_x = seed.slot_x;
        gpu_seed.slot_z = seed.slot_z;
        gpu_seed.base_seed = seed.base_seed;
        gpu_seed.object_mask_low = seed.object_mask_low;
        gpu_seed.object_mask_high = seed.object_mask_high;
        gpu_seed.density_scale = seed.density_scale;
        gpu_seed.c_hemi = seed.c_hemi;
        gpu_seed.c_sun = seed.c_sun;
        seeds_gpu.emplace_back(gpu_seed);
    }

    xr_vector<SlotReferenceGPUData> slots_gpu;
    slots_gpu.reserve(tile.slot_count);
    for (u32 i = 0; i < tile.slot_count; ++i)
    {
        SlotReferenceGPUData ref = {};
        ref.slot_index = tile.slot_refs[i].slot_index;
        ref.slot_local_x = tile.slot_refs[i].slot_local_x;
        ref.slot_local_z = tile.slot_refs[i].slot_local_z;
        slots_gpu.emplace_back(ref);
    }

    xr_vector<SlotHeightGPUData> heights_gpu;
    heights_gpu.reserve(tile.slot_count);
    for (u32 i = 0; i < tile.slot_count; ++i)
    {
        SlotHeightGPUData h = {};
        h.min_height = tile.slot_heights[i].min_height;
        h.max_height = tile.slot_heights[i].max_height;
        heights_gpu.emplace_back(h);
    }

    xr_vector<std::array<u32, 4>> objects_gpu;
    objects_gpu.reserve(tile.slot_count);
    for (u32 i = 0; i < tile.slot_count; ++i)
    {
        const u8* src = tile.object_ids + i * 4;
        objects_gpu.push_back({ { src[0], src[1], src[2], src[3] } });
    }

    xr_vector<Fvector4> palette_gpu;
    palette_gpu.reserve(tile.slot_count * 4);
    const u32 tile_span = tile.tile_span;
    const u32 tile_texels = tile_span * tile_span;
    const u32 layer_stride_bytes = tile_texels * 4;
    const u8* palette_src = tile.palette_data;

    for (u32 slot_idx = 0; slot_idx < tile.slot_count; ++slot_idx)
    {
        const u32 texel_index = tile.slot_refs[slot_idx].slot_local_z * tile_span + tile.slot_refs[slot_idx].slot_local_x;
        for (u32 layer = 0; layer < 4; ++layer)
        {
            const u8* src = palette_src + layer * layer_stride_bytes + texel_index * 4;
            Fvector4 val;
            val.x = src[0] / 255.0f;
            val.y = src[1] / 255.0f;
            val.z = src[2] / 255.0f;
            val.w = src[3] / 255.0f;
            palette_gpu.emplace_back(val);
        }
    }

    xr_vector<float> heightmap_gpu;
    if (tile.height_bytes > 0)
    {
        const u16* height_src = reinterpret_cast<const u16*>(tile.height_data);
        const size_t height_count = tile.height_bytes / sizeof(u16);
        heightmap_gpu.resize(height_count);
        for (size_t i = 0; i < height_count; ++i)
        {
            heightmap_gpu[i] = float(height_src[i]) * kHeightQuantStep - kHeightQuantOffset;
        }
    }

    auto createStructuredBuffer = [&](const void* data, u32 stride, u32 count, ID3DBuffer** outBuffer, ID3DShaderResourceView** outSrv)
    {
        if (count == 0)
        {
            *outBuffer = nullptr;
            *outSrv = nullptr;
            return;
        }

        D3D_BUFFER_DESC desc = {};
        desc.ByteWidth = stride * count;
        desc.Usage = D3D_USAGE_IMMUTABLE;
        desc.BindFlags = D3D_BIND_SHADER_RESOURCE;
        desc.CPUAccessFlags = 0;
        desc.MiscFlags = D3D_RESOURCE_MISC_BUFFER_STRUCTURED;
        desc.StructureByteStride = stride;

        D3D11_SUBRESOURCE_DATA init = {};
        init.pSysMem = data;

        CHK_DX(device->CreateBuffer(&desc, &init, outBuffer));

        D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
        srv_desc.Format = DXGI_FORMAT_UNKNOWN;
        srv_desc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        srv_desc.Buffer.FirstElement = 0;
        srv_desc.Buffer.NumElements = count;

        CHK_DX(device->CreateShaderResourceView(*outBuffer, &srv_desc, outSrv));
    };

    ID3DBuffer* seed_buffer = nullptr;
    ID3DShaderResourceView* seed_srv = nullptr;
    createStructuredBuffer(seeds_gpu.data(), sizeof(PlacementSeedGPUData), static_cast<u32>(seeds_gpu.size()), &seed_buffer, &seed_srv);

    ID3DBuffer* slot_buffer = nullptr;
    ID3DShaderResourceView* slot_srv = nullptr;
    createStructuredBuffer(slots_gpu.data(), sizeof(SlotReferenceGPUData), static_cast<u32>(slots_gpu.size()), &slot_buffer, &slot_srv);

    ID3DBuffer* height_buffer = nullptr;
    ID3DShaderResourceView* height_srv = nullptr;
    createStructuredBuffer(heights_gpu.data(), sizeof(SlotHeightGPUData), static_cast<u32>(heights_gpu.size()), &height_buffer, &height_srv);

    ID3DBuffer* object_buffer = nullptr;
    ID3DShaderResourceView* object_srv = nullptr;
    createStructuredBuffer(objects_gpu.data(), sizeof(std::array<u32, 4>), static_cast<u32>(objects_gpu.size()), &object_buffer, &object_srv);

    ID3DBuffer* palette_buffer = nullptr;
    ID3DShaderResourceView* palette_srv = nullptr;
    createStructuredBuffer(palette_gpu.data(), sizeof(Fvector4), static_cast<u32>(palette_gpu.size()), &palette_buffer, &palette_srv);

    ID3DBuffer* heightmap_buffer = nullptr;
    ID3DShaderResourceView* heightmap_srv = nullptr;
    if (!heightmap_gpu.empty())
        createStructuredBuffer(heightmap_gpu.data(), sizeof(float), static_cast<u32>(heightmap_gpu.size()), &heightmap_buffer, &heightmap_srv);

    const float density = ps_current_detail_density > EPS ? ps_current_detail_density : DETAIL_SLOT_SIZE;
    const u32 sample_dim = static_cast<u32>(std::ceil(DETAIL_SLOT_SIZE / density)) + 1u;
    const u32 samples_per_slot = sample_dim * sample_dim;
    const float jitter = density / 1.7f;

    PlacementParamsGPU params = {};
    params.instance_offset = 0;
    params.slot_offset = 0;
    params.slot_count = tile.seed_count;
    params.tile_span = tile.tile_span;
    params.tile_resolution = (heightmap_srv != nullptr) ? tile.tile_resolution : 0u;
    params.samples_per_slot = samples_per_slot;
    params.sample_dim = sample_dim;
    params.max_instances = m_max_instances;
    params.slot_size = DETAIL_SLOT_SIZE;
    params.density = density;
    params.jitter_amplitude = jitter;
    params.detail_height_scale = ps_current_detail_height;
    params.tile_origin_x = float(tile.payload.coord.region_x) * float(tile.tile_span) * DETAIL_SLOT_SIZE;
    params.tile_origin_z = float(tile.payload.coord.region_z) * float(tile.tile_span) * DETAIL_SLOT_SIZE;

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    CHK_DX(context->Map(m_gpu.placement_params_cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped));
    memcpy(mapped.pData, &params, sizeof(PlacementParamsGPU));
    context->Unmap(m_gpu.placement_params_cb, 0);

    ID3DBuffer* cbs[] = { m_gpu.placement_params_cb };
    context->CSSetConstantBuffers(0, 1, cbs);

    ID3D11ShaderResourceView* srvs[] = { seed_srv, slot_srv, height_srv, m_gpu.detail_object_srv, object_srv, palette_srv, heightmap_srv };
    context->CSSetShaderResources(0, static_cast<UINT>(std::size(srvs)), srvs);

    ID3D11UnorderedAccessView* uavs[] = { m_gpu.instance_buffer_uav, m_gpu.instance_counter_uav };
    UINT initial_counts[2] = { D3D11_APPEND_ALIGNED_ELEMENT, D3D11_APPEND_ALIGNED_ELEMENT };
    context->CSSetUnorderedAccessViews(0, 2, uavs, initial_counts);

    context->CSSetShader(m_gpu.placement_shader->sh, nullptr, 0);

    const u32 threads_per_group = 64;
    const u32 total_samples = tile.seed_count * samples_per_slot;
    const u32 dispatch_x = (total_samples + threads_per_group - 1u) / threads_per_group;
    context->Dispatch(dispatch_x, 1, 1);

    ID3D11ShaderResourceView* null_srvs[7] = { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
    context->CSSetShaderResources(0, 7, null_srvs);
    ID3D11UnorderedAccessView* null_uavs[2] = { nullptr, nullptr };
    UINT dummy_counts[2] = { 0, 0 };
    context->CSSetUnorderedAccessViews(0, 2, null_uavs, dummy_counts);
    ID3DBuffer* null_cbs[1] = { nullptr };
    context->CSSetConstantBuffers(0, 1, null_cbs);
    context->CSSetShader(nullptr, nullptr, 0);

    _RELEASE(seed_srv);
    _RELEASE(seed_buffer);
    _RELEASE(slot_srv);
    _RELEASE(slot_buffer);
    _RELEASE(height_srv);
    _RELEASE(height_buffer);
    _RELEASE(object_srv);
    _RELEASE(object_buffer);
    _RELEASE(palette_srv);
    _RELEASE(palette_buffer);
    if (heightmap_srv)
        _RELEASE(heightmap_srv);
    if (heightmap_buffer)
        _RELEASE(heightmap_buffer);
#else
    XR_UNUSED(cmd_list);
    XR_UNUSED(tile);
#endif
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
    auto* context = HW.get_context(cmd_list.context_id);

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

    // PERFORMANCE FIX: Removed massive debug buffer clear (up to 1.6MB per frame!)
    // Kept counter clear (12 bytes, needed for InterlockedAdd) and indirect args init (60 bytes)

    // Clear atomic counters (needed for InterlockedAdd operations in compute shader)
    u32 zeros[3] = { 0, 0, 0 };
    context->UpdateSubresource(m_gpu.counter_buffer, 0, nullptr, zeros, 0, 0);

    // Initialize indirect args with index_count (compute shader doesn't know this value)
    // The compute shader will update instance_count via InterlockedAdd at offset 4
    IndirectDrawArgs init_args = {};
    init_args.index_count = m_index_count;  // Set by DetailManager after creating GPU geometry
    init_args.instance_count = 0;           // Will be set by compute shader
    init_args.start_index = 0;
    init_args.base_vertex = 0;
    init_args.start_instance = 0;

    // Upload initial index_count to all 3 indirect args buffers
    // We need to upload the full struct because UpdateSubresource doesn't support partial updates cleanly
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

    // PERFORMANCE FIX: Removed GPU→CPU readback from hot path
    // The CopyResource was happening every frame and could cause GPU→CPU stalls
    // Stats are only read in ReadDebugData() which is called every 60 frames, so do the copy there
#endif // USE_DX11
}

// ===========================
// Indirect Rendering
// ===========================

void DetailComputeManager::RenderIndirect(CBackend& cmd_list, u32 vis_id)
{
    if (!m_initialized || m_instance_count == 0)
        return;

    if (vis_id >= 3)
    {
        Msg("! [DetailComputeManager] Invalid vis_id: %u", vis_id);
        return;
    }

#if defined(USE_DX11)

    auto* context = HW.get_context(cmd_list.context_id);

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
    if (Device.dwFrame != last_frame && !(Device.dwFrame % 1000) && vis_id == 0)
    {
        // Read back indirect args to see what we're actually drawing
        ID3DBuffer* readback_buf = static_cast<ID3DBuffer*>(m_gpu.indirect_args_readback);
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

    cmd_list.SRVSManager.Apply(cmd_list.context_id);
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

    // PERFORMANCE FIX: Removed Flush() - it was forcing GPU to finish ALL pending work
    // The CopyResource creates an implicit dependency, so driver handles synchronization
    // If debug data seems stale, increase the readback interval instead of using Flush()

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

    // Debug: Print first 10 entries to see what's actually in the buffer
    Msg("  [DEBUG] First 10 debug entries:");
    for (u32 i = 0; i < _min(10u, m_instance_count); i++)
    {
        Msg("    [%u]: idx=%u, cull=%u, vis=%u, dist=%u",
            i, data[i].instance_idx, data[i].cull_reason, data[i].vis_id, data[i].dist_sqr);
    }

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
