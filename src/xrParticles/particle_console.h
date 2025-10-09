#pragma once

#include "xrCore/xr_token.h"

// Token values for particle implementation selection
extern PARTICLES_API const xr_token particle_implementation_token[];

// Console variables for particle system
extern PARTICLES_API int ps_particle_implementation;  // 0 = vanilla, 1 = ECS

// Initialize particle console commands
void xrParticles_initconsole();
