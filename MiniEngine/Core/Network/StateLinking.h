#pragma once
#include <windows.h>
#include <unordered_map>

#include "StateObjects.h"
#include "StructuredLogFile.h"

class StateInputOutput;

interface INetworkObject
{
public:
    struct MemberDataPosition
    {
        StateNodeType Type;
        SIZE_T OffsetBytes;
        SIZE_T SizeBytes;
    };
    virtual VOID GetMemberDatas( const MemberDataPosition** ppMemberDatas, UINT* pMemberDataCount ) const = 0;

public:
    virtual VOID SetNodeID( UINT ID ) {}
    virtual UINT GetNodeID() const { return 0; }
    virtual VOID SetRemote( BOOL Remote ) = 0;
    virtual UINT GetType() const { return 0; }

    BOOL FindMemberData( StateNodeType MemberType, UINT CreationCode, VOID** ppData, SIZE_T* pDataSizeBytes ) const
    {
        const MemberDataPosition* pMemberDatas = nullptr;
        UINT MemberDataCount = 0;
        GetMemberDatas( &pMemberDatas, &MemberDataCount );

        if( CreationCode < MemberDataCount && MemberType == pMemberDatas[CreationCode].Type )
        {
            *ppData = (VOID*)( (BYTE*)this + pMemberDatas[CreationCode].OffsetBytes );
            *pDataSizeBytes = pMemberDatas[CreationCode].SizeBytes;
            return TRUE;
        }

        return FALSE;
    }

    virtual BOOL CreateDynamicChildNode( const VOID* pCreationData, const SIZE_T CreationDataSizeBytes, const StateNodeType NodeType, VOID** ppCreatedData, SIZE_T* pCreatedDataSizeBytes ) { return FALSE; }

    virtual UINT CreateAdditionalBindings( StateInputOutput* pStateIO, UINT ParentID, UINT FirstChildID ) { return FirstChildID; }
};

typedef std::unordered_map<UINT, INetworkObject*> NetworkObjectMap;

struct StateLinkNode
{
public:
    UINT32 ID;
    BOOL IncludeInSnapshot;
    StateNodeType Type;
    StateLinkNode* pParent;
    StateLinkNode* pFirstChild;
    StateLinkNode* pSibling;
    VOID* pData;
    StateNodeCreationData CreationData;
};

class StateInputOutput
{
private:
    typedef std::unordered_map<UINT32, StateLinkNode*> StateLinkNodeMap;
    StateLinkNodeMap m_NodeMap;
    StateLinkNode* m_pRootNode;

    UINT32 m_SnapshotIndex;

    StateSnapshot* m_pNullSnapshot;

    BOOL m_ClientMode;

    StateLinkNode* m_pLoggingNode;
    StructuredLogFile m_LogFile;

public:
    StateInputOutput();

    VOID SetSnapshotIndex( UINT32 Index ) { m_SnapshotIndex = Index; }
    VOID SetClientMode( BOOL ClientMode ) { m_ClientMode = ClientMode; }

    VOID SetLoggingNode( StateLinkNode* pNode ) { m_pLoggingNode = pNode; }
    HRESULT StartLogging( const WCHAR* strFileName );
    HRESULT StopLogging();

    // receive input from remote host:
    BOOL UpdateNodeData( UINT32 ID, const VOID* pData, SIZE_T DataSizeBytes );

    // local host use only:
    StateLinkNode* FindNode( UINT32 ID );
    BOOL CreateNode( UINT32 ParentID, UINT32 ID, StateNodeType Type, VOID* pData, SIZE_T DataSizeBytes, UINT CreationCode, const VOID* pCreationData, SIZE_T CreationDataSizeBytes, BOOL IncludeInSnapshot );
    UINT32 CreateNodeGroup( UINT32 ParentID, UINT32 StartingNodeID, INetworkObject* pProxyObject, const VOID* pCreationData, SIZE_T CreationDataSizeBytes, BOOL IncludeInSnapshot );
    BOOL DeleteNodeAndChildren( UINT32 ID );

    // output:
    StateSnapshot* CreateSnapshot();
    StateSnapshot* GetNullSnapshot() const { return m_pNullSnapshot; }

private:
    VOID DeleteNodeTree( StateLinkNode* pNode );
    VOID LogUpdate( const StateLinkNode* pNode, LARGE_INTEGER Timestamp );
};
