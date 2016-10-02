#pragma once

#include <windows.h>
#include "LineProtocol.h"

struct NetPacketHeader;
struct PacketQueue
{
private:
    UINT32 m_FrameIndex;
    LARGE_INTEGER m_Timestamp;

    BYTE* m_pBuffer;
    SIZE_T m_TotalSizeBytes;
    SIZE_T m_UsedSizeBytes;

public:
    PacketQueue(SIZE_T InitialSizeBytes = 65536)
    {
        m_FrameIndex = -1;
        m_Timestamp.QuadPart = 0;
        m_pBuffer = nullptr;
        m_TotalSizeBytes = 0;
        m_UsedSizeBytes = 0;
        Allocate(InitialSizeBytes);
    }

    ~PacketQueue()
    {
        m_TotalSizeBytes = 0;
        m_UsedSizeBytes = 0;
        delete[] m_pBuffer;
        m_pBuffer = nullptr;
    }

    void SetFrameIndex(UINT32 FrameIndex, LARGE_INTEGER Timestamp)
    {
        assert(m_FrameIndex == -1 || m_FrameIndex == FrameIndex);
        m_FrameIndex = FrameIndex;
        m_Timestamp = Timestamp;
    }
    UINT32 GetFrameIndex() const { return m_FrameIndex; }
    LARGE_INTEGER GetTimestamp() const { return m_Timestamp; }

    void Reset()
    {
        m_FrameIndex = -1;
        m_Timestamp.QuadPart = 0;
        m_UsedSizeBytes = 0;
    }

    // write:
    bool CopyPacket(const NetPacketHeader* pSrcPacket);

    // read:
    SIZE_T GetUsedSizeBytes() const { return m_UsedSizeBytes; }
    void GetBuffer(const BYTE** ppBuffer, SIZE_T* pSizeBytes) const
    {
        *ppBuffer = m_pBuffer;
        *pSizeBytes = m_UsedSizeBytes;
    }
    bool GetNextPacket(const NetPacketHeader** ppPacket) const;

private:
    bool Allocate(SIZE_T NewSizeBytes);
};

inline bool PacketQueue::Allocate(SIZE_T NewSizeBytes)
{
    if (NewSizeBytes <= m_TotalSizeBytes)
    {
        return true;
    }

    BYTE* pNewBuffer = new BYTE[NewSizeBytes];
    if (pNewBuffer == nullptr)
    {
        return false;
    }
    if (m_UsedSizeBytes > 0)
    {
        memcpy(pNewBuffer, m_pBuffer, m_UsedSizeBytes);
    }
    delete[] m_pBuffer;
    m_pBuffer = pNewBuffer;
    m_TotalSizeBytes = NewSizeBytes;
    return true;
}

inline bool PacketQueue::CopyPacket(const NetPacketHeader* pSrcPacket)
{
    assert(pSrcPacket != nullptr);

    const SIZE_T PacketSizeBytes = pSrcPacket->GetByteCount();
    if (PacketSizeBytes + m_UsedSizeBytes > m_TotalSizeBytes)
    {
        SIZE_T NewSizeBytes = (m_UsedSizeBytes + PacketSizeBytes + 65535) & ~0xFFFF;
        if (!Allocate(NewSizeBytes))
        {
            return false;
        }
    }

    BYTE* pDest = m_pBuffer + m_UsedSizeBytes;
    memcpy(pDest, pSrcPacket, PacketSizeBytes);
    m_UsedSizeBytes += PacketSizeBytes;
    return true;
}

inline bool PacketQueue::GetNextPacket(const NetPacketHeader** ppPacket) const
{
    assert(ppPacket != nullptr);

    if (m_UsedSizeBytes <= 0)
    {
        *ppPacket = nullptr;
        return false;
    }

    const NetPacketHeader* pCurrentPacket = *ppPacket;
    if (pCurrentPacket == nullptr)
    {
        pCurrentPacket = (const NetPacketHeader*)m_pBuffer;
        *ppPacket = pCurrentPacket;
        return true;
    }
    else
    {
        const SIZE_T CurrentPos = (const BYTE*)pCurrentPacket - m_pBuffer;
        assert(CurrentPos < m_UsedSizeBytes);
        const SIZE_T PacketSizeBytes = pCurrentPacket->GetByteCount();
        assert(CurrentPos + PacketSizeBytes <= m_UsedSizeBytes);
        SIZE_T NextPos = CurrentPos + PacketSizeBytes;
        if (NextPos >= m_UsedSizeBytes)
        {
            *ppPacket = nullptr;
            return false;
        }
        *ppPacket = (const NetPacketHeader*)(m_pBuffer + NextPos);
        return true;
    }
}
