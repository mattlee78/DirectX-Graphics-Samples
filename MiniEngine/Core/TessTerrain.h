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

//----------------------------------------------------------------------------------
// Defines and draws one ring of tiles in a concentric, nested set of rings.  Each ring
// is a different LOD and uses a different tile/patch size.  These are actually square
// rings.  (Is there a term for that?)  But they could conceivably change to circular.
// The inner-most LOD is a square, represented by a degenerate ring.
//
#pragma once

#include <d3d12.h>

#include <DirectXMath.h>
#include <DirectXPackedVector.h>
#include <DirectXCollision.h>
using namespace DirectX;
using namespace DirectX::PackedVector;

#include "GpuBuffer.h"
#include "CommandContext.h"
#include "ColorBuffer.h"

struct InstanceData;
struct Adjacency;
struct CBLightShadowWorldConstants;
namespace Graphics
{
    class Model;
}

// Int dimensions specified to the ctor are in numbers of tiles.  It's symmetrical in
// each direction.  (Don't read much into the exact numbers of #s in this diagram.)
//
//    <-   outerWidth  ->
//    ###################
//    ###################
//    ###             ###
//    ###<-holeWidth->###
//    ###             ###
//    ###    (0,0)    ###
//    ###             ###
//    ###             ###
//    ###             ###
//    ###################
//    ###################
//
class TileRing
{
public:
	// holeWidth & outerWidth are nos. of tiles
	// tileSize is a world-space length
	TileRing(int holeWidth, int outerWidth, float tileSize);
	~TileRing();

// 	static void CreateInputLayout(ID3D11Device*, ID3DX11EffectTechnique*);
// 	static void ReleaseInputLayout();

    static void GetInputLayout(const D3D12_INPUT_ELEMENT_DESC** ppDescs, UINT32* pElementCount);

	void SetRenderingState(GraphicsContext* pContext) const;

	int   outerWidth() const { return m_outerWidth; }
	int   nTiles()     const { return m_nTiles; }
	float tileSize()   const { return m_tileSize; }
    float GetRadius()  const { return m_tileSize * (FLOAT)m_outerWidth; }

private:
	void CreateInstanceDataVB();
	bool InRing(int x, int y) const;
	void AssignNeighbourSizes(int x, int y, Adjacency*) const;

    StructuredBuffer m_PositionsVB;

	const int m_holeWidth, m_outerWidth, m_ringWidth;
	const int m_nTiles;
	const float m_tileSize;
	InstanceData* m_pVBData;

	// Revoked:
	TileRing(const TileRing&);
	TileRing& operator=(const TileRing&);
};

__declspec(align(16))
struct TessellatedTerrainRenderDesc
{
    XMFLOAT4X4A matView;
    XMFLOAT4X4A matProjection;
    XMFLOAT4X4A matWorldToShadow;
    XMFLOAT4X4A matWorldToShadowOuter;
    XMFLOAT4A CameraPosWorld;
    D3D12_VIEWPORT Viewport;
    bool ZPrePass;
    const CBLightShadowWorldConstants* pLightShadowConstants;
    D3D12_CPU_DESCRIPTOR_HANDLE* pExtraTextures;
};

struct InstancePlacementVertex
{
    XMFLOAT4 PositionXYZScale;
    XMFLOAT4 OrientationQuaternion;
    XMFLOAT4 UVRect;
};

class TessellatedTerrain
{
private:
    static const int VTX_PER_TILE_EDGE = 9;				// overlap => -2
    static const int TRI_STRIP_INDEX_COUNT = (VTX_PER_TILE_EDGE - 1) * (2 * VTX_PER_TILE_EDGE + 2);
    static const int QUAD_LIST_INDEX_COUNT = (VTX_PER_TILE_EDGE - 1) * (VTX_PER_TILE_EDGE - 1) * 4;
    static const int MAX_RINGS = 10;
    int m_nRings = 0;

    TileRing* m_pTileRings[MAX_RINGS];
    float m_OuterRingWorldSize = 0.0f;
    float m_SnapGridSize = 0.0f;

    bool m_Culling = true;

    ByteAddressBuffer m_TileTriStripIB;
    ByteAddressBuffer m_TileQuadListIB;

    ColorBuffer m_HeightMap;
    ColorBuffer m_GradientMap;
    ColorBuffer m_ZoneMap;
    ColorBuffer m_MaterialMap;

    D3D12_SUBRESOURCE_FOOTPRINT m_PhysicsFootprint;
    UINT32 m_FootprintSizeBytes;
    ColorBuffer m_PhysicsHeightMap;
    ColorBuffer m_PhysicsZoneMap;
    GpuResource m_ReadbackResource;
    BYTE* m_pReadbackData;
    UINT32 m_PhysicsHeightmapCount;
    UINT64 m_AvailableMapMask;
    ColorBuffer m_DebugPhysicsHeightMaps[4];
    UINT32 m_CurrentDebugHeightmapIndex;

    RootSignature m_RootSig;
    GraphicsPSO m_TessellationPSO;
    GraphicsPSO m_TessellationDistantPSO;
    GraphicsPSO m_TessellationWireframePSO;
    GraphicsPSO m_TessellationDepthPSO;
    GraphicsPSO m_NoTessellationPSO;
    GraphicsPSO m_NoTessellationWireframePSO;
    GraphicsPSO m_NoTessellationDepthPSO;

    GraphicsPSO m_InitializationPSO;
    GraphicsPSO m_GradientPSO;

    const ManagedTexture* m_pNoiseTexture;
    const ManagedTexture* m_pDetailNoiseTexture;
    const ManagedTexture* m_pDetailNoiseGradTexture;
    GpuResource m_ColorNoiseTexture;
    D3D12_CPU_DESCRIPTOR_HANDLE m_hColorNoiseSRV;

    struct TerrainTexture
    {
        const ManagedTexture* pDiffuseMap;
        const ManagedTexture* pHeightMap;
        const ManagedTexture* pNormalMap;
    };
    
    enum TerrainTextureLayers
    {
        TTL_Rock = 0,
        TTL_Dirt = 1,
        TTL_Grass = 2,
        TTL_Snow = 3,
        TTL_Sand = 4,
        TTL_Count
    };

    TerrainTexture m_TerrainTextures[TTL_Count];
    D3D12_CPU_DESCRIPTOR_HANDLE m_hTerrainTextures[TTL_Count * 4];

    __declspec(align(16))
    struct CBInstancedDecorationLayer
    {
        XMFLOAT4 ModelSpaceSizeOffset;
        XMFLOAT4 LODFadeRadius;
        XMFLOAT4 ModelSpaceTranslation;
    };

    struct InstanceSourcePlacementVertex
    {
        XMFLOAT2 PositionXZ;
        FLOAT RandomValue;
    };

    struct InstanceMeshVertex
    {
        XMHALF4 PositionXYZ;
        XMXDECN4 Normal;
        XMXDECN4 Tangent;
        XMXDECN4 Binormal;
        XMHALF2 TexCoord;
    };

    struct InstancedDecorationLayer
    {
        UINT32 InstanceCount;
        UINT32 InstanceArgumentIndex;
        UINT32 InstanceArgumentCount;

        StructuredBuffer InstancePlacementBuffer;
        D3D12_VERTEX_BUFFER_VIEW PlacementVBView;

        Graphics::Model* pModel;

        FLOAT VisibleRadius;
        FLOAT FadeRadius;
    };

    InstancedDecorationLayer m_InstanceLayers[8];
    UINT32 m_MaxInstanceCount;
    StructuredBuffer m_SourcePlacementBuffer;
    ComputePSO m_InstancePlacementPSO;
    GraphicsPSO m_InstanceRenderPSO;

    ByteAddressBuffer m_DrawInstancedArgumentBuffer;
    UINT32 m_DrawInstancedArgumentCount;

    __declspec(align(16))
    struct CBTerrain
    {
        XMFLOAT4 DetailNoiseScale;
        XMFLOAT4 DetailUVScale;

        XMFLOAT4 EyePos;
        XMFLOAT4 ViewDir;

        XMFLOAT4X4 WorldViewProj;
        XMFLOAT4X4 WorldViewLOD;
        XMFLOAT4X4 WorldViewProjLOD;
        XMFLOAT4X4 Proj;

        XMINT4 DebugShowPatches;
        XMINT4 DebugShowTiles;
        XMFLOAT4 fDisplacementHeight;
        XMFLOAT4 screenSize;
        XMINT4 tessellatedTriWidth;
        XMFLOAT4 tileWorldSize;

        XMFLOAT4X4 ModelToShadow;
        XMFLOAT4X4 ModelToShadowOuter;
        XMFLOAT4X4 World;
    } m_CBTerrain;
    C_ASSERT(sizeof(CBTerrain) == 38 * sizeof(XMFLOAT4));

    __declspec(align(16))
    struct CBCommon
    {
        XMINT4 FractalOctaves;
        XMFLOAT4 TextureWorldOffset;
        XMFLOAT4 CoarseSampleSpacing;
    } m_CBCommon;
    C_ASSERT(sizeof(CBCommon) == 3 * sizeof(XMFLOAT4));

    __declspec(align(16))
    struct CBDeform
    {
        XMFLOAT4 DeformMin;
        XMFLOAT4 DeformMax;
        XMFLOAT4 DeformConstants;
    } m_CBDeform;
    C_ASSERT(sizeof(CBDeform) == 3 * sizeof(XMFLOAT4));

    __declspec(align(16))
    struct CBWireframe
    {
        XMFLOAT4 Viewport;
        XMFLOAT4 WireWidth;
        XMFLOAT4 WireAlpha;
    } m_CBWireframe;
    C_ASSERT(sizeof(CBWireframe) == 3 * sizeof(XMFLOAT4));

public:
    TessellatedTerrain();

    void Initialize();
    void Terminate();

    FLOAT GetWorldScale() const;

    void OffscreenRender(GraphicsContext* pContext, const TessellatedTerrainRenderDesc* pDesc);
    void Render(GraphicsContext* pContext, const TessellatedTerrainRenderDesc* pDesc);
    void UIRender(TextContext& Text);

    UINT32 PhysicsRender(GraphicsContext* pContext, const XMVECTOR& EyePos, FLOAT WorldScale, const FLOAT** ppHeightSamples, D3D12_SUBRESOURCE_FOOTPRINT* pFootprint);
    void FreePhysicsHeightmap(UINT32 Index);

private:
    void CreateTileTriangleIB();
    void CreateTileQuadListIB();
    void CreateTextures();
    void CreatePhysicsTextures();
    void CreateNoiseTextures();
    void CreateRootSignature();
    void CreateTessellationPSO();
    void CreateDeformPSO();
    void CreateTileRings();
    void LoadTerrainTextures();
    void LoadTerrainTexture(const CHAR* strNamePrefix, TerrainTextureLayers TTL);

    void CreateInstancePSOs();
    void CreateInstanceLayers();
    void TerminateInstanceLayers();

    void RenderTerrainHeightmap(
        GraphicsContext* pContext, 
        ColorBuffer* pHeightmap, 
        ColorBuffer* pZonemap, 
        ColorBuffer* pGradientMap, 
        ColorBuffer* pMaterialMap,
        XMFLOAT4 CameraPosWorld,
        FLOAT UVScale);
    void RenderTerrain(GraphicsContext* pContext, const TessellatedTerrainRenderDesc* pDesc);
    void SetMatrices(const TessellatedTerrainRenderDesc* pDesc);
    void SetTextureWorldOffset(const XMFLOAT4& CameraPosWorld);
    UINT32 FindAvailablePhysicsHeightmap();
    void SetTerrainTextures(GraphicsContext* pContext);

    void RenderInstanceLayers(GraphicsContext* pContext, const TessellatedTerrainRenderDesc* pDesc);
    void UpdateInstanceLayer(ComputeContext* pContext, const TessellatedTerrainRenderDesc* pDesc, InstancedDecorationLayer& Layer);
    void RenderInstanceLayer(GraphicsContext* pContext, const TessellatedTerrainRenderDesc* pDesc, InstancedDecorationLayer& Layer);
};
