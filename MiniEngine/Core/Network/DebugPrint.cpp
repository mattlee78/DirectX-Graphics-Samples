#include "pch.h"
#include <Psapi.h>
#include "DebugPrint.h"

DebuggerNetListener g_DebuggerNetListener;

VOID DebugPrintContext::SetName( const CHAR* strFormat, ... )
{
    ZeroMemory( m_strName, sizeof(m_strName) );

    va_list args;
    va_start( args, strFormat );

    vsprintf_s( m_strName, strFormat, args );

    va_end( args );

    UINT Length = (UINT)strlen( m_strName );
    UINT MaxLength = ARRAYSIZE( m_strName ) - 3;
    while( Length < MaxLength )
    {
        m_strName[Length++] = ' ';
    }
    m_strName[Length] = '\0';

    strcat_s( m_strName, ": " );
}

VOID DebugPrintContext::DbgPrint( const CHAR* strFormat, ... )
{
    DbgOut( TRUE, m_strName );

    va_list args;
    va_start( args, strFormat );

    DbgFormat( strFormat, args );

    va_end( args );
}

VOID DebugPrintContext::DbgOut( BOOL Label, const CHAR* strText )
{
    auto iter = m_DebugListeners.begin();
    auto end = m_DebugListeners.end();
    while( iter != end )
    {
        auto* p = *iter++;
        p->OutputString( Label, strText );
    }
}

VOID DebugPrintContext::DbgFormat( const CHAR* strFormat, va_list args )
{
    CHAR strLine[512];
    vsprintf_s( strLine, strFormat, args );
    DbgOut( FALSE, strLine );
}

VOID DebugPrint( const CHAR* strFormat, ... )
{
    CHAR strLine[1024];

    va_list args;
    va_start( args, strFormat );
    vsprintf_s( strLine, strFormat, args );
    va_end( args );

    OutputDebugStringA( strLine );
}
