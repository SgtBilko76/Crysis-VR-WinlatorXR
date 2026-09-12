#pragma once
#include <openxr/openxr.h>

#include "EVRHand.h"
#include "IActionMapManager.h"
#include "WinlatorXR.h"

class OpenXRInput
{
public:
	void Init(XrInstance instance, XrSession session, XrSpace space);
	// WinlatorXR backend: the same gameplay mappings, fed from the XrAPI packet instead of OpenXR
	// actions (the bindings mirror the Touch profile in SuggestBindings, see UpdateWinlatorXRActions)
	void InitWinlatorXR();
	void Shutdown();

	void Update();

	Matrix34 GetControllerTransform(int hand);
	Matrix34 GetControllerWeaponTransform(int hand);
	Vec3 GetControllerVelocity(int hand);

	void EnableHandMovementForQuickMenu();
	void DisableHandMovementForQuickMenu();

	float GetGripAmount(int side) const;

	void SendHapticEvent(EVRHand hand, float duration, float amplitude, float frequency = XR_FREQUENCY_UNSPECIFIED);
	void SendHapticEvent(float duration, float amplitude, float frequency = XR_FREQUENCY_UNSPECIFIED);
	void StopHaptics(EVRHand hand);
	void StopHaptics();

	// WinlatorXR has no haptic API of its own; OpenXRRuntime forwards this level once per frame via
	// WinlatorXR::SendState (0 = off)
	float GetWinlatorHapticAmplitude(int side) const;

private:
	struct BooleanAction
	{
		XrAction handle = XR_NULL_HANDLE;
		ActionId* onPress = nullptr;
		ActionId* onLongPress = nullptr;
		bool sendRelease = true;
		bool sendLongRelease = true;
		bool pressOnRelease = false;
		float longPressActivationTime = 0.25f;
		bool longPressActive = false;
		float timePressed = -1;
	};

	XrInstance m_instance = XR_NULL_HANDLE;
	XrSession m_session = XR_NULL_HANDLE;
	XrSpace m_trackingSpace = XR_NULL_HANDLE;

	XrActionSet m_ingameSet = XR_NULL_HANDLE;
	XrAction m_controller[2] = {};
	XrAction m_moveX = XR_NULL_HANDLE;
	XrAction m_moveY = XR_NULL_HANDLE;
	XrAction m_rotateYaw = XR_NULL_HANDLE;
	XrAction m_rotatePitch = XR_NULL_HANDLE;
	XrAction m_jumpCrouch = XR_NULL_HANDLE;
	XrAction m_grip[2] = {};
	XrAction m_trigger[2] = {};
	XrAction m_haptics[2] = {};
	BooleanAction m_primaryFire;
	BooleanAction m_sprint;
	BooleanAction m_reload;
	BooleanAction m_menu;
	BooleanAction m_suitMenu;
	BooleanAction m_binoculars;
	BooleanAction m_nextWeapon;
	BooleanAction m_use;
	BooleanAction m_gripUse;
	BooleanAction m_nightvision;
	BooleanAction m_melee;
	BooleanAction m_grenades;
	XrSpace m_gripSpace[2] = {};

	XrActionSet m_menuSet = XR_NULL_HANDLE;
	BooleanAction m_menuClick;
	BooleanAction m_menuBack;
	BooleanAction m_dropWeapon;

	XrActionSet m_vehicleSet = XR_NULL_HANDLE;
	BooleanAction m_vecBoost;
	BooleanAction m_vecAfterburner;
	BooleanAction m_vecSecondaryFire;
	BooleanAction m_vecAscend;
	BooleanAction m_vecHorn;
	BooleanAction m_vecLights;
	BooleanAction m_vecExit;
	BooleanAction m_vecSwitchSeatView;

	float m_gripAmount[2] = {};
	float m_triggerAmount[2] = {};

	bool m_wasJumpActive = false;
	bool m_wasCrouchActive = false;
	int m_snapTurnState = 0;

	static const int MOUSE_SAMPLE_COUNT = 10;
	Vec2 m_hudMousePosSamples[MOUSE_SAMPLE_COUNT];
	int m_curMouseSampleIdx = 0;

	bool m_quickMenuActive = false;
	Vec3 m_quickMenuInitialHandPosition;

	Vec3 m_controllerPos[2];
	Quat m_controllerRot[2];

	// --- WinlatorXR backend ---
	// Synthetic action handles: small integers that never collide with real OpenXR handles, so the
	// existing gameplay code can keep passing XrAction around unchanged. (XrAction is a pointer typedef
	// on 64 bit and a uint64_t on 32 bit; both accept an integer cast.)
	enum WxrAction : unsigned int
	{
		WXR_NONE = 0,
		WXR_PRIMARY_FIRE, WXR_SPRINT, WXR_RELOAD, WXR_MENU, WXR_SUIT_MENU, WXR_BINOCULARS, WXR_NEXT_WEAPON,
		WXR_USE, WXR_GRIP_USE, WXR_NIGHTVISION, WXR_MELEE, WXR_GRENADES,
		WXR_MENU_CLICK, WXR_MENU_BACK, WXR_DROP_WEAPON,
		WXR_VEC_BOOST, WXR_VEC_AFTERBURNER, WXR_VEC_SECONDARY_FIRE, WXR_VEC_ASCEND, WXR_VEC_HORN, WXR_VEC_LIGHTS, WXR_VEC_EXIT, WXR_VEC_SWITCH,
		WXR_MOVE_X, WXR_MOVE_Y, WXR_ROTATE_YAW, WXR_ROTATE_PITCH, WXR_JUMP_CROUCH,
		WXR_LGRIP, WXR_LTRIGGER, WXR_RGRIP, WXR_RTRIGGER,
		WXR_ACTION_COUNT
	};
	struct WxrBool { bool active = false; bool state = false; bool changed = false; };
	struct WxrFloat { bool active = false; float value = 0.f; };

	static XrAction WxrHandle(WxrAction action) { return (XrAction)(uintptr_t)action; }
	static WxrAction WxrFromHandle(XrAction handle)
	{
		uintptr_t id = (uintptr_t)handle;
		return id < WXR_ACTION_COUNT ? (WxrAction)id : WXR_NONE;
	}

	bool m_usingWinlatorXR = false;
	WinlatorXR::InputState m_wxrState;
	WxrBool m_wxrBool[WXR_ACTION_COUNT];
	WxrFloat m_wxrFloat[WXR_ACTION_COUNT];
	float m_wxrHapticAmplitude[2] = { 0.f, 0.f };
	float m_wxrHapticEndTime[2] = { 0.f, 0.f };
	Vec3 m_wxrVelocity[2];
	bool m_wxrWarnedNoGrip = false;
	bool m_wxrMouseDown = false;

	void UpdateWinlatorXRActions();
	void SetWxrBool(WxrAction action, bool state);
	void SetWxrFloat(WxrAction action, float value);
	// raw controller pose (Crysis space, before the grip correction) from the latest packet
	Matrix34 GetWinlatorControllerPose(int hand) const;
	// backend-neutral queries used by the Update*/HandleAction helpers
	void GetBoolState(XrAction action, XrActionStateBoolean& out);
	void GetFloatState(XrAction action, XrActionStateFloat& out);
	// --- end WinlatorXR backend ---

	void CreateInputActions();
	void SuggestBindings();
	void AttachActionSets();
	void CreateBooleanAction(XrActionSet actionSet, BooleanAction& action, const char* name, const char* description, ActionId* onPress, ActionId* onLongPress = nullptr, bool sendRelease = true, bool sendLongRelease = true, bool pressOnRelease = false, float longPressActivationTime = 0.25f);
	void UpdateMeleeAttacks();
	void UpdateIngameActions();
	void UpdateVehicleActions();
	void UpdateMenuActions();
	void UpdateHUDActions();
	void UpdatePlayerMovement();
	void UpdateGripAmount();
	void UpdateWeaponScopes();
	void UpdateBooleanAction(BooleanAction& action);
	void UpdateBooleanActionForMenu(BooleanAction& action, EDeviceId device, EKeyId key);
	void UpdateControllerPoses();

	bool CalcControllerHudIntersection(int hand, float& x, float& y);

	void DestroyAction(XrAction& action);
};
