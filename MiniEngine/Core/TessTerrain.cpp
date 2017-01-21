// Copyright (c) 2011 NVIDIA Corporation. All rights reserved.
//
// TO  THE MAXIMUM  EXTENT PERMITTED  BY APPLICABLE  LAW, THIS SOFTWARE  IS PROVIDED
// *AS IS*  AND NVIDIA AND  ITS SUPPLIERS DISCLAIM  ALL WARRANTIES,  EITHER  EXPRESS
// OR IMPLIED, INCLUDING, BUT NOT LIMITED  TO, NONINFRINGEMENT,IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.  IN NO EVENT SHALL  NVIDIA 
// OR ITS SUPPLIERS BE  LIABLE  FOR  ANY  DIRECT, SPECIAL,  INCIDENTAL,  INDIRECT,  OR  
// CONSEQUENTIAL DAMAGES WHATSOEVER (INCLUDING, WITHOUT LIMITATION,  DAMAGES FOR LOSS 
// OF BUSINESS PROFITS, BUSINESS INTERRUPTION, LOSS OF BUSINESS INFORMATION, OR ANY 
// OTHER PECUNIARY LOSS) ARISING OUT OF THE  USE OF OR INABILITY  TO USE THIS SOFTWARE, 
// EVEN IF NVIDIA HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGES.
//
// Please direct any bugs or questions to SDKFeedback@nvidia.com

#include "pch.h"
#include "TessTerrain.h"
#include "BufferManager.h"

#include "CompiledShaders\InitializationVS.h"
#include "CompiledShaders\InitializationPS.h"
#include "CompiledShaders\GradientPS.h"
#include "CompiledShaders\HwTessellationPassThruVS.h"
#include "CompiledShaders\TerrainScreenspaceLODHS.h"
#include "CompiledShaders\TerrainDisplaceDS.h"
#include "CompiledShaders\SmoothShadePS.h"
#include "CompiledShaders\VTFDisplacementVS.h"
#include "CompiledShaders\GSSolidWire.h"
#include "CompiledShaders\PSSolidWire.h"

BoolVar g_HwTessellation("Terrain/HW Tessellation", true);
IntVar g_TessellatedTriWidth("Terrain/Tessellated Triangle Width", 6, 1, 100);

BoolVar g_TerrainWireframe("Terrain/Wireframe", false);
NumVar g_WireframeAlpha("Terrain/Wireframe Alpha", 0.5f, 0, 5, 0.1f);
BoolVar g_DebugDrawPatches("Terrain/Debug Draw Patches", false);
BoolVar g_DebugDrawTiles("Terrain/Debug Draw Tiles", false);

static const int MAX_OCTAVES = 15;
IntVar g_RidgeOctaves("Terrain/Ridge Octaves", 3, 1, MAX_OCTAVES);
IntVar g_fBmOctaves("Terrain/fBm Octaves", 3, 1, MAX_OCTAVES);
IntVar g_TexTwistOctaves("Terrain/Tex Twist Octaves", 1, 1, MAX_OCTAVES);
IntVar g_DetailNoiseScale("Terrain/Detail Noise Scale", 20, 1, 200);

struct Adjacency
{
	// These are the size of the neighbours along +/- x or y axes.  For interior tiles
	// this is 1.  For edge tiles it is 0.5 or 2.0.
	float neighbourMinusX;
	float neighbourMinusY;
	float neighbourPlusX;
	float neighbourPlusY;
};

struct InstanceData
{
	float x,y;
	Adjacency adjacency;
};

TileRing::TileRing(int holeWidth, int outerWidth, float tileSize):
	m_holeWidth(holeWidth),
	m_outerWidth(outerWidth), 
	m_ringWidth((outerWidth - holeWidth) / 2),				// No remainder - see assert below.
	m_nTiles(outerWidth*outerWidth - holeWidth*holeWidth),
	m_tileSize(tileSize),
	m_pVBData(NULL)
{
	assert((outerWidth - holeWidth) % 2 == 0);
	CreateInstanceDataVB();
}

TileRing::~TileRing()
{
    delete[] m_pVBData;
    m_PositionsVB.Destroy();
}

void TileRing::GetInputLayout(const D3D12_INPUT_ELEMENT_DESC** ppDescs, UINT32* pElementCount)
{
    static const D3D12_INPUT_ELEMENT_DESC layout[] =
    {
        { "POSITION_2D",     0, DXGI_FORMAT_R32G32_FLOAT, 0, 0,  D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
        { "ADJACENCY_SIZES", 0, DXGI_FORMAT_R32_FLOAT,    0, 8,  D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
        { "ADJACENCY_SIZES", 1, DXGI_FORMAT_R32_FLOAT,    0, 12, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
        { "ADJACENCY_SIZES", 2, DXGI_FORMAT_R32_FLOAT,    0, 16, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
        { "ADJACENCY_SIZES", 3, DXGI_FORMAT_R32_FLOAT,    0, 20, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
    };

    *ppDescs = layout;
    *pElementCount = ARRAYSIZE(layout);
}

bool TileRing::InRing(int x, int y) const
{
	assert(x >= 0 && x < m_outerWidth);
	assert(y >= 0 && y < m_outerWidth);
	return (x < m_ringWidth || y < m_ringWidth || x >= m_outerWidth - m_ringWidth || y >= m_outerWidth - m_ringWidth);
}

void TileRing::AssignNeighbourSizes(int x, int y, Adjacency* pAdj) const
{
	pAdj->neighbourPlusX  = 1.0f;
	pAdj->neighbourPlusY  = 1.0f;
	pAdj->neighbourMinusX = 1.0f;
	pAdj->neighbourMinusY = 1.0f;

	// TBD: these aren't necessarily 2x different.  Depends on the relative tiles sizes supplied to ring ctors.
	const float innerNeighbourSize = 0.5f;
	const float outerNeighbourSize = 2.0f;

	// Inner edges abut tiles that are smaller.  (But not on the inner-most.)
	if (m_holeWidth > 0)
	{
		if (y >= m_ringWidth && y < m_outerWidth-m_ringWidth)
		{
			if (x == m_ringWidth-1)
				pAdj->neighbourPlusX  = innerNeighbourSize;
			if (x == m_outerWidth - m_ringWidth)
				pAdj->neighbourMinusX = innerNeighbourSize;
		}
		if (x >= m_ringWidth && x < m_outerWidth-m_ringWidth)
		{
			if (y == m_ringWidth-1)
				pAdj->neighbourPlusY  = innerNeighbourSize;
			if (y == m_outerWidth - m_ringWidth)
				pAdj->neighbourMinusY = innerNeighbourSize;
		}
	}

	// Outer edges abut tiles that are larger.  We could skip this on the outer-most ring.  But it will
	// make almost zero visual or perf difference.
	if (x == 0)
		pAdj->neighbourMinusX = outerNeighbourSize;
	if (y == 0)
		pAdj->neighbourMinusY = outerNeighbourSize;
	if (x == m_outerWidth - 1)
		pAdj->neighbourPlusX  = outerNeighbourSize;
	if (y == m_outerWidth - 1)
		pAdj->neighbourPlusY  = outerNeighbourSize;
}

void TileRing::CreateInstanceDataVB()
{    
    int index = 0;
    m_pVBData = new InstanceData[m_nTiles];

	const float halfWidth = 0.5f * (float) m_outerWidth;
    for (int y = 0; y < m_outerWidth; ++y)
    {
        for (int x = 0; x < m_outerWidth; ++x)
        {
			if (InRing(x,y))
			{
				m_pVBData[index].x = m_tileSize * ((float) x - halfWidth);
				m_pVBData[index].y = m_tileSize * ((float) y - halfWidth);
				AssignNeighbourSizes(x, y, &(m_pVBData[index].adjacency));
				index++;
			}
        }
    }
	assert(index == m_nTiles);

    WCHAR strName[32];
    swprintf_s(strName, L"Terrain Ring %u-%u", m_holeWidth, m_outerWidth);
    m_PositionsVB.Create(strName, m_nTiles, sizeof(InstanceData), m_pVBData);
}

void TileRing::SetRenderingState(GraphicsContext* pContext) const
{
    pContext->SetVertexBuffer(0, m_PositionsVB.VertexBufferView());
}

TessellatedTerrain::TessellatedTerrain()
{
    ZeroMemory(m_pTileRings, sizeof(m_pTileRings));
}

void TessellatedTerrain::Initialize()
{
    CreateTextures();

    CreateRootSignature();
    CreateTessellationPSO();
    CreateDeformPSO();

    CreateTileRings();

    CreateTileTriangleIB();
    CreateTileQuadListIB();

    ZeroMemory(&m_CBCommon, sizeof(m_CBCommon));
    ZeroMemory(&m_CBTerrain, sizeof(m_CBTerrain));
    ZeroMemory(&m_CBWireframe, sizeof(m_CBWireframe));

    m_CBTerrain.fDisplacementHeight.x = 1.0f;
}

void TessellatedTerrain::Terminate()
{
    for (UINT32 i = 0; i < ARRAYSIZE(m_pTileRings); ++i)
    {
        if (m_pTileRings[i] != nullptr)
        {
            delete m_pTileRings[i];
            m_pTileRings[i] = nullptr;
        }
    }

    m_HeightMap.Destroy();
    m_GradientMap.Destroy();
    m_TileTriStripIB.Destroy();
    m_TileQuadListIB.Destroy();
}

void TessellatedTerrain::CreateTileTriangleIB()
{
    int index = 0;
    unsigned long* pIndices = new unsigned long[TRI_STRIP_INDEX_COUNT];

    // Using the same patch-corner vertices as for h/w tessellaiton, tessellate them into 2 tris each.
    // Create the usual zig-zag pattern of stripped triangles for a regular gridded terrain tile.
    for (int y = 0; y < VTX_PER_TILE_EDGE - 1; ++y)
    {
        const int rowStart = y * VTX_PER_TILE_EDGE;

        for (int x = 0; x < VTX_PER_TILE_EDGE; ++x)
        {
            pIndices[index++] = rowStart + x;
            pIndices[index++] = rowStart + x + VTX_PER_TILE_EDGE;
        }

        // Repeat the last one on this row and the first on the next to join strips with degenerates.
        pIndices[index] = pIndices[index - 1];
        ++index;
        pIndices[index++] = rowStart + VTX_PER_TILE_EDGE;
    }
    assert(index == TRI_STRIP_INDEX_COUNT);

    m_TileTriStripIB.Create(L"Tile Tri Strip IB", TRI_STRIP_INDEX_COUNT, sizeof(unsigned long), pIndices);

    delete[] pIndices;
}

void TessellatedTerrain::CreateTileQuadListIB()
{
    int index = 0;
    unsigned long* pIndices = new unsigned long[QUAD_LIST_INDEX_COUNT];

    // The IB describes one tile of NxN patches.
    // Four vertices per quad, with VTX_PER_TILE_EDGE-1 quads per tile edge.
    for (int y = 0; y < VTX_PER_TILE_EDGE - 1; ++y)
    {
        const int rowStart = y * VTX_PER_TILE_EDGE;

        for (int x = 0; x < VTX_PER_TILE_EDGE - 1; ++x)
        {
            pIndices[index++] = rowStart + x;
            pIndices[index++] = rowStart + x + VTX_PER_TILE_EDGE;
            pIndices[index++] = rowStart + x + VTX_PER_TILE_EDGE + 1;
            pIndices[index++] = rowStart + x + 1;
        }
    }
    assert(index == QUAD_LIST_INDEX_COUNT);

    m_TileQuadListIB.Create(L"Tile Quad List IB", QUAD_LIST_INDEX_COUNT, sizeof(unsigned long), pIndices);

    delete[] pIndices;
}

void TessellatedTerrain::CreateRootSignature()
{
    m_RootSig.Reset(5, 5);
    m_RootSig.InitStaticSampler(0, Graphics::SamplerLinearClampDesc, D3D12_SHADER_VISIBILITY_ALL);
    m_RootSig.InitStaticSampler(1, Graphics::SamplerLinearWrapDesc, D3D12_SHADER_VISIBILITY_ALL);
    SamplerDesc PointWrapDesc;
    PointWrapDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    PointWrapDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    m_RootSig.InitStaticSampler(2, PointWrapDesc, D3D12_SHADER_VISIBILITY_ALL);
    SamplerDesc AnisoDesc = Graphics::SamplerAnisoWrapDesc;
    AnisoDesc.MaxAnisotropy = 4;
    m_RootSig.InitStaticSampler(3, AnisoDesc, D3D12_SHADER_VISIBILITY_ALL);
    AnisoDesc.MaxAnisotropy = 16;
    m_RootSig.InitStaticSampler(4, AnisoDesc, D3D12_SHADER_VISIBILITY_ALL);
    m_RootSig[0].InitAsConstantBuffer(0, D3D12_SHADER_VISIBILITY_ALL);
    m_RootSig[1].InitAsConstantBuffer(1, D3D12_SHADER_VISIBILITY_ALL);
    m_RootSig[2].InitAsConstantBuffer(2, D3D12_SHADER_VISIBILITY_ALL);
    m_RootSig[3].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 4, D3D12_SHADER_VISIBILITY_ALL);
    m_RootSig[4].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 4, 3, D3D12_SHADER_VISIBILITY_ALL);
    m_RootSig.Finalize(L"TessellatedTerrain RootSig", D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
}

void TessellatedTerrain::CreateTessellationPSO()
{
    const D3D12_INPUT_ELEMENT_DESC* pElements = nullptr;
    UINT32 ElementCount = 0;
    TileRing::GetInputLayout(&pElements, &ElementCount);

    DXGI_FORMAT ColorFormat = Graphics::g_SceneColorBuffer.GetFormat();
    DXGI_FORMAT DepthFormat = Graphics::g_SceneDepthBuffer.GetFormat();

    D3D12_RASTERIZER_DESC NormalDesc = Graphics::RasterizerDefault;
    NormalDesc.MultisampleEnable = TRUE;
    NormalDesc.CullMode = D3D12_CULL_MODE_FRONT;
    D3D12_RASTERIZER_DESC WireframeDesc = NormalDesc;
    WireframeDesc.FillMode = D3D12_FILL_MODE_WIREFRAME;

    m_TessellationPSO.SetRootSignature(m_RootSig);
    m_TessellationPSO.SetRasterizerState(NormalDesc);
    m_TessellationPSO.SetBlendState(Graphics::BlendTraditional);
    m_TessellationPSO.SetDepthStencilState(Graphics::DepthStateTestEqual);
    m_TessellationPSO.SetInputLayout(ElementCount, pElements);
    m_TessellationPSO.SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH);
    m_TessellationPSO.SetRenderTargetFormats(1, &ColorFormat, DepthFormat);
    m_TessellationPSO.SetVertexShader(g_pHwTessellationPassThruVS, sizeof(g_pHwTessellationPassThruVS));
    m_TessellationPSO.SetHullShader(g_pTerrainScreenspaceLODHS, sizeof(g_pTerrainScreenspaceLODHS));
    m_TessellationPSO.SetDomainShader(g_pTerrainDisplaceDS, sizeof(g_pTerrainDisplaceDS));
    m_TessellationPSO.SetGeometryShader(nullptr, 0);
    m_TessellationPSO.SetPixelShader(g_pSmoothShadePS, sizeof(g_pSmoothShadePS));
    m_TessellationPSO.Finalize();

    m_TessellationDepthPSO = m_TessellationPSO;
    m_TessellationDepthPSO.SetDepthStencilState(Graphics::DepthStateReadWrite);
    m_TessellationDepthPSO.SetRenderTargetFormats(0, nullptr, DepthFormat);
    m_TessellationDepthPSO.SetPixelShader(nullptr, 0);
    m_TessellationDepthPSO.Finalize();

    m_TessellationWireframePSO = m_TessellationPSO;
    m_TessellationWireframePSO.SetRasterizerState(WireframeDesc);
    m_TessellationWireframePSO.SetGeometryShader(g_pGSSolidWire, sizeof(g_pGSSolidWire));
    m_TessellationWireframePSO.SetPixelShader(g_pPSSolidWire, sizeof(g_pPSSolidWire));
    m_TessellationWireframePSO.Finalize();

    NormalDesc.CullMode = D3D12_CULL_MODE_BACK;
    WireframeDesc.CullMode = D3D12_CULL_MODE_BACK;

    m_NoTessellationPSO = m_TessellationPSO;
    m_NoTessellationPSO.SetRasterizerState(NormalDesc);
    m_NoTessellationPSO.SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
    m_NoTessellationPSO.SetVertexShader(g_pVTFDisplacementVS, sizeof(g_pVTFDisplacementVS));
    m_NoTessellationPSO.SetHullShader(nullptr, 0);
    m_NoTessellationPSO.SetDomainShader(nullptr, 0);
    m_NoTessellationPSO.SetGeometryShader(nullptr, 0);
    m_NoTessellationPSO.SetPixelShader(g_pSmoothShadePS, sizeof(g_pSmoothShadePS));
    m_NoTessellationPSO.Finalize();

    m_NoTessellationDepthPSO = m_NoTessellationPSO;
    m_NoTessellationDepthPSO.SetDepthStencilState(Graphics::DepthStateReadWrite);
    m_NoTessellationDepthPSO.SetRenderTargetFormats(0, nullptr, DepthFormat);
    m_NoTessellationDepthPSO.SetPixelShader(nullptr, 0);
    m_NoTessellationDepthPSO.Finalize();

    m_NoTessellationWireframePSO = m_NoTessellationPSO;
    m_NoTessellationWireframePSO.SetRasterizerState(WireframeDesc);
    m_NoTessellationWireframePSO.SetGeometryShader(g_pGSSolidWire, sizeof(g_pGSSolidWire));
    m_NoTessellationWireframePSO.SetPixelShader(g_pPSSolidWire, sizeof(g_pPSSolidWire));
    m_NoTessellationWireframePSO.Finalize();
}

void TessellatedTerrain::CreateDeformPSO()
{
    DXGI_FORMAT ColorFormat = m_HeightMap.GetFormat();

    m_InitializationPSO.SetRootSignature(m_RootSig);
    m_InitializationPSO.SetRasterizerState(Graphics::RasterizerDefault);
    m_InitializationPSO.SetBlendState(Graphics::BlendDisable);
    m_InitializationPSO.SetDepthStencilState(Graphics::DepthStateDisabled);
    m_InitializationPSO.SetInputLayout(0, nullptr);
    m_InitializationPSO.SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
    m_InitializationPSO.SetRenderTargetFormats(1, &ColorFormat, DXGI_FORMAT_UNKNOWN);
    m_InitializationPSO.SetVertexShader(g_pInitializationVS, sizeof(g_pInitializationVS));
    m_InitializationPSO.SetPixelShader(g_pInitializationPS, sizeof(g_pInitializationPS));
    m_InitializationPSO.Finalize();

    ColorFormat = m_GradientMap.GetFormat();
    m_GradientPSO = m_InitializationPSO;
    m_GradientPSO.SetPixelShader(g_pGradientPS, sizeof(g_pGradientPS));
    m_GradientPSO.SetRenderTargetFormats(1, &ColorFormat, DXGI_FORMAT_UNKNOWN);
    m_GradientPSO.Finalize();
}

void TessellatedTerrain::CreateTextures()
{
    m_HeightMap.Create(L"TerrainTessellation Heightmap", COARSE_HEIGHT_MAP_SIZE, COARSE_HEIGHT_MAP_SIZE, 1, DXGI_FORMAT_R32_FLOAT);
    m_GradientMap.Create(L"TerrainTessellation GradientMap", COARSE_HEIGHT_MAP_SIZE, COARSE_HEIGHT_MAP_SIZE, 1, DXGI_FORMAT_R16G16_FLOAT);

    m_pNoiseTexture = TextureManager::LoadFromFile("GaussianNoise256", false);
    m_pDetailNoiseTexture = TextureManager::LoadFromFile("fBm5Octaves", false);
    m_pDetailNoiseGradTexture = TextureManager::LoadFromFile("fBm5OctavesGrad", false);
}

void TessellatedTerrain::CreateTileRings()
{
    int widths[] = { 0, 16, 16, 16, 16 };
    m_nRings = sizeof(widths) / sizeof(widths[0]) - 1;		// widths[0] doesn't define a ring hence -1
    assert(m_nRings <= MAX_RINGS);

    float tileWidth = 0.125f;
    for (int i = 0; i != m_nRings && i != MAX_RINGS; ++i)
    {
        m_pTileRings[i] = new TileRing(widths[i] / 2, widths[i + 1], tileWidth);
        tileWidth *= 2.0f;
    }
}

void TessellatedTerrain::SetUVOffset(const TessellatedTerrainRenderDesc* pDesc, XMFLOAT4* pDest) const
{
    XMFLOAT4 eye = pDesc->CameraPosWorld;
    eye.y = 0;
    if (SNAP_GRID_SIZE > 0)
    {
        eye.x = floorf(eye.x / SNAP_GRID_SIZE) * SNAP_GRID_SIZE;
        eye.z = floorf(eye.z / SNAP_GRID_SIZE) * SNAP_GRID_SIZE;
    }
    eye.x /= WORLD_SCALE;
    eye.z /= -WORLD_SCALE;
    *pDest = eye;
}

void TessellatedTerrain::OffscreenRender(GraphicsContext* pContext, const TessellatedTerrainRenderDesc* pDesc)
{
    // TODO: compare camera eye pos to previous and set m_UpdateTerrainTexture if different
    m_UpdateTerrainTexture = true;

    pContext->SetRootSignature(m_RootSig);

    m_CBCommon.FractalOctaves.x = g_RidgeOctaves;
    m_CBCommon.FractalOctaves.y = g_fBmOctaves;
    m_CBCommon.FractalOctaves.z = g_TexTwistOctaves;

    m_CBCommon.CoarseSampleSpacing.x = WORLD_SCALE * m_pTileRings[m_nRings - 1]->outerWidth() / (float)COARSE_HEIGHT_MAP_SIZE;

    SetUVOffset(pDesc, &m_CBCommon.TextureWorldOffset);

    pContext->SetDynamicConstantBufferView(1, sizeof(m_CBCommon), &m_CBCommon);

    D3D12_CPU_DESCRIPTOR_HANDLE hNoiseTextures[3] = {};
    hNoiseTextures[0] = m_pDetailNoiseTexture->GetSRV();
    hNoiseTextures[1] = m_pDetailNoiseGradTexture->GetSRV();
    hNoiseTextures[2] = m_pNoiseTexture->GetSRV();
    pContext->SetDynamicDescriptors(4, 0, ARRAYSIZE(hNoiseTextures), hNoiseTextures);

    if (m_UpdateTerrainTexture)
    {
        DeformInitTerrain(pContext, pDesc);
        m_UpdateTerrainTexture = false;
    }
}

void TessellatedTerrain::Render(GraphicsContext* pContext, const TessellatedTerrainRenderDesc* pDesc)
{
    if (pDesc->ZPrePass && g_TerrainWireframe)
    {
        return;
    }

    pContext->SetRootSignature(m_RootSig);

    pContext->SetDynamicConstantBufferView(1, sizeof(m_CBCommon), &m_CBCommon);

    D3D12_CPU_DESCRIPTOR_HANDLE hNoiseTextures[3] = {};
    hNoiseTextures[0] = m_pDetailNoiseTexture->GetSRV();
    hNoiseTextures[1] = m_pDetailNoiseGradTexture->GetSRV();
    hNoiseTextures[2] = m_pNoiseTexture->GetSRV();
    pContext->SetDynamicDescriptors(4, 0, ARRAYSIZE(hNoiseTextures), hNoiseTextures);

    // Something's wrong in the shader and the tri size is out by a factor of 2.  Why?!?
    m_CBTerrain.tessellatedTriWidth.x = 2 * g_TessellatedTriWidth;

    m_CBTerrain.DebugShowPatches.x = (INT)g_DebugDrawPatches;
    m_CBTerrain.DebugShowTiles.x = (INT)g_DebugDrawTiles;

    const float wireAlpha = 0.01f * g_WireframeAlpha;

    // Below 1.0, we fade the lines out with blending; above 1, we increase line thickness.
    if (wireAlpha < 1)
    {
        m_CBWireframe.WireAlpha.x = wireAlpha;
        m_CBWireframe.WireWidth.x = 1.0f;
    }
    else
    {
        m_CBWireframe.WireAlpha.x = 1.0f;
        m_CBWireframe.WireWidth.x = wireAlpha;
    }

    m_CBTerrain.DetailNoiseScale.x = 0.001f * (FLOAT)g_DetailNoiseScale;

    m_CBWireframe.Viewport.x = pDesc->Viewport.Width;
    m_CBWireframe.Viewport.y = pDesc->Viewport.Height;
    m_CBWireframe.Viewport.z = pDesc->Viewport.TopLeftX;
    m_CBWireframe.Viewport.w = pDesc->Viewport.TopLeftY;
    m_CBTerrain.screenSize.x = pDesc->Viewport.Width;
    m_CBTerrain.screenSize.y = pDesc->Viewport.Height;

    // I'm still trying to figure out if the detail scale can be derived from any combo of ridge + twist.
    // I don't think this works well (nor does ridge+twist+fBm).  By contrast the relationship with fBm is
    // straightforward.  The -4 is a fudge factor that accounts for the frequency of the coarsest octave
    // in the pre-rendered detail map.
    const float DETAIL_UV_SCALE = powf(2.0f, std::max(g_RidgeOctaves, g_TexTwistOctaves) + g_fBmOctaves - 4.0f);
    m_CBTerrain.DetailUVScale.x = DETAIL_UV_SCALE;
    m_CBTerrain.DetailUVScale.y = 1.0f / DETAIL_UV_SCALE;

    RenderTerrain(pContext, pDesc);
}

void TessellatedTerrain::DeformInitTerrain(GraphicsContext* pContext, const TessellatedTerrainRenderDesc* pDesc)
{
    static const D3D12_VIEWPORT vp = { 0,0, (float)COARSE_HEIGHT_MAP_SIZE, (float)COARSE_HEIGHT_MAP_SIZE, 0.0f, 1.0f };
    static const D3D12_RECT rect = { 0, 0, COARSE_HEIGHT_MAP_SIZE, COARSE_HEIGHT_MAP_SIZE };

    m_CBDeform.DeformMin = XMFLOAT4(-1, -1, 0, 0);
    m_CBDeform.DeformMax = XMFLOAT4(1, 1, 0, 0);

    D3D12_CPU_DESCRIPTOR_HANDLE hRTV = m_HeightMap.GetRTV();
    pContext->SetRenderTargets(1, &hRTV);
    pContext->SetViewport(vp);
    pContext->SetScissor(rect);

    pContext->TransitionResource(m_HeightMap, D3D12_RESOURCE_STATE_RENDER_TARGET);

    pContext->SetPipelineState(m_InitializationPSO);
    pContext->SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    pContext->SetDynamicConstantBufferView(0, sizeof(m_CBDeform), &m_CBDeform);
    pContext->Draw(4, 0);

    pContext->TransitionResource(m_HeightMap, D3D12_RESOURCE_STATE_GENERIC_READ);
    pContext->TransitionResource(m_GradientMap, D3D12_RESOURCE_STATE_RENDER_TARGET);

    hRTV = m_GradientMap.GetRTV();
    pContext->SetRenderTargets(1, &hRTV);
    pContext->SetViewport(vp);
    pContext->SetScissor(rect);
    pContext->SetPipelineState(m_GradientPSO);

    D3D12_CPU_DESCRIPTOR_HANDLE hSRVs[4] = { m_HeightMap.GetSRV() };
    pContext->SetDynamicDescriptors(3, 0, 1, hSRVs);

    pContext->SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    pContext->SetDynamicConstantBufferView(0, sizeof(m_CBDeform), &m_CBDeform);
    pContext->Draw(4, 0);

    pContext->SetRenderTargets(0, nullptr);

    pContext->TransitionResource(m_GradientMap, D3D12_RESOURCE_STATE_GENERIC_READ);
}

void TessellatedTerrain::RenderTerrain(GraphicsContext* pContext, const TessellatedTerrainRenderDesc* pDesc)
{
    SetMatrices(pDesc);

    if (g_HwTessellation)
    {
        pContext->SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_4_CONTROL_POINT_PATCHLIST);
        pContext->SetIndexBuffer(m_TileQuadListIB.IndexBufferView(0, -1, true));

        if (g_TerrainWireframe)
        {
            pContext->SetPipelineState(m_TessellationWireframePSO);
        }
        else if (pDesc->ZPrePass)
        {
            pContext->SetPipelineState(m_TessellationDepthPSO);
        }
        else
        {
            pContext->SetPipelineState(m_TessellationPSO);
        }
    }
    else
    {
        pContext->SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        pContext->SetIndexBuffer(m_TileTriStripIB.IndexBufferView(0, -1, true));

        if (g_TerrainWireframe)
        {
            pContext->SetPipelineState(m_NoTessellationWireframePSO);
        }
        else if (pDesc->ZPrePass)
        {
            pContext->SetPipelineState(m_NoTessellationDepthPSO);
        }
        else
        {
            pContext->SetPipelineState(m_NoTessellationPSO);
        }
    }

    pContext->SetDynamicConstantBufferView(2, sizeof(m_CBWireframe), &m_CBWireframe);

    D3D12_CPU_DESCRIPTOR_HANDLE hSRVs[4] = {};
    hSRVs[0] = m_HeightMap.GetSRV();
    hSRVs[1] = m_GradientMap.GetSRV();
    pContext->SetDynamicDescriptors(3, 0, 2, hSRVs);

    for (int i = 0; i < m_nRings; ++i)
    {
        const TileRing* pRing = m_pTileRings[i];
        pRing->SetRenderingState(pContext);

        m_CBTerrain.tileSize.x = pRing->tileSize();

        pContext->SetDynamicConstantBufferView(0, sizeof(m_CBTerrain), &m_CBTerrain);

        // Instancing is used: one tiles is one instance and the index buffer describes all the 
        // NxN patches within one tile.
        const int nIndices = (g_HwTessellation) ? QUAD_LIST_INDEX_COUNT : TRI_STRIP_INDEX_COUNT;
        pContext->DrawIndexedInstanced(nIndices, pRing->nTiles(), 0, 0, 0);
    }
}

void TessellatedTerrain::SetMatrices(const TessellatedTerrainRenderDesc* pDesc)
{
    // Set matrices
    XMMATRIX mWorld, mScale, mTrans;
    mScale = XMMatrixScaling(WORLD_SCALE, WORLD_SCALE, WORLD_SCALE);

    // We keep the eye centered in the middle of the tile rings.  The height map scrolls in the horizontal 
    // plane instead.
    const XMFLOAT4 eye = pDesc->CameraPosWorld;
    float snappedX = eye.x, snappedZ = eye.z;
    if (SNAP_GRID_SIZE > 0)
    {
        snappedX = floorf(snappedX / SNAP_GRID_SIZE) * SNAP_GRID_SIZE;
        snappedZ = floorf(snappedZ / SNAP_GRID_SIZE) * SNAP_GRID_SIZE;
    }
    const float dx = eye.x - snappedX;
    const float dz = eye.z - snappedZ;
    snappedX = eye.x - 2 * dx;				// Why the 2x?  I'm confused.  But it works.
    snappedZ = eye.z - 2 * dz;
    mTrans = XMMatrixTranslation(snappedX, 0, snappedZ);
    mWorld = XMMatrixMultiply(mScale, mTrans);

    XMMATRIX mView = XMLoadFloat4x4A(&pDesc->matView);
    XMMATRIX mProj = XMLoadFloat4x4A(&pDesc->matProjection);
    XMMATRIX mWorldView = mWorld * mView;
    XMMATRIX mWorldViewProj = mWorldView * mProj;
    XMStoreFloat4x4(&m_CBTerrain.WorldViewProj, mWorldViewProj);
    XMStoreFloat4x4(&m_CBTerrain.Proj, mProj);

    // For LOD calculations, we always use the master camera's view matrix.
    XMMATRIX mWorldViewLOD = mWorld * mView;
    XMMATRIX mWorldViewProjLOD = mWorldViewLOD * mProj;
    XMStoreFloat4x4(&m_CBTerrain.WorldViewProjLOD, mWorldViewProjLOD);
    XMStoreFloat4x4(&m_CBTerrain.WorldViewLOD, mWorldViewLOD);

    // Due to the snapping tricks, the centre of projection moves by a small amount in the range ([0,2*dx],[0,2*dz])
    // relative to the terrain.  For frustum culling, we need this eye position.
    XMFLOAT4 cullingEye = eye;
    cullingEye.x -= snappedX;
    cullingEye.z -= snappedZ;
    m_CBTerrain.EyePos = cullingEye;

    XMVECTOR Det;
    XMMATRIX mCameraWorld = XMMatrixInverse(&Det, mView);
    XMStoreFloat4(&m_CBTerrain.ViewDir, mCameraWorld.r[2]);
}
