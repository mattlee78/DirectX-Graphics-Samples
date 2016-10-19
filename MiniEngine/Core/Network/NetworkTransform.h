#pragma once

#include "StateLinking.h"
#include "VectorMath.h"
#include "ClientPredict.h"

#pragma warning(disable: 4800)

class NetworkTransform : public INetworkObject
{
protected:
    StateFloat3Delta m_NetPosition;
    FLOAT m_NetScale;
    StateFloat4Delta m_NetOrientation;
    UINT32 m_NetFlags;
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
        };

        *ppMemberDatas = Members;
        *pMemberDataCount = ARRAYSIZE(Members);
    }

    virtual void SetRemote(BOOL Remote) { m_IsRemote = (bool)Remote; }

    virtual void SetNodeID(UINT ID) { m_NodeID = ID; }
    virtual UINT GetNodeID() const { return m_NodeID; }

    Math::Matrix4 GetNetworkMatrix(INT64 ClientTicks, bool UseScale = false)
    {
        Math::Matrix4 m;
        XMVECTOR PredictedPosition = m_NetPosition.Lerp(ClientTicks);
        XMVECTOR PredictedOrientation = m_NetOrientation.LerpQuaternion(ClientTicks);
        FLOAT Scale = UseScale ? m_NetScale : 1.0f;
        m.Compose(Math::Vector3(PredictedPosition), Scale, Math::Vector4(PredictedOrientation));
        return m;
    }

    void SetNetworkMatrix(const Math::Matrix4& Transform)
    {
        Math::Vector3 vPosition;
        Math::Vector4 vOrientation;
        Transform.Decompose(vPosition, m_NetScale, vOrientation);
        m_NetPosition.SetRawValue(vPosition);
        m_NetOrientation.SetRawValue(vOrientation);
    }
};
