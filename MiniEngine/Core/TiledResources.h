#pragma once

#include "PixelBuffer.h"

class IndexList
{
private:
    std::vector<USHORT> m_Indices;
    USHORT m_FirstFreeIndex;
    USHORT m_FreeCount;

public:
    static const USHORT InvalidIndex = -1;

    IndexList(USHORT InitialSize = 0)
    {
        m_FirstFreeIndex = InvalidIndex;
        m_FreeCount = 0;
        AddIndices(InitialSize);
    }

    USHORT GetIndexCount() const { return (USHORT)m_Indices.size(); }

    void AddIndices(UINT32 Count)
    {
        if (Count == 0)
        {
            return;
        }

        m_Indices.reserve(m_Indices.size() + Count);
        const USHORT FirstIndex = (USHORT)m_Indices.size();
        for (USHORT i = 0; i < (Count - 1); ++i)
        {
            m_Indices.push_back(i + 1);
        }
        m_Indices.push_back(m_FirstFreeIndex);
        m_FirstFreeIndex = FirstIndex;
        m_FreeCount += (USHORT)Count;
    }

    USHORT Allocate(UINT32 Count = 1)
    {
        if (m_FreeCount < Count)
        {
            return InvalidIndex;
        }

        USHORT ReturnIndex = InvalidIndex;
        while (Count > 0)
        {
            assert(m_FirstFreeIndex != InvalidIndex);
            const USHORT NextFree = m_Indices[m_FirstFreeIndex];
            m_Indices[m_FirstFreeIndex] = ReturnIndex;
            ReturnIndex = m_FirstFreeIndex;
            m_FirstFreeIndex = NextFree;
            --m_FreeCount;
            --Count;
        }

        return ReturnIndex;
    }

    void Free(USHORT FirstIndex)
    {
        while (FirstIndex != InvalidIndex)
        {
            assert(FirstIndex < (USHORT)m_Indices.size());
            USHORT NextIndex = m_Indices[FirstIndex];
            m_Indices[FirstIndex] = InvalidIndex;
            m_FirstFreeIndex = FirstIndex;
            FirstIndex = NextIndex;
        }
    }

    USHORT GetNextIndex(USHORT Index) const
    {
        assert(Index < (USHORT)m_Indices.size());
        return m_Indices[Index];
    }
};

class TiledTextureBuffer;

class ElasticTilePool
{
private:
    struct HeapSlab
    {
        ID3D12Heap* pHeap;
    };

    HeapSlab m_Slabs[32];
    UINT32 m_SlabCount;
    UINT32 m_SlabTileShift;
    UINT32 m_TileGroupShift;

    IndexList m_FreeTiles;

public:
    ElasticTilePool();
    ~ElasticTilePool();

    void Initialize(UINT32 TileGroupShift, UINT32 SlabTileShift, UINT32 SlabCount);
    void Terminate();

    bool MapTiledTextureSubresource(ID3D12CommandQueue* pQueue, TiledTextureBuffer* pTexture, UINT32 MipIndex, UINT32 SliceIndex = 0);
    bool UnmapTiledTextureSubresource(ID3D12CommandQueue* pQueue, TiledTextureBuffer* pTexture, UINT32 MipIndex, UINT32 SliceIndex = 0);
};

extern ElasticTilePool g_TilePool;

class TiledTextureBuffer : public PixelBuffer
{
public:
    TiledTextureBuffer()
    {
        m_SRVHandle.ptr = D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN;
        std::memset(m_UAVHandle, 0xFF, sizeof(m_UAVHandle));
    }

    void Create(const std::wstring& Name, uint32_t Width, uint32_t Height, uint32_t ArrayCount, uint32_t NumMips, DXGI_FORMAT Format);

    // Get pre-created CPU-visible descriptor handles
    const D3D12_CPU_DESCRIPTOR_HANDLE& GetSRV(void) const { return m_SRVHandle; }
    const D3D12_CPU_DESCRIPTOR_HANDLE& GetUAV(void) const { return m_UAVHandle[0]; }

protected:
    friend class ElasticTilePool;

    void CreateDerivedViews(ID3D12Device* Device, DXGI_FORMAT Format, uint32_t ArraySize, uint32_t NumMips = 1);

    D3D12_CPU_DESCRIPTOR_HANDLE m_SRVHandle;
    D3D12_CPU_DESCRIPTOR_HANDLE m_UAVHandle[12];
    uint32_t m_NumMipMaps; // number of texture sublevels

    UINT32 m_MipCount;
    UINT32 m_TotalTileCount;
    D3D12_PACKED_MIP_INFO m_PackedMips;
    D3D12_TILE_SHAPE m_TileShape;
    D3D12_SUBRESOURCE_TILING* m_pSubresourceTilings;

    USHORT* m_pTileIndices;
};
