#pragma once

#include <windows.h>
#include <assert.h>

class RefCountBase
{
private:
    volatile LONG m_RefCount;

public:
    RefCountBase()
        : m_RefCount(1)
    {
    }

    virtual ~RefCountBase()
    {
        assert( m_RefCount == 0 );
    }

    LONG AddRef()
    {
        assert( m_RefCount > 0 );
        return InterlockedIncrement( &m_RefCount );
    }

    LONG Release()
    {
        const LONG NewCount = InterlockedDecrement( &m_RefCount );
        if (NewCount == 0)
        {
            FinalRelease();
            delete this;
        }
        return NewCount;
    }

protected:
    virtual void FinalRelease() = 0;
};

