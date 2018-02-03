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
    FLOAT GetLerpValue() const { return PrevLerpValue; }

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

    void ResetPrediction()
    {
        PrevValue = CurrentValue;
        PreviousRecvTimestamp = CurrentRecvTimestamp;
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
        //assert(LerpValue > 0.0f);
        /*
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
        */
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
        /*
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
        */
        PrevLerpValue = LerpValue;
        const XMVECTOR Current = StateLoad(&CurrentValue);
        XMVECTOR Result = XMQuaternionSlerp(Prev, Current, LerpValue);
        return Result;
    }
};

// typedef StateDelta<XMFLOAT3> StateFloat3Delta;
typedef StateDelta<XMFLOAT4> StateFloat4Delta;

struct ExpFilteredVector3
{
private:
    XMFLOAT3 m_LastReceivedValue;
    XMFLOAT3 m_CurrentExtrapolatedValue;
    XMFLOAT3 m_CurrentTrend;
    INT64 m_LastReceivedTicks;
    INT64 m_LastExtrapolatedTicks;
    FLOAT m_PrevLerpValue;

private:
    static inline void StateStore(XMFLOAT3* pValue, CXMVECTOR v) { XMStoreFloat3(pValue, v); }
    static inline XMVECTOR StateLoad(const XMFLOAT3* pValue) { return XMLoadFloat3(pValue); }

public:
    ExpFilteredVector3()
    {
        XMFLOAT3 ZeroValue;
        StateStore(&ZeroValue, g_XMIdentityR3);
        Reset(ZeroValue);
    }

    const XMFLOAT3* GetRawData() const { return &m_LastReceivedValue; }
    XMVECTOR GetRawValue() const { return StateLoad(&m_LastReceivedValue); }
    void SetRawValue(CXMVECTOR Value) { StateStore(&m_LastReceivedValue, Value); }
    INT64 GetSampleTime() const { return m_LastReceivedTicks; }
    FLOAT GetLerpValue() const { return m_PrevLerpValue; }

    void Reset(const XMFLOAT3& Value)
    {
        m_LastReceivedValue = Value;
        m_CurrentExtrapolatedValue = Value;
        StateStore(&m_CurrentTrend, XMVectorZero());
        m_LastExtrapolatedTicks = 0;
        m_LastReceivedTicks = 0;
        m_PrevLerpValue = 0;
    }

    void ReceiveNewValue(const XMVECTOR Value, LARGE_INTEGER CurrentTimestamp)
    {
        if (m_LastExtrapolatedTicks != 0 && CurrentTimestamp.QuadPart > m_LastExtrapolatedTicks)
        {
            Lerp(CurrentTimestamp.QuadPart);
        }
        XMVECTOR LastExtrapolatedValue = StateLoad(&m_CurrentExtrapolatedValue);
        XMVECTOR LastTrend = StateLoad(&m_CurrentTrend);
        const XMVECTOR Error = Value - LastExtrapolatedValue;
        XMVECTOR NewTrend = Value - StateLoad(&m_LastReceivedValue);
        StateStore(&m_LastReceivedValue, Value);
        m_LastReceivedTicks = CurrentTimestamp.QuadPart;
        if (m_LastExtrapolatedTicks == 0)
        {
            m_LastExtrapolatedTicks = CurrentTimestamp.QuadPart;
        }
        static const FLOAT TrendSmoothingFactor = 0.9f;
        XMVECTOR SmoothedTrend = XMVectorLerp(LastTrend, NewTrend, TrendSmoothingFactor) + Error;
        StateStore(&m_CurrentTrend, SmoothedTrend);
    }

    void ResetPrediction()
    {
        INT64 Ticks = m_LastExtrapolatedTicks;
        Reset(m_CurrentExtrapolatedValue);
        m_LastReceivedTicks = Ticks;
        m_LastExtrapolatedTicks = Ticks;
    }

    inline XMVECTOR Lerp(INT64 CurrentTime)
    {
        XMVECTOR ExtrapolatedValue = StateLoad(&m_CurrentExtrapolatedValue);
        if (m_LastExtrapolatedTicks > 0)
        {
            FLOAT LerpValue = (FLOAT)((DOUBLE)(CurrentTime - m_LastExtrapolatedTicks) / (DOUBLE)(g_ClientPredictConstants.FrameTickLength));
            ExtrapolatedValue += StateLoad(&m_CurrentTrend) * LerpValue;
            StateStore(&m_CurrentExtrapolatedValue, ExtrapolatedValue);
            m_LastExtrapolatedTicks = CurrentTime;
            m_PrevLerpValue = LerpValue;
        }
        return ExtrapolatedValue;
    }
};

struct ExpFilteredQuaternion
{
private:
    XMFLOAT4 m_LastReceivedValue;
    XMFLOAT4 m_CurrentExtrapolatedValue;
    XMFLOAT4 m_CurrentTrend;
    XMFLOAT4 m_CurrentTrendAxisAngle;
    XMFLOAT4 m_CurrentErrorAxisAngle;
    INT64 m_LastReceivedTicks;
    INT64 m_LastExtrapolatedTicks;
    FLOAT m_PrevLerpValue;

private:
    static inline void StateStore(XMFLOAT4* pValue, CXMVECTOR v) { XMStoreFloat4(pValue, v); }
    static inline XMVECTOR StateLoad(const XMFLOAT4* pValue) { return XMLoadFloat4(pValue); }

    static inline XMVECTOR RotationBetweenQuaternions(XMVECTOR A, XMVECTOR B)
    {
        XMVECTOR R = XMQuaternionMultiply(B, XMQuaternionInverse(A));
        XMVECTOR T = XMQuaternionMultiply(A, R);
        return R;
    }

    static inline XMVECTOR QuaternionToAxisAngle(XMVECTOR Q)
    {
        XMVECTOR Axis;
        FLOAT Angle;
        XMQuaternionToAxisAngle(&Axis, &Angle, Q);
        Axis = XMVectorSetW(Axis, Angle);
        return Axis;
    }

    static inline XMVECTOR QuaternionRotationAxis(XMVECTOR AA)
    {
        FLOAT Angle = XMVectorGetW(AA);
        if (fabsf(Angle) >= 0.01f)
        {
            return XMQuaternionRotationAxis(AA, Angle);
        }
        return XMQuaternionIdentity();
    }

public:
    ExpFilteredQuaternion()
    {
        Reset(XMFLOAT4(0, 0, 0, 1));
    }

    const XMFLOAT4* GetRawData() const { return &m_LastReceivedValue; }
    XMVECTOR GetRawValue() const { return StateLoad(&m_LastReceivedValue); }
    void SetRawValue(CXMVECTOR Value) { StateStore(&m_LastReceivedValue, Value); }
    INT64 GetSampleTime() const { return m_LastReceivedTicks; }
    FLOAT GetLerpValue() const { return m_PrevLerpValue; }

    void Reset(const XMFLOAT4& Value)
    {
        m_LastReceivedValue = Value;
        m_CurrentExtrapolatedValue = Value;
        StateStore(&m_CurrentTrend, g_XMIdentityR3);
        StateStore(&m_CurrentTrendAxisAngle, g_XMIdentityR1);
        StateStore(&m_CurrentErrorAxisAngle, g_XMIdentityR1);
        m_LastExtrapolatedTicks = 0;
        m_LastReceivedTicks = 0;
        m_PrevLerpValue = 0;
    }

    void ReceiveNewValue(const XMVECTOR Value, LARGE_INTEGER CurrentTimestamp)
    {
        XMVECTOR LastExtrapolatedValue = StateLoad(&m_CurrentExtrapolatedValue);
        XMVECTOR LastTrend = StateLoad(&m_CurrentTrend);
        const XMVECTOR Error = RotationBetweenQuaternions(LastExtrapolatedValue, Value);
        const XMVECTOR NewTrend = RotationBetweenQuaternions(StateLoad(&m_LastReceivedValue), Value);
        StateStore(&m_LastReceivedValue, Value);
        m_LastReceivedTicks = CurrentTimestamp.QuadPart;
        if (m_LastExtrapolatedTicks == 0)
        {
            Reset(m_LastReceivedValue);
            m_LastReceivedTicks = CurrentTimestamp.QuadPart;
            m_LastExtrapolatedTicks = CurrentTimestamp.QuadPart;
        }
        else
        {
            static const FLOAT TrendSmoothingFactor = 0.9f;
            XMVECTOR SmoothedTrend = XMQuaternionSlerp(LastTrend, NewTrend, TrendSmoothingFactor);
            StateStore(&m_CurrentTrend, SmoothedTrend);
            StateStore(&m_CurrentTrendAxisAngle, QuaternionToAxisAngle(SmoothedTrend));
            StateStore(&m_CurrentErrorAxisAngle, QuaternionToAxisAngle(Error));
            m_PrevLerpValue = 0;
        }
    }

    void ResetPrediction()
    {
        INT64 Ticks = m_LastExtrapolatedTicks;
        Reset(m_CurrentExtrapolatedValue);
        m_LastReceivedTicks = Ticks;
        m_LastExtrapolatedTicks = Ticks;
    }

    inline XMVECTOR LerpQuaternion(INT64 CurrentTime)
    {
        //return StateLoad(&m_LastReceivedValue);

        XMVECTOR ExtrapolatedValue = StateLoad(&m_CurrentExtrapolatedValue);
        if (m_LastExtrapolatedTicks > 0)
        {
            FLOAT LerpValue = (FLOAT)((DOUBLE)(CurrentTime - m_LastExtrapolatedTicks) / (DOUBLE)(g_ClientPredictConstants.FrameTickLength));
            const XMVECTOR AALerp = XMVectorSet(1, 1, 1, LerpValue);
            const XMVECTOR ErrorAA = StateLoad(&m_CurrentErrorAxisAngle) * AALerp;
            const XMVECTOR TrendAA = StateLoad(&m_CurrentTrendAxisAngle) * AALerp;
            const XMVECTOR ErrorQ = QuaternionRotationAxis(ErrorAA);
            const XMVECTOR TrendQ = QuaternionRotationAxis(TrendAA);
            ExtrapolatedValue = XMQuaternionMultiply(ExtrapolatedValue, ErrorQ);
            ExtrapolatedValue = XMQuaternionMultiply(ExtrapolatedValue, TrendQ);
            StateStore(&m_CurrentExtrapolatedValue, ExtrapolatedValue);
            m_LastExtrapolatedTicks = CurrentTime;
            m_PrevLerpValue = LerpValue;
        }
        return ExtrapolatedValue;
    }
};

typedef ExpFilteredVector3 StateFloat3Delta;
//typedef ExpFilteredQuaternion StateFloat4Delta;
