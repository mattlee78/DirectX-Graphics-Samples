#include "pch.h"
#include "ClientPredict.h"

using namespace DirectX;

ClientPredictionConstants g_ClientPredictConstants = {};
LARGE_INTEGER g_LerpThresholdTicks = { UINT_MAX, 0 };

XMVECTOR ClientPredictVector<XMFLOAT3>::GetZeroValue() const
{
    return g_XMZero;
}

XMVECTOR ClientPredictVector<XMFLOAT3>::GetZeroTrend() const
{
    return g_XMZero;
}

XMVECTOR ClientPredictVector<XMFLOAT3>::LoadValue(const XMFLOAT3* pValue) const
{
    return XMLoadFloat3(pValue);
}

void ClientPredictVector<XMFLOAT3>::StoreValue(XMFLOAT3* pValue, XMVECTOR Value)
{
    XMStoreFloat3(pValue, Value);
}

XMVECTOR ClientPredictVector<XMFLOAT3>::LerpValue(XMVECTOR A, XMVECTOR B, float Param) const
{
    return XMVectorLerp(A, B, Param);
}

XMVECTOR ClientPredictVector<XMFLOAT3>::Norm(XMVECTOR Value) const
{
    return Value;
}

XMVECTOR ClientPredictVector<XMFLOAT4>::GetZeroValue() const
{
    return g_XMIdentityR3;
}

XMVECTOR ClientPredictVector<XMFLOAT4>::GetZeroTrend() const
{
    return g_XMZero;
}

XMVECTOR ClientPredictVector<XMFLOAT4>::LoadValue(const XMFLOAT4* pValue) const
{
    return XMLoadFloat4(pValue);
}

void ClientPredictVector<XMFLOAT4>::StoreValue(XMFLOAT4* pValue, XMVECTOR Value)
{
    XMStoreFloat4(pValue, Value);
}

XMVECTOR ClientPredictVector<XMFLOAT4>::LerpValue(XMVECTOR A, XMVECTOR B, float Param) const
{
    return XMQuaternionSlerp(A, B, Param);
}

XMVECTOR ClientPredictVector<XMFLOAT4>::Norm(XMVECTOR Value) const
{
    return XMQuaternionNormalize(Value);
}

template<typename T>
void ClientPredictVector<T>::UpdateFromNetwork(XMVECTOR RawValue, INT64 Timestamp)
{
    if (m_State == PredictionState::Zero)
    {
        UpdateStatic(RawValue);
        m_LastTimestamp = Timestamp;
        return;
    }

    const XMVECTOR PrevFilteredValue = LoadValue(&m_FilteredValue);
    const XMVECTOR PrevTrend = LoadValue(&m_Trend);
    const XMVECTOR PrevRawValue = LoadValue(&m_RawValue);

    XMVECTOR FilteredValue;
    XMVECTOR Trend;
    XMVECTOR Diff;

    switch (m_State)
    {
    case PredictionState::StaticValue:
        FilteredValue = Norm((PrevRawValue + RawValue) * g_XMOneHalf);
        Diff = FilteredValue - PrevFilteredValue;
        Trend = XMVectorLerp(PrevTrend, Diff, g_ClientPredictConstants.Correction);
        m_State = PredictionState::MovingValue;
        break;
    case PredictionState::MovingValue:
        FilteredValue = LerpValue(RawValue, Norm(PrevFilteredValue + PrevTrend), g_ClientPredictConstants.Smoothing);
        Diff = FilteredValue - PrevFilteredValue;
        Trend = XMVectorLerp(PrevTrend, Diff, g_ClientPredictConstants.Correction);
        break;
    }

    StoreValue(&m_FilteredValue, FilteredValue);
    StoreValue(&m_RawValue, RawValue);
    StoreValue(&m_Trend, Trend);
    m_LastTimestamp = Timestamp;
}

template void ClientPredictVector<XMFLOAT3>::UpdateFromNetwork(XMVECTOR NewValue, INT64 Timestamp);
template void ClientPredictVector<XMFLOAT4>::UpdateFromNetwork(XMVECTOR NewValue, INT64 Timestamp);

template<typename T>
XMVECTOR ClientPredictVector<T>::GetPredictedValue(INT64 Timestamp)
{
    if (m_State != PredictionState::MovingValue)
    {
        return LoadValue(&m_FilteredValue);
    }

    INT64 DeltaTicks = Timestamp - m_LastTimestamp;

    if (DeltaTicks >= g_ClientPredictConstants.FrameTickLength)
    {
        const INT64 SynthTimestamp = m_LastTimestamp + g_ClientPredictConstants.FrameTickLength;
        assert(Timestamp >= SynthTimestamp);
        UpdateFromNetwork(LoadValue(&m_RawValue), SynthTimestamp);
        XMVECTOR Trend = LoadValue(&m_Trend);
        Trend *= g_XMOneHalf;
        StoreValue(&m_Trend, Trend);
        DeltaTicks = Timestamp - m_LastTimestamp;
    }

    DOUBLE NormPredict = (DOUBLE)DeltaTicks / (DOUBLE)g_ClientPredictConstants.FrameTickLength;
    NormPredict += g_ClientPredictConstants.Prediction;

    const XMVECTOR CurrentPos = LoadValue(&m_FilteredValue);
    const XMVECTOR Result = Norm(CurrentPos + LoadValue(&m_Trend) * (FLOAT)NormPredict);
    return Result;
}

template XMVECTOR ClientPredictVector<XMFLOAT3>::GetPredictedValue(INT64 Timestamp);
template XMVECTOR ClientPredictVector<XMFLOAT4>::GetPredictedValue(INT64 Timestamp);
