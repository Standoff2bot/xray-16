# Physics engine selection
option(XRPHYSICS_USE_JOLT "Use Jolt Physics instead of ODE" OFF)
option(XRPHYSICS_USE_ODE "Use ODE Physics (legacy)" ON)

if(XRPHYSICS_USE_JOLT AND XRPHYSICS_USE_ODE)
    message(FATAL_ERROR "Cannot enable both XRPHYSICS_USE_JOLT and XRPHYSICS_USE_ODE simultaneously")
endif()

if(NOT XRPHYSICS_USE_JOLT AND NOT XRPHYSICS_USE_ODE)
    message(FATAL_ERROR "Must enable either XRPHYSICS_USE_JOLT or XRPHYSICS_USE_ODE")
endif()

add_library(xrPhysics)

target_sources_grouped(
    TARGET xrPhysics
    NAME "Kernel"
    FILES
    stdafx.cpp
    StdAfx.h
    xrPhysics.cpp
    xrPhysics.h
)

# Physics abstraction layer
target_sources_grouped(
    TARGET xrPhysics
    NAME "Physics\\Adapters"
    FILES
    adapters/IPhysicsAdapter.h
    adapters/PhysicsEngineRegistry.cpp
)

target_sources_grouped(
    TARGET xrPhysics
    NAME "Physics"
    FILES
    console_vars.cpp
    console_vars.h
    ShellHit.cpp
)

target_sources_grouped(
    TARGET xrPhysics
    NAME "Physics\\ActivationBox"
    FILES
    IActivationShape.cpp
    IActivationShape.h
    PHActivationShape.cpp
    PHActivationShape.h
)

target_sources_grouped(
    TARGET xrPhysics
    NAME "Physics\\Base"
    FILES
    DamageSource.h
    ode_include.h
    ode_redefine.h
    PhysicsCommon.h
    PhysicsExternalCommon.cpp
    PhysicsExternalCommon.h
)

target_sources_grouped(
    TARGET xrPhysics
    NAME "Physics\\Base\\Debug"
    FILES
    debug_output.cpp
    debug_output.h
)

target_sources_grouped(
    TARGET xrPhysics
    NAME "Physics\\Base\\Impact"
    FILES
    PHImpact.h
)

target_sources_grouped(
    TARGET xrPhysics
    NAME "Physics\\Base\\Objects"
    FILES
    PHDefs.h
)

target_sources_grouped(
    TARGET xrPhysics
    NAME "Physics\\Base\\Objects\\BaseShell"
    FILES
    IPhysicsShellHolder.h
    PhysicsShell.cpp
    PhysicsShell.h
    PhysicsShellScript.cpp
)

target_sources_grouped(
    TARGET xrPhysics
    NAME "Physics\\Base\\Objects\\BaseShell\\PhysicsShellAnimator"
    FILES
    PhysicsShellAnimatorBoneData.h
    PhysicsShellAnimator.cpp
    PhysicsShellAnimator.h
)

target_sources_grouped(
    TARGET xrPhysics
    NAME "Physics\\Base\\Objects\\BodyEffectors"
    FILES
    PHBaseBodyEffector.h
    PHContactBodyEffector.cpp
    PHContactBodyEffector.h
)

target_sources_grouped(
    TARGET xrPhysics
    NAME "Physics\\Base\\Objects\\Breakable"
    FILES
    PHFracture.cpp
    PHFracture.h
    PHJointDestroyInfo.cpp
    PHJointDestroyInfo.h
    PHShellSplitter.cpp
    PHShellSplitter.h
)

target_sources_grouped(
    TARGET xrPhysics
    NAME "Physics\\Base\\Objects\\CollideValidator"
    FILES
    ICollideValidator.h
    PHCollideValidator.cpp
    PHCollideValidator.h
)

target_sources_grouped(
    TARGET xrPhysics
    NAME "Physics\\Base\\Objects\\Disabling"
    FILES
    DisablingParams.cpp
    DisablingParams.h
    PHDisabling.cpp
    PHDisabling.h
)

target_sources_grouped(
    TARGET xrPhysics
    NAME "Physics\\Base\\Objects\\Element"
    FILES
    PHElement.cpp
    PHElement.h
    PHElementInline.h
    PHElementNetState.cpp
)

target_sources_grouped(
    TARGET xrPhysics
    NAME "Physics\\Base\\Objects\\Element\\Geom"
    FILES
    ExtendedGeom.cpp
    ExtendedGeom.h
    Geometry.cpp
    Geometry.h
    PHGeometryOwner.cpp
    PHGeometryOwner.h
)

target_sources_grouped(
    TARGET xrPhysics
    NAME "Physics\\Base\\Objects\\Joint"
    FILES
    PHJoint.cpp
    PHJoint.h
)

target_sources_grouped(
    TARGET xrPhysics
    NAME "Physics\\Base\\Objects\\MoveStorage"
    FILES
    PHMoveStorage.cpp
    PHMoveStorage.h
)

target_sources_grouped(
    TARGET xrPhysics
    NAME "Physics\\Base\\Objects\\PHCommander"
    FILES
    PHCommander.cpp
    PHCommander.h
    PHReqComparer.h
    PHScriptCall.cpp
    PHScriptCall.h
    PHSimpleCalls.cpp
    PHSimpleCalls.h
    PHSimpleCallsScript.cpp
)

target_sources_grouped(
    TARGET xrPhysics
    NAME "Physics\\Base\\Objects\\PHIsland"
    FILES
    PHIsland.cpp
    PHIsland.h
)

target_sources_grouped(
    TARGET xrPhysics
    NAME "Physics\\Base\\Objects\\PHNet"
    FILES
    ../xrServerEntities/PHNetState.cpp
    ../xrServerEntities/PHNetState.h
)

target_sources_grouped(
    TARGET xrPhysics
    NAME "Physics\\Base\\Objects\\PHObject"
    FILES
    PHObject.cpp
    PHObject.h
    PHUpdateObject.h
)

target_sources_grouped(
    TARGET xrPhysics
    NAME "Physics\\Base\\Objects\\PhyMove\\ActorCharacter"
    FILES
    PHActorCharacter.cpp
    PHActorCharacter.h
    PHActorCharacterInline.h
)

target_sources_grouped(
    TARGET xrPhysics
    NAME "Physics\\Base\\Objects\\PhyMove\\AICharacter"
    FILES
    PHAICharacter.cpp
    PHAICharacter.h
)

target_sources_grouped(
    TARGET xrPhysics
    NAME "Physics\\Base\\Objects\\PhyMove\\BaseCharacter"
    FILES
    IColisiondamageInfo.h
    MovementBoxDynamicActivate.cpp
    MovementBoxDynamicActivate.h
    PHCharacter.cpp
    PHCharacter.h
)

target_sources_grouped(
    TARGET xrPhysics
    NAME "Physics\\Base\\Objects\\PhyMove\\Capture"
    FILES
    IPHCapture.h
    PHCapture.cpp
    PHCapture.h
    PHCaptureInit.cpp
)

target_sources_grouped(
    TARGET xrPhysics
    NAME "Physics\\Base\\Objects\\PhyMove\\SimpleCharacter"
    FILES
    ElevatorState.cpp
    ElevatorState.h
    IClimableObject.h
    IElevatorState.h
    PHSimpleCharacter.cpp
    PHSimpleCharacter.h
    PHSimpleCharacterInline.h
)

target_sources_grouped(
    TARGET xrPhysics
    NAME "Physics\\Base\\Objects\\Shell"
    FILES
    PHShell.cpp
    PHShell.h
    PHShellActivate.cpp
    PHShellBuildJoint.h
    PHShellNetState.cpp
)

target_sources_grouped(
    TARGET xrPhysics
    NAME "Physics\\Base\\Objects\\SplitedShell"
    FILES
    PHSplitedShell.cpp
    PHSplitedShell.h
)

target_sources_grouped(
    TARGET xrPhysics
    NAME "Physics\\Base\\Objects\\StaticShell"
    FILES
    IPHStaticGeomShell.h
    PHStaticGeomShell.cpp
    PHStaticGeomShell.h
)

target_sources_grouped(
    TARGET xrPhysics
    NAME "Physics\\Base\\Objects\\utils"
    FILES
    ph_valid_ode.h
    phvalide.cpp
    phvalide.h
    PHValideValues.h
)

target_sources_grouped(
    TARGET xrPhysics
    NAME "Physics\\Base\\Objects\\utils\\Interpolation"
    FILES
    PHInterpolation.cpp
    PHInterpolation.h
)

target_sources_grouped(
    TARGET xrPhysics
    NAME "Physics\\Base\\Objects\\utils\\Math"
    FILES
    CalculateTriangle.h
    MathUtils.cpp
    MathUtils.h
    MathUtilsOde.h
    matrix_utils.h
    PHDynamicData.cpp
    PHDynamicData.h
    SpaceUtils.h
)

target_sources_grouped(
    TARGET xrPhysics
    NAME "Physics\\Base\\Objects\\utils\\Storage"
    FILES
    BlockAllocator.h
    CycleConstStorage.h
)

target_sources_grouped(
    TARGET xrPhysics
    NAME "Physics\\Base\\Objects\\World"
    FILES
    GeometryBits.cpp
    GeometryBits.h
    IPHWorld.h
    params.cpp
    params.h
    PHItemList.h
    PHWorld.cpp
    PHWorld.h
    PHWorldScript.cpp
    Physics.cpp
    Physics.h
)

target_sources_grouped(
    TARGET xrPhysics
    NAME "Physics\\Camera collision"
    FILES
    ActorCameraCollision.cpp
    ActorCameraCollision.h
)

target_sources_grouped(
    TARGET xrPhysics
    NAME "Physics\\colliders\\cyl"
    FILES
    dcylinder/dCylinder.cpp
    dcylinder/dCylinder.h
)

target_sources_grouped(
    TARGET xrPhysics
    NAME "Physics\\colliders\\RayMotions"
    FILES
    dRayMotions.cpp
    dRayMotions.h
)

target_sources_grouped(
    TARGET xrPhysics
    NAME "Physics\\colliders\\tri"
    FILES
    tri-colliderknoopc/__aabb_tri.h
    tri-colliderknoopc/dcTriangle.h
    #tri-colliderknoopc/dcTriListCollider.cpp
    #tri-colliderknoopc/dcTriListCollider.h
    tri-colliderknoopc/dSortTriPrimitive.cpp
    tri-colliderknoopc/dSortTriPrimitive.h
    tri-colliderknoopc/dTriBox.cpp
    tri-colliderknoopc/dTriBox.h
    tri-colliderknoopc/dTriCallideK.cpp
    tri-colliderknoopc/dTriCollideK.h
    tri-colliderknoopc/dTriColliderCommon.h
    tri-colliderknoopc/dTriColliderMath.h
    tri-colliderknoopc/dTriCylinder.cpp
    tri-colliderknoopc/dTriCylinder.h
    tri-colliderknoopc/dTriList.cpp
    tri-colliderknoopc/dTriList.h
    tri-colliderknoopc/dTriSphere.cpp
    tri-colliderknoopc/dTriSphere.h
    tri-colliderknoopc/dxTriList.h
    tri-colliderknoopc/TriPrimitiveCollideClassDef.h
)

target_sources_grouped(
    TARGET xrPhysics
    NAME "Physics\\Damage receiver"
    FILES
    collisiondamagereceiver.cpp
    icollisiondamagereceiver.h
)

# Jolt Physics specific sources
if(XRPHYSICS_USE_JOLT)
    target_sources_grouped(
        TARGET xrPhysics
        NAME "Physics\\Adapters\\Jolt"
        FILES
        adapters/Jolt/JoltPhysicsWorld.h
        adapters/Jolt/JoltPhysicsWorld.cpp
        adapters/Jolt/JoltPhysicsBody.h
        adapters/Jolt/JoltPhysicsBody.cpp
        adapters/Jolt/JoltPhysicsShape.h
        adapters/Jolt/JoltPhysicsShape.cpp
        adapters/Jolt/JoltPhysicsConstraint.h
        adapters/Jolt/JoltPhysicsConstraint.cpp
        adapters/Jolt/JoltNetworkSync.h
        adapters/Jolt/JoltNetworkSync.cpp
    )
endif()

target_include_directories(xrPhysics
    PUBLIC
    "${CMAKE_SOURCE_DIR}/Externals"

    PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_SOURCE_DIR}/Externals/imgui"
)

# Add physics engine specific includes
if(XRPHYSICS_USE_ODE)
    target_include_directories(xrPhysics
        PRIVATE
        "${CMAKE_SOURCE_DIR}/Externals/ode/include"
    )
endif()

if(XRPHYSICS_USE_JOLT)
    target_include_directories(xrPhysics
        PRIVATE
        "${CMAKE_SOURCE_DIR}/Externals/JoltPhysics"
    )
endif()

target_link_libraries(xrPhysics
    PRIVATE
    xrCore
    xrEngine
    xrMaterialSystem
    xrMiscMath
    xrScriptEngine
    xrAPI
    xrCDB
    xrSound
)

# Link physics engine
if(XRPHYSICS_USE_ODE)
    target_link_libraries(xrPhysics PUBLIC xrODE)
endif()

if(XRPHYSICS_USE_JOLT)
    # Configure Jolt build
    set(INTERPROCEDURAL_OPTIMIZATION OFF CACHE BOOL "Disable IPO for Jolt" FORCE)
    set(USE_STATIC_MSVC_RUNTIME_LIBRARY OFF CACHE BOOL "Don't use static MSVC runtime" FORCE)
    set(CROSS_PLATFORM_DETERMINISTIC OFF CACHE BOOL "Disable cross-platform deterministic" FORCE)
    set(DOUBLE_PRECISION OFF CACHE BOOL "Use single precision" FORCE)
    set(GENERATE_DEBUG_SYMBOLS ON CACHE BOOL "Generate debug symbols" FORCE)
    set(OVERRIDE_CXX_FLAGS OFF CACHE BOOL "Don't override CXX flags" FORCE)
    set(BUILD_SHARED_LIBS_SAVED ${BUILD_SHARED_LIBS})
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build Jolt as static library" FORCE)

    add_subdirectory("${CMAKE_SOURCE_DIR}/Externals/JoltPhysics/Build" JoltPhysics EXCLUDE_FROM_ALL)

    set(BUILD_SHARED_LIBS ${BUILD_SHARED_LIBS_SAVED} CACHE BOOL "Restore BUILD_SHARED_LIBS" FORCE)

    target_link_libraries(xrPhysics PRIVATE Jolt)
endif()

target_compile_definitions(xrPhysics
    PRIVATE
    XRPHYSICS_EXPORTS
    PURE_DYNAMIC_CAST
)

# Physics engine compile definitions
if(XRPHYSICS_USE_JOLT)
    target_compile_definitions(xrPhysics PRIVATE XRPHYSICS_JOLT=1)
    message(STATUS "xrPhysics: Using Jolt Physics engine")
endif()

if(XRPHYSICS_USE_ODE)
    target_compile_definitions(xrPhysics PRIVATE XRPHYSICS_ODE=1)
    message(STATUS "xrPhysics: Using ODE Physics engine (legacy)")
endif()

set_target_properties(xrPhysics PROPERTIES
    PREFIX ""
)

target_precompile_headers(xrPhysics
    PRIVATE
    StdAfx.h
)

if (BUILD_SHARED_LIBS)
    install(TARGETS xrPhysics LIBRARY
        DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    )
endif()
