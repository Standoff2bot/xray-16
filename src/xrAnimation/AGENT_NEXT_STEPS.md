# AGENT_NEXT_STEPS

**Last Updated:** 2025-10-09
**Status:** Production-ready runtime complete, optimization phase begins

---

## Current Status - Production Success ✅

- ✅ **MVP delivered**: Runtime fully functional in-game
- ✅ **Asset conversion**: All OGF/OMF converted at startup
- ✅ **Animation playback**: All converted assets animate correctly
- ✅ **Memory stable**: No leaks or crashes detected
- ✅ **Real-time swapping**: Legacy → ozz replacement works seamlessly

---

## Immediate Focus: Performance Optimization

The runtime is working. Now we optimize for scale.

**Goal**: Achieve **1000+ animated characters** with parallel processing

**Reference**: See `ROADMAP.md` for detailed 8-week optimization plan

---

## Phase 5: Parallel Startup Conversion (Weeks 1-2)

### Week 1: Thread Pool Implementation

**File**: `src/xrAnimation/OzzConversionThreadPool.h/cpp`

#### Tasks
1. Create thread pool for asset conversion
   - Use `std::thread::hardware_concurrency()` for thread count
   - Task queue with `std::mutex` protection
   - Progress tracking (`std::atomic<int>` for completed/total)

2. Batch scheduling for OGF/OMF conversion
   - Scan `gamedata/` for unconverted assets
   - Submit independent conversion tasks
   - No dependencies between tasks = perfect parallelism

3. Cache invalidation system
   - Check file timestamps (OGF vs OZZX)
   - Skip conversion if output is newer
   - Respect `-noprefetch` flag

**Success Criteria**:
- ✅ 100 assets convert in <1s (8-core system)
- ✅ Linear scaling with core count
- ✅ Progress shown in loading screen
- ✅ No corruption or race conditions

---

## Phase 6: Parallel Animation Processing (Weeks 3-4)

### Week 3: Per-Character Parallel Update

**Pattern**: Based on `Externals/ozz-animation/samples/multithread/sample_multithread.cc`

#### Key Insight from ozz-animation

All ozz jobs are **thread-safe by design**:
- `ozz::animation::SamplingJob`
- `ozz::animation::BlendingJob`
- `ozz::animation::LocalToModelJob`

**Why?** Data-driven architecture - clear separation between data (buffers) and processes (jobs)

#### Implementation Pattern

**Data Isolation** (per-character):
```cpp
struct OzzCharacterData {
    // Thread-local mutable data
    ozz::animation::SamplingJob::Context context;
    ozz::vector<ozz::math::SoaTransform> locals;
    ozz::vector<ozz::math::Float4x4> models;

    // Shared read-only data (safe!)
    const ozz::animation::Skeleton* skeleton;
    const ozz::animation::Animation* animation;
};
```

**Recursive Parallel-For**:
```cpp
bool ParallelUpdateCharacters(Args args, OzzCharacterData* chars, int num) {
    if (num <= grain_size) {
        // Sequential batch
        for (int i = 0; i < num; ++i) {
            UpdateCharacter(&chars[i]);
        }
    } else {
        // Split: half async, half this thread
        int half = num / 2;
        auto future = std::async(std::launch::async, ParallelUpdateCharacters,
                                args, chars + half, num - half);
        ParallelUpdateCharacters(args, chars, half);
        return future.get();
    }
}
```

**Grain Size Tuning**:
- Default: 64-128 characters per task
- Simple anims: 128
- Complex blend: 64
- Full-body IK: 32

**Success Criteria**:
- ✅ 500 simple characters @ 60 FPS
- ✅ 200 blended characters @ 60 FPS
- ✅ Animation update < 4ms frame budget
- ✅ Thread utilization > 70%

---

### Week 4: SIMD Optimization

#### Tasks
1. Verify SoA layout usage
   - Use `ozz::vector<ozz::math::SoaTransform>` (CORRECT)
   - NOT `ozz::vector<ozz::math::Transform>` (no SIMD)

2. Profile SIMD utilization
   - Target: >60% SIMD instruction usage
   - Use performance profiler to verify

3. Benchmark suite
   - Automated tests for 100/500/1000 characters
   - Assert frame time budgets
   - Run on CI

**Success Criteria**:
- ✅ SIMD utilization > 60%
- ✅ Benchmark suite passing
- ✅ No regressions vs baseline

---

## Phase 7: Animation Polish (Weeks 5-6)

### Week 5: Blending & Partitions

**Pattern**: Based on `Externals/ozz-animation/samples/partial_blend/`

#### Per-Joint Weight Masks

Correct partition-aware blending requires per-joint weights:

```cpp
// Get partition mask (torso, legs, head)
auto torso_mask = partition_masks_.GetMask(TORSO_PARTITION);

// Setup blending layer with mask
ozz::animation::BlendingJob::Layer layer;
layer.transform = sampled_locals;
layer.weight = 1.0f;
layer.joint_weights = torso_mask;  // Per-joint weights!
```

#### Smooth Transitions

Use blend curves for cross-fades:

```cpp
float ComputeBlendWeight(float t, float duration) {
    float ratio = t / duration;
    // Smoothstep (ease in/out)
    return ratio * ratio * (3.0f - 2.0f * ratio);
}
```

**Success Criteria**:
- ✅ No visible pops in transitions
- ✅ Torso/legs independent blending works
- ✅ Partition masks < 0.1ms compute time

---

### Week 6: Callback Timing

#### Tasks
1. Precise motion mark callbacks
   - Track `prev_ratio` vs `current_ratio`
   - Detect threshold crossings
   - Fire callbacks within 1 frame accuracy

2. Loop point detection
   - Detect `current_ratio < prev_ratio` (wrap-around)
   - Fire loop callbacks

3. Rapid state change buffering
   - Queue state changes to avoid conflicts
   - Process queue after animation update

**Success Criteria**:
- ✅ Motion marks fire within 1 frame
- ✅ Loop callbacks work correctly
- ✅ No dropped callbacks during rapid changes

---

## Phase 8: ECS Preparation (Weeks 7-8)

### Week 7: Data-Oriented Refactoring

#### Goals
- Separate data from logic
- Reduce virtual calls in hot paths
- Prepare component-based layout

#### Component-Style Data

```cpp
// Future ECS components
struct AnimationComponent {
    SharedOzzSkeleton skeleton;
    SharedOzzAnimation current_anim;
    float time_ratio;
};

struct AnimationBuffersComponent {
    ozz::vector<SoaTransform> locals;
    ozz::vector<Float4x4> models;
    SamplingContext context;
};
```

**Success Criteria**:
- ✅ Virtual calls reduced by 50%
- ✅ System-based update prototype works
- ✅ No performance regression

---

### Week 8: Shared Motion Container (DEFERRED)

**Decision**: Defer to ECS migration

**Why?**:
- Current per-instance motion libraries work fine
- Memory usage acceptable for current load
- ECS will change memory layout requirements anyway
- Avoid premature optimization

**When to implement**:
- During ECS migration
- When memory profiling shows duplication issues
- When supporting 500+ unique character types

**Design notes**: See `ROADMAP.md` Phase 8.2

---

## Performance Targets Summary

### Startup
- **Current**: 2-5s for 100 assets (single-threaded)
- **Target**: <1s for 100 assets (8-core parallel)
- **Strategy**: Thread pool + recursive task splitting

### Runtime
- **Current**: ~50 characters functional (not optimized)
- **Target**: 500-1000 characters @ 60 FPS
- **Strategy**: Parallel update + SIMD optimization

### Quality
- **Current**: Animations play, some blend pops
- **Target**: Smooth transitions, correct partitions
- **Strategy**: Per-joint masks + blend curves

---

## Implementation Checklist

### Phase 5 (Weeks 1-2)
- [ ] Create `OzzConversionThreadPool.h/cpp`
- [ ] Implement task queue with `std::mutex`
- [ ] Add progress tracking (`std::atomic`)
- [ ] Integrate with `IGame_Persistent::OnGameStart()`
- [ ] Add cache invalidation (timestamp checks)
- [ ] Hook progress to `LoadTitle()`
- [ ] Test with 100/500 asset sets
- [ ] Verify no race conditions (thread sanitizer)

### Phase 6 (Weeks 3-4)
- [ ] Create `OzzParallelUpdate.h/cpp`
- [ ] Implement per-character data isolation
- [ ] Implement recursive parallel-for
- [ ] Add grain size tuning (console vars)
- [ ] Integrate with `CLevel::Update()`
- [ ] Profile SIMD utilization
- [ ] Create benchmark suite
- [ ] Run performance tests on CI

### Phase 7 (Weeks 5-6)
- [ ] Create `OzzPartitionMasks.h/cpp`
- [ ] Implement per-joint weight masks
- [ ] Add smoothstep blend curves
- [ ] Fix callback timing (ratio tracking)
- [ ] Add rapid state change queue
- [ ] Test partition-aware blending
- [ ] Verify callback precision

### Phase 8 (Weeks 7-8)
- [ ] Design component-based data layout
- [ ] Prototype system-based update
- [ ] Cache virtual function pointers
- [ ] Profile virtual call overhead
- [ ] Document ECS migration path
- [ ] **DEFER**: Shared motion container

---

## Risk Management

### Risk: Thread Safety Issues
- **Mitigation**: Follow ozz patterns exactly (proven thread-safe)
- **Testing**: Thread sanitizer on CI
- **Fallback**: Console var to disable multi-threading

### Risk: Performance Regression
- **Mitigation**: Benchmark suite with automated tests
- **Testing**: Profile every major change
- **Fallback**: Grain size tuning to reduce overhead

### Risk: Memory Overhead
- **Mitigation**: Buffers sized exactly to skeleton
- **Testing**: Memory profiler in nightly builds
- **Fallback**: Dynamic character count reduction

---

## Key Resources

### ozz-animation Samples (Study These!)
- **Multithread**: `Externals/ozz-animation/samples/multithread/`
  - Shows 4096 characters with parallel processing
  - Recursive parallel-for pattern
  - Grain size tuning

- **Millipede**: `Externals/ozz-animation/samples/millipede/`
  - Up to 1023 joints per skeleton
  - Procedural animation generation
  - Large-scale character demo

- **Partial Blend**: `Externals/ozz-animation/samples/partial_blend/`
  - Per-joint weight masks
  - Partition-aware blending
  - Upper/lower body independent animations

- **Optimize**: `Externals/ozz-animation/samples/optimize/`
  - Keyframe reduction
  - Tolerance tuning
  - Memory footprint optimization

### Current Implementation
- **Runtime**: `src/xrAnimation/OzzKinematicsAnimated.cpp`
- **Visual**: `src/Layers/xrRender/OzzKinematicsVisual.cpp`
- **Converter**: `tools/xray_to_ozz_converter/`
- **Tests**: `src/xrAnimation/tests/`

### Documentation
- **ROADMAP.md**: Detailed 8-week optimization plan
- **IMPLEMENTATION_STATUS.md**: Current status and completed phases
- **README.md**: High-level overview and usage
- **AGENT_DOCS.md**: Session context and tooling reference

---

## Next Immediate Actions

1. **Read ROADMAP.md** for detailed implementation patterns
2. **Study ozz multithread sample** (`sample_multithread.cc`)
3. **Start Phase 5.1**: Thread pool for startup conversion
4. **Benchmark current state**: Baseline performance metrics

---

## Success Definition

**We'll know we're done when:**
- ✅ Game starts in <1s with 100+ assets (parallel conversion)
- ✅ 500+ characters animate at 60 FPS (parallel update)
- ✅ Smooth blending with no visible pops (polish)
- ✅ Correct partition-aware playback (torso/legs/head)
- ✅ Architecture ready for ECS migration (data-oriented)

**Stretch Goal:**
- 🎯 1000+ characters @ 60 FPS (8-16 core system)
- 🎯 Sub-2ms animation update budget
- 🎯 Full ECS integration with shared motion container
