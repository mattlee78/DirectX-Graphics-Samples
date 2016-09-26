#pragma once

#include <windows.h>
#include <vector>
#include "DebugPrint.h"

enum class LogFileColumnType
{
    UInt32,
    UInt64,
    Float,
    Enum
};

struct LogFileColumn
{
    const CHAR* strName;
    LogFileColumnType Type;
    const CHAR** strEnums;
};

class StructuredLogFile
{
private:
    UINT m_NumColumns;
    std::vector<LogFileColumnType> m_ColumnTypes;
    std::vector<const CHAR**> m_ColumnEnums;
    UINT64* m_pLineData;

    HANDLE m_hFile;

public:
    StructuredLogFile();
    ~StructuredLogFile();

    BOOL IsOpen() const { return m_hFile != INVALID_HANDLE_VALUE; }

    HRESULT Open( const WCHAR* strFileName, UINT NumColumns, const LogFileColumn* pColumns );

    HRESULT SetUInt32Data( UINT StartColumnIndex, UINT NumColumns, const UINT32* pData );
    HRESULT SetUInt64Data( UINT StartColumnIndex, UINT NumColumns, const UINT64* pData );
    HRESULT SetFloatData( UINT StartColumnIndex, UINT NumColumns, const FLOAT* pData );

    HRESULT FlushLine();

    HRESULT Close();

private:
    VOID WriteHeaderLine( UINT NumColumns, const LogFileColumn* pColumns );
    VOID AllocateLineData();
};

class TimestampedLogFile : public INetDebugListener
{
private:
    LARGE_INTEGER m_StartTime;
    LARGE_INTEGER m_PerfFreq;
    HANDLE m_hFile;

public:
    TimestampedLogFile();
    ~TimestampedLogFile();

    BOOL IsOpen() const { return m_hFile != INVALID_HANDLE_VALUE; }

    HRESULT Open( const WCHAR* strFileName, INT64 StartTime = 0 );
    HRESULT Close();

    VOID WriteLine( INT64 Timestamp, const CHAR* strFormat, ... )
    {
        if( !IsOpen() ) return;

        if( Timestamp == 0 )
        {
            LARGE_INTEGER CurrentTime;
            QueryPerformanceCounter( &CurrentTime );
            Timestamp = CurrentTime.QuadPart;
        }

        DOUBLE TimeSeconds = (DOUBLE)( Timestamp - m_StartTime.QuadPart ) / (DOUBLE)m_PerfFreq.QuadPart;

        CHAR strLine[1024];
        sprintf_s( strLine, "%8.3f [%10I64d]:", TimeSeconds, Timestamp );
        INT Chars = (INT)strlen( strLine );
        INT CharsRemaining = ARRAYSIZE(strLine) - Chars;
        CHAR* strMessage = strLine + Chars;

        va_list args;
        va_start( args, strFormat );

        vsprintf_s( strMessage, CharsRemaining, strFormat, args );

        va_end( args );

        Chars = (INT)strlen( strLine );

        if( strLine[Chars - 1] == '\n' && Chars < ARRAYSIZE(strLine) )
        {
            strLine[Chars - 1] = '\r';
            strLine[Chars] = '\n';
            ++Chars;
        }

        DWORD BytesWritten = 0;
        WriteFile( m_hFile, strLine, Chars, &BytesWritten, NULL );
    }

    virtual VOID OutputString( BOOL Label, const CHAR* strLine )
    {
        if( !Label )
        {
            WriteLine( 0, strLine );
        }
    }

private:
    VOID WriteOpeningLine();
    VOID WriteClosingLine();
};

