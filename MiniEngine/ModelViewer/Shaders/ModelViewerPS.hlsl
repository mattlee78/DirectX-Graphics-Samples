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
// Author(s):  James Stanard
//             Alex Nankervis
//

#include "ModelViewerRS.hlsli"
#include "LightAndShadow.hlsli"

[RootSignature(ModelViewer_RootSig)]
float3 main(ObjectVSOutput vsOutput) : SV_Target0
{
    return DefaultMaterialLightAndShadow(
        vsOutput.texcoord0,
        uint2(vsOutput.position.xy),
        vsOutput.viewDir,
        vsOutput.tangent,
        vsOutput.bitangent,
        vsOutput.normal,
        vsOutput.shadowCoord,
        vsOutput.shadowCoordOuter
    );
}
