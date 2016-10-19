#include "NetworkLayer.h"

VOID GameNetClient::InitializeClient()
{
    m_World.Initialize();
}

INetworkObject* GameNetClient::CreateRemoteObject(INetworkObject* pParentObject, UINT ID, const VOID* pCreationData, SIZE_T CreationDataSizeBytes)
{
    if (CreationDataSizeBytes >= sizeof(RMsg_SpawnObject))
    {
        const RMsg_SpawnObject* pMsg = (const RMsg_SpawnObject*)pCreationData;
        ModelInstance* pMI = m_World.SpawnModelInstance(pMsg->strTemplateName, pMsg->strObjectName, pMsg->Transform, true);
        pMI->SetRemote(TRUE);
        pMI->SetNodeID(ID);
        return pMI;
    }
    return nullptr;
}

VOID GameNetClient::DeleteRemoteObject(INetworkObject* pObject)
{

}

VOID GameNetClient::TickClient(FLOAT DeltaTime, DOUBLE AbsoluteTime, StateSnapshot* pSnapshot, SnapshotSendQueue* pSendQueue)
{

}

VOID GameNetClient::TerminateClient()
{

}

//--------------------------------------------------------------------------------------------------------

VOID GameNetServer::InitializeServer()
{
    m_NextObjectID = 1000;
    m_World.Initialize(false);
}

VOID GameNetServer::TickServer(FLOAT DeltaTime, DOUBLE AbsoluteTime)
{
    m_World.Tick(DeltaTime, 0);
}

VOID GameNetServer::TerminateServer()
{

}

INetworkObject* GameNetServer::SpawnObject(ConnectedClient* pClient, const CHAR* strTemplateName, const CHAR* strObjectName, const DecomposedTransform& InitialTransform, const XMFLOAT3& LinearImpulse)
{
    assert(strTemplateName != nullptr);

    RMsg_SpawnObject Msg = {};
    strcpy_s(Msg.strTemplateName, strTemplateName);
    if (strObjectName != nullptr)
    {
        strcpy_s(Msg.strObjectName, strObjectName);
    }
    Msg.Transform = InitialTransform;
    Msg.LinearImpulse = LinearImpulse;
    return SpawnObject(pClient, &Msg);
}

INetworkObject* GameNetServer::SpawnObject(ConnectedClient* pClient, const RMsg_SpawnObject* pMsg)
{
    INetworkObject* pNO = nullptr;

    ModelInstance* pMI = m_World.SpawnModelInstance(pMsg->strTemplateName, pMsg->strObjectName, pMsg->Transform);
    if (pMI != nullptr)
    {
        pNO = pMI;
    }

    if (pNO != nullptr)
    {
        pNO->SetRemote(FALSE);
        m_NextObjectID = m_StateIO.CreateNodeGroup(0, m_NextObjectID, pNO, pMsg, sizeof(*pMsg), TRUE);

        if (pMI != nullptr)
        {
            RigidBody* pRB = pMI->GetRigidBody();
            if (pRB != nullptr)
            {
                pRB->ApplyLinearImpulse(XMLoadFloat3(&pMsg->LinearImpulse));
            }
        }
    }

    return pNO;
}

INetworkObject* GameNetServer::CreateRemoteObject(VOID* pSenderContext, INetworkObject* pParentObject, UINT ID, const VOID* pCreationData, SIZE_T CreationDataSizeBytes)
{
    return nullptr;
}

VOID GameNetServer::DeleteRemoteObject(INetworkObject* pObject)
{

}
