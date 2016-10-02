#pragma once

#include <windows.h>
#include <vector>

interface INetDebugListener
{
public:
    virtual VOID OutputString( BOOL Label, const CHAR* strLine ) = 0;
};

class DebuggerNetListener : public INetDebugListener
{
public:
    VOID OutputString( BOOL Label, const CHAR* strLine ) override final
    {
        OutputDebugStringA( strLine );
    }
};
extern DebuggerNetListener g_DebuggerNetListener;

class DebugPrintContext
{
private:
    std::vector<INetDebugListener*> m_DebugListeners;
    CHAR m_strName[16];

public:
    DebugPrintContext()
    {
        m_strName[0] = '\0';
#ifdef _DEBUG
        AddDebugListener( &g_DebuggerNetListener );
#endif
    }

    VOID AddDebugListener( INetDebugListener* pListener ) { m_DebugListeners.push_back( pListener ); }

    VOID SetName( const CHAR* strFormat, ... );
    VOID DbgPrint( const CHAR* strFormat, ... );

protected:
    VOID DbgFormat( const CHAR* strFormat, va_list args );
    VOID DbgOut( BOOL Label, const CHAR* strText );
};

VOID DebugPrint( const CHAR* strFormat, ... );