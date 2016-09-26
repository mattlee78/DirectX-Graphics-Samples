#pragma once

#include <windows.h>
#include <set>
#include <functional>
#include <assert.h>

#include <DirectXMath.h>
using namespace DirectX;

#include "ZoneAllocator.h"

#define DOUBLE_EXPONENTIAL_PREDICTION 1

typedef ZoneAllocator<65536> StateZoneAllocator;

extern LARGE_INTEGER g_CurrentRecvTimestamp;
extern LARGE_INTEGER g_LerpThresholdTicks;

class StateNode;
class StateNodeChildSet;
class StateSnapshot;

enum class StateNodeType
{
    Complex = 0,
    Integer,
    Integer4,
    Float,
    Float2,
    Float3,
    Float4,
    Float4AsByteN4,
    Float2AsHalf2,
    Float4AsHalf4,
    Matrix43,
    Matrix44,
    String,
    WideString,
    Float3Delta,
    Float3AsHalf4Delta,
    Float4AsHalf4Delta,
    Float3AsQwordDelta,
    Blob,
};

inline bool IsDeltaType( StateNodeType Type )
{
    switch (Type)
    {
    case StateNodeType::Float3Delta:
    case StateNodeType::Float3AsHalf4Delta:
    case StateNodeType::Float4AsHalf4Delta:
    case StateNodeType::Float3AsQwordDelta:
        return true;
    default:
        return false;
    }
}

class StateNodeTypeCodec
{
public:
    static SIZE_T GetStorageSize( StateNodeType Type );
    static SIZE_T GetExpandedSize( StateNodeType Type );

    static VOID Encode( StateNodeType Type, VOID* pStorageData, const VOID* pExpandedData );
    static VOID Decode( StateNodeType Type, VOID* pExpandedData, const VOID* pStorageData );
};

struct StateBlob
{
public:
    VOID* pBuffer;
    SIZE_T SizeBytes;

    StateBlob()
        : pBuffer( nullptr ),
          SizeBytes( 0 )
    { }
};

struct Float3Qword
{
    INT64 X : 22;
    INT64 Z : 22;
    INT64 Y : 20;

    XMVECTOR GetFloat3() const
    {
        return XMVectorSet( (FLOAT)X, (FLOAT)Y, (FLOAT)Z, 0.0f ) * XMVectorReplicate( 0.001f );
    }
    VOID SetFloat3( XMVECTOR v )
    {
        XMFLOAT3 Encoded;
        XMStoreFloat3( &Encoded, v * XMVectorReplicate( 1000.0f ) );
        X = (INT64)Encoded.x;
        Y = (INT64)Encoded.y;
        Z = (INT64)Encoded.z;
    }
};

inline FLOAT CalcJitterAdjustment( INT64 A, INT64 B, INT64 C )
{
    if( B == A )
    {
        return 1.0f;
    }
    return (FLOAT)( (DOUBLE)( C - B ) / (DOUBLE)( B - A ) );
}

inline FLOAT AdjustLerpValue( FLOAT LerpValue )
{
    assert( LerpValue >= 0.0f );
    return ( LerpValue <= 6.0f ) ? LerpValue : 1.0f;
}

static const FLOAT g_Smoothing = 0.25f;
static const FLOAT g_Correction = 0.75f;

template <typename T>
struct StateDelta
{
private:
    T CurrentValue;
    T PrevValue;
    LARGE_INTEGER CurrentRecvTimestamp;
    LARGE_INTEGER PreviousRecvTimestamp;

private:
    static inline void StateStore( XMFLOAT3* pValue, CXMVECTOR v ) { XMStoreFloat3( pValue, v ); }
    static inline void StateStore( XMFLOAT4* pValue, CXMVECTOR v ) { XMStoreFloat4( pValue, v ); }
    static inline XMVECTOR StateLoad( const XMFLOAT3* pValue ) { return XMLoadFloat3( pValue ); }
    static inline XMVECTOR StateLoad( const XMFLOAT4* pValue ) { return XMLoadFloat4( pValue ); }

public:
    StateDelta()
    {
        StateStore( &CurrentValue, XMVectorZero() );
        StateStore( &PrevValue, XMVectorZero() );
        CurrentRecvTimestamp.QuadPart = 0;
        PreviousRecvTimestamp.QuadPart = 0;
    }

    const T* GetRawData() const { return &CurrentValue; }
    XMVECTOR GetRawValue() const { return StateLoad( &CurrentValue ); }
    void SetRawValue( CXMVECTOR Value ) { StateStore( &CurrentValue, Value ); }
    INT64 GetSampleTime() const { return CurrentRecvTimestamp.QuadPart; }
    XMVECTOR GetCurrentValue() { return Lerp( GetSampleTime() ); }
    XMVECTOR GetCurrentValueQuaternion() { return LerpQuaternion( GetSampleTime() ); }

    VOID Reset( const T& Value )
    {
        CurrentValue = Value;
        PrevValue = Value;
        CurrentRecvTimestamp.QuadPart = 0;
        PreviousRecvTimestamp.QuadPart = 0;
    }

    VOID ReceiveNewValue( const XMVECTOR Value, LARGE_INTEGER CurrentTimestamp )
    {
        PrevValue = CurrentValue;
        PreviousRecvTimestamp = CurrentRecvTimestamp;
        StateStore( &CurrentValue, Value );
        CurrentRecvTimestamp = CurrentTimestamp;
    }

    inline XMVECTOR Lerp( INT64 CurrentTime ) const
    {
        const XMVECTOR Prev = StateLoad( &PrevValue );
        if (CurrentRecvTimestamp.QuadPart <= PreviousRecvTimestamp.QuadPart)
        {
            return Prev;
        }
        FLOAT LerpValue = (FLOAT)( (DOUBLE)( CurrentTime - CurrentRecvTimestamp.QuadPart ) / (DOUBLE)( CurrentRecvTimestamp.QuadPart - PreviousRecvTimestamp.QuadPart ) );
        if ( ( CurrentTime - CurrentRecvTimestamp.QuadPart ) > g_LerpThresholdTicks.QuadPart)
        {
            LerpValue = 1.0f;
        }
        const XMVECTOR Current = StateLoad( &CurrentValue );
        XMVECTOR Result = XMVectorLerp(Prev, Current, LerpValue);
        return Result;
    }

    inline XMVECTOR LerpQuaternion( INT64 CurrentTime ) const
    {
        const XMVECTOR Prev = StateLoad(&PrevValue);
        if (CurrentRecvTimestamp.QuadPart <= PreviousRecvTimestamp.QuadPart)
        {
            return Prev;
        }
        FLOAT LerpValue = (FLOAT)((DOUBLE)(CurrentTime - CurrentRecvTimestamp.QuadPart) / (DOUBLE)(CurrentRecvTimestamp.QuadPart - PreviousRecvTimestamp.QuadPart));
        if ((CurrentTime - CurrentRecvTimestamp.QuadPart) > g_LerpThresholdTicks.QuadPart)
        {
            LerpValue = 1.0f;
        }
        const XMVECTOR Current = StateLoad(&CurrentValue);
        XMVECTOR Result = XMQuaternionSlerp(Prev, Current, LerpValue);
        return Result;
    }
};

typedef StateDelta<XMFLOAT3> StateFloat3Delta;
typedef StateDelta<XMFLOAT4> StateFloat4Delta;

struct StateNodeCreationData : public StateBlob
{
public:
    BOOL OwnsMemory;
    UINT32 CreationCode;

    StateNodeCreationData()
        : OwnsMemory( FALSE ),
          CreationCode( 0 )
    { }

    ~StateNodeCreationData()
    {
        if( OwnsMemory )
        {
            delete[] pBuffer;
        }
    }

    VOID Clone( const StateNodeCreationData& Other, StateSnapshot* pSnapshot );
    VOID Clone( const VOID* pData, SIZE_T DataSizeBytes );
};

class StateNode
{
private:
    UINT32 m_ID;
    StateNodeType m_Type;
    bool m_PreviouslyChanged;
    union
    {
        VOID* m_pData;
        StateNodeChildSet* m_pChildSet;
    };
    StateNodeCreationData m_CreationData;

public:
    StateNode( StateSnapshot* pSnapshot, UINT32 ID, StateNodeType Type, const VOID* pData )
        : m_ID( ID ),
        m_Type( Type ),
        m_PreviouslyChanged( false )
    {
        m_pData = CreateLocalData( pSnapshot, pData );
        CopyToLocalData( pSnapshot, pData );
    }
    ~StateNode();

    UINT32 GetID() const { return m_ID; }
    StateNodeType GetType() const { return m_Type; }
    const VOID* GetRawData() const { return m_pData; }
    SIZE_T GetStorageDataSize() const { return StateNodeTypeCodec::GetStorageSize( m_Type ); }
    SIZE_T GetExpandedDataSize() const { return StateNodeTypeCodec::GetExpandedSize( m_Type ); }

    bool WasPreviouslyChanged() const { return m_PreviouslyChanged; }
    void SetPreviouslyChanged() { m_PreviouslyChanged = true; }

    StateNodeCreationData& GetCreationData() { return m_CreationData; }

    BOOL IsComplex() const { return m_Type == StateNodeType::Complex; }
    BOOL IsBlob() const { return m_Type == StateNodeType::Blob || m_Type == StateNodeType::String || m_Type == StateNodeType::WideString; }

    BOOL HasEqualData( const StateNode* pOther ) const;

    StateNodeChildSet* GetChildSet() const { return ( m_Type == StateNodeType::Complex ) ? m_pChildSet : nullptr; }

private:
    VOID* CreateLocalData( StateSnapshot* pSnapshot, const VOID* pData );
    VOID CopyToLocalData( StateSnapshot* pSnapshot, const VOID* pData );
};

class StateNodeChildSet
{
private:
    StateZoneAllocator* m_pAllocator;

    struct ChildNode
    {
        StateNode* pChild;
        ChildNode* pNext;
    };

    ChildNode* m_pRoot;

public:
    StateNodeChildSet()
        : m_pRoot( nullptr ),
          m_pAllocator( nullptr )
    { }

    VOID Initialize( StateZoneAllocator* pAllocator )
    {
        m_pAllocator = pAllocator;
    }

    BOOL AddChild( StateNode* pNode )
    {
        ChildNode** ppNext = &m_pRoot;
        ChildNode* p = m_pRoot;
        while( !NodeLess( pNode, p ) )
        {
            ppNext = &p->pNext;
            p = p->pNext;
        }
        Insert( ppNext, pNode );

#ifdef _DEBUG
        Validate();
#endif

        return TRUE;
    }

public:
    struct Iterator
    {
    private:
        friend class StateNodeChildSet;

        ChildNode* m_pCurrent;

        StateNode* CurrentValue() const { return m_pCurrent != nullptr ? m_pCurrent->pChild : nullptr; }

    public:
        StateNode* operator*()
        {
            return CurrentValue();
        }

        Iterator& operator++()
        {
            if( m_pCurrent != nullptr )
            {
                m_pCurrent = m_pCurrent->pNext;
            }
            return *this;
        }

        Iterator operator++(int)
        {
            Iterator i = *this;
            ++*this;
            return i;
        }

        bool operator==( const Iterator& RHS ) const
        {
            return CurrentValue() == RHS.CurrentValue();
        }

        bool operator!=( const Iterator& RHS ) const
        {
            return CurrentValue() != RHS.CurrentValue();
        }
    };

    Iterator Begin()
    {
        Iterator iter;
        iter.m_pCurrent = m_pRoot;
        return iter;
    }

    Iterator End()
    {
        Iterator iter;
        iter.m_pCurrent = nullptr;
        return iter;
    }

private:
    VOID Insert( ChildNode** ppNext, StateNode* p )
    {
        assert( m_pAllocator != nullptr );
        auto* pChild = m_pAllocator->Allocate<ChildNode>();
        pChild->pChild = p;
        pChild->pNext = *ppNext;
        *ppNext = pChild;
    }

    BOOL NodeLess( StateNode* pA, ChildNode* pB )
    {
        assert( pA != nullptr );
        if( pB == nullptr )
        {
            return TRUE;
        }
        return pA->GetID() < pB->pChild->GetID();
    }

    VOID Validate()
    {
        UINT ID = 0;
        ChildNode* p = m_pRoot;
        while( p != nullptr )
        {
            UINT NextID = p->pChild->GetID();
            assert( NextID > ID );
            ID = NextID;
            p = p->pNext;
        }
    }
};

interface IStateSnapshotDebug
{
    virtual VOID PrintLine( UINT32 Indent, const CHAR* strFormat, ... ) = 0;
};

interface IStateSnapshotDiff
{
    virtual VOID NodeCreated( StateNode* pNode, StateNode* pParentNode ) = 0;
    virtual VOID NodeDeleted( StateNode* pNode ) = 0;
    virtual VOID NodeChanged( StateNode* pPrev, StateNode* pCurrent ) = 0;
    virtual VOID NodeSame( StateNode* pPrev, StateNode* pCurrent ) {}
};

class StateSnapshot : public StateNodeChildSet
{
private:
    UINT32 m_Index;
    UINT32 m_Refcount;
    StateZoneAllocator m_ZoneAllocator;

public:
    StateSnapshot( UINT32 Index );
    ~StateSnapshot();

    UINT32 AddRef() { return ++m_Refcount; }
    UINT32 Release();

    UINT32 GetIndex() const { assert( m_Index != -1 ); return m_Index; }

    StateZoneAllocator* GetAllocator() { return &m_ZoneAllocator; }

    VOID DebugPrint( IStateSnapshotDebug* pDebug );

    VOID Diff( StateSnapshot* pNew, IStateSnapshotDiff* pIDiff );

    StateNode* AddComplex( StateNode* pParent, UINT32 ID );
    StateNode* AddDataType( StateNode* pParent, UINT32 ID, StateNodeType Type, const VOID* pData );

    StateNode* AddFloat( StateNode* pParent, UINT32 ID, const FLOAT* pExistingFloat ) { return AddDataType( pParent, ID, StateNodeType::Float, pExistingFloat ); }
    StateNode* AddFloat4( StateNode* pParent, UINT32 ID, const FLOAT* pExistingFloat4 ) { return AddDataType( pParent, ID, StateNodeType::Float4, pExistingFloat4 ); }
    StateNode* AddMatrix44( StateNode* pParent, UINT32 ID, const FLOAT* pExistingMatrix ) { return AddDataType( pParent, ID, StateNodeType::Matrix44, pExistingMatrix ); }
};
