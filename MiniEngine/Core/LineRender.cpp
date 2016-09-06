#include "pch.h"
#include "PipelineState.h"
#include "RootSignature.h"
#include "LineRender.h"
#include "LinearAllocator.h"
#include "CommandContext.h"
#include "BufferManager.h"
#include "CompiledShaders\LineRenderVS.h"
#include "CompiledShaders\LineRenderPS.h"

using namespace Math;
using namespace Graphics;

namespace
{
    struct DrawIndexedArgs
    {
        UINT32 StartIndex;
        UINT32 IndexCount;
    };
    typedef std::vector<D3D12_INDEX_BUFFER_VIEW> IBVector;
    typedef std::vector<D3D12_VERTEX_BUFFER_VIEW> VBVector;
    typedef std::vector<DrawIndexedArgs> DIAVector;

    GraphicsPSO s_PipelineState;
    RootSignature s_RS;

    UINT32 s_LinesPerSlab;

    IBVector s_IBs;
    VBVector s_VBs;
    DIAVector s_DrawArgs;

    USHORT* s_pSlabIB;
    LineRenderVertex* s_pSlabVB;
    UINT32 s_VertexCount;
    UINT32 s_CurrentVertex;
    UINT32 s_IndexCount;
    UINT32 s_CurrentIndex;

    LinearAllocator* s_pLinearAllocator;
}

HRESULT LineRender::Initialize(UINT32 LinesPerSlab)
{
    s_pLinearAllocator = new LinearAllocator(kCpuWritable);

    s_LinesPerSlab = LinesPerSlab;

    s_RS.Reset(3, 0);
    s_RS[0].InitAsConstantBuffer(0);
    s_RS[1].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 1);
    s_RS[2].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 0, 1);
    s_RS.Finalize(L"Line Render", D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    const D3D12_INPUT_ELEMENT_DESC Layout[] =
    {
        { "POSITION",   0,  DXGI_FORMAT_R32G32B32_FLOAT,        0,  0,      D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,    0 },
        { "COLOR",      0,  DXGI_FORMAT_R8G8B8A8_UNORM,         0,  12,      D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,    0 },
    };

    s_PipelineState.SetRootSignature(s_RS);
    s_PipelineState.SetInputLayout(ARRAYSIZE(Layout), Layout);
    s_PipelineState.SetVertexShader(g_pLineRenderVS, sizeof(g_pLineRenderVS));
    s_PipelineState.SetPixelShader(g_pLineRenderPS, sizeof(g_pLineRenderPS));
    s_PipelineState.SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE);

    CD3DX12_RASTERIZER_DESC RasterizerState(D3D12_DEFAULT);
    RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
    RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    s_PipelineState.SetRasterizerState(RasterizerState);
    s_PipelineState.SetBlendState(CD3DX12_BLEND_DESC(D3D12_DEFAULT));
    s_PipelineState.SetDepthStencilState(DepthStateReadWrite);
    s_PipelineState.SetRenderTargetFormat(g_SceneColorBuffer.GetFormat(), g_SceneDepthBuffer.GetFormat(), 1);
    s_PipelineState.Finalize();

    return S_OK;
}

HRESULT LineRender::Terminate()
{
    return S_OK;
}

bool EnsureSpace(UINT32 VertexCount, UINT32 IndexCount, USHORT** ppIndices, LineRenderVertex** ppVertices, UINT32* pBaseIndex)
{
    if (s_CurrentVertex + VertexCount > s_VertexCount ||
        s_CurrentIndex + IndexCount > s_IndexCount)
    {
        if (s_CurrentIndex > 0)
        {
            DrawIndexedArgs DIA = {};
            DIA.StartIndex = 0;
            DIA.IndexCount = s_CurrentIndex;
            s_DrawArgs.push_back(DIA);
        }

        s_CurrentIndex = 0;
        s_CurrentVertex = 0;
        s_VertexCount = 0;
        s_IndexCount = 0;

        const SIZE_T BytesPerVertex = sizeof(LineRenderVertex) + sizeof(USHORT);
        const UINT32 VertexCountPerSlab = s_LinesPerSlab * 2;
        const SIZE_T SlabSizeBytes = BytesPerVertex * VertexCountPerSlab;

        DynAlloc DA = s_pLinearAllocator->Allocate(SlabSizeBytes);
        BYTE* pSlab = (BYTE*)DA.DataPtr;
        D3D12_GPU_VIRTUAL_ADDRESS SlabVA = DA.GpuAddress;

        D3D12_VERTEX_BUFFER_VIEW VBV = {};
        VBV.BufferLocation = SlabVA;
        VBV.SizeInBytes = VertexCountPerSlab * sizeof(LineRenderVertex);
        VBV.StrideInBytes = sizeof(LineRenderVertex);
        s_VBs.push_back(VBV);

        D3D12_INDEX_BUFFER_VIEW IBV = {};
        IBV.BufferLocation = SlabVA + VBV.SizeInBytes;
        IBV.Format = DXGI_FORMAT_R16_UINT;
        IBV.SizeInBytes = VertexCountPerSlab * sizeof(USHORT);
        s_IBs.push_back(IBV);

        s_pSlabVB = (LineRenderVertex*)pSlab;
        s_pSlabIB = (USHORT*)(pSlab + VertexCountPerSlab * sizeof(LineRenderVertex));
        s_VertexCount = VertexCountPerSlab;
        s_IndexCount = VertexCountPerSlab;
    }

    *pBaseIndex = s_CurrentIndex;
    *ppIndices = s_pSlabIB + s_CurrentIndex;
    s_CurrentIndex += IndexCount;
    *ppVertices = s_pSlabVB + s_CurrentVertex;
    s_CurrentVertex += IndexCount;
    return true;
}

inline void TransformVertices(FXMMATRIX matWorld, bool SingleColor, const UINT32 VertexCount, LineRenderVertex* pVertices, const XMFLOAT4* pColors, const XMFLOAT3* pPositions)
{
    if (SingleColor)
    {
        XMUBYTEN4 Color;
        XMStoreUByteN4(&Color, XMLoadFloat4(pColors));
        for (UINT32 i = 0; i < VertexCount; ++i)
        {
            XMVECTOR pos = XMLoadFloat3(&pPositions[i]);
            pos = XMVector3TransformCoord(pos, matWorld);
            XMStoreFloat3(&pVertices[i].Position, pos);
            pVertices[i].Color = Color;
        }
    }
    else
    {
        for (UINT32 i = 0; i < VertexCount; ++i)
        {
            XMVECTOR pos = XMLoadFloat3(&pPositions[i]);
            pos = XMVector3TransformCoord(pos, matWorld);
            XMStoreFloat3(&pVertices[i].Position, pos);
            XMStoreUByteN4(&pVertices[i].Color, XMLoadFloat4(&pColors[i]));
        }
    }
}

void LineRender::AddLineList(FXMMATRIX matWorld, UINT32 LineCount, const XMFLOAT3* pPositions, const XMFLOAT4* pColors, bool SingleColor)
{
    const UINT32 VertexCount = LineCount * 2;

    USHORT* pIndices = nullptr;
    LineRenderVertex* pVertices = nullptr;
    UINT32 BaseIndex = 0;
    EnsureSpace(VertexCount, VertexCount, &pIndices, &pVertices, &BaseIndex);

    TransformVertices(matWorld, SingleColor, VertexCount, pVertices, pColors, pPositions);

    for (UINT32 i = 0; i < VertexCount; ++i)
    {
        pIndices[i] = (USHORT)(i + BaseIndex);
    }
}

void LineRender::AddLineStrip(FXMMATRIX matWorld, UINT32 LineCount, const XMFLOAT3* pPositions, const XMFLOAT4* pColors, bool SingleColor)
{

}

void LineRender::Render(GraphicsContext& Context, const Matrix4& ViewProjMat)
{
    if (s_CurrentIndex > 0)
    {
        DrawIndexedArgs DIA = {};
        DIA.StartIndex = 0;
        DIA.IndexCount = s_CurrentIndex;
        s_DrawArgs.push_back(DIA);
    }
    if (s_DrawArgs.empty())
    {
        return;
    }
    assert(s_DrawArgs.size() == s_IBs.size());

    DynAlloc DA = s_pLinearAllocator->Allocate(sizeof(XMFLOAT4X4));
    XMFLOAT4X4* pCB = (XMFLOAT4X4*)DA.DataPtr;
    D3D12_GPU_VIRTUAL_ADDRESS CBVA = DA.GpuAddress;
    XMStoreFloat4x4(pCB, ViewProjMat);

    Context.SetRootSignature(s_RS);
    Context.SetPipelineState(s_PipelineState);
    Context.SetConstantBuffer(0, CBVA);
    Context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);

    const UINT32 DrawCount = (UINT32)s_DrawArgs.size();
    for (UINT32 i = 0; i < DrawCount; ++i)
    {
        Context.SetIndexBuffer(s_IBs[i]);
        Context.SetVertexBuffer(0, s_VBs[i]);
        const DrawIndexedArgs& DIA = s_DrawArgs[i];
        Context.DrawIndexedInstanced(DIA.IndexCount, 1, DIA.StartIndex, 0, 0);
    }

    s_DrawArgs.clear();
    s_IBs.clear();
    s_VBs.clear();
    s_CurrentIndex = 0;
    s_IndexCount = 0;
    s_CurrentVertex = 0;
    s_VertexCount = 0;
    s_pSlabIB = nullptr;
    s_pSlabVB = nullptr;

    s_pLinearAllocator->CleanupUsedPages(g_CommandManager.GetGraphicsQueue().GetNextFenceValue());
}

void LineRender::DrawLine(FXMVECTOR A, FXMVECTOR B, FXMVECTOR Color)
{
    XMFLOAT3 Positions[2];
    XMStoreFloat3(&Positions[0], A);
    XMStoreFloat3(&Positions[1], B);
    XMFLOAT4 Colors[1];
    XMStoreFloat4(&Colors[0], Color);
    AddLineList(XMMatrixIdentity(), 1, Positions, Colors, true);
}

void LineRender::DrawAxis(FXMMATRIX matWorld)
{
    const XMFLOAT3 Positions[6] =
    {
        XMFLOAT3(0, 0, 0),
        XMFLOAT3(1, 0, 0),
        XMFLOAT3(0, 0, 0),
        XMFLOAT3(0, 1, 0),
        XMFLOAT3(0, 0, 0),
        XMFLOAT3(0, 0, 1),
    };

    const XMFLOAT4 Colors[6] =
    {
        XMFLOAT4(1, 0, 0, 1),
        XMFLOAT4(1, 0, 0, 1),
        XMFLOAT4(0, 1, 0, 1),
        XMFLOAT4(0, 1, 0, 1),
        XMFLOAT4(0, 0, 1, 1),
        XMFLOAT4(0, 0, 1, 1),
    };

    AddLineList(matWorld, 3, Positions, Colors, false);
}

void LineRender::DrawCube(FXMMATRIX matWorld, FXMVECTOR Color)
{
    const XMFLOAT3 Corners[] =
    {
        XMFLOAT3(-0.5f, -0.5f, -0.5f),
        XMFLOAT3( 0.5f, -0.5f, -0.5f),
        XMFLOAT3( 0.5f,  0.5f, -0.5f),
        XMFLOAT3(-0.5f,  0.5f, -0.5f),
        XMFLOAT3(-0.5f, -0.5f,  0.5f),
        XMFLOAT3( 0.5f, -0.5f,  0.5f),
        XMFLOAT3( 0.5f,  0.5f,  0.5f),
        XMFLOAT3(-0.5f,  0.5f,  0.5f),
    };

    const USHORT Indices[] =
    {
        0, 1,
        1, 2,
        2, 3,
        3, 0,
        4, 5,
        5, 6,
        6, 7,
        7, 4,
        0, 4,
        1, 5,
        2, 6,
        3, 7,
    };

    LineRenderVertex* pVerts = nullptr;
    USHORT* pIndices = nullptr;
    UINT32 BaseIndex = 0;

    EnsureSpace(ARRAYSIZE(Corners), ARRAYSIZE(Indices), &pIndices, &pVerts, &BaseIndex);

    for (UINT32 i = 0; i < ARRAYSIZE(Corners); ++i)
    {
        XMVECTOR Pos = XMLoadFloat3(&Corners[i]);
        Pos = XMVector3TransformCoord(Pos, matWorld);
        XMStoreFloat3(&pVerts[i].Position, Pos);
        XMStoreUByteN4(&pVerts[i].Color, Color);
    }

    for (UINT32 i = 0; i < ARRAYSIZE(Indices); ++i)
    {
        pIndices[i] = (USHORT)(BaseIndex + Indices[i]);
    }
}

void LineRender::DrawAxisAlignedBox(FXMVECTOR A, FXMVECTOR B, FXMVECTOR Color)
{
    const XMVECTOR Center = (A + B) * g_XMOneHalf;
    const XMVECTOR Extents = XMVectorAbs(B - A);

    const XMMATRIX matScaling = XMMatrixScalingFromVector(Extents);
    const XMMATRIX matTranslation = XMMatrixTranslationFromVector(Center);

    DrawCube(matScaling * matTranslation, Color);
}

void LineRender::DrawRing(FXMMATRIX matWorld, FXMVECTOR Axis1, FXMVECTOR Axis2, UINT32 SegmentCount, CXMVECTOR Color)
{
    const XMVECTOR Delta = XMVectorReplicate(XM_2PI / (FLOAT)SegmentCount);

    LineRenderVertex* pVerts = nullptr;
    USHORT* pIndices = nullptr;
    UINT32 BaseIndex = 0;

    EnsureSpace(SegmentCount, SegmentCount * 2, &pIndices, &pVerts, &BaseIndex);

    for (UINT32 i = 0; i < SegmentCount; ++i)
    {
        XMVECTOR Sin;
        XMVECTOR Cos;
        XMVectorSinCos(&Sin, &Cos, Delta * (FLOAT)i);
        XMVECTOR Pos = Sin * Axis1 + Cos * Axis2;
        Pos = XMVector3TransformCoord(Pos, matWorld);
        XMStoreFloat3(&pVerts[i].Position, Pos);
        XMStoreUByteN4(&pVerts[i].Color, Color);

        pIndices[i * 2] = BaseIndex + i;
        pIndices[i * 2 + 1] = BaseIndex + (i + 1) % SegmentCount;
    }
}

void LineRender::DrawRingXZ(FXMMATRIX matWorld, FLOAT Radius, UINT32 SegmentCount, FXMVECTOR Color)
{
    XMVECTOR Axis1 = g_XMIdentityR0 * Radius;
    XMVECTOR Axis2 = g_XMIdentityR2 * Radius;
    DrawRing(matWorld, Axis1, Axis2, SegmentCount, Color);
}

void LineRender::DrawGrid(FXMMATRIX matWorld, FXMVECTOR Axis1, FXMVECTOR Axis2, UINT32 DivisionCount1, UINT32 DivisionCount2, CXMVECTOR Color)
{
    const XMVECTOR Delta1 = Axis1 * (2.0f / (FLOAT)DivisionCount1);
    const XMVECTOR Delta2 = Axis2 * (2.0f / (FLOAT)DivisionCount2);

    LineRenderVertex* pVerts = nullptr;
    USHORT* pIndices = nullptr;
    UINT32 BaseIndex = 0;

    UINT32 SegmentCount = DivisionCount1 + DivisionCount2 + 2;
    UINT32 VertexCount = SegmentCount * 2;

    EnsureSpace(VertexCount, VertexCount, &pIndices, &pVerts, &BaseIndex);

    XMVECTOR LineOrigin = -Axis1 - Axis2;
    XMVECTOR LineDirection = Axis2 * 2;
    for (UINT32 i = 0; i <= DivisionCount1; ++i)
    {
        XMVECTOR Pos1 = LineOrigin;
        XMVECTOR Pos2 = LineOrigin + LineDirection;
        Pos1 = XMVector3TransformCoord(Pos1, matWorld);
        Pos2 = XMVector3TransformCoord(Pos2, matWorld);
        XMStoreFloat3(&pVerts[i * 2].Position, Pos1);
        XMStoreUByteN4(&pVerts[i * 2].Color, Color);
        XMStoreFloat3(&pVerts[i * 2 + 1].Position, Pos2);
        XMStoreUByteN4(&pVerts[i * 2 + 1].Color, Color);

        pIndices[i * 2] = BaseIndex + (i * 2);
        pIndices[i * 2 + 1] = BaseIndex + (i * 2) + 1;

        LineOrigin += Delta1;
    }

    UINT32 IndexOffset = (DivisionCount1 + 1) * 2;

    LineOrigin = -Axis1 - Axis2;
    LineDirection = Axis1 * 2;
    for (UINT32 i = 0; i <= DivisionCount2; ++i)
    {
        XMVECTOR Pos1 = LineOrigin;
        XMVECTOR Pos2 = LineOrigin + LineDirection;
        Pos1 = XMVector3TransformCoord(Pos1, matWorld);
        Pos2 = XMVector3TransformCoord(Pos2, matWorld);
        XMStoreFloat3(&pVerts[IndexOffset + i * 2].Position, Pos1);
        XMStoreUByteN4(&pVerts[IndexOffset + i * 2].Color, Color);
        XMStoreFloat3(&pVerts[IndexOffset + i * 2 + 1].Position, Pos2);
        XMStoreUByteN4(&pVerts[IndexOffset + i * 2 + 1].Color, Color);

        pIndices[IndexOffset + i * 2] = BaseIndex + IndexOffset + (i * 2);
        pIndices[IndexOffset + i * 2 + 1] = BaseIndex + IndexOffset + (i * 2) + 1;

        LineOrigin += Delta2;
    }
}

void LineRender::DrawGridXZ(FXMMATRIX matWorld, FLOAT Size, UINT32 DivisionCount, FXMVECTOR Color)
{
    XMVECTOR Axis1 = g_XMIdentityR0 * Size;
    XMVECTOR Axis2 = g_XMIdentityR2 * Size;
    DrawGrid(matWorld, Axis1, Axis2, DivisionCount, DivisionCount, Color);
}

void LineRender::DrawFrustum(const Math::Frustum& frustum, FXMMATRIX matWorld, FXMVECTOR Color)
{
    USHORT* pIndices = nullptr;
    LineRenderVertex* pVertices = nullptr;
    UINT32 BaseIndex = 0;
    EnsureSpace(8, 24, &pIndices, &pVertices, &BaseIndex);

    for (UINT32 i = 0; i < 8; ++i)
    {
        XMVECTOR Pos = frustum.GetFrustumCorner((Math::Frustum::CornerID)i);
        Pos = XMVector3TransformCoord(Pos, matWorld);
        XMStoreFloat3(&pVertices[i].Position, Pos);
        XMStoreUByteN4(&pVertices[i].Color, Color);
    }

    pIndices[ 0] = BaseIndex + 0;
    pIndices[ 1] = BaseIndex + 1;
    pIndices[ 2] = BaseIndex + 1;
    pIndices[ 3] = BaseIndex + 3;
    pIndices[ 4] = BaseIndex + 3;
    pIndices[ 5] = BaseIndex + 2;
    pIndices[ 6] = BaseIndex + 2;
    pIndices[ 7] = BaseIndex + 0;
    pIndices[ 8] = BaseIndex + 4;
    pIndices[ 9] = BaseIndex + 5;
    pIndices[10] = BaseIndex + 5;
    pIndices[11] = BaseIndex + 7;
    pIndices[12] = BaseIndex + 7;
    pIndices[13] = BaseIndex + 6;
    pIndices[14] = BaseIndex + 6;
    pIndices[15] = BaseIndex + 4;
    pIndices[16] = BaseIndex + 0;
    pIndices[17] = BaseIndex + 4;
    pIndices[18] = BaseIndex + 1;
    pIndices[19] = BaseIndex + 5;
    pIndices[20] = BaseIndex + 3;
    pIndices[21] = BaseIndex + 7;
    pIndices[22] = BaseIndex + 2;
    pIndices[23] = BaseIndex + 6;
}
