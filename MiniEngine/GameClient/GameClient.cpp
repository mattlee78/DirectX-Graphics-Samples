//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
// Developed by Minigraph
//
// Author(s):  Alex Nankervis
//             James Stanard
//

#include "GameCore.h"
#include "GraphicsCore.h"
#include "CameraController.h"
#include "BufferManager.h"
#include "Camera.h"
#include "Model.h"
#include "GpuBuffer.h"
#include "CommandContext.h"
#include "SamplerManager.h"
#include "MotionBlur.h"
#include "DepthOfField.h"
#include "PostEffects.h"
#include "SSAO.h"
#include "FXAA.h"
#include "SystemTime.h"
#include "TextRenderer.h"
#include "ShadowCamera.h"
#include "ParticleEffectManager.h"
#include "GameInput.h"
#include "LineRender.h"
#include "BulletPhysics.h"
#include "ModelInstance.h"
#include "NetworkLayer.h"
#include "DataFile.h"

#include "TessTerrain.h"
#include "InstancedLODModels.h"

#include "CompiledShaders/DepthViewerVS.h"
#include "CompiledShaders/DepthViewerPS.h"
#include "CompiledShaders/GameClientVS.h"
#include "CompiledShaders/GameClientPS.h"

using namespace GameCore;
using namespace Math;
using namespace Graphics;

const XMFLOAT3 g_CylinderCenterPos(100, 360, 0);

class PrintfDebugListener : public INetDebugListener
{
public:
    void OutputString(BOOL Label, const CHAR* strLine) override final
    {
        Utility::Print(strLine);
    }
};

class GameClient : public GameCore::IGameApp, public IClientNotifications
{
public:

	GameClient()
		: m_pCameraController(nullptr),
          m_pInputRemoting(nullptr)
	{
        m_ClientObjectsCreated = false;
        m_FollowCameraEnabled = false;
        const FLOAT Brightness = 5.0f;
        Color SkyColor(0.5f * Brightness, 0.5f * Brightness, 1.0f * Brightness);
        Graphics::g_SceneColorBuffer.SetClearColor(SkyColor);
		m_pLogFile = nullptr;
	}

	virtual void Startup( void ) override;
	virtual void Cleanup( void ) override;

	virtual void Update( float deltaT ) override;
	virtual void RenderScene( void ) override;
    virtual void RenderUI(class GraphicsContext& Context) override;

    virtual bool IsDone(void) override;

private:

    void ProcessCommandLine();
    bool ProcessCommand(const CHAR* strCommand, const CHAR* strArgument);
	void RenderObjects(GraphicsContext& Context, const BaseCamera& Camera, PsoLayoutCache* pPsoCache, RenderPass PassType);

    void RemoteObjectCreated(ModelInstance* pModelInstance, UINT ParentObjectID);
    void RemoteObjectDeleted(ModelInstance* pModelInstance);

	void LogMovementSample(const Vector3& ClientPos, const Vector3& Delta, FLOAT Distance, const Vector3& NetworkPos, INT64 Timestamp, FLOAT LerpValue);

	Camera m_Camera;
	CameraController* m_pCameraController;
    FollowCameraController* m_pFollowCameraController;
    bool m_FollowCameraEnabled;
	Matrix4 m_ViewProjMatrix;
	D3D12_VIEWPORT m_MainViewport;
	D3D12_RECT m_MainScissor;

	RootSignature m_RootSig;
	GraphicsPSO m_DepthPSO;
    PsoLayoutCache m_DepthPSOCache;
	GraphicsPSO m_ModelPSO;
    PsoLayoutCache m_ModelPSOCache;
	GraphicsPSO m_ShadowPSO;
    PsoLayoutCache m_ShadowPSOCache;

	D3D12_CPU_DESCRIPTOR_HANDLE m_ExtraTextures[3];

	Vector3 m_SunDirection;
	ShadowCamera m_SunShadow;
    ShadowCamera m_SunShadowOuter;

    CHAR m_strConnectToServerName[64];
    UINT32 m_ConnectToPort;
    GameNetClient m_NetClient;
    World* m_pClientWorld;
    InputRemotingObject* m_pInputRemoting;

    bool m_ClientObjectsCreated;
    std::set<ModelInstance*> m_ControllableModelInstances;

    bool m_StartServer;
    GameNetServer m_NetServer;
    PrintfDebugListener m_ServerDebugListener;

    Vector3 DebugVector;

    Vector3 m_LastCameraPos;
    Vector3 m_LastTargetPos;
    FLOAT m_LastTargetVelocity;

	FILE* m_pLogFile;
};

CREATE_APPLICATION( GameClient )

ExpVar m_SunLightIntensity("Application/Sun Light Intensity", 4.0f, 0.0f, 16.0f, 0.1f);
ExpVar m_AmbientIntensity("Application/Ambient Intensity", 0.1f, -16.0f, 16.0f, 0.1f);
NumVar m_SunOrientation("Application/Sun Orientation", -0.5f, -100.0f, 100.0f, 0.1f );
NumVar m_SunInclination("Application/Sun Inclination", 0.5f, 0.0f, 1.0f, 0.01f );
NumVar ShadowDimX("Application/Shadow Dim X", 64, 16, 10000, 16 );
NumVar ShadowDimY("Application/Shadow Dim Y", 64, 16, 10000, 16 );
NumVar ShadowDimZ("Application/Shadow Dim Z", 1000, 100, 10000, 100 );
NumVar OuterShadowDimX("Application/Outer Shadow Dim X", 512, 16, 10000, 16);
NumVar OuterShadowDimY("Application/Outer Shadow Dim Y", 512, 16, 10000, 16);
NumVar ShadowCamOffset("Application/Shadow Cam Position Offset", 0.5f, 0, 3.0f, 0.025f);
NumVar OuterShadowCamOffset("Application/Outer Shadow Cam Position Offset", 0.25f, 0, 3.0f, 0.025f);
BoolVar ShadowDebug("Application/Render Shadow Map", false);
BoolVar DisplayPhysicsDebug("Application/Debug Draw Physics", false);
BoolVar DisplayServerPhysicsDebug("Application/Debug Draw Server Physics", false);

struct TestData
{
    XMFLOAT3 Position;
    BOOL IsEnabled;
    FLOAT FloatValue;
};

STRUCT_TEMPLATE_START_FILE(TestData, nullptr, nullptr)
MEMBER_VECTOR3(Position)
MEMBER_BOOL(IsEnabled)
MEMBER_FLOAT(FloatValue)
STRUCT_TEMPLATE_END(TestData)

static const UINT32 g_RingSize = 8;
DecomposedTransform CreateCylinderTransform(UINT32 Index, XMFLOAT3 CenterPos)
{
    const FLOAT CubeSize = 2.0f;
    const FLOAT RingRadius = CubeSize * 2;
    const FLOAT RingTheta = XM_2PI / ((FLOAT)g_RingSize);

    const UINT32 YLevel = Index / g_RingSize;
    FLOAT Theta = RingTheta * (FLOAT)(Index % g_RingSize);
    if (YLevel % 2 == 1)
    {
        Theta += RingTheta * 0.5f;
    }
    const FLOAT Ypos = ((FLOAT)YLevel + 0.5f) * CubeSize + CenterPos.y;
    const FLOAT Xpos = RingRadius * sinf(Theta) + CenterPos.x;
    const FLOAT Zpos = RingRadius * cosf(Theta) + CenterPos.z;

    XMFLOAT4 Orientation;
    XMStoreFloat4(&Orientation, XMQuaternionRotationRollPitchYaw(0, Theta, 0));

    return DecomposedTransform::CreateFromComponents(XMFLOAT3(Xpos, Ypos, Zpos), Orientation);
}

void GameClient::LogMovementSample(const Vector3& ClientPos, const Vector3& Delta, FLOAT Distance, const Vector3& NetworkPos, INT64 Timestamp, FLOAT LerpValue)
{
	if (m_pLogFile != nullptr)
	{
		fprintf_s(
            m_pLogFile, 
            "%0.3f, %0.3f, %0.3f, %0.3f, "
            "%0.3f, %0.3f, %0.3f, "
            "%0.3f, %0.3f, %0.3f, "
            "%I64d, %0.4f\n", 
            Distance, 
            (FLOAT)Delta.GetX(), (FLOAT)Delta.GetY(), (FLOAT)Delta.GetZ(),
            (FLOAT)ClientPos.GetX(), (FLOAT)ClientPos.GetY(), (FLOAT)ClientPos.GetZ(),
            (FLOAT)NetworkPos.GetX(), (FLOAT)NetworkPos.GetY(), (FLOAT)NetworkPos.GetZ(), 
            Timestamp, LerpValue);
	}
}

void GameClient::Startup( void )
{
    m_StartServer = true;
    strcpy_s(m_strConnectToServerName, "localhost");
    m_ConnectToPort = 31338;

    m_LastCameraPos = Vector3(kZero);
    m_LastTargetPos = Vector3(kZero);
    m_LastTargetVelocity = 0;

    ProcessCommandLine();

	//fopen_s(&m_pLogFile, "movement.csv", "w+");

    DataFile::SetDataFileRootPath("Data");
    TestData* pTestData = (TestData*)DataFile::LoadStructFromFile(STRUCT_TEMPLATE_REFERENCE(TestData), "foo");

    World::LoadTerrainConstructionDesc("World1");

	m_RootSig.Reset(6, 2);
	m_RootSig.InitStaticSampler(0, SamplerAnisoWrapDesc, D3D12_SHADER_VISIBILITY_PIXEL);
	m_RootSig.InitStaticSampler(15, SamplerShadowDesc, D3D12_SHADER_VISIBILITY_PIXEL);
	m_RootSig[0].InitAsConstantBuffer(0, D3D12_SHADER_VISIBILITY_VERTEX);
	m_RootSig[1].InitAsConstantBuffer(0, D3D12_SHADER_VISIBILITY_PIXEL);
	m_RootSig[2].InitAsBufferSRV(0, D3D12_SHADER_VISIBILITY_VERTEX);
	m_RootSig[3].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 6, D3D12_SHADER_VISIBILITY_PIXEL);
	m_RootSig[4].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 64, 3, D3D12_SHADER_VISIBILITY_PIXEL);
	m_RootSig[5].InitAsConstants(1, 1, D3D12_SHADER_VISIBILITY_VERTEX);
	m_RootSig.Finalize(L"GameClient", D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	DXGI_FORMAT ColorFormat = g_SceneColorBuffer.GetFormat();
	DXGI_FORMAT DepthFormat = g_SceneDepthBuffer.GetFormat();
	DXGI_FORMAT ShadowFormat = g_ShadowBuffer.GetFormat();
    assert(g_OuterShadowBuffer.GetFormat() == ShadowFormat);

	D3D12_INPUT_ELEMENT_DESC vertElem[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "BITANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
	};

    g_InputLayoutCache.FindOrAddLayout(vertElem, _countof(vertElem));

	m_DepthPSO.SetRootSignature(m_RootSig);
	m_DepthPSO.SetRasterizerState(RasterizerDefault);
	m_DepthPSO.SetBlendState(BlendNoColorWrite);
	m_DepthPSO.SetDepthStencilState(DepthStateReadWrite);
	m_DepthPSO.SetInputLayout(_countof(vertElem), vertElem);
	m_DepthPSO.SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
	m_DepthPSO.SetRenderTargetFormats(0, nullptr, DepthFormat);
	m_DepthPSO.SetVertexShader(g_pDepthViewerVS, sizeof(g_pDepthViewerVS));
	m_DepthPSO.SetPixelShader(g_pDepthViewerPS, sizeof(g_pDepthViewerPS));
	m_DepthPSO.Finalize();
    m_DepthPSOCache.Initialize(&m_DepthPSO);

	m_ShadowPSO = m_DepthPSO;
	m_ShadowPSO.SetRasterizerState(RasterizerShadow);
	m_ShadowPSO.SetRenderTargetFormats(0, nullptr, g_ShadowBuffer.GetFormat());
	m_ShadowPSO.Finalize();
    m_ShadowPSOCache.Initialize(&m_ShadowPSO);

	m_ModelPSO = m_DepthPSO;
	m_ModelPSO.SetBlendState(BlendDisable);
	m_ModelPSO.SetDepthStencilState(DepthStateTestEqual);
	m_ModelPSO.SetRenderTargetFormats(1, &ColorFormat, DepthFormat);
	m_ModelPSO.SetVertexShader( g_pGameClientVS, sizeof(g_pGameClientVS) );
	m_ModelPSO.SetPixelShader( g_pGameClientPS, sizeof(g_pGameClientPS) );
	m_ModelPSO.Finalize();
    m_ModelPSOCache.Initialize(&m_ModelPSO);

	m_ExtraTextures[0] = g_SSAOFullScreen.GetSRV();
	m_ExtraTextures[1] = g_ShadowBuffer.GetSRV();
    m_ExtraTextures[2] = g_OuterShadowBuffer.GetSRV();

	TextureManager::Initialize(L"Textures/");

    m_NetClient.InitializeWorld();
    m_pClientWorld = m_NetClient.GetWorld();

    Vector3 eye(100, 100, -100);
	m_Camera.SetEyeAtUp( eye, Vector3(kZero), Vector3(kYUnitVector) );
	m_Camera.SetZRange( 1.0f, 10000.0f );
	m_pCameraController = new CameraController(m_Camera, Vector3(kYUnitVector));
    m_pFollowCameraController = new FollowCameraController(m_Camera, Vector3(0, 3, -4), 2, 0.5f, 1.5f);

	MotionBlur::Enable = true;
	TemporalAA::Enable = true;
	FXAA::Enable = true;
	PostEffects::EnableHDR = true;
	PostEffects::EnableAdaptation = true;

    m_NetServer.AddDebugListener(&m_ServerDebugListener);
    if (m_StartServer)
    {
        m_NetServer.Start(15, m_ConnectToPort, false);
        strcpy_s(m_strConnectToServerName, "localhost");
    }

    g_ClientPredictConstants.Correction = 0.9f;
    g_ClientPredictConstants.Smoothing = 0.1f;
    g_ClientPredictConstants.Prediction = 0.0f;
    LARGE_INTEGER PerfFreq;
    QueryPerformanceFrequency(&PerfFreq);
    g_ClientPredictConstants.FrameTickLength = PerfFreq.QuadPart / 15;

    if (m_NetServer.IsStarted() || !m_StartServer)
    {
        if (m_ConnectToPort != 0)
        {
            Utility::Printf("Attempting to connect to server %s on port %u...\n", m_strConnectToServerName, m_ConnectToPort);
            m_NetClient.SetNotificationClient(this);
            m_NetClient.Connect(15, m_strConnectToServerName, m_ConnectToPort, L"", L"");
        }
    }

    if (false && m_NetServer.IsStarted())
    {
        DecomposedTransform DT;
        //m_NetServer.SpawnObject(nullptr, "Models/sponza.h3d", nullptr, DT, XMFLOAT3(0, 0, 0));

        const CHAR* strRocks[] =
        {
            "RockLargeA",
            "RockLargeB",
            "RockLargeC",
        };

        Math::RandomNumberGenerator rng;
        const UINT32 RockCount = 30;
        for (UINT32 i = 0; i < RockCount; ++i)
        {
            FLOAT Radius = rng.NextFloat(300, 500);
            FLOAT Angle = XM_2PI * (FLOAT)i / (FLOAT)RockCount;
            XMFLOAT3 Position;
            Position.x = Radius * sinf(Angle);
            Position.y = 0;
            Position.z = Radius * cosf(Angle);
            XMFLOAT4 Orientation;
            XMStoreFloat4(&Orientation, XMQuaternionRotationRollPitchYaw(0, Angle, 0));
            DT = DecomposedTransform::CreateFromComponents(Position, Orientation);

            UINT32 RockIndex = rng.NextInt(0, ARRAYSIZE(strRocks) - 1);
            m_NetServer.SpawnObject(nullptr, strRocks[RockIndex], nullptr, DT, XMFLOAT3(0, 0, 0));
        }

        DT = DecomposedTransform::CreateFromComponents(XMFLOAT3(200, 0, 100));
        m_NetServer.SpawnObject(nullptr, "*plane", nullptr, DT, XMFLOAT3(0, 0, 0));
        m_NetServer.SpawnObject(nullptr, "ramp1", nullptr, DT, XMFLOAT3(0, 0, 0));

        //DT = DecomposedTransform::CreateFromComponents(XMFLOAT3(0, 50, 0));
        //ModelInstance* pMI = (ModelInstance*)m_NetServer.SpawnObject(nullptr, "*cube", nullptr, DT, XMFLOAT3(0, 0, 0));
        //pMI->SetLifetimeRemaining(10);

        DT = DecomposedTransform::CreateFromComponents(XMFLOAT3(0, 9, -100));
        m_NetServer.SpawnObject(nullptr, "*waterbox20:9:20", nullptr, DT, XMFLOAT3(0, 0, 0));

        DT = DecomposedTransform::CreateFromComponents(XMFLOAT3(0, 10, -80));
        m_NetServer.SpawnObject(nullptr, "*staticbox20:10:1", nullptr, DT, XMFLOAT3(0, 0, 0));
        DT = DecomposedTransform::CreateFromComponents(XMFLOAT3(0, 10, -120));
        m_NetServer.SpawnObject(nullptr, "*staticbox20:10:1", nullptr, DT, XMFLOAT3(0, 0, 0));
        DT = DecomposedTransform::CreateFromComponents(XMFLOAT3(20, 10, -100), 0, XM_PIDIV2);
        m_NetServer.SpawnObject(nullptr, "*staticbox20:10:1", nullptr, DT, XMFLOAT3(0, 0, 0));
        DT = DecomposedTransform::CreateFromComponents(XMFLOAT3(-20, 10, -100), 0, -XM_PIDIV2);
        m_NetServer.SpawnObject(nullptr, "*staticbox20:10:1", nullptr, DT, XMFLOAT3(0, 0, 0));

        DT = DecomposedTransform::CreateFromComponents(XMFLOAT3(50, 10, -100), 18.43f * (XM_PI / 180.0f), XM_PIDIV2);
        m_NetServer.SpawnObject(nullptr, "*staticbox10:0.5:31.5", nullptr, DT, XMFLOAT3(0, 0, 0));
        DT = DecomposedTransform::CreateFromComponents(XMFLOAT3(12.5, 17.5, -100), -18.43f * (XM_PI / 180.0f), XM_PIDIV2);
        m_NetServer.SpawnObject(nullptr, "*staticbox20:0.5:7.875", nullptr, DT, XMFLOAT3(0, 0, 0));

//         for (UINT32 i = 0; i < 10; ++i)
//         {
//             FLOAT Pitch = (FLOAT)i * 0.1f;
//             FLOAT Ypos = 25.0f + (FLOAT)i * 4;
//             DT = DecomposedTransform::CreateFromComponents(XMFLOAT3(0, Ypos, -100), Pitch, 0);
//             m_NetServer.SpawnObject(nullptr, "*cube1.5", nullptr, DT, XMFLOAT3(0, 0, 0));
//         }

// 		DT = DecomposedTransform::CreateFromComponents(XMFLOAT3(0, 0, 0), 0, 0);
// 		m_NetServer.SpawnObject(nullptr, "Models\\BeechAmerican_B.bmesh", nullptr, DT, XMFLOAT3(0, 0, 0));
    }

//     InstancedLODModel LMT;
//     LMT.Load("Models\\MapleGreenMountain_A.bmesh");
//     LMT.Unload();

	if (0)
	{
		InstancedLODModel* pLODModel = g_LODModelManager.FindOrLoadModel("Models\\BeechAmerican_B.bmesh");
		if (pLODModel != nullptr)
		{
			const FLOAT InstanceSpacing = 100.0f;
			const FLOAT Ypos = 0;
			const INT GridSize = 50;
			for (INT32 z = -GridSize; z < GridSize; ++z)
			{
				const FLOAT Zpos = (FLOAT)z * InstanceSpacing;
				for (INT32 x = -GridSize; x < GridSize; ++x)
				{
					const FLOAT Xpos = (FLOAT)x * InstanceSpacing;
					MeshPlacementVertex MPV;
					MPV.WorldPosition = XMFLOAT3(Xpos, Ypos, Zpos);
					//XMVECTOR qRotation = XMQuaternionRotationRollPitchYaw(0, (Xpos + Zpos) * 0.1f, 0);
					//XMStoreFloat4(&MPV.Orientation, qRotation);
					MPV.Orientation = XMFLOAT4(0, 0, 0, 1);
					MPV.UniformScale = 1.0f;
					pLODModel->AddDynamicPlacement(MPV);
				}
			}
		}
	}
}

bool GameClient::ProcessCommand(const CHAR* strCommand, const CHAR* strArgument)
{
    bool InvalidArgument = false;
    bool Result = true;

    if (_stricmp(strCommand, "port") == 0)
    {
        if (strArgument != nullptr)
        {
            m_ConnectToPort = (UINT32)atoi(strArgument);
            if (m_ConnectToPort < 1024 || m_ConnectToPort > 32767)
            {
                m_ConnectToPort = 0;
                InvalidArgument = true;
            }
        }
        else
        {
            InvalidArgument = true;
        }
    }
    else if (_stricmp(strCommand, "connect") == 0)
    {
        if (strArgument != nullptr)
        {
            strcpy_s(m_strConnectToServerName, strArgument);
            m_StartServer = false;
        }
    }
    else if (_stricmp(strCommand, "noconnect") == 0)
    {
        m_StartServer = false;
        m_ConnectToPort = 0;
    }
    else
    {
        Utility::Printf("Invalid command: %s\n", strCommand);
        Result = false;
    }

    if (InvalidArgument)
    {
        Utility::Printf("Invalid argument to command %s: %s\n", strCommand, strArgument);
        Result = false;
    }

    return Result;
}

void GameClient::ProcessCommandLine()
{
    CHAR strCmdLine[512];
    strcpy_s(strCmdLine, GetCommandLineA());

    const CHAR* strCommand = nullptr;
    const CHAR* strArgument = nullptr;

    const CHAR* strSeparators = " ";
    CHAR* strToken = nullptr;
    CHAR* strNextToken = nullptr;

    strToken = strtok_s(strCmdLine, strSeparators, &strNextToken);
    while (strToken != nullptr)
    {
        if (strToken[0] == '-' || strToken[0] == '/')
        {
            if (strCommand != nullptr)
            {
                ProcessCommand(strCommand, strArgument);
                strCommand = nullptr;
                strArgument = nullptr;
            }
            strCommand = strToken + 1;
        }
        else if (strArgument == nullptr)
        {
            if (strCommand == nullptr)
            {
                Utility::Printf("Invalid command line option: %s\n", strToken);
            }
            else
            {
                strArgument = strToken;
            }
        }
        else
        {
            Utility::Printf("Invalid command line option: %s\n", strToken);
        }
        strToken = strtok_s(nullptr, strSeparators, &strNextToken);
    }

    if (strCommand != nullptr)
    {
        ProcessCommand(strCommand, strArgument);
    }
}

void GameClient::Cleanup( void )
{
    if (m_NetClient.IsConnected(nullptr))
    {
        m_NetClient.RequestDisconnect();
    }

    if (m_NetServer.IsStarted())
    {
        m_NetServer.Stop();
        m_NetServer.Terminate();
    }

    m_NetClient.Terminate();

	delete m_pCameraController;
	m_pCameraController = nullptr;

    delete m_pFollowCameraController;
    m_pFollowCameraController = nullptr;

	if (m_pLogFile != nullptr)
	{
		fclose(m_pLogFile);
		m_pLogFile = nullptr;
	}
}

void GameClient::RemoteObjectCreated(ModelInstance* pModelInstance, UINT ParentObjectID)
{
    if (ParentObjectID == m_NetClient.GetClientBaseObjectID())
    {
        if (pModelInstance->GetTemplate()->IsPlayerControllable())
        {
            m_ControllableModelInstances.insert(pModelInstance);
            m_pInputRemoting->ClientSetTargetNodeID(pModelInstance->GetNodeID());
            m_FollowCameraEnabled = true;
        }
    }
}

void GameClient::RemoteObjectDeleted(ModelInstance* pModelInstance)
{
    auto iter = m_ControllableModelInstances.find(pModelInstance);
    if (iter != m_ControllableModelInstances.end())
    {
        m_ControllableModelInstances.erase(iter);
        if (m_pInputRemoting->ClientGetTargetNodeID() == pModelInstance->GetNodeID())
        {
            m_pInputRemoting->ClientSetTargetNodeID(0);
        }
    }

    if (m_ClientObjectsCreated && m_ControllableModelInstances.empty())
    {
        m_ClientObjectsCreated = false;
    }
}

namespace Graphics
{
	extern EnumVar DebugZoom;
}

void GameClient::Update( float deltaT )
{
	ScopedTimer _prof(L"Update State");

    m_NetClient.SingleThreadedTick();
    LARGE_INTEGER ClientTicks;
    QueryPerformanceCounter(&ClientTicks);

    if (m_NetClient.IsConnected(nullptr))
    {
        ClientTicks.QuadPart = m_NetClient.GetCurrentTime();

        if (!m_ClientObjectsCreated && m_NetClient.CanSpawnObjects())
        {
            m_ClientObjectsCreated = true;
            DecomposedTransform DT = DecomposedTransform::CreateFromComponents(XMFLOAT3(100, 180, 100));
            m_NetClient.SpawnObjectOnServer("Vehicle2", nullptr, DT, XMFLOAT3(0, 00, 0));
        }

        if (m_pInputRemoting == nullptr)
        {
            m_pInputRemoting = (InputRemotingObject*)m_NetClient.SpawnObjectOnClient(InputRemotingObject::GetTemplateName());
        }
        else
        {
            if (GameInput::IsMouseExclusive() && m_FollowCameraEnabled)
            {
                NetworkInputState InputState = {};
                float forward = (
                    GameInput::GetAnalogInput(GameInput::kAnalogLeftStickY) +
                    (GameInput::IsPressed(GameInput::kKey_w) ? 1.0f : 0.0f) +
                    (GameInput::IsPressed(GameInput::kKey_s) ? -1.0f : 0.0f)
                    );
                float strafe = (
                    GameInput::GetAnalogInput(GameInput::kAnalogLeftStickX) +
                    (GameInput::IsPressed(GameInput::kKey_d) ? 1.0f : 0.0f) +
                    (GameInput::IsPressed(GameInput::kKey_a) ? -1.0f : 0.0f)
                    );
                float ascent = (
                    GameInput::GetTimeCorrectedAnalogInput(GameInput::kAnalogRightTrigger) -
                    GameInput::GetTimeCorrectedAnalogInput(GameInput::kAnalogLeftTrigger) +
                    (GameInput::IsPressed(GameInput::kKey_e) ? 1.0f : 0.0f) +
                    (GameInput::IsPressed(GameInput::kKey_q) ? -1.0f : 0.0f)
                    );
                float lt = (
                    GameInput::GetAnalogInput(GameInput::kAnalogLeftTrigger) +
                    (GameInput::IsPressed(GameInput::kKey_s) ? 1.0f : 0.0f)
                    );
                float rt = (
                    GameInput::GetAnalogInput(GameInput::kAnalogRightTrigger) +
                    (GameInput::IsPressed(GameInput::kKey_w) ? 1.0f : 0.0f)
                    );
                InputState.XAxis0 = strafe;
                InputState.YAxis0 = forward;
                InputState.YAxis1 = ascent;
                InputState.LeftTrigger = lt;
                InputState.RightTrigger = rt;
                InputState.Buttons[1] = GameInput::IsPressed(GameInput::kKey_f);
                m_pInputRemoting->ClientUpdate(InputState);
            }
            else if (0)
            {
                NetworkInputState InputState = {};
                InputState.Buttons[1] = true;
                m_pInputRemoting->ClientUpdate(InputState);
            }
        }
    }

    m_pClientWorld->Tick(deltaT, ClientTicks.QuadPart);

    if (DisplayPhysicsDebug)
    {
        m_pClientWorld->GetPhysicsWorld()->DebugRender();
    }

	if (GameInput::IsFirstPressed(GameInput::kLShoulder))
		DebugZoom.Decrement();
	else if (GameInput::IsFirstPressed(GameInput::kRShoulder))
		DebugZoom.Increment();

    if (GameInput::IsFirstPressed(GameInput::kKey_escape))
    {
        if (GameInput::IsMouseExclusive())
        {
            GameInput::SetMouseExclusive(false);
        }
    }

    if (GameInput::IsFirstPressed(GameInput::kKey_c))
    {
        m_FollowCameraEnabled = !m_FollowCameraEnabled;
    }

    if (GameInput::IsFirstPressed(GameInput::kKey_k))
    {
        if (!m_ControllableModelInstances.empty())
        {
            auto iter = m_ControllableModelInstances.begin();
            ModelInstance* pFirstMI = *iter;
            m_NetClient.DestroyObjectOnServer(pFirstMI->GetNodeID());
        }
    }

    if (m_ControllableModelInstances.empty() || !m_FollowCameraEnabled)
    {
        m_pCameraController->Update(deltaT);
    }
    else
    {
        auto iter = m_ControllableModelInstances.begin();
        ModelInstance* pFirstMI = *iter;
        static Vector3 LastWorldPos(0, 0, 0);
        Vector3 WorldPos = pFirstMI->GetWorldPosition();
        //DebugVector = (WorldPos - LastWorldPos) * 1000.0f;
        LastWorldPos = WorldPos;
        m_pFollowCameraController->Update(pFirstMI->GetWorldTransform(), deltaT, pFirstMI);
    }
	m_ViewProjMatrix = m_Camera.GetViewProjMatrix();

    m_LastCameraPos = m_Camera.GetPosition();
    if (!m_ControllableModelInstances.empty())
    {
        ModelInstance* pFirstMI = *m_ControllableModelInstances.begin();
        Vector3 LastTargetPos = m_LastTargetPos;
        m_LastTargetPos = pFirstMI->GetWorldPosition();
		const Vector3 Delta = m_LastTargetPos - LastTargetPos;
        FLOAT FrameDistance = XMVectorGetX(XMVector3Length(Delta));
		LogMovementSample(m_LastTargetPos, Delta, FrameDistance, pFirstMI->GetRawPosition(), pFirstMI->GetRawPositionTimestamp(), pFirstMI->GetRawPositionLerpValue());
        FLOAT FrameVelocity = FrameDistance / deltaT;
        FLOAT Lerp = std::min(1.0f, deltaT * 3);
        m_LastTargetVelocity = FrameVelocity * Lerp + m_LastTargetVelocity * (1.0f - Lerp);
    }
    else
    {
        m_LastTargetPos = Vector3(kZero);
        m_LastTargetVelocity = 0;
    }

	float costheta = cosf(m_SunOrientation);
	float sintheta = sinf(m_SunOrientation);
	float cosphi = cosf(m_SunInclination * 3.14159f * 0.5f);
	float sinphi = sinf(m_SunInclination * 3.14159f * 0.5f);
	m_SunDirection = Normalize(Vector3( costheta * cosphi, sinphi, sintheta * cosphi ));

	// We use viewport offsets to jitter our color samples from frame to frame (with TAA.)
	// D3D has a design quirk with fractional offsets such that the implicit scissor
	// region of a viewport is floor(TopLeftXY) and floor(TopLeftXY + WidthHeight), so
	// having a negative fractional top left, e.g. (-0.25, -0.25) would also shift the
	// BottomRight corner up by a whole integer.  One solution is to pad your viewport
	// dimensions with an extra pixel.  My solution is to only use positive fractional offsets,
	// but that means that the average sample position is +0.5, which I use when I disable
	// temporal AA.
	if (TemporalAA::Enable && !DepthOfField::Enable)
	{
		uint64_t FrameIndex = Graphics::GetFrameCount();
#if 1
		// 2x super sampling with no feedback
		float SampleOffsets[2][2] =
		{
			{ 0.25f, 0.25f },
			{ 0.75f, 0.75f },
		};
		const float* Offset = SampleOffsets[FrameIndex & 1];
#else
		// 4x super sampling via controlled feedback
		float SampleOffsets[4][2] =
		{
			{ 0.125f, 0.625f },
			{ 0.375f, 0.125f },
			{ 0.875f, 0.375f },
			{ 0.625f, 0.875f }
		};
		const float* Offset = SampleOffsets[FrameIndex & 3];
#endif
		m_MainViewport.TopLeftX = Offset[0];
		m_MainViewport.TopLeftY = Offset[1];
	}
	else
	{
		m_MainViewport.TopLeftX = 0.5f;
		m_MainViewport.TopLeftY = 0.5f;
	}

	m_MainViewport.Width = (float)g_SceneColorBuffer.GetWidth();
	m_MainViewport.Height = (float)g_SceneColorBuffer.GetHeight();
	m_MainViewport.MinDepth = 0.0f;
	m_MainViewport.MaxDepth = 1.0f;

	m_MainScissor.left = 0;
	m_MainScissor.top = 0;
	m_MainScissor.right = (LONG)g_SceneColorBuffer.GetWidth();
	m_MainScissor.bottom = (LONG)g_SceneColorBuffer.GetHeight();

    LineRender::DrawAxis(XMMatrixScalingFromVector(XMVectorReplicate(10)));
    //LineRender::DrawGridXZ(XMMatrixIdentity(), 5000, 100, g_XMOne);

    m_NetServer.SingleThreadedTick();

    if (DisplayServerPhysicsDebug && !DisplayPhysicsDebug)
    {
        m_NetServer.GetWorld()->GetPhysicsWorld()->DebugRender();
    }
}

bool GameClient::IsDone()
{
    return false;
}

void GameClient::RenderObjects(GraphicsContext& gfxContext, const BaseCamera& Camera, PsoLayoutCache* pPsoCache, RenderPass PassType)
{
    ModelRenderContext MRC;
    MRC.pContext = &gfxContext;
    MRC.CameraPosition = Camera.GetPosition();
    MRC.ModelToShadow = m_SunShadow.GetShadowMatrix();
    MRC.ModelToShadowOuter = m_SunShadowOuter.GetShadowMatrix();
    Matrix4 ViewMatrix = Camera.GetViewMatrix();
    ViewMatrix.SetW(Vector4(g_XMIdentityR3));
    Matrix4 VPMatrix = Camera.GetProjMatrix() * ViewMatrix;
    MRC.ViewProjection = VPMatrix;
    MRC.pPsoCache = pPsoCache;
    MRC.LastInputLayoutIndex = -1;
    MRC.CurrentPassType = PassType;

    m_pClientWorld->Render(MRC);

    g_LODModelManager.Render(gfxContext, &MRC);
}

void GameClient::RenderScene( void )
{
    const Vector3 CameraPos = m_Camera.GetPosition();
    
    m_SunShadow.SetSceneCameraPos(CameraPos);
    Vector3 ShadowTargetPos = ((ShadowDimX + ShadowDimY) * ShadowCamOffset * 0.5f) * m_Camera.GetForwardVec() + CameraPos;
    m_SunShadow.UpdateMatrix(-m_SunDirection, ShadowTargetPos - m_SunDirection * (ShadowDimZ * 0.25f), Vector3(ShadowDimX, ShadowDimY, ShadowDimZ),
        (uint32_t)g_ShadowBuffer.GetWidth(), (uint32_t)g_ShadowBuffer.GetHeight(), 16);

    m_SunShadowOuter.SetSceneCameraPos(CameraPos);
    ShadowTargetPos = ((OuterShadowDimX + OuterShadowDimY) * OuterShadowCamOffset * 0.5f) * m_Camera.GetForwardVec() + CameraPos;
    m_SunShadowOuter.UpdateMatrix(-m_SunDirection, ShadowTargetPos - m_SunDirection * (ShadowDimZ * 0.25f), Vector3(OuterShadowDimX, OuterShadowDimY, ShadowDimZ),
        (uint32_t)g_OuterShadowBuffer.GetWidth(), (uint32_t)g_OuterShadowBuffer.GetHeight(), 16);

    GraphicsContext& gfxContext = GraphicsContext::Begin(L"Scene Render");

    m_NetClient.GetWorld()->ServerRender(&gfxContext);
    if (m_NetServer.IsStarted())
    {
        m_NetServer.GetWorld()->ServerRender(&gfxContext);
    }

    CBLightShadowWorldConstants psConstants = {};
    psConstants.sunDirection = m_SunDirection;
    psConstants.sunLight = Vector3(1.0f, 1.0f, 1.0f) * m_SunLightIntensity;
    psConstants.ambientLight = Vector3(1.0f, 1.0f, 1.0f) * m_AmbientIntensity;
    psConstants.ShadowTexelSize = 1.0f / g_ShadowBuffer.GetWidth();

    TessellatedTerrainRenderDesc RD = {};
    XMStoreFloat4x4A(&RD.matView, m_Camera.GetViewMatrix());
    XMStoreFloat4x4A(&RD.matProjection, m_Camera.GetProjMatrix());
    const XMMATRIX matShadow = m_SunShadow.GetShadowMatrix();
    XMStoreFloat4x4A(&RD.matWorldToShadow, matShadow);
    XMStoreFloat4x4A(&RD.matWorldToShadowOuter, m_SunShadowOuter.GetShadowMatrix());
    XMStoreFloat4A(&RD.CameraPosWorld, CameraPos);
    RD.Viewport = m_MainViewport;
    RD.ZPrePass = false;
    RD.pLightShadowConstants = &psConstants;
    RD.pExtraTextures = m_ExtraTextures;

    m_NetClient.GetWorld()->TrackCameraPos(CameraPos);
    m_NetClient.GetWorld()->GetTerrain()->OffscreenRender(&gfxContext, &RD);

	ComputeContext& cContext = gfxContext.GetComputeContext();
	CBInstanceMeshCulling cbIMC = {};
	{
		XMMATRIX matView = m_Camera.GetViewMatrix();
		XMMATRIX matProj = m_Camera.GetProjMatrix();
		XMVECTOR Det;
		XMMATRIX matWorld = XMMatrixInverse(&Det, matView);
		XMStoreFloat4x4(&cbIMC.g_CameraWorldViewProj, matView * matProj);
		XMStoreFloat4(&cbIMC.g_CameraWorldDir, matWorld.r[2]);
		XMStoreFloat4(&cbIMC.g_CameraWorldPos, matWorld.r[3]);
	}
	g_LODModelManager.CullAndSort(cContext, &cbIMC);

	{
		ScopedTimer _prof(L"Z PrePass", gfxContext);

		gfxContext.TransitionResource(g_SceneDepthBuffer, D3D12_RESOURCE_STATE_DEPTH_WRITE, true);
		gfxContext.ClearDepth(g_SceneDepthBuffer);

		gfxContext.SetRootSignature(m_RootSig);
		gfxContext.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		gfxContext.SetDynamicConstantBufferView(1, sizeof(psConstants), &psConstants);

		gfxContext.SetDepthStencilTarget(g_SceneDepthBuffer.GetDSV());
		gfxContext.SetViewportAndScissor(m_MainViewport, m_MainScissor);
		RenderObjects(gfxContext, m_Camera, &m_DepthPSOCache, RenderPass_ZPrePass);

        RD.ZPrePass = true;
        m_NetClient.GetWorld()->GetTerrain()->Render(&gfxContext, &RD);
	}

	SSAO::Render(gfxContext, m_Camera);

	if (!SSAO::DebugDraw)
	{
		ScopedTimer _prof(L"Main Render", gfxContext);

		gfxContext.TransitionResource(g_SceneColorBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, true);
		gfxContext.ClearColor(g_SceneColorBuffer);

		// Set the default state for command lists
		auto& pfnSetupGraphicsState = [&](void)
		{
			gfxContext.SetRootSignature(m_RootSig);
			gfxContext.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			gfxContext.SetDynamicDescriptors(4, 0, 3, m_ExtraTextures);
			gfxContext.SetDynamicConstantBufferView(1, sizeof(psConstants), &psConstants);
		};

		pfnSetupGraphicsState();

		{
			ScopedTimer _prof(L"Render Inner Shadow Map", gfxContext);

			g_ShadowBuffer.BeginRendering(gfxContext);
            RenderObjects(gfxContext, m_SunShadow, &m_ShadowPSOCache, RenderPass_Shadow);
			g_ShadowBuffer.EndRendering(gfxContext);
		}

        {
            ScopedTimer _prof(L"Render Outer Shadow Map", gfxContext);

            g_OuterShadowBuffer.BeginRendering(gfxContext);
            RenderObjects(gfxContext, m_SunShadowOuter, &m_ShadowPSOCache, RenderPass_Shadow);
            g_OuterShadowBuffer.EndRendering(gfxContext);
        }

        if (SSAO::AsyncCompute)
		{
			gfxContext.Flush();
			pfnSetupGraphicsState();

			// Make the 3D queue wait for the Compute queue to finish SSAO
			g_CommandManager.GetGraphicsQueue().StallForProducer(g_CommandManager.GetComputeQueue());
		}

		{
			ScopedTimer _prof(L"Render Color", gfxContext);
			gfxContext.TransitionResource(g_SSAOFullScreen, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
			gfxContext.TransitionResource(g_SceneColorBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET);
			gfxContext.TransitionResource(g_SceneDepthBuffer, D3D12_RESOURCE_STATE_DEPTH_WRITE);
			gfxContext.SetRenderTarget(g_SceneColorBuffer.GetRTV(), g_SceneDepthBuffer.GetDSV());
			gfxContext.SetViewportAndScissor(m_MainViewport, m_MainScissor);
            gfxContext.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            RenderObjects(gfxContext, m_Camera, &m_ModelPSOCache, RenderPass_Color);

            RD.ZPrePass = false;
            m_NetClient.GetWorld()->GetTerrain()->Render(&gfxContext, &RD);

            LineRender::Render(gfxContext, m_ViewProjMatrix);
        }

        {
            ScopedTimer _prof(L"Render Alpha", gfxContext);

            // transparent render
            m_NetClient.GetWorld()->GetTerrain()->AlphaRender(&gfxContext, &RD);
        }
	}

	// Until I work out how to couple these two, it's "either-or".
	if (DepthOfField::Enable)
		DepthOfField::Render(gfxContext, m_Camera.GetNearClip(), m_Camera.GetFarClip());
	else
		MotionBlur::RenderCameraBlur(gfxContext, m_Camera);

	gfxContext.Finish();
}

void GameClient::RenderUI(class GraphicsContext& Context)
{
    TextContext Text(Context);
    Text.Begin();

    Text.SetCursorY(30);
    if (!m_NetClient.IsConnected(nullptr) && m_ConnectToPort != 0)
    {
        Text.DrawString("Connecting...");
    }
    Text.DrawFormattedString("Camera: <%0.2f %0.2f %0.2f>\n", (FLOAT)m_LastCameraPos.GetX(), (FLOAT)m_LastCameraPos.GetY(), (FLOAT)m_LastCameraPos.GetZ());
    if (!m_ControllableModelInstances.empty())
    {
        Text.DrawFormattedString("Target: %0.1f m/s %0.1f mph <%0.2f %0.2f %0.2f>\n", m_LastTargetVelocity, m_LastTargetVelocity * 2.23694f, (FLOAT)m_LastTargetPos.GetX(), (FLOAT)m_LastTargetPos.GetY(), (FLOAT)m_LastTargetPos.GetZ());
    }
    //Text.DrawFormattedString("Debug Vector: %10.3f %10.3f %10.3f", (FLOAT)DebugVector.GetX(), (FLOAT)DebugVector.GetY(), (FLOAT)DebugVector.GetZ());

    if (ShadowDebug)
    {
        INT Width = 256;
        Text.DrawTexturedRect(g_ShadowBuffer.GetSRV(), 1920 - Width, 0, Width, Width, true);
        Text.DrawTexturedRect(g_OuterShadowBuffer.GetSRV(), 1920 - Width, Width, Width, Width, true);
    }

    m_NetClient.GetWorld()->GetTerrain()->UIRender(Text);

    Text.End();
}
