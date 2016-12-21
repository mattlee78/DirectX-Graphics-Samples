#include "pch.h"
#include "GpuJobQueue.h"
#include "CommandContext.h"
#include "CommandListManager.h"

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

GraphicsJob* GpuJobQueue::CreateGraphicsJob()
{
    GraphicsJob* pJob = AllocGraphicsJob(false);
    assert(pJob != nullptr);

    ZeroMemory(pJob, sizeof(*pJob));
    pJob->CancelJob = false;
    pJob->HoldJobOpen = false;
    pJob->pContext = &GraphicsContext::Begin();

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
    if (m_GraphicsJobs.empty())
    {
        return;
    }

    struct  
    {
        bool operator() (const GraphicsJob* pA, const GraphicsJob* pB) const
        {
            return pA->SortKey > pB->SortKey;
        }
    } GpuJobComparator;

    EnterCriticalSection(&m_JobCritSec);
    std::sort(m_GraphicsJobs.begin(), m_GraphicsJobs.end(), GpuJobComparator);
    LeaveCriticalSection(&m_JobCritSec);

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
            if (!pJob->CancelJob)
            {
                assert(pJob->CompletionFenceValue == 0);
                ExecutionBudgetUsec -= pJob->EstimatedGpuUsec;
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
        }
    }
}

