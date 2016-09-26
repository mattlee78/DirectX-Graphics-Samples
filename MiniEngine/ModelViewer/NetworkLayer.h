#pragma once

#include "GameCore.h"
#include "BulletPhysics.h"
#include "ModelInstance.h"
#include "Network\NetServerBase.h"
#include "Network\NetClientBase.h"

struct RMsg_SpawnObject
{
    CHAR strTemplateName[32];
    CHAR strObjectName[32];
    DecomposedTransform Transform;
    DirectX::XMFLOAT3 LinearImpulse;
};

class GameNetClient : public NetClientBase
{
private:
    World m_World;
public:
    World* GetWorld() { return &m_World; }

private:
    virtual VOID InitializeClient();
    virtual INetworkObject* CreateRemoteObject(INetworkObject* pParentObject, UINT ID, const VOID* pCreationData, SIZE_T CreationDataSizeBytes);
    virtual VOID DeleteRemoteObject(INetworkObject* pObject);
    virtual VOID TickClient(FLOAT DeltaTime, DOUBLE AbsoluteTime, StateSnapshot* pSnapshot, SnapshotSendQueue* pSendQueue);
    virtual VOID TerminateClient();
};

class GameNetServer : public NetServerBase
{
private:
    World m_World;
    UINT32 m_NextObjectID;

public:
    INetworkObject* SpawnObject(ConnectedClient* pClient, const CHAR* strTemplateName, const CHAR* strObjectName, const DecomposedTransform& InitialTransform, const XMFLOAT3& LinearImpulse);
    INetworkObject* SpawnObject(ConnectedClient* pClient, const RMsg_SpawnObject* pMsg);
    World* GetWorld() { return &m_World; }

private:
    virtual VOID InitializeServer();
    virtual VOID TickServer(FLOAT DeltaTime, DOUBLE AbsoluteTime);
    virtual VOID TerminateServer();
    virtual INetworkObject* CreateRemoteObject(VOID* pSenderContext, INetworkObject* pParentObject, UINT ID, const VOID* pCreationData, SIZE_T CreationDataSizeBytes);
    virtual VOID DeleteRemoteObject(INetworkObject* pObject);
};
