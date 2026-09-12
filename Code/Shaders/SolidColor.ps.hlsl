// Outputs a constant colour; used with FullScreenTri.vs.hlsl to fill a viewport-limited rectangle
// (WinlatorXR frame-sync marker) or, with an alpha-only write mask, to force the back buffer alpha to 1.
cbuffer SolidColorParams : register(b0)
{
	float4 color;
};

float4 main(in float4 position : SV_POSITION, in float2 texcoord : TEXCOORD0) : SV_TARGET {
	return color;
}
