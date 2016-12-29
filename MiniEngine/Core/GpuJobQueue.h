#pragma once

#include <deque>
#include "TiledResources.h"

class CommandContext;
class ComputeContext;

struct GraphicsJob
{
    UINT32 EstimatedGpuUsec;
    GraphicsContext* pContext;
    UINT64 CompletionFenceValue;
    bool HoldJobOpen;
    bool CancelJob;

    bool IsSubmitted() const { return CompletionFenceValue != 0; }
    bool IsComplete() const;
};

struct ComputeJob : public GraphicsJob
{
    ComputeContext* pComputeContext;
};

struct PagingQueueEntry
{
    TiledTextureBuffer TiledTexture[2];
    UINT64 MostRecentTimestamp;
    union
    {
        FLOAT PositiveWeight;
        UINT32 SortKey;
    };
    GraphicsJob* pGraphicsJob;

    void SetUnmapped() { SortKey = -1; }
    bool IsUnmapped() const { return SortKey == -1; }
};

typedef std::deque<PagingQueueEntry*> PagingQueueEntryDeque;

struct PagingQueueEntryComparator
{
public:
    bool operator() (const PagingQueueEntry* pA, const PagingQueueEntry* pB) const
    {
        return pA->SortKey > pB->SortKey;
    }
};

class GpuJobQueue
{
private:
    CRITICAL_SECTION m_JobCritSec;
    std::deque<GraphicsJob*> m_GraphicsJobs;
    std::deque<GraphicsJob*> m_CompleteGraphicsJobs;
    PagingQueueEntryDeque m_PagingMapQueue;
    PagingQueueEntryDeque m_PagingUnmapQueue;

public:
    GpuJobQueue();
    ~GpuJobQueue();

    void AddPagingMapEntry(PagingQueueEntry* pEntry);
    void AddPagingUnmapEntry(PagingQueueEntry* pEntry);
    void RemovePagingEntry(PagingQueueEntry* pEntry);

    GraphicsJob* CreateGraphicsJob(const std::wstring& ID = L"");
    void SubmitGraphicsJob(GraphicsJob* pJob);
    void CloseGraphicsJob(GraphicsJob* pJob);

    void ExecuteGraphicsJobs(UINT32 ExecutionBudgetUsec = -1);

    const PagingQueueEntryDeque& GetPagingMapDeque() const { return m_PagingMapQueue; }

private:
    void PerformPageMapping();
    GraphicsJob* AllocGraphicsJob(bool ForceCreate = false);
    void FreeGraphicsJob(GraphicsJob* pJob);
    UINT32 ExecuteSingleGraphicsJob(GraphicsJob* pJob);
};

extern GpuJobQueue g_GpuJobQueue;

