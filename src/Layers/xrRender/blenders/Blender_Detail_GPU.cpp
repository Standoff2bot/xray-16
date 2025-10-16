#include "stdafx.h"
#pragma hdrstop

#include "Blender_Detail_GPU.h"

namespace xray::render::RENDER_NAMESPACE
{
CBlender_Detail_GPU::CBlender_Detail_GPU()
{
    description.CLS = 0; // Reuse B_DETAIL class ID
}

LPCSTR CBlender_Detail_GPU::getComment()
{
    return "INTERNAL: GPU instanced detail rendering";
}

#if RENDER == R_R4
void CBlender_Detail_GPU::Compile(CBlender_Compile& C)
{
    IBlender::Compile(C);

    VERIFY(!C.L_textures.empty());
    LPCSTR texture_path = C.L_textures[0].c_str();

    bool bUseATOC = (RImplementation.o.msaa_alphatest == CRender::MSAA_ATEST_DX10_0_ATOC);

    PIX_EVENT(INSTANCED_GRASS_BLENDER);

    switch (C.iElement)
    {
    case 0:
        if (!bUseATOC)
            break;

        {
            PIX_EVENT(INSTANCED_GRASS_RENDER_PASS_0);
            C.r_Pass("lod_gpu", "detail_gpu", FALSE, FALSE, FALSE);
            C.r_dx11Texture("s_base", texture_path);
            C.r_dx11Sampler("smp_base");

            C.r_Stencil(TRUE, D3DCMP_ALWAYS, 0xff, 0x7f, D3DSTENCILOP_KEEP, D3DSTENCILOP_REPLACE, D3DSTENCILOP_KEEP);
            C.r_End();
        }

        break;
    case 1:

    {
        PIX_EVENT(INSTANCED_GRASS_RENDER_PASS_1);

        C.r_Pass("lod_gpu", "detail_gpu", FALSE, FALSE, FALSE);
        C.r_dx11Texture("s_base", texture_path);
        C.r_dx11Sampler("smp_base");
        C.r_Stencil(TRUE, D3DCMP_ALWAYS, 0xff, 0x7f, D3DSTENCILOP_KEEP, D3DSTENCILOP_REPLACE, D3DSTENCILOP_KEEP);
        C.r_End();
    }
        break;
    }
}
#else
// R1/R2 implementation - not used for GPU instancing
void CBlender_Detail_GPU::Compile(CBlender_Compile& C)
{
    IBlender::Compile(C);
    // GPU instancing only supported in R4
}
#endif
} // namespace xray::render::RENDER_NAMESPACE
