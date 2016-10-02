#include "pch.h"
#include "StringID.h"

const WCHAR*             StringID::s_EmptyString = L"";

CRITICAL_SECTION g_StringIDCritSec;

VOID StringID::Initialize()
{
    InitializeCriticalSection( &g_StringIDCritSec );
}

VOID StringID::Terminate()
{
    DeleteCriticalSection( &g_StringIDCritSec );
}

//-----------------------------------------------------------------------------
// Name: StringID::GetStringTable
// Desc: returns static string table data- used to ensure initialization is done
//-----------------------------------------------------------------------------
std::list<const WCHAR *>* StringID::GetStringTable() 
{
    static std::list<const WCHAR *> s_StringLists[ StringID_HASHSIZE ];  
    return s_StringLists;
}

//-----------------------------------------------------------------------------
// Name: StringID::operator==
// Desc: compare a string with a WCHAR 
//-----------------------------------------------------------------------------
BOOL StringID::operator== ( const WCHAR* strRHS ) const
{
    if( strRHS == NULL )
    {
        if( m_strString == s_EmptyString )
            return TRUE;
        return FALSE;
    }

    if( m_strString == strRHS )
        return TRUE;

    return ( wcscmp( m_strString, strRHS ) == 0 );
}

const WCHAR* StringID::AddStringAnsi( const CHAR* strString, INT StringLength )
{
    if( strString == nullptr )
    {
        return AddString( nullptr );
    }
    BOOL NullTerminate = TRUE;
    if( StringLength < 0 )
    {
        StringLength = (INT)strlen( strString ) + 1;
        NullTerminate = FALSE;
    }
    WCHAR strUnicode[512];
    MultiByteToWideChar( CP_ACP, 0, strString, StringLength, strUnicode, ARRAYSIZE( strUnicode ) );
    if( NullTerminate )
    {
        strUnicode[StringLength] = L'\0';
    }
    return AddString( strUnicode );
}

//-----------------------------------------------------------------------------
// Name: StringID::AddString 
// Desc: Add a string to the string table
//-----------------------------------------------------------------------------
const WCHAR* StringID::AddString( const WCHAR* strString )
{
    if( strString == NULL )
        return NULL;
    if( strString[0] == NULL )
        return s_EmptyString;

    EnterCriticalSection( &g_StringIDCritSec );

    int uBucketIndex = HashString( strString ) % StringID_HASHSIZE;
    std::list<const WCHAR*>& CurrentList = GetStringTable()[ uBucketIndex ];

    std::list<const WCHAR*>::iterator iter = CurrentList.begin();
    std::list<const WCHAR*>::iterator end = CurrentList.end();

    while( iter != end )
    {
        const WCHAR* strTest = *iter;
        if( wcscmp( strTest, strString ) == 0 )
        {
            LeaveCriticalSection( &g_StringIDCritSec );
            return strTest;
        }
        ++iter;
    }

    // $OPTIMIZE: use a fixed size allocator here
    DWORD bufferLength = (DWORD)wcslen( strString ) + 1;
    WCHAR* strCopy = new WCHAR[ bufferLength ];
    wcscpy_s( strCopy, bufferLength, strString );
    CurrentList.push_back( strCopy );

    LeaveCriticalSection( &g_StringIDCritSec );

    return strCopy;
}


//-----------------------------------------------------------------------------
// Name: StringID::HashString
// Desc: Create a hash value from a string
//-----------------------------------------------------------------------------
DWORD StringID::HashString( const WCHAR* strString )
{
    DWORD HashVal = 0;        
    const WCHAR *pChar;

    for ( pChar = strString; *pChar; pChar++ )
    {
        HashVal += *pChar * 193951;
        HashVal *= 399283;
    }
    return HashVal;
}

