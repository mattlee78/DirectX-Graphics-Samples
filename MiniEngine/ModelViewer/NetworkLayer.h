#pragma once

#include "GameCore.h"
#include "BulletPhysics.h"
#include "ModelInstance.h"
#include "Network\NetServerBase.h"
#include "Network\NetClientBase.h"

class GameNetClient;
class GameNetServer;

enum class GameReliableMessageType
{
    SpawnObject = (INT)ReliableMessageType::FirstImplReliableMessage,
    AssignClientObjectIDs,
    RequestMoreClientObjectIDs,
    DestroyObject,
};

struct RMsg_SpawnObject
{
    CHAR strTemplateName[32];
    CHAR strObjectName[32];
    UINT32 ParentObjectID;
    DecomposedTransform Transform;
    DirectX::XMFLOAT3 LinearImpulse;
};

struct RMsg_AssignClientObjectIDs
{
    UINT m_ConnectionBaseID;
    UINT m_FirstObjectID;
    UINT m_ObjectIDCount;
};

struct RMsg_DestroyObject
{
    UINT32 ObjectID;
};

class SystemNetworkObject : public INetworkObject
{
protected:
    UINT32 m_NodeID;
    BOOL m_IsRemote;
    ConnectedClient* m_pClient;

public:
    SystemNetworkObject()
        : m_NodeID(0),
          m_IsRemote(FALSE),
          m_pClient(nullptr)
    { }

    void SetNodeID(UINT ID) { m_NodeID = ID; }
    UINT GetNodeID() const { return m_NodeID; }
    void SetRemote(BOOL Remote) { m_IsRemote = Remote; }
    BOOL IsRemote() const { return m_IsRemote; }

    void SetClient(ConnectedClient* pClient) { m_pClient = pClient; }
    ConnectedClient* GetClient() const { return m_pClient; }

    virtual void ServerTick(GameNetServer* pServer, FLOAT DeltaTime, DOUBLE AbsoluteTime) {}
    virtual void NetworkObjectDeleted(INetworkObject* pNO) {}
};

class IClientNotifications
{
public:
    virtual void RemoteObjectCreated(ModelInstance* pModelInstance, UINT ParentObjectID) = 0;
    virtual void RemoteObjectDeleted(ModelInstance* pModelInstance) = 0;
};

class GameNetClient : public NetClientBase
{
private:
    World m_World;
    UINT m_NextObjectID;
    UINT m_LastObjectID;
    UINT m_BaseObjectID;
    IClientNotifications* m_pNotifications;

public:
    GameNetClient();
    void SetNotificationClient(IClientNotifications* pNotifications) { m_pNotifications = pNotifications; }
    World* GetWorld() { return &m_World; }
    UINT GetClientBaseObjectID() const { return m_BaseObjectID; }
    bool CanSpawnObjects() const { return m_BaseObjectID != 0; }
    bool SpawnObjectOnServer(const CHAR* strTemplateName, const CHAR* strInstanceName, const DecomposedTransform& InitialTransform, const XMFLOAT3& LinearImpulse);
    INetworkObject* SpawnObjectOnClient(const CHAR* strTemplateName);
    INetworkObject* SpawnObjectOnClient(const RMsg_SpawnObject* pMsg);
    void DestroyObjectOnServer(UINT32 ObjectID);

private:
    virtual VOID InitializeClient();
    virtual INetworkObject* CreateRemoteObject(INetworkObject* pParentObject, UINT ID, const VOID* pCreationData, SIZE_T CreationDataSizeBytes);
    virtual VOID DeleteRemoteObject(INetworkObject* pObject);
    virtual VOID TickClient(FLOAT DeltaTime, DOUBLE AbsoluteTime, StateSnapshot* pSnapshot, SnapshotSendQueue* pSendQueue);
    virtual VOID TerminateClient();
    virtual BOOL HandleReliableMessage(VOID* pSenderContext, const UINT Opcode, const UINT UniqueIndex, const BYTE* pPayload, const UINT PayloadSizeBytes);
};

class GameNetServer : public NetServerBase, IWorldNotifications
{
private:
    World m_World;
    UINT32 m_NextObjectID;
    std::set<SystemNetworkObject*> m_SystemObjects;
    std::unordered_map<UINT32, ModelInstance*> m_ModelInstances;

public:
    INetworkObject* SpawnObject(ConnectedClient* pClient, const CHAR* strTemplateName, const CHAR* strObjectName, const DecomposedTransform& InitialTransform, const XMFLOAT3& LinearImpulse);
    INetworkObject* SpawnObject(ConnectedClient* pClient, const RMsg_SpawnObject* pMsg);
    World* GetWorld() { return &m_World; }
    ModelInstance* FindModelInstance(UINT32 NodeID);
    virtual void ModelInstanceDeleted(ModelInstance* pMI);

private:
    virtual VOID InitializeServer();
    virtual VOID TickServer(FLOAT DeltaTime, DOUBLE AbsoluteTime);
    virtual VOID TerminateServer();
    virtual INetworkObject* CreateRemoteObject(VOID* pSenderContext, INetworkObject* pParentObject, UINT ID, const VOID* pCreationData, SIZE_T CreationDataSizeBytes);
    virtual VOID DeleteRemoteObject(INetworkObject* pObject);
    virtual VOID ClientConnected(ConnectedClient* pClient);
    virtual VOID ClientDisconnected(ConnectedClient* pClient);
    virtual BOOL HandleReliableMessage(VOID* pSenderContext, const UINT Opcode, const UINT UniqueIndex, const BYTE* pPayload, const UINT PayloadSizeBytes);
};

SystemNetworkObject* CreateSystemNetworkObject(const CHAR* strTemplateName);

class ConnectionBaseObject : public SystemNetworkObject
{
public:
    static const CHAR* GetTemplateName() { return "$ConnectionBase"; }

    VOID GetMemberDatas(const MemberDataPosition** ppMemberDatas, UINT* pMemberDataCount) const
    {
        *ppMemberDatas = nullptr;
        *pMemberDataCount = 0;
    }
};

class InputRemotingObject : public SystemNetworkObject
{
private:
    FLOAT m_XAxis0;
    FLOAT m_YAxis0;
    FLOAT m_XAxis1;
    FLOAT m_YAxis1;
    FLOAT m_LeftTrigger;
    FLOAT m_RightTrigger;
    NetworkSequence m_Buttons[8];
    NetworkSequence m_TargetNodeID;
    ModelInstance* m_pTargetModelInstance;

public:
    InputRemotingObject()
        : m_pTargetModelInstance(nullptr)
    {
        ClientZero();
    }

    static const CHAR* GetTemplateName() { return "$InputRemoting"; }

    VOID GetMemberDatas(const MemberDataPosition** ppMemberDatas, UINT* pMemberDataCount) const;

    void ClientZero()
    {
        NetworkInputState ZeroState = {};
        ClientUpdate(ZeroState);
    }
    void ClientUpdate(const NetworkInputState& InputState);
    void ClientSetTargetNodeID(UINT32 NodeID) { m_TargetNodeID.SourceSetValue(NodeID); }
    UINT32 ClientGetTargetNodeID() const { return m_TargetNodeID.GetCurrentValue(); }

    void ServerTick(GameNetServer* pServer, FLOAT DeltaTime, DOUBLE AbsoluteTime);
    void NetworkObjectDeleted(INetworkObject* pNO);
};
