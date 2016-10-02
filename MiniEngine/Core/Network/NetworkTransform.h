#pragma once

#include "StateLinking.h"
#include "VectorMath.h"

#pragma warning(disable: 4800)

class NetworkTransform : public INetworkObject
{
protected:
    DirectX::XMFLOAT3 m_NetPosition;
    FLOAT m_NetScale;
    DirectX::XMFLOAT4 m_NetOrientation;
    UINT32 m_NetFlags;
    UINT32 m_NodeID;
    bool m_IsRemote;

public:
    NetworkTransform()
        : m_NodeID(0),
          m_IsRemote(false),
          m_NetFlags(0)
    { 
        m_NetPosition = XMFLOAT3(0, 0, 0);
        m_NetScale = 1;
        m_NetOrientation = XMFLOAT4(0, 0, 0, 1);
    }

    virtual void GetMemberDatas(const MemberDataPosition** ppMemberDatas, UINT* pMemberDataCount) const
    {
        static const MemberDataPosition Members[] =
        {
            { StateNodeType::Float3,            offsetof(NetworkTransform, m_NetPosition),       sizeof(m_NetPosition) },
            { StateNodeType::Float,             offsetof(NetworkTransform, m_NetScale),          sizeof(m_NetScale) },
            { StateNodeType::Float4AsHalf4,     offsetof(NetworkTransform, m_NetOrientation),    sizeof(m_NetOrientation) },
            { StateNodeType::Integer,           offsetof(NetworkTransform, m_NetFlags),          sizeof(m_NetFlags) },
        };

        *ppMemberDatas = Members;
        *pMemberDataCount = ARRAYSIZE(Members);
    }

    virtual void SetRemote(BOOL Remote) { m_IsRemote = (bool)Remote; }

    virtual void SetNodeID(UINT ID) { m_NodeID = ID; }
    virtual UINT GetNodeID() const { return m_NodeID; }

    Math::Matrix4 GetNetworkMatrix() const
    {
        Math::Matrix4 m;
        m.Compose(Math::Vector3(XMLoadFloat3(&m_NetPosition)), m_NetScale, Math::Vector4(XMLoadFloat4(&m_NetOrientation)));
        return m;
    }

    void SetNetworkMatrix(const Math::Matrix4& Transform)
    {
        Math::Vector3 vPosition;
        Math::Vector4 vOrientation;
        Transform.Decompose(vPosition, m_NetScale, vOrientation);
        XMStoreFloat3(&m_NetPosition, vPosition);
        XMStoreFloat4(&m_NetOrientation, vOrientation);
    }
};
