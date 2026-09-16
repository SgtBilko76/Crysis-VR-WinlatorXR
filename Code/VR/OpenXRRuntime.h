#pragma once
#include <openxr/openxr.h>
#include "OpenXRInput.h"

struct ID3D11Device;
struct ID3D11Texture2D;
struct _LUID;
enum D3D_FEATURE_LEVEL;

class OpenXRRuntime
{
public:
	bool Init();
	void Shutdown();
	void GetD3D11Requirements(_LUID* adapterLuid, D3D_FEATURE_LEVEL* minRequiredLevel);
	void CreateSession(ID3D11Device* device);
	void AwaitFrame();
	void FinishFrame();

	void Update();

	Matrix34 GetRenderEyeTransform(int eye) const;
	Matrix44 GetHmdTransform() const;
	void GetFov(int eye, float& tanl, float& tanr, float& tant, float& tanb) const;
	Vec2i GetRecommendedRenderSize() const;

	void SubmitEyes(ID3D11Texture2D* leftEyeTex, const RectF& leftArea, ID3D11Texture2D* rightEyeTex, const RectF& rightArea);
	void SubmitHud(ID3D11Texture2D* hudTex);

	XrTime GetNextFrameDisplayTime() const { return m_predictedNextFrameDisplayTime; }

	OpenXRInput* GetInput() { return &m_input; }

	XrPosef GetHudPose() const;
	void SetHudPose(const XrPosef& pose);
	void SetHudSize(float width, float height);
	float GetHudWidth() const { return m_hudDisplayWidth; }
	float GetHudHeight() const { return m_hudDisplayHeight; }
	void SetHudVisibility(bool visible) { m_hudVisible = visible;}
	bool IsHudVisible() const { return m_hudVisible; }

	bool ArePosesValid() const { return m_posesValid; }

	bool HasReverbG2BindingsExtension() const;
	bool HasSteamFrameBindingsExtension() const;

	// --- WinlatorXR backend (standalone Quest/Pico via Wine, see VR/WinlatorXR.h) -----------------
	// When running under WinlatorXR there is no OpenXR runtime. Instead the head/eye views, FOV and
	// HUD pose data above are fed from WinlatorXR's UDP packets, so the rest of the mod keeps using
	// this class unchanged. The stereo frame is composed into the game's back buffer by VRManager.
	bool IsUsingWinlatorXR() const { return m_usingWinlatorXR; }
	// alternate-eye rendering: every frame renders and carries a single eye at full resolution
	bool UseWinlatorAER() const;
	// which eye the current frame renders/carries in AER mode (0 = left, 1 = right)
	int CurrentAerEye() const { return m_winlatorAerEye; }
	void AdvanceAerEye() { m_winlatorAerEye ^= 1; }
	// pose slot id of the packet the current frame was rendered with (painted into the sync pixel)
	int GetWinlatorFrameSyncId() const { return m_winlatorFrameSync; }
	// distance between the eyes as reported by WinlatorXR, in metres
	float GetWinlatorEyeSeparation() const { return m_winlatorEyeSeparation; }
	// y offset (metres) to add to any raw WinlatorXR pose position to make it floor-relative
	float GetWinlatorFloorOffset() const { return m_winlatorFloorOffsetValid ? m_winlatorFloorOffset : 0.f; }
	// what to tell WinlatorXR to do with the frame that was just composed (see WinlatorXR::SendState)
	void SetWinlatorFrameMode(int modeVr, int mode3d) { m_winlatorModeVr = modeVr; m_winlatorMode3d = mode3d; }
	int GetWinlatorModeVr() const { return m_winlatorModeVr; }

private:
	OpenXRInput m_input;
	XrInstance m_instance = XR_NULL_HANDLE;
	XrSystemId m_system = 0;
	XrSession m_session = XR_NULL_HANDLE;
	XrSpace m_space = XR_NULL_HANDLE;
	bool m_sessionActive = false;
	bool m_frameStarted = false;
	bool m_shouldRender = false;
	XrTime m_predictedDisplayTime = 0;
	XrTime m_predictedNextFrameDisplayTime = 0;
	XrView m_renderViews[2] = {};
	bool m_posesValid = false;
	XrSwapchain m_stereoSwapchain = XR_NULL_HANDLE;
	int m_stereoWidth = 0;
	int m_stereoHeight = 0;
	int m_submittedEyeWidth[2] = { 0, 0 };
	int m_submittedEyeHeight[2] = { 0, 0 };
	std::vector<ID3D11Texture2D*> m_stereoImages;
	XrSwapchain m_hudSwapchain = XR_NULL_HANDLE;
	int m_hudWidth = 0;
	int m_hudHeight = 0;
	bool m_hudVisible = true;
	std::vector<ID3D11Texture2D*> m_hudImages;
	XrSessionState m_lastSessionState = XR_SESSION_STATE_UNKNOWN;

	XrPosef m_hudPose;
	float m_hudDisplayWidth = 2;
	float m_hudDisplayHeight = 2;

	bool m_recalibrationPending = false;
	XrTime m_recalibrationTime = 0;

	bool CreateInstance();
	void HandleSessionStateChange(XrEventDataSessionStateChanged* event);
	void HandleSpaceRecalibration(XrEventDataReferenceSpaceChangePending* event);
	void CreateStereoSwapchain(int width, int height);
	void CreateHudSwapchain(int width, int height);

	// --- WinlatorXR backend state ---
	bool m_usingWinlatorXR = false;
	int m_winlatorAerEye = 0;
	int m_winlatorFrameSync = 0;
	int m_winlatorModeVr = 2;
	int m_winlatorMode3d = 0;
	float m_winlatorEyeSeparation = 0.064f;
	float m_winlatorFovH = 90.f;
	float m_winlatorFovV = 90.f;
	// native FOV latched from the first packet (0 = not seen yet), sent back every frame
	float m_winlatorNativeFovH = 0.f;
	float m_winlatorNativeFovV = 0.f;
	// WinlatorXR poses live in OpenXR LOCAL space (origin = headset at session start, not the floor).
	// Protocol 0.5 also reports the head's height above the floor, from which we derive this offset
	// to lift all poses (head and controllers) into a floor-relative frame like the STAGE space the
	// OpenXR path uses.
	float m_winlatorFloorOffset = 0.f;
	bool m_winlatorFloorOffsetValid = false;
	bool m_winlatorPoseLogged = false;
	int m_winlatorFrameCount = 0;
	float m_winlatorLastFpsReport = 0.f;

	// pulls the latest WinlatorXR packet and turns it into m_renderViews / m_posesValid
	void UpdateWinlatorXRPose();
};

extern OpenXRRuntime *gXR;
