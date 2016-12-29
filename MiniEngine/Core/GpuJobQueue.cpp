#include "pch.h"
#include "GpuJobQueue.h"
#include "CommandContext.h"
#include "CommandListManager.h"
#include "TiledResources.h"

GpuJobQueue g_GpuJobQueue;

bool GraphicsJob::IsComplete() const
{
    if (CompletionFenceValue == 0)
    {
        // Not submitted yet
        return false;
    }
    return Graphics::g_CommandManager.GetGraphicsQueue().IsFenceComplete(CompletionFenceValue);
}

GpuJobQueue::GpuJobQueue()
{
    InitializeCriticalSection(&m_JobCritSec);
    for (UINT32 i = 0; i < 10; ++i)
    {
        m_CompleteGraphicsJobs.push_back(new GraphicsJob());
    }
}

GpuJobQueue::~GpuJobQueue()
{
    EnterCriticalSection(&m_JobCritSec);
    while (!m_CompleteGraphicsJobs.empty())
    {
        FreeGraphicsJob(m_CompleteGraphicsJobs.front());
        m_CompleteGraphicsJobs.pop_front();
    }
    while (!m_GraphicsJobs.empty())
    {
        FreeGraphicsJob(m_GraphicsJobs.front());
        m_GraphicsJobs.pop_front();
    }
    LeaveCriticalSection(&m_JobCritSec);
    DeleteCriticalSection(&m_JobCritSec);
}

void GpuJobQueue::AddPagingMapEntry(PagingQueueEntry* pEntry)
{
    m_PagingMapQueue.push_back(pEntry);
}

void GpuJobQueue::AddPagingUnmapEntry(PagingQueueEntry* pEntry)
{
    m_PagingUnmapQueue.push_back(pEntry);
}

void GpuJobQueue::RemovePagingEntry(PagingQueueEntry* pEntry)
{
    {
        auto iter = m_PagingMapQueue.begin();
        auto end = m_PagingMapQueue.end();
        while (iter != end)
        {
            if (*iter == pEntry)
            {
                m_PagingMapQueue.erase(iter);
                break;
            }
            ++iter;
        }
    }

    {
        auto iter = m_PagingUnmapQueue.begin();
        auto end = m_PagingUnmapQueue.end();
        while (iter != end)
        {
            if (*iter == pEntry)
            {
                m_PagingUnmapQueue.erase(iter);
                break;
            }
            ++iter;
        }
    }
}

GraphicsJob* GpuJobQueue::AllocGraphicsJob(bool ForceCreate)
{
    GraphicsJob* pJob = nullptr;

    if (!ForceCreate)
    {
        EnterCriticalSection(&m_JobCritSec);
        if (!m_CompleteGraphicsJobs.empty())
        {
            pJob = m_CompleteGraphicsJobs.front();
            m_CompleteGraphicsJobs.pop_front();
        }
        LeaveCriticalSection(&m_JobCritSec);
    }

    if (pJob == nullptr)
    {
        pJob = new GraphicsJob();
    }   

    return pJob;
}

void GpuJobQueue::FreeGraphicsJob(GraphicsJob* pJob)
{
    delete pJob;
}

GraphicsJob* GpuJobQueue::CreateGraphicsJob(const std::wstring& ID)
{
    GraphicsJob* pJob = AllocGraphicsJob(false);
    assert(pJob != nullptr);

    ZeroMemory(pJob, sizeof(*pJob));
    pJob->CancelJob = false;
    pJob->HoldJobOpen = false;
    pJob->pContext = &GraphicsContext::Begin(ID, true);

    return pJob;
}

void GpuJobQueue::SubmitGraphicsJob(GraphicsJob* pJob)
{
    assert(pJob != nullptr);

    EnterCriticalSection(&m_JobCritSec);
    m_GraphicsJobs.push_back(pJob);
    LeaveCriticalSection(&m_JobCritSec);
}

void GpuJobQueue::CloseGraphicsJob(GraphicsJob* pJob)
{
    pJob->HoldJobOpen = false;
    EnterCriticalSection(&m_JobCritSec);
    m_CompleteGraphicsJobs.push_back(pJob);
    LeaveCriticalSection(&m_JobCritSec);
}

void GpuJobQueue::ExecuteGraphicsJobs(UINT32 ExecutionBudgetUsec)
{
    PerformPageMapping();

    if (m_GraphicsJobs.empty())
    {
        return;
    }

    while (!m_GraphicsJobs.empty())
    {
        GraphicsJob* pJob = nullptr;

        EnterCriticalSection(&m_JobCritSec);
        if (!m_GraphicsJobs.empty())
        {
            pJob = m_GraphicsJobs.front();
        }
        if (pJob != nullptr && pJob->EstimatedGpuUsec <= ExecutionBudgetUsec)
        {
            m_GraphicsJobs.pop_front();
        }
        else
        {
            pJob = nullptr;
        }
        LeaveCriticalSection(&m_JobCritSec);

        if (pJob != nullptr)
        {
            ExecutionBudgetUsec -= ExecuteSingleGraphicsJob(pJob);
        }
    }
}

UINT32 GpuJobQueue::ExecuteSingleGraphicsJob(GraphicsJob* pJob)
{
    if (pJob->pContext == nullptr)
    {
        return 0;
    }

    UINT32 UsecElapsed = 0;
    if (!pJob->CancelJob)
    {
        assert(pJob->CompletionFenceValue == 0);
        UsecElapsed = pJob->EstimatedGpuUsec;
        UINT64 CompletionFence = pJob->pContext->Finish(false);
        pJob->CompletionFenceValue = CompletionFence;
        pJob->pContext = nullptr;
    }
    else
    {
        pJob->pContext->Finish(false, true);
        pJob->CompletionFenceValue = 0;
        pJob->pContext = nullptr;
    }

    if (!pJob->HoldJobOpen)
    {
        CloseGraphicsJob(pJob);
    }

    return UsecElapsed;
}

void GpuJobQueue::PerformPageMapping()
{
    ID3D12CommandQueue* pQueue = Graphics::g_CommandManager.GetQueue(D3D12_COMMAND_LIST_TYPE_DIRECT).GetCommandQueue();

    while (!m_PagingUnmapQueue.empty())
    {
        PagingQueueEntry* pEntry = m_PagingUnmapQueue.front();
        m_PagingUnmapQueue.pop_front();

        for (UINT32 i = 0; i < ARRAYSIZE(pEntry->TiledTexture); ++i)
        {
            if (pEntry->TiledTexture[i].IsSubresourceMapped(0))
            {
                g_TilePool.UnmapTiledTextureSubresource(pQueue, &pEntry->TiledTexture[i], 0, 0);
            }
        }
        pEntry->SetUnmapped();
    }

    PagingQueueEntryComparator Comparator;
    std::sort(m_PagingMapQueue.begin(), m_PagingMapQueue.end(), Comparator);

    auto iter = m_PagingMapQueue.begin();
    auto end = m_PagingMapQueue.end();
    while (iter != end)
    {
        auto nextiter = iter;
        ++nextiter;
        PagingQueueEntry* pEntry = *iter;
        if (pEntry->IsUnmapped())
        {
            m_PagingMapQueue.erase(iter);
            iter = nextiter;
            continue;
        }

        if (pEntry->SortKey == 0)
        {
            iter = nextiter;
            continue;
        }

        UINT32 TileGroupCount = 0;
        bool NextEntry = false;
        for (UINT32 i = 0; i < ARRAYSIZE(pEntry->TiledTexture); ++i)
        {
            if (pEntry->TiledTexture[i].IsSubresourceMapped(0))
            {
                NextEntry = true;
                break;
            }
            TileGroupCount += g_TilePool.GetTileGroupCount(pEntry->TiledTexture[i].GetTotalTileCount());
        }

        if (NextEntry)
        {
            iter = nextiter;
            continue;
        }

        if (TileGroupCount > g_TilePool.GetFreeTileGroupCount())
        {
            // Ran out of tiles.
            break;
        }

        for (UINT32 i = 0; i < ARRAYSIZE(pEntry->TiledTexture); ++i)
        {
            TiledTextureBuffer* pTT = &pEntry->TiledTexture[i];
            bool Success = g_TilePool.MapTiledTextureSubresource(pQueue, pTT, 0, 0);
            assert(Success);
        }

        if (pEntry->pGraphicsJob != nullptr)
        {
            ExecuteSingleGraphicsJob(pEntry->pGraphicsJob);
        }

        iter = nextiter;
    }
}
