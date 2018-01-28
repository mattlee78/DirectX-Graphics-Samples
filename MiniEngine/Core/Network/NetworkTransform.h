#pragma once

#include "StateLinking.h"
#include "VectorMath.h"
#include "ClientPredict.h"

#pragma warning(disable: 4800)

class NetworkSequence
{
private:
    UINT32 m_CurrentValue;
    UINT32 m_LastCheckedValue;

public:
    NetworkSequence()
        : m_CurrentValue(0),
        m_LastCheckedValue(0)
    { }

    UINT32* GetRemotedValue() { return &m_CurrentValue; }
    UINT32 GetCurrentValue() const { return m_CurrentValue; }

    void SourceUpdate()
    {
        ++m_CurrentValue;
    }
    void SourceSetValue(UINT32 Value)
    {
        m_CurrentValue = Value;
    }

    bool DestinationCheckAndUpdate(UINT32* pValue = nullptr)
    {
        const bool Changed = (m_CurrentValue != m_LastCheckedValue);
        m_LastCheckedValue = m_CurrentValue;
        if (pValue != nullptr)
        {
            *pValue = m_CurrentValue;
        }
        return Changed;
    }
};

class NetworkTransform : public INetworkObject
{
protected:
    StateFloat3Delta m_NetPosition;
    FLOAT m_NetScale;
    StateFloat4Delta m_NetOrientation;
    UINT32 m_NetFlags;
    NetworkSequence m_ContinuityEpoch;
    UINT32 m_NodeID;
    bool m_IsRemote;

public:
    NetworkTransform()
        : m_NodeID(0),
          m_IsRemote(false),
          m_NetFlags(0)
    { 
        m_NetScale = 1;
    }

    virtual void GetMemberDatas(const MemberDataPosition** ppMemberDatas, UINT* pMemberDataCount) const
    {
        static const MemberDataPosition Members[] =
        {
            { StateNodeType::Float3Delta,       offsetof(NetworkTransform, m_NetPosition),       sizeof(m_NetPosition) },
            { StateNodeType::Float,             offsetof(NetworkTransform, m_NetScale),          sizeof(m_NetScale) },
            { StateNodeType::Float4AsHalf4Delta,offsetof(NetworkTransform, m_NetOrientation),    sizeof(m_NetOrientation) },
            { StateNodeType::Integer,           offsetof(NetworkTransform, m_NetFlags),          sizeof(m_NetFlags) },
            { StateNodeType::Integer,           offsetof(NetworkTransform, m_ContinuityEpoch),   sizeof(UINT32) },
        };

        *ppMemberDatas = Members;
        *pMemberDataCount = ARRAYSIZE(Members);
    }

    virtual void SetRemote(BOOL Remote) 
    { 
        m_IsRemote = (bool)Remote; 
        if (!m_IsRemote)
        {
            m_ContinuityEpoch.SourceUpdate();
        }
    }

    virtual void SetNodeID(UINT ID) { m_NodeID = ID; }
    virtual UINT GetNodeID() const { return m_NodeID; }

    Math::Matrix4 GetNetworkMatrix(INT64 ClientTicks, bool UseScale = false)
    {
        const bool Discontinuous = m_ContinuityEpoch.DestinationCheckAndUpdate();
        if (Discontinuous)
        {
            m_NetPosition.ResetPrediction();
            m_NetOrientation.ResetPrediction();
        }

        Math::Matrix4 m;
        XMVECTOR PredictedPosition = m_NetPosition.Lerp(ClientTicks);
        XMVECTOR PredictedOrientation = m_NetOrientation.LerpQuaternion(ClientTicks);
        FLOAT Scale = UseScale ? m_NetScale : 1.0f;
        m.Compose(Math::Vector3(PredictedPosition), Scale, Math::Vector4(PredictedOrientation));
        return m;
    }

    void SetNetworkMatrix(const Math::Matrix4& Transform, bool Discontinuous)
    {
        Math::Vector3 vPosition;
        Math::Vector4 vOrientation;
        Transform.Decompose(vPosition, m_NetScale, vOrientation);
        m_NetPosition.SetRawValue(vPosition);
        m_NetOrientation.SetRawValue(vOrientation);
        if (Discontinuous)
        {
            m_ContinuityEpoch.SourceUpdate();
        }
    }

    Math::Vector3 GetRawPosition() const { return Math::Vector3(m_NetPosition.GetRawValue()); }
    INT64 GetRawPositionTimestamp() const { return m_NetPosition.GetSampleTime(); }
    FLOAT GetRawPositionLerpValue() const { return m_NetPosition.GetLerpValue(); }
};
