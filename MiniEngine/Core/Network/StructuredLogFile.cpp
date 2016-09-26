#include "pch.h"
#include "StructuredLogFile.h"


StructuredLogFile::StructuredLogFile(void)
    : m_pLineData( nullptr ),
      m_NumColumns( 0 ),
      m_hFile( INVALID_HANDLE_VALUE )
{
}

StructuredLogFile::~StructuredLogFile(void)
{
    Close();
}

HRESULT StructuredLogFile::Open( const WCHAR* strFileName, UINT NumColumns, const LogFileColumn* pColumns )
{
    if( NumColumns == 0 || pColumns == nullptr )
    {
        return E_INVALIDARG;
    }

    m_hFile = CreateFile( strFileName, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, 0, NULL );
    if( m_hFile == INVALID_HANDLE_VALUE )
    {
        return E_FAIL;
    }

    m_NumColumns = NumColumns;

    WriteHeaderLine( NumColumns, pColumns );
    AllocateLineData();

    for( UINT i = 0; i < m_NumColumns; ++i )
    {
        m_ColumnTypes.push_back( pColumns[i].Type );
        m_ColumnEnums.push_back( pColumns[i].strEnums );
    }

    return S_OK;
}

VOID StructuredLogFile::WriteHeaderLine( UINT NumColumns, const LogFileColumn* pColumns )
{
    assert( m_hFile != INVALID_HANDLE_VALUE );

    CHAR strLine[1024] = "";
    const CHAR* strComma = "";

    for( UINT i = 0; i < NumColumns; ++i )
    {
        strcat_s( strLine, strComma );
        strComma = " ,";

        if( pColumns[i].strName != nullptr )
        {
            strcat_s( strLine, pColumns[i].strName );
        }
    }

    strcat_s( strLine, "\n" );

    DWORD BytesWritten = 0;
    WriteFile( m_hFile, strLine, (DWORD)strlen(strLine), &BytesWritten, NULL );
}

VOID StructuredLogFile::AllocateLineData()
{
    assert( m_NumColumns > 0 );

    m_pLineData = new UINT64[m_NumColumns];
    ZeroMemory( m_pLineData, sizeof(UINT64) * m_NumColumns );
}

HRESULT StructuredLogFile::SetUInt32Data( UINT StartColumnIndex, UINT NumColumns, const UINT32* pData )
{
    for( UINT i = 0; i < NumColumns; ++i )
    {
        UINT Index = StartColumnIndex + i;
        if( Index >= m_NumColumns )
        {
            return E_INVALIDARG;
        }

        *(UINT32*)&m_pLineData[Index] = pData[i];
    }

    return S_OK;
}

HRESULT StructuredLogFile::SetUInt64Data( UINT StartColumnIndex, UINT NumColumns, const UINT64* pData )
{
    for( UINT i = 0; i < NumColumns; ++i )
    {
        UINT Index = StartColumnIndex + i;
        if( Index >= m_NumColumns )
        {
            return E_INVALIDARG;
        }

        m_pLineData[Index] = pData[i];
    }

    return S_OK;
}

HRESULT StructuredLogFile::SetFloatData( UINT StartColumnIndex, UINT NumColumns, const FLOAT* pData )
{
    for( UINT i = 0; i < NumColumns; ++i )
    {
        UINT Index = StartColumnIndex + i;
        if( Index >= m_NumColumns )
        {
            return E_INVALIDARG;
        }

        *(FLOAT*)&m_pLineData[Index] = pData[i];
    }

    return S_OK;
}

HRESULT StructuredLogFile::FlushLine()
{
    if( m_hFile == INVALID_HANDLE_VALUE )
    {
        return E_FAIL;
    }

    CHAR strLine[1024] = "";
    CHAR strElement[32];
    const CHAR* strComma = "";

    for( UINT i = 0; i < m_NumColumns; ++i )
    {
        strcat_s( strLine, strComma );
        strComma = " ,";

        switch( m_ColumnTypes[i] )
        {
        case LogFileColumnType::UInt32:
            sprintf_s( strElement, "%u", *(UINT32*)&m_pLineData[i] );
            break;
        case LogFileColumnType::UInt64:
            sprintf_s( strElement, "%I64u", m_pLineData[i] );
            break;
        case LogFileColumnType::Float:
            sprintf_s( strElement, "%f", *(FLOAT*)&m_pLineData[i] );
            break;
        case LogFileColumnType::Enum:
            {
                UINT32 Index = *(UINT32*)&m_pLineData[i];
                const CHAR** strEnums = m_ColumnEnums[i];
                assert( strEnums != nullptr );
                strcpy_s( strElement, strEnums[Index] );
                break;
            }
        }

        strcat_s( strLine, strElement );
    }

    strcat_s( strLine, "\n" );

    DWORD BytesWritten = 0;
    WriteFile( m_hFile, strLine, (DWORD)strlen(strLine), &BytesWritten, NULL );
    if( BytesWritten == 0 )
    {
        return E_FAIL;
    }

    ZeroMemory( m_pLineData, sizeof(UINT64) * m_NumColumns );

    return S_OK;
}

HRESULT StructuredLogFile::Close()
{
    if( m_hFile != INVALID_HANDLE_VALUE )
    {
        CloseHandle( m_hFile );
        m_hFile = INVALID_HANDLE_VALUE;

        delete[] m_pLineData;
        m_pLineData = nullptr;

        m_NumColumns = 0;
        m_ColumnTypes.clear();
    }

    return S_OK;
}

TimestampedLogFile::TimestampedLogFile()
    : m_hFile( INVALID_HANDLE_VALUE )
{
    QueryPerformanceCounter( &m_StartTime );
    QueryPerformanceFrequency( &m_PerfFreq );
}

TimestampedLogFile::~TimestampedLogFile()
{
    Close();
}

HRESULT TimestampedLogFile::Open( const WCHAR* strFileName, INT64 StartTime )
{
    Close();

    if( StartTime != 0 )
    {
        m_StartTime.QuadPart = StartTime;
    }

    m_hFile = CreateFile( strFileName, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, 0, NULL );
    if( m_hFile == INVALID_HANDLE_VALUE )
    {
        return E_FAIL;
    }

    WriteOpeningLine();

    return S_OK;
}

HRESULT TimestampedLogFile::Close()
{
    if( m_hFile != INVALID_HANDLE_VALUE )
    {
        WriteClosingLine();
        CloseHandle( m_hFile );
        m_hFile = INVALID_HANDLE_VALUE;
    }
    return S_OK;
}

VOID TimestampedLogFile::WriteOpeningLine()
{
    SYSTEMTIME Time;
    GetLocalTime( &Time );
    WriteLine( 0, "Opened log file date %04u-%02u-%02u time %02u:%02u:%02u started at tick %I64d frequency %I64d\n",
        Time.wYear, Time.wMonth, Time.wDay,
        Time.wHour, Time.wMinute, Time.wSecond,
        m_StartTime.QuadPart, m_PerfFreq.QuadPart );
}

VOID TimestampedLogFile::WriteClosingLine()
{
    SYSTEMTIME Time;
    GetLocalTime( &Time );
    WriteLine( 0, "Closed log file date %04u-%02u-%02u time %02u:%02u:%02u\n",
        Time.wYear, Time.wMonth, Time.wDay,
        Time.wHour, Time.wMinute, Time.wSecond );
}
