#include "pch.h"
#include "TiledResources.h"
#include "GraphicsCore.h"

ElasticTilePool g_TilePool;
static const UINT32 g_DefaultSlabTileShift = 10;

ElasticTilePool::ElasticTilePool()
{
    ZeroMemory(m_Slabs, sizeof(m_Slabs));
}

ElasticTilePool::~ElasticTilePool()
{
}

void ElasticTilePool::Initialize(UINT32 TileGroupShift, UINT32 SlabTileShift, UINT32 SlabCount)
{
    assert(SlabCount <= ARRAYSIZE(m_Slabs));
    assert(TileGroupShift < g_DefaultSlabTileShift);
    if (SlabTileShift == 0)
    {
        SlabTileShift = g_DefaultSlabTileShift - TileGroupShift;
    }
    m_SlabTileShift = SlabTileShift;
    m_TileGroupShift = TileGroupShift;
    m_SlabCount = SlabCount;

    D3D12_HEAP_DESC HeapDesc = {};
    HeapDesc.SizeInBytes = (UINT64)(D3D12_TILED_RESOURCE_TILE_SIZE_IN_BYTES << TileGroupShift) << SlabTileShift;
    HeapDesc.Alignment = D3D12_TILED_RESOURCE_TILE_SIZE_IN_BYTES;
    HeapDesc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
    HeapDesc.Flags = D3D12_HEAP_FLAG_NONE;

    for (UINT32 i = 0; i < SlabCount; ++i)
    {
        HeapSlab& Slab = m_Slabs[i];
        Graphics::g_Device->CreateHeap(&HeapDesc, MY_IID_PPV_ARGS(&Slab.pHeap));
    }

    m_FreeTiles.AddIndices((1U << SlabTileShift) * SlabCount);
}

void ElasticTilePool::Terminate()
{
    for (UINT32 i = 0; i < m_SlabCount; ++i)
    {
        m_Slabs[i].pHeap->Release();
    }
}

bool ElasticTilePool::MapTiledTextureSubresource(ID3D12CommandQueue* pQueue, TiledTextureBuffer* pTexture, UINT32 MipIndex, UINT32 SliceIndex)
{
    const UINT32 SubresourceIndex = ((pTexture->m_NumMipMaps + 1) * SliceIndex) + MipIndex;
    USHORT TileIndex = pTexture->m_pTileIndices[SubresourceIndex];

    if (TileIndex != IndexList::InvalidIndex)
    {
        return false;
    }

    const D3D12_SUBRESOURCE_TILING& SubTiling = pTexture->m_pSubresourceTilings[SubresourceIndex];
    if (SubTiling.StartTileIndexInOverallResource == D3D12_PACKED_TILE)
    {
        return false;
    }

    const UINT32 TileSliceSize = SubTiling.WidthInTiles * SubTiling.HeightInTiles;

    UINT32 TileCount = (SubTiling.WidthInTiles * SubTiling.HeightInTiles * SubTiling.DepthInTiles);
    const UINT32 TileGroupCount = (TileCount + (1 << m_TileGroupShift) - 1) >> m_TileGroupShift;
    assert(TileGroupCount > 0 && (TileGroupCount << m_TileGroupShift) >= TileCount);

    TileIndex = m_FreeTiles.Allocate(TileGroupCount);
    if (TileIndex == IndexList::InvalidIndex)
    {
        return false;
    }

    pTexture->m_pTileIndices[SubresourceIndex] = TileIndex;

    D3D12_TILED_RESOURCE_COORDINATE StartCoord[32];
    D3D12_TILE_REGION_SIZE RegionSizes[32];
    D3D12_TILE_RANGE_FLAGS RangeFlags[32];
    UINT RangeStartOffsets[32];
    UINT RangeTileCounts[32];

    ID3D12Heap* pPrevHeap = nullptr;
    UINT32 RangeCount = 0;
    UINT32 TileWithinSubresource = 0;

    while (TileIndex != IndexList::InvalidIndex)
    {
        const UINT32 SlabIndex = (UINT32)(TileIndex >> m_SlabTileShift);
        const UINT32 IndexWithinSlab = (UINT32)TileIndex & ((1U << m_SlabTileShift) - 1);
        assert(SlabIndex < m_SlabCount);
        HeapSlab& Slab = m_Slabs[SlabIndex];
        assert(Slab.pHeap != nullptr);
        if (Slab.pHeap != pPrevHeap)
        {
            if (pPrevHeap != nullptr)
            {
                pQueue->UpdateTileMappings(pTexture->GetResource(), RangeCount, StartCoord, RegionSizes, pPrevHeap, RangeCount, RangeFlags, RangeStartOffsets, RangeTileCounts, D3D12_TILE_MAPPING_FLAG_NO_HAZARD);
            }
            RangeCount = 0;
            pPrevHeap = Slab.pHeap;
        }

        const UINT32 RangeTileCount = std::min(TileCount, 1U << m_TileGroupShift);

        StartCoord[RangeCount].Subresource = SubresourceIndex;
        StartCoord[RangeCount].X = (TileWithinSubresource % SubTiling.WidthInTiles);
        StartCoord[RangeCount].Y = (TileWithinSubresource % TileSliceSize) / SubTiling.WidthInTiles;
        StartCoord[RangeCount].Z = TileWithinSubresource / TileSliceSize;

        RegionSizes[RangeCount].NumTiles = RangeTileCount;
        RegionSizes[RangeCount].UseBox = FALSE;

        RangeFlags[RangeCount] = D3D12_TILE_RANGE_FLAG_NONE;

        RangeStartOffsets[RangeCount] = IndexWithinSlab << m_TileGroupShift;
        RangeTileCounts[RangeCount] = RangeTileCount;

        TileIndex = m_FreeTiles.GetNextIndex(TileIndex);
        ++RangeCount;
        TileWithinSubresource += RangeTileCount;
        TileCount -= RangeTileCount;
    }

    if (RangeCount > 0)
    {
        pQueue->UpdateTileMappings(pTexture->GetResource(), RangeCount, StartCoord, RegionSizes, pPrevHeap, RangeCount, RangeFlags, RangeStartOffsets, RangeTileCounts, D3D12_TILE_MAPPING_FLAG_NO_HAZARD);
    }

    return true;
}

bool ElasticTilePool::UnmapTiledTextureSubresource(ID3D12CommandQueue* pQueue, TiledTextureBuffer* pTexture, UINT32 MipIndex, UINT32 SliceIndex)
{
    const UINT32 SubresourceIndex = ((pTexture->m_NumMipMaps + 1) * SliceIndex) + MipIndex;
    const D3D12_SUBRESOURCE_TILING& SubTiling = pTexture->m_pSubresourceTilings[SubresourceIndex];
    if (SubTiling.StartTileIndexInOverallResource == D3D12_PACKED_TILE)
    {
        return false;
    }

    USHORT TileIndex = pTexture->m_pTileIndices[SubresourceIndex];
    if (TileIndex == IndexList::InvalidIndex)
    {
        return true;
    }

    m_FreeTiles.Free(TileIndex);

    const UINT32 TileCount = SubTiling.WidthInTiles * SubTiling.HeightInTiles * SubTiling.DepthInTiles;
    D3D12_TILED_RESOURCE_COORDINATE StartCoord = {};
    StartCoord.Subresource = SubresourceIndex;
    D3D12_TILE_REGION_SIZE RegionSize = {};
    RegionSize.NumTiles = TileCount;
    RegionSize.UseBox = FALSE;
    D3D12_TILE_RANGE_FLAGS RangeFlag = D3D12_TILE_RANGE_FLAG_NULL;
    UINT RangeStartOffset = 0;
    UINT RangeTileCount = TileCount;

    pQueue->UpdateTileMappings(pTexture->GetResource(), 1, &StartCoord, &RegionSize, nullptr, 1, &RangeFlag, &RangeStartOffset, &RangeTileCount, D3D12_TILE_MAPPING_FLAG_NO_HAZARD);

    return true;
}

void ElasticTilePool::FreeTiledTextureTiles(TiledTextureBuffer* pTexture)
{
    for (UINT32 i = 0; i < pTexture->m_SubresourceCount; ++i)
    {
        m_FreeTiles.Free(pTexture->m_pTileIndices[i]);
        pTexture->m_pTileIndices[i] = IndexList::InvalidIndex;
    }
}

void TiledTextureBuffer::Create(const std::wstring& Name, uint32_t Width, uint32_t Height, uint32_t ArrayCount, uint32_t NumMips, DXGI_FORMAT Format)
{
    D3D12_RESOURCE_DESC ResourceDesc = DescribeTex2D(Width, Height, ArrayCount, 1, Format, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    ResourceDesc.Layout = D3D12_TEXTURE_LAYOUT_64KB_UNDEFINED_SWIZZLE;

    CreateReservedResource(Graphics::g_Device, Name, ResourceDesc);
    CreateDerivedViews(Graphics::g_Device, Format, ArrayCount, 1);

    const UINT32 SubresourceCount = ArrayCount * NumMips;
    m_SubresourceCount = SubresourceCount;
    m_pSubresourceTilings = new D3D12_SUBRESOURCE_TILING[SubresourceCount];
    ZeroMemory(m_pSubresourceTilings, SubresourceCount * sizeof(D3D12_SUBRESOURCE_TILING));

    UINT32 NumSubresourceTilings = SubresourceCount;
    Graphics::g_Device->GetResourceTiling(m_pResource.Get(), &m_TotalTileCount, &m_PackedMips, &m_TileShape, &NumSubresourceTilings, 0, m_pSubresourceTilings);

    m_pTileIndices = new USHORT[SubresourceCount];
    for (UINT32 i = 0; i < SubresourceCount; ++i)
    {
        m_pTileIndices[i] = IndexList::InvalidIndex;
    }
}

void TiledTextureBuffer::CreateDerivedViews(ID3D12Device* Device, DXGI_FORMAT Format, uint32_t ArraySize, uint32_t NumMips)
{
    ASSERT(ArraySize == 1 || NumMips == 1, "We don't support auto-mips on texture arrays");

    m_NumMipMaps = NumMips - 1;

    D3D12_UNORDERED_ACCESS_VIEW_DESC UAVDesc = {};
    D3D12_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};

    UAVDesc.Format = GetUAVFormat(Format);
    SRVDesc.Format = Format;
    SRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

    if (ArraySize > 1)
    {
        UAVDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
        UAVDesc.Texture2DArray.MipSlice = 0;
        UAVDesc.Texture2DArray.FirstArraySlice = 0;
        UAVDesc.Texture2DArray.ArraySize = (UINT)ArraySize;

        SRVDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
        SRVDesc.Texture2DArray.MipLevels = NumMips;
        SRVDesc.Texture2DArray.MostDetailedMip = 0;
        SRVDesc.Texture2DArray.FirstArraySlice = 0;
        SRVDesc.Texture2DArray.ArraySize = (UINT)ArraySize;
    }
    else
    {
        UAVDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        UAVDesc.Texture2D.MipSlice = 0;

        SRVDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        SRVDesc.Texture2D.MipLevels = NumMips;
        SRVDesc.Texture2D.MostDetailedMip = 0;
    }

    if (m_SRVHandle.ptr == D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN)
    {
        m_SRVHandle = Graphics::AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }

    ID3D12Resource* Resource = m_pResource.Get();

    // Create the shader resource view
    Device->CreateShaderResourceView(Resource, &SRVDesc, m_SRVHandle);

    // Create the UAVs for each mip level (RWTexture2D)
    for (uint32_t i = 0; i < NumMips; ++i)
    {
        if (m_UAVHandle[i].ptr == D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN)
            m_UAVHandle[i] = Graphics::AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

        Device->CreateUnorderedAccessView(Resource, nullptr, &UAVDesc, m_UAVHandle[i]);

        UAVDesc.Texture2D.MipSlice++;
    }
}
