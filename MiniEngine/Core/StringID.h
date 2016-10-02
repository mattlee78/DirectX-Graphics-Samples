#pragma once

#include <windows.h>
#include <list>

//-----------------------------------------------------------------------------
// Name: StringID
// Desc: Memory management for strings- strings will be inserted into a hash
//       table uniquely, and can be referenced by pointer.  If you want to 
//       insert a string case-insensitively, use SetCaseInsensitive
//-----------------------------------------------------------------------------    

// This is the number of lists in the string hashtable - should be prime
const int StringID_HASHSIZE = 61;

class StringID
{
public:
    static VOID Initialize();
    static VOID Terminate();

    // constructors
    StringID()                          { m_strString = s_EmptyString; }    
    StringID( const WCHAR* strString )  { m_strString = AddString( strString ); }
    StringID( const StringID& other )   { m_strString = other.m_strString; }

    // Assignment
    StringID& operator= ( const StringID& RHS ) { m_strString = RHS.m_strString; return *this; }    
    StringID& operator= ( const WCHAR* strRHS ) { m_strString = AddString( strRHS ); return *this; }

    // Comparison
    BOOL operator== ( const StringID& RHS ) const { return m_strString == RHS.m_strString; }    
    BOOL operator== ( const WCHAR* strRHS ) const;
    BOOL IsEmptyString() const { return m_strString == s_EmptyString; }
    BOOL IsNull() const { return m_strString == nullptr; }

    // Casting
    operator const WCHAR* () const { return m_strString; }
    const WCHAR* GetSafeString() const { return ( m_strString ? m_strString : L"null" ); }

    // ANSI support
    VOID SetAnsi( const CHAR* strAnsi, INT StringLength = -1 ) { m_strString = AddStringAnsi( strAnsi, StringLength ); }

    // Hash lookup function
    static DWORD        HashString( const WCHAR* strString );    

protected:
    static const WCHAR* AddStringAnsi( const CHAR* strString, INT StringLength = -1 );
    static const WCHAR* AddString( const WCHAR* strString );
    static std::list<const WCHAR *>* GetStringTable();

protected:
    const WCHAR*                    m_strString;               
    static const WCHAR*             s_EmptyString;
};

