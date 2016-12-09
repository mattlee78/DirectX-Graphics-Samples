// DedicatedServer.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"

class PrintfDebugListener : public INetDebugListener
{
public:
    void OutputString(BOOL Label, const CHAR* strLine) override final
    {
        puts(strLine);
    }
};

PrintfDebugListener g_DebugListener;
GameNetServer g_Server;

void InitializeEngine()
{
    StringID::Initialize();
    SystemTime::Initialize();
    DataFile::SetDataFileRootPath("Data");
}

int main()
{
    UINT32 ConnectToPort = 31338;

    g_Server.AddDebugListener(&g_DebugListener);

    InitializeEngine();

    g_Server.Start(15, ConnectToPort, false);

    while (!g_Server.IsStarted())
    {
        Sleep(0);
    }

    while (g_Server.IsStarted())
    {
        g_Server.SingleThreadedTick();
    }

    return 0;
}

