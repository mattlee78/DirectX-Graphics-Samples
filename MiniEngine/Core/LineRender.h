#pragma once

#include "pch.h"
#include "VectorMath.h"
#include <DirectXPackedVector.h>
#include <DirectXCollision.h>
#include "Math\Frustum.h"

using namespace DirectX;
using namespace DirectX::PackedVector;

struct LineRenderVertex
{
    XMFLOAT3 Position;
    XMUBYTEN4 Color;
};

namespace LineRender
{
    HRESULT Initialize(UINT32 LinesPerSlab = 512);
    HRESULT Terminate();

    void Render(GraphicsContext& Context, const Math::Matrix4& ViewProjMat);

    void DrawLine(FXMVECTOR A, FXMVECTOR B, FXMVECTOR Color);
    void DrawAxis(FXMMATRIX matWorld);
    void DrawCube(FXMMATRIX matWorld, FXMVECTOR Color);
    void DrawAxisAlignedBox(FXMVECTOR A, FXMVECTOR B, FXMVECTOR Color);
    void DrawRing(FXMMATRIX matWorld, FXMVECTOR Axis1, FXMVECTOR Axis2, UINT32 SegmentCount, CXMVECTOR Color);
    void DrawRingXZ(FXMMATRIX matWorld, FLOAT Radius, UINT32 SegmentCount, FXMVECTOR Color);
    void DrawGrid(FXMMATRIX matWorld, FXMVECTOR Axis1, FXMVECTOR Axis2, UINT32 DivisionCount1, UINT32 DivisionCount2, CXMVECTOR Color);
    void DrawGridXZ(FXMMATRIX matWorld, FLOAT Size, UINT32 DivisionCount, FXMVECTOR Color);
    void DrawFrustum(const Math::Frustum& frustum, FXMMATRIX matWorld, FXMVECTOR Color);
    void DrawFrustum(const DirectX::BoundingFrustum& frustum, FXMMATRIX matWorld, FXMVECTOR Color);

    void AddLineList(FXMMATRIX matWorld, UINT32 LineCount, const XMFLOAT3* pPositions, const XMFLOAT4* pColors, bool SingleColor = true);
    void AddLineStrip(FXMMATRIX matWorld, UINT32 LineCount, const XMFLOAT3* pPositions, const XMFLOAT4* pColors, bool SingleColor = true);
    void AddLineListIndexed(FXMMATRIX matWorld, 
                            UINT32 VertexCount, 
                            const XMFLOAT3* pPositions, 
                            const XMFLOAT4* pColors, 
                            UINT32 IndexCount, 
                            const USHORT* pIndices, 
                            bool SingleColor = true);
};

