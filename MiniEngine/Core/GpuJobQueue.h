#pragma once

#include <deque>

class CommandContext;
class ComputeContext;

struct GraphicsJob
{
    UINT32 SortKey;
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

class GpuJobQueue
{
private:
    CRITICAL_SECTION m_JobCritSec;
    std::deque<GraphicsJob*> m_GraphicsJobs;
    std::deque<GraphicsJob*> m_CompleteGraphicsJobs;

public:
    GpuJobQueue();
    ~GpuJobQueue();

    GraphicsJob* CreateGraphicsJob();
    void SubmitGraphicsJob(GraphicsJob* pJob);
    void CloseGraphicsJob(GraphicsJob* pJob);

    void ExecuteGraphicsJobs(UINT32 ExecutionBudgetUsec = -1);

private:
    GraphicsJob* AllocGraphicsJob(bool ForceCreate = false);
    void FreeGraphicsJob(GraphicsJob* pJob);
};

extern GpuJobQueue g_GpuJobQueue;

