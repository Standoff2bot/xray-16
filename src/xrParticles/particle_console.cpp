#include "stdafx.h"
#include "particle_console.h"
#include "xrEngine/XR_IOConsole.h"
#include "xrEngine/xr_ioc_cmd.h"

// Token definitions
const xr_token particle_implementation_token[] = {
    { "vanilla", 0 },
    { "ecs",     1 },
    { nullptr,   0 }
};

// Console variable definitions
int ps_particle_implementation = 0;  // Default to vanilla implementation

// Console command classes
class CCC_ParticleImplementation : public CCC_Token
{
public:
    CCC_ParticleImplementation(LPCSTR N, int* V, const xr_token* T) : CCC_Token(N, (u32*)V, T) {}

    void Execute(LPCSTR args) override
    {
        CCC_Token::Execute(args);
        Msg("! Particle implementation set to: %s", args);
        Msg("! Note: Particle implementation change requires engine restart to take full effect");
        Msg("! Active particles will be cleared on manager recreation");
    }
};

class CCC_ParticleStats : public IConsole_Command
{
public:
    CCC_ParticleStats(LPCSTR N) : IConsole_Command(N) { bEmptyArgsHandled = true; }

    void Execute(LPCSTR /*args*/) override
    {
        Msg("=== Particle System Statistics ===");
        Msg("Implementation: %s", ps_particle_implementation == 0 ? "Vanilla" : "ECS");
        // TODO: Add more detailed statistics when manager is implemented
        Msg("==================================");
    }
};

void xrParticles_initconsole()
{
    CMD3(CCC_ParticleImplementation, "ps_particle_implementation", &ps_particle_implementation, particle_implementation_token);
    CMD1(CCC_ParticleStats, "ps_particle_stats");

    Msg("* xrParticles console commands initialized");
}
