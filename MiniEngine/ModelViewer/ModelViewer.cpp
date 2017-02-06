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
#include "GridTerrain.h"
#include "GridTerrainJobs.h"
#include "GpuJobQueue.h"

#include "TessTerrain.h"

#include "CompiledShaders/DepthViewerVS.h"
#include "CompiledShaders/DepthViewerPS.h"
#include "CompiledShaders/ModelViewerVS.h"
#include "CompiledShaders/ModelViewerPS.h"

using namespace GameCore;
using namespace Math;
using namespace Graphics;

static const bool g_TestTerrain = false;

class PrintfDebugListener : public INetDebugListener
{
public:
    void OutputString(BOOL Label, const CHAR* strLine) override final
    {
        Utility::Print(strLine);
    }
};

class ModelViewer : public GameCore::IGameApp, public IClientNotifications
{
public:

	ModelViewer()
		: m_pCameraController(nullptr),
          m_pInputRemoting(nullptr)
	{
        m_ClientObjectsCreated = false;
        m_FollowCameraEnabled = false;
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
    void CreateParticleEffects();

    void RemoteObjectCreated(ModelInstance* pModelInstance, UINT ParentObjectID);
    void RemoteObjectDeleted(ModelInstance* pModelInstance);

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

	D3D12_CPU_DESCRIPTOR_HANDLE m_ExtraTextures[2];

	Vector3 m_SunDirection;
	ShadowCamera m_SunShadow;

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
    std::vector<ModelInstance*> m_PlacedModelInstances;

    GridTerrain m_GT;

    TessellatedTerrain m_TessTerrain;

    Vector3 DebugVector;
};

CREATE_APPLICATION( ModelViewer )

ExpVar m_SunLightIntensity("Application/Sun Light Intensity", 4.0f, 0.0f, 16.0f, 0.1f);
ExpVar m_AmbientIntensity("Application/Ambient Intensity", 0.1f, -16.0f, 16.0f, 0.1f);
NumVar m_SunOrientation("Application/Sun Orientation", -0.5f, -100.0f, 100.0f, 0.1f );
NumVar m_SunInclination("Application/Sun Inclination", 0.75f, 0.0f, 1.0f, 0.01f );
NumVar ShadowDimX("Application/Shadow Dim X", 5000, 100, 10000, 100 );
NumVar ShadowDimY("Application/Shadow Dim Y", 3000, 100, 10000, 100 );
NumVar ShadowDimZ("Application/Shadow Dim Z", 3000, 1000, 10000, 100 );
BoolVar DisplayPhysicsDebug("Application/Debug Draw Physics", false);
BoolVar DisplayServerPhysicsDebug("Application/Debug Draw Server Physics", true);

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

void ModelViewer::Startup( void )
{
    m_StartServer = true;
    strcpy_s(m_strConnectToServerName, "localhost");
    m_ConnectToPort = 31338;

    ProcessCommandLine();

    DataFile::SetDataFileRootPath("Data");
    TestData* pTestData = (TestData*)DataFile::LoadStructFromFile(STRUCT_TEMPLATE_REFERENCE(TestData), "foo");

	m_RootSig.Reset(6, 2);
	m_RootSig.InitStaticSampler(0, SamplerAnisoWrapDesc, D3D12_SHADER_VISIBILITY_PIXEL);
	m_RootSig.InitStaticSampler(1, SamplerShadowDesc, D3D12_SHADER_VISIBILITY_PIXEL);
	m_RootSig[0].InitAsConstantBuffer(0, D3D12_SHADER_VISIBILITY_VERTEX);
	m_RootSig[1].InitAsConstantBuffer(0, D3D12_SHADER_VISIBILITY_PIXEL);
	m_RootSig[2].InitAsBufferSRV(0, D3D12_SHADER_VISIBILITY_VERTEX);
	m_RootSig[3].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 6, D3D12_SHADER_VISIBILITY_PIXEL);
	m_RootSig[4].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 64, 3, D3D12_SHADER_VISIBILITY_PIXEL);
	m_RootSig[5].InitAsConstants(1, 1, D3D12_SHADER_VISIBILITY_VERTEX);
	m_RootSig.Finalize(L"ModelViewer", D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	DXGI_FORMAT ColorFormat = g_SceneColorBuffer.GetFormat();
	DXGI_FORMAT DepthFormat = g_SceneDepthBuffer.GetFormat();
	DXGI_FORMAT ShadowFormat = g_ShadowBuffer.GetFormat();

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
	m_ModelPSO.SetVertexShader( g_pModelViewerVS, sizeof(g_pModelViewerVS) );
	m_ModelPSO.SetPixelShader( g_pModelViewerPS, sizeof(g_pModelViewerPS) );
	m_ModelPSO.Finalize();
    m_ModelPSOCache.Initialize(&m_ModelPSO);

	m_ExtraTextures[0] = g_SSAOFullScreen.GetSRV();
	m_ExtraTextures[1] = g_ShadowBuffer.GetSRV();

	TextureManager::Initialize(L"Textures/");

    m_pClientWorld = m_NetClient.GetWorld();

    ModelInstance* pMI = nullptr;

	//CreateParticleEffects();

    Vector3 eye(100, 100, -100);
    if (pMI != nullptr)
    {
        const Model* pModel = pMI->GetModel();
        float modelRadius = Length(pModel->m_Header.boundingBox.max - pModel->m_Header.boundingBox.min) * .5f;
        if (modelRadius >= 200)
        {
            eye = pMI->GetWorldPosition() + (pModel->m_Header.boundingBox.min + pModel->m_Header.boundingBox.max) * .5f + Vector3(modelRadius * .5f, 0.0f, 0.0f);
        }
    }
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

    if (m_NetServer.IsStarted())
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

        XMFLOAT3 CenterPos(200, 0, 150);
        for (UINT32 i = 0; i < (g_RingSize * 8); ++i)
        {
            DT = CreateCylinderTransform(i, CenterPos);
            ModelInstance* pMI = (ModelInstance*)m_NetServer.SpawnObject(nullptr, "*cube", nullptr, DT, XMFLOAT3(0, 0, 0));
            m_PlacedModelInstances.push_back(pMI);
        }

        DT = DecomposedTransform::CreateFromComponents(XMFLOAT3(0, 50, 0));
        ModelInstance* pMI = (ModelInstance*)m_NetServer.SpawnObject(nullptr, "*cube", nullptr, DT, XMFLOAT3(0, 0, 0));
        pMI->SetLifetimeRemaining(10);

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

        for (UINT32 i = 0; i < 10; ++i)
        {
            FLOAT Pitch = (FLOAT)i * 0.1f;
            FLOAT Ypos = 25.0f + (FLOAT)i * 4;
            DT = DecomposedTransform::CreateFromComponents(XMFLOAT3(0, Ypos, -100), Pitch, 0);
            m_NetServer.SpawnObject(nullptr, "*cube1.5", nullptr, DT, XMFLOAT3(0, 0, 0));
        }

        m_NetServer.GetWorld()->InitializeTerrain(&m_TessTerrain);
    }

    if (g_TestTerrain)
    {
        GridTerrainConfig Config = {};
        Config.SetDefault();
        Config.pWorld = m_NetClient.GetWorld();
        Config.pd3dDevice = Graphics::g_Device;
        m_GT.Initialize(&Config);
    }

    m_TessTerrain.Initialize();
}

bool ModelViewer::ProcessCommand(const CHAR* strCommand, const CHAR* strArgument)
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

void ModelViewer::ProcessCommandLine()
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

void ModelViewer::Cleanup( void )
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

    m_GT.Terminate();
    m_TessTerrain.Terminate();

	delete m_pCameraController;
	m_pCameraController = nullptr;

    delete m_pFollowCameraController;
    m_pFollowCameraController = nullptr;
}

void ModelViewer::RemoteObjectCreated(ModelInstance* pModelInstance, UINT ParentObjectID)
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

void ModelViewer::RemoteObjectDeleted(ModelInstance* pModelInstance)
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

void ModelViewer::Update( float deltaT )
{
	ScopedTimer _prof(L"Update State");

    m_NetClient.SingleThreadedTick();

    if (m_NetClient.IsConnected(nullptr))
    {
        if (!m_ClientObjectsCreated && m_NetClient.CanSpawnObjects())
        {
            m_ClientObjectsCreated = true;
            DecomposedTransform DT = DecomposedTransform::CreateFromComponents(XMFLOAT3(100, 3, 100));
            m_NetClient.SpawnObjectOnServer("Vehicle1", nullptr, DT, XMFLOAT3(0, 0, 0));
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
                m_pInputRemoting->ClientUpdate(InputState);
            }
            else
            {
                m_pInputRemoting->ClientZero();
            }
        }

        LARGE_INTEGER ClientTicks;
        QueryPerformanceCounter(&ClientTicks);
        m_pClientWorld->Tick(deltaT, ClientTicks.QuadPart);
    }

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

    if (GameInput::IsFirstPressed(GameInput::kKey_p))
    {
        const UINT32 Count = (UINT32)m_PlacedModelInstances.size();
        XMFLOAT3 CenterPos(200, 0, 150);
        for (UINT32 i = 0; i < Count; ++i)
        {
            DecomposedTransform DT = CreateCylinderTransform(i, CenterPos);
            m_PlacedModelInstances[i]->SetWorldTransform(DT.GetMatrix());
        }
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

    if (g_TestTerrain)
    {
        DirectX::BoundingFrustum BF;
        DirectX::BoundingFrustum::CreateFromMatrix(BF, m_Camera.GetProjMatrix());
        XMMATRIX CameraWorld = m_Camera.GetWorldMatrix();
        GridTerrainUpdate GTU = {};
        BF.Transform(GTU.TransformedFrustum, CameraWorld);
        GTU.matVP = m_ViewProjMatrix;
        GTU.matCameraWorld = CameraWorld;
        m_GT.Update(GTU);
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

    m_NetServer.SingleThreadedTick();

    if (DisplayServerPhysicsDebug && !DisplayPhysicsDebug)
    {
        m_NetServer.GetWorld()->GetPhysicsWorld()->DebugRender();
    }
}

bool ModelViewer::IsDone()
{
    return false;
}

void ModelViewer::RenderObjects(GraphicsContext& gfxContext, const BaseCamera& Camera, PsoLayoutCache* pPsoCache, RenderPass PassType)
{
    ModelRenderContext MRC;
    MRC.pContext = &gfxContext;
    MRC.CameraPosition = Camera.GetPosition();
    MRC.ModelToShadow = m_SunShadow.GetShadowMatrix();
    Matrix4 ViewMatrix = Camera.GetViewMatrix();
    ViewMatrix.SetW(Vector4(g_XMIdentityR3));
    Matrix4 VPMatrix = Camera.GetProjMatrix() * ViewMatrix;
    MRC.ViewProjection = VPMatrix;
    MRC.pPsoCache = pPsoCache;
    MRC.LastInputLayoutIndex = -1;
    MRC.CurrentPassType = PassType;

    m_pClientWorld->Render(MRC);
}

void ModelViewer::RenderScene( void )
{
    g_GpuJobQueue.ExecuteGraphicsJobs();

    GraphicsContext& gfxContext = GraphicsContext::Begin(L"Scene Render");

    if (m_NetServer.IsStarted())
    {
        m_NetServer.GetWorld()->GetTerrainPhysicsTracker()->ServerRender(&gfxContext);
    }

	ParticleEffects::Update(gfxContext.GetComputeContext(), Graphics::GetFrameTime());

    TessellatedTerrainRenderDesc RD = {};
    XMStoreFloat4x4A(&RD.matView, m_Camera.GetViewMatrix());
    XMStoreFloat4x4A(&RD.matProjection, m_Camera.GetProjMatrix());
    XMStoreFloat4A(&RD.CameraPosWorld, m_Camera.GetPosition());
    RD.Viewport = m_MainViewport;
    RD.ZPrePass = false;

    if (0)
    {
        UINT32 HeightmapIndex;
        const FLOAT WorldScale = 400;
        const XMVECTOR CenterEyePos = XMVectorSet((FLOAT)Graphics::GetFrameCount() * 10.0f, 0, 0, 0);
        XMVECTOR EyePos;

        EyePos = CenterEyePos + XMVectorSet(0, 0, 0, 0);
        HeightmapIndex = m_TessTerrain.PhysicsRender(&gfxContext, EyePos, WorldScale, nullptr, nullptr);
        m_TessTerrain.FreePhysicsHeightmap(HeightmapIndex);

        EyePos = CenterEyePos + XMVectorSet(WorldScale, 0, 0, 0);
        HeightmapIndex = m_TessTerrain.PhysicsRender(&gfxContext, EyePos, WorldScale, nullptr, nullptr);
        m_TessTerrain.FreePhysicsHeightmap(HeightmapIndex);

        EyePos = CenterEyePos + XMVectorSet(0, 0, WorldScale, 0);
        HeightmapIndex = m_TessTerrain.PhysicsRender(&gfxContext, EyePos, WorldScale, nullptr, nullptr);
        m_TessTerrain.FreePhysicsHeightmap(HeightmapIndex);

        EyePos = CenterEyePos + XMVectorSet(WorldScale, 0, WorldScale, 0);
        HeightmapIndex = m_TessTerrain.PhysicsRender(&gfxContext, EyePos, WorldScale, nullptr, nullptr);
        m_TessTerrain.FreePhysicsHeightmap(HeightmapIndex);
    }

    m_TessTerrain.OffscreenRender(&gfxContext, &RD);

	__declspec(align(16)) struct
	{
		Vector3 sunDirection;
		Vector3 sunLight;
		Vector3 ambientLight;
		float ShadowTexelSize;
	} psConstants;

	psConstants.sunDirection = m_SunDirection;
	psConstants.sunLight = Vector3(1.0f, 1.0f, 1.0f) * m_SunLightIntensity;
	psConstants.ambientLight = Vector3(1.0f, 1.0f, 1.0f) * m_AmbientIntensity;
	psConstants.ShadowTexelSize = 1.0f / g_ShadowBuffer.GetWidth();

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
        m_TessTerrain.Render(&gfxContext, &RD);
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
			gfxContext.SetDynamicDescriptors(4, 0, 2, m_ExtraTextures);
			gfxContext.SetDynamicConstantBufferView(1, sizeof(psConstants), &psConstants);
		};

		pfnSetupGraphicsState();

		{
			ScopedTimer _prof(L"Render Shadow Map", gfxContext);

            m_SunShadow.SetSceneCameraPos(m_Camera.GetPosition());
			m_SunShadow.UpdateMatrix(-m_SunDirection, Vector3(0, -500.0f, 0), Vector3(ShadowDimX, ShadowDimY, ShadowDimZ),
				(uint32_t)g_ShadowBuffer.GetWidth(), (uint32_t)g_ShadowBuffer.GetHeight(), 16);

			g_ShadowBuffer.BeginRendering(gfxContext);
            RenderObjects(gfxContext, m_SunShadow, &m_ShadowPSOCache, RenderPass_Shadow);
			g_ShadowBuffer.EndRendering(gfxContext);
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
			gfxContext.TransitionResource(g_SceneDepthBuffer, D3D12_RESOURCE_STATE_DEPTH_READ);
			gfxContext.SetRenderTarget(g_SceneColorBuffer.GetRTV(), g_SceneDepthBuffer.GetDSV());
			gfxContext.SetViewportAndScissor(m_MainViewport, m_MainScissor);
            gfxContext.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            RenderObjects(gfxContext, m_Camera, &m_ModelPSOCache, RenderPass_Color);

            if (g_TestTerrain)
            {
                GridTerrainRender GTR = {};
                GTR.AbsoluteTime = SystemTime::TicksToSeconds(SystemTime::GetCurrentTick());
                GTR.matVP = m_ViewProjMatrix;
                GTR.pContext = &gfxContext;
                GTR.vCameraOffset = g_XMZero;
                GTR.Wireframe = false;
                m_GT.RenderOpaque(GTR);
            }

            RD.ZPrePass = false;
            m_TessTerrain.Render(&gfxContext, &RD);

            LineRender::Render(gfxContext, m_ViewProjMatrix);
        }
	}

	//ParticleEffects::Render(gfxContext, m_Camera, g_SceneColorBuffer, g_SceneDepthBuffer, g_LinearDepth);

	// Until I work out how to couple these two, it's "either-or".
	if (DepthOfField::Enable)
		DepthOfField::Render(gfxContext, m_Camera.GetNearClip(), m_Camera.GetFarClip());
	else
		MotionBlur::RenderCameraBlur(gfxContext, m_Camera);

	gfxContext.Finish();
}

void ModelViewer::RenderUI(class GraphicsContext& Context)
{
    TextContext Text(Context);
    Text.Begin();

    Text.SetCursorY(30);
    if (!m_NetClient.IsConnected(nullptr) && m_ConnectToPort != 0)
    {
        Text.DrawString("Connecting...");
    }
    //Text.DrawFormattedString("Debug Vector: %10.3f %10.3f %10.3f", (FLOAT)DebugVector.GetX(), (FLOAT)DebugVector.GetY(), (FLOAT)DebugVector.GetZ());

    m_TessTerrain.UIRender(Text);

    if (0)
    {
        const PagingQueueEntryDeque& PMD = g_GpuJobQueue.GetPagingMapDeque();
        auto iter = PMD.begin();
        auto end = PMD.end();
        while (iter != end)
        {
            const PagingQueueEntry* pEntry = *iter++;
            if (pEntry->IsUnmapped())
            {
                continue;
            }
            const TiledTextureBuffer& TTB = pEntry->TiledTexture[0];
            UINT32 Format = TTB.GetFormat();
            if (TTB.IsSubresourceMapped(0) && Format == g_GridTerrainJobs.GetHeightmapFormat())
            {
                UINT32 Width = TTB.GetWidth();
                UINT32 Height = TTB.GetHeight();
                Text.DrawTexturedRect(TTB.GetSRV(), 10, 100, Width, Height, true);
                break;
            }
        }
    }

    Text.End();
}

void ModelViewer::CreateParticleEffects()
{
	ParticleEffectProperties Effect = ParticleEffectProperties();
	Effect.MinStartColor = Effect.MaxStartColor = Effect.MinEndColor = Effect.MaxEndColor = Color(1.0f, 1.0f, 1.0f, 0.0f);
	Effect.TexturePath = L"sparkTex.dds";

	Effect.TotalActiveLifetime = FLT_MAX;
	Effect.Size = Vector4(4.0f, 8.0f, 4.0f, 8.0f);
	Effect.Velocity = Vector4(20.0f, 200.0f, 50.0f, 180.0f);
	Effect.LifeMinMax = XMFLOAT2(1.0f, 3.0f);
	Effect.MassMinMax = XMFLOAT2(4.5f, 15.0f);
	Effect.EmitProperties.Gravity = XMFLOAT3(0.0f, -100.0f, 0.0f);
	Effect.EmitProperties.FloorHeight = -0.5f;
	Effect.EmitProperties.EmitPosW = Effect.EmitProperties.LastEmitPosW = XMFLOAT3(-1200.0f, 185.0f, -445.0f);
	Effect.EmitProperties.MaxParticles = 800;
	Effect.EmitRate = 64.0f;
	Effect.Spread.x = 20.0f;
	Effect.Spread.y = 50.0f;
	ParticleEffects::InstantiateEffect( &Effect );

	ParticleEffectProperties Smoke = ParticleEffectProperties();
	Smoke.TexturePath = L"smoke.dds";

	Smoke.TotalActiveLifetime = FLT_MAX;;
	Smoke.EmitProperties.MaxParticles = 25;
	Smoke.EmitProperties.EmitPosW = Smoke.EmitProperties.LastEmitPosW = XMFLOAT3(1120.0f, 185.0f, -445.0f);
	Smoke.EmitRate = 64.0f;
	Smoke.LifeMinMax = XMFLOAT2(2.5f, 4.0f);
	Smoke.Size = Vector4(60.0f, 108.0f, 30.0f, 208.0f);
	Smoke.Velocity = Vector4(30.0f, 30.0f, 10.0f, 40.0f);
	Smoke.MassMinMax = XMFLOAT2(1.0, 3.5);
	Smoke.Spread.x = 60.0f;
	Smoke.Spread.y = 70.0f;
	Smoke.Spread.z = 20.0f;
	ParticleEffects::InstantiateEffect( &Smoke );

	ParticleEffectProperties Fire = ParticleEffectProperties();
	Fire.MinStartColor = Fire.MaxStartColor = Fire.MinEndColor = Fire.MaxEndColor = Color(1.0f, 1.0f, 1.0f, 0.0f);
	Fire.TexturePath = L"fire.dds";

	Fire.TotalActiveLifetime = FLT_MAX;
	Fire.Size = Vector4(54.0f, 68.0f, 0.1f, 0.3f);
	Fire.Velocity = Vector4 (10.0f, 30.0f, 50.0f, 50.0f);
	Fire.LifeMinMax = XMFLOAT2(1.0f, 3.0f);
	Fire.MassMinMax = XMFLOAT2(10.5f, 14.0f);
	Fire.EmitProperties.Gravity = XMFLOAT3(0.0f, 1.0f, 0.0f);
	Fire.EmitProperties.EmitPosW = Fire.EmitProperties.LastEmitPosW = XMFLOAT3(1120.0f, 125.0f, 405.0f);
	Fire.EmitProperties.MaxParticles = 25;
	Fire.EmitRate = 64.0f;
	Fire.Spread.x = 1.0f;
	Fire.Spread.y = 60.0f;
	ParticleEffects::InstantiateEffect( &Fire );
}