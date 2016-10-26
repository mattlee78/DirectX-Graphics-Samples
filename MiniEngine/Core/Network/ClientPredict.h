#pragma once

struct ClientPredictionConstants
{
    INT64 FrameTickLength;
    FLOAT Correction;
    FLOAT Smoothing;
    FLOAT Prediction;
};

extern ClientPredictionConstants g_ClientPredictConstants;

template<typename T>
class ClientPredictVector
{
private:
    enum class PredictionState
    {
        Zero = 0,
        StaticValue = 1,
        MovingValue = 2,
    };

    T m_FilteredValue;
    T m_Trend;
    T m_RawValue;
    PredictionState m_State;
    INT64 m_LastTimestamp;

public:
    ClientPredictVector()
    {
        m_State = PredictionState::Zero;
        StoreValue(&m_RawValue, GetZeroValue());
        StoreValue(&m_FilteredValue, GetZeroValue());
        StoreValue(&m_Trend, GetZeroTrend());
        m_LastTimestamp = 0;
    }

    void UpdateStatic(XMVECTOR NewValue)
    {
        StoreValue(&m_FilteredValue, NewValue);
        StoreValue(&m_RawValue, NewValue);
        StoreValue(&m_Trend, GetZeroTrend());
        m_State = PredictionState::StaticValue;
    }
    XMVECTOR GetStaticValue() const
    {
        assert(m_State != PredictionState::MovingValue);
        return LoadValue(&m_FilteredValue);
    }

    void UpdateFromNetwork(XMVECTOR RawValue, INT64 Timestamp);

    XMVECTOR GetPredictedValue(INT64 Timestamp);

private:
    XMVECTOR GetZeroValue() const;
    XMVECTOR GetZeroTrend() const;
    XMVECTOR LoadValue(const T* pValue) const;
    void StoreValue(T* pValue, XMVECTOR Value);
    XMVECTOR LerpValue(XMVECTOR A, XMVECTOR B, float Param) const;
    XMVECTOR Norm(XMVECTOR Value) const;
};

typedef ClientPredictVector<XMFLOAT3> PredictionFloat3;
typedef ClientPredictVector<XMFLOAT4> PredictionQuaternion;

template <typename T>
struct StateDelta
{
private:
    T CurrentValue;
    T PrevValue;
    LARGE_INTEGER CurrentRecvTimestamp;
    LARGE_INTEGER PreviousRecvTimestamp;
    FLOAT PrevLerpValue;

private:
    static inline void StateStore(XMFLOAT3* pValue, CXMVECTOR v) { XMStoreFloat3(pValue, v); }
    static inline void StateStore(XMFLOAT4* pValue, CXMVECTOR v) { XMStoreFloat4(pValue, v); }
    static inline XMVECTOR StateLoad(const XMFLOAT3* pValue) { return XMLoadFloat3(pValue); }
    static inline XMVECTOR StateLoad(const XMFLOAT4* pValue) { return XMLoadFloat4(pValue); }

public:
    StateDelta()
    {
        StateStore(&CurrentValue, XMVectorZero());
        StateStore(&PrevValue, XMVectorZero());
        CurrentRecvTimestamp.QuadPart = 0;
        PreviousRecvTimestamp.QuadPart = 0;
        PrevLerpValue = 0;
    }

    const T* GetRawData() const { return &CurrentValue; }
    XMVECTOR GetRawValue() const { return StateLoad(&CurrentValue); }
    void SetRawValue(CXMVECTOR Value) { StateStore(&CurrentValue, Value); }
    INT64 GetSampleTime() const { return CurrentRecvTimestamp.QuadPart; }
    XMVECTOR GetCurrentValue() { return Lerp(GetSampleTime()); }
    XMVECTOR GetCurrentValueQuaternion() { return LerpQuaternion(GetSampleTime()); }

    VOID Reset(const T& Value)
    {
        CurrentValue = Value;
        PrevValue = Value;
        CurrentRecvTimestamp.QuadPart = 0;
        PreviousRecvTimestamp.QuadPart = 0;
        PrevLerpValue = 0;
    }

    VOID ReceiveNewValue(const XMVECTOR Value, LARGE_INTEGER CurrentTimestamp)
    {
        PrevValue = CurrentValue;
        PreviousRecvTimestamp = CurrentRecvTimestamp;
        StateStore(&CurrentValue, Value);
        CurrentRecvTimestamp = CurrentTimestamp;
        PrevLerpValue = 0;
    }

    inline XMVECTOR Lerp(INT64 CurrentTime)
    {
        const XMVECTOR Prev = StateLoad(&PrevValue);
        if (CurrentRecvTimestamp.QuadPart <= PreviousRecvTimestamp.QuadPart)
        {
            return Prev;
        }
        //FLOAT LerpValue = (FLOAT)((DOUBLE)(CurrentTime - CurrentRecvTimestamp.QuadPart) / (DOUBLE)(CurrentRecvTimestamp.QuadPart - PreviousRecvTimestamp.QuadPart));
        FLOAT LerpValue = (FLOAT)((DOUBLE)(CurrentTime - CurrentRecvTimestamp.QuadPart) / (DOUBLE)(g_ClientPredictConstants.FrameTickLength));
        if (LerpValue > 1.0f)
        {
            FLOAT FracLerp = (LerpValue - 1.0f) * 0.5f;
            LerpValue = 1.0f + FracLerp;
        }
        if ((CurrentTime - CurrentRecvTimestamp.QuadPart) > g_LerpThresholdTicks.QuadPart)
        {
            LARGE_INTEGER CT;
            CT.QuadPart = CurrentTime;
            ReceiveNewValue(GetCurrentValue(), CT);
            CurrentRecvTimestamp.QuadPart = 0;
        }
        PrevLerpValue = LerpValue;
        const XMVECTOR Current = StateLoad(&CurrentValue);
        XMVECTOR Result = XMVectorLerp(Prev, Current, LerpValue);
        return Result;
    }

    inline XMVECTOR LerpQuaternion(INT64 CurrentTime)
    {
        const XMVECTOR Prev = StateLoad(&PrevValue);
        if (CurrentRecvTimestamp.QuadPart <= PreviousRecvTimestamp.QuadPart)
        {
            return Prev;
        }
        //FLOAT LerpValue = (FLOAT)((DOUBLE)(CurrentTime - CurrentRecvTimestamp.QuadPart) / (DOUBLE)(CurrentRecvTimestamp.QuadPart - PreviousRecvTimestamp.QuadPart));
        FLOAT LerpValue = (FLOAT)((DOUBLE)(CurrentTime - CurrentRecvTimestamp.QuadPart) / (DOUBLE)(g_ClientPredictConstants.FrameTickLength));
        if (LerpValue > 1.0f)
        {
            FLOAT FracLerp = (LerpValue - 1.0f) * 0.5f;
            LerpValue = 1.0f + FracLerp;
        }
        if ((CurrentTime - CurrentRecvTimestamp.QuadPart) > g_LerpThresholdTicks.QuadPart)
        {
            LARGE_INTEGER CT;
            CT.QuadPart = CurrentTime;
            ReceiveNewValue(GetCurrentValue(), CT);
            CurrentRecvTimestamp.QuadPart = 0;
        }
        PrevLerpValue = LerpValue;
        const XMVECTOR Current = StateLoad(&CurrentValue);
        XMVECTOR Result = XMQuaternionSlerp(Prev, Current, LerpValue);
        return Result;
    }
};

typedef StateDelta<XMFLOAT3> StateFloat3Delta;
typedef StateDelta<XMFLOAT4> StateFloat4Delta;
