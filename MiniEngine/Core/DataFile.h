#pragma once

#include <windows.h>

#include <assert.h>

#include <stack>
#include <list>
#include <vector>

#pragma warning( disable: 4200 )

#include "..\3rdParty\json\src\json.hpp"
using json = nlohmann::json;

enum DataIndirection
{
    DI_Value,
    DI_Pointer,
    DI_GrowableArray,
    DI_STL_PointerVector,
    DI_STL_PointerList,
    DI_Terminator,
    DI_Union,
};

enum DataType
{
    DT_Void,
    DT_Struct,
    DT_Bool,
    DT_Int8,
    DT_UInt8,
    DT_Int16,
    DT_UInt16,
    DT_Int32,
    DT_UInt32,
    DT_Int64,
    DT_UInt64,
    DT_Float,
    DT_Double,
    DT_String,
    DT_WString,
    DT_StringID,
    DT_Buffer,
};

enum StructLocation
{
    SL_Inline,
    SL_File
};

struct DataMemberEnum
{
    const WCHAR*    strText;
    INT             iValue;
};
#define MEMBER_ENUM_TERMINATOR {NULL,0}

struct DataStructTemplate;
struct DataMemberTemplate
{
    DWORD                       dwOffsetInStruct;
    DWORD                       dwAlignmentInBytes;
    const CHAR*                 strMemberName;
    DataIndirection             Indirection;
    DWORD                       dwArraySize;
    DataType                    Type;
    DataStructTemplate*         pStructTemplate;
    const DataMemberEnum*       pEnums;
};

typedef VOID StructPostLoadFunction( VOID* pData );

struct DataStructTemplate
{
    const CHAR*                 strName;
    StructLocation              Location;
    DWORD                       dwSize;
    DWORD                       dwAlignmentInBytes;
    BOOL                        UnionMembers;
    StructPostLoadFunction*     pPostFunction;
    DataStructTemplate*         pParentStruct;
    DataMemberTemplate          pMembers[];
};

class DataFile
{
public:
    static DWORD GetMemberSize( const DataMemberTemplate* pMember );
    static DWORD GetDataTypeSize( const DataMemberTemplate* pMember );
    static DWORD GetStructSize( const DataStructTemplate* pTemplate );

    static VOID SetDataFileRootPath( const CHAR* strRootPath );
    static VOID* LoadStructFromFile( const DataStructTemplate* pTemplate, const CHAR* strName, VOID* pBuffer = NULL );
    static VOID WriteStructToFile( const DataStructTemplate* pTemplate, const CHAR* strName, const VOID* pBuffer );
    static VOID Unload( VOID* pBuffer );
    static VOID UnloadAll();
    static VOID* StructAlloc(SIZE_T SizeBytes);
};

struct GrowableArrayBase
{
protected:
    CHAR*   m_pElements;
    DWORD   m_dwCount;
    DWORD   m_dwCapacity;
    DWORD   m_dwStride;
public:
    GrowableArrayBase()
        : m_pElements( NULL ),
        m_dwCount( 0 ),
        m_dwCapacity( 0 ),
        m_dwStride( 0 )
    {
    }
    ~GrowableArrayBase()
    {
        delete[] m_pElements;
    }
    VOID SetStride( DWORD dwStride ) { m_dwStride = dwStride; }
    DWORD GetStride() const { return m_dwStride; }
    DWORD GetCount() const { return m_dwCount; }
    DWORD GetCapacity() const { return m_dwCapacity; }
    CHAR* GetElement( DWORD dwIndex )
    {
        assert( dwIndex < m_dwCount );
        return (CHAR*)( m_pElements + dwIndex * m_dwStride );
    }
    const CHAR* GetElement( DWORD dwIndex ) const
    {
        assert( dwIndex < m_dwCount );
        return (const CHAR*)( m_pElements + dwIndex * m_dwStride );
    }
    VOID Add( const CHAR* pNewElement );
    CHAR* AddEmpty();
protected:
    VOID GrowCapacity( DWORD dwNewCapacity );
};

template<class T>
class GrowableArray : public GrowableArrayBase
{
public:
    GrowableArray()
    {
        SetStride( sizeof(T) );
    }
    VOID AddItem( const T* pNewElement ) { Add( (const CHAR*)pNewElement ); }
    T* AddEmptyItem() { return (T*)AddEmpty(); }
    T& operator[]( DWORD dwIndex ) { return *(T*)GetElement(dwIndex); }
    const T& operator[]( DWORD dwIndex ) const { return *(T*)GetElement(dwIndex); }
};

struct Buffer
{
    DWORD   dwBufferSize;
    VOID*   pBuffer;
};

typedef std::vector<VOID*> VoidPtrVector;
typedef std::list<VOID*> VoidPtrList;

class DataFileParser
{
protected:
    struct ParseContext
    {
        const CHAR* m_strEntranceName;
        const DataStructTemplate* m_pTemplate;
        BOOL m_bStructEntered;
        CHAR* m_pBuffer;
    };
    std::stack<ParseContext> m_ParsePoints;

public:
    DataFileParser( const DataStructTemplate* pTemplate, VOID* pBuffer );

    HRESULT ParseJsonTree(const CHAR* strBuffer);

protected:
    HRESULT ParseJsonTreeHelper( const CHAR* strAnsiName, const json& pValue, const UINT Depth, const BOOL ProcessThisLevel );

    VOID Push( const CHAR* strEntranceName, const DataStructTemplate* pTemplate, CHAR* pBuffer, BOOL bStructEntered );
    VOID Pop();
    const CHAR* GetCurrentContextName();
    const DataStructTemplate* GetCurrentTemplate();
    CHAR* GetCurrentBuffer();
    BOOL IsStructEntered();
    VOID SetStructEntered( BOOL bEntered );

    const DataMemberTemplate* FindMember( const WCHAR* strName, UINT NameLen, const DataStructTemplate* pStruct );
    
    HRESULT ProcessElement( const CHAR* strAnsiName, const json& Value );
    VOID ProcessMember( const DataMemberTemplate* pTemplate, const json& Value );
    VOID ProcessEndStruct( const CHAR* strAnsiName, const json& Value );

    VOID ParseValue( const DataMemberTemplate* pTemplate, const json& Value, INT OffsetOverride );
    VOID ParseString( const DataMemberTemplate* pTemplate, const json& Value, INT OffsetOverride );
    VOID ParseWideString( const DataMemberTemplate* pTemplate, const json& Value, INT OffsetOverride );
    VOID ParseBuffer( const DataMemberTemplate* pTemplate, const json& FileNameValue, INT OffsetOverride );
    VOID ParseValueIntoGrowableArray( const DataMemberTemplate* pTemplate, const json& Value, GrowableArrayBase& ArrayBase );
    VOID ParseStringIntoGrowableArray( const DataMemberTemplate* pTemplate, const json& Value, GrowableArrayBase& ArrayBase );
    VOID ParseWideStringIntoGrowableArray( const DataMemberTemplate* pTemplate, const json& Value, GrowableArrayBase& ArrayBase );
    VOID ParseBufferIntoGrowableArray( const DataMemberTemplate* pTemplate, const json& FileNameValue, GrowableArrayBase& ArrayBase );
    VOID* ParseScalarValue( DataType Type, const json& Value, VOID* pDest, const DataMemberEnum* pEnums );
};

#define STRUCT_TEMPLATE_NAME(Name) __StructTemplate_##Name
#define STRUCT_TEMPLATE_REFERENCE(Name) &(STRUCT_TEMPLATE_NAME(Name))
#define STRUCT_TEMPLATE_EXTERNAL(Name) extern DataStructTemplate STRUCT_TEMPLATE_NAME(Name);

DWORD __Struct_ComputeStructOffsets( DataStructTemplate* pStructTemplate );

#define STRUCT_INITIALIZE(Name) \
    DWORD __StructInit_##Name = __Struct_ComputeStructOffsets( STRUCT_TEMPLATE_REFERENCE(Name) ); \

#ifdef _DEBUG

INT __Struct_Validate( const CHAR* strStructName, DWORD dwCompilerSize, const DataStructTemplate* pStructTemplate );

#define STRUCT_DEBUG_CHECK(Name) \
    INT __StructDebugCheck_##Name = __Struct_Validate( #Name, sizeof( ##Name ), STRUCT_TEMPLATE_REFERENCE( Name ) ); \

#else

#define STRUCT_DEBUG_CHECK(Name)

#endif

extern DataStructTemplate __StructTemplate_STRUCT_TEMPLATE_SELF;
#define __StructTemplate_0 __StructTemplate_STRUCT_TEMPLATE_SELF
#define __StructTemplate_nullptr __StructTemplate_STRUCT_TEMPLATE_SELF

#define STRUCT_TEMPLATE_START_FILE(Name, PostLoadFunction, ParentStruct) \
    DataStructTemplate STRUCT_TEMPLATE_NAME(Name) = { #Name, SL_File, 0, 0, FALSE, PostLoadFunction, STRUCT_TEMPLATE_REFERENCE(ParentStruct), { 

#define STRUCT_TEMPLATE_START_INLINE(Name, PostLoadFunction, ParentStruct) \
    DataStructTemplate STRUCT_TEMPLATE_NAME(Name) = { #Name, SL_Inline, 0, 0, FALSE, PostLoadFunction, STRUCT_TEMPLATE_REFERENCE(ParentStruct), { 

#define UNION_TEMPLATE_START_INLINE(Name, PostLoadFunction) \
    DataStructTemplate STRUCT_TEMPLATE_NAME(Name) = { #Name, SL_Inline, 0, 0, TRUE, PostLoadFunction, &__StructTemplate_0, { 

#define STRUCT_TEMPLATE_END(Name) \
    MEMBER_TERMINATOR } }; \
    STRUCT_INITIALIZE(Name) \
    STRUCT_DEBUG_CHECK(Name) \

#define UNION_TEMPLATE_END STRUCT_TEMPLATE_END

#define MEMBER_TERMINATOR { 0, 0, NULL, DI_Terminator, 0, DT_Void, NULL, NULL }
#define MEMBER_NAMELESS_UNION { 0, 0, NULL, DI_Union, 0, DT_Void, NULL, NULL },
#define MEMBER_NAMELESS_UNION_END { 0, 0, NULL, DI_Union, 1, DT_Void, NULL, NULL },

#define MEMBER_TYPED_VALUE(Name, Type, ArraySize, Alignment) { 0, Alignment, #Name, DI_Value, ArraySize, Type, NULL, NULL },
#define MEMBER_TYPED_POINTER(Name, Type, ArraySize ) { 0, __alignof(VOID*), #Name, DI_Pointer, ArraySize, Type, NULL, NULL },

#define MEMBER_BOOL(Name) MEMBER_TYPED_VALUE( Name, DT_Bool, 1, __alignof(BOOL) )
#define MEMBER_SBYTE(Name) MEMBER_TYPED_VALUE( Name, DT_Int8, 1, __alignof(CHAR) )
#define MEMBER_UBYTE(Name) MEMBER_TYPED_VALUE( Name, DT_UInt8, 1, __alignof(UCHAR) )
#define MEMBER_SHORT(Name) MEMBER_TYPED_VALUE( Name, DT_Int16, 1, __alignof(SHORT) )
#define MEMBER_USHORT(Name) MEMBER_TYPED_VALUE( Name, DT_UInt16, 1, __alignof(USHORT) )
#define MEMBER_INT(Name) MEMBER_TYPED_VALUE( Name, DT_Int32, 1, __alignof(INT) )
#define MEMBER_UINT(Name) MEMBER_TYPED_VALUE( Name, DT_UInt32, 1, __alignof(UINT) )
#define MEMBER_FLOAT(Name) MEMBER_TYPED_VALUE( Name, DT_Float, 1, __alignof(FLOAT) )
#define MEMBER_DOUBLE(Name) MEMBER_TYPED_VALUE( Name, DT_Double, 1, __alignof(DOUBLE) )
#define MEMBER_INT64(Name) MEMBER_TYPED_VALUE( Name, DT_Int64, 1, __alignof(INT64) )
#define MEMBER_UINT64(Name) MEMBER_TYPED_VALUE( Name, DT_UInt64, 1, __alignof(UINT64) )

#define MEMBER_ENUM(Name, EnumStructs) { 0, __alignof(INT), #Name, DI_Value, 1, DT_Int32, NULL, EnumStructs },

#define MEMBER_BOOL_ARRAY(Name, ArraySize) MEMBER_TYPED_VALUE( Name, DT_Bool, ArraySize, __alignof(BOOL) )
#define MEMBER_INT_ARRAY(Name, ArraySize) MEMBER_TYPED_VALUE( Name, DT_Int32, ArraySize, __alignof(INT) )
#define MEMBER_UINT_ARRAY(Name, ArraySize) MEMBER_TYPED_VALUE( Name, DT_UInt32, ArraySize, __alignof(UINT) )
#define MEMBER_FLOAT_ARRAY(Name, ArraySize) MEMBER_TYPED_VALUE( Name, DT_Float, ArraySize, __alignof(FLOAT) )
#define MEMBER_DOUBLE_ARRAY(Name, ArraySize) MEMBER_TYPED_VALUE( Name, DT_Double, ArraySize, __alignof(DOUBLE) )

#define MEMBER_VECTOR2(Name) MEMBER_FLOAT_ARRAY(Name, 2)
#define MEMBER_VECTOR3(Name) MEMBER_FLOAT_ARRAY(Name, 3)
#define MEMBER_VECTOR4(Name) MEMBER_FLOAT_ARRAY(Name, 4)
#define MEMBER_ALIGNED_VECTOR4(Name) MEMBER_TYPED_VALUE( Name, DT_Float, 4, 16 )

#define MEMBER_STRING(Name) MEMBER_TYPED_VALUE(Name, DT_String, 1, __alignof(CHAR*) )
#define MEMBER_WSTRING(Name) MEMBER_TYPED_VALUE(Name, DT_WString, 1, __alignof(WCHAR*) )
#define MEMBER_STRINGID(Name) MEMBER_TYPED_VALUE(Name, DT_StringID, 1, __alignof(CHAR*) )
#define MEMBER_BUFFER(Name) MEMBER_TYPED_VALUE(Name, DT_Buffer, 1, __alignof(Buffer) )

#define MEMBER_PADDING(SizeInBytes,AlignmentInBytes) { 0, AlignmentInBytes, NULL, DI_Value, SizeInBytes, DT_Void, NULL },
#define MEMBER_PADDING_POINTER() MEMBER_PADDING(sizeof(VOID*),__alignof(VOID*))

#define MEMBER_STRUCT_VALUEARRAY(Name, StructTemplate, ArraySize, Alignment) { 0, Alignment, #Name, DI_Value, ArraySize, DT_Struct, STRUCT_TEMPLATE_REFERENCE(StructTemplate), NULL },
#define MEMBER_STRUCT_POINTERARRAY(Name, StructTemplate, ArraySize, Alignment) { 0, Alignment, #Name, DI_Pointer, ArraySize, DT_Struct, STRUCT_TEMPLATE_REFERENCE(StructTemplate), NULL },
#define MEMBER_STRUCT_VALUE(Name, StructTemplate) MEMBER_STRUCT_VALUEARRAY(Name, StructTemplate, 1, 0)
#define MEMBER_STRUCT_POINTER(Name, StructTemplate) MEMBER_STRUCT_POINTERARRAY(Name, StructTemplate, 1, __alignof(VOID*))

#define MEMBER_TYPED_GROWABLE_ARRAY(Name, Type, StructTemplate) { 0, __alignof(GrowableArrayBase), #Name, DI_GrowableArray, 1, Type, StructTemplate, NULL },
#define MEMBER_STRUCT_GROWABLE_ARRAY(Name, StructTemplate) MEMBER_TYPED_GROWABLE_ARRAY( Name, DT_Struct, STRUCT_TEMPLATE_REFERENCE(StructTemplate) )
#define MEMBER_INT_GROWABLE_ARRAY(Name) MEMBER_TYPED_GROWABLE_ARRAY( Name, DT_Int32, NULL )
#define MEMBER_UINT_GROWABLE_ARRAY(Name) MEMBER_TYPED_GROWABLE_ARRAY( Name, DT_UInt32, NULL )
#define MEMBER_FLOAT_GROWABLE_ARRAY(Name) MEMBER_TYPED_GROWABLE_ARRAY( Name, DT_Float, NULL )
#define MEMBER_STRING_GROWABLE_ARRAY(Name) MEMBER_TYPED_GROWABLE_ARRAY( Name, DT_String, NULL )
#define MEMBER_WSTRING_GROWABLE_ARRAY(Name) MEMBER_TYPED_GROWABLE_ARRAY( Name, DT_WString, NULL )
#define MEMBER_STRINGID_GROWABLE_ARRAY(Name) MEMBER_TYPED_GROWABLE_ARRAY( Name, DT_StringID, NULL )
#define MEMBER_BUFFER_GROWABLE_ARRAY(Name) MEMBER_TYPED_GROWABLE_ARRAY( Name, DT_Buffer, NULL )

#define MEMBER_TYPED_STL_POINTER_VECTOR(Name, Type, StructTemplate) { 0, __alignof(VoidPtrVector), #Name, DI_STL_PointerVector, 1, Type, StructTemplate, NULL },
#define MEMBER_STRUCT_STL_POINTER_VECTOR(Name, StructTemplate) MEMBER_TYPED_STL_POINTER_VECTOR( Name, DT_Struct, STRUCT_TEMPLATE_REFERENCE(StructTemplate) )
#define MEMBER_STRING_STL_POINTER_VECTOR(Name) MEMBER_TYPED_STL_POINTER_VECTOR( Name, DT_String, NULL )
#define MEMBER_WSTRING_STL_POINTER_VECTOR(Name) MEMBER_TYPED_STL_POINTER_VECTOR( Name, DT_WString, NULL )
#define MEMBER_STRINGID_STL_POINTER_VECTOR(Name) MEMBER_TYPED_STL_POINTER_VECTOR( Name, DT_StringID, NULL )
#define MEMBER_INT_STL_POINTER_VECTOR(Name) MEMBER_TYPED_STL_POINTER_VECTOR( Name, DT_Int32, NULL )
#define MEMBER_UINT_STL_POINTER_VECTOR(Name) MEMBER_TYPED_STL_POINTER_VECTOR( Name, DT_UInt32, NULL )
#define MEMBER_FLOAT_STL_POINTER_VECTOR(Name) MEMBER_TYPED_STL_POINTER_VECTOR( Name, DT_Float, NULL )
#define MEMBER_BOOL_STL_POINTER_VECTOR(Name) MEMBER_TYPED_STL_POINTER_VECTOR( Name, DT_Bool, NULL )

#define MEMBER_TYPED_STL_POINTER_LIST(Name, Type, StructTemplate) { 0, __alignof(VoidPtrList), #Name, DI_STL_PointerList, 1, Type, StructTemplate, NULL },
#define MEMBER_STRUCT_STL_POINTER_LIST(Name, StructTemplate) MEMBER_TYPED_STL_POINTER_LIST( Name, DT_Struct, STRUCT_TEMPLATE_REFERENCE(StructTemplate) )
#define MEMBER_STRING_STL_POINTER_LIST(Name) MEMBER_TYPED_STL_POINTER_LIST( Name, DT_String, NULL )
#define MEMBER_WSTRING_STL_POINTER_LIST(Name) MEMBER_TYPED_STL_POINTER_LIST( Name, DT_WString, NULL )
#define MEMBER_STRINGID_STL_POINTER_LIST(Name) MEMBER_TYPED_STL_POINTER_LIST( Name, DT_StringID, NULL )
#define MEMBER_INT_STL_POINTER_LIST(Name) MEMBER_TYPED_STL_POINTER_LIST( Name, DT_Int32, NULL )
#define MEMBER_UINT_STL_POINTER_LIST(Name) MEMBER_TYPED_STL_POINTER_LIST( Name, DT_UInt32, NULL )
#define MEMBER_FLOAT_STL_POINTER_LIST(Name) MEMBER_TYPED_STL_POINTER_LIST( Name, DT_Float, NULL )
#define MEMBER_BOOL_STL_POINTER_LIST(Name) MEMBER_TYPED_STL_POINTER_LIST( Name, DT_Bool, NULL )
