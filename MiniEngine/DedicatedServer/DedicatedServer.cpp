// DedicatedServer.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include "TessTerrain.h"

class PrintfDebugListener : public INetDebugListener
{
public:
    void OutputString(BOOL Label, const CHAR* strLine) override final
    {
        printf(strLine);
    }
};

PrintfDebugListener g_DebugListener;
GameNetServer g_Server;
TessellatedTerrain m_TessTerrain;

void InitializeEngine()
{
    StringID::Initialize();
    Graphics::Initialize();
    SystemTime::Initialize();
    DataFile::SetDataFileRootPath("Data");
    m_TessTerrain.Initialize();
}

void TerminateEngine()
{
    m_TessTerrain.Terminate();
    Graphics::Terminate();
    Graphics::Shutdown();
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

    g_Server.GetWorld()->InitializeTerrain(&m_TessTerrain);

    while (g_Server.IsStarted())
    {
        if (g_Server.SingleThreadedTick())
        {
            GraphicsContext& gfxContext = GraphicsContext::Begin(L"Server Render");
            g_Server.GetWorld()->GetTerrainPhysicsTracker()->ServerRender(&gfxContext);
            gfxContext.Finish();
        }
    }

    TerminateEngine();

    return 0;
}

