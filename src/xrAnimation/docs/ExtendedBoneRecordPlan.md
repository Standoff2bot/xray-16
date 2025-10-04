Extended BoneRecord Plan

## Status – 2025-10-02
- `.ozzx` bundle header now targets version 2; a tagged metadata block carries the extended bone payload for every joint.
- Converter emits rest-length, dominant axis, collision hints, and physics descriptors; runtime hydrates `CBoneData` via `ApplyExtendedBoneMetadata`.
- Parity automation covers metadata hydration through `OzzKinematicsAppliesBoneMetadata` and converter bundle tests.

- New structure layout (converter side)

  struct BoneRecord
  {
  // existing fields
  std::string name;
  std::string parent_name;
  int parent_index{-1};
  Fmatrix local_transform{}; // rest local from IK chunk
  Fmatrix global_transform{}; // accumulated rest transform
  float mass{0.f};
  Fvector center_of_mass{};
  SBoneShape shape{};
  SJointIKData joint_ik{};
  shared_str game_mtl;

      // newly synthesised data
      float rest_length{0.f};                  // distance from parent to child origin
      Fvector dominant_axis{};                 // normalized axis of max extent in local space
      Fvector local_aabb_min{}, local_aabb_max{}; // AABB enclosing the bone in its parent space
      Fmatrix inverse_global_transform{};      // cached inverse for quick world→bone
      Fvector inertia_tensor{};                // diagonal inertia approximation from shape & mass
      float volume{0.f};                       // derived from shape
      Flags32 collision_layers;                // synthesized group bits (soft tissue, rigid, weapon…)
      bool ground_contact_candidate{false};    // heuristics (feet, claws)
      bool weapon_anchor_candidate{false};     // heuristics (hands, weapon bones)

  };

- How we compute the extras
  1. rest_length
  2. dominant_axis
     Transform basis vectors of the shape (box/cylinder) into parent space and pick the axis with largest extent; fallback to normalized translation from parent to child.
  3. local AABB
     Sample the collision shape (box corners, sphere radius, cylinder hull), transform by local_transform, and record min/max. This gives quick volume queries for penetration or footstep IK.
  4. inverse_global_transform
     Invert global_transform once here; runtime can use it for contact projections.
  5. volume + inertia_tensor
     Use analytic formulas per shape type (box: 8\*halfExtent, sphere: (4/3)πr³, cylinder: πr²h). Combine with mass to spit out moment of inertia coefficients; these feed ragdoll tuning.
  6. collision_layers
     Before proceeding- let's clarify what exactly these collision layers will be used for. Start with a simple heuristic: map game_mtl or bone name to layer bits (e.g., "bone_l_hand" → weapon layer, "bip01_spine" → torso). Expose a table so designers can adjust later.
  7. ground_contact_candidate
     Flag feet/toes by name or by checking that the dominant axis lines up with -Y and the global height is near the overall minimum.
  8. weapon_anchor_candidate
     Identify hand or weapon bones via naming (weapon, hand, wpn) plus checking for child meshes tagged as weapon. Set the flag for future IK or attachments. Need to inspect existing meshes for non-humanoid and monsters as well. System should be robust enough to handle all these cases.
- Converter workflow change
  1. Parsing – while reading IK data, populate the added fields on BoneRecord.
  2. Post-processing – after hierarchy + transforms are known, run the heuristics (length, AABB, inertia, layer classification).
  3. Bundle serialization – append a new “bone metadata” section: count + array of structs (shape data already serialized? store as raw SBoneShape, SJointIKData, strings, floats, flags).
  4. Runtime hydration – COzzKinematicsVisual reads the block, fills CBoneData members, and caches helper values (e.g., inertia) for physics.

A few ideas worth baking in while we’re already touching the converter:

- Bone-space geometry cues. From the rest pose we can compute bone lengths, dominant axes, and local AABBs. Having those numbers serialized once lets gameplay and IK code reason about limb reach, footprint size, or alignment without recomputing per load.
- Mass/inertia scaffolding. Once shapes are mirrored, we can auto-derive inertia tensors, volume estimates, and mass fractions from the collision primitives. That gives ragdoll tuning a sane starting point and opens the door for future mass-redistribution or damage-force logic.
- Attachment/anchor hints. We can mark “ground-contact” or “weapon-grip” candidates by analysing bone names plus mesh weighting (e.g., foot bones whose transforms align with -Y and influence the sole). Storing those anchors makes footstep IK, climbing, or weapon alignment easier to prototype.
- Collision layer defaults. Synthesize suggested collision groups (soft tissue, rigid prop, weapon) based on material strings or hierarchy, so downstream systems can opt into richer contact responses without hard-coding bone lists.

All of these can be emitted alongside the existing physics metadata in the .ozzx bundle. They’re optional—legacy behaviour ignores them—but they give us headroom for richer IK, ragdoll, or gameplay features later without revisiting the conversion pipeline again.

Physics Metadata Parity Plan

- Current Gap
  game_mtl_name.clear(), mass = 0, etc.). Legacy code populates those fields in SkeletonCustom.cpp from the IK chunk (OGF_S_IKDATA). Because they stay empty in the Ozz path, has_physics_collision_shapes(\*K) fails on doors and ragdolls.
- Converter Enhancements
  - Read the IK chunk once and capture its data in a richer DTO. LoadSkeletonBonesFromOgf already reads the chunk for rest pose; extend BoneRecord to carry SBoneShape, SJointIKData, game_mtl_name, mass, center_of_mass.
  - Exported .ozzx should contain updated BoneRecord with all this new data.
- Bundle Format Update
  - Bump .ozzx version and append a custom block for per-bone physics metadata (e.g., count + array of serialized SBoneShape, SJointIKData, material string, mass/CoM).
  - Update WriteOzzxBundle / ReadOzzxBundle to emit and consume the block; keep backward compatibility (omit block for older version).
- Runtime Hydration
  - COzzKinematicsVisual::InitializeFromPayload should hydrate those structures after InitializeFromOzzBuffer. Populate bones[bone_id]->shape, game_mtl_name, game_mtl_idx (via GMLib.GetMaterialIdx), IK_data, mass, center_of_mass.
  - Leave the existing hierarchy/transform logic alone; we’re just filling the metadata that physics queries expect.
- Validation
  - update ozz_animation_viewer IK code first, so we can test there more quickly. We can build out code for IK targets, ragdoll etc, and then base our engine code on that logic later on.
  - Add parity tests in ozz_kinematics_parity_tests that load a known .ozzx bundle and compare bone shapes/material indices against the legacy CKinematics.
  - Run targeted game smoke tests (doors with physics shells, ragdolls) once the converter and runtime changes land.
