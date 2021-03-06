#include "NetworkLayer.h"

GameNetClient::GameNetClient()
{
    m_pNotifications = nullptr;
    m_NextObjectID = 0;
    m_LastObjectID = 0;
    m_BaseObjectID = 0;
}

void GameNetClient::InitializeWorld()
{
    m_World.Initialize(true, nullptr);
}

VOID GameNetClient::InitializeClient()
{
}

INetworkObject* GameNetClient::CreateRemoteObject(INetworkObject* pParentObject, UINT ID, const VOID* pCreationData, SIZE_T CreationDataSizeBytes)
{
    INetworkObject* pNO = nullptr;
    ModelInstance* pMI = nullptr;
    if (CreationDataSizeBytes >= sizeof(RMsg_SpawnObject))
    {
        const RMsg_SpawnObject* pMsg = (const RMsg_SpawnObject*)pCreationData;
        if (pMsg->strTemplateName[0] == '$')
        {
            pNO = CreateSystemNetworkObject(pMsg->strTemplateName);
        }
        else
        {
            pMI = m_World.SpawnModelInstance(pMsg->strTemplateName, pMsg->strObjectName, pMsg->Transform, true);
            pNO = pMI;
        }
    }
    if (pNO != nullptr)
    {
        pNO->SetRemote(TRUE);
        pNO->SetNodeID(ID);

        if (m_pNotifications != nullptr)
        {
            UINT ParentObjectID = 0;
            if (pParentObject != nullptr)
            {
                ParentObjectID = pParentObject->GetNodeID();
            }
            m_pNotifications->RemoteObjectCreated(pMI, ParentObjectID);
        }
    }
    return pNO;
}

VOID GameNetClient::DeleteRemoteObject(INetworkObject* pObject)
{
    ModelInstance* pMI = (ModelInstance*)pObject;
    pMI->MarkForDeletion();
    if (m_pNotifications != nullptr)
    {
        m_pNotifications->RemoteObjectDeleted(pMI);
    }
}

VOID GameNetClient::TickClient(FLOAT DeltaTime, DOUBLE AbsoluteTime, StateSnapshot* pSnapshot, SnapshotSendQueue* pSendQueue)
{

}

VOID GameNetClient::TerminateClient()
{

}

BOOL GameNetClient::HandleReliableMessage(VOID* pSenderContext, const UINT Opcode, const UINT UniqueIndex, const BYTE* pPayload, const UINT PayloadSizeBytes)
{
    switch (Opcode)
    {
    case (UINT)GameReliableMessageType::AssignClientObjectIDs:
    {
        assert(PayloadSizeBytes >= sizeof(RMsg_AssignClientObjectIDs));
        auto* pMsg = (const RMsg_AssignClientObjectIDs*)pPayload;
        m_NextObjectID = pMsg->m_FirstObjectID;
        m_LastObjectID = m_NextObjectID + pMsg->m_ObjectIDCount;
        m_BaseObjectID = pMsg->m_ConnectionBaseID;
        return TRUE;
    }
    }

    return NetClientBase::HandleReliableMessage(pSenderContext, Opcode, UniqueIndex, pPayload, PayloadSizeBytes);
}

bool GameNetClient::SpawnObjectOnServer(const CHAR* strTemplateName, const CHAR* strInstanceName, const DecomposedTransform& InitialTransform, const XMFLOAT3& LinearImpulse)
{
    if (!CanSpawnObjects())
    {
        return false;
    }

    RMsg_SpawnObject Msg = {};
    strcpy_s(Msg.strTemplateName, strTemplateName);
    if (strInstanceName != nullptr)
    {
        strcpy_s(Msg.strObjectName, strInstanceName);
    }
    Msg.Transform = InitialTransform;
    Msg.LinearImpulse = LinearImpulse;
    Msg.ParentObjectID = m_BaseObjectID;

    m_SendQueue.QueueReliableMessage((UINT)GameReliableMessageType::SpawnObject, &Msg);

    return true;
}

void GameNetClient::DestroyObjectOnServer(UINT32 ObjectID)
{
    RMsg_DestroyObject Msg = {};
    Msg.ObjectID = ObjectID;

    m_SendQueue.QueueReliableMessage((UINT)GameReliableMessageType::DestroyObject, &Msg);
}

INetworkObject* GameNetClient::SpawnObjectOnClient(const CHAR* strTemplateName)
{
    RMsg_SpawnObject Msg = {};
    strcpy_s(Msg.strTemplateName, strTemplateName);
    Msg.Transform = DecomposedTransform();
    return SpawnObjectOnClient(&Msg);
}

INetworkObject* GameNetClient::SpawnObjectOnClient(const RMsg_SpawnObject* pMsg)
{
    if (!CanSpawnObjects())
    {
        return nullptr;
    }

    INetworkObject* pNO = nullptr;
    ModelInstance* pMI = nullptr;

    if (pMsg->strTemplateName[0] == '$')
    {
        pNO = CreateSystemNetworkObject(pMsg->strTemplateName);
    }
    else
    {
        // TODO: create client controlled model instances
        // pMI = m_World.SpawnModelInstance(pMsg->strTemplateName, pMsg->strObjectName, pMsg->Transform);
        // if (pMI != nullptr)
        // {
        //     pNO = pMI;
        // }
    }

    if (pNO != nullptr)
    {
        pNO->SetRemote(FALSE);
        m_NextObjectID = m_StateIO.CreateNodeGroup(0, m_NextObjectID, pNO, pMsg, sizeof(*pMsg), TRUE);
        assert(m_NextObjectID <= m_LastObjectID);

        // if (pMI != nullptr)
        // {
        //     RigidBody* pRB = pMI->GetRigidBody();
        //     if (pRB != nullptr)
        //     {
        //         pRB->ApplyLinearImpulse(XMLoadFloat3(&pMsg->LinearImpulse));
        //     }
        // }

        m_NextObjectID = pNO->CreateAdditionalBindings(&m_StateIO, pNO->GetNodeID(), m_NextObjectID);
        assert(m_NextObjectID <= m_LastObjectID);
    }

    return pNO;
}

void GameNetClient::Terminate()
{
    m_World.Terminate();
}

//--------------------------------------------------------------------------------------------------------

VOID GameNetServer::InitializeServer()
{
    m_NextObjectID = 1000;
    m_World.Initialize(false, this);

	//DecomposedTransform DT = DecomposedTransform::CreateFromComponents(XMFLOAT3(0, 300, 0));
	//m_pTestInstance = (ModelInstance*)SpawnObject(nullptr, "*staticbox1:1:1", nullptr, DT, XMFLOAT3(0, 0, 0));
    m_pTestInstance = nullptr;
}

VOID GameNetServer::TickServer(FLOAT DeltaTime, DOUBLE AbsoluteTime)
{
    if (m_pTestInstance != nullptr)
    {
        const FLOAT Radius = 100.0f;
        FLOAT XPos = Radius * (FLOAT)sin(AbsoluteTime * 0.31f);
        FLOAT ZPos = Radius * (FLOAT)cos(AbsoluteTime * 0.29f);
        XMMATRIX matTransform = XMMatrixTranslation(XPos, 300, ZPos);
        m_pTestInstance->SetWorldTransform(Math::Matrix4(matTransform));
    }

	auto iter = m_SystemObjects.begin();
    auto end = m_SystemObjects.end();
    while (iter != end)
    {
        SystemNetworkObject* pSNO = *iter++;
        pSNO->ServerTick(this, DeltaTime, AbsoluteTime);
    }
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
    ModelInstance* pMI = nullptr;
    SystemNetworkObject* pSNO = nullptr;

    UINT32 ParentObjectID = pMsg->ParentObjectID;

    if (pMsg->strTemplateName[0] == '$')
    {
        pSNO = CreateSystemNetworkObject(pMsg->strTemplateName);
        if (pSNO != nullptr)
        {
            m_SystemObjects.insert(pSNO);
        }
        pNO = pSNO;
    }
    else
    {
        pMI = m_World.SpawnModelInstance(pMsg->strTemplateName, pMsg->strObjectName, pMsg->Transform);
        pNO = pMI;
    }

    if (pNO != nullptr)
    {
        pNO->SetRemote(FALSE);
        m_NextObjectID = m_StateIO.CreateNodeGroup(ParentObjectID, m_NextObjectID, pNO, pMsg, sizeof(*pMsg), TRUE);

        if (pMI != nullptr)
        {
            m_ModelInstances[pMI->GetNodeID()] = pMI;
            RigidBody* pRB = pMI->GetRigidBody();
            if (pRB != nullptr)
            {
                pRB->ApplyLinearImpulse(XMLoadFloat3(&pMsg->LinearImpulse));
            }
        }

        m_NextObjectID = pNO->CreateAdditionalBindings(&m_StateIO, pNO->GetNodeID(), m_NextObjectID);
    }

    return pNO;
}

INetworkObject* GameNetServer::CreateRemoteObject(VOID* pSenderContext, INetworkObject* pParentObject, UINT ID, const VOID* pCreationData, SIZE_T CreationDataSizeBytes)
{
    INetworkObject* pNO = nullptr;
    SystemNetworkObject* pSNO = nullptr;
    ModelInstance* pMI = nullptr;

    if (CreationDataSizeBytes >= sizeof(RMsg_SpawnObject))
    {
        const RMsg_SpawnObject* pMsg = (const RMsg_SpawnObject*)pCreationData;
        if (pMsg->strTemplateName[0] == '$')
        {
            pSNO = CreateSystemNetworkObject(pMsg->strTemplateName);
            if (pSNO != nullptr)
            {
                m_SystemObjects.insert(pSNO);
            }
            pNO = pSNO;
        }
        else
        {
            pMI = m_World.SpawnModelInstance(pMsg->strTemplateName, pMsg->strObjectName, pMsg->Transform, true);
            pNO = pMI;
        }
    }

    if (pNO != nullptr)
    {
        pNO->SetRemote(TRUE);
        pNO->SetNodeID(ID);

        if (pMI != nullptr)
        {
            m_ModelInstances[pMI->GetNodeID()] = pMI;
        }
    }

    return pNO;
}

VOID GameNetServer::DeleteRemoteObject(INetworkObject* pObject)
{
    {
        auto iter = m_ModelInstances.find(pObject->GetNodeID());
        if (iter != m_ModelInstances.end())
        {
            m_ModelInstances.erase(iter);
        }
    }
    {
        m_SystemObjects.erase((SystemNetworkObject*)pObject);
    }
    delete pObject;
}

SystemNetworkObject* CreateSystemNetworkObject(const CHAR* strTemplateName)
{
    if (_stricmp(strTemplateName, ConnectionBaseObject::GetTemplateName()) == 0)
    {
        return new ConnectionBaseObject();
    }
    else if (_stricmp(strTemplateName, InputRemotingObject::GetTemplateName()) == 0)
    {
        return new InputRemotingObject();
    }
    return nullptr;
}

VOID GameNetServer::ClientConnected(ConnectedClient* pClient)
{
    DecomposedTransform DT;
    INetworkObject* pNO = SpawnObject(pClient, ConnectionBaseObject::GetTemplateName(), nullptr, DT, XMFLOAT3(0, 0, 0));
    ConnectionBaseObject* pConnectionBase = (ConnectionBaseObject*)pNO;
    pConnectionBase->SetClient(pClient);
    pClient->m_ConnectionBaseObjectID = pNO->GetNodeID();

    RMsg_AssignClientObjectIDs Msg;
    Msg.m_ObjectIDCount = 250;
    Msg.m_FirstObjectID = m_NextObjectID;
    Msg.m_ConnectionBaseID = pConnectionBase->GetNodeID();
    m_NextObjectID += Msg.m_ObjectIDCount;

    pClient->m_SendQueue.QueueReliableMessage((UINT)GameReliableMessageType::AssignClientObjectIDs, &Msg);
}

VOID GameNetServer::ClientDisconnected(ConnectedClient* pClient)
{

}

BOOL GameNetServer::HandleReliableMessage(VOID* pSenderContext, const UINT Opcode, const UINT UniqueIndex, const BYTE* pPayload, const UINT PayloadSizeBytes)
{
    ConnectedClient* pClient = (ConnectedClient*)pSenderContext;

    switch (Opcode)
    {
    case (UINT)GameReliableMessageType::SpawnObject:
        if (PayloadSizeBytes >= sizeof(RMsg_SpawnObject) && pPayload != nullptr)
        {
            SpawnObject(pClient, (const RMsg_SpawnObject*)pPayload);
        }
        return TRUE;
    case (UINT)GameReliableMessageType::DestroyObject:
        if (PayloadSizeBytes >= sizeof(RMsg_DestroyObject) && pPayload != nullptr)
        {
            const RMsg_DestroyObject* pMsg = (const RMsg_DestroyObject*)pPayload;
            ModelInstance* pMI = FindModelInstance(pMsg->ObjectID);
            StateLinkNode* pLN = m_StateIO.FindNode(pMsg->ObjectID);
            if (pMI != nullptr && pLN != nullptr && pLN->pParent != nullptr)
            {
                StateLinkNode* pParent = pLN->pParent;
                if (pParent->ID == pClient->m_ConnectionBaseObjectID)
                {
                    pMI->MarkForDeletion();
                }
            }
        }
        return TRUE;
    }

    return NetServerBase::HandleReliableMessage(pSenderContext, Opcode, UniqueIndex, pPayload, PayloadSizeBytes);
}

ModelInstance* GameNetServer::FindModelInstance(UINT32 NodeID)
{
    auto iter = m_ModelInstances.find(NodeID);
    if (iter != m_ModelInstances.end())
    {
        return iter->second;
    }
    return nullptr;
}

void GameNetServer::ModelInstanceDeleted(ModelInstance* pMI)
{
    m_StateIO.DeleteNodeAndChildren(pMI->GetNodeID());

    auto iter = m_SystemObjects.begin();
    auto end = m_SystemObjects.end();
    while (iter != end)
    {
        SystemNetworkObject* pSNO = *iter++;
        pSNO->NetworkObjectDeleted(pMI);
    }
}

void GameNetServer::Terminate()
{
    m_World.Terminate();
}

bool GameNetServer::LoadLevel(const CHAR* strLevelName)
{
    if (m_pLevelDesc != nullptr)
    {
        ClearLevel();
    }

    LevelDesc* pLevelDesc = (LevelDesc*)DataFile::LoadStructFromFile(STRUCT_TEMPLATE_REFERENCE(LevelDesc), strLevelName);
    if (pLevelDesc == nullptr)
    {
        return false;
    }

    strcpy_s(m_strLevelName, strLevelName);
    m_pLevelDesc = pLevelDesc;

    const UINT32 TemplateCount = (UINT32)m_pLevelDesc->Templates.size();
    for (UINT32 i = 0; i < TemplateCount; ++i)
    {
        TemplateDesc* pTD = m_pLevelDesc->Templates[i];
        m_TemplateDescs[pTD->Name] = pTD;
    }

    const UINT32 NodeCount = (UINT32)m_pLevelDesc->Nodes.size();
    for (UINT32 i = 0; i < NodeCount; ++i)
    {
        BuildNode(XMMatrixIdentity(), m_pLevelDesc->Nodes[i]);
    }

    return true;
}

void GameNetServer::BuildNode(FXMMATRIX matTransform, PlaceNode* pNode)
{
    const XMMATRIX matLocal = pNode->GetLocalTransform();
    const Math::Matrix4 matWorld(matLocal * matTransform);

    if (!pNode->TemplateName.IsEmptyString())
    {
        if (pNode->Type == PNT_ModelOrNull)
        {
            Math::Vector3 Pos;
            Math::Vector4 Orientation;
            FLOAT Scale;
            matWorld.Decompose(Pos, Scale, Orientation);
            DecomposedTransform DT = DecomposedTransform::CreateFromComponents(Pos, Orientation, Scale);
            const WCHAR* strWideTemplateName = pNode->TemplateName.GetSafeString();
            CHAR strTemplateName[64];
            WideCharToMultiByte(CP_ACP, 0, strWideTemplateName, (INT)wcslen(strWideTemplateName) + 1, strTemplateName, ARRAYSIZE(strTemplateName), nullptr, nullptr);
            INetworkObject* pNO = SpawnObject(nullptr, strTemplateName, nullptr, DT, XMFLOAT3(0, 0, 0));
            pNode->pGameObject = pNO;
        }
        else if (pNode->Type == PNT_Template)
        {
            auto iter = m_TemplateDescs.find(pNode->TemplateName);
            if (iter != m_TemplateDescs.end())
            {
                TemplateDesc* pTemplate = iter->second;
                const UINT32 NodeCount = (UINT32)pTemplate->Nodes.size();
                for (UINT32 i = 0; i < NodeCount; ++i)
                {
                    CopyNodes(pNode, pTemplate->Nodes[i]);
                }
            }
        }
    }

    const UINT32 ChildCount = (UINT32)pNode->Children.size();
    for (UINT32 i = 0; i < ChildCount; ++i)
    {
        BuildNode(matWorld, pNode->Children[i]);
    }
}

void GameNetServer::CopyNodes(PlaceNode* pDestParent, PlaceNode* pSrc)
{
    PlaceNode* pCopy = (PlaceNode*)DataFile::StructAlloc(sizeof(PlaceNode));
    new (pCopy) PlaceNode();
    pCopy->Position = pSrc->Position;
    pCopy->RotationYaw = pSrc->RotationYaw;
    pCopy->RotationPitch = pSrc->RotationPitch;
    pCopy->RotationRoll = pSrc->RotationRoll;
    pCopy->Type = pSrc->Type;
    pCopy->TemplateName = pSrc->TemplateName;

    pDestParent->Children.push_back(pCopy);

    const UINT32 ChildCount = (UINT32)pSrc->Children.size();
    for (UINT32 i = 0; i < ChildCount; ++i)
    {
        CopyNodes(pCopy, pSrc->Children[i]);
    }
}

void GameNetServer::ClearLevel()
{
    const UINT32 NodeCount = (UINT32)m_pLevelDesc->Nodes.size();
    for (UINT32 i = 0; i < NodeCount; ++i)
    {
        ClearNode(m_pLevelDesc->Nodes[i]);
    }

    m_TemplateDescs.clear();

    DataFile::Unload(m_pLevelDesc);
    m_pLevelDesc = nullptr;
}

void GameNetServer::ClearNode(PlaceNode* pNode)
{
    if (pNode->pGameObject != nullptr)
    {
        ModelInstance* pMI = (ModelInstance*)pNode->pGameObject;
        pMI->MarkForDeletion();
        pNode->pGameObject = nullptr;
    }

    const UINT32 ChildCount = (UINT32)pNode->Children.size();
    for (UINT32 i = 0; i < ChildCount; ++i)
    {
        ClearNode(pNode->Children[i]);
    }

    DataFile::Unload(pNode);
}

void InputRemotingObject::GetMemberDatas(const MemberDataPosition** ppMemberDatas, UINT* pMemberDataCount) const
{
    static const MemberDataPosition Members[] =
    {
        { StateNodeType::Float,             offsetof(InputRemotingObject, m_XAxis0),         sizeof(FLOAT) },
        { StateNodeType::Float,             offsetof(InputRemotingObject, m_YAxis0),         sizeof(FLOAT) },
        { StateNodeType::Float,             offsetof(InputRemotingObject, m_XAxis1),         sizeof(FLOAT) },
        { StateNodeType::Float,             offsetof(InputRemotingObject, m_YAxis1),         sizeof(FLOAT) },
        { StateNodeType::Float,             offsetof(InputRemotingObject, m_LeftTrigger),    sizeof(FLOAT) },
        { StateNodeType::Float,             offsetof(InputRemotingObject, m_RightTrigger),   sizeof(FLOAT) },
        { StateNodeType::Integer,           offsetof(InputRemotingObject, m_Buttons[0]),     sizeof(UINT32) },
        { StateNodeType::Integer,           offsetof(InputRemotingObject, m_Buttons[1]),     sizeof(UINT32) },
        { StateNodeType::Integer,           offsetof(InputRemotingObject, m_Buttons[2]),     sizeof(UINT32) },
        { StateNodeType::Integer,           offsetof(InputRemotingObject, m_Buttons[3]),     sizeof(UINT32) },
        { StateNodeType::Integer,           offsetof(InputRemotingObject, m_TargetNodeID),   sizeof(UINT32) },
    };

    *ppMemberDatas = Members;
    *pMemberDataCount = ARRAYSIZE(Members);
}

void InputRemotingObject::ClientUpdate(const NetworkInputState& InputState)
{
    m_XAxis0 = InputState.XAxis0;
    m_YAxis0 = InputState.YAxis0;
    m_XAxis1 = InputState.XAxis1;
    m_YAxis1 = InputState.YAxis1;
    m_LeftTrigger = InputState.LeftTrigger;
    m_RightTrigger = InputState.RightTrigger;
    for (UINT32 i = 0; i < ARRAYSIZE(m_Buttons); ++i)
    {
        if (InputState.Buttons[i])
        {
            m_Buttons[i].SourceUpdate();
        }
    }
}

void InputRemotingObject::ServerTick(GameNetServer* pServer, FLOAT DeltaTime, DOUBLE AbsoluteTime)
{
    UINT32 TargetNodeID = 0;
    if (m_TargetNodeID.DestinationCheckAndUpdate(&TargetNodeID))
    {
        if (TargetNodeID == 0)
        {
            m_pTargetModelInstance = nullptr;
        }
        else
        {
            m_pTargetModelInstance = pServer->FindModelInstance(TargetNodeID);
        }
    }

    NetworkInputState IS = {};
    IS.XAxis0 = m_XAxis0;
    IS.YAxis0 = m_YAxis0;
    IS.XAxis1 = m_XAxis1;
    IS.YAxis1 = m_YAxis1;
    IS.LeftTrigger = m_LeftTrigger;
    IS.RightTrigger = m_RightTrigger;
    for (UINT32 i = 0; i < ARRAYSIZE(IS.Buttons); ++i)
    {
        IS.Buttons[i] = m_Buttons[i].DestinationCheckAndUpdate();
    }

    if (m_pTargetModelInstance != nullptr)
    {
        m_pTargetModelInstance->ServerProcessInput(IS, DeltaTime, AbsoluteTime);
    }
}

void InputRemotingObject::NetworkObjectDeleted(INetworkObject* pNO)
{
    if (m_pTargetModelInstance == pNO)
    {
        m_pTargetModelInstance = nullptr;
    }
}
