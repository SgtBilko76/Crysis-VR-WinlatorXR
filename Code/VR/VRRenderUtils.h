#pragma once

#include <d3d10_1.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

#define CHECK_D3D10(call) { HRESULT hr = call; if (FAILED(hr)) CryLogAlways("D3D10 call failed at %s:%d\n", __FILE__, __LINE__); }

class D3D10StateGuard
{
public:
    explicit D3D10StateGuard(ID3D10Device* device);
    ~D3D10StateGuard();

private:
    ComPtr<ID3D10Device> device;
	ComPtr<ID3D10VertexShader> vertexShader;
	ComPtr<ID3D10PixelShader> pixelShader;
	ComPtr<ID3D10InputLayout> inputLayout;
	D3D10_PRIMITIVE_TOPOLOGY topology;
	ID3D10Buffer *vertexBuffers[D3D10_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT];
	UINT strides[D3D10_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT];
	UINT offsets[D3D10_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT];
	ComPtr<ID3D10Buffer> indexBuffer;
	DXGI_FORMAT format;
	UINT offset;
	ID3D10RenderTargetView *renderTargets[D3D10_SIMULTANEOUS_RENDER_TARGET_COUNT];
	ComPtr<ID3D10DepthStencilView> depthStencil;
	ComPtr<ID3D10RasterizerState> rasterizerState;
	ComPtr<ID3D10DepthStencilState> depthStencilState;
	UINT stencilRef;
	D3D10_VIEWPORT viewports[D3D10_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
	UINT numViewports = 0;
	D3D10_RECT scissorRects[D3D10_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
	UINT numScissorRects = 0;
	ComPtr<ID3D10BlendState> blendState;
	float blendFactor[4];
	UINT sampleMask;
	ComPtr<ID3D10Buffer> vsConstantBuffer;
	ComPtr<ID3D10Buffer> psConstantBuffer;
	ComPtr<ID3D10ShaderResourceView> psResource;
	ComPtr<ID3D10SamplerState> psSampler;
};

// integer pixel rectangle in render target space
struct VRRect
{
	int x = 0, y = 0, w = 0, h = 0;
	VRRect() {}
	VRRect(int x_, int y_, int w_, int h_) : x(x_), y(y_), w(w_), h(h_) {}
};

class VRRenderUtils
{
public:
    ~VRRenderUtils();

    void Init(ID3D10Device* device);
    void Shutdown();
    void CopyEyeToScreenMirror(ID3D10ShaderResourceView* eyeTexture, const RectF& bounds);

	enum RectBlend
	{
		RB_OPAQUE,        // copy the texture's RGB, leave the destination alpha untouched
		RB_ALPHA,         // classic "over" blend of the texture's RGB using its alpha
	};

	// WinlatorXR frame composition helpers. Each draws with the fullscreen-triangle vertex shader into a
	// viewport covering exactly 'dest' (so the whole source texture is stretched over it), clipped to
	// 'scissor' when given. Callers are expected to hold a D3D10StateGuard and to have bound the
	// intended render target.
	void DrawTextureRect(ID3D10ShaderResourceView* texture, const VRRect& dest, const VRRect* scissor, RectBlend blend);
	// Fills 'dest' with a constant colour. writeAlphaOnly = true only writes the alpha channel (used to
	// force the back buffer alpha to 1 after compositing).
	void FillRect(const VRRect& dest, const ColorF& color, bool writeAlphaOnly);

private:
    ComPtr<ID3D10Device> m_device;
    ComPtr<ID3D10VertexShader> m_fullScreenTriVertexShader;
    ComPtr<ID3D10PixelShader> m_drawTexturePixelShader;
    ComPtr<ID3D10PixelShader> m_solidColorPixelShader;
    ComPtr<ID3D10Buffer> m_solidColorConstants;
    ComPtr<ID3D10SamplerState> m_sampler;
    ComPtr<ID3D10SamplerState> m_linearSampler;
    ComPtr<ID3D10RasterizerState> m_rasterizerState;
    ComPtr<ID3D10RasterizerState> m_scissorRasterizerState;
    ComPtr<ID3D10BlendState> m_srcAlphaBlend;
    ComPtr<ID3D10BlendState> m_NoBlend;
    ComPtr<ID3D10BlendState> m_overBlend;
    ComPtr<ID3D10BlendState> m_alphaOnlyWrite;
    ComPtr<ID3D10BlendState> m_writeAll;
    ComPtr<ID3D10DepthStencilState> m_disableDepth;

	void SetupRectDraw(const VRRect& dest, const VRRect* scissor);
};

extern VRRenderUtils* gVRRenderUtils;
