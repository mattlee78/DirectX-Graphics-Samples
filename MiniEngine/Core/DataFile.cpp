#include "pch.h"
#include "DataFile.h"
#include "Utility.h"
#include "FileUtility.h"
#include "StringID.h"

#pragma warning(disable:4267)

// Treat StringIDs as pointers for insertion into vector/list:
C_ASSERT( sizeof(StringID) == sizeof(VOID*) );

CHAR g_strDataFileRootPath[MAX_PATH] = "";

DataStructTemplate __StructTemplate_STRUCT_TEMPLATE_SELF = { 0 };

#define MSG_WARNING(format, ...) DEBUGPRINT(format, __VA_ARGS__)
#define MSG_ERROR(format, ...) ERROR(format, __VA_ARGS__)

struct LoadedDataFile
{
    CHAR strFileName[MAX_PATH];
    VOID* pBuffer;
    DWORD dwBufferSize;
    const DataStructTemplate* pTemplate;
};
std::vector<LoadedDataFile> g_LoadedDataFiles;

inline DWORD NextMultiple( DWORD dwBase, DWORD dwAlignment )
{
    assert( dwAlignment >= 1 );
    DWORD dwResult = dwAlignment * ( ( dwBase + ( dwAlignment - 1 ) ) / dwAlignment );
    return dwResult;
}

// Struct validation performed at CRT initialization time
// First ensures that the size of the struct template matches the size of the struct
// Next ensures the various members have valid settings (correct sizes, alignments, etc)
INT __Struct_Validate( const CHAR* strStructName, DWORD dwCompilerSize, const DataStructTemplate* pStructTemplate )
{
    DWORD dwComputedSize = DataFile::GetStructSize( pStructTemplate );
    if( dwCompilerSize != dwComputedSize )
    {
        MSG_ERROR( "Invalid structure template for struct \"%s\".  Size mismatch (template is %d bytes, struct is %d bytes).\n", strStructName, dwComputedSize, dwCompilerSize );
    }

    const DataMemberTemplate* pMember = pStructTemplate->pMembers;
    while( pMember->Indirection != DI_Terminator )
    {
        if( pMember->Indirection == DI_Union )
        {
            if( pMember->strMemberName != NULL )
            {
                MSG_ERROR( "Union sigil of struct \"%s\" cannot have a name.", strStructName );
            }
            ++pMember;
            continue;
        }

        if( pMember->dwAlignmentInBytes == 0 )
        {
            MSG_ERROR( "Member \"%s\" of struct \"%s\" has a zero alignment; it must be 1 or greater.", pMember->strMemberName, strStructName );
        }
        if( pMember->dwArraySize < 1 )
        {
            MSG_ERROR( "Member \"%s\" of struct \"%s\" has a zero array size; it must be 1 or greater.", pMember->strMemberName, strStructName );
        }
        if( pMember->Indirection == DI_GrowableArray && pMember->dwArraySize > 1 )
        {
            MSG_ERROR( "Member \"%s\" of struct \"%s\" cannot be an array member since it is already a growable array.", pMember->strMemberName, strStructName );
        }
        switch( pMember->Type )
        {
        case DT_String:
        case DT_WString:
        case DT_StringID:
        case DT_Buffer:
            if( pMember->dwArraySize > 1 )
            {
                MSG_ERROR( "Member \"%s\" of struct \"%s\" cannot have an array size since it is a string or buffer type.", pMember->strMemberName, strStructName );
            }
            break;
        }
        ++pMember;
    }
    return 0;
}

DWORD ComputeUnionAlignment( DataMemberTemplate* pMember )
{
    assert( pMember != NULL );

    DataMemberTemplate* pStartMember = pMember;

    DWORD dwMaxAlignment = 0;

    while( pMember->Indirection != DI_Union || pMember->dwArraySize != 1 )
    {
        if( pMember->Indirection == DI_Terminator )
        {
            return dwMaxAlignment;
        }

        if( pMember->Indirection == DI_Union )
        {
            assert( pMember->dwArraySize == 0 );
            ++pMember;
            continue;
        }

        if( pMember->pStructTemplate != NULL && pMember->dwAlignmentInBytes == 0 )
        {
            __Struct_ComputeStructOffsets( pMember->pStructTemplate );
            pMember->dwAlignmentInBytes = pMember->pStructTemplate->dwAlignmentInBytes;
        }

        dwMaxAlignment = std::max( dwMaxAlignment, pMember->dwAlignmentInBytes );

        ++pMember;
    }

    return dwMaxAlignment;
}

// Computes each member's offset in the struct, and stores it in the member data structures (and the sum in the struct template)
DWORD __Struct_ComputeStructOffsets( DataStructTemplate* pStructTemplate )
{
    // If we have already computed the struct size, don't do it again
    if( pStructTemplate->dwSize > 0 )
    {
        return pStructTemplate->dwSize;
    }

    // Compute the size of the parent struct
    DWORD ParentStructSize = 0;
    if( pStructTemplate->pParentStruct != &__StructTemplate_0 && pStructTemplate->pParentStruct != NULL )
    {
        ParentStructSize = __Struct_ComputeStructOffsets( pStructTemplate->pParentStruct );
    }

    // Search for struct members with a special struct template
    // Set these members to the parent's struct template
    // These are used for self-referential structures (trees, linked lists, etc)
    DataMemberTemplate* pMember = pStructTemplate->pMembers;
    while( pMember->Indirection != DI_Terminator )
    {
        if( pMember->pStructTemplate == &__StructTemplate_STRUCT_TEMPLATE_SELF )
        {
            pMember->pStructTemplate = pStructTemplate;
        }
        ++pMember;
    }

    // Walk through the members and compute their offsets, taking alignment rules into account
    DWORD dwMaxAlignment = 0;
    DWORD dwOffset = ParentStructSize;

    DWORD dwUnionStartOffset = (DWORD)-1;
    DWORD dwUnionEndOffset = 0;
    DWORD dwUnionAlignmentBytes = 0;

    pMember = pStructTemplate->pMembers;

    // If this entire struct is a union, initialize union state now
    if( pStructTemplate->UnionMembers )
    {
        dwUnionStartOffset = 0;
        dwUnionAlignmentBytes = ComputeUnionAlignment( pMember );
    }

    while( pMember->Indirection != DI_Terminator )
    {
        // read out the current member alignment in a variable; we may adjust it upwards
        DWORD dwMemberAlignment = pMember->dwAlignmentInBytes;

        // check for union sigils
        if( pMember->Indirection == DI_Union )
        {
            if( pStructTemplate->UnionMembers )
            {
                MSG_ERROR( "Nameless unions not supported inside union template \"%s\".", pStructTemplate->strName );
            }

            if( pMember->dwArraySize == 0 )
            {
                // entering union

                // make sure we're not re-entering union
                if( dwUnionStartOffset != (DWORD)-1 )
                {
                    MSG_ERROR( "Struct template \"%s\" error: Must exit union before entering union again.", pStructTemplate->strName );
                }

                // store the start offset of the union
                dwUnionStartOffset = dwOffset;

                // store the max alignment of the union - all union members will be aligned to this size
                dwUnionAlignmentBytes = ComputeUnionAlignment( pMember );
            }
            else
            {
                // exiting union

                // move struct offset to the end of the union
                dwOffset = std::max( dwOffset, dwUnionEndOffset );

                // clear union state variables
                dwUnionStartOffset = (DWORD)-1;
                dwUnionEndOffset = 0;
                dwUnionAlignmentBytes = 0;
            }

            ++pMember;
            continue;
        }


        // if we're inside a union, reset the offset to the beginning of the union, and align to the union alignment
        if( dwUnionStartOffset != (DWORD)-1 )
        {
            dwOffset = dwUnionStartOffset;
            dwMemberAlignment = std::max( dwMemberAlignment, dwUnionAlignmentBytes );
        }

        // compute alignment for struct value members
        if( pMember->Type == DT_Struct && pMember->Indirection == DI_Value && pMember->pStructTemplate != pStructTemplate )
        {
            __Struct_ComputeStructOffsets( pMember->pStructTemplate );
            pMember->dwAlignmentInBytes = pMember->pStructTemplate->dwAlignmentInBytes;
            dwMemberAlignment = pMember->dwAlignmentInBytes;
        }

        // ensure that our computed member alignment is valid
        assert( dwMemberAlignment >= 1 );
        assert( dwMemberAlignment >= pMember->dwAlignmentInBytes );

        // store the max alignment for this struct
        dwMaxAlignment = std::max( dwMaxAlignment, dwMemberAlignment );

        // align the current offset and store it to the current member
        dwOffset = NextMultiple( dwOffset, dwMemberAlignment );
        pMember->dwOffsetInStruct = dwOffset;

        // increment the offset by the current member's size
        DWORD dwMemberSize = DataFile::GetMemberSize( pMember );
        dwOffset += dwMemberSize;

        // if we're inside a union, record the max offset as the union end offset
        if( dwUnionStartOffset != (DWORD)-1 )
        {
            dwUnionEndOffset = std::max( dwUnionEndOffset, dwOffset );
        }

        ++pMember;
    }
    dwOffset = NextMultiple( dwOffset, dwMaxAlignment );
    pStructTemplate->dwSize = dwOffset;
    pStructTemplate->dwAlignmentInBytes = dwMaxAlignment;
    return dwOffset;
}

VOID DataFile::SetDataFileRootPath( const CHAR* strRootPath )
{
    strcpy_s( g_strDataFileRootPath, strRootPath );
}

// Returns the size of a member's data type given by the member template
DWORD DataFile::GetDataTypeSize( const DataMemberTemplate* pMember )
{
    DWORD dwElementSize = 0;

    switch( pMember->Type )
    {
    case DT_Bool:
    case DT_Int32:
    case DT_UInt32:
    case DT_Float:
        dwElementSize = 4;
        break;
    case DT_Int8:
    case DT_UInt8:
        dwElementSize = 1;
        break;
    case DT_Int16:
    case DT_UInt16:
        dwElementSize = 2;
        break;
    case DT_Double:
        dwElementSize = sizeof(DOUBLE);
        break;
    case DT_String:
        dwElementSize = sizeof(CHAR*);
        break;
    case DT_WString:
        dwElementSize = sizeof(WCHAR*);
        break;
    case DT_StringID:
        dwElementSize = sizeof(StringID);
        break;
    case DT_Struct:
        dwElementSize = GetStructSize( pMember->pStructTemplate );
        break;
    case DT_Buffer:
        dwElementSize = sizeof(Buffer);
        break;
    case DT_Void:
        dwElementSize = 1;
        break;
    default:
        ERROR( "Unknown type encountered in GetDataTypeSize." );
        break;
    }

    // Multiply by the array size
    assert( pMember->dwArraySize >= 1 );
    DWORD dwTotalSize = pMember->dwArraySize * dwElementSize;

    return dwTotalSize;
}

// Returns the size of a data member, taking into account various styles of indirection
DWORD DataFile::GetMemberSize( const DataMemberTemplate* pMember )
{
    assert( pMember != NULL );
    if( pMember->Indirection == DI_Terminator )
    {
        return 0;
    }
    else if( pMember->Indirection == DI_Pointer )
    {
        return sizeof( VOID* );
    }
    else if( pMember->Indirection == DI_GrowableArray )
    {
        return sizeof( GrowableArrayBase );
    }
    else if( pMember->Indirection == DI_STL_PointerVector )
    {
        return sizeof( VoidPtrVector );
    }
    else if( pMember->Indirection == DI_STL_PointerList )
    {
        return sizeof( VoidPtrList );
    }
    return GetDataTypeSize( pMember );
}

DWORD DataFile::GetStructSize( const DataStructTemplate* pTemplate )
{
    assert( pTemplate != NULL );
    if( pTemplate->pMembers == NULL )
    {
        return 0;
    }
    if( pTemplate->dwSize > 0 )
    {
        return pTemplate->dwSize;
    }
    DWORD dwTotalSize = 0;
    DWORD dwMaxAlignment = 0;
    const DataMemberTemplate* pMember = &pTemplate->pMembers[0];
    while( pMember->Indirection != DI_Terminator )
    {
        dwMaxAlignment = std::max( dwMaxAlignment, pMember->dwAlignmentInBytes );
        dwTotalSize = NextMultiple( dwTotalSize, pMember->dwAlignmentInBytes );
        DWORD dwMemberSize = GetMemberSize( pMember );
        dwTotalSize += dwMemberSize;
        ++pMember;
    }
    dwTotalSize = NextMultiple( dwTotalSize, dwMaxAlignment );
    return dwTotalSize;
}

VOID* AllocateStructMemory( SIZE_T Size )
{
    assert( Size > 0 );
    VOID* pBuffer = malloc( Size );
    ZeroMemory( pBuffer, Size );
    return pBuffer;
}

VOID FreeStructMemory( VOID* pBuffer )
{
    assert( pBuffer != NULL );
    free( pBuffer );
}

// Initializes a struct after it has been allocated, basically the universal constructor for the data loader
// Fills in the growable array bases (kinda like a constructor)
VOID StructInitialize( CHAR* pBuffer, const DataStructTemplate* pTemplate )
{
    assert( pBuffer != NULL && pTemplate != NULL );

    const DataMemberTemplate* pMember = pTemplate->pMembers;
    while( pMember->Indirection != DI_Terminator )
    {
        // Initialize growable array
        if( pMember->Indirection == DI_GrowableArray )
        {
            GrowableArrayBase* pArrayBase = (GrowableArrayBase*)( pBuffer + pMember->dwOffsetInStruct );
            DWORD dwStride = DataFile::GetDataTypeSize( pMember );
            pArrayBase->SetStride( dwStride );
        }
        else if( pMember->Indirection == DI_STL_PointerList )
        {
            auto* pList = (VoidPtrList*)( pBuffer + pMember->dwOffsetInStruct );
            new (pList) VoidPtrList();
        }
        // Initialize inline struct
        if( pMember->Indirection == DI_Value && pMember->Type == DT_Struct )
        {
            StructInitialize( pBuffer + pMember->dwOffsetInStruct, pMember->pStructTemplate );
        }
        ++pMember;
    }
}

const LoadedDataFile* FindLoadedFile( const CHAR* strFileName )
{
    const DWORD dwCount = (DWORD)g_LoadedDataFiles.size();
    for( DWORD i = 0; i < dwCount; ++i )
    {
        LoadedDataFile& ldf = g_LoadedDataFiles[i];
        if( _stricmp( strFileName, ldf.strFileName ) == 0 )
        {
            return &ldf;
        }
    }
    return NULL;
}

// Entry point for data loader.
VOID* DataFile::LoadStructFromFile( const DataStructTemplate* pTemplate, const CHAR* strName, VOID* pBuffer )
{
    assert( pTemplate != NULL && strName != NULL );
    assert( pTemplate->Location == SL_File );
    assert( pTemplate->dwSize > 0 );

    // Build a filename using the struct template name and the given file name
    CHAR strFileName[MAX_PATH];
    sprintf_s( strFileName, "%s\\%s.%s.json", g_strDataFileRootPath, strName, pTemplate->strName );

    // If we have loaded this file before, return that buffer
    const LoadedDataFile* pLoadedFile = FindLoadedFile( strFileName );
    if( pLoadedFile != NULL )
    {
        assert( pLoadedFile->pTemplate == pTemplate );
        assert( pLoadedFile->dwBufferSize == pTemplate->dwSize );
        if( pBuffer == NULL )
        {
            return pLoadedFile->pBuffer;
        }
        else
        {
            memcpy( pBuffer, pLoadedFile->pBuffer, pTemplate->dwSize );
            return pBuffer;
        }
    }

    // Initialize memory for the top level struct if none was provided
    BOOL bProvidedMemory = TRUE;
    if( pBuffer == NULL )
    {
        bProvidedMemory = FALSE;
        pBuffer = AllocateStructMemory( pTemplate->dwSize );
        StructInitialize( (CHAR*)pBuffer, pTemplate );
    }

    // Parse data in strFileName into struct defined in pTemplate and stored in pBuffer.
    /*
    XMLParser Parser;
    Parser.RegisterSAXCallbackInterface( &DFParser );
    HRESULT hr = Parser.ParseXMLFile( strFileName );
    */

    WCHAR strWideFileName[MAX_PATH];
    MultiByteToWideChar(CP_ACP, 0, strFileName, (INT)strlen(strFileName) + 1, strWideFileName, ARRAYSIZE(strWideFileName));
    Utility::ByteArray DataFile = Utility::ReadFileSync(strWideFileName);

    if(DataFile->size() > 0)
    {
        DataFileParser DFParser(pTemplate, pBuffer);
        DataFile->push_back(0);
        DFParser.ParseJsonTree((const CHAR*)DataFile->data());
    }
    else
    {
        MSG_WARNING( "Could not load file \"%s\" with struct template \"%s\".", strFileName, pTemplate->strName );
        if( !bProvidedMemory )
        {
            FreeStructMemory( pBuffer );
        }
        pBuffer = NULL;
    }

    // Store a record for the loaded file
    if( pBuffer != NULL )
    {
        LoadedDataFile ldf;
        ldf.pBuffer = pBuffer;
        ldf.dwBufferSize = pTemplate->dwSize;
        ldf.pTemplate = pTemplate;
        strcpy_s( ldf.strFileName, strFileName );
        g_LoadedDataFiles.push_back( ldf );
    }

    return pBuffer;
}

VOID DataFile::Unload( VOID* pBuffer )
{
    FreeStructMemory( pBuffer );
}

VOID DataFile::UnloadAll()
{
    UINT Count = (UINT)g_LoadedDataFiles.size();
    for( UINT i = 0; i < Count; ++i )
    {
        LoadedDataFile& LDF = g_LoadedDataFiles[i];
        Unload( LDF.pBuffer );
        LDF.pBuffer = nullptr;
    }
    g_LoadedDataFiles.clear();
}

VOID* DataFile::StructAlloc(SIZE_T SizeBytes)
{
    return AllocateStructMemory(SizeBytes);
}

VOID WriteStruct( json& Writer, BOOL bWriteEnclosingTag, const DataStructTemplate* pTemplate, const VOID* pBuffer );

const WCHAR* FindEnum( const DataMemberEnum* pEnums, INT iValue )
{
    while( pEnums->strText != NULL )
    {
        if( pEnums->iValue == iValue )
        {
            return pEnums->strText;
        }
        ++pEnums;
    }
    return L"";
}

VOID WriteValue( json& BaseWriter, const DataMemberTemplate* pTemplate, const VOID* pBuffer )
{
    assert( pBuffer != NULL );

    const DWORD dwArraySize = pTemplate->dwArraySize;
    if( dwArraySize > 1 )
    {
        //BaseWriter.resize( dwArraySize );
        assert(false);
    }

    for( DWORD i = 0; i < dwArraySize; ++i )
    {
        json* pWriter = &BaseWriter;
        if( dwArraySize > 1 )
        {
            pWriter = &BaseWriter[i];
        }

        switch( pTemplate->Type )
        {
        case DT_Bool:
        case DT_Int32:
            {
                INT iValue = *( (INT*)pBuffer + i );
                if( pTemplate->pEnums != nullptr )
                {
                    const WCHAR* strValue = FindEnum( pTemplate->pEnums, iValue );
                    CHAR strAnsiValue[128];
                    WideCharToMultiByte( CP_ACP, 0, strValue, (INT)wcslen( strValue ) + 1, strAnsiValue, ARRAYSIZE(strAnsiValue), NULL, NULL );
                    *pWriter = strAnsiValue;
                }
                else
                {
                    *pWriter = iValue;
                }
                break;
            }
        case DT_UInt32:
            {
                UINT iValue = *( (UINT*)pBuffer + i );
                *pWriter = iValue;
                break;
            }
        case DT_Float:
            {
                FLOAT fValue = *( (FLOAT*)pBuffer + i );
                *pWriter = (DOUBLE)fValue;
                break;
            }
        case DT_Double:
            {
                DOUBLE fValue = *( (DOUBLE*)pBuffer + i );
                *pWriter = fValue;
                break;
            }
        case DT_Int16:
            {
                INT iValue = (INT)*( (SHORT*)pBuffer + i );
                *pWriter = iValue;
                break;
            }
        case DT_UInt16:
            {
                UINT iValue = (UINT)*( (USHORT*)pBuffer + i );
                *pWriter = iValue;
                break;
            }
        case DT_Int8:
            {
                INT iValue = (INT)*( (CHAR*)pBuffer + i );
                *pWriter = iValue;
                break;
            }
        case DT_UInt8:
            {
                UINT iValue = (UINT)*( (UCHAR*)pBuffer + i );
                *pWriter = iValue;
                break;
            }
        }
    }
}

VOID WriteMember( json& ParentWriter, const DataMemberTemplate* pTemplate, const VOID* pBuffer, BOOL bWriteEnclosingTag )
{
    if( pBuffer == NULL )
        return;

    switch( pTemplate->Type )
    {
    case DT_String:
    case DT_WString:
        VOID* pData = *(VOID**)pBuffer;
        if( pData == NULL )
            return;
        break;
    }

    json::value_t vt = json::value_t::null;
    if( pTemplate->Type == DT_Struct )
    {
        vt = json::value_t::object;
    }
    if( pTemplate->dwArraySize > 1 )
    {
        vt = json::value_t::array;
    }
    json Writer( vt );

    switch( pTemplate->Type )
    {
    case DT_Int32:
    case DT_Bool:
    case DT_UInt32:
    case DT_UInt16:
    case DT_Int16:
    case DT_UInt8:
    case DT_Int8:
    case DT_Float:
    case DT_Double:
        {
            WriteValue( Writer, pTemplate, pBuffer );
            break;
        }
    case DT_String:
        {
            CHAR* pString = *(CHAR**)pBuffer;
            assert( pString != NULL );
            Writer = pString;
            break;
        }
    case DT_WString:
        {
            WCHAR* pString = *(WCHAR**)pBuffer;
            assert( pString != NULL );
            CHAR strValue[512];
            WideCharToMultiByte( CP_ACP, 0, pString, (INT)wcslen( pString ) + 1, strValue, ARRAYSIZE(strValue), NULL, NULL );
            Writer = strValue;
            break;
        }
    case DT_StringID:
        {
            StringID String = *(StringID*)pBuffer;
            const WCHAR* pString = String.GetSafeString();
            CHAR strValue[512];
            WideCharToMultiByte( CP_ACP, 0, pString, (INT)wcslen( pString ) + 1, strValue, ARRAYSIZE(strValue), NULL, NULL );
            Writer = strValue;
            break;
        }
    case DT_Struct:
        assert( pTemplate->pStructTemplate != NULL );
        if( pTemplate->pStructTemplate->Location == SL_File )
        {
            Writer = pTemplate->strMemberName;
            DataFile::WriteStructToFile( pTemplate->pStructTemplate, pTemplate->strMemberName, pBuffer );
        }
        else
        {
            WriteStruct( Writer, FALSE, pTemplate->pStructTemplate, pBuffer );
        }
        break;
    }

    const CHAR* strAttributeName = pTemplate->strMemberName;

    switch( pTemplate->Indirection )
    {
    case DI_GrowableArray:
    case DI_STL_PointerList:
    case DI_STL_PointerVector:
        ParentWriter[strAttributeName].push_back( Writer );
        break;
    default:
        ParentWriter[strAttributeName] = Writer;
        break;
    }
}

VOID WriteStruct( json& Writer, BOOL bWriteEnclosingTag, const DataStructTemplate* pTemplate, const VOID* pBuffer )
{
    const DataMemberTemplate* pMember = pTemplate->pMembers;
    while( pMember != NULL && pMember->Indirection != DI_Terminator )
    {
        if( pMember->Indirection == DI_GrowableArray || pMember->Type == DT_Struct || pMember->Type == DT_Void )
        {
            ++pMember;
            continue;
        }
        const VOID* pMemberData = (const VOID*)( (const BYTE*)pBuffer + pMember->dwOffsetInStruct );
        if( pMember->Indirection == DI_Pointer )
        {
            const VOID* pPtr = *(const VOID**)pMemberData;
            pMemberData = pPtr;
        }
        WriteMember( Writer, pMember, pMemberData, FALSE );
        ++pMember;
    }

    pMember = pTemplate->pMembers;
    while( pMember != NULL && pMember->Indirection != DI_Terminator )
    {
        if( ( pMember->Type != DT_Struct && pMember->Type != DI_GrowableArray ) || pMember->Type == DT_Void )
        {
            ++pMember;
            continue;
        }

        const VOID* pMemberData = (const VOID*)( (const BYTE*)pBuffer + pMember->dwOffsetInStruct );
        if( pMember->Indirection == DI_Pointer )
        {
            const VOID* pPtr = *(const VOID**)pMemberData;
            pMemberData = pPtr;
        }
        else if( pMember->Indirection == DI_GrowableArray )
        {
            GrowableArrayBase* pGAB = (GrowableArrayBase*)pMemberData;
            for( DWORD i = 0; i < pGAB->GetCount(); ++i )
            {
                WriteMember( Writer, pMember, pGAB->GetElement( i ), TRUE );
            }
        }
        else if( pMember->Indirection == DI_STL_PointerVector )
        {
            VoidPtrVector* pVec = (VoidPtrVector*)pMemberData;
            for( DWORD i = 0; i < pVec->size(); ++i )
            {
                WriteMember( Writer, pMember, (*pVec)[i], TRUE );
            }
        }
        else if( pMember->Indirection == DI_STL_PointerList )
        {
            VoidPtrList* pList = (VoidPtrList*)pMemberData;
            VoidPtrList::iterator iter = pList->begin();
            VoidPtrList::iterator end = pList->end();
            while( iter != end )
            {
                WriteMember( Writer, pMember, *iter, TRUE );
                ++iter;
            }
        }
        else
        {
            WriteMember( Writer, pMember, pMemberData, TRUE );
        }
        ++pMember;
    }
}

VOID DataFile::WriteStructToFile( const DataStructTemplate* pTemplate, const CHAR* strName, const VOID* pBuffer )
{
    assert( pBuffer != NULL && strName != NULL && pTemplate != NULL );
    assert( pTemplate->Location == SL_File );

    assert( pTemplate->dwSize > 0 );

    CHAR strFileName[MAX_PATH];
    sprintf_s( strFileName, "%s\\%s.%s.json", g_strDataFileRootPath, strName, pTemplate->strName );

    json Root( json::value_t::object );
    WriteStruct( Root, TRUE, pTemplate, pBuffer );

//     auto string << Root;
//     WriteFileA( strFileName, string.c_str(), (UINT)string.length() );
}

DataFileParser::DataFileParser( const DataStructTemplate* pTemplate, VOID* pBuffer )
{
    Push( pTemplate->strName, pTemplate, (CHAR*)pBuffer, FALSE );
}

VOID DataFileParser::Push( const CHAR* strEntranceName, const DataStructTemplate* pTemplate, CHAR* pBuffer, BOOL bStructEntered )
{
    ParseContext pp = { strEntranceName, pTemplate, bStructEntered, pBuffer };
    m_ParsePoints.push( pp );
}

VOID DataFileParser::Pop()
{
    m_ParsePoints.pop();
}

const CHAR* DataFileParser::GetCurrentContextName()
{
    const ParseContext& pp = m_ParsePoints.top();
    return pp.m_strEntranceName;
}

const DataStructTemplate* DataFileParser::GetCurrentTemplate()
{
    const ParseContext& pp = m_ParsePoints.top();
    return pp.m_pTemplate;
}

CHAR* DataFileParser::GetCurrentBuffer()
{
    const ParseContext& pp = m_ParsePoints.top();
    return pp.m_pBuffer;
}

BOOL DataFileParser::IsStructEntered()
{
    const ParseContext& pp = m_ParsePoints.top();
    return pp.m_bStructEntered;
}

VOID DataFileParser::SetStructEntered( BOOL bEntered )
{
    ParseContext& pp = m_ParsePoints.top();
    pp.m_bStructEntered = bEntered;
}

BOOL MatchStrings( const WCHAR* strA, UINT LenA, const CHAR* strB )
{
    static CHAR strMatch[200];
    assert( LenA < ARRAYSIZE( strMatch ) );
    WideCharToMultiByte( CP_ACP, 0, strA, LenA, strMatch, ARRAYSIZE(strMatch), NULL, NULL );
    strMatch[LenA] = '\0';
    return _stricmp( strMatch, strB ) == 0;
}

HRESULT DataFileParser::ParseJsonTree(const CHAR* strBuffer)
{
    const CHAR* strRootName = "";
    if( GetCurrentTemplate() != nullptr )
    {
        strRootName = GetCurrentTemplate()->strName;
    }

    json result = json::parse(strBuffer);

    return ParseJsonTreeHelper(strRootName, result, 0, TRUE);
}

inline BOOL IsComplexArray( const json& Value )
{
    if( !Value.is_array() )
    {
        return FALSE;
    }
    const json& FirstChild = Value[0U];
    return ( FirstChild.is_object() || FirstChild.is_array() );
}

HRESULT DataFileParser::ParseJsonTreeHelper( const CHAR* strAnsiName, const json& Value, const UINT Depth, const BOOL ProcessThisLevel )
{
    const BOOL ComplexArray = IsComplexArray( Value );
    const INT LoopCount = ComplexArray ? Value.size() : 1;

    for( INT LoopIndex = 0; LoopIndex < LoopCount; ++LoopIndex )
    {
        if( ProcessThisLevel )
        {
            ProcessElement( strAnsiName, Value );
            //DebugSpew( "%u: Processing key \"%s\" type %d\n", Depth, strAnsiName, Value.type() );
        }

        if( Value.is_object() )
        {
            auto iter = Value.begin();
            auto end = Value.end();
            while( iter != end )
            {
                HRESULT hr = ParseJsonTreeHelper( iter.key().c_str(), *iter, Depth + 1, TRUE );
                if( FAILED(hr) )
                {
                    return hr;
                }
                ++iter;
            }
        }
        else if( ComplexArray )
        {
            ParseJsonTreeHelper( strAnsiName, Value[LoopIndex], Depth + 1, FALSE );
        }

        if( ProcessThisLevel )
        {
            ProcessEndStruct( strAnsiName, Value );
        }
    }

    return S_OK;
}

HRESULT DataFileParser::ProcessElement( const CHAR* strAnsiName, const json& Value )
{
    const UINT NameLen = (UINT)strlen( strAnsiName );
    WCHAR strName[128];
    MultiByteToWideChar( CP_ACP, 0, strAnsiName, NameLen + 1, strName, ARRAYSIZE(strName) );

    // Make sure the parse stack has at least one entry
    assert( m_ParsePoints.size() > 0 );

    // If we haven't entered a struct yet, look for the struct name specified by the current struct template
    if( !IsStructEntered() )
    {
        if( MatchStrings( strName, NameLen, GetCurrentTemplate()->strName ) )
        {
            // Enter the struct if we get the element name we're expecting for this struct template
            SetStructEntered( TRUE );
        }

        // Discontinue parsing this element, next element will be one of this struct's members
        return S_OK;
    }

    // Search for the member specified by the element name
    const DataMemberTemplate* pMember = FindMember( strName, NameLen, GetCurrentTemplate() );
    if( pMember == NULL )
    {
        // Member not found
        WCHAR strTemp[100];
        wcsncpy_s( strTemp, strName, NameLen );
        strTemp[NameLen] = L'\0';
        MSG_WARNING( "Did not find a match for tag \"%S\" in struct \"%s\".\n", strTemp, GetCurrentTemplate()->strName );
        return S_OK;
    }

    // Process the member
    // If we've found a struct member, this method will push a new entry onto the parse stack
    ProcessMember( pMember, Value );

    return S_OK;
}

VOID DataFileParser::ProcessEndStruct( const CHAR* strAnsiName, const json& Value )
{
    const UINT NameLen = (UINT)strlen( strAnsiName );
    WCHAR strName[128];
    MultiByteToWideChar( CP_ACP, 0, strAnsiName, NameLen + 1, strName, ARRAYSIZE(strName) );

    if( IsStructEntered() )
    {
        if( MatchStrings( strName, NameLen, GetCurrentContextName() ) )
        {
            SetStructEntered( FALSE );
            const DataStructTemplate* pTemplate = GetCurrentTemplate();
            if( pTemplate->pPostFunction != NULL )
            {
                pTemplate->pPostFunction( GetCurrentBuffer() );
            }
            Pop();
        }
        else
        {
            //MSG_WARNING( "Encountered unexpected XML close tag for struct \"%s\", context \"%s\".\n", GetCurrentTemplate()->strName, GetCurrentContextName() );
        }
    }
}

const DataMemberTemplate* DataFileParser::FindMember( const WCHAR* strName, UINT NameLen, const DataStructTemplate* pStruct )
{
    const DataMemberTemplate* pMember = &pStruct->pMembers[0];
    while( pMember->Indirection != DI_Terminator )
    {
        if( pMember->strMemberName != NULL && MatchStrings( strName, NameLen, pMember->strMemberName ) )
        {
            return pMember;
        }
        ++pMember;
    }

    if( pStruct->pParentStruct != &__StructTemplate_0 )
    {
        pMember = FindMember( strName, NameLen, pStruct->pParentStruct );
    }
    else
    {
        pMember = NULL;
    }

    return pMember;
}

VOID DataFileParser::ProcessMember( const DataMemberTemplate* pTemplate, const json& Value )
{
    // Compute this member's destination address
    CHAR* pDestination = GetCurrentBuffer() + pTemplate->dwOffsetInStruct;

    CHAR strStructName[256];

    // Value indirection - we parse straight into a place within previously allocated memory
    if( pTemplate->Indirection == DI_Value )
    {
        switch( pTemplate->Type )
        {
        case DT_Struct:
            {
                const DataStructTemplate* pStruct = pTemplate->pStructTemplate;
                if( pStruct->Location == SL_File )
                {
                    // Load from a new file
                    const std::string& s = Value;
                    strcpy_s( strStructName, s.c_str());
                    VOID* pData = DataFile::LoadStructFromFile( pStruct, strStructName, (VOID*)pDestination );
                    return;
                }
                else
                {
                    // Load inline struct
                    Push( pTemplate->strMemberName, pStruct, pDestination, TRUE );
                    return;
                }
                break;
            }
        case DT_Buffer:
            ParseBuffer( pTemplate, Value, -1 );
            break;
        case DT_String:
            ParseString( pTemplate, Value, -1 );
            break;
        case DT_WString:
        case DT_StringID:
            ParseWideString( pTemplate, Value, -1 );
            break;
        default:
            ParseValue( pTemplate, Value, -1 );
            break;
        }
    }
    // Pointer indirection - we parse into a new allocation
    else if( pTemplate->Indirection == DI_Pointer )
    {
        switch( pTemplate->Type )
        {
        case DT_Struct:
            {
                const DataStructTemplate* pStruct = pTemplate->pStructTemplate;
                if( pStruct->Location == SL_File )
                {
                    // Load from a new file
                    const std::string& s = Value;
                    strcpy_s( strStructName, s.c_str());
                    VOID* pData = DataFile::LoadStructFromFile( pStruct, strStructName, NULL );
                    *(VOID**)pDestination = pData;
                    return;
                }
                else
                {
                    // Create new struct buffer and set local pointer to point to the buffer
                    assert( pStruct->dwSize > 0 );
                    VOID* pBuffer = AllocateStructMemory( pStruct->dwSize );
                    StructInitialize( (CHAR*)pBuffer, pStruct );
                    *(VOID**)pDestination = pBuffer;
                    Push( pTemplate->strMemberName, pStruct, (CHAR*)pBuffer, TRUE );
                    return;
                }
                break;
            }
        case DT_String:
            {
                VOID* pBuffer = AllocateStructMemory( DataFile::GetMemberSize( pTemplate ) );
                Push( NULL, NULL, (CHAR*)pBuffer, TRUE );
                ParseString( pTemplate, Value, 0 );
                Pop();
                *(VOID**)pDestination = pBuffer;
                break;
            }
        case DT_WString:
        case DT_StringID:
            {
                VOID* pBuffer = AllocateStructMemory( DataFile::GetMemberSize( pTemplate ) );
                Push( NULL, NULL, (CHAR*)pBuffer, TRUE );
                ParseWideString( pTemplate, Value, 0 );
                Pop();
                *(VOID**)pDestination = pBuffer;
                break;
            }
        case DT_Buffer:
            {
                VOID* pBuffer = AllocateStructMemory( DataFile::GetMemberSize( pTemplate ) );
                Push( NULL, NULL, (CHAR*)pBuffer, TRUE );
                ParseBuffer( pTemplate, Value, 0 );
                Pop();
                *(VOID**)pDestination = pBuffer;
                break;
            }
        default:
            {
                VOID* pBuffer = AllocateStructMemory( DataFile::GetMemberSize( pTemplate ) );
                Push( NULL, NULL, (CHAR*)pBuffer, TRUE );
                ParseValue( pTemplate, Value, 0 );
                Pop();
                *(VOID**)pDestination = pBuffer;
                break;
            }
        }
    }
    // Vector of pointers indirection - we parse into a new allocation, and add it to a vector
    else if( pTemplate->Indirection == DI_STL_PointerVector )
    {
        assert( pTemplate->dwArraySize == 1 );
        VoidPtrVector& Destination = *(VoidPtrVector*)pDestination;
        switch( pTemplate->Type )
        {
        case DT_Struct:
            {
                const DataStructTemplate* pStruct = pTemplate->pStructTemplate;
                if( pStruct->Location == SL_File )
                {
                    // Load from a new file
                    if (Value.is_array())
                    {
                        for (UINT32 i = 0; i < Value.size(); ++i)
                        {
                            const std::string& s = Value[i];
                            strcpy_s(strStructName, s.c_str());
                            VOID* pData = DataFile::LoadStructFromFile(pStruct, strStructName, NULL);
                            if (pData != nullptr)
                            {
                                Destination.push_back(pData);
                            }
                        }
                    }
                    else
                    { 
                        const std::string& s = Value;
                        strcpy_s(strStructName, s.c_str());
                        VOID* pData = DataFile::LoadStructFromFile(pStruct, strStructName, NULL);
                        if (pData != nullptr)
                        {
                            Destination.push_back(pData);
                        }
                    }
                    return;
                }
                else
                {
                    // Create new struct buffer and set local pointer to point to the buffer
                    assert( pStruct->dwSize > 0 );
                    VOID* pBuffer = AllocateStructMemory( pStruct->dwSize );
                    StructInitialize( (CHAR*)pBuffer, pStruct );
                    Destination.push_back( pBuffer );
                    Push( pTemplate->strMemberName, pStruct, (CHAR*)pBuffer, TRUE );
                    return;
                }
                break;
            }
        case DT_String:
            {
                CHAR* strString = NULL;
                if (Value.is_array())
                {
                    for (UINT32 i = 0; i < Value.size(); ++i)
                    {
                        Push( NULL, NULL, (CHAR*)&strString, TRUE );
                        ParseString( pTemplate, Value[i], 0 );
                        Pop();
                        Destination.push_back( strString );
                    }
                }
                else
                {
                    Push( NULL, NULL, (CHAR*)&strString, TRUE );
                    ParseString( pTemplate, Value, 0 );
                    Pop();
                    Destination.push_back( strString );
                }
                break;
            }
        case DT_WString:
        case DT_StringID:
            {
                WCHAR* strString = NULL;
                Push( NULL, NULL, (CHAR*)&strString, TRUE );
                ParseWideString( pTemplate, Value, 0 );
                Pop();
                Destination.push_back( strString );
                break;
            }
        case DT_Buffer:
            {
                VOID* pBuffer = AllocateStructMemory( DataFile::GetMemberSize( pTemplate ) );
                Push( NULL, NULL, (CHAR*)pBuffer, TRUE );
                ParseBuffer( pTemplate, Value, 0 );
                Pop();
                Destination.push_back( pBuffer );
                break;
            }
        default:
            {
                VOID* pBuffer = AllocateStructMemory( DataFile::GetMemberSize( pTemplate ) );
                Push( NULL, NULL, (CHAR*)pBuffer, TRUE );
                ParseValue( pTemplate, Value, 0 );
                Pop();
                Destination.push_back( pBuffer );
                break;
            }
        }
    }
    // List of pointers indirection - we parse into a new allocation, and add it to a list
    else if( pTemplate->Indirection == DI_STL_PointerList )
    {
        assert( pTemplate->dwArraySize == 1 );
        VoidPtrList& Destination = *(VoidPtrList*)pDestination;
        switch( pTemplate->Type )
        {
        case DT_Struct:
            {
                const DataStructTemplate* pStruct = pTemplate->pStructTemplate;
                if( pStruct->Location == SL_File )
                {
                    // Load from a new file
                    const std::string& s = Value;
                    strcpy_s( strStructName, s.c_str());
                    VOID* pData = DataFile::LoadStructFromFile( pStruct, strStructName, NULL );
                    Destination.push_back( pData );
                    return;
                }
                else
                {
                    // Create new struct buffer and set local pointer to point to the buffer
                    assert( pStruct->dwSize > 0 );
                    VOID* pBuffer = AllocateStructMemory( pStruct->dwSize );
                    StructInitialize( (CHAR*)pBuffer, pStruct );
                    Destination.push_back( pBuffer );
                    Push( pTemplate->strMemberName, pStruct, (CHAR*)pBuffer, TRUE );
                    return;
                }
                break;
            }
        case DT_String:
            {
                CHAR* strString = NULL;
                Push( NULL, NULL, (CHAR*)&strString, TRUE );
                ParseString( pTemplate, Value, 0 );
                Pop();
                Destination.push_back( strString );
                break;
            }
        case DT_WString:
        case DT_StringID:
            {
                WCHAR* strString = NULL;
                Push( NULL, NULL, (CHAR*)&strString, TRUE );
                ParseWideString( pTemplate, Value, 0 );
                Pop();
                Destination.push_back( strString );
                break;
            }
        case DT_Buffer:
            {
                VOID* pBuffer = AllocateStructMemory( DataFile::GetMemberSize( pTemplate ) );
                Push( NULL, NULL, (CHAR*)pBuffer, TRUE );
                ParseBuffer( pTemplate, Value, 0 );
                Pop();
                Destination.push_back( pBuffer );
                break;
            }
        default:
            {
                VOID* pBuffer = AllocateStructMemory( DataFile::GetMemberSize( pTemplate ) );
                Push( NULL, NULL, (CHAR*)pBuffer, TRUE );
                ParseValue( pTemplate, Value, 0 );
                Pop();
                Destination.push_back( pBuffer );
                break;
            }
        }
    }
    // Growable array indirection - we parse into a new element in a growable array
    else if( pTemplate->Indirection == DI_GrowableArray )
    {
        assert( pTemplate->dwArraySize == 1 );
        GrowableArrayBase& ArrayBase = *(GrowableArrayBase*)pDestination;
        switch( pTemplate->Type )
        {
        case DT_Struct:
            {
                const DataStructTemplate* pStruct = pTemplate->pStructTemplate;
                if( pStruct->Location == SL_File )
                {
                    // Load from a new file
                    const std::string& s = Value;
                    strcpy_s( strStructName, s.c_str());
                    CHAR* pNewElement = ArrayBase.AddEmpty();
                    StructInitialize( pNewElement, pStruct );
                    VOID* pData = DataFile::LoadStructFromFile( pStruct, strStructName, pNewElement );
                    return;
                }
                else
                {
                    // Create new struct buffer and set local pointer to point to the buffer
                    assert( pStruct->dwSize > 0 );
                    CHAR* pNewElement = ArrayBase.AddEmpty();
                    StructInitialize( pNewElement, pStruct );
                    Push( pTemplate->strMemberName, pStruct, pNewElement, TRUE );
                    return;
                }
                break;
            }
        case DT_Bool:
        case DT_Int32:
        case DT_UInt32:
        case DT_UInt16:
        case DT_Int16:
        case DT_UInt8:
        case DT_Int8:
        case DT_Float:
        case DT_Double:
            {
                ParseValueIntoGrowableArray( pTemplate, Value, ArrayBase );
                break;
            }
        case DT_String:
            {
                ParseStringIntoGrowableArray( pTemplate, Value, ArrayBase );
                break;
            }
        case DT_WString:
        case DT_StringID:
            {
                ParseWideStringIntoGrowableArray( pTemplate, Value, ArrayBase );
                break;
            }
        case DT_Buffer:
            {
                ParseBufferIntoGrowableArray( pTemplate, Value, ArrayBase );
                break;
            }
        default:
            ERROR( "Invalid type associated with GrowableArray indirection." );
            break;
        }
    }
}

VOID DataFileParser::ParseValue( const DataMemberTemplate* pTemplate, const json& Value, INT OffsetOverride )
{
    // Compute our destination
    if( OffsetOverride < 0 )
    {
        OffsetOverride = (INT)pTemplate->dwOffsetInStruct;
    }
    VOID* pDest = (VOID*)( GetCurrentBuffer() + OffsetOverride );

    // Fast path for a scalar value
    if( pTemplate->dwArraySize <= 1 )
    {
        ParseScalarValue( pTemplate->Type, Value, pDest, pTemplate->pEnums );
        return;
    }

    // Tokenize the value string and parse each element as a scalar
    const UINT ValueArraySize = Value.size();
    const UINT TemplateArraySize = pTemplate->dwArraySize;
    const UINT ArraySize = std::min( ValueArraySize, TemplateArraySize );
    for( UINT i = 0; i < ArraySize; ++i )
    {
        pDest = ParseScalarValue( pTemplate->Type, Value[i], pDest, pTemplate->pEnums );
    }
}

VOID DataFileParser::ParseString( const DataMemberTemplate* pTemplate, const json& Value, INT OffsetOverride )
{
    // Strings can only be scalar
    assert( pTemplate->dwArraySize == 1 );

    // Compute destination
    if( OffsetOverride < 0 )
    {
        OffsetOverride = (INT)pTemplate->dwOffsetInStruct;
    }
    VOID* pDest = (VOID*)( GetCurrentBuffer() + OffsetOverride );

    // Convert string into an ansi string
    const std::string& s = Value;
    const CHAR* strSrc = s.c_str();
    const UINT ValueLen = (UINT)strlen( strSrc );
    CHAR* pBuffer = (CHAR*)AllocateStructMemory( ValueLen + 1 );
    strcpy_s( pBuffer, ValueLen + 1, strSrc );
    pBuffer[ValueLen] = '\0';
    *(CHAR**)pDest = pBuffer;
}

VOID DataFileParser::ParseWideString( const DataMemberTemplate* pTemplate, const json& Value, INT OffsetOverride )
{
    // Wide strings can only be scalar
    assert( pTemplate->dwArraySize == 1 );

    // Compute destination
    if( OffsetOverride < 0 )
    {
        OffsetOverride = (INT)pTemplate->dwOffsetInStruct;
    }
    VOID* pDest = (VOID*)( GetCurrentBuffer() + OffsetOverride );

    const std::string& s = Value;
    const CHAR* strValue = s.c_str();
    const UINT ValueLen = (UINT)strlen( strValue );
    if( pTemplate->Type == DT_WString )
    {
        // Standard wide string
        WCHAR* pBuffer = (WCHAR*)AllocateStructMemory( ( ValueLen + 1 ) * sizeof(WCHAR) );
        MultiByteToWideChar( CP_ACP, 0, strValue, ValueLen + 1, pBuffer, ValueLen + 1 );
        pBuffer[ValueLen] = L'\0';
        *(WCHAR**)pDest = pBuffer;
    }
    else if( pTemplate->Type == DT_StringID )
    {
        // Make a temp copy of the string (to null terminate)
        WCHAR strBuffer[256];
        MultiByteToWideChar( CP_ACP, 0, strValue, ValueLen + 1, strBuffer, ARRAYSIZE(strBuffer) );
        strBuffer[ValueLen] = L'\0';

        // Initialize the StringID using its assignment operator
        *(StringID*)pDest = strBuffer;
    }
}

VOID DataFileParser::ParseBuffer( const DataMemberTemplate* pTemplate, const json& FileNameValue, INT OffsetOverride )
{
    // Buffers can only be scalar
    assert( pTemplate->dwArraySize == 1 );

    // Compute destination
    if( OffsetOverride < 0 )
    {
        OffsetOverride = (INT)pTemplate->dwOffsetInStruct;
    }
    Buffer& DestBuffer = *(Buffer*)( GetCurrentBuffer() + OffsetOverride );

    // Build the filename
    const std::string& s = FileNameValue;
    const CHAR* strValueTemp = s.c_str();

    // Load file into buffer
    //HRESULT hr = LoadFileA( strValueTemp, &DestBuffer.pBuffer, (UINT*)&DestBuffer.dwBufferSize );
    HRESULT hr = E_FAIL;
    if( FAILED(hr) )
    {
        MSG_WARNING( "Could not load file \"%s\" specified by buffer data member %s.\n", strValueTemp, pTemplate->strMemberName );
    }
}

VOID DataFileParser::ParseValueIntoGrowableArray( const DataMemberTemplate* pTemplate, const json& Value, GrowableArrayBase& ArrayBase )
{
    // Ensure that the array stride matches the data type size
    assert( ArrayBase.GetStride() == DataFile::GetDataTypeSize( pTemplate ) );

    // Fast path for scalar
    if( pTemplate->dwArraySize <= 1 )
    {
        // Add an empty item
        CHAR* pDest = ArrayBase.AddEmpty();
        ParseScalarValue( pTemplate->Type, Value, (VOID*)pDest, pTemplate->pEnums );
        return;
    }

    // Tokenize and parse array type
    const UINT ValueArraySize = Value.size();
    const UINT TemplateArraySize = pTemplate->dwArraySize;
    const UINT ArraySize = std::min( ValueArraySize, TemplateArraySize );
    for( UINT i = 0; i < ArraySize; ++i )
    {
        // Add an empty item
        CHAR* pDest = ArrayBase.AddEmpty();
        ParseScalarValue( pTemplate->Type, Value, (VOID*)pDest, pTemplate->pEnums );
    }
}

VOID DataFileParser::ParseStringIntoGrowableArray( const DataMemberTemplate* pTemplate, const json& Value, GrowableArrayBase& ArrayBase )
{
    // Buffers can only be scalar
    assert( pTemplate->dwArraySize == 1 );

    // Compute destination from a new array element
    CHAR** ppDest = (CHAR**)ArrayBase.AddEmpty();

    // Alloc and convert string 
    const std::string& s = Value;
    const CHAR* strSrc = s.c_str();
    const UINT ValueLen = (UINT)strlen( strSrc );
    CHAR* pBuffer = (CHAR*)AllocateStructMemory( ValueLen + 1 );
    strcpy_s( pBuffer, ValueLen + 1, strSrc );
    pBuffer[ValueLen] = '\0';
    *ppDest = pBuffer;
}

VOID DataFileParser::ParseWideStringIntoGrowableArray( const DataMemberTemplate* pTemplate, const json& Value, GrowableArrayBase& ArrayBase )
{
    // Strings can only be scalar
    assert( pTemplate->dwArraySize == 1 );

    const std::string& s = Value;
    const CHAR* strValue = s.c_str();
    const UINT ValueLen = (UINT)strlen( strValue );
    if( pTemplate->Type == DT_WString )
    {
        // Standard wide string
        WCHAR* pBuffer = (WCHAR*)AllocateStructMemory( ( ValueLen + 1 ) * sizeof(WCHAR) );
        MultiByteToWideChar( CP_ACP, 0, strValue, ValueLen + 1, pBuffer, ValueLen + 1 );
        pBuffer[ValueLen] = L'\0';

        // New array element
        WCHAR** ppDest = (WCHAR**)ArrayBase.AddEmpty();
        *ppDest = pBuffer;
    }
    else if( pTemplate->Type == DT_StringID )
    {
        // Make a temp copy of the string (to null terminate)
        WCHAR strBuffer[256];
        MultiByteToWideChar( CP_ACP, 0, strValue, ValueLen + 1, strBuffer, ARRAYSIZE(strBuffer) );
        strBuffer[ValueLen] = L'\0';

        // New array element, assign the string ID
        StringID* pDest = (StringID*)ArrayBase.AddEmpty();
        *pDest = strBuffer;
    }
}

VOID DataFileParser::ParseBufferIntoGrowableArray( const DataMemberTemplate* pTemplate, const json& FileNameValue, GrowableArrayBase& ArrayBase )
{
    // Buffers can only be scalar
    assert( pTemplate->dwArraySize == 1 );

    // Compute destination from a new array element
    Buffer& DestBuffer = *(Buffer*)ArrayBase.AddEmpty();

    // Build the filename
    const std::string& s = FileNameValue;
    const CHAR* strValueTemp = s.c_str();

    // Load file into buffer
    //HRESULT hr = LoadFileA( strValueTemp, &DestBuffer.pBuffer, (UINT*)&DestBuffer.dwBufferSize );
    HRESULT hr = E_FAIL;
    if( FAILED(hr) )
    {
        MSG_WARNING( "Could not load file \"%s\" specified by buffer data member %s.\n", strValueTemp, pTemplate->strMemberName );
    }
}


// Search for enum string and return the matching value
VOID ParseEnum( INT* pDestValue, const WCHAR* strValue, const DataMemberEnum* pEnums )
{
    assert( pEnums != NULL );
    assert( strValue != NULL );
    assert( pDestValue != NULL );

    while( pEnums->strText != NULL )
    {
        if( _wcsicmp( strValue, pEnums->strText ) == 0 )
        {
            *pDestValue = pEnums->iValue;
            return;
        }
        ++pEnums;
    }

    // No enum matched, just convert it as an integer
    *pDestValue = _wtoi( strValue );
}


// Parse a variety of scalar value types
VOID* DataFileParser::ParseScalarValue( DataType Type, const json& Value, VOID* pDest, const DataMemberEnum* pEnums )
{
    switch( Type )
    {
    case DT_Int32:
        if( pEnums != NULL )
        {
            WCHAR strValue[128];
            const std::string& s = Value;
            const CHAR* strNarrowValue = s.c_str();
            MultiByteToWideChar( CP_ACP, 0, strNarrowValue, (INT)strlen(strNarrowValue) + 1, strValue, ARRAYSIZE(strValue) );
            ParseEnum( (INT*)pDest, strValue, pEnums );
        }
        else
        {
            *(INT*)pDest = Value;
        }
        return (VOID*)( (INT*)pDest + 1 );
    case DT_UInt32:
        *(UINT*)pDest = (UINT)Value;
        return (VOID*)( (UINT*)pDest + 1 );
    case DT_UInt16:
        *(USHORT*)pDest = (USHORT)Value;
        return (VOID*)( (USHORT*)pDest + 1 );
    case DT_UInt8:
        *(UCHAR*)pDest = (UCHAR)Value;
        return (VOID*)( (UCHAR*)pDest + 1 );
    case DT_Int16:
        *(SHORT*)pDest = (SHORT)Value;
        return (VOID*)( (SHORT*)pDest + 1 );
    case DT_Int8:
        *(CHAR*)pDest = (int)Value;
        return (VOID*)( (CHAR*)pDest + 1 );
    case DT_Float:
        *(FLOAT*)pDest = (FLOAT)Value;
        return (VOID*)( (FLOAT*)pDest + 1 );
    case DT_Double:
        *(DOUBLE*)pDest = (DOUBLE)Value;
        return (VOID*)( (DOUBLE*)pDest + 1 );
    case DT_Bool:
        if (Value.is_boolean())
        {
            *(BOOL*)pDest = (bool)Value;
        }
        else if (Value.is_number_integer())
        {
            *(BOOL*)pDest = ((int)Value) != 0;
        }
        else if (Value.is_number_float())
        {
            *(BOOL*)pDest = ((float)Value) != 0;
        }
        else if (Value.is_string())
        {
            const std::string& s = Value;
            const CHAR* strValue = s.c_str();
            *(BOOL*)pDest = (strValue[0] == 't' || strValue[0] == 'T' || strValue[0] == '1');
        }
        else
        {
            *(BOOL*)pDest = 0;
        }
        return (VOID*)( (BOOL*)pDest + 1 );
    default:
        ERROR( "Invalid type encountered in ParseScalarValue." );
        break;
    }
    return pDest;
}

VOID GrowableArrayBase::Add( const CHAR* pNewElement )
{
    CHAR* pDest = AddEmpty();
    memcpy( pDest, pNewElement, m_dwStride );
}

CHAR* GrowableArrayBase::AddEmpty()
{
    if( m_dwCount >= m_dwCapacity )
    {
        DWORD dwNewCapacity = m_dwCapacity + ( m_dwCapacity >> 1 );
        dwNewCapacity = std::max( dwNewCapacity, ( m_dwCapacity + 1 ) );
        GrowCapacity( dwNewCapacity );
    }
    assert( m_dwCount < m_dwCapacity );
    ++m_dwCount;
    CHAR* pElement = GetElement( m_dwCount - 1 );
    ZeroMemory( pElement, m_dwStride );
    return pElement;
}

VOID GrowableArrayBase::GrowCapacity( DWORD dwNewCapacity )
{
    assert( dwNewCapacity > 0 );
    if( dwNewCapacity <= m_dwCapacity )
        return;

    assert( m_dwStride > 0 );
    DWORD dwNewBufferSize = dwNewCapacity * m_dwStride;
    CHAR* pNewBuf = new CHAR[dwNewBufferSize];
    DWORD dwCurrentBufferSize = m_dwCount * m_dwStride;
    if( dwCurrentBufferSize > 0 && m_pElements != NULL )
    {
        memcpy( pNewBuf, m_pElements, dwCurrentBufferSize );
        delete[] m_pElements;
    }
    m_pElements = pNewBuf;
    m_dwCapacity = dwNewCapacity;
}
