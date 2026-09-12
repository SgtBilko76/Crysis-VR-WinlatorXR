#include "StdAfx.h"
#define XR_USE_PLATFORM_WIN32 1
#define XR_USE_GRAPHICS_API_D3D11 1
#include <d3d11.h>
#include <wrl/client.h>
#include <openxr/openxr_platform.h>
#include "OpenXRRuntime.h"

#include "GameCVars.h"
#include "VRManager.h"
#include "VRRenderer.h"
#include "Menus/FlashMenuObject.h"
#include "WinlatorXR.h"
#include <tlhelp32.h>

OpenXRRuntime g_xrRuntime;
OpenXRRuntime *gXR = &g_xrRuntime;

using Microsoft::WRL::ComPtr;

const int OpenXRInput::MOUSE_SAMPLE_COUNT;


bool XR_CheckResult(XrResult result, const char *description, XrInstance instance = XR_NULL_HANDLE)
{
	if ( XR_SUCCEEDED( result ) ) {
		return true;
	}

	char resultString[XR_MAX_RESULT_STRING_SIZE];
	xrResultToString( instance, result, resultString );
	CryLogAlways("XR %s failed: %s (%i)", description, resultString, result);
	return false;
}

// OpenXR: x = right, y = up, -z = forward
// Crysis: x = right, y = forward, z = up
// both use the metric system, i.e. 1 unit = 1m
Matrix34 OpenXRToCrysis(const XrQuaternionf& orientation, const XrVector3f& position)
{
	Vec3 pos(position.x, position.y, position.z);
	Quat rot(orientation.w, orientation.x, orientation.y, orientation.z);
	Matrix34 xr(Vec3(1, 1, 1), rot, pos);

	Matrix34 m;
	m.m00 = xr.m00;
	m.m01 = -xr.m02;
	m.m02 = xr.m01;
	m.m03 = xr.m03;
	m.m10 = -xr.m20;
	m.m11 = xr.m22;
	m.m12 = -xr.m21;
	m.m13 = -xr.m23;
	m.m20 = xr.m10;
	m.m21 = -xr.m12;
	m.m22 = xr.m11;
	m.m23 = xr.m13;
	return m;
}

Vec3 OpenXRToCrysis(const XrVector3f& position)
{
	Vec3 pos(position.x, -position.z, position.y);
	return pos;
}

XrPosef CrysisToOpenXR(const Matrix34& transform)
{
	Matrix34 m;
	m.m00 = transform.m00;
	m.m01 = transform.m02;
	m.m02 = -transform.m01;
	m.m03 = transform.m03;
	m.m10 = transform.m20;
	m.m11 = transform.m22;
	m.m12 = -transform.m21;
	m.m13 = transform.m23;
	m.m20 = -transform.m10;
	m.m21 = -transform.m12;
	m.m22 = transform.m11;
	m.m23 = -transform.m13;

	Vec3 pos = m.GetTranslation();
	Quat rot = GetQuatFromMat33((Matrix33)m);
	XrPosef pose;
	pose.position.x = pos.x;
	pose.position.y = pos.y;
	pose.position.z = pos.z;
	pose.orientation.w = rot.w;
	pose.orientation.x = rot.v.x;
	pose.orientation.y = rot.v.y;
	pose.orientation.z = rot.v.z;
	return pose;
}

namespace
{

	bool XR_KHR_D3D11_enable_available = false;
	bool XR_EXT_debug_utils_available = false;
	bool XR_EXT_hp_mixed_reality_controller_available = false;
	bool XR_VALVE_frame_controller_interaction_available = false;

#define XR_DECLARE_FN_PTR(name) PFN_##name name = nullptr
	XR_DECLARE_FN_PTR(xrGetD3D11GraphicsRequirementsKHR);
	XR_DECLARE_FN_PTR(xrCreateDebugUtilsMessengerEXT);

	void XR_CheckAvailableExtensions()
	{
		XR_KHR_D3D11_enable_available = false;
		XR_EXT_debug_utils_available = false;
		XR_EXT_hp_mixed_reality_controller_available = false;
		XR_VALVE_frame_controller_interaction_available = false;

		uint32_t extensionsCount = 0;
		XrResult result = xrEnumerateInstanceExtensionProperties(nullptr, 0, &extensionsCount, nullptr);
		if (!XR_CheckResult(result, "querying number of available extensions"))
			return;

		std::vector<XrExtensionProperties> extensionProperties (extensionsCount);
		for (auto &ext : extensionProperties)
		{
			ext.type = XR_TYPE_EXTENSION_PROPERTIES;
			ext.next = nullptr;
		}
		result = xrEnumerateInstanceExtensionProperties( nullptr, extensionProperties.size(), &extensionsCount, extensionProperties.data() );
		if (!XR_CheckResult( result, "querying available extensions" ))
			return;

		CryLogAlways("XR supported extensions:");
		for ( auto ext : extensionProperties )
		{
			CryLogAlways("  %s", ext.extensionName);

			if (strcmp(ext.extensionName, XR_KHR_D3D11_ENABLE_EXTENSION_NAME) == 0)
			{
				XR_KHR_D3D11_enable_available = true;
			}
			if (strcmp(ext.extensionName, XR_EXT_DEBUG_UTILS_EXTENSION_NAME) == 0)
			{
				XR_EXT_debug_utils_available = true;
			}
			if (strcmp(ext.extensionName, XR_EXT_HP_MIXED_REALITY_CONTROLLER_EXTENSION_NAME) == 0)
			{
				XR_EXT_hp_mixed_reality_controller_available = true;
			}
			if (strcmp(ext.extensionName, "XR_VALVE_frame_controller_interaction") == 0)
			{
				XR_VALVE_frame_controller_interaction_available = true;
			}
		}
	}

	void XR_CheckAvailableApiLayers()
	{
		uint32_t layersCount = 0;
		XrResult result = xrEnumerateApiLayerProperties(0, &layersCount, nullptr);
		if (!XR_CheckResult(result, "enumerating API layers", XR_NULL_HANDLE))
			return;

		std::vector<XrApiLayerProperties> layerProperties (layersCount);
		for (auto &layer : layerProperties)
		{
			layer.type = XR_TYPE_API_LAYER_PROPERTIES;
			layer.next = nullptr;
		}
		result = xrEnumerateApiLayerProperties(layerProperties.size(), &layersCount, layerProperties.data());
		if (!XR_CheckResult(result, "enumerating API layers", XR_NULL_HANDLE))
			return;

		CryLogAlways("XR supported API layers:");
		for ( auto layer : layerProperties )
		{
			CryLogAlways("  %s", layer.layerName);
		}
	}

	void XR_LoadExtensionFunctions(XrInstance instance)
	{
#define XR_LOAD_FN_PTR(name) XR_CheckResult(xrGetInstanceProcAddr(instance, #name, (PFN_xrVoidFunction*)& name), "loading extension function " #name, instance)
		XR_LOAD_FN_PTR(xrGetD3D11GraphicsRequirementsKHR);
		//XR_LOAD_FN_PTR(xrCreateDebugUtilsMessengerEXT);
	}

	XrBool32 XRAPI_PTR XR_DebugMessengerCallback(
			XrDebugUtilsMessageSeverityFlagsEXT              messageSeverity,
			XrDebugUtilsMessageTypeFlagsEXT                  messageTypes,
			const XrDebugUtilsMessengerCallbackDataEXT*      callbackData,
			void*                                            userData) {
		CryLogAlways("XR in %s: %s", callbackData->functionName, callbackData->message);
		return XR_TRUE;
	}

	void XR_SetupDebugMessenger(XrInstance instance)
	{
		static XrDebugUtilsMessengerEXT debugMessenger = XR_NULL_HANDLE;

		XrDebugUtilsMessengerCreateInfoEXT createInfo{ XR_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT };
		createInfo.messageSeverities = 0xffffffff;
		createInfo.messageTypes = 0xffffffff;
		createInfo.userCallback = &XR_DebugMessengerCallback;
		xrCreateDebugUtilsMessengerEXT(instance, &createInfo, &debugMessenger);
	}
}

bool OpenXRRuntime::Init()
{
	memset(&m_hudPose, 0, sizeof(m_hudPose));
	m_hudPose.orientation.w = 1;
	m_hudPose.position.z = -2.f;

	m_usingWinlatorXR = WinlatorXR::IsLikelyPresent();
	if (m_usingWinlatorXR)
	{
		// Running under WinlatorXR (Wine on a standalone Quest/Pico headset): there is no OpenXR runtime,
		// so use WinlatorXR's XrAPI UDP protocol for poses/input and compose the stereo frame ourselves.
		CryLogAlways("Detected WinlatorXR environment - using WinlatorXR XrAPI backend instead of OpenXR");
		WinlatorXR::Init();

		// The engine's crash handler can't attribute addresses to modules under Wine, so dump the module
		// map once - it makes call stacks in the log readable.
		HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, GetCurrentProcessId());
		if (snapshot != INVALID_HANDLE_VALUE)
		{
			MODULEENTRY32 me;
			me.dwSize = sizeof(me);
			if (Module32First(snapshot, &me))
			{
				do
				{
					CryLogAlways("[WinlatorXR] module %-28s base 0x%p size 0x%08X", me.szModule, (void*)me.modBaseAddr, (unsigned)me.modBaseSize);
				} while (Module32Next(snapshot, &me));
			}
			CloseHandle(snapshot);
		}

		// symmetric 90 degree placeholder FOV until the first packet reports the headset's real one
		for (int eye = 0; eye < 2; ++eye)
		{
			m_renderViews[eye].type = XR_TYPE_VIEW;
			m_renderViews[eye].pose.orientation.w = 1.f;
			m_renderViews[eye].fov.angleLeft = -DEG2RAD(45.f);
			m_renderViews[eye].fov.angleRight = DEG2RAD(45.f);
			m_renderViews[eye].fov.angleUp = DEG2RAD(45.f);
			m_renderViews[eye].fov.angleDown = -DEG2RAD(45.f);
		}

		m_input.InitWinlatorXR();
		return true;
	}

	if (!CreateInstance())
		return false;

	return true;
}

void OpenXRRuntime::Shutdown()
{
	if (m_usingWinlatorXR)
	{
		m_input.Shutdown();
		WinlatorXR::Shutdown();
		return;
	}

	m_input.Shutdown();

	xrDestroySwapchain(m_stereoSwapchain);
	m_stereoSwapchain = XR_NULL_HANDLE;
	m_stereoImages.clear();
	xrDestroySwapchain(m_hudSwapchain);
	m_hudSwapchain = XR_NULL_HANDLE;
	xrDestroySpace(m_space);
	m_space = XR_NULL_HANDLE;
	xrDestroySession(m_session);
	m_session = XR_NULL_HANDLE;
	xrDestroyInstance(m_instance);
	m_instance = XR_NULL_HANDLE;
}

void OpenXRRuntime::GetD3D11Requirements(LUID* adapterLuid, D3D_FEATURE_LEVEL* minRequiredLevel)
{
	if (m_usingWinlatorXR)
	{
		// no D3D11 interop needed: frames are composed into the game's own D3D10 back buffer
		if (adapterLuid) memset(adapterLuid, 0, sizeof(*adapterLuid));
		if (minRequiredLevel) *minRequiredLevel = (D3D_FEATURE_LEVEL)0;
		return;
	}
	XrGraphicsRequirementsD3D11KHR d3dReqs{};
	d3dReqs.type = XR_TYPE_GRAPHICS_REQUIREMENTS_D3D11_KHR;
	XR_CheckResult(xrGetD3D11GraphicsRequirementsKHR(m_instance, m_system, &d3dReqs), "getting D3D11 requirements", m_instance);
	if (adapterLuid)
		*adapterLuid = d3dReqs.adapterLuid;
	if (minRequiredLevel)
		*minRequiredLevel = d3dReqs.minFeatureLevel;
}

void OpenXRRuntime::CreateSession(ID3D11Device* device)
{
	if (m_usingWinlatorXR)
		return;
	if (m_session)
	{
		m_input.Shutdown();
		xrDestroySession(m_session);
		m_session = XR_NULL_HANDLE;
	}
	XrGraphicsBindingD3D11KHR graphicsBinding{};
	graphicsBinding.type = XR_TYPE_GRAPHICS_BINDING_D3D11_KHR;
	graphicsBinding.device = device;
	XrSessionCreateInfo createInfo{};
	createInfo.type = XR_TYPE_SESSION_CREATE_INFO;
	createInfo.next = &graphicsBinding;
	createInfo.systemId = m_system;
	XrResult result = xrCreateSession(m_instance, &createInfo, &m_session);
	XR_CheckResult(result, "creating session", m_instance);
	m_sessionActive = false;

	XrReferenceSpaceCreateInfo spaceCreateInfo{};
	spaceCreateInfo.type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO;
	spaceCreateInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
	spaceCreateInfo.poseInReferenceSpace.orientation.w = 1;
	result = xrCreateReferenceSpace(m_session, &spaceCreateInfo, &m_space);
	XR_CheckResult(result, "creating roomscale reference space", m_instance);

	m_input.Init(m_instance, m_session, m_space);
}

void OpenXRRuntime::AwaitFrame()
{
	if (m_usingWinlatorXR)
	{
		// WinlatorXR has no blocking wait: it streams poses over UDP at the headset's refresh rate, so
		// just pick up the most recent one for this frame
		UpdateWinlatorXRPose();
		return;
	}

	if (!m_session)
		return;

	XrEventDataBuffer eventData{};
	eventData.type = XR_TYPE_EVENT_DATA_BUFFER;
	XrResult result = xrPollEvent(m_instance, &eventData);
	if (!XR_CheckResult(result, "polling event", m_instance))
		return;

	if (eventData.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED)
		HandleSessionStateChange(reinterpret_cast<XrEventDataSessionStateChanged*>(&eventData));
	if (eventData.type == XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING)
		HandleSpaceRecalibration(reinterpret_cast<XrEventDataReferenceSpaceChangePending*>(&eventData));

	if (!m_sessionActive)
		return;

	XrFrameState frameState{};
	frameState.type = XR_TYPE_FRAME_STATE;
	xrWaitFrame(m_session, nullptr, &frameState);
	m_shouldRender = frameState.shouldRender;
	m_predictedDisplayTime = frameState.predictedDisplayTime;
	m_predictedNextFrameDisplayTime = m_predictedDisplayTime + frameState.predictedDisplayPeriod;

	result = xrBeginFrame(m_session, nullptr);
	XR_CheckResult(result, "begin frame", m_instance);
	m_frameStarted = true;

	XrViewLocateInfo viewLocateInfo{};
	viewLocateInfo.type = XR_TYPE_VIEW_LOCATE_INFO;
	viewLocateInfo.space = m_space;
	viewLocateInfo.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
	viewLocateInfo.displayTime = m_predictedDisplayTime;
	XrViewState viewState{XR_TYPE_VIEW_STATE};
	uint32_t viewCount = 0;
	for (int i = 0; i < 2; ++i)
	{
		m_renderViews[i].type = XR_TYPE_VIEW;
		m_renderViews[i].next = nullptr;
	}
	result = xrLocateViews(m_session, &viewLocateInfo, &viewState, 2, &viewCount, m_renderViews);
	XR_CheckResult(result, "getting eye views", m_instance);

	m_posesValid = viewState.viewStateFlags & (XR_VIEW_STATE_POSITION_TRACKED_BIT | XR_VIEW_STATE_ORIENTATION_TRACKED_BIT);
}

void OpenXRRuntime::FinishFrame()
{
	if (m_usingWinlatorXR)
	{
		// The frame itself was already composed into the back buffer by VRManager; all that is left is
		// telling WinlatorXR how to display it. fov 0/0 = "keep the headset's native FOV", which it then
		// reports back in every packet and which is what our cameras render with. Controller vibration
		// is a per-frame level (WinlatorXR applies its own decay), so forward the current amplitudes.
		WinlatorXR::SendState(m_input.GetWinlatorHapticAmplitude(0), m_input.GetWinlatorHapticAmplitude(1), m_winlatorModeVr, m_winlatorMode3d, 0.f, 0.f);

		if (UseWinlatorAER() && m_winlatorModeVr == 1)
			AdvanceAerEye();

		// periodic frame-rate report: there is no overlay/tooling on the headset, and the frame rate
		// decides how much WinlatorXR's reprojection has to warp our (stale) frames
		float now = gEnv->pTimer->GetAsyncCurTime();
		++m_winlatorFrameCount;
		if (m_winlatorLastFpsReport == 0.f)
			m_winlatorLastFpsReport = now;
		else if (now - m_winlatorLastFpsReport >= 10.f)
		{
			Vec2i size = gVR->GetRenderSize();
			CryLogAlways("[WinlatorXR] %.1f fps (modeVr %d, mode3d %d, %s, %dx%d per eye)", m_winlatorFrameCount / (now - m_winlatorLastFpsReport), m_winlatorModeVr, m_winlatorMode3d, UseWinlatorAER() ? "AER" : "SBS", size.x, size.y);
			m_winlatorFrameCount = 0;
			m_winlatorLastFpsReport = now;
		}
		return;
	}

	if (!m_frameStarted || !m_sessionActive)
		return;

	XrCompositionLayerProjectionView views[2] = {};
	views[0].type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
	views[0].pose = m_renderViews[0].pose;
	views[0].fov = m_renderViews[0].fov;
	views[0].subImage.swapchain = m_stereoSwapchain;
	views[0].subImage.imageRect.offset.x = 0;
	views[0].subImage.imageRect.offset.y = 0;
	views[0].subImage.imageRect.extent.width = m_submittedEyeWidth[0];
	views[0].subImage.imageRect.extent.height = m_submittedEyeHeight[0];
	views[1].type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
	views[1].pose = m_renderViews[1].pose;
	views[1].fov = m_renderViews[1].fov;
	views[1].subImage.swapchain = m_stereoSwapchain;
	views[1].subImage.imageRect.offset.x = m_stereoWidth;
	views[1].subImage.imageRect.offset.y = 0;
	views[1].subImage.imageRect.extent.width = m_submittedEyeWidth[1];
	views[1].subImage.imageRect.extent.height = m_submittedEyeHeight[1];

	XrCompositionLayerProjection stereoLayer{ XR_TYPE_COMPOSITION_LAYER_PROJECTION };
	stereoLayer.space = m_space;
	stereoLayer.viewCount = 2;
	stereoLayer.views = views;

	XrCompositionLayerQuad cinema3DLayerLeft{ XR_TYPE_COMPOSITION_LAYER_QUAD };
	cinema3DLayerLeft.eyeVisibility = XR_EYE_VISIBILITY_LEFT;
	cinema3DLayerLeft.space = m_space;
	cinema3DLayerLeft.subImage.swapchain = m_stereoSwapchain;
	cinema3DLayerLeft.subImage.imageRect.extent.width = m_stereoWidth;
	cinema3DLayerLeft.subImage.imageRect.extent.height = m_stereoHeight;
	cinema3DLayerLeft.pose = GetHudPose();
	cinema3DLayerLeft.size.width = GetHudWidth();
	cinema3DLayerLeft.size.height = GetHudHeight();
	cinema3DLayerLeft.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
	XrCompositionLayerQuad cinema3DLayerRight{ XR_TYPE_COMPOSITION_LAYER_QUAD };
	cinema3DLayerRight.eyeVisibility = XR_EYE_VISIBILITY_RIGHT;
	cinema3DLayerRight.space = m_space;
	cinema3DLayerRight.subImage.swapchain = m_stereoSwapchain;
	cinema3DLayerRight.subImage.imageRect.offset.x = m_stereoWidth;
	cinema3DLayerRight.subImage.imageRect.extent.width = m_stereoWidth;
	cinema3DLayerRight.subImage.imageRect.extent.height = m_stereoHeight;
	cinema3DLayerRight.pose = GetHudPose();
	cinema3DLayerRight.size.width = GetHudWidth();
	cinema3DLayerRight.size.height = GetHudHeight();
	cinema3DLayerRight.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;

	XrCompositionLayerQuad hudLayer{ XR_TYPE_COMPOSITION_LAYER_QUAD };
	hudLayer.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
	hudLayer.space = m_space;
	hudLayer.subImage.swapchain = m_hudSwapchain;
	hudLayer.subImage.imageRect.extent.width = m_hudWidth;
	hudLayer.subImage.imageRect.extent.height = m_hudHeight;
	hudLayer.pose = GetHudPose();
	hudLayer.size.width = GetHudWidth();
	hudLayer.size.height = GetHudHeight();
	hudLayer.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;

	std::vector<XrCompositionLayerBaseHeader*> layers;
	VRRenderMode renderMode = gVRRenderer->GetRenderMode();
	if (renderMode == RM_VR)
	{
		layers.push_back(reinterpret_cast<XrCompositionLayerBaseHeader*>(&stereoLayer));
	}
	else if (renderMode == RM_3D)
	{
		layers.push_back(reinterpret_cast<XrCompositionLayerBaseHeader*>(&cinema3DLayerLeft));
		layers.push_back(reinterpret_cast<XrCompositionLayerBaseHeader*>(&cinema3DLayerRight));
	}
	if (m_hudVisible)
		layers.push_back(reinterpret_cast<XrCompositionLayerBaseHeader*>(&hudLayer));

	XrFrameEndInfo endInfo{ XR_TYPE_FRAME_END_INFO };
	endInfo.displayTime = m_predictedDisplayTime;
	endInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
	endInfo.layerCount = layers.size();
	endInfo.layers = layers.data();

	XrResult result = xrEndFrame(m_session, &endInfo);
	XR_CheckResult(result, "submitting frame", m_instance);

	if (m_recalibrationPending && m_recalibrationTime < m_predictedDisplayTime)
	{
		CryLogAlways("XR runtime space recalibration - reset view\n");
		gVR->RecalibrateView();
		m_recalibrationPending = false;
	}
}

void OpenXRRuntime::Update()
{
	m_input.Update();
}

Matrix34 OpenXRRuntime::GetRenderEyeTransform(int eye) const
{
	return OpenXRToCrysis(m_renderViews[eye].pose.orientation, m_renderViews[eye].pose.position);
}

Matrix44 OpenXRRuntime::GetHmdTransform() const
{
	Matrix44 hmdTransform = GetRenderEyeTransform(0);
	hmdTransform = (hmdTransform + GetRenderEyeTransform(1)) * .5f;
	return hmdTransform;
}

void OpenXRRuntime::GetFov(int eye, float& tanl, float& tanr, float& tant, float& tanb) const
{
	tanl = tanf(m_renderViews[eye].fov.angleLeft);
	tanr = tanf(m_renderViews[eye].fov.angleRight);
	tant = tanf(m_renderViews[eye].fov.angleUp);
	tanb = tanf(m_renderViews[eye].fov.angleDown);
}

Vec2i OpenXRRuntime::GetRecommendedRenderSize() const
{
	if (m_usingWinlatorXR)
	{
		// The eye is rendered at the headset's (symmetric) FOV aspect; height is the resolution knob.
		// Under WinlatorXR the eye image *is* the game's back buffer (side-by-side halves or, with AER,
		// the whole frame), which WinlatorXR scales onto the X screen and then onto its square per-eye
		// framebuffers, so the container screen size should keep this same aspect.
		int height = max(g_pGameCVars->vr_winlatorxr_render_height, 240);
		int width = (int)(height * tanf(DEG2RAD(m_winlatorFovH) / 2.f) / tanf(DEG2RAD(m_winlatorFovV) / 2.f));
		return Vec2i(width, height);
	}

	uint32_t viewCount = 0;
	XrViewConfigurationView views[2] = {};
	views[0].type = XR_TYPE_VIEW_CONFIGURATION_VIEW;
	views[1].type = XR_TYPE_VIEW_CONFIGURATION_VIEW;
	xrEnumerateViewConfigurationViews(m_instance, m_system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 2, &viewCount, views);
	float scale = FClamp(g_pGameCVars->vr_resolution_scale, 0.25f, 4.f);
	return Vec2i(views[0].recommendedImageRectWidth * scale, views[0].recommendedImageRectHeight * scale);
}

void OpenXRRuntime::SubmitEyes(ID3D11Texture2D* leftEyeTex, const RectF& leftArea, ID3D11Texture2D* rightEyeTex, const RectF& rightArea)
{
	if (!m_sessionActive || m_usingWinlatorXR)
		return;

	D3D11_TEXTURE2D_DESC lDesc, rDesc;
	leftEyeTex->GetDesc(&lDesc);
	rightEyeTex->GetDesc(&rDesc);
	m_submittedEyeWidth[0] = lDesc.Width * leftArea.w;
	m_submittedEyeHeight[0] = lDesc.Height * leftArea.h;
	m_submittedEyeWidth[1] = rDesc.Width * rightArea.w;
	m_submittedEyeHeight[1] = rDesc.Height * rightArea.h;
	int width = max(m_submittedEyeWidth[0], m_submittedEyeWidth[1]);
	int height = max(m_submittedEyeHeight[0], m_submittedEyeHeight[1]);

	if (!m_stereoSwapchain || m_stereoWidth != width || m_stereoHeight != height)
	{
		CreateStereoSwapchain(width, height);
		if (!m_stereoSwapchain)
			return;
	}

	ComPtr<ID3D11Device> device;
	leftEyeTex->GetDevice(device.GetAddressOf());
	ComPtr<ID3D11DeviceContext> context;
	device->GetImmediateContext(context.GetAddressOf());

	uint32_t imageIdx;
	XR_CheckResult(xrAcquireSwapchainImage(m_stereoSwapchain, nullptr, &imageIdx), "acquiring swapchain image", m_instance);
	XrSwapchainImageWaitInfo waitInfo{ XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
	waitInfo.timeout = 1000000000;
	XR_CheckResult(xrWaitSwapchainImage(m_stereoSwapchain, &waitInfo), "waiting for swapchain image", m_instance);

	bool isVR = gVRRenderer->GetRenderMode() == RM_VR;
	D3D11_BOX rect;
	rect.left = lDesc.Width * leftArea.x;
	rect.top = lDesc.Height * leftArea.y;
	rect.right = rect.left + m_submittedEyeWidth[0];
	rect.bottom = rect.top + m_submittedEyeHeight[0];
	rect.front = 0;
	rect.back = 1;
	context->CopySubresourceRegion(m_stereoImages[imageIdx], 0, 0, 0, 0, leftEyeTex, 0, isVR ? &rect : nullptr);
	rect.left = rDesc.Width * rightArea.x;
	rect.top = rDesc.Height * rightArea.y;
	rect.right = rect.left + m_submittedEyeWidth[1];
	rect.bottom = rect.top + m_submittedEyeHeight[1];
	context->CopySubresourceRegion(m_stereoImages[imageIdx], 0, width, 0, 0, rightEyeTex, 0, isVR ? &rect : nullptr);

	XR_CheckResult(xrReleaseSwapchainImage(m_stereoSwapchain, nullptr), "releasing swapchain image", m_instance);
}

void OpenXRRuntime::SubmitHud(ID3D11Texture2D* hudTex)
{
	if (!m_sessionActive || m_usingWinlatorXR)
		return;

	D3D11_TEXTURE2D_DESC desc;
	hudTex->GetDesc(&desc);

	if (!m_hudSwapchain || m_hudWidth != desc.Width || m_hudHeight != desc.Height)
	{
		CreateHudSwapchain(desc.Width, desc.Height);
		if (!m_hudSwapchain)
			return;
	}

	ComPtr<ID3D11Device> device;
	hudTex->GetDevice(device.GetAddressOf());
	ComPtr<ID3D11DeviceContext> context;
	device->GetImmediateContext(context.GetAddressOf());

	uint32_t imageIdx;
	XR_CheckResult(xrAcquireSwapchainImage(m_hudSwapchain, nullptr, &imageIdx), "acquiring swapchain image", m_instance);
	XrSwapchainImageWaitInfo waitInfo{ XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
	waitInfo.timeout = 1000000000;
	XR_CheckResult(xrWaitSwapchainImage(m_hudSwapchain, &waitInfo), "waiting for swapchain image", m_instance);

	context->CopyResource(m_hudImages[imageIdx], hudTex);

	XR_CheckResult(xrReleaseSwapchainImage(m_hudSwapchain, nullptr), "releasing swapchain image", m_instance);
}

XrPosef OpenXRRuntime::GetHudPose() const
{
	return m_hudPose;
}

void OpenXRRuntime::SetHudPose(const XrPosef& pose)
{
	m_hudPose = pose;
}

void OpenXRRuntime::SetHudSize(float width, float height)
{
	m_hudDisplayWidth = width;
	m_hudDisplayHeight = height;
}

bool OpenXRRuntime::HasReverbG2BindingsExtension() const
{
	return XR_EXT_hp_mixed_reality_controller_available;
}

bool OpenXRRuntime::HasSteamFrameBindingsExtension() const
{
	return XR_VALVE_frame_controller_interaction_available;
}

bool OpenXRRuntime::CreateInstance()
{
	CryLogAlways("Creating OpenXR instance...");
	XR_CheckAvailableExtensions();
	XR_CheckAvailableApiLayers();

	if (!XR_KHR_D3D11_enable_available)
	{
		CryError("Error: XR runtime does not support D3D11");
		return false;
	}

	std::vector<const char*> enabledExtensions;
	enabledExtensions.push_back(XR_KHR_D3D11_ENABLE_EXTENSION_NAME);
	if (XR_EXT_hp_mixed_reality_controller_available)
		enabledExtensions.push_back(XR_EXT_HP_MIXED_REALITY_CONTROLLER_EXTENSION_NAME);

	XrInstanceCreateInfo createInfo{ XR_TYPE_INSTANCE_CREATE_INFO };
	createInfo.applicationInfo.apiVersion = XR_CURRENT_API_VERSION;
	createInfo.applicationInfo.applicationVersion = 1;
	createInfo.applicationInfo.engineVersion = 1;
	strcpy(createInfo.applicationInfo.applicationName, "Crysis VR");
	strcpy(createInfo.applicationInfo.engineName, "CryEngine 2");
	createInfo.enabledExtensionCount = enabledExtensions.size();
	createInfo.enabledExtensionNames = enabledExtensions.data();

	XrResult result = xrCreateInstance(&createInfo, &m_instance);
	if (!XR_CheckResult(result, "creating the instance"))
		return false;

	XrInstanceProperties instanceProperties = {
		XR_TYPE_INSTANCE_PROPERTIES,
		nullptr,
	};
	xrGetInstanceProperties(m_instance, &instanceProperties);
	CryLogAlways("OpenXR runtime: %s (v%d.%d.%d)", instanceProperties.runtimeName,
		XR_VERSION_MAJOR(instanceProperties.runtimeVersion),
		XR_VERSION_MINOR(instanceProperties.runtimeVersion),
		XR_VERSION_PATCH(instanceProperties.runtimeVersion));

	XR_LoadExtensionFunctions(m_instance);

	XrSystemGetInfo systemInfo{};
	systemInfo.type = XR_TYPE_SYSTEM_GET_INFO;
	systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
	result = xrGetSystem(m_instance, &systemInfo, &m_system);
	if (!XR_CheckResult(result, "getting system", m_instance))
	{
		char message[1024];
		snprintf(message, sizeof(message), "Could not find your headset. Make sure it is running and connected and that your active OpenXR runtime is set correctly. Your current runtime is %s", instanceProperties.runtimeName);
		MessageBoxA(nullptr, message, "No VR headset found", MB_OK);
		CryError(message);
		return false;
	}

	return true;
}

void OpenXRRuntime::HandleSessionStateChange(XrEventDataSessionStateChanged* event)
{
	XrSessionBeginInfo beginInfo = {
		XR_TYPE_SESSION_BEGIN_INFO,
		nullptr,
		XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
	};
	XrResult result;

	switch (event->state)
	{
	case XR_SESSION_STATE_READY:
		CryLogAlways("XR session is ready, beginning session...");
		result = xrBeginSession(m_session, &beginInfo);
		XR_CheckResult(result, "beginning session", m_instance);
		m_sessionActive = true;
		break;

	case XR_SESSION_STATE_SYNCHRONIZED:
		CryLogAlways("XR session state changed to synchronized");
		m_sessionActive = true;
		break;
	case XR_SESSION_STATE_VISIBLE:
		CryLogAlways("XR session state changed to visible");
		m_sessionActive = true;
		if (m_lastSessionState == XR_SESSION_STATE_FOCUSED)
		{
			// happens when e.g. the SteamVR system menu is opened. In this case we want to pause the game and go to the menu
			SAFE_MENU_FUNC(ShowInGameMenu(true));
		}
		break;
	case XR_SESSION_STATE_FOCUSED:
		CryLogAlways("XR session state changed to focused");
		m_sessionActive = true;
		break;

	case XR_SESSION_STATE_IDLE:
		m_sessionActive = false;
		break;

	case XR_SESSION_STATE_STOPPING:
	case XR_SESSION_STATE_LOSS_PENDING:
		m_sessionActive = false;
		CryLogAlways("XR session lost or stopped");
		result = xrEndSession(m_session);
		XR_CheckResult(result, "ending session", m_instance);
		break;

	case XR_SESSION_STATE_EXITING:
		CryLogAlways("Received quit request from XR runtime, shutting down...");
		gEnv->pSystem->Quit();
		break;
	}

	m_lastSessionState = event->state;
}

void OpenXRRuntime::HandleSpaceRecalibration(XrEventDataReferenceSpaceChangePending* event)
{
	m_recalibrationPending = true;
	m_recalibrationTime = event->changeTime;
}

void OpenXRRuntime::CreateStereoSwapchain(int width, int height)
{
	if (m_stereoSwapchain)
	{
		xrDestroySwapchain(m_stereoSwapchain);
		m_stereoSwapchain = XR_NULL_HANDLE;
		m_stereoWidth = 0;
		m_stereoHeight = 0;
	}

	CryLogAlways("XR: Creating stereo swapchain of size %i x %i", width, height);

	XrSwapchainCreateInfo createInfo{ XR_TYPE_SWAPCHAIN_CREATE_INFO };
	createInfo.width = width * 2;
	createInfo.height = height;
	createInfo.format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	createInfo.usageFlags = XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
	createInfo.sampleCount = 1;
	createInfo.faceCount = 1;
	createInfo.arraySize = 1;
	createInfo.mipCount = 1;
	XrResult result = xrCreateSwapchain(m_session, &createInfo, &m_stereoSwapchain);
	if (!XR_CheckResult(result, "creating stereo swapchain", m_instance))
		return;
	m_stereoWidth = width;
	m_stereoHeight = height;

	uint32_t imageCount;
	xrEnumerateSwapchainImages(m_stereoSwapchain, 0, &imageCount, nullptr);
	std::vector<XrSwapchainImageD3D11KHR> images (imageCount);
	for (auto &image : images)
	{
		image.type = XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR;
		image.next = nullptr;
	}
	result = xrEnumerateSwapchainImages(m_stereoSwapchain, images.size(), &imageCount, reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data()));
	XR_CheckResult(result, "getting swapchain images", m_instance);
	m_stereoImages.clear();

	for (const auto& image: images)
	{
		m_stereoImages.push_back(image.texture);
	}
}

void OpenXRRuntime::CreateHudSwapchain(int width, int height)
{
	if (m_hudSwapchain)
	{
		xrDestroySwapchain(m_hudSwapchain);
		m_hudSwapchain = XR_NULL_HANDLE;
		m_hudWidth = 0;
		m_hudHeight = 0;
	}

	CryLogAlways("XR: Creating HUD swapchain of size %i x %i", width, height);

	XrSwapchainCreateInfo createInfo{ XR_TYPE_SWAPCHAIN_CREATE_INFO };
	createInfo.width = width;
	createInfo.height = height;
	createInfo.format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	createInfo.usageFlags = XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
	createInfo.sampleCount = 1;
	createInfo.faceCount = 1;
	createInfo.arraySize = 1;
	createInfo.mipCount = 1;
	XrResult result = xrCreateSwapchain(m_session, &createInfo, &m_hudSwapchain);
	if (!XR_CheckResult(result, "creating hud swapchain", m_instance))
		return;
	m_hudWidth = width;
	m_hudHeight = height;

	uint32_t imageCount;
	xrEnumerateSwapchainImages(m_hudSwapchain, 0, &imageCount, nullptr);
	std::vector<XrSwapchainImageD3D11KHR> images (imageCount);
	for (auto &image : images)
	{
		image.type = XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR;
		image.next = nullptr;
	}
	result = xrEnumerateSwapchainImages(m_hudSwapchain, images.size(), &imageCount, reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data()));
	XR_CheckResult(result, "getting swapchain images", m_instance);
	m_hudImages.clear();

	for (const auto& image: images)
	{
		m_hudImages.push_back(image.texture);
	}
}

// ---------------------------------------------------------------------------------------------------
// WinlatorXR backend
// ---------------------------------------------------------------------------------------------------

bool OpenXRRuntime::UseWinlatorAER() const
{
	return m_usingWinlatorXR && g_pGameCVars && g_pGameCVars->vr_winlatorxr_aer != 0;
}

void OpenXRRuntime::UpdateWinlatorXRPose()
{
	WinlatorXR::InputState state = WinlatorXR::GetLatestState();
	if (!state.valid)
	{
		// keep whatever we had (initially: invalid) until the first packet arrives
		return;
	}

	float qx = state.hmdQx, qy = state.hmdQy, qz = state.hmdQz, qw = state.hmdQw;
	float len = sqrtf(qx * qx + qy * qy + qz * qz + qw * qw);
	if (len < 1e-4f)
	{
		// degenerate (all-zero) quaternion, e.g. before the runtime has produced its first real pose
		return;
	}
	qx /= len; qy /= len; qz /= len; qw /= len;

	// Lift LOCAL-space poses to floor level: protocol 0.5 reports the head's height above the floor
	// (STAGE space), so offset = altitude - local y. Kept as a running value (it only changes if
	// WinlatorXR recentres) and shared with the controller poses via GetWinlatorFloorOffset().
	if (state.hmdAltitude > 0.3f && state.hmdAltitude < 3.f)
	{
		float offset = state.hmdAltitude - state.hmdY;
		if (!m_winlatorFloorOffsetValid)
			CryLogAlways("[WinlatorXR] floor offset from HMD altitude: %.3f m (head %.3f m above floor)", offset, state.hmdAltitude);
		m_winlatorFloorOffset = offset;
		m_winlatorFloorOffsetValid = true;
	}

	// eye separation: WinlatorXR reports the distance between the two OpenXR eye views in metres.
	// Be lenient about units in case a future protocol version switches to millimetres.
	float ipd = state.ipd;
	if (ipd > 1.f)
		ipd *= 0.001f;
	if (ipd >= 0.045f && ipd <= 0.085f)
		m_winlatorEyeSeparation = ipd;

	// symmetric FOV as reported by the headset runtime (degrees); ignore obviously bogus values
	if (state.fovH >= 40.f && state.fovH <= 150.f && state.fovV >= 40.f && state.fovV <= 150.f)
	{
		if (fabsf(state.fovH - m_winlatorFovH) > 1e-3f || fabsf(state.fovV - m_winlatorFovV) > 1e-3f)
		{
			m_winlatorFovH = state.fovH;
			m_winlatorFovV = state.fovV;
			CryLogAlways("[WinlatorXR] FOV updated: horz %.1f deg  vert %.1f deg", state.fovH, state.fovV);
		}
	}

	// WinlatorXR only gives us the centre pose plus the IPD, so the eye offset is a plain horizontal
	// shift along the head's local x axis (left eye towards -x, right eye towards +x; OpenXR convention)
	XrQuaternionf orientation = { qx, qy, qz, qw };
	// local +x axis of the head rotation
	float rightX = 1.f - 2.f * (qy * qy + qz * qz);
	float rightY = 2.f * (qx * qy + qw * qz);
	float rightZ = 2.f * (qx * qz - qw * qy);
	float floorY = state.hmdY + GetWinlatorFloorOffset();
	for (int eye = 0; eye < 2; ++eye)
	{
		float shift = (eye == 0 ? -0.5f : 0.5f) * m_winlatorEyeSeparation;
		m_renderViews[eye].type = XR_TYPE_VIEW;
		m_renderViews[eye].next = nullptr;
		m_renderViews[eye].pose.orientation = orientation;
		m_renderViews[eye].pose.position.x = state.hmdX + rightX * shift;
		m_renderViews[eye].pose.position.y = floorY + rightY * shift;
		m_renderViews[eye].pose.position.z = state.hmdZ + rightZ * shift;
		m_renderViews[eye].fov.angleLeft = -DEG2RAD(m_winlatorFovH) / 2.f;
		m_renderViews[eye].fov.angleRight = DEG2RAD(m_winlatorFovH) / 2.f;
		m_renderViews[eye].fov.angleUp = DEG2RAD(m_winlatorFovV) / 2.f;
		m_renderViews[eye].fov.angleDown = -DEG2RAD(m_winlatorFovV) / 2.f;
	}
	m_posesValid = true;
	// remember which of WinlatorXR's pose slots this frame is rendered with (0..252 in steps of 12)
	m_winlatorFrameSync = clamp_tpl(state.frameId, 0, 255);

	if (!m_winlatorPoseLogged)
	{
		m_winlatorPoseLogged = true;
		CryLogAlways("[WinlatorXR] Received first head pose: pos=(%.3f, %.3f, %.3f) ipd=%.4f m fov=%.1fx%.1f", state.hmdX, state.hmdY, state.hmdZ, m_winlatorEyeSeparation, state.fovH, state.fovV);
	}
}
