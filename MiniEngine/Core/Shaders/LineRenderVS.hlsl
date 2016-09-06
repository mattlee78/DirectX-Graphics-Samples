cbuffer cb0
{
    row_major float4x4 mViewProj : packoffset(c0);
}

void main( float3 pos : POSITION, float4 VertexColor : COLOR0, out float4 Color0 : COLOR0, out float4 OutPosition : SV_POSITION )
{
    Color0 = VertexColor;
	OutPosition = mul(float4(pos, 1), mViewProj);
}
