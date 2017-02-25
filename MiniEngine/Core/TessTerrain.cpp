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
#include "LineRender.h"

#include "CompiledShaders\InitializationVS.h"
#include "CompiledShaders\InitializationPS.h"
#include "CompiledShaders\GradientPS.h"
#include "CompiledShaders\HwTessellationPassThruVS.h"
#include "CompiledShaders\TerrainScreenspaceLODHS.h"
#include "CompiledShaders\TerrainDisplaceDS.h"
#include "CompiledShaders\SmoothShadePS.h"
#include "CompiledShaders\SmoothShadeDistantPS.h"
#include "CompiledShaders\VTFDisplacementVS.h"
#include "CompiledShaders\GSSolidWire.h"
#include "CompiledShaders\PSSolidWire.h"

BoolVar g_TerrainEnabled("Terrain/Enabled", true);
BoolVar g_HwTessellation("Terrain/HW Tessellation", true);
IntVar g_TessellatedTriWidth("Terrain/Tessellated Triangle Width", 20, 1, 100);

BoolVar g_TerrainWireframe("Terrain/Wireframe", false);
NumVar g_WireframeAlpha("Terrain/Wireframe Alpha", 0.5f, 0, 5, 0.1f);
BoolVar g_DebugDrawPatches("Terrain/Debug Draw Patches", false);
BoolVar g_DebugDrawTiles("Terrain/Debug Draw Tiles", false);
BoolVar g_DrawHeightmap("Terrain/Debug Draw Heightmap", false);
BoolVar g_DrawGradientmap("Terrain/Debug Draw Gradient Map", false);
BoolVar g_PlaceAtOrigin("Terrain/Place at Origin", false);
BoolVar g_CameraAtOrigin("Terrain/Camera at Origin", false);

static const int MAX_OCTAVES = 15;
IntVar g_RidgeOctaves("Terrain/Ridge Octaves", 3, 1, MAX_OCTAVES);
IntVar g_fBmOctaves("Terrain/fBm Octaves", 3, 1, MAX_OCTAVES);
IntVar g_TexTwistOctaves("Terrain/Tex Twist Octaves", 1, 1, MAX_OCTAVES);
IntVar g_DetailNoiseScale("Terrain/Detail Noise Scale", 0, 0, 200);
NumVar g_WorldScale("Terrain/World Scale", 512, 50, 2000, 50);
NumVar g_VerticalScale("Terrain/Vertical Scale", 1.5f, 0.05f, 25.0f, 0.05f);
ExpVar g_HeightmapDimension("Terrain/Heightmap Dimension", 1024, 5, 12, 1);
NumVar g_DeformScale("Terrain/Generated Scale", 1.0f, 0.01f, 100.0f, 0.01f);
NumVar g_DeformOffset("Terrain/Generated Offset", 0.0f, -500.0f, 500.0f, 0.01f);

BoolVar g_DebugGrid("Terrain/Debug Grid Enable", false);
NumVar g_DebugGridScale("Terrain/Debug Grid Scale", 512, 50.0f, 5000.0f, 50.0f);

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
    CreateNoiseTextures();
    CreatePhysicsTextures();

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
    m_ZoneMap.Destroy();
    m_TileTriStripIB.Destroy();
    m_TileQuadListIB.Destroy();
    m_ColorNoiseTexture.Destroy();

    m_PhysicsHeightMap.Destroy();
    m_PhysicsZoneMap.Destroy();
    m_ReadbackResource.GetResource()->Release();
    m_ReadbackResource.Destroy();
    for (UINT32 i = 0; i < ARRAYSIZE(m_DebugPhysicsHeightMaps); ++i)
    {
        m_DebugPhysicsHeightMaps[i].Destroy();
    }
}

FLOAT TessellatedTerrain::GetWorldScale() const
{
    return g_WorldScale;
}

FLOAT TessellatedTerrain::GetVerticalScale() const
{
    return g_VerticalScale;
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

enum TerrainRootParams
{
    TerrainRootParam_CBLightAndShadow = 0,
    TerrainRootParam_CBDeform = 0,
    TerrainRootParam_CBCommon,
    TerrainRootParam_CBWireframe,
    TerrainRootParam_CBTerrain,
    TerrainRootParam_DTHeightmap,
    TerrainRootParam_DTNoisemap,
    TerrainRootParam_DTShadowSSAO,
};

void TessellatedTerrain::CreateRootSignature()
{
    m_RootSig.Reset(7, 6);
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
    m_RootSig.InitStaticSampler(15, Graphics::SamplerShadowDesc, D3D12_SHADER_VISIBILITY_PIXEL);
    m_RootSig[0].InitAsConstantBuffer(0, D3D12_SHADER_VISIBILITY_ALL);
    m_RootSig[1].InitAsConstantBuffer(1, D3D12_SHADER_VISIBILITY_ALL);
    m_RootSig[2].InitAsConstantBuffer(2, D3D12_SHADER_VISIBILITY_ALL);
    m_RootSig[3].InitAsConstantBuffer(3, D3D12_SHADER_VISIBILITY_ALL);
    m_RootSig[4].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 4, D3D12_SHADER_VISIBILITY_ALL);
    m_RootSig[5].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 4, 4, D3D12_SHADER_VISIBILITY_ALL);
    m_RootSig[6].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 64, 3, D3D12_SHADER_VISIBILITY_PIXEL);
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
    if (!m_Culling)
    {
        NormalDesc.CullMode = D3D12_CULL_MODE_NONE;
    }
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
    m_TessellationWireframePSO.SetDepthStencilState(Graphics::DepthStateReadOnly);
    m_TessellationWireframePSO.Finalize();

    m_TessellationDistantPSO = m_TessellationPSO;
    m_TessellationDistantPSO.SetPixelShader(g_pSmoothShadeDistantPS, sizeof(g_pSmoothShadeDistantPS));
    m_TessellationDistantPSO.Finalize();

    if (m_Culling)
    {
        NormalDesc.CullMode = D3D12_CULL_MODE_BACK;
        WireframeDesc.CullMode = D3D12_CULL_MODE_BACK;
    }

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
    m_NoTessellationWireframePSO.SetDepthStencilState(Graphics::DepthStateReadOnly);
    m_NoTessellationWireframePSO.Finalize();
}

void TessellatedTerrain::CreateDeformPSO()
{
    DXGI_FORMAT ColorFormat[2] = { m_HeightMap.GetFormat(), m_ZoneMap.GetFormat() };

    m_InitializationPSO.SetRootSignature(m_RootSig);
    m_InitializationPSO.SetRasterizerState(Graphics::RasterizerDefault);
    m_InitializationPSO.SetBlendState(Graphics::BlendDisable);
    m_InitializationPSO.SetDepthStencilState(Graphics::DepthStateDisabled);
    m_InitializationPSO.SetInputLayout(0, nullptr);
    m_InitializationPSO.SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
    m_InitializationPSO.SetRenderTargetFormats(2, ColorFormat, DXGI_FORMAT_UNKNOWN);
    m_InitializationPSO.SetVertexShader(g_pInitializationVS, sizeof(g_pInitializationVS));
    m_InitializationPSO.SetPixelShader(g_pInitializationPS, sizeof(g_pInitializationPS));
    m_InitializationPSO.Finalize();

    ColorFormat[0] = m_GradientMap.GetFormat();
    m_GradientPSO = m_InitializationPSO;
    m_GradientPSO.SetPixelShader(g_pGradientPS, sizeof(g_pGradientPS));
    m_GradientPSO.SetRenderTargetFormats(1, ColorFormat, DXGI_FORMAT_UNKNOWN);
    m_GradientPSO.Finalize();
}

void TessellatedTerrain::CreateTextures()
{
    m_HeightMap.Create(L"TerrainTessellation Heightmap", (UINT32)g_HeightmapDimension, (UINT32)g_HeightmapDimension, 1, DXGI_FORMAT_R32_FLOAT);
    m_GradientMap.Create(L"TerrainTessellation GradientMap", (UINT32)g_HeightmapDimension, (UINT32)g_HeightmapDimension, 1, DXGI_FORMAT_R16G16_FLOAT);
    m_ZoneMap.Create(L"TerrainTessellation ZoneMap", (UINT32)g_HeightmapDimension, (UINT32)g_HeightmapDimension, 1, DXGI_FORMAT_R8G8B8A8_UNORM);
}

void TessellatedTerrain::CreateNoiseTextures()
{
    m_pNoiseTexture = TextureManager::LoadFromFile("GaussianNoise256", false);
    m_pDetailNoiseTexture = TextureManager::LoadFromFile("fBm5Octaves", false);
    m_pDetailNoiseGradTexture = TextureManager::LoadFromFile("fBm5OctavesGrad", false);

    const UINT32 NoiseTextureSize = 256;
    const UINT32 TexelCount = NoiseTextureSize * NoiseTextureSize;
    UINT32* pTexels = new UINT32[TexelCount];
    for (UINT32 i = 0; i < TexelCount; ++i)
    {
        UINT32 Value = rand() & 0xFF;
        Value = (Value << 8) | (rand() & 0xFF);
        Value = (Value << 8) | (rand() & 0xFF);
        Value = (Value << 8) | (rand() & 0xFF);
        pTexels[i] = Value;
    }

    D3D12_HEAP_PROPERTIES HeapProps;
    HeapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
    HeapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    HeapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    HeapProps.CreationNodeMask = 1;
    HeapProps.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC ResourceDesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8G8B8A8_UNORM, NoiseTextureSize, NoiseTextureSize, 1, 1);
    ID3D12Resource* pColorNoiseTexture = nullptr;
    Graphics::g_Device->CreateCommittedResource(&HeapProps, D3D12_HEAP_FLAG_NONE, &ResourceDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, __uuidof(ID3D12Resource), (void**)&pColorNoiseTexture);
    m_ColorNoiseTexture = GpuResource(pColorNoiseTexture, D3D12_RESOURCE_STATE_COPY_DEST);
    pColorNoiseTexture->Release();
    m_ColorNoiseTexture->SetName(L"Color Noise Texture");

    D3D12_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
    SRVDesc.Format = ResourceDesc.Format;
    SRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    SRVDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    SRVDesc.Texture2D.MipLevels = 1;
    SRVDesc.Texture2D.MostDetailedMip = 0;
    m_hColorNoiseSRV = Graphics::AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    Graphics::g_Device->CreateShaderResourceView(m_ColorNoiseTexture.GetResource(), &SRVDesc, m_hColorNoiseSRV);

    D3D12_SUBRESOURCE_DATA InitData = {};
    InitData.pData = pTexels;
    InitData.RowPitch = NoiseTextureSize * sizeof(UINT32);
    InitData.SlicePitch = InitData.RowPitch * NoiseTextureSize;
    CommandContext::InitializeTexture(m_ColorNoiseTexture, 1, &InitData);

    delete[] pTexels;
}

void TessellatedTerrain::CreatePhysicsTextures()
{
    const UINT32 PhysicsTextureSize = 64 + 1;

    m_PhysicsFootprint.Format = m_HeightMap.GetFormat();
    m_PhysicsFootprint.Width = PhysicsTextureSize;
    m_PhysicsFootprint.Height = PhysicsTextureSize;
    m_PhysicsFootprint.Depth = 1;
    const UINT32 AlignmentMask = D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1;
    m_PhysicsFootprint.RowPitch = ((PhysicsTextureSize * sizeof(FLOAT)) + AlignmentMask) & ~AlignmentMask;

    m_PhysicsHeightMap.Create(L"TerrainTessellation Physics Heightmap", m_PhysicsFootprint.Width, m_PhysicsFootprint.Height, 1, m_PhysicsFootprint.Format);
    m_PhysicsZoneMap.Create(L"TerrainTessellation Physics ZoneMap", m_PhysicsFootprint.Width, m_PhysicsFootprint.Height, 1, m_ZoneMap.GetFormat());

    for (UINT32 i = 0; i < ARRAYSIZE(m_DebugPhysicsHeightMaps); ++i)
    {
        m_DebugPhysicsHeightMaps[i].Create(L"Debug Heightmap", m_PhysicsFootprint.Width, m_PhysicsFootprint.Height, 1, m_PhysicsFootprint.Format);
    }
    m_CurrentDebugHeightmapIndex = 0;

    m_FootprintSizeBytes = m_PhysicsFootprint.RowPitch * m_PhysicsFootprint.Height;
    const UINT32 AllocCount = 24;
    const UINT32 ReadbackSizeBytes = AllocCount * m_FootprintSizeBytes;
    D3D12_RESOURCE_DESC BufferDesc = CD3DX12_RESOURCE_DESC::Buffer(ReadbackSizeBytes);
    CD3DX12_HEAP_PROPERTIES HeapProps(D3D12_HEAP_TYPE_READBACK);
    ID3D12Resource* pReadbackBuffer = nullptr;
    Graphics::g_Device->CreateCommittedResource(&HeapProps, D3D12_HEAP_FLAG_NONE, &BufferDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, __uuidof(ID3D12Resource), (void**)&pReadbackBuffer);
    pReadbackBuffer->SetName(L"Physics Heightmap Readback Buffer");
    pReadbackBuffer->Map(0, nullptr, (void**)&m_pReadbackData);
    m_ReadbackResource = GpuResource(pReadbackBuffer, D3D12_RESOURCE_STATE_COPY_DEST);

    m_AvailableMapMask = (1Ui64 << AllocCount) - 1;
}

void TessellatedTerrain::CreateTileRings()
{
    int widths[] = { 0, 16, 16, 16, 32 };
    m_nRings = sizeof(widths) / sizeof(widths[0]) - 1;		// widths[0] doesn't define a ring hence -1
    assert(m_nRings <= MAX_RINGS);

    float tileWidth = 0.125f;
    for (int i = 0; i != m_nRings && i != MAX_RINGS; ++i)
    {
        m_pTileRings[i] = new TileRing(widths[i] / 2, widths[i + 1], tileWidth);
        tileWidth *= 2.0f;
    }

    TileRing* pLastRing = m_pTileRings[m_nRings - 1];
    m_OuterRingWorldSize = (FLOAT)pLastRing->outerWidth() * pLastRing->tileSize();
}

void TessellatedTerrain::OffscreenRender(GraphicsContext* pContext, const TessellatedTerrainRenderDesc* pDesc)
{
    if (!g_TerrainEnabled)
    {
        return;
    }

    const UINT32 CurrentDimension = m_HeightMap.GetWidth();
    if (CurrentDimension != (UINT32)g_HeightmapDimension)
    {
        Graphics::g_CommandManager.GetQueue().WaitForIdle();
        m_HeightMap.Destroy();
        m_GradientMap.Destroy();
        m_ZoneMap.Destroy();

        CreateTextures();
    }

    XMFLOAT4A CameraPosWorld = pDesc->CameraPosWorld;
    CameraPosWorld.x -= (g_WorldScale * m_OuterRingWorldSize * 0.5f);
    CameraPosWorld.z += (g_WorldScale * m_OuterRingWorldSize * 0.5f);

    if (g_CameraAtOrigin)
    {
        CameraPosWorld = XMFLOAT4A(0, 0, 0, 0);
    }

    RenderTerrainHeightmap(pContext, &m_HeightMap, &m_ZoneMap, &m_GradientMap, CameraPosWorld, 1.0f);
}

void TessellatedTerrain::Render(GraphicsContext* pContext, const TessellatedTerrainRenderDesc* pDesc)
{
    if (!g_TerrainEnabled)
    {
        return;
    }

    if (pDesc->ZPrePass && g_TerrainWireframe)
    {
        return;
    }

    pContext->SetRootSignature(m_RootSig);

    pContext->SetDynamicConstantBufferView(TerrainRootParam_CBLightAndShadow, sizeof(*pDesc->pLightShadowConstants), pDesc->pLightShadowConstants);
    pContext->SetDynamicConstantBufferView(TerrainRootParam_CBCommon, sizeof(m_CBCommon), &m_CBCommon);
    pContext->SetDynamicDescriptors(TerrainRootParam_DTShadowSSAO, 0, 3, pDesc->pExtraTextures);

    D3D12_CPU_DESCRIPTOR_HANDLE hNoiseTextures[4] = {};
    hNoiseTextures[0] = m_pDetailNoiseTexture->GetSRV();
    hNoiseTextures[1] = m_pDetailNoiseGradTexture->GetSRV();
    hNoiseTextures[2] = m_pNoiseTexture->GetSRV();
    hNoiseTextures[3] = m_hColorNoiseSRV;
    pContext->SetDynamicDescriptors(TerrainRootParam_DTNoisemap, 0, ARRAYSIZE(hNoiseTextures), hNoiseTextures);

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

    if (g_DebugGrid)
    {
        const FLOAT BoxScale = g_DebugGridScale;
        for (INT32 z = -20; z < 20; ++z)
        {
            for (INT32 x = -20; x < 20; ++x)
            {
                XMVECTOR Min = XMVectorSet((FLOAT)x, -1, (FLOAT)z, 0) * BoxScale;
                XMVECTOR Max = XMVectorSet((FLOAT)(x + 1), 1, (FLOAT)(z + 1), 0) * BoxScale;
                XMVECTOR Color = (x == 0 && z == 0) ? XMVectorSet(1, 0, 1, 1) : g_XMOne;
                LineRender::DrawAxisAlignedBox(Min, Max, Color);
            }
        }
    }
}

void TessellatedTerrain::RenderTerrainHeightmap(GraphicsContext* pContext, ColorBuffer* pHeightmap, ColorBuffer* pZonemap, ColorBuffer* pGradientMap, XMFLOAT4 CameraPosWorld, FLOAT UVScale)
{
    assert(pHeightmap != nullptr && pZonemap != nullptr);
    const UINT32 Dimension = pHeightmap->GetWidth();
    assert(Dimension == pHeightmap->GetHeight());
    assert(Dimension == pZonemap->GetHeight() && Dimension == pZonemap->GetWidth());
    if (pGradientMap != nullptr)
    {
        assert(Dimension == pGradientMap->GetWidth() && Dimension == pGradientMap->GetHeight());
    }

    pContext->SetRootSignature(m_RootSig);

    m_CBCommon.FractalOctaves.x = g_RidgeOctaves;
    m_CBCommon.FractalOctaves.y = g_fBmOctaves;
    m_CBCommon.FractalOctaves.z = g_TexTwistOctaves;

    m_CBCommon.CoarseSampleSpacing.x = g_WorldScale * m_pTileRings[m_nRings - 1]->outerWidth() / (FLOAT)Dimension;
    m_CBCommon.CoarseSampleSpacing.y = g_WorldScale;
    m_CBCommon.CoarseSampleSpacing.z = g_VerticalScale;

    XMFLOAT4 eye = CameraPosWorld;
    eye.y = 0;
    if (SNAP_GRID_SIZE > 0)
    {
        eye.x = floorf(eye.x / SNAP_GRID_SIZE) * SNAP_GRID_SIZE;
        eye.z = floorf(eye.z / SNAP_GRID_SIZE) * SNAP_GRID_SIZE;
    }
    eye.x /= (g_WorldScale * 32);
    eye.z /= -(g_WorldScale * 32);
    m_CBCommon.TextureWorldOffset = eye;

    pContext->SetDynamicConstantBufferView(TerrainRootParam_CBCommon, sizeof(m_CBCommon), &m_CBCommon);

    D3D12_CPU_DESCRIPTOR_HANDLE hNoiseTextures[4] = {};
    hNoiseTextures[0] = m_pDetailNoiseTexture->GetSRV();
    hNoiseTextures[1] = m_pDetailNoiseGradTexture->GetSRV();
    hNoiseTextures[2] = m_pNoiseTexture->GetSRV();
    hNoiseTextures[3] = m_hColorNoiseSRV;
    pContext->SetDynamicDescriptors(TerrainRootParam_DTNoisemap, 0, ARRAYSIZE(hNoiseTextures), hNoiseTextures);

    const D3D12_VIEWPORT vp = { 0,0, (FLOAT)Dimension, (FLOAT)Dimension, 0.0f, 1.0f };
    const D3D12_RECT rect = { 0, 0, (LONG)Dimension, (LONG)Dimension };

    m_CBDeform.DeformMin = XMFLOAT4(-1, -1, 0, 0);
    m_CBDeform.DeformMax = XMFLOAT4(1, 1, UVScale, 0);
    m_CBDeform.DeformConstants = XMFLOAT4(g_DeformScale, g_DeformOffset, 0, 0);

    D3D12_CPU_DESCRIPTOR_HANDLE hRTVs[2] = { pHeightmap->GetRTV(), pZonemap->GetRTV() };
    pContext->SetRenderTargets(2, hRTVs);
    pContext->SetViewport(vp);
    pContext->SetScissor(rect);

    pContext->TransitionResource(*pHeightmap, D3D12_RESOURCE_STATE_RENDER_TARGET);

    pContext->SetPipelineState(m_InitializationPSO);
    pContext->SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    pContext->SetDynamicConstantBufferView(TerrainRootParam_CBDeform, sizeof(m_CBDeform), &m_CBDeform);
    pContext->Draw(4, 0);

    pContext->TransitionResource(*pHeightmap, D3D12_RESOURCE_STATE_GENERIC_READ);

    if (pGradientMap != nullptr)
    {
        pContext->TransitionResource(*pGradientMap, D3D12_RESOURCE_STATE_RENDER_TARGET);

        hRTVs[0] = pGradientMap->GetRTV();
        pContext->SetRenderTargets(1, hRTVs);
        pContext->SetViewport(vp);
        pContext->SetScissor(rect);
        pContext->SetPipelineState(m_GradientPSO);

        D3D12_CPU_DESCRIPTOR_HANDLE hSRVs[4] = { pHeightmap->GetSRV() };
        pContext->SetDynamicDescriptors(TerrainRootParam_DTHeightmap, 0, 1, hSRVs);

        pContext->SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        m_CBDeform.DeformMax.z = 1.0f;
        pContext->SetDynamicConstantBufferView(TerrainRootParam_CBDeform, sizeof(m_CBDeform), &m_CBDeform);
        pContext->Draw(4, 0);

        pContext->SetRenderTargets(0, nullptr);

        pContext->TransitionResource(*pGradientMap, D3D12_RESOURCE_STATE_GENERIC_READ);
    }
}

void TessellatedTerrain::RenderTerrain(GraphicsContext* pContext, const TessellatedTerrainRenderDesc* pDesc)
{
    SetMatrices(pDesc);

    bool LODShaderEnabled = false;
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
            LODShaderEnabled = true;
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

    pContext->SetDynamicConstantBufferView(TerrainRootParam_CBWireframe, sizeof(m_CBWireframe), &m_CBWireframe);

    D3D12_CPU_DESCRIPTOR_HANDLE hSRVs[4] = {};
    hSRVs[0] = m_HeightMap.GetSRV();
    hSRVs[1] = m_GradientMap.GetSRV();
    pContext->SetDynamicDescriptors(TerrainRootParam_DTHeightmap, 0, 2, hSRVs);

    for (int i = 0; i < m_nRings; ++i)
    {
        if (LODShaderEnabled && i > 0)
        {
            pContext->SetPipelineState(m_TessellationDistantPSO);
        }

        const TileRing* pRing = m_pTileRings[i];
        pRing->SetRenderingState(pContext);

        m_CBTerrain.tileWorldSize.x = pRing->tileSize();
        m_CBTerrain.tileWorldSize.y = m_OuterRingWorldSize;

        pContext->SetDynamicConstantBufferView(TerrainRootParam_CBTerrain, sizeof(m_CBTerrain), &m_CBTerrain);

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
    mScale = XMMatrixScalingFromVector(XMVectorReplicate(g_WorldScale));

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
    if (g_PlaceAtOrigin)
    {
        mTrans = XMMatrixIdentity();
    }
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

    XMMATRIX matCCWorld = mWorld;
    XMVECTOR ccOffset = mWorld.r[3] - XMLoadFloat4A(&pDesc->CameraPosWorld);
    matCCWorld.r[3] = XMVectorSelect(g_XMOne, ccOffset, g_XMSelect1110);
    XMMATRIX mModelToShadow = matCCWorld * XMLoadFloat4x4A(&pDesc->matWorldToShadow);
    XMMATRIX mModelToShadowOuter = matCCWorld * XMLoadFloat4x4A(&pDesc->matWorldToShadowOuter);
    XMStoreFloat4x4(&m_CBTerrain.ModelToShadow, mModelToShadow);
    XMStoreFloat4x4(&m_CBTerrain.ModelToShadowOuter, mModelToShadowOuter);
    XMStoreFloat4x4(&m_CBTerrain.World, matCCWorld);

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

void TessellatedTerrain::UIRender(TextContext& Text)
{
    if (!g_TerrainEnabled)
    {
        return;
    }

    INT Xpos = 0;
    const INT Ypos = 100;
    const INT Width = (INT)g_HeightmapDimension / 4;
    const INT Spacing = 10;
    if (g_DrawHeightmap)
    {
        Text.DrawTexturedRect(m_HeightMap.GetSRV(), Xpos, Ypos, Width, Width, true);
        Xpos += (Width + Spacing);
        Text.DrawTexturedRect(m_ZoneMap.GetSRV(), Xpos, Ypos, Width, Width, false);
        Xpos += (Width + Spacing);

        for (UINT32 i = 0; i < 4; ++i)
        {
            ColorBuffer& HM = m_DebugPhysicsHeightMaps[i];
            INT OffsetX = (i % 2) * (m_PhysicsFootprint.Width + 4);
            INT OffsetY = (i / 2) * (m_PhysicsFootprint.Height + 4);
            Text.DrawTexturedRect(HM.GetSRV(), Xpos + OffsetX, Ypos + OffsetY, m_PhysicsFootprint.Width, m_PhysicsFootprint.Height, true);
        }
    }
    if (g_DrawGradientmap)
    {
        Text.DrawTexturedRect(m_GradientMap.GetSRV(), Xpos, Ypos, Width, Width, false);
        Xpos += (Width + Spacing);
    }

//     Text.SetCursorX(0);
//     Text.SetCursorY(Ypos + Width + Spacing);
//     const XMFLOAT4& TWO = m_CBCommon.TextureWorldOffset;
//     Text.DrawFormattedString("Terrain Eye XYZ: <%0.2f, %0.2f, %0.2f>", TWO.x, TWO.y, TWO.z);
}

UINT32 TessellatedTerrain::PhysicsRender(GraphicsContext* pContext, const XMVECTOR& EyePos, FLOAT WorldScale, const FLOAT** ppHeightSamples, D3D12_SUBRESOURCE_FOOTPRINT* pFootprint)
{
    const UINT32 FootprintIndex = FindAvailablePhysicsHeightmap();
    if (FootprintIndex == -1)
    {
        assert(FALSE);
        return -1;
    }

    const UINT32 HeightmapWidth = m_PhysicsHeightMap.GetWidth();
    const FLOAT PlusOneScalingFactor = (FLOAT)HeightmapWidth / (FLOAT)(HeightmapWidth - 1);
    const FLOAT UVScale = PlusOneScalingFactor * ((WorldScale / (g_WorldScale * 2)) / 16.0f);

    XMFLOAT4 CameraPos;
    XMStoreFloat4(&CameraPos, EyePos);
    RenderTerrainHeightmap(pContext, &m_PhysicsHeightMap, &m_PhysicsZoneMap, nullptr, CameraPos, UVScale);

    ColorBuffer& DebugBuffer = m_DebugPhysicsHeightMaps[m_CurrentDebugHeightmapIndex];
    m_CurrentDebugHeightmapIndex = (m_CurrentDebugHeightmapIndex + 1) % ARRAYSIZE(m_DebugPhysicsHeightMaps);
    pContext->TransitionResource(DebugBuffer, D3D12_RESOURCE_STATE_COPY_DEST);
    pContext->CopySubresource(DebugBuffer, 0, m_PhysicsHeightMap, 0);
    pContext->TransitionResource(DebugBuffer, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT PlacedFootprint = {};
    PlacedFootprint.Footprint = m_PhysicsFootprint;
    PlacedFootprint.Offset = m_FootprintSizeBytes * FootprintIndex;
    pContext->CopySubresource(m_ReadbackResource, PlacedFootprint, m_PhysicsHeightMap, 0);

    if (pFootprint != nullptr)
    {
        *pFootprint = m_PhysicsFootprint;
    }
    if (ppHeightSamples != nullptr)
    {
        *ppHeightSamples = (FLOAT*)(m_pReadbackData + PlacedFootprint.Offset);
    }

    return FootprintIndex;
}

UINT32 TessellatedTerrain::FindAvailablePhysicsHeightmap()
{
    if (m_AvailableMapMask == 0)
    {
        return -1;
    }

    DWORD Shift = 64;
    _BitScanForward64(&Shift, m_AvailableMapMask);
    const UINT64 Mask = 1Ui64 << Shift;
    assert((Mask & m_AvailableMapMask) == Mask);
    m_AvailableMapMask &= ~Mask;

    return Shift;
}

void TessellatedTerrain::FreePhysicsHeightmap(UINT32 Index)
{
    assert(Index < m_PhysicsHeightmapCount);
    const UINT64 Mask = 1Ui64 << Index;
    assert((m_AvailableMapMask & Mask) == 0);
    m_AvailableMapMask |= Mask;
}
