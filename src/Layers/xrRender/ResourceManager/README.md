# Modern ResourceManager

## Overview

This is the **new modern ResourceManager** designed for the FrameGraph-based rendering path. It is completely separate from the legacy `ResourceManager.cpp` in the parent directory.

## Architecture

```
ResourceManager (Top Level)
├── TextureManager (Texture streaming, memory management)
├── BufferManager (Static + dynamic buffers, ring allocation)
└── SamplerCache (Sampler state deduplication)
```

## Key Features

### TextureManager
- **Generational Handles**: Detect use-after-free bugs
- **Streaming System**: Load high mips first, stream in detail progressively
- **Memory Budget**: Enforce VRAM limits with LRU eviction
- **Priority System**: Critical > High > Medium > Low > VeryLow
- **Async Loading**: Non-blocking texture loads (Week 3)

### BufferManager
- **Static Buffers**: Geometry buffers (vertex, index)
- **Dynamic Buffers**: Per-frame data via ring buffer allocation
- **Ring Buffers**: Efficient circular allocation for constants
- **Zero-copy**: Persistent CPU mapping for dynamic data

### SamplerCache
- **Deduplication**: Hash-based sampler reuse
- **Common Samplers**: LinearClamp, LinearWrap, AnisotropicWrap, etc.

## Integration with Legacy Code

**IMPORTANT**: This manager is **ONLY** used when `psDeviceFlags.test(rsUseFrameGraph)` is true.

```cpp
if (psDeviceFlags.test(rsUseFrameGraph)) {
    // Use modern path
    auto* texManager = modernResourceManager->GetTextureManager();
    TextureHandle handle = texManager->LoadTexture("textures/base.dds");
} else {
    // Use legacy path
    ref_texture tex = Device.Resources->_CreateTexture("base");
}
```

## File Structure

- `ResourceHandle.h` - Generational handle types
- `TextureManager.h/.cpp` - Texture management and streaming
- `BufferManager.h/.cpp` - Buffer allocation and ring buffers
- `SamplerCache.h/.cpp` - Sampler state caching
- `DDSLoader.h/.cpp` - DDS file format parser
- `TextureStreaming.h/.cpp` - Mip streaming system (Week 2)
- `TestTextureManager.cpp` - Unit tests

## Implementation Timeline

- **Week 1**: Core handle system, TextureManager skeleton, DDS loading
- **Week 2**: Streaming, memory management, BufferManager, SamplerCache
- **Week 3**: Async I/O, threading
- **Week 4**: FrameGraph integration

## Usage Example

```cpp
// Initialize (once at startup)
RenderDevice device;
ResourceManager resourceManager(&device);

// Load texture
auto* texManager = resourceManager.GetTextureManager();
TextureHandle handle = texManager->LoadTexture(
    "textures/concrete.dds",
    TexturePriority::High
);

// Use in rendering
nvrhi::ITexture* nvrhiTex = texManager->GetNVRHITexture(handle);
renderContext.SetTexture(0, nvrhiTex);

// Request more mips (streaming)
texManager->RequestMips(handle, 8);

// Cleanup
texManager->Release(handle);
```

## Performance Goals

- **Load Time**: <100ms for 1024x1024 BC1 texture
- **Memory Overhead**: <1KB per texture metadata
- **Handle Validation**: <10 CPU cycles
- **Streaming Bandwidth**: 32 MB/frame default

## Status

- ✅ Directory structure created
- ⏳ Week 1 implementation in progress
- ⏳ Week 2-4 pending

## References

- NVIDIA RTX Texture Streaming SDK (2025)
- Disney Animation SIGGRAPH 2025 Texture Streaming Pipeline
- [FrameGraph docs](../../framegraph_docs/)
