#include "pch.h"
#include "GridTerrain.h"
#include <algorithm>
#include "BulletPhysics.h"

#include "CompiledShaders\GridTerrainVS.h"
#include "CompiledShaders\GridTerrainPS.h"
#include "CompiledShaders\GridTerrainWaterVS.h"
#include "CompiledShaders\GridTerrainWaterPS.h"

volatile UINT32 GridBlock::m_BlockCount = 0;

static const XMVECTOR g_LODColors[12] =
{
    { 1, 0, 0, 1 },
    { 1, 0.5f, 0, 1 },
    { 1, 1, 0, 1 },
    { 0.5f, 1, 0, 1 },
    { 0, 1, 0, 1 },
    { 0, 1, 0.5f, 1 },
    { 0, 1, 1, 1 },
    { 0, 0.5f, 1, 1 },
    { 0, 0, 1, 1 },
    { 0.5f, 0, 1, 1 },
    { 1, 0, 1, 1 },
    { 1, 0, 0.5f, 1 },
};

HRESULT GridBlock::Initialize(const GridTerrainConfig* pConfig, GridBlockCoord Coord, GridBlock* pParent, UINT32 QuadrantOfParent, const TerrainFeaturesBlock* pFeaturesBlock, bool SynchronousGeometry)
{
    m_pConfig = pConfig;
    m_State = Initializing;
    m_Coord = Coord;
    m_QuadrantOfParent = QuadrantOfParent;
    m_LastSeenTime = 0;
    m_LastFrameRendered = 0;
    m_pParent = pParent;
    assert(pFeaturesBlock != nullptr);
    m_pFeaturesBlock = pFeaturesBlock;

    if (m_pParent == nullptr)
    {
        assert(m_QuadrantOfParent == -1);
    }
    else
    {
        assert(m_QuadrantOfParent < 4);
    }

    // Add a refcount before starting the asynchronous build geometry task:
    AddRef();

    if (SynchronousGeometry)
    {
        BuildGeometry(pConfig);
    }
    else
    {
        assert(FALSE);
        //g_JobQueue.AddJobSimple((JobWorkerFunction)BuildGeometryWorkerFunction, this, (void*)pConfig);
    }

    return S_OK;
}

void GridBlock::BuildGeometry(const GridTerrainConfig* pConfig)
{
    assert(m_State == Initializing);
    //assert(m_pFeaturesBlock->CanMakeTerrain());
    //assert(m_pFeaturesBlock->GetConfig() == pConfig);

    const INT32 GridVertexEdgeCount = pConfig->GetBlockVertexCount();
    UINT32 VertexCount = (GridVertexEdgeCount + 1) * (GridVertexEdgeCount + 1);

    const bool PositionOnly = (pConfig->pPhysicsWorld != nullptr);

    SIZE_T VertexSizeBytes = PositionOnly ? sizeof(FLOAT) : sizeof(GridVertex);
    SIZE_T RowSizeBytes = VertexSizeBytes * (GridVertexEdgeCount + 1);
    m_pVertexData = malloc(VertexSizeBytes * VertexCount);
    assert(m_pVertexData != nullptr);

    GridVertex* pVerts = nullptr;
    if (!PositionOnly)
    {
        pVerts = (GridVertex*)m_pVertexData;
    }
    GridVertex* pRow = pVerts;
    GridVertex* pNormalCenterRow = nullptr;
    GridVertex* vNormalNegZRow = nullptr;

    FLOAT* pPositionVerts = nullptr;
    if (PositionOnly)
    {
        pPositionVerts = (FLOAT*)m_pVertexData;
    }
    FLOAT* pPositionRow = pPositionVerts;

    const FLOAT PhysicsHeightScale = (FLOAT)GridVertexEdgeCount / (FLOAT)(1 << m_Coord.SizeShift);
    const FLOAT InvGridEdgeCount = 1.0f / (FLOAT)GridVertexEdgeCount;
    const FLOAT TangentGridSize = InvGridEdgeCount * (FLOAT)(2 << m_Coord.SizeShift);
    XMVECTOR NormalizedXZPos = g_XMZero;

    const XMVECTOR SizeColor = XMColorHSVToRGB(XMVectorSet((FLOAT)(m_Coord.SizeShift % 10) * 0.1f, 1, 1, 1));

    const XMVECTOR CornerXYPos = XMVectorSwizzle<0, 2, 1, 3>(m_Coord.GetMin());
    const XMVECTOR BlockScale = m_Coord.GetScale();
    XMVECTOR RelativePos = XMVectorZero();

    XMVECTOR Min = g_XMFltMax;
    XMVECTOR Max = -g_XMFltMax;

    XMVECTOR PreviousStartRowHeight = g_XMZero;
    XMVECTOR CurrentStartRowHeight = g_XMZero;
    XMVECTOR PreviousEndRowHeight = g_XMZero;
    FLOAT* pPrevTopRowHeights = nullptr;
    if (!PositionOnly)
    {
        pPrevTopRowHeights = new FLOAT[GridVertexEdgeCount + 1];
    }

//     TerrainCornerData CornerData;
//     m_pFeaturesBlock->MakeTerrainCornerData(CornerXYPos, &CornerData);

    for (INT32 y = -1; y <= (GridVertexEdgeCount + 1); ++y)
    {
        const FLOAT YPos = (FLOAT)y * InvGridEdgeCount;
        NormalizedXZPos = XMVectorSetY(NormalizedXZPos, YPos);
        const bool StoreThisRow = (y >= 0 && y <= GridVertexEdgeCount);
        for (INT32 x = -1; x <= (GridVertexEdgeCount + 1); ++x)
        {
            const bool StoreThisColumn = (x >= 0 && x <= GridVertexEdgeCount);

            const FLOAT XPos = (FLOAT)x * InvGridEdgeCount;
            NormalizedXZPos = XMVectorSetX(NormalizedXZPos, XPos);
            XMVECTOR RelativeXYPos = NormalizedXZPos * BlockScale;
            RelativeXYPos = XMVectorSelect(g_XMZero, RelativeXYPos, g_XMSelect1100);

            //TerrainPointResult Result;
            //m_pFeaturesBlock->MakeTerrain(&CornerData, RelativeXYPos, &Result);
            //Result.TerrainHeight = XMVectorReplicate(5);
            //Result.TerrainHeight = XMVector2LengthEst(CornerXYPos + RelativeXYPos);
            const XMVECTOR TerrainHeight = XMVectorZero();
            const FLOAT TerrainHeightValue = XMVectorGetX(TerrainHeight);

            FLOAT* pDestHeight = nullptr;
            GridVertex* pDest = nullptr;
            if (PositionOnly)
            {
                pDestHeight = &pPositionRow[x];
            }
            else
            {
                pDest = &pRow[x];
            }

            if (StoreThisRow)
            {
                if (StoreThisColumn)
                {
                    if (pDest != nullptr)
                    {
                        pDest->PosY = TerrainHeightValue;
                        //XMStoreUByteN4(&pDest->TextureBlend, Result.TextureBlend);
                        XMStoreUByteN4(&pDest->TextureBlend, XMVectorSet(1, 0, 0, 0));
                    }
                    else
                    {
                        *pDestHeight = TerrainHeightValue * PhysicsHeightScale;
                    }

                    Min = XMVectorMin(Min, XMVectorSplatX(TerrainHeight));
                    Max = XMVectorMax(Max, XMVectorSplatX(TerrainHeight));
                }
                else if (x < 0)
                {
                    PreviousStartRowHeight = CurrentStartRowHeight;
                    CurrentStartRowHeight = TerrainHeight;
                }
                else
                {
                    PreviousEndRowHeight = TerrainHeight;
                }
            }
            else if (y < 0 && StoreThisColumn)
            {
                if (!PositionOnly)
                {
                    pPrevTopRowHeights[x] = TerrainHeightValue;
                }
            }
            else if (x < 0)
            {
                PreviousStartRowHeight = CurrentStartRowHeight;
                CurrentStartRowHeight = TerrainHeight;
            }

            // Compute normals for the previous row:
            if (!PositionOnly && StoreThisColumn && y >= 1)
            {
                assert(pNormalCenterRow != nullptr);
                GridVertex& NormalDest = pNormalCenterRow[x];
                const FLOAT CenterHeight = NormalDest.PosY;
                const FLOAT PosZHeight = TerrainHeightValue;
                FLOAT NegZHeight;
                if (y > 1)
                {
                    assert(vNormalNegZRow != nullptr);
                    NegZHeight = vNormalNegZRow[x].PosY;
                }
                else
                {
                    NegZHeight = pPrevTopRowHeights[x];
                }
                FLOAT PosXHeight = pNormalCenterRow[x + 1].PosY;
                if (x == GridVertexEdgeCount)
                {
                    PosXHeight = XMVectorGetX(PreviousEndRowHeight);
                }
                FLOAT NegXHeight;
                if (x == 0)
                {
                    NegXHeight = XMVectorGetX(PreviousStartRowHeight);
                }
                else
                {
                    NegXHeight = pNormalCenterRow[x - 1].PosY;
                }
                XMVECTOR vTangentX = XMVectorSet(TangentGridSize, PosXHeight - NegXHeight, 0, 0);
                vTangentX = XMVector3Normalize(vTangentX);
                XMVECTOR vTangentZ = XMVectorSet(0, PosZHeight - NegZHeight, TangentGridSize, 0);
                vTangentZ = XMVector3Normalize(vTangentZ);
                XMVECTOR vNormal = XMVector3Cross(vTangentZ, vTangentX);
                vNormal = (vNormal * g_XMOneHalf) + g_XMOneHalf;
                XMStoreUDecN4(&NormalDest.Normal, vNormal);
            }
        }

        if (y >= 0)
        {
            vNormalNegZRow = pNormalCenterRow;
            pNormalCenterRow = pRow;
            if (PositionOnly)
            {
                pPositionRow += (GridVertexEdgeCount + 1);
            }
            else
            {
                pRow += (GridVertexEdgeCount + 1);
            }
        }
    }

    m_MinHeight = XMVectorGetY(Min);
    m_MaxHeight = XMVectorGetY(Max);

    if (pConfig->pd3dDevice != nullptr)
    {
        m_VB.Create(L"TerrainBlock", VertexCount, sizeof(GridVertex), m_pVertexData);
        BuildDetailGeometry(pConfig);
        //free(m_pVertexData);
        //m_pVertexData = nullptr;
    }
    else
    {
        const FLOAT GridScale = (FLOAT)(1 << m_Coord.SizeShift) / (FLOAT)GridVertexEdgeCount;
        m_pCollisionShape = CollisionShape::CreateHeightfield((const FLOAT*)m_pVertexData, 
                                                              GridVertexEdgeCount + 1, 
                                                              GridVertexEdgeCount + 1, 
                                                              m_MinHeight * PhysicsHeightScale, 
                                                              m_MaxHeight * PhysicsHeightScale);
        m_pCollisionShape->SetUniformScale(GridScale);
        const XMVECTOR CoordMin = m_Coord.GetMin();
        const XMVECTOR CoordMax = m_Coord.GetMax();
        XMVECTOR Pos = (CoordMin + CoordMax) * g_XMOneHalf;
        Pos = XMVectorSetY(Pos, (m_MaxHeight + m_MinHeight) * 0.5f * GridScale * PhysicsHeightScale);
        XMMATRIX matTransform = XMMatrixTranslationFromVector(Pos);
        m_pRigidBody = new RigidBody(m_pCollisionShape, 0, matTransform);
        //pConfig->pPhysicsWorld->AddRigidBodyOffThread(m_pRigidBody, false);

        if (m_MinHeight < pConfig->WaterLevelYpos)
        {
            XMVECTOR WaterCoordMax = XMVectorSetY(CoordMax, pConfig->WaterLevelYpos);
            XMVECTOR WaterCoordMin = XMVectorSetY(CoordMin, m_MinHeight);
            XMVECTOR WaterBlockHalfSize = (WaterCoordMax - WaterCoordMin) * g_XMOneHalf;
            XMVECTOR WaterBlockCenter = (WaterCoordMax + WaterCoordMin) * g_XMOneHalf;
            matTransform = XMMatrixTranslationFromVector(WaterBlockCenter);
            m_pWaterCollisionShape = CollisionShape::CreateBox(WaterBlockHalfSize);
            m_pWaterRigidBody = new RigidBody(m_pWaterCollisionShape, 0, matTransform);
            //pConfig->pPhysicsWorld->AddRigidBodyOffThread(m_pWaterRigidBody, true);
        }
    }

    if (pPrevTopRowHeights != nullptr)
    {
        delete[] pPrevTopRowHeights;
    }

    __faststorefence();
    m_State = Initialized;
    __faststorefence();

    // Initialize added an extra refcount before starting the build geometry task:
    Release();
}    

inline const GridVertex* GridVertexLerp(FXMVECTOR NormPos, const GridVertex* pVerts, UINT32 RowWidth, FLOAT* pLerpPosY, XMVECTOR* pLerpNormal)
{
    const UINT32 RowPitch = RowWidth + 1;
    XMVECTOR GridExactPos = NormPos * XMVectorReplicate((FLOAT)RowWidth);
    XMVECTOR GridPos = XMVectorFloor(GridExactPos);
    XMVECTOR FracPos = GridExactPos - GridPos;
    const FLOAT LerpX = XMVectorGetX(FracPos);
    const FLOAT LerpZ = XMVectorGetZ(FracPos);
    UINT32 GridX = (UINT32)XMVectorGetX(GridPos);
    assert(GridX < RowWidth);
    UINT32 GridY = (UINT32)XMVectorGetZ(GridPos);
    assert(GridY < RowWidth);

    const GridVertex& GV00 = pVerts[GridY * RowPitch + GridX];
    const GridVertex& GV10 = pVerts[GridY * RowPitch + GridX + 1];
    const GridVertex& GV01 = pVerts[(GridY + 1) * RowPitch + GridX];
    const GridVertex& GV11 = pVerts[(GridY + 1) * RowPitch + GridX + 1];

    FLOAT Pos0 = LerpX * GV10.PosY + (1 - LerpX) * GV00.PosY;
    FLOAT Pos1 = LerpX * GV11.PosY + (1 - LerpX) * GV01.PosY;
    *pLerpPosY = LerpZ * Pos1 + (1 - LerpZ) * Pos0;

    XMVECTOR Norm00 = XMLoadUDecN4(&GV00.Normal);
    XMVECTOR Norm10 = XMLoadUDecN4(&GV10.Normal);
    XMVECTOR Norm01 = XMLoadUDecN4(&GV01.Normal);
    XMVECTOR Norm11 = XMLoadUDecN4(&GV11.Normal);

    XMVECTOR Norm0 = XMVectorLerp(Norm00, Norm10, LerpX);
    XMVECTOR Norm1 = XMVectorLerp(Norm01, Norm11, LerpX);
    XMVECTOR Norm = XMVectorLerp(Norm0, Norm1, LerpZ);
    *pLerpNormal = XMVector3Normalize(Norm * g_XMTwo - g_XMOne);

    return &GV00;
}

/*
inline UINT32 GenerateRandomDecorations(UINT32 SpotCount, CXMVECTOR TexUVWH, FLOAT MinSize, FLOAT MaxSize, const GridTerrainConfig* pConfig, void* m_pVertexData, DecorationInstanceVertexList &InstanceVerts, RandomGenerator& Random)
{
    FLOAT WaterLevel = pConfig->WaterLevelYpos;

    const GridVertex* pVerts = (const GridVertex*)m_pVertexData;
    const UINT32 RowWidth = pConfig->GetBlockVertexCount();
    const UINT32 RowPitch = pConfig->GetBlockVertexCount() + 1;
    const XMVECTOR GridSize = XMVectorReplicate((FLOAT)RowWidth);

    UINT32 AddedCount = 0;

    for (UINT32 i = 0; i < SpotCount; ++i)
    {
        XMVECTOR NormPos = Random.GetUNormVector3();
        NormPos = XMVectorSelect(g_XMZero, NormPos, g_XMSelect1011);

        FLOAT GroundPosY;
        XMVECTOR GroundNormal;
        const GridVertex* pGV = GridVertexLerp(NormPos, pVerts, RowWidth, &GroundPosY, &GroundNormal);

        if (GroundPosY < WaterLevel)
        {
            continue;
        }

        XMVECTOR GroundDotUp = XMVector3Dot(GroundNormal, g_XMIdentityR1);
        const FLOAT DotValue = XMVectorGetX(GroundDotUp);

        FLOAT GrassAmount = 0.0f;
        const FLOAT BlendLerp = (FLOAT)pGV->TextureBlend.x / 255.0f;
        if (pGV->TextureBlend.z == TerrainTextureLayer_Grass)
        {
            GrassAmount = BlendLerp;
        }
        if (pGV->TextureBlend.w == TerrainTextureLayer_Grass)
        {
            GrassAmount = max(GrassAmount, 1.0f - BlendLerp);
        }

        if (GrassAmount <= 0.0f)
        {
            continue;
        }

        GrassAmount = (GrassAmount * GrassAmount) * powf(DotValue, 10);
        if (GrassAmount < 1.0f)
        {
            FLOAT Discard = Random.GetUNormFloat();
            if (Discard >= GrassAmount)
            {
                continue;
            }
        }

        XMVECTOR Orientation;
        if (DotValue >= -0.5f && DotValue < 0.99f)
        {
            FLOAT Angle = acosf(DotValue);
            XMVECTOR Axis = XMVector3Normalize(XMVector3Cross(g_XMIdentityR1, GroundNormal));
            Orientation = XMQuaternionRotationNormal(Axis, Angle);
        }
        else
        {
            Orientation = XMQuaternionIdentity();
        }
        XMVECTOR YRotation = XMQuaternionRotationRollPitchYaw(0, Random.GetUNormFloat() * XM_2PI, 0);
        Orientation = XMQuaternionMultiply(YRotation, Orientation);

        NormPos = XMVectorSetY(NormPos, GroundPosY);
        NormPos = XMVectorSetW(NormPos, Random.GetRangedFloat(MinSize, MaxSize));

        DecorationInstanceVertex IV;
        XMStoreHalf4(&IV.PositionScale, NormPos);
        XMStoreHalf4(&IV.Orientation, Orientation);

        // TODO
        XMStoreUByteN4(&IV.TexCoordUVWH, TexUVWH);
        FLOAT Lightness = Random.GetRangedFloat(0.8f, 1.0f);
        XMVECTOR Intensity = XMVectorSet(Lightness, Lightness, Lightness, 1);
        Intensity = g_XMOne;
        XMStoreUByteN4(&IV.BottomColor, g_XMOne * Intensity);
        XMStoreUByteN4(&IV.TopColor, g_XMOne * Intensity);

        InstanceVerts.push_back(IV);
        ++AddedCount;
    }

    return AddedCount;
}

void GridBlock::AddDecorationSet(const DecorationSet& DSet)
{
    if (DSet.InstanceCount == 0)
    {
        return;
    }
    for (UINT32 i = 0; i < ARRAYSIZE(m_DecorationSets); ++i)
    {
        if (m_DecorationSets[i].InstanceCount == 0)
        {
            m_DecorationSets[i] = DSet;
            return;
        }
    }
}

void GridBlock::SetDecorationLOD(FLOAT MinDistance, FLOAT MaxDistance, FLOAT Blend)
{
    if (MinDistance <= 0)
    {
        MinDistance = -2.0f;
    }
    m_LODConstants.x = (MinDistance + MaxDistance) * 0.5f;
    m_LODConstants.y = (MaxDistance - MinDistance) * 0.5f;
    m_LODConstants.z = Blend;
}

void GridBlock::BuildItemInstances(const GridTerrainConfig* pConfig)
{
    pConfig->pGridTerrain->BuildInstanceMap(m_Instances);
    if (m_Instances.empty())
    {
        return;
    }

    const RECT BlockRect = m_Coord.GetRect();

    DecorationItemInstances* pInstanceList = nullptr;

    const TerrainDecorationObjectVector& Objects = m_pFeaturesBlock->GetObjects();
    const UINT32 ObjectCount = (UINT32)Objects.size();
    for (UINT32 i = 0; i < ObjectCount; ++i)
    {
        const TerrainDecorationObject& TDO = Objects[i];

        INT32 X, Z;
        TDO.GetIntPosition(&X, &Z);
        if (X < BlockRect.left || X >= BlockRect.right ||
            Z < BlockRect.top || Z >= BlockRect.bottom)
        {
            continue;
        }

        FindInstanceList(TDO.ItemName, &pInstanceList);
        assert(pInstanceList != nullptr);

        if (pConfig->pd3dDevice != nullptr)
        {
            XMMATRIX matTransform = ComposeMatrix(XMLoadFloat3(&TDO.Position), TDO.Scale, XMLoadFloat4(&TDO.Orientation));
            XMFLOAT4X4 Transform;
            XMStoreFloat4x4(&Transform, matTransform);
            pInstanceList->Transforms.push_back(Transform);
            pInstanceList->pModel->NeedsUpdate = true;
            pInstanceList->pModel->InstanceCount++;
        }
        else
        { 
            Item* pItem = pInstanceList->pModel->pItem;
            ItemTransform IT = IT.FromComponents(XMLoadFloat3(&TDO.Position), TDO.Scale, XMLoadFloat4(&TDO.Orientation));
            pConfig->pWorld->EnterLock();
            ItemInstance* pII = pConfig->pWorld->SpawnItemInstance(pItem, L"", IT);
            pConfig->pWorld->LeaveLock();
            pInstanceList->ItemInstances.push_back(pII);
        }
    }
}

void GridBlock::FindInstanceList(const StringID& ItemName, DecorationItemInstances** ppInstanceList)
{
    if (*ppInstanceList != nullptr && (*ppInstanceList)->pModel->ItemName == ItemName)
    {
        return;
    }

    auto iter = m_Instances.find(ItemName);
    if (iter == m_Instances.end())
    {
        *ppInstanceList = nullptr;
    }
    *ppInstanceList = iter->second;
}

inline UINT32 GenerateGrass(UINT32 TotalCount, bool CloseUpMix, FLOAT MinSize, FLOAT MaxSize, const GridTerrainConfig* pConfig, void* m_pVertexData, DecorationInstanceVertexList &InstanceVerts)
{
    static const XMVECTOR GrassRect = XMVectorSet(0.5f, 0.76f, 0.25f, 0.24f);
    UINT32 GrassCount = 0;
    static const XMVECTOR Crab0Rect = XMVectorSet(0.0f, 0.01f, 0.25f, 0.49f);
    UINT32 Crab0Count = 0;
    static const XMVECTOR Crab1Rect = XMVectorSet(0.25f, 0.01f, 0.25f, 0.49f);
    UINT32 Crab1Count = 0;
    static const XMVECTOR Crab2Rect = XMVectorSet(0.0f, 0.51f, 0.25f, 0.49f);
    UINT32 Crab2Count = 0;
    static const XMVECTOR Crab3Rect = XMVectorSet(0.25f, 0.51f, 0.25f, 0.49f);
    UINT32 Crab3Count = 0;
    static const XMVECTOR Weed0Rect = XMVectorSet(0.5f, 0.0f, 0.25f, 0.25f);
    UINT32 Weed0Count = 0;
    static const XMVECTOR Weed1Rect = XMVectorSet(0.5f, 0.5f, 0.25f, 0.25f);
    UINT32 Weed1Count = 0;

    if (CloseUpMix)
    {
        Weed0Count = (UINT32)((FLOAT)TotalCount * 0.05f);
        Weed1Count = (UINT32)((FLOAT)TotalCount * 0.05f);
    }
    Crab0Count = (UINT32)((FLOAT)TotalCount * 0.1f);
    Crab1Count = (UINT32)((FLOAT)TotalCount * 0.1f);
    Crab2Count = (UINT32)((FLOAT)TotalCount * 0.1f);
    Crab3Count = (UINT32)((FLOAT)TotalCount * 0.1f);
    GrassCount = TotalCount - (Weed0Count + Weed1Count + Crab0Count + Crab1Count + Crab2Count + Crab3Count);

    RandomGenerator Random(12345);
    
    UINT32 ResultCount = 0;
    if (Weed0Count > 0)
    {
        ResultCount += GenerateRandomDecorations(Weed0Count, Weed0Rect, MinSize * 0.5f, MaxSize * 0.5f, pConfig, m_pVertexData, InstanceVerts, Random);
    }
    if (Weed1Count > 0)
    {
        ResultCount += GenerateRandomDecorations(Weed1Count, Weed1Rect, MinSize * 0.5f, MaxSize * 0.5f, pConfig, m_pVertexData, InstanceVerts, Random);
    }
    if (Crab0Count > 0)
    {
        ResultCount += GenerateRandomDecorations(Crab0Count, Crab0Rect, MinSize, MaxSize * 1.5f, pConfig, m_pVertexData, InstanceVerts, Random);
    }
    if (Crab1Count > 0)
    {
        ResultCount += GenerateRandomDecorations(Crab1Count, Crab1Rect, MinSize, MaxSize * 1.5f, pConfig, m_pVertexData, InstanceVerts, Random);
    }
    if (Crab2Count > 0)
    {
        ResultCount += GenerateRandomDecorations(Crab2Count, Crab2Rect, MinSize, MaxSize * 1.5f, pConfig, m_pVertexData, InstanceVerts, Random);
    }
    if (Crab3Count > 0)
    {
        ResultCount += GenerateRandomDecorations(Crab3Count, Crab3Rect, MinSize, MaxSize * 1.5f, pConfig, m_pVertexData, InstanceVerts, Random);
    }
    if (GrassCount > 0)
    {
        ResultCount += GenerateRandomDecorations(GrassCount, GrassRect, MinSize, MaxSize, pConfig, m_pVertexData, InstanceVerts, Random);
    }
    return ResultCount;
}

void GridBlock::BuildDetailGeometry(const GridTerrainConfig* pConfig)
{
    assert(m_pVertexData != nullptr);
    assert(pConfig->pd3dDevice != nullptr);
    
    if (m_MaxHeight < pConfig->WaterLevelYpos)
    {
        return;
    }

    DecorationInstanceVertexList& InstanceVerts = m_InstanceVerts;

    DecorationSet DS = {};


    switch (m_Coord.SizeShift)
    {
    case 8:
    {
        SetDecorationLOD(150, 1500, 0.05f);
        DS.InstanceCount = GenerateGrass(8000, false, 0.7f, 0.7f, pConfig, m_pVertexData, InstanceVerts);
        DS.Type = DecorationModelType_Grass;
        AddDecorationSet(DS);
        BuildItemInstances(pConfig);
        break;
    }
//     case 8:
//     {
//         SetDecorationLOD(200, 1500, 0.1f);
//         DS.InstanceCount = GenerateGrass(8000, false, 1, 1, pConfig, m_pVertexData, InstanceVerts);
//         DS.Type = DecorationModelType_DistantGrass;
//         AddDecorationSet(DS);
//         break;
//     }
//     case 7:
//     {
//         SetDecorationLOD(150, 500, 0.1f);
//         DS.InstanceCount = GenerateGrass(2000, false, 0.5f, 0.5f, pConfig, m_pVertexData, InstanceVerts);
//         DS.Type = DecorationModelType_DistantGrass;
//         AddDecorationSet(DS);
//         break;
//     }
    case 6:
    {
        SetDecorationLOD(0, 200, 0.05f);
        DS.InstanceCount = GenerateGrass(4000, false, 0.4f, 0.6f, pConfig, m_pVertexData, InstanceVerts);
        DS.Type = DecorationModelType_Grass;
        AddDecorationSet(DS);
        break;
    }
    case 5:
    {
        SetDecorationLOD(0, 150, 0.1f);
        DS.InstanceCount = GenerateGrass(1000, true, 0.4f, 0.6f, pConfig, m_pVertexData, InstanceVerts);
        DS.Type = DecorationModelType_Grass;
        AddDecorationSet(DS);
        break;
    }
    case 4:
    {
        SetDecorationLOD(0, 50, 0.1f);
        DS.InstanceCount = GenerateGrass(400, true, 0.4f, 0.6f, pConfig, m_pVertexData, InstanceVerts);
        DS.Type = DecorationModelType_Grass;
        AddDecorationSet(DS);
        break;
    }
    case 3:
    {
        SetDecorationLOD(0, 20, 0.25f);
        DS.InstanceCount = GenerateGrass(200, true, 0.4f, 0.6f, pConfig, m_pVertexData, InstanceVerts);
        DS.Type = DecorationModelType_Grass;
        AddDecorationSet(DS);
        break;
    }
    //     case 3:
//     {
//         m_DecorationLOD = 30.0f;
//         DS.InstanceCount = GenerateRandomDecorations(200, XMVectorSet(0, 0, 0.4375f, 0.5f), 0.2f, 0.4f, pConfig, m_pVertexData, InstanceVerts);
//         DS.Type = DecorationModelType_SmallGrass;
//         AddDecorationSet(DS);
//         break;
//     }
    }

    const UINT32 InstanceCount = (UINT32)InstanceVerts.size();
    if (InstanceCount > 0)
    {
        m_pDecorationInstanceVB = SimpleResources11::CreateVertexBuffer(pConfig->pd3dDevice, InstanceCount * sizeof(DecorationInstanceVertex), FALSE, &InstanceVerts[0]);
    }
}
*/

void GridBlock::Terminate()
{
    TerminateInstances();

    for (UINT32 i = 0; i < ARRAYSIZE(m_pChildren); ++i)
    {
        if (m_pChildren[i] != nullptr)
        {
            m_pChildren[i]->Release();
            m_pChildren[i] = nullptr;
        }
    }

    m_VB.Destroy();
    //SAFE_RELEASE(m_pDecorationInstanceVB);
    assert(m_pRigidBody == nullptr);
    assert(m_pCollisionShape == nullptr);
    assert(m_pWaterRigidBody == nullptr);
    assert(m_pWaterCollisionShape == nullptr);
    if (m_pVertexData != nullptr)
    {
        free(m_pVertexData);
        m_pVertexData = nullptr;
    }
    m_State = Initializing;
    m_pParent = nullptr;
}

void GridBlock::TerminatePhysics(PhysicsWorld* pWorld)
{
    if (pWorld == nullptr)
    {
        return;
    }
    if (m_pRigidBody != nullptr)
    {
        pWorld->RemoveRigidBody(m_pRigidBody);
        delete m_pRigidBody;
        m_pRigidBody = nullptr;
    }
    if (m_pWaterRigidBody != nullptr)
    {
        pWorld->RemoveRigidBody(m_pWaterRigidBody);
        delete m_pWaterRigidBody;
        m_pWaterRigidBody = nullptr;
    }
    if (m_pCollisionShape != nullptr)
    {
        delete m_pCollisionShape;
        m_pCollisionShape = nullptr;
    }
    if (m_pWaterCollisionShape != nullptr)
    {
        delete m_pWaterCollisionShape;
        m_pWaterCollisionShape = nullptr;
    }
    if (m_pVertexData != nullptr)
    {
        free(m_pVertexData);
        m_pVertexData = nullptr;
    }
}

void GridBlock::Render(const GridTerrainRender& GTR, LinearAllocator* pCBAllocator, const D3D12_INDEX_BUFFER_VIEW& IBV, UINT32 IndexCount, const GridTerrainConfig* pConfig)
{
    GraphicsContext* pContext = GTR.pContext;
    XMMATRIX matWorld = GetTransform(GTR.vCameraOffset);
    DynAlloc DA = pCBAllocator->Allocate(sizeof(CBGridBlock));
    CBGridBlock* pCBBlock = (CBGridBlock*)DA.DataPtr;
    XMStoreFloat4x4(&pCBBlock->matWorld, matWorld);
    XMStoreFloat4x4(&pCBBlock->matViewProj, GTR.matVP);
    XMStoreFloat4(&pCBBlock->PositionToTexCoord, m_Coord.GetShaderOffsetScale());
    XMStoreFloat4(&pCBBlock->ModulateColor, g_LODColors[m_Coord.SizeShift % ARRAYSIZE(g_LODColors)]);
    pContext->SetConstantBuffer(0, DA.GpuAddress);

    UINT32 Stride = sizeof(GridVertex);
    UINT32 Offset = 0;
    D3D12_VERTEX_BUFFER_VIEW VBV = m_VB.VertexBufferView();
    pContext->SetVertexBuffer(0, VBV);
    pContext->SetIndexBuffer(IBV);
    pContext->DrawIndexed(IndexCount, 0, 0);

    if (0 && m_pVertexData != nullptr && pConfig != nullptr && m_Coord.SizeShift < 5)
    {
        const GridVertex* pVerts = (const GridVertex*)m_pVertexData;
        FLOAT InvGridSize = 1.0f / (FLOAT)pConfig->GetBlockVertexCount();
        const UINT32 GridSize = pConfig->GetBlockVertexCount() + 1;
        XMVECTOR Min = m_Coord.GetMin();
        XMVECTOR Max = m_Coord.GetMax();
        XMVECTOR Delta = Max - Min;
        for (UINT32 z = 0; z < GridSize; ++z)
        {
            XMVECTOR RowPos = Min + Delta * XMVectorSet(0, 0, (FLOAT)z * InvGridSize, 0);
            for (UINT32 x = 0; x < GridSize; ++x)
            {
                XMVECTOR GridPos = RowPos + Delta * XMVectorSet((FLOAT)x * InvGridSize, 0, 0, 0);
                const GridVertex& GV = pVerts[z * GridSize + x];
                GridPos = XMVectorSetY(GridPos, GV.PosY);
                XMVECTOR Normal = XMLoadUDecN4(&GV.Normal) * g_XMTwo - g_XMOne;
                XMVECTOR NormPos = GridPos + Normal;
                //DebugDraw11::CacheLineSegment(g_XMOne, GridPos, NormPos, 0);
            }
        }
    }
}

// void GridBlock::RenderDecorations(const GridTerrainRender& GTR, ID3D11Buffer* pBlockCB, const GridTerrainConfig* pConfig, const DecorationModelSubset* pSubsets)
// {
//     ID3D11DeviceContext* pd3dContext = GTR.pd3dContext;
//     UINT32 InstanceOffset = 0;
//     for (UINT32 i = 0; i < ARRAYSIZE(m_DecorationSets); ++i)
//     {
//         const DecorationSet& Set = m_DecorationSets[i];
//         if (Set.InstanceCount == 0)
//         {
//             return;
//         }
// 
//         if (i == 0)
//         {
//             assert(m_pDecorationInstanceVB != nullptr);
//             XMMATRIX matWorld = GetTransform(GTR.vCameraOffset);
//             D3D11_MAPPED_SUBRESOURCE MapData = {};
//             pd3dContext->Map(pBlockCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &MapData);
//             CBGridBlock* pCBBlock = (CBGridBlock*)MapData.pData;
//             XMStoreFloat4x4(&pCBBlock->matWorld, matWorld);
//             XMStoreFloat4x4(&pCBBlock->matViewProj, GTR.matVP);
//             XMStoreFloat4(&pCBBlock->PositionToTexCoord, m_Coord.GetShaderOffsetScale());
//             XMStoreFloat4(&pCBBlock->ModulateColor, g_LODColors[m_Coord.SizeShift % ARRAYSIZE(g_LODColors)]);
//             pCBBlock->LODConstants = m_LODConstants;
//             pd3dContext->Unmap(pBlockCB, 0);
//             pd3dContext->VSSetConstantBuffers(0, 1, &pBlockCB);
//             pd3dContext->PSSetConstantBuffers(0, 1, &pBlockCB);
//         }
// 
//         const DecorationModelSubset& Subset = pSubsets[Set.Type];
//         pd3dContext->DrawIndexedInstanced(Subset.IndexCount, Set.InstanceCount, Subset.StartIndex, 0, InstanceOffset);
//         InstanceOffset += Set.InstanceCount;
//     }
// 
//     /*
//     if (pConfig != nullptr)
//     {
//         XMVECTOR Min = m_Coord.GetMin();
//         XMVECTOR Max = m_Coord.GetMax();
//         XMVECTOR Delta = Max - Min;
// 
//         for (UINT32 i = 0; i < m_DecorationInstanceCount; ++i)
//         {
//             DecorationInstanceVertex& IV = m_InstanceVerts[i];
//             XMVECTOR Position = XMLoadHalf4(&IV.PositionScale);
//             XMVECTOR WorldPos = Position * Delta + Min;
//             WorldPos = XMVectorSelect(Position, WorldPos, g_XMSelect1010);
//             XMVECTOR Orientation = XMLoadHalf4(&IV.Orientation);
// 
//             XMVECTOR NV = XMVectorSet(0, 0.5f, 0, 0);
// 
//             //XMMATRIX matRotation = XMMatrixRotationQuaternion(Orientation);
//             //XMVECTOR Normal = XMVector3TransformCoord(NV, matRotation);
// 
//             XMVECTOR NY = NV + g_XMTwo * XMVector3Cross(Orientation, XMVector3Cross(Orientation, NV) + XMVectorSplatW(Orientation) * NV);
//             NV = XMVectorSet(0.5f, 0, 0, 0);
//             XMVECTOR NX = NV + g_XMTwo * XMVector3Cross(Orientation, XMVector3Cross(Orientation, NV) + XMVectorSplatW(Orientation) * NV);
//             NV = XMVectorSet(0, 0, 0.5f, 0);
//             XMVECTOR NZ = NV + g_XMTwo * XMVector3Cross(Orientation, XMVector3Cross(Orientation, NV) + XMVectorSplatW(Orientation) * NV);
// 
//             DebugDraw11::CacheLineSegment(g_XMIdentityR1, WorldPos, WorldPos + NY, 0);
//             DebugDraw11::CacheLineSegment(g_XMIdentityR0, WorldPos, WorldPos + NX, 0);
//             DebugDraw11::CacheLineSegment(g_XMIdentityR2, WorldPos, WorldPos + NZ, 0);
//         }
//     */
// }

bool GridBlock::CheckForExpiration(UINT32 CutoffTickCount, const GridTerrainConfig* pConfig)
{
    if (m_pRigidBody != nullptr && m_pRigidBody->GetPhysicsWorld() == nullptr)
    {
        return false;
    }
    if (GetTime() <= CutoffTickCount)
    {
        TerminatePhysics(pConfig->pPhysicsWorld);
        Release();
        return true;
    }
    return false;
}

void GridBlock::TerminateInstances()
{
    /*
    if (m_Instances.empty())
    {
        return;
    }

    World* pWorld = m_pConfig->pWorld;
    GridTerrain* pGT = m_pConfig->pGridTerrain;

    auto iter = m_Instances.begin();
    auto end = m_Instances.end();
    while (iter != end)
    {
        DecorationItemInstances* pInst = iter->second;
        DecorationItem* pItem = pInst->pModel;
        pGT->EnterLock();
        pItem->InstanceCount -= (UINT32)pInst->Transforms.size();
        pItem->NeedsUpdate = true;
        pItem->Placements.erase(pInst);
        pGT->LeaveLock();
        pInst->pModel = nullptr;

        UINT32 IICount = (UINT32)pInst->ItemInstances.size();
        if (IICount > 0)
        {
            pWorld->EnterLock();
            for (UINT32 i = 0; i < IICount; ++i)
            {
                pWorld->DestroyItemInstance(pInst->ItemInstances[i]);
            }
            pWorld->LeaveLock();
        }

        delete pInst;

        ++iter;
    }

    m_Instances.clear();
    */
}

/*
inline UINT32 GenerateModelQuad(DecorationModelVertexList& VB, DecorationModelIndexList& IB, FLOAT YRotation, FLOAT AxisLength, FLOAT AxisOffset, FLOAT Height, FLOAT TangentOffsetBottom, FLOAT TangentOffsetTop, FLOAT UScale = 1.0f)
{
    const XMVECTOR Axis = XMVectorSet(sinf(YRotation), 0, cosf(YRotation), 0);
    const XMVECTOR Tangent = XMVectorSet(sinf(YRotation - XM_PIDIV2), 0, cosf(YRotation - XM_PIDIV2), 0);

    const USHORT BaseIndex = (USHORT)VB.size();
    DecorationModelVertex MV = {};

    XMVECTOR BottomCenter = Tangent * TangentOffsetBottom + Axis * AxisOffset;
    XMVECTOR TopCenter = XMVectorSet(0, Height, 0, 0) + Tangent * TangentOffsetTop + Axis * AxisOffset;
    XMVECTOR UpperLeft = TopCenter - Axis * AxisLength;
    XMVECTOR UpperRight = TopCenter + Axis * AxisLength;
    XMVECTOR LowerLeft = BottomCenter - Axis * AxisLength;
    XMVECTOR LowerRight = BottomCenter + Axis * AxisLength;

    XMVECTOR Normal = XMVector3Normalize(XMVector3Cross(UpperRight - UpperLeft, LowerLeft - UpperLeft));
    XMStoreHalf4(&MV.Normal, Normal);

    XMStoreHalf4(&MV.Position, UpperLeft);
    XMStoreUByteN4(&MV.TexCoord, XMVectorSet(0, 0, 0, 0));
    VB.push_back(MV);

    XMStoreHalf4(&MV.Position, UpperRight);
    XMStoreUByteN4(&MV.TexCoord, XMVectorSet(UScale, 0, 0, 0));
    VB.push_back(MV);

    XMStoreHalf4(&MV.Position, LowerLeft);
    XMStoreUByteN4(&MV.TexCoord, XMVectorSet(0, 1, 0, 0));
    VB.push_back(MV);

    XMStoreHalf4(&MV.Position, LowerRight);
    XMStoreUByteN4(&MV.TexCoord, XMVectorSet(UScale, 1, 0, 0));
    VB.push_back(MV);

    IB.push_back(BaseIndex + 0);
    IB.push_back(BaseIndex + 1);
    IB.push_back(BaseIndex + 2);
    IB.push_back(BaseIndex + 2);
    IB.push_back(BaseIndex + 1);
    IB.push_back(BaseIndex + 3);

    return 6;
}
*/

HRESULT GridTerrain::Initialize(const GridTerrainConfig* pConfig)
{
    if (pConfig == nullptr)
    {
        return E_INVALIDARG;
    }
    if (pConfig->BlockVertexShift < 1)
    {
        return E_INVALIDARG;
    }
    if (pConfig->FeaturesBlockShift <= pConfig->LargestBlockShift)
    {
        return E_INVALIDARG;
    }

    InitializeCriticalSection(&m_CritSec);
    m_Config = *pConfig;
    m_Config.pGridTerrain = this;
    if (m_Config.pd3dDevice != nullptr)
    {
        if (m_Config.pPhysicsWorld != nullptr)
        {
            return E_INVALIDARG;
        }
        m_Config.pd3dDevice->AddRef();
    }
    else if (m_Config.pPhysicsWorld != nullptr)
    {
        if (pConfig->LargestBlockShift != pConfig->SmallestBlockShift)
        {
            return E_INVALIDARG;
        }
    }
    m_LastBlockUpdateTime = 0;
    m_ExpirationThresholdMsec = 2000;
    ZeroMemory(m_EdgeIB, sizeof(m_EdgeIB));

    //BiomeInitialize();

    //m_pFeatures = new TerrainFeatures();
    //m_pFeatures->Initialize(&m_Config);

    if (m_Config.pd3dDevice != nullptr)
    {
        //StreamContext SC;
        //SC.pd3dDevice = m_Config.pd3dDevice;
        //wcscpy_s(SC.strBasePath, L"common\\");
        //StreamJobs::LoadTextureFromFile(&SC, "Terrain6.dds", &m_pTerrainTexture);
        //StreamJobs::LoadTextureFromFile(&SC, "decorations.dds", &m_pDecorationTexture);

        const UINT64 MaxIndexCount = ARRAYSIZE(m_EdgeIB) * 6Ui64 * (1Ui64 << (m_Config.BlockVertexShift * 2));
        UINT32* pIndices = new UINT32[MaxIndexCount];
        UINT32 IndexCount = 0;

        const INT32 GridVertexEdgeCount = m_Config.GetBlockVertexCount();
        assert(GridVertexEdgeCount % 2 == 0);

        for (UINT32 i = 0; i < ARRAYSIZE(m_EdgeIB); ++i)
        {
            const UINT32 StartIndexCount = IndexCount;
            m_EdgeIB[i].IBAddress = StartIndexCount * sizeof(UINT32);
            for (INT32 y = 0; y < GridVertexEdgeCount; y += 2)
            {
                const bool IsTopEdge = (y == 0);
                const bool IsBottomEdge = (y == (GridVertexEdgeCount - 2));

                const UINT32 R0FirstVertex = y * (GridVertexEdgeCount + 1);
                const UINT32 R1FirstVertex = R0FirstVertex + (GridVertexEdgeCount + 1);
                const UINT32 R2FirstVertex = R1FirstVertex + (GridVertexEdgeCount + 1);
                for (INT32 x = 0; x < GridVertexEdgeCount; x += 2)
                {
                    const bool IsLeftEdge = (x == 0);
                    const bool IsRightEdge = (x == (GridVertexEdgeCount - 2));

                    const UINT32 QuadTopVertex = R0FirstVertex + x;
                    const UINT32 QuadMidVertex = R1FirstVertex + x;
                    const UINT32 QuadBotVertex = R2FirstVertex + x;

                    if (IsTopEdge && (i & 0x1))
                    {
                        pIndices[IndexCount++] = QuadTopVertex;
                        pIndices[IndexCount++] = QuadMidVertex + 1;
                        pIndices[IndexCount++] = QuadTopVertex + 2;
                    }
                    else
                    {
                        pIndices[IndexCount++] = QuadTopVertex;
                        pIndices[IndexCount++] = QuadMidVertex + 1;
                        pIndices[IndexCount++] = QuadTopVertex + 1;
                        pIndices[IndexCount++] = QuadTopVertex + 1;
                        pIndices[IndexCount++] = QuadMidVertex + 1;
                        pIndices[IndexCount++] = QuadTopVertex + 2;
                    }

                    if (IsRightEdge && (i & 0x2))
                    {
                        pIndices[IndexCount++] = QuadTopVertex + 2;
                        pIndices[IndexCount++] = QuadMidVertex + 1;
                        pIndices[IndexCount++] = QuadBotVertex + 2;
                    }
                    else
                    {
                        pIndices[IndexCount++] = QuadTopVertex + 2;
                        pIndices[IndexCount++] = QuadMidVertex + 1;
                        pIndices[IndexCount++] = QuadMidVertex + 2;
                        pIndices[IndexCount++] = QuadMidVertex + 2;
                        pIndices[IndexCount++] = QuadMidVertex + 1;
                        pIndices[IndexCount++] = QuadBotVertex + 2;
                    }

                    if (IsBottomEdge && (i & 0x4))
                    {
                        pIndices[IndexCount++] = QuadBotVertex + 2;
                        pIndices[IndexCount++] = QuadMidVertex + 1;
                        pIndices[IndexCount++] = QuadBotVertex;
                    }
                    else
                    {
                        pIndices[IndexCount++] = QuadBotVertex + 2;
                        pIndices[IndexCount++] = QuadMidVertex + 1;
                        pIndices[IndexCount++] = QuadBotVertex + 1;
                        pIndices[IndexCount++] = QuadBotVertex + 1;
                        pIndices[IndexCount++] = QuadMidVertex + 1;
                        pIndices[IndexCount++] = QuadBotVertex;
                    }

                    if (IsLeftEdge && (i & 0x8))
                    {
                        pIndices[IndexCount++] = QuadBotVertex;
                        pIndices[IndexCount++] = QuadMidVertex + 1;
                        pIndices[IndexCount++] = QuadTopVertex;
                    }
                    else
                    {
                        pIndices[IndexCount++] = QuadTopVertex;
                        pIndices[IndexCount++] = QuadMidVertex;
                        pIndices[IndexCount++] = QuadMidVertex + 1;
                        pIndices[IndexCount++] = QuadMidVertex + 1;
                        pIndices[IndexCount++] = QuadMidVertex;
                        pIndices[IndexCount++] = QuadBotVertex;
                    }
                }
            }

            m_EdgeIB[i].IndexCount = IndexCount - StartIndexCount;
        }

        m_GridIB.Create(L"Grid Edge IB", IndexCount, sizeof(UINT32), pIndices);
        const UINT64 IBAddress = m_GridIB.GetGpuVirtualAddress();
        for (UINT32 i = 0; i < ARRAYSIZE(m_EdgeIB); ++i)
        {
            m_EdgeIB[i].IBAddress += IBAddress;
        }

        m_pCBAllocator = new LinearAllocator(kCpuWritable);

        /*
        m_OpaqueTerrainLayout = SimpleResources11::RegisterVertexLayout(GridVertexDesc, ARRAYSIZE(GridVertexDesc));
        m_pOpaqueTerrainLayout = SimpleResources11::FindOrCreateInputLayout(m_Config.pd3dDevice, m_OpaqueTerrainLayout, Shader_GridTerrainVS, sizeof(Shader_GridTerrainVS));

        m_Config.pd3dDevice->CreateVertexShader(Shader_GridTerrainVS, sizeof(Shader_GridTerrainVS), nullptr, &m_pOpaqueTerrainVS);
        m_Config.pd3dDevice->CreatePixelShader(Shader_GridTerrainPS, sizeof(Shader_GridTerrainPS), nullptr, &m_pOpaqueTerrainPS);
        m_pOpaqueTerrainMaterial = new ShaderMaterial(L"TerrainOpaque");
        m_pOpaqueTerrainMaterial->InitializeVSPS(Shader_GridTerrainVS, sizeof(Shader_GridTerrainVS), Shader_GridTerrainPS, sizeof(Shader_GridTerrainPS));

        m_WaterLayout = SimpleResources11::RegisterVertexLayout(GridVertexDesc, ARRAYSIZE(GridVertexDesc));
        m_pWaterLayout = SimpleResources11::FindOrCreateInputLayout(m_Config.pd3dDevice, m_WaterLayout, Shader_GridTerrainWaterVS, sizeof(Shader_GridTerrainWaterVS));

        m_Config.pd3dDevice->CreateVertexShader(Shader_GridTerrainWaterVS, sizeof(Shader_GridTerrainWaterVS), nullptr, &m_pWaterVS);
        m_Config.pd3dDevice->CreatePixelShader(Shader_GridTerrainWaterPS, sizeof(Shader_GridTerrainWaterPS), nullptr, &m_pWaterPS);
        m_pWaterMaterial = new ShaderMaterial(L"TerrainWater");
        m_pWaterMaterial->InitializeVSPS(Shader_GridTerrainWaterVS, sizeof(Shader_GridTerrainWaterVS), Shader_GridTerrainWaterPS, sizeof(Shader_GridTerrainWaterPS));

        m_pTerrainCB = SimpleResources11::CreateConstantBuffer(m_Config.pd3dDevice, sizeof(CBTerrain));
        m_pBlockCB = SimpleResources11::CreateConstantBuffer(m_Config.pd3dDevice, sizeof(CBGridBlock));

        m_pDirLightInvDirection = m_Config.pRootParams->FindParameter(L"vInverseLightDirection");
        m_pDirLightColor = m_Config.pRootParams->FindParameter(L"vDirectionalLightColor");
        m_pAmbientLightColor = m_Config.pRootParams->FindParameter(L"vAmbientLightColor");
        m_pCameraPosWorld = m_Config.pRootParams->FindParameter(L"vCameraPosWorld");

        m_pShadowSize0 = m_Config.pRootParams->FindParameter(L"vShadowSize0");
        m_pWorldToShadow0 = m_Config.pRootParams->FindParameter(L"matCCWorldToShadow0");
        m_pWorldToShadow1 = m_Config.pRootParams->FindParameter(L"matCCWorldToShadow1");
        m_pWorldToShadow2 = m_Config.pRootParams->FindParameter(L"matCCWorldToShadow2");

        m_pShadowMapTexture0 = m_Config.pRootParams->FindParameter(L"ShadowMap0");
        m_pShadowMapTexture1 = m_Config.pRootParams->FindParameter(L"ShadowMap1");
        m_pShadowMapTexture2 = m_Config.pRootParams->FindParameter(L"ShadowMap2");
        m_pShadowMapSampler = m_Config.pRootParams->FindParameter(L"ShadowMapSampler");
        */

        UINT32 VertexCount = (GridVertexEdgeCount + 1) * (GridVertexEdgeCount + 1);

        FixedGridVertex* pVerts = new FixedGridVertex[VertexCount];
        FixedGridVertex* pRow = pVerts;
        const FLOAT InvGridEdgeCount = 1.0f / (FLOAT)GridVertexEdgeCount;
        XMVECTOR NormalizedXZPos = g_XMZero;

        for (INT32 y = 0; y < (GridVertexEdgeCount + 1); ++y)
        {
            const FLOAT YPos = (FLOAT)y * InvGridEdgeCount;
            NormalizedXZPos = XMVectorSetY(NormalizedXZPos, YPos);
            for (INT32 x = 0; x < (GridVertexEdgeCount + 1); ++x)
            {
                const FLOAT XPos = (FLOAT)x * InvGridEdgeCount;
                NormalizedXZPos = XMVectorSetX(NormalizedXZPos, XPos);
                XMStoreHalf2(&pRow[x].PosXZ, NormalizedXZPos);
            }
            pRow += (GridVertexEdgeCount + 1);
        }

        m_GridBlockVB.Create(L"Grid Block VB", VertexCount, sizeof(FixedGridVertex), pVerts);
        delete[] pVerts;

        /*
        DecorationModelVertexList DecorationVB;
        DecorationModelIndexList DecorationIB;

        DecorationModelSubset& GrassModel = m_DecorationModels[DecorationModelType_Grass];
        GrassModel.StartIndex = (UINT32)DecorationIB.size();
        GrassModel.IndexCount = 0;
//         GrassModel.IndexCount += GenerateModelQuad(DecorationVB, DecorationIB, 0, 1.0f, 0, 1.0f, 0, 0);
//         GrassModel.IndexCount += GenerateModelQuad(DecorationVB, DecorationIB, XM_2PI / 3.0f, 1.0f, 0, 1.0f, 0, 0);
//         GrassModel.IndexCount += GenerateModelQuad(DecorationVB, DecorationIB, XM_2PI * (2.0f / 3.0f), 1.0f, 0, 1.0f, 0, 0);
        GrassModel.IndexCount += GenerateModelQuad(DecorationVB, DecorationIB, 0, 1.0f, 0, 1.0f, 1, 2);
        GrassModel.IndexCount += GenerateModelQuad(DecorationVB, DecorationIB, XM_2PI / 3.0f, 1.0f, 0, 1.0f, 1, 2);
        GrassModel.IndexCount += GenerateModelQuad(DecorationVB, DecorationIB, XM_2PI * (2.0f / 3.0f), 1.0f, 0, 1.0f, 1, 2);

        DecorationModelSubset& SmallGrassModel = m_DecorationModels[DecorationModelType_SmallGrass];
        SmallGrassModel.StartIndex = (UINT32)DecorationIB.size();
        SmallGrassModel.IndexCount = 0;
        SmallGrassModel.IndexCount += GenerateModelQuad(DecorationVB, DecorationIB, 0, 1.0f, 0.5f, 1.0f, 0.0f, 1.0f);
        SmallGrassModel.IndexCount += GenerateModelQuad(DecorationVB, DecorationIB, XM_2PI / 3.0f, 1.0f, 0.5f, 1.0f, 0.0f, 1.0f);
        SmallGrassModel.IndexCount += GenerateModelQuad(DecorationVB, DecorationIB, XM_2PI * (2.0f / 3.0f), 1.0f, 0.5f, 1.0f, 0.0f, 1.0f);

        DecorationModelSubset& DistantGrassModel = m_DecorationModels[DecorationModelType_DistantGrass];
        DistantGrassModel.StartIndex = (UINT32)DecorationIB.size();
        DistantGrassModel.IndexCount = 0;
        const FLOAT GrassWidth = 3.0f;
        const FLOAT GrassHeight = 1.0f;
        DistantGrassModel.IndexCount += GenerateModelQuad(DecorationVB, DecorationIB, 0, GrassWidth, 0, GrassHeight, GrassWidth, GrassWidth * 2, GrassWidth);
        DistantGrassModel.IndexCount += GenerateModelQuad(DecorationVB, DecorationIB, XM_2PI / 3.0f, GrassWidth, 0, GrassHeight, GrassWidth, GrassWidth * 2, GrassWidth);
        DistantGrassModel.IndexCount += GenerateModelQuad(DecorationVB, DecorationIB, XM_2PI * (2.0f / 3.0f), GrassWidth, 0, GrassHeight, GrassWidth, GrassWidth * 2, GrassWidth);

        m_pDecorationVB = SimpleResources11::CreateVertexBuffer(m_Config.pd3dDevice, (UINT32)DecorationVB.size() * sizeof(DecorationModelVertex), FALSE, &DecorationVB.front());
        m_pDecorationIB = SimpleResources11::CreateIndexBuffer(m_Config.pd3dDevice, (UINT32)DecorationIB.size(), FALSE, FALSE, &DecorationIB.front());

        m_DecorationLayout = SimpleResources11::RegisterVertexLayout(DecorationVertexDesc, ARRAYSIZE(DecorationVertexDesc));
        m_pDecorationLayout = SimpleResources11::FindOrCreateInputLayout(m_Config.pd3dDevice, m_DecorationLayout, Shader_GridTerrainDecorationVS, sizeof(Shader_GridTerrainDecorationVS));

        m_Config.pd3dDevice->CreateVertexShader(Shader_GridTerrainDecorationVS, sizeof(Shader_GridTerrainDecorationVS), nullptr, &m_pDecorationVS);
        m_Config.pd3dDevice->CreatePixelShader(Shader_GridTerrainDecorationPS, sizeof(Shader_GridTerrainDecorationPS), nullptr, &m_pDecorationPS);
        m_pDecorationMaterial = new ShaderMaterial(L"TerrainDecoration");
        m_pDecorationMaterial->InitializeVSPS(Shader_GridTerrainDecorationVS, sizeof(Shader_GridTerrainDecorationVS), Shader_GridTerrainDecorationPS, sizeof(Shader_GridTerrainDecorationPS));
        */
    }
    else
    {
        m_pCBAllocator = nullptr;
    }

    if (m_Config.pWorld != nullptr)
    {
        //CreateItem(L"rock1");
        //CreateItem(L"tree1");
    }

    return S_OK;
}

/*
DecorationItem* GridTerrain::CreateItem(const StringID& ItemName)
{
    Item* pItem = m_Config.pWorld->FindItem(ItemName);
    if (pItem != nullptr)
    {
        DecorationItem* pDI = new DecorationItem();
        pDI->ItemName = ItemName;
        pDI->pItem = pItem;
        pDI->NeedsUpdate = false;
        pDI->pInstanceBuffer = nullptr;
        pDI->pInstanceSRV = nullptr;
        pDI->InstanceCount = 0;
        pDI->MaxInstanceCount = 4096;

        if (m_Config.pd3dDevice != nullptr)
        {
            D3D11_TEXTURE1D_DESC TexDesc = {};
            TexDesc.Width = pDI->MaxInstanceCount * 4;
            TexDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
            TexDesc.ArraySize = 1;
            TexDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            TexDesc.MipLevels = 1;
            TexDesc.Usage = D3D11_USAGE_DYNAMIC;
            TexDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

            HRESULT hr = m_Config.pd3dDevice->CreateTexture1D(&TexDesc, NULL, &pDI->pInstanceBuffer);
            assert(SUCCEEDED(hr));

            hr = m_Config.pd3dDevice->CreateShaderResourceView(pDI->pInstanceBuffer, nullptr, &pDI->pInstanceSRV);
            assert(SUCCEEDED(hr));

            ItemCreation IC = {};
            IC.pItem = pItem;
            IC.InitialTransform = ItemTransform::Identity();
            IC.Name = L"";
            IC.InstancedRendering = TRUE;

            m_Config.pWorld->EnterLock();
            ItemInstance* pRenderII = m_Config.pWorld->SpawnItemInstance(&IC);
            assert(pRenderII != nullptr);
            m_Config.pWorld->LeaveLock();

            pRenderII->SetCullingDisabled(true);
            pDI->pRenderII = pRenderII;
        }

        m_Items[ItemName] = pDI;
        return pDI;
    }
    return nullptr;
}

void GridTerrain::InitializeRenderPasses(RenderPass* pSceneOpaque, RenderPass* pSceneAlpha, RenderPass* pShadow)
{
    if (pSceneOpaque != nullptr)
    {
        pSceneOpaque->AddMaterial(m_pOpaqueTerrainMaterial);
    }

    if (pSceneAlpha != nullptr)
    {
        pSceneAlpha->AddMaterial(m_pWaterMaterial);
        pSceneAlpha->AddMaterial(m_pDecorationMaterial);
    }

    if (pShadow != nullptr)
    {
        pShadow->AddMaterial(m_pOpaqueTerrainMaterial);
    }
}
*/

void GridTerrain::Terminate()
{
    auto iter = m_RootBlocks.begin();
    auto end = m_RootBlocks.end();
    while (iter != end)
    {
        GridBlock* p = iter->second;
        p->Terminate();
        delete p;
        ++iter;
    }
    m_RootBlocks.clear();

    m_GridIB.Destroy();
    m_GridBlockVB.Destroy();

    m_Config.pd3dDevice->Release();
}

void GridTerrain::Update(const DirectX::BoundingFrustum& TransformedFrustum, CXMMATRIX matVP)
{
    assert(m_Config.pd3dDevice != nullptr);

    m_LastBlockUpdateTime = GetTickCount();
    m_RenderBlockList.clear();
    m_AllRenderFlags = 0;

    XMFLOAT3 FrustumCorners[8];
    TransformedFrustum.GetCorners(FrustumCorners);

    XMVECTOR Min = g_XMFltMax;
    XMVECTOR Max = -g_XMFltMax;

    for (UINT32 i = 0; i < ARRAYSIZE(FrustumCorners); ++i)
    {
        XMVECTOR Corner = XMLoadFloat3(&FrustumCorners[i]);
        Min = XMVectorMin(Min, Corner);
        Max = XMVectorMax(Max, Corner);
    }

    RECT FrustumRect = {};
    FrustumRect.left = (LONG)XMVectorGetX(Min);
    FrustumRect.top = (LONG)XMVectorGetZ(Min);
    FrustumRect.right = (LONG)XMVectorGetX(Max);
    FrustumRect.bottom = (LONG)XMVectorGetZ(Max);

    //bool FeaturesReady = m_pFeatures->Update(FrustumRect, false);
    bool FeaturesReady = true;

    INT32 MinX = FrustumRect.left >> m_Config.LargestBlockShift;
    INT32 MinY = FrustumRect.top >> m_Config.LargestBlockShift;
    INT32 MaxX = FrustumRect.right >> m_Config.LargestBlockShift;
    INT32 MaxY = FrustumRect.bottom >> m_Config.LargestBlockShift;

//     MinX = max(0, MinX);
//     MaxX = min(2, MaxX);
//     MinY = max(0, MinY);
//     MaxY = min(2, MaxY);

    GridBlockCoord Coord;
    Coord.SizeShift = m_Config.LargestBlockShift;
    for (INT32 Y = MinY; Y <= MaxY; ++Y)
    {
        Coord.Y = Y << Coord.SizeShift;
        for (INT32 X = MinX; X <= MaxX; ++X)
        {
            Coord.X = X << Coord.SizeShift;
            //TerrainFeaturesBlock* pFeaturesBlock = m_pFeatures->GetFeaturesBlock(Coord);
            TerrainFeaturesBlock* pFeaturesBlock = nullptr;
            GridBlock* pChildBlock = nullptr;
            TestGridBlock(Coord, pFeaturesBlock, nullptr, TransformedFrustum, matVP, &pChildBlock, -1);
        }
    }

    ResolveEdges();

    CheckRootBlocksForExpiration();

    //UpdateItemInstances();
}

/*
void GridTerrain::UpdateItemInstances()
{
    assert(m_Config.pd3dDevice != nullptr);

    ID3D11DeviceContext* pd3dContext = nullptr;
    m_Config.pd3dDevice->GetImmediateContext(&pd3dContext);

    EnterLock();

    auto iter = m_Items.begin();
    auto end = m_Items.end();
    while (iter != end)
    {
        DecorationItem* pDI = iter->second;
        ++iter;

        if (!pDI->NeedsUpdate)
        {
            continue;
        }

        pDI->NeedsUpdate = false;

        const UINT32 InstanceCount = pDI->InstanceCount;
        if (pDI->Placements.empty() || InstanceCount == 0)
        {
            SAFE_RELEASE(pDI->pInstanceSRV);
            SAFE_RELEASE(pDI->pInstanceBuffer);
            continue;
        }

        assert(pDI->pInstanceBuffer != nullptr);
        assert(pDI->pInstanceSRV != nullptr);
        assert(pDI->pRenderII != nullptr);

        XMFLOAT4X4* pMatrixBuffer = nullptr;

        D3D11_MAPPED_SUBRESOURCE MapData = {};
        pd3dContext->Map(pDI->pInstanceBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &MapData);
        pMatrixBuffer = (XMFLOAT4X4*)MapData.pData;

        UINT32 MatrixIndex = 0;

        auto pliter = pDI->Placements.begin();
        auto plend = pDI->Placements.end();
        while (pliter != plend)
        {
            DecorationItemInstances* pII = *pliter++;
            const UINT32 TransformCount = (UINT32)pII->Transforms.size();
            const UINT32 SafeCount = min(TransformCount, pDI->MaxInstanceCount - MatrixIndex);
            if (SafeCount > 0)
            {
                memcpy(pMatrixBuffer, &pII->Transforms.front(), SafeCount * sizeof(XMFLOAT4X4));
                MatrixIndex += SafeCount;
                pMatrixBuffer += SafeCount;
            }
        }

        assert(MatrixIndex <= pDI->InstanceCount);

        pd3dContext->Unmap(pDI->pInstanceBuffer, 0);

        pDI->pRenderII->SetInstanceParams(pDI->pInstanceSRV, MatrixIndex, 0);
    }

    LeaveLock();

    SAFE_RELEASE(pd3dContext);
}
*/

void GridTerrain::Update(RECT XZBoundingRect, UINT32 UpdateTime, bool Synchronous)
{
    assert(m_Config.pd3dDevice == nullptr);

    if (UpdateTime == -1)
    {
        UpdateTime = GetTickCount();
    }
    const bool FullUpdate = (UpdateTime > m_LastBlockUpdateTime);
    m_LastBlockUpdateTime = UpdateTime;

    //bool FeaturesReady = m_pFeatures->Update(XZBoundingRect, Synchronous);
    bool FeaturesReady = true;

    INT32 MinX = XZBoundingRect.left >> m_Config.LargestBlockShift;
    INT32 MinY = XZBoundingRect.top >> m_Config.LargestBlockShift;
    INT32 MaxX = XZBoundingRect.right >> m_Config.LargestBlockShift;
    INT32 MaxY = XZBoundingRect.bottom >> m_Config.LargestBlockShift;

    GridBlockCoord Coord;
    Coord.SizeShift = m_Config.LargestBlockShift;
    for (INT32 Y = MinY; Y <= MaxY; ++Y)
    {
        Coord.Y = Y << Coord.SizeShift;
        for (INT32 X = MinX; X <= MaxX; ++X)
        {
            Coord.X = X << Coord.SizeShift;
            //TerrainFeaturesBlock* pFeaturesBlock = m_pFeatures->GetFeaturesBlock(Coord);
            TerrainFeaturesBlock* pFeaturesBlock = nullptr;
            assert(pFeaturesBlock != nullptr);
            UpdateGridBlockNoSubdivision(Coord, pFeaturesBlock, Synchronous);
        }
    }

    if (FullUpdate)
    {
        CheckRootBlocksForExpiration();
    }
}

void GridTerrain::Precache(RECT XZBoundingRect)
{
    //m_pFeatures->Update(XZBoundingRect, true);
}

void GridTerrain::UpdateGridBlockNoSubdivision(const GridBlockCoord& Coord, const TerrainFeaturesBlock* pFeaturesBlock, bool Synchronous)
{
    assert(m_Config.SmallestBlockShift == m_Config.LargestBlockShift);
    assert(Coord.SizeShift == m_Config.SmallestBlockShift);

    // If the features block is not yet ready, return:
    //if (!pFeaturesBlock->CanMakeTerrain())
    //{
    //    return;
    //}

    GridBlock* pBlock = nullptr;
    // This block is a root block; attempt to find it in the root block map:
    auto iter = m_RootBlocks.find(Coord.Value);
    if (iter != m_RootBlocks.end())
    {
        pBlock = iter->second;
    }
    if (pBlock == nullptr)
    {
        // This block is a root block; create it and add to the root block map if it doesn't already exist:
        pBlock = new GridBlock();
        pBlock->Initialize(&m_Config, Coord, nullptr, -1, pFeaturesBlock, Synchronous);
        m_RootBlocks[Coord.Value] = pBlock;
    }

    assert(pBlock != nullptr);

    if (pBlock->IsInitialized() && pBlock->GetTime() < m_LastBlockUpdateTime)
    {
        pBlock->Update(m_LastBlockUpdateTime, false);
    }
}

UINT32 GridTerrain::TestGridBlock(const GridBlockCoord& Coord, 
                                  const TerrainFeaturesBlock* pFeaturesBlock,
                                  GridBlock* pParentBlock, 
                                  const DirectX::BoundingFrustum& Frustum, 
                                  CXMMATRIX matVP, 
                                  GridBlock** ppChildBlock,
                                  UINT32 QuadrantOfParent)
{
    UINT32 LowestLevelReached = Coord.SizeShift;

    assert(ppChildBlock != nullptr);

    // Create world space AABB representing the grid block coords:
    XMVECTOR MinCorner = Coord.GetMin();
    XMVECTOR MaxCorner = Coord.GetMax();
    XMVECTOR BlockCenter = (MinCorner + MaxCorner) * g_XMOneHalf;
    XMVECTOR CameraPosWorld = XMLoadFloat3(&Frustum.Origin);
    XMVECTOR CameraToBlock = BlockCenter - CameraPosWorld;
    XMVECTOR CameraToBlockNorm = XMVector3Normalize(CameraToBlock);
    XMMATRIX matFrustum = XMMatrixRotationQuaternion(XMLoadFloat4(&Frustum.Orientation));
    XMVECTOR vForward = XMVector3TransformNormal(g_XMIdentityR2, matFrustum);
    XMVECTOR Dot = XMVector3Dot(CameraToBlockNorm, vForward);

    // Assign parent block Y values if present: 
    if (pParentBlock != nullptr)
    {
        FLOAT MaxHeight = std::max(pParentBlock->GetMaxHeight(), m_Config.WaterLevelYpos);
        MinCorner = XMVectorSetY(MinCorner, pParentBlock->GetMinHeight());
        MaxCorner = XMVectorSetY(MaxCorner, MaxHeight);
    }
    else
    {
        MinCorner = XMVectorSetY(MinCorner, -512.0f);
        MaxCorner = XMVectorSetY(MaxCorner, 512.0f);
    }

    BoundingBox AABB;
    BoundingBox::CreateFromPoints(AABB, MinCorner, MaxCorner);

    GridBlock* pBlock = nullptr;
    if (pParentBlock == nullptr)
    {
        // This block is a root block; attempt to find it in the root block map:
        auto iter = m_RootBlocks.find(Coord.Value);
        if (iter != m_RootBlocks.end())
        {
            pBlock = iter->second;
        }
    }
    else if (*ppChildBlock != nullptr)
    {
        // This block is a child of another block, and has already been created:
        pBlock = *ppChildBlock;
    }

    const bool FrustumVisible = (Frustum.Contains(AABB) != ContainmentType::DISJOINT);
    const bool CloseFrustumVisible = (XMVectorGetX(Dot) >= 0.1f);

    // Check if AABB intersects the frustum:
    if (!FrustumVisible && !CloseFrustumVisible)
    {
        // This AABB is not visible; stop processing here.

        // Check self and children for expiration via pBlock.
        if (pBlock != nullptr)
        {
            if (pBlock->CheckForExpiration(m_LastBlockUpdateTime - m_ExpirationThresholdMsec, &m_Config))
            {
                // Null out the pointer to this block if we just released it.
                *ppChildBlock = nullptr;
                if (pParentBlock == nullptr)
                {
                    m_RootBlocks[Coord.Value] = nullptr;
                }
            }
        }

        return LowestLevelReached;
    }

    // If the features block is not yet ready, return:
    //if (!pFeaturesBlock->CanMakeTerrain())
    //{
    //    return LowestLevelReached;
    //}

    if (pParentBlock == nullptr)
    {
        // This block is a root block; create it and add to the root block map if it doesn't already exist:
        if (pBlock == nullptr)
        {
            pBlock = new GridBlock();
            pBlock->Initialize(&m_Config, Coord, nullptr, -1, pFeaturesBlock, false);
            m_RootBlocks[Coord.Value] = pBlock;
        }
        *ppChildBlock = pBlock;
    }
    else if (pBlock == nullptr)
    {
        // This block is a child of another block, and needs to be created:
        pBlock = new GridBlock();
        pBlock->Initialize(&m_Config, Coord, pParentBlock, QuadrantOfParent, pFeaturesBlock, false);
        *ppChildBlock = pBlock;
    }

    // Must have a valid block at this point:
    assert(pBlock != nullptr);

    // If the block is not yet initialized, return now (can't test for subdivision yet):
    if (!pBlock->IsInitialized())
    {
        return LowestLevelReached;
    }

    if (0 && Coord.SizeShift == m_Config.LargestBlockShift)
    {
        XMVECTOR CenterPos = Coord.GetCenter();
        CenterPos = XMVectorSetY(CenterPos, (pBlock->m_MinHeight + pBlock->m_MaxHeight) * 0.5f);
        XMVECTOR BoxScale = Coord.GetScale() * g_XMOneHalf;
        BoxScale = XMVectorSetY(BoxScale, (pBlock->m_MaxHeight - pBlock->m_MinHeight) * 0.5f);
        //DebugDraw11::CacheWireframeCube(g_XMOne, CenterPos, BoxScale, 0);
    }

    XMVECTOR vRight = XMVector3TransformNormal(g_XMIdentityR0, matFrustum);
    XMVECTOR DistanceToCamera = XMVector3Length(XMVectorSelect(g_XMZero, CameraToBlock, g_XMSelect1010));
    XMVECTOR DistanceVector = vForward * XMVectorSplatX(DistanceToCamera);
    XMVECTOR RightVector = vRight * Coord.GetScale();
    XMVECTOR SyntheticBlockPos = CameraPosWorld + DistanceVector + RightVector;
    XMVECTOR ViewBlockPos = XMVector3TransformCoord(SyntheticBlockPos, matVP);
    FLOAT Width = XMVectorGetX(ViewBlockPos);

    // Determine if pBlock is a candidate for subdivision by looking at the projected width in view space:
    bool TransitionedToSubdivided = false;
    if (Width >= m_Config.BlockViewSpaceWidthThreshold && Coord.SizeShift > m_Config.SmallestBlockShift)
    {
        // Recursively create/update the four children of pBlock:
        if (pBlock->GetState() == GridBlock::Initialized)
        {
            pBlock->m_State = GridBlock::PendingSubdivision;
        }
        UINT32 LR0 = TestGridBlock(Coord.Get0(), pFeaturesBlock, pBlock, Frustum, matVP, &pBlock->m_pChildren[0], 0);
        UINT32 LR1 = TestGridBlock(Coord.Get1(), pFeaturesBlock, pBlock, Frustum, matVP, &pBlock->m_pChildren[1], 1);
        UINT32 LR2 = TestGridBlock(Coord.Get2(), pFeaturesBlock, pBlock, Frustum, matVP, &pBlock->m_pChildren[2], 2);
        UINT32 LR3 = TestGridBlock(Coord.Get3(), pFeaturesBlock, pBlock, Frustum, matVP, &pBlock->m_pChildren[3], 3);

        LR0 = std::min(LR0, LR1);
        LR2 = std::min(LR2, LR3);
        LR0 = std::min(LR0, LR2);

        // Determine if all of the existing children of pBlock are initialized or not:
        bool ChildrenInitialized = true;
        for (UINT32 i = 0; i < ARRAYSIZE(pBlock->m_pChildren); ++i)
        {
            GridBlock* pChild = pBlock->m_pChildren[i];
            if (pChild != nullptr && !pChild->IsInitialized())
            {
                ChildrenInitialized = false;
            }
        }

        // If all existing children of pBlock are initialized, then set this block's state to subdivided, which
        // will cause this block to stop rendering and the children will render instead:
        if (ChildrenInitialized)
        {
            if (pBlock->GetState() != GridBlock::Subdivided)
            {
                TransitionedToSubdivided = true;
            }
            pBlock->m_State = GridBlock::Subdivided;
            LowestLevelReached = LR0;
        }
    }
    else
    {
        // Block either shouldn't be or cannot be subdivided.
        pBlock->m_State = GridBlock::Initialized;

        // Check children of pBlock for expiration.
        for (UINT32 i = 0; i < ARRAYSIZE(pBlock->m_pChildren); ++i)
        {
            GridBlock* pChild = pBlock->m_pChildren[i];
            if (pChild != nullptr)
            {
                if (pChild->CheckForExpiration(m_LastBlockUpdateTime - m_ExpirationThresholdMsec, &m_Config))
                {
                    pBlock->m_pChildren[i] = nullptr;
                }
            }
        }
    }

    UINT32 RenderFlags = 0;

    // Determine if pBlock should render this frame or not:
    switch (pBlock->GetState())
    {
    case GridBlock::Initialized:
    case GridBlock::PendingSubdivision:
        if (pParentBlock == nullptr || pParentBlock->GetState() == GridBlock::Subdivided)
        {
            RenderFlags |= GridTerrainRender_OpaqueTerrain;
        }
        break;
    case GridBlock::Subdivided:
        if (TransitionedToSubdivided)
        {
            RenderFlags |= GridTerrainRender_OpaqueTerrain;
        }
        break;
    }

    if (RenderFlags & GridTerrainRender_OpaqueTerrain)
    {
        if (pBlock->m_MinHeight < m_Config.WaterLevelYpos)
        {
            RenderFlags |= GridTerrainRender_TransparentWater;
        }
    }

    if (pBlock->HasDecorations())
    {
        RenderFlags |= GridTerrainRender_TransparentDecoration;
    }

    // If we're going to render pBlock, do the appropriate bookkeeping:
    if (RenderFlags != 0)
    {
        m_AllRenderFlags |= RenderFlags;
        pBlock->Update(m_LastBlockUpdateTime, FrustumVisible);
        if (FrustumVisible && m_Config.pd3dDevice != nullptr)
        {
            BlockRender BR = {};
            BR.pBlock = pBlock;
            BR.EdgeMask = 0;
            BR.RenderFlags = RenderFlags;
            m_RenderBlockList.push_back(BR);
        }
    }

    return LowestLevelReached;
}

void GridTerrain::CheckRootBlocksForExpiration()
{
    if (m_LastBlockUpdateTime == 0)
    {
        return;
    }

    const RECT EmptyRect = { INT_MAX, INT_MAX, INT_MIN, INT_MIN };
    m_RootBlockExtents = EmptyRect;

    const UINT32 ExpireTime = m_LastBlockUpdateTime - m_ExpirationThresholdMsec;

    auto iter = m_RootBlocks.begin();
    auto end = m_RootBlocks.end();
    while (iter != end)
    {
        auto nextiter = iter;
        ++nextiter;

        GridBlock* pBlock = iter->second;
        if (pBlock != nullptr && 
            pBlock->IsInitialized() && 
            pBlock->CheckForExpiration(ExpireTime, &m_Config))
        {
            m_RootBlocks.erase(iter);
        }
        else if (pBlock != nullptr)
        {
            RECT BlockRect = pBlock->GetCoord().GetRect();
            m_RootBlockExtents = RectSet::UnionRect(m_RootBlockExtents, BlockRect);
        }

        iter = nextiter;
    }
}

bool FindOrCreateEdge(GridBlockEdgeList& EdgeList, INT32 SortCoordinate, INT32 Coordinate, UINT32 Shift, GridBlockEdge** ppEdge, const CHAR* strLabel)
{
    auto iter = EdgeList.find(SortCoordinate);
    auto end = EdgeList.end();

    bool Found = false;
    INT32 EndCoordinate = Coordinate + (1 << Shift);
    while (iter != end)
    {
        if (iter->first != SortCoordinate)
        {
            break;
        }
        GridBlockEdge& E = iter->second;
        INT32 EdgeEndCoordinate = E.Coordinate + (1 << E.Shift);
        if (Coordinate >= E.Coordinate && EndCoordinate <= EdgeEndCoordinate)
        {
            *ppEdge = &E;
            Found = true;
            break;
        }
        ++iter;
    }

    if (!Found)
    {
        GridBlockEdge NewEdge = {};
        NewEdge.Coordinate = Coordinate;
        NewEdge.Shift = Shift;
        NewEdge.LeftTopBlocks[0] = -1;
        NewEdge.LeftTopBlocks[1] = -1;
        NewEdge.RightBottomBlocks[0] = -1;
        NewEdge.RightBottomBlocks[1] = -1;
        NewEdge.LeftTopSolo = false;
        NewEdge.RightBottomSolo = false;
        NewEdge.DebugIndex = (UINT32)EdgeList.size();
        iter = EdgeList.emplace(SortCoordinate, NewEdge);
        *ppEdge = &iter->second;
    }

    //DebugSpew("%s %s edge at %d %d size %u\n", Found ? "found" : "new", strLabel, SortCoordinate, Coordinate, 1U << Shift);

    return true;
}

void ProcessSingleEdge(GridBlockEdgeList& EdgeList, INT32 SortPos, INT32 CoordPos, UINT32 Shift, bool IsLeftTop, UINT32 RenderIndex, const CHAR* strLabel)
{
    GridBlockEdge* pEdge = nullptr;
    FindOrCreateEdge(EdgeList, SortPos, CoordPos, Shift, &pEdge, strLabel);
    assert(pEdge->Shift >= Shift);
    assert(pEdge->Coordinate <= CoordPos);
    if (pEdge->Shift == Shift)
    {
        if (IsLeftTop)
        {
            pEdge->LeftTopBlocks[0] = RenderIndex;
            pEdge->LeftTopSolo = true;
        }
        else
        {
            pEdge->RightBottomBlocks[0] = RenderIndex;
            pEdge->RightBottomSolo = true;
        }
    }
    else
    {
        if (pEdge->Shift != Shift + 1)
        {
            return;
        }
        INT32 SlotIndex = 0;
        if (pEdge->Coordinate < CoordPos)
        {
            SlotIndex = 1;
        }
        if (IsLeftTop)
        {
            pEdge->LeftTopBlocks[SlotIndex] = RenderIndex;
        }
        else
        {
            pEdge->RightBottomBlocks[SlotIndex] = RenderIndex;
        }
    }
}

void ProcessEdges(GridBlockEdgeList& HorizEdges, GridBlockEdgeList& VertEdges, const GridBlockCoord& Coord, UINT32 RenderIndex)
{
    const INT32 BlockSize = 1 << Coord.SizeShift;
    ProcessSingleEdge(HorizEdges, Coord.Y, Coord.X, Coord.SizeShift, false, RenderIndex, "top");
    ProcessSingleEdge(HorizEdges, (INT32)Coord.Y + BlockSize, Coord.X, Coord.SizeShift, true, RenderIndex, "bottom");
    ProcessSingleEdge(VertEdges, Coord.X, Coord.Y, Coord.SizeShift, false, RenderIndex, "left");
    ProcessSingleEdge(VertEdges, (INT32)Coord.X + BlockSize, Coord.Y, Coord.SizeShift, true, RenderIndex, "right");
}

void GridTerrain::ResolveEdges()
{
    if (m_RenderBlockList.empty())
    {
        return;
    }

    std::sort(m_RenderBlockList.begin(), m_RenderBlockList.end());

    m_HorizEdges.clear();
    m_VertEdges.clear();

    const UINT32 BlockCount = (UINT32)m_RenderBlockList.size();
    for (UINT32 BlockIndex = 0; BlockIndex < BlockCount; ++BlockIndex)
    {
        const BlockRender& BR = m_RenderBlockList[BlockIndex];
        if ((BR.RenderFlags & GridTerrainRender_OpaqueTerrain) == 0)
        {
            continue;
        }
        const GridBlockCoord Coord = BR.pBlock->GetCoord();
        ProcessEdges(m_HorizEdges, m_VertEdges, Coord, BlockIndex);
    }

    {
        auto iter = m_HorizEdges.begin();
        auto end = m_HorizEdges.end();
        while (iter != end)
        {
            const GridBlockEdge& E = iter->second;
            if (E.IsMismatchedScale())
            {
                if (E.LeftTopSolo)
                {
                    if (E.RightBottomBlocks[0] != -1)
                    {
                        m_RenderBlockList[E.RightBottomBlocks[0]].EdgeMask |= TopEdge;
                    }
                    if (E.RightBottomBlocks[1] != -1)
                    {
                        m_RenderBlockList[E.RightBottomBlocks[1]].EdgeMask |= TopEdge;
                    }
                }
                else
                {
                    if (E.LeftTopBlocks[0] != -1)
                    {
                        m_RenderBlockList[E.LeftTopBlocks[0]].EdgeMask |= BottomEdge;
                    }
                    if (E.LeftTopBlocks[1] != -1)
                    {
                        m_RenderBlockList[E.LeftTopBlocks[1]].EdgeMask |= BottomEdge;
                    }
                }
            }
            ++iter;
        }
    }

    {
        auto iter = m_VertEdges.begin();
        auto end = m_VertEdges.end();
        while (iter != end)
        {
            const GridBlockEdge& E = iter->second;
            if (E.IsMismatchedScale())
            {
                if (E.LeftTopSolo)
                {
                    if (E.RightBottomBlocks[0] != -1)
                    {
                        m_RenderBlockList[E.RightBottomBlocks[0]].EdgeMask |= LeftEdge;
                    }
                    if (E.RightBottomBlocks[1] != -1)
                    {
                        m_RenderBlockList[E.RightBottomBlocks[1]].EdgeMask |= LeftEdge;
                    }
                }
                else
                {
                    if (E.LeftTopBlocks[0] != -1)
                    {
                        m_RenderBlockList[E.LeftTopBlocks[0]].EdgeMask |= RightEdge;
                    }
                    if (E.LeftTopBlocks[1] != -1)
                    {
                        m_RenderBlockList[E.LeftTopBlocks[1]].EdgeMask |= RightEdge;
                    }
                }
            }
            ++iter;
        }
    }
}

void GridTerrain::RenderOpaque(const GridTerrainRender& GTR)
{
    assert(m_Config.pd3dDevice != nullptr);
    GraphicsContext* pContext = GTR.pContext;

    /*
    SimpleResources11::SetBlendStateNone(pd3dContext);
    if (GTR.Wireframe)
    {
        SimpleResources11::SetRastStateWireframe(pd3dContext);
    }
    else
    {
        SimpleResources11::SetRastStateSolid(pd3dContext);
    }
    */

    UpdateTerrainCB(pContext, GTR.AbsoluteTime);

    pContext->SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
//     pd3dContext->IASetInputLayout(m_pOpaqueTerrainLayout);
//     pd3dContext->VSSetShader(m_pOpaqueTerrainVS, nullptr, 0);
//     pd3dContext->PSSetShader(m_pOpaqueTerrainPS, nullptr, 0);
//     ID3D11ShaderResourceView* pSRV = m_pTerrainTexture->GetSRView();
//     pd3dContext->PSSetShaderResources(0, 1, &pSRV);
//     SimpleResources11::SetPixelSamplerBilinear(pd3dContext, 0);
//     UINT32 Stride = sizeof(FixedGridVertex);
//     UINT32 Offset = 0;
    pContext->SetVertexBuffer(1, m_GridBlockVB.VertexBufferView());

    //ID3D11ShaderResourceView* pShadowMap[3];
    //pShadowMap[0] = (ID3D11ShaderResourceView*)m_pShadowMapTexture0->GetExtraData();
    //pShadowMap[1] = (ID3D11ShaderResourceView*)m_pShadowMapTexture1->GetExtraData();
    //pShadowMap[2] = (ID3D11ShaderResourceView*)m_pShadowMapTexture2->GetExtraData();
    //pd3dContext->PSSetShaderResources(4, 3, pShadowMap);

    //ID3D11SamplerState* pShadowSampler = (ID3D11SamplerState*)m_pShadowMapSampler->BufferValue();
    //pd3dContext->PSSetSamplers(4, 1, &pShadowSampler);

    auto iter = m_RenderBlockList.rbegin();
    auto end = m_RenderBlockList.rend();
    while (iter != end)
    {
        BlockRender BR = *iter++;
        if (BR.RenderFlags & GridTerrainRender_OpaqueTerrain)
        {
            IBTile& t = m_EdgeIB[BR.EdgeMask & 0xF];
            D3D12_INDEX_BUFFER_VIEW IBV = {};
            IBV.BufferLocation = t.IBAddress;
            IBV.Format = DXGI_FORMAT_R32_UINT;
            IBV.SizeInBytes = t.IndexCount * sizeof(UINT32);
            BR.pBlock->Render(GTR, m_pCBAllocator, IBV, t.IndexCount, &m_Config);
        }
    }

    //pd3dContext->VSSetShader(nullptr, nullptr, 0);
    //pd3dContext->PSSetShader(nullptr, nullptr, 0);
    //
    //pShadowMap[0] = nullptr;
    //pShadowMap[1] = nullptr;
    //pShadowMap[2] = nullptr;
    //pd3dContext->PSSetShaderResources(4, 3, pShadowMap);
}

void GridTerrain::UpdateTerrainCB(GraphicsContext* pContext, DOUBLE AbsoluteTime)
{
    const XMMATRIX matRotation0 = XMMatrixRotationZ(1.0f);
    const XMVECTOR FlatRotation0 = XMVectorSelect(XMVectorSwizzle<2, 3, 0, 1>(matRotation0.r[1]), matRotation0.r[0], g_XMSelect1100);
    const XMMATRIX matRotation1 = XMMatrixRotationZ(2.0f);
    const XMVECTOR FlatRotation1 = XMVectorSelect(XMVectorSwizzle<2, 3, 0, 1>(matRotation1.r[1]), matRotation1.r[0], g_XMSelect1100);

    DynAlloc DA = m_pCBAllocator->Allocate(sizeof(CBTerrain));
    CBTerrain* pCB = (CBTerrain*)DA.DataPtr;
    ZeroMemory(pCB, sizeof(*pCB));
    XMStoreFloat4(&pCB->TexCoordTransform0, FlatRotation0);
    XMStoreFloat4(&pCB->TexCoordTransform1, FlatRotation1);
    //XMStoreFloat4(&pCB->AmbientLightColor, m_pAmbientLightColor->VectorValue());
    //XMStoreFloat4(&pCB->InverseLightDirection, m_pDirLightInvDirection->VectorValue());
    //XMStoreFloat4(&pCB->DirectionalLightColor, m_pDirLightColor->VectorValue());
    //XMStoreFloat4(&pCB->CameraPosWorld, m_pCameraPosWorld->VectorValue());
    pCB->WaterConstants.x = m_Config.WaterLevelYpos;
    pCB->WaterConstants.y = (FLOAT)AbsoluteTime;
    //XMStoreFloat4(&pCB->ShadowSize0, m_pShadowSize0->VectorValue());
    //XMStoreFloat4x4(&pCB->matWorldToShadow0, m_pWorldToShadow0->MatrixValue());
    //XMStoreFloat4x4(&pCB->matWorldToShadow1, m_pWorldToShadow1->MatrixValue());
    //XMStoreFloat4x4(&pCB->matWorldToShadow2, m_pWorldToShadow2->MatrixValue());

    pContext->SetConstantBuffer(1, DA.GpuAddress);
}

void GridTerrain::RenderTransparent(const GridTerrainRender& GTR)
{
    if ((m_AllRenderFlags & GridTerrainRender_Transparent) == 0)
    {
        return;
    }

    GraphicsContext* pContext = GTR.pContext;

    //if (GTR.Wireframe)
    //{
    //    SimpleResources11::SetRastStateWireframe(pd3dContext);
    //}
    //else
    //{
    //    SimpleResources11::SetRastStateSolidNoCull(pd3dContext);
    //}

    UpdateTerrainCB(GTR.pContext, GTR.AbsoluteTime);

    pContext->SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    //if (m_AllRenderFlags & GridTerrainRender_TransparentDecoration)
    //{
    //    SimpleResources11::SetBlendStateCoverageAlpha(pd3dContext);
    //    pd3dContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    //    pd3dContext->IASetInputLayout(m_pDecorationLayout);
    //    pd3dContext->VSSetShader(m_pDecorationVS, nullptr, 0);
    //    pd3dContext->PSSetShader(m_pDecorationPS, nullptr, 0);
    //    pd3dContext->IASetIndexBuffer(m_pDecorationIB, DXGI_FORMAT_R16_UINT, 0);
    //    ID3D11ShaderResourceView* pSRV = m_pDecorationTexture->GetSRView();
    //    pd3dContext->PSSetShaderResources(0, 1, &pSRV);
    //
    //    ID3D11ShaderResourceView* pShadowMap[3];
    //    pShadowMap[0] = (ID3D11ShaderResourceView*)m_pShadowMapTexture0->GetExtraData();
    //    pShadowMap[1] = (ID3D11ShaderResourceView*)m_pShadowMapTexture1->GetExtraData();
    //    pShadowMap[2] = (ID3D11ShaderResourceView*)m_pShadowMapTexture2->GetExtraData();
    //    pd3dContext->PSSetShaderResources(4, 3, pShadowMap);
    //
    //    ID3D11SamplerState* pShadowSampler = (ID3D11SamplerState*)m_pShadowMapSampler->BufferValue();
    //    pd3dContext->PSSetSamplers(4, 1, &pShadowSampler);
    //
    //    UINT32 Strides[] = { sizeof(DecorationModelVertex), sizeof(DecorationInstanceVertex) };
    //    UINT32 Offsets[] = { 0, 0 };
    //    ID3D11Buffer* pVBs[] = { m_pDecorationVB, nullptr };
    //
    //    auto iter = m_RenderBlockList.begin();
    //    auto end = m_RenderBlockList.end();
    //    while (iter != end)
    //    {
    //        BlockRender BR = *iter++;
    //        GridBlock* pBlock = BR.pBlock;
    //        if ((BR.RenderFlags & GridTerrainRender_TransparentDecoration) == GridTerrainRender_TransparentDecoration)
    //        {
    //            pVBs[1] = pBlock->m_pDecorationInstanceVB;
    //            pd3dContext->IASetVertexBuffers(0, 2, pVBs, Strides, Offsets);
    //            pBlock->RenderDecorations(GTR, m_pBlockCB, &m_Config, m_DecorationModels);
    //        }
    //    }
    //
    //    pShadowMap[0] = nullptr;
    //    pShadowMap[1] = nullptr;
    //    pShadowMap[2] = nullptr;
    //    pd3dContext->PSSetShaderResources(4, 3, pShadowMap);
    //}

    if (m_AllRenderFlags & GridTerrainRender_TransparentWater)
    {
        //SimpleResources11::SetBlendStateAlpha(pd3dContext);
        
        //pd3dContext->IASetInputLayout(m_pWaterLayout);
        //pd3dContext->VSSetShader(m_pWaterVS, nullptr, 0);
        //pd3dContext->PSSetShader(m_pWaterPS, nullptr, 0);
        pContext->SetVertexBuffer(1, m_GridBlockVB.VertexBufferView());

        auto iter = m_RenderBlockList.begin();
        auto end = m_RenderBlockList.end();
        while (iter != end)
        {
            BlockRender BR = *iter++;
            GridBlock* pBlock = BR.pBlock;
            if ((BR.RenderFlags & GridTerrainRender_TransparentWater) == GridTerrainRender_TransparentWater)
            {
                IBTile& t = m_EdgeIB[BR.EdgeMask & 0xF];
                D3D12_INDEX_BUFFER_VIEW IBV = {};
                IBV.BufferLocation = t.IBAddress;
                IBV.Format = DXGI_FORMAT_R32_UINT;
                IBV.SizeInBytes = t.IndexCount * sizeof(UINT32);
                BR.pBlock->Render(GTR, m_pCBAllocator, IBV, t.IndexCount, nullptr);
            }
        }
    }
}

/*
void GridBlock::DebugRender(GlyphRenderer* pGR, FXMVECTOR CameraPosWorld, FXMVECTOR RenderScale, CXMVECTOR ScreenOffset)
{
    const GridBlockCoord& Coord = m_Coord;
    XMVECTOR Min = (Coord.GetMin() - CameraPosWorld) * RenderScale + ScreenOffset;
    XMVECTOR Max = (Coord.GetMax() - CameraPosWorld) * RenderScale + ScreenOffset;
    RECT r;
    r.left = (LONG)XMVectorGetX(Min);
    r.top = (LONG)XMVectorGetZ(Min);
    r.right = (LONG)XMVectorGetX(Max);
    r.bottom = (LONG)XMVectorGetZ(Max);

    const DWORD Colors[4]       = { 0xFF00FFFF, 0xFFC0C0C0, 0xFF00FF00, 0xFFFF0000 };
    const DWORD CulledColors[4] = { 0xFF008080, 0xFF404040, 0xFF008000, 0xFF800000 };

    const bool Culled = (m_LastFrameRendered != m_LastSeenTime);
    const DWORD SelectedColor = Culled ? CulledColors[m_State] : Colors[m_State];

    pGR->DrawRect(r, 1, SelectedColor);

    for (UINT32 i = 0; i < ARRAYSIZE(m_pChildren); ++i)
    {
        if (m_pChildren[i] != nullptr)
        {
            m_pChildren[i]->DebugRender(pGR, CameraPosWorld, RenderScale, ScreenOffset);
        }
    }
}

void GridTerrain::DebugRender(GlyphRenderer* pGR, const DirectX::BoundingFrustum& TransformedFrustum, FXMVECTOR CameraPosWorld, FLOAT FarZ, FLOAT Scale, UINT32 Flags)
{
    const FLOAT DrawSize = 1400;
    const FLOAT DrawScale = DrawSize / (FarZ * 2.0f);

    const XMVECTOR RenderScale = XMVectorReplicate(DrawScale * Scale);

    const INT XYOffset = 10;
    const XMVECTOR ScreenOffset = XMVectorReplicate((FLOAT)XYOffset + (FLOAT)DrawSize * 0.5f);

    TerrainFeaturesBlock* pLastFB = nullptr;
    auto iter = m_RootBlocks.begin();
    auto end = m_RootBlocks.end();
    while (iter != end)
    {
        GridBlock* pRB = iter->second;
        ++iter;

        if (pRB != nullptr)
        {
            if (Flags & 0x1)
            {
                pRB->DebugRender(pGR, CameraPosWorld, RenderScale, ScreenOffset);
            }
            if (Flags & 0x2)
            {
                TerrainFeaturesBlock* pFB = m_pFeatures->GetFeaturesBlock(pRB->GetCoord());
                if (pFB != pLastFB && pFB->CanMakeTerrain())
                {
                    pLastFB = pFB;
                    pFB->DebugRender(pGR, RenderScale, CameraPosWorld, ScreenOffset);
                }
            }
        }
    }

    pGR->DrawRect(XYOffset, XYOffset, (INT)DrawSize, (INT)DrawSize, 2, 0xFF00FFFF);
    if (Scale != 1.0f)
    {
        pGR->DrawText(XYOffset, XYOffset - 10, 10, 0xFFFFFFFF, L"%0.1fx scale", Scale);
    }

    if (Flags & 0x4)
    {
        auto iter = m_HorizEdges.begin();
        auto end = m_HorizEdges.end();
        while (iter != end)
        {
            const INT32 Ypos = iter->first;
            const GridBlockEdge& E = iter->second;
            INT32 Size = 1 << E.Shift;
            XMVECTOR VectorColor = XMVectorSet(0.5f, 0.5f, 1, 1);
            if (E.IsMismatchedScale())
            {
                VectorColor = XMVectorSet(1, 0.5f, 0.5f, 1);
            }
            XMUBYTEN4 Color;
            XMStoreUByteN4(&Color, XMVectorSwizzle<2, 1, 0, 3>(VectorColor));
            INT32 LineXpos = (INT32)(((FLOAT)E.Coordinate - XMVectorGetX(CameraPosWorld)) * XMVectorGetX(RenderScale) + XMVectorGetX(ScreenOffset));
            INT32 LineYpos = (INT32)(((FLOAT)Ypos - XMVectorGetZ(CameraPosWorld)) * XMVectorGetZ(RenderScale) + XMVectorGetZ(ScreenOffset));
            INT32 LineXend = (INT32)(((FLOAT)(E.Coordinate + Size) - XMVectorGetX(CameraPosWorld)) * XMVectorGetX(RenderScale) + XMVectorGetX(ScreenOffset));
            pGR->DrawLine(LineXpos, LineYpos, LineXend, LineYpos, 2, Color.v);
            pGR->DrawText((LineXpos + LineXend) / 2, LineYpos, 10, 0xFFFFFFFF, L"H%u", E.DebugIndex);
//             for (UINT32 i = 0; i < 2; ++i)
//             {
//                 if (E.LeftTopBlocks[i] != -1)
//                 {
//                     pGR->DrawText(E.Coordinate + (Size * (i*2 + 1)) / 4, Ypos - 8, 10, Color.v, L"%u", E.LeftTopBlocks[i]);
//                 }
//                 if (E.RightBottomBlocks[i] != -1)
//                 {
//                     pGR->DrawText(E.Coordinate + (Size * (i*2 + 1)) / 4, Ypos, 10, Color.v, L"%u", E.RightBottomBlocks[i]);
//                 }
//             }
            ++iter;
        }
    }

    if (Flags & 0x4)
    {
        auto iter = m_VertEdges.begin();
        auto end = m_VertEdges.end();
        while (iter != end)
        {
            const INT32 Xpos = iter->first;
            const GridBlockEdge& E = iter->second;
            INT32 Size = 1 << E.Shift;
            XMVECTOR VectorColor = XMVectorSet(0.5f, 0.5f, 1, 1);
            if (E.IsMismatchedScale())
            {
                VectorColor = XMVectorSet(1, 0.5f, 0.5f, 1);
            }
            XMUBYTEN4 Color;
            XMStoreUByteN4(&Color, XMVectorSwizzle<2, 1, 0, 3>(VectorColor));
            INT32 LineYpos = (INT32)(((FLOAT)E.Coordinate - XMVectorGetZ(CameraPosWorld)) * XMVectorGetZ(RenderScale) + XMVectorGetZ(ScreenOffset));
            INT32 LineXpos = (INT32)(((FLOAT)Xpos - XMVectorGetX(CameraPosWorld)) * XMVectorGetX(RenderScale) + XMVectorGetX(ScreenOffset));
            INT32 LineYend = (INT32)(((FLOAT)(E.Coordinate + Size) - XMVectorGetZ(CameraPosWorld)) * XMVectorGetZ(RenderScale) + XMVectorGetZ(ScreenOffset));
            pGR->DrawLine(LineXpos, LineYpos, LineXpos, LineYend, 2, Color.v);
            pGR->DrawText(LineXpos, (LineYpos + LineYend) / 2, 10, 0xFFFFFFFF, L"V%u", E.DebugIndex);
            ++iter;
        }
    }

    if (Flags & 0x8)
    {
        XMFLOAT3 WorldCorners[8];
        TransformedFrustum.GetCorners(WorldCorners);

        POINT XYCorners[8];
        for (UINT32 i = 0; i < 8; ++i)
        {
            XMVECTOR WC = XMLoadFloat3(&WorldCorners[i]);
            XMVECTOR TC = (WC - CameraPosWorld) * RenderScale + ScreenOffset;
            XYCorners[i].x = (INT32)XMVectorGetX(TC);
            XYCorners[i].y = (INT32)XMVectorGetZ(TC);
        }

        for (UINT32 i = 0; i < 4; ++i)
        {
            const POINT& S = XYCorners[i];
            const POINT& E = XYCorners[i + 4];
            pGR->DrawLine(S.x, S.y, E.x, E.y, 1, 0xFFFF00FF);
        }
    }
}
*/

FLOAT GridTerrain::QueryHeight(FXMVECTOR PositionXYZ)
{
    /*
    const TerrainFeaturesBlock* pFeaturesBlock = m_pFeatures->GetFeaturesBlock(PositionXYZ);
    assert(pFeaturesBlock != nullptr);
    assert(pFeaturesBlock->CanMakeTerrain());
    TerrainCornerData CornerData;
    pFeaturesBlock->MakeTerrainCornerData(g_XMZero, &CornerData);
    TerrainPointResult Result = {};
    pFeaturesBlock->MakeTerrain(&CornerData, XMVectorSwizzle<0, 2, 1, 3>(PositionXYZ), &Result);
    return XMVectorGetX(Result.TerrainHeight);
    */
    return 0;
}

/*
void GridTerrain::BuildInstanceMap(DecorationInstanceMap& IM)
{
    auto iter = m_Items.begin();
    auto end = m_Items.end();
    while (iter != end)
    {
        DecorationItem* pItem = iter->second;
        DecorationItemInstances* pI = new DecorationItemInstances();
        pI->pModel = pItem;
        IM[pItem->ItemName] = pI;
        pItem->Placements.insert(pI);

        ++iter;
    }
}

void GridTerrainProxy::GetMemberDatas(const MemberDataPosition** ppMemberDatas, UINT* pMemberDataCount) const
{
    static const MemberDataPosition Members[] =
    {
        { StateNodeType::Integer, offsetof(GridTerrainProxy, m_Seed), sizeof(m_Seed) },
    };

    *ppMemberDatas = Members;
    *pMemberDataCount = ARRAYSIZE(Members);
}

void GridTerrainProxy::ServerInitializeTerrain(UINT32 Seed, EngineServer* pServer)
{
    if (Seed == 0)
    {
        Seed = 1;
    }
    m_Seed = Seed;
    m_Pad1.Initialize(m_Seed);
    m_Pad2.Initialize(m_Seed + 7);

    ZeroMemory(&m_Config, sizeof(m_Config));
    m_Config.pPad1 = &m_Pad1;
    m_Config.pPad2 = &m_Pad2;
    m_Config.LargestBlockShift = 8;
    m_Config.SmallestBlockShift = m_Config.LargestBlockShift;
    m_Config.BlockVertexShift = 6;
    m_Config.FeaturesBlockShift = 13;

    if (pServer != nullptr)
    {
        Initialize(pServer);
    }
}

BOOL GridTerrainProxy::Initialize(EngineServer* pServer)
{
    if (m_pGT != nullptr)
    {
        return TRUE;
    }

    m_Config.pPhysicsWorld = pServer->GetWorld()->GetPhysicsWorld();

    m_pGT = new GridTerrain();
    m_pGT->Initialize(&m_Config);

    return TRUE;
}

VOID GridTerrainProxy::Update(FLOAT DeltaTime, DOUBLE AbsoluteTime, EngineServer* pServer)
{
    UINT32 UpdateTime = GetTickCount();

    RectSet RS(m_pGT->GetRootBlockShift());
    
    auto iter = pServer->GetWorld()->FirstItemInstance();
    auto end = pServer->GetWorld()->LastItemInstance();
    while (iter != end)
    {
        ItemInstance* pII = *iter++;
        if (pII->GetRigidBody() == nullptr)
        {
            continue;
        }
        RigidBody* pRB = pII->GetRigidBody();
        if (pRB->IsStatic())
        {
            continue;
        }
        XMVECTOR Pos = pRB->GetWorldPosition();
        XMVECTOR Velocity = pRB->GetLinearVelocity();
        RS.AddVector(Pos, Velocity * g_XMTwo, 5);
    }

    UINT32 RectCount = RS.GetRectCount();
    for (UINT32 i = 0; i < RectCount; ++i)
    {
        m_pGT->Update(RS.GetRect(i), UpdateTime);
    }
}

bool GridTerrainProxy::ClientInitialize(ID3D11Device* pd3dDevice, ParameterCollection* pRootParams, PhysicsWorld* pPhysicsWorld)
{
    if (!ClientIsSynced())
    {
        return false;
    }
    m_Pad1.Initialize(m_Seed);
    m_Pad2.Initialize(m_Seed + 7);

    ZeroMemory(&m_Config, sizeof(m_Config));
    m_Config.pPad1 = &m_Pad1;
    m_Config.pPad2 = &m_Pad2;
    m_Config.LargestBlockShift = 10;
    m_Config.SmallestBlockShift = 0;
    m_Config.BlockVertexShift = 4;
    m_Config.BlockViewSpaceWidthThreshold = 0.75f;
    m_Config.FeaturesBlockShift = 13;

    if (pd3dDevice != nullptr)
    {
        m_Config.pd3dDevice = pd3dDevice;
        m_Config.pPhysicsWorld = nullptr;
        m_Config.pRootParams = pRootParams;
        m_pRenderGT = new GridTerrain();
        m_pRenderGT->Initialize(&m_Config);
    }

    if (pPhysicsWorld != nullptr)
    {
        m_Config.pd3dDevice = nullptr;
        m_Config.pPhysicsWorld = pPhysicsWorld;
        m_Config.LargestBlockShift = m_Config.SmallestBlockShift;
        m_Config.BlockVertexShift = 4;
        m_pGT = new GridTerrain();
        m_pGT->Initialize(&m_Config);
    }

    return true;
}
*/