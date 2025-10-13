#include "StdAfx.h"
#include "PhysicsShell.h"
#include "PHInterpolation.h"
#include "PHObject.h"
#include "PHWorld.h"
#include "PHShell.h"

void CPHShell::net_Import(NET_Packet& P)
{
    const u8 mode = P.r_u8();
#ifdef XRPHYSICS_JOLT
    if (mode == 1)
    {
        m_jolt_net_state.Read(P);
        if (m_jolt_ragdoll)
            m_jolt_net_state.Apply(*m_jolt_ragdoll);
        return;
    }
#endif

    auto i = elements.begin(), e = elements.end();
    for (; i != e; ++i)
    {
        (*i)->net_Import(P);
    }
}

void CPHShell::net_Export(NET_Packet& P)
{
#ifdef XRPHYSICS_JOLT
    if (m_jolt_ragdoll)
    {
        P.w_u8(1);
        m_jolt_net_state.Capture(*m_jolt_ragdoll);
        m_jolt_net_state.Write(P);
        return;
    }
#endif

    P.w_u8(0);
    auto i = elements.begin(), e = elements.end();
    for (; i != e; ++i)
    {
        (*i)->net_Export(P);
    }
}
