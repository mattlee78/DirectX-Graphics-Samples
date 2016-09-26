#pragma once

#include <windows.h>
#include <assert.h>

template< size_t ChunkSize >
class ZoneAllocator
{
private:
    struct ZoneChunk
    {
        ZoneChunk* pNext;
        VOID* pBuffer;
        SIZE_T SizeRemaining;
    };

private:
    ZoneChunk* m_pRootChunk;
    ZoneChunk* m_pCurrentChunk;

public:
    ZoneAllocator()
        : m_pRootChunk( nullptr ),
        m_pCurrentChunk( nullptr )
    {
    }

    ~ZoneAllocator()
    {
        ZoneChunk* p = m_pRootChunk;
        while( p != nullptr )
        {
            ZoneChunk* pNext = p->pNext;
            free( p->pBuffer );
            delete p;
            p = pNext;
        }

        m_pRootChunk = nullptr;
        m_pCurrentChunk = nullptr;
    }

    template< class T >
    T* Allocate()
    {
        return (T*)AllocateBytes( sizeof(T) );
    }

    VOID* AllocateBytes( SIZE_T SizeBytes )
    {
        assert( SizeBytes <= ChunkSize );
        if( m_pCurrentChunk == nullptr ||
            m_pCurrentChunk->SizeRemaining < SizeBytes )
        {
            ZoneChunk* pNewChunk = new ZoneChunk();
            pNewChunk->pBuffer = malloc( ChunkSize );
            pNewChunk->SizeRemaining = ChunkSize;
            pNewChunk->pNext = nullptr;

            if( m_pCurrentChunk != nullptr )
            {
                assert( m_pCurrentChunk->pNext == nullptr );
                m_pCurrentChunk->pNext = pNewChunk;
                m_pCurrentChunk = pNewChunk;
            }
            else
            {
                assert( m_pRootChunk == nullptr );
                m_pCurrentChunk = pNewChunk;
                m_pRootChunk = pNewChunk;
            }
        }

        assert( m_pCurrentChunk->SizeRemaining >= SizeBytes );
        VOID* pReturn = (VOID*)( (BYTE*)m_pCurrentChunk->pBuffer + ( ChunkSize - m_pCurrentChunk->SizeRemaining ) );
        m_pCurrentChunk->SizeRemaining -= SizeBytes;

        return pReturn;
    }
};

