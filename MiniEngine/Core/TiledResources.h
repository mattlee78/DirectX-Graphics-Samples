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
    USHORT GetFreeIndexCount() const { return m_FreeCount; }

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
        ValidateFreeCount();
    }

    USHORT Allocate(UINT32 Count = 1)
    {
        if (m_FreeCount < Count)
        {
            return InvalidIndex;
        }

        const UINT32 CheckCount = Count;
        const UINT32 CheckFreeCount = m_FreeCount;
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
        ValidateFreeCount();
        assert(CountList(ReturnIndex) == CheckCount);
        assert(CheckCount + m_FreeCount == CheckFreeCount);

        return ReturnIndex;
    }

    void Free(USHORT FirstIndex)
    {
        while (FirstIndex != InvalidIndex)
        {
            assert(FirstIndex < (USHORT)m_Indices.size());
            USHORT NextIndex = m_Indices[FirstIndex];
            m_Indices[FirstIndex] = m_FirstFreeIndex;
            m_FirstFreeIndex = FirstIndex;
            FirstIndex = NextIndex;
            ++m_FreeCount;
            assert(m_FreeCount <= m_Indices.size());
        }
        ValidateFreeCount();
    }

    USHORT GetNextIndex(USHORT Index) const
    {
        assert(Index < (USHORT)m_Indices.size());
        return m_Indices[Index];
    }

private:
    void ValidateFreeCount() const
    {
        assert(CountList(m_FirstFreeIndex) == m_FreeCount);
    }

    USHORT CountList(USHORT StartIndex) const
    {
        UINT32 Count = 0;
        USHORT i = StartIndex;
        while (i != InvalidIndex)
        {
            ++Count;
            i = m_Indices[i];
        }
        return Count;
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

    UINT32 GetFreeTileGroupCount() const { return (UINT32)m_FreeTiles.GetFreeIndexCount(); }
    UINT32 GetTileGroupCount(UINT32 TileCount) const { return (TileCount + (1 << m_TileGroupShift) - 1) >> m_TileGroupShift; }

    bool MapTiledTextureSubresource(ID3D12CommandQueue* pQueue, TiledTextureBuffer* pTexture, UINT32 MipIndex, UINT32 SliceIndex = 0);
    bool UnmapTiledTextureSubresource(ID3D12CommandQueue* pQueue, TiledTextureBuffer* pTexture, UINT32 MipIndex, UINT32 SliceIndex = 0);

    void FreeTiledTextureTiles(TiledTextureBuffer* pTexture);
};

extern ElasticTilePool g_TilePool;

class TiledTextureBuffer : public PixelBuffer
{
public:
    TiledTextureBuffer()
    {
        m_SRVHandle.ptr = D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN;
        std::memset(m_UAVHandle, 0xFF, sizeof(m_UAVHandle));
        m_SubresourceCount = 0;
        m_TotalTileCount = 0;
        ZeroMemory(&m_PackedMips, sizeof(m_PackedMips));
        ZeroMemory(&m_TileShape, sizeof(m_TileShape));
        m_pSubresourceTilings = nullptr;
        m_pTileIndices = nullptr;
    }

    void Create(const std::wstring& Name, uint32_t Width, uint32_t Height, uint32_t ArrayCount, uint32_t NumMips, DXGI_FORMAT Format);

    // Get pre-created CPU-visible descriptor handles
    const D3D12_CPU_DESCRIPTOR_HANDLE& GetSRV(void) const { return m_SRVHandle; }
    const D3D12_CPU_DESCRIPTOR_HANDLE& GetUAV(void) const { return m_UAVHandle[0]; }

    bool IsSubresourceMapped(UINT32 Index) const { return m_pTileIndices[Index] != IndexList::InvalidIndex; }
    UINT32 GetTotalTileCount() const { return m_TotalTileCount; }

protected:
    friend class ElasticTilePool;

    void CreateDerivedViews(ID3D12Device* Device, DXGI_FORMAT Format, uint32_t ArraySize, uint32_t NumMips = 1);

    D3D12_CPU_DESCRIPTOR_HANDLE m_SRVHandle;
    D3D12_CPU_DESCRIPTOR_HANDLE m_UAVHandle[12];
    uint32_t m_NumMipMaps; // number of texture sublevels

    UINT32 m_SubresourceCount;
    UINT32 m_TotalTileCount;
    D3D12_PACKED_MIP_INFO m_PackedMips;
    D3D12_TILE_SHAPE m_TileShape;
    D3D12_SUBRESOURCE_TILING* m_pSubresourceTilings;

    USHORT* m_pTileIndices;
};
