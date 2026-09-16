#include "StdAfx.h"
#include "OpenXRInput.h"

#include "GameActions.h"
#include "GameCVars.h"
#include "imgui.h"
#include "IPlayerInput.h"
#include "Melee.h"
#include "OpenXRRuntime.h"
#include "Player.h"
#include "VRHaptics.h"
#include "VRManager.h"
#include "VRRenderer.h"
#include "Weapon.h"
#include "HUD/HUD.h"
#include "Menus/FlashMenuObject.h"

extern bool XR_CheckResult(XrResult result, const char* description, XrInstance instance = XR_NULL_HANDLE);
extern Matrix34 OpenXRToCrysis(const XrQuaternionf& orientation, const XrVector3f& position);
extern Vec3 OpenXRToCrysis(const XrVector3f& position);

namespace
{
	std::string replace_all(std::string input, const std::string& search, const std::string& replace)
	{
		if (search.empty())
			return input;

		size_t pos = 0;
		while ((pos = input.find(search, pos)) != std::string::npos)
		{
			input.replace(pos, search.length(), replace);
			pos += replace.length();
		}
		return input;
	}
}

class SuggestedProfileBinding
{
public:
	SuggestedProfileBinding(XrInstance instance, bool hasXYButtonsOnLeft) : m_instance(instance), m_hasXYButtonsOnLeft(hasXYButtonsOnLeft) {}

	void AddBinding(XrAction action, const char* binding)
	{
		const char* weaponHand = g_pGameCVars->vr_weapon_hand == 0 ? "left" : "right";
		const char* nonWeaponHand = g_pGameCVars->vr_weapon_hand == 1 ? "left" : "right";
		const char* movementHand = g_pGameCVars->vr_movement_hand == 0 ? "left" : "right";
		const char* nonMovementHand = g_pGameCVars->vr_movement_hand == 1 ? "left" : "right";
		std::string finalBinding = binding;
		finalBinding = replace_all(finalBinding, "<weapon>", weaponHand);
		finalBinding = replace_all(finalBinding, "<!weapon>", nonWeaponHand);
		finalBinding = replace_all(finalBinding, "<movement>", movementHand);
		finalBinding = replace_all(finalBinding, "<!movement>", nonMovementHand);

		if (m_hasXYButtonsOnLeft && finalBinding.find("/user/hand/left") == 0)
		{
			size_t pos = finalBinding.size() - 2;
			if (finalBinding.substr(pos) == "/a")
				finalBinding.replace(pos, 2, "/x");
			if (finalBinding.substr(pos) == "/b")
				finalBinding.replace(pos, 2, "/y");
			pos = finalBinding.size() - strlen("/a/click");
			if (finalBinding.substr(pos) == "/a/click" || finalBinding.substr(pos) == "/a/touch")
				finalBinding.replace(pos, 2, "/x");
			if (finalBinding.substr(pos) == "/b/click" || finalBinding.substr(pos) == "/b/touch")
				finalBinding.replace(pos, 2, "/y");
		}

		XrPath bindingPath;
		XR_CheckResult(xrStringToPath(m_instance, finalBinding.c_str(), &bindingPath), "string to path");
		m_bindings.push_back({ action, bindingPath });
	}

	void SuggestBindings(const char* profile)
	{
		XrInteractionProfileSuggestedBinding suggestedBindings{ XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING };
		XR_CheckResult(xrStringToPath(m_instance, profile, &suggestedBindings.interactionProfile), "profile string to path");
		suggestedBindings.suggestedBindings = m_bindings.data();
		suggestedBindings.countSuggestedBindings = m_bindings.size();
		XR_CheckResult(xrSuggestInteractionProfileBindings(m_instance, &suggestedBindings), "suggesting bindings");
	}

private:
	XrInstance m_instance;
	std::vector<XrActionSuggestedBinding> m_bindings;
	bool m_hasXYButtonsOnLeft;
};

void OpenXRInput::Init(XrInstance instance, XrSession session, XrSpace space)
{
	m_instance = instance;
	m_session = session;
	m_trackingSpace = space;

	XrActionSetCreateInfo setCreateInfo{ XR_TYPE_ACTION_SET_CREATE_INFO };
	strcpy(setCreateInfo.actionSetName, "ingame");
	strcpy(setCreateInfo.localizedActionSetName, "Ingame");
	XR_CheckResult(xrCreateActionSet(m_instance, &setCreateInfo, &m_ingameSet), "creating ingame action set", m_instance);
	strcpy(setCreateInfo.actionSetName, "menu");
	strcpy(setCreateInfo.localizedActionSetName, "Menu");
	XR_CheckResult(xrCreateActionSet(m_instance, &setCreateInfo, &m_menuSet), "creating menu action set", m_instance);
	strcpy(setCreateInfo.actionSetName, "vehicles");
	strcpy(setCreateInfo.localizedActionSetName, "Vehicles");
	XR_CheckResult(xrCreateActionSet(m_instance, &setCreateInfo, &m_vehicleSet), "creating vehicle action set", m_instance);

	CreateInputActions();
	SuggestBindings();
	AttachActionSets();
}

void OpenXRInput::Shutdown()
{
	if (m_usingWinlatorXR)
	{
		m_usingWinlatorXR = false;
		return;
	}

	DestroyAction(m_jumpCrouch);
	DestroyAction(m_moveX);
	DestroyAction(m_moveY);
	DestroyAction(m_rotatePitch);
	DestroyAction(m_rotateYaw);
	for (int i = 0; i < 2; ++i)
	{
		DestroyAction(m_controller[i]);
		DestroyAction(m_grip[i]);
		DestroyAction(m_trigger[i]);
		DestroyAction(m_haptics[i]);
	}
	DestroyAction(m_primaryFire.handle);
	DestroyAction(m_sprint.handle);
	DestroyAction(m_reload.handle);
	DestroyAction(m_menu.handle);
	DestroyAction(m_suitMenu.handle);
	DestroyAction(m_binoculars.handle);
	DestroyAction(m_nextWeapon.handle);
	DestroyAction(m_use.handle);
	DestroyAction(m_gripUse.handle);
	DestroyAction(m_nightvision.handle);
	DestroyAction(m_melee.handle);
	DestroyAction(m_grenades.handle);
	DestroyAction(m_menuClick.handle);
	DestroyAction(m_menuBack.handle);
	DestroyAction(m_dropWeapon.handle);
	DestroyAction(m_vecBoost.handle);
	DestroyAction(m_vecAfterburner.handle);
	DestroyAction(m_vecAscend.handle);
	DestroyAction(m_vecSecondaryFire.handle);
	DestroyAction(m_vecHorn.handle);
	DestroyAction(m_vecLights.handle);
	DestroyAction(m_vecExit.handle);
	DestroyAction(m_vecSwitchSeatView.handle);

	xrDestroySpace(m_gripSpace[0]);
	xrDestroySpace(m_gripSpace[1]);
	xrDestroyActionSet(m_ingameSet);
	xrDestroyActionSet(m_menuSet);
	xrDestroyActionSet(m_vehicleSet);

	m_session = XR_NULL_HANDLE;
	m_instance = XR_NULL_HANDLE;
}

void OpenXRInput::Update()
{
	if (m_usingWinlatorXR)
	{
		UpdateWinlatorXRActions();
	}
	else
	{
		if (!m_session)
			return;

		std::vector<XrActiveActionSet> activeSets;
		activeSets.push_back({ m_ingameSet, XR_NULL_PATH });
		activeSets.push_back({ m_menuSet, XR_NULL_PATH });
		activeSets.push_back({ m_vehicleSet, XR_NULL_PATH });
		XrActionsSyncInfo syncInfo{ XR_TYPE_ACTIONS_SYNC_INFO };
		syncInfo.countActiveActionSets = activeSets.size();
		syncInfo.activeActionSets = activeSets.data();
		XR_CheckResult(xrSyncActions(m_session, &syncInfo), "syncing actions");
	}

	UpdateControllerPoses();

	// Under WinlatorXR a menu shown on WinlatorXR's flat screen is operated by WinlatorXR's own controller
	// pointer, which moves the (Win32) hardware mouse for us. When we compose the menu ourselves as a panel
	// in the room (VR frame mode), the controller ray intersection has to drive the cursor, like in-game.
	bool nativePointer = m_usingWinlatorXR && g_pGameCVars->vr_winlatorxr_native_menu_pointer != 0 && g_pGame->GetMenu() && g_pGame->GetMenu()->IsMenuActive()
		&& gXR->GetWinlatorModeVr() != 1;

	float pointerX, pointerY;
	if (!nativePointer && CalcControllerHudIntersection(g_pGameCVars->vr_weapon_hand, pointerX, pointerY))
	{
		m_hudMousePosSamples[m_curMouseSampleIdx].x = pointerX;
		m_hudMousePosSamples[m_curMouseSampleIdx].y = pointerY;
		m_curMouseSampleIdx = (m_curMouseSampleIdx + 1) % MOUSE_SAMPLE_COUNT;

		Vec2 smoothedMousePos (0, 0);
		for (int i = 0; i < MOUSE_SAMPLE_COUNT; ++i)
		{
			smoothedMousePos += m_hudMousePosSamples[i];
		}
		smoothedMousePos /= MOUSE_SAMPLE_COUNT;

		Vec2i windowSize = gVRRenderer->GetWindowSize();
		gEnv->pHardwareMouse->SetHardwareMouseClientPosition(smoothedMousePos.x * windowSize.x, smoothedMousePos.y * windowSize.y);
		Vec2i renderSize = gVR->GetRenderSize();
		ImGui::GetIO().AddMousePosEvent(smoothedMousePos.x * renderSize.x, smoothedMousePos.y * renderSize.y);
	}
	else
	{
		Vec2i windowSize = gVRRenderer->GetWindowSize();
		Vec2i renderSize = gVR->GetRenderSize();
		float mouseX, mouseY;
		gEnv->pHardwareMouse->GetHardwareMouseClientPosition(&mouseX, &mouseY);
		ImGui::GetIO().AddMousePosEvent(mouseX / windowSize.x * renderSize.x, mouseY / windowSize.y * renderSize.y);
	}

	if (g_pGame->GetMenu()->IsMenuActive())
	{
		UpdateMenuActions();
		return;
	}
	if (g_pGame->GetHUD() && g_pGame->GetHUD()->GetModalHUD())
	{
		UpdateHUDActions();
		return;
	}

	CPlayer *pPlayer = static_cast<CPlayer *>(gEnv->pGame->GetIGameFramework()->GetClientActor());
	if (pPlayer && pPlayer->GetLinkedVehicle())
	{
		UpdateVehicleActions();
	}
	else
	{
		UpdateIngameActions();
	}
}

Matrix34 OpenXRInput::GetControllerTransform(int hand)
{
	hand = clamp_tpl(hand, 0, 1);
	return Matrix34(Vec3(1, 1, 1), m_controllerRot[hand], m_controllerPos[hand]);
}

Matrix34 OpenXRInput::GetControllerWeaponTransform(int hand)
{
	// Weapon bones are offset to what our grip pose is, so we need to rotate the pose a bit
	Matrix33 correction = Matrix33::CreateRotationX(-gf_PI/2) * Matrix33::CreateRotationY(-gf_PI/2);
	int dir = hand == 1 ? 1 : -1;
	Matrix33 gripAngleAdjust = Matrix33::CreateRotationX(DEG2RAD(g_pGameCVars->vr_weapon_pitch_offset)) * Matrix33::CreateRotationZ(dir * DEG2RAD(g_pGameCVars->vr_weapon_yaw_offset));
	return GetControllerTransform(hand) * gripAngleAdjust * correction;
}

Vec3 OpenXRInput::GetControllerVelocity(int hand)
{
	hand = clamp_tpl(hand, 0, 1);

	if (m_usingWinlatorXR)
	{
		// the packet carries no velocity; UpdateWinlatorXRActions finite-differences the aim position
		return m_wxrVelocity[hand];
	}

	XrSpaceLocation location{ XR_TYPE_SPACE_LOCATION };
	XrSpaceVelocity velocity { XR_TYPE_SPACE_VELOCITY };
	location.next = &velocity;
	XR_CheckResult(xrLocateSpace(m_gripSpace[hand], m_trackingSpace, gXR->GetNextFrameDisplayTime(), &location), "locating grip velocity", m_instance);

	return OpenXRToCrysis(velocity.linearVelocity);
}

void OpenXRInput::EnableHandMovementForQuickMenu()
{
	m_quickMenuActive = true;
	m_quickMenuInitialHandPosition = gVR->GetControllerTransform(g_pGameCVars->vr_suit_hand).GetTranslation();
}

void OpenXRInput::DisableHandMovementForQuickMenu()
{
	m_quickMenuActive = false;
}

float OpenXRInput::GetGripAmount(int side) const
{
	return max(m_gripAmount[side], m_triggerAmount[side]);
}

void OpenXRInput::SendHapticEvent(EVRHand hand, float duration, float amplitude, float frequency)
{
	if (!g_pGameCVars->vr_haptics_enabled)
		return;

	int side = gVR->GetHandSide(hand);

	if (m_usingWinlatorXR)
	{
		// WinlatorXR pulses the controller as long as it keeps receiving a level > 0 (forwarded once per
		// frame by OpenXRRuntime::FinishFrame), so keep the strongest pending pulse alive for its duration
		float amp = clamp(amplitude * g_pGameCVars->vr_haptics_strength, 0.f, 1.f);
		float now = gEnv->pTimer->GetAsyncCurTime();
		float end = now + max(duration, 0.03f);
		if (amp >= m_wxrHapticAmplitude[side] || now >= m_wxrHapticEndTime[side])
		{
			m_wxrHapticAmplitude[side] = amp;
			m_wxrHapticEndTime[side] = end;
		}
		return;
	}

	XrHapticActionInfo hapticInfo{ XR_TYPE_HAPTIC_ACTION_INFO };
	hapticInfo.action = m_haptics[side];
	hapticInfo.subactionPath = XR_NULL_PATH;
	XrHapticVibration hapticEvent{ XR_TYPE_HAPTIC_VIBRATION };
	hapticEvent.duration = 1000000000.f * duration;
	hapticEvent.amplitude = clamp(amplitude * g_pGameCVars->vr_haptics_strength, 0.f, 1.f);
	hapticEvent.frequency = frequency;

	xrApplyHapticFeedback(m_session, &hapticInfo, reinterpret_cast<XrHapticBaseHeader*>(&hapticEvent));
}

void OpenXRInput::SendHapticEvent(float duration, float amplitude, float frequency)
{
	SendHapticEvent(LEFT_HAND, duration, amplitude, frequency);
	SendHapticEvent(RIGHT_HAND, duration, amplitude, frequency);
}

void OpenXRInput::StopHaptics(EVRHand hand)
{
	int side = gVR->GetHandSide(hand);

	if (m_usingWinlatorXR)
	{
		m_wxrHapticAmplitude[side] = 0.f;
		m_wxrHapticEndTime[side] = 0.f;
		return;
	}

	XrHapticActionInfo hapticInfo{ XR_TYPE_HAPTIC_ACTION_INFO };
	hapticInfo.action = m_haptics[side];
	hapticInfo.subactionPath = XR_NULL_PATH;

	xrStopHapticFeedback(m_session, &hapticInfo);
}

void OpenXRInput::StopHaptics()
{
	StopHaptics(LEFT_HAND);
	StopHaptics(RIGHT_HAND);
}

void OpenXRInput::CreateInputActions()
{
	CreateBooleanAction(m_ingameSet, m_primaryFire, "primary_fire", "Primary Fire", &g_pGameActions->attack1);
	CreateBooleanAction(m_ingameSet, m_suitMenu, "suit_menu", "Suit Menu", &g_pGameActions->defensemode, &g_pGameActions->hud_suit_menu, false, true, false, 0.15f);
	CreateBooleanAction(m_ingameSet, m_menu, "menu_toggle", "Open menu / show objectives", &g_pGameActions->xi_hud_back);
	CreateBooleanAction(m_ingameSet, m_sprint, "sprint", "Sprint", &g_pGameActions->sprint);
	CreateBooleanAction(m_ingameSet, m_reload, "reload", "Reload", &g_pGameActions->reload, &g_pGameActions->firemode);
	CreateBooleanAction(m_ingameSet, m_nextWeapon, "next_weapon", "Next Weapon", &g_pGameActions->nextitem, nullptr, false);
	CreateBooleanAction(m_ingameSet, m_use, "use", "Use", &g_pGameActions->xi_use);
	CreateBooleanAction(m_ingameSet, m_gripUse, "grip_use", "Grip/Use", &g_pGameActions->use);
	CreateBooleanAction(m_ingameSet, m_binoculars, "binoculars", "Binoculars", &g_pGameActions->xi_binoculars);
	CreateBooleanAction(m_ingameSet, m_nightvision, "nightvision", "Nightvision", &g_pGameActions->hud_night_vision);
	CreateBooleanAction(m_ingameSet, m_melee, "melee", "Melee Attack", &g_pGameActions->special);
	CreateBooleanAction(m_ingameSet, m_grenades, "grenades", "Switch and throw grenades", &g_pGameActions->handgrenade, &g_pGameActions->grenade);

	CreateBooleanAction(m_menuSet, m_menuClick, "click", "Menu Click", &g_pGameActions->hud_mouseclick);
	CreateBooleanAction(m_menuSet, m_menuBack, "back", "Menu Back", nullptr);
	CreateBooleanAction(m_menuSet, m_dropWeapon, "drop_weapon", "Drop Weapon", &g_pGameActions->drop);

	CreateBooleanAction(m_vehicleSet, m_vecBoost, "vec_boost", "Vehicle Boost", &g_pGameActions->v_boost);
	CreateBooleanAction(m_vehicleSet, m_vecAfterburner, "vec_afterburner", "Vehicle Afterburner", &g_pGameActions->v_afterburner);
	CreateBooleanAction(m_vehicleSet, m_vecAscend, "vec_ascend", "Vehicle (VTOL) Ascend", &g_pGameActions->v_moveup);
	CreateBooleanAction(m_vehicleSet, m_vecSecondaryFire, "vec_secondary_fire", "Vehicle Secondary Fire", &g_pGameActions->zoom);
	CreateBooleanAction(m_vehicleSet, m_vecExit, "vec_exit", "Exit vehicle", &g_pGameActions->use, nullptr, false, false, true);
	CreateBooleanAction(m_vehicleSet, m_vecHorn, "vec_horn", "Vehicle horn", &g_pGameActions->v_horn);
	CreateBooleanAction(m_vehicleSet, m_vecLights, "vec_lights", "Vehicle lights", &g_pGameActions->v_lights);
	CreateBooleanAction(m_vehicleSet, m_vecSwitchSeatView, "vec_switch", "Switch vehicle seat / view", &g_pGameActions->v_changeseat, &g_pGameActions->v_changeview, false, false);

	XrActionCreateInfo createInfo{ XR_TYPE_ACTION_CREATE_INFO };
	createInfo.actionType = XR_ACTION_TYPE_POSE_INPUT;
	strcpy(createInfo.actionName, "left_controller");
	strcpy(createInfo.localizedActionName, "Left Controller");
	XR_CheckResult(xrCreateAction(m_ingameSet, &createInfo, &m_controller[0]), "creating controller pose action");
	strcpy(createInfo.actionName, "right_controller");
	strcpy(createInfo.localizedActionName, "Right Controller");
	XR_CheckResult(xrCreateAction(m_ingameSet, &createInfo, &m_controller[1]), "creating controller pose action");

	XrActionSpaceCreateInfo spaceCreateInfo{ XR_TYPE_ACTION_SPACE_CREATE_INFO };
	spaceCreateInfo.action = m_controller[0];
	spaceCreateInfo.subactionPath = XR_NULL_PATH;
	spaceCreateInfo.poseInActionSpace.orientation.w = 1;
	XR_CheckResult(xrCreateActionSpace(m_session, &spaceCreateInfo, &m_gripSpace[0]), "creating controller tracking space", m_instance);
	spaceCreateInfo.action = m_controller[1];
	XR_CheckResult(xrCreateActionSpace(m_session, &spaceCreateInfo, &m_gripSpace[1]), "creating controller tracking space", m_instance);

	createInfo.actionType = XR_ACTION_TYPE_FLOAT_INPUT;
	strcpy(createInfo.actionName, "move_x");
	strcpy(createInfo.localizedActionName, "Move X");
	XR_CheckResult(xrCreateAction(m_ingameSet, &createInfo, &m_moveX), "creating movement action");
	strcpy(createInfo.actionName, "move_y");
	strcpy(createInfo.localizedActionName, "Move Y");
	XR_CheckResult(xrCreateAction(m_ingameSet, &createInfo, &m_moveY), "creating movement action");
	strcpy(createInfo.actionName, "rotate_yaw");
	strcpy(createInfo.localizedActionName, "Rotate Yaw");
	XR_CheckResult(xrCreateAction(m_ingameSet, &createInfo, &m_rotateYaw), "creating movement action");
	strcpy(createInfo.actionName, "rotate_pitch");
	strcpy(createInfo.localizedActionName, "Rotate Pitch");
	XR_CheckResult(xrCreateAction(m_ingameSet, &createInfo, &m_rotatePitch), "creating movement action");
	strcpy(createInfo.actionName, "jump_crouch");
	strcpy(createInfo.localizedActionName, "Jump / Crouch / Prone");
	XR_CheckResult(xrCreateAction(m_ingameSet, &createInfo, &m_jumpCrouch), "creating movement action");

	strcpy(createInfo.actionName, "lgrip");
	strcpy(createInfo.localizedActionName, "Left Grip");
	XR_CheckResult(xrCreateAction(m_ingameSet, &createInfo, &m_grip[0]), "creating movement action");
	strcpy(createInfo.actionName, "ltrigger");
	strcpy(createInfo.localizedActionName, "Left Trigger");
	XR_CheckResult(xrCreateAction(m_ingameSet, &createInfo, &m_trigger[0]), "creating movement action");
	strcpy(createInfo.actionName, "rgrip");
	strcpy(createInfo.localizedActionName, "Right Grip");
	XR_CheckResult(xrCreateAction(m_ingameSet, &createInfo, &m_grip[1]), "creating movement action");
	strcpy(createInfo.actionName, "rtrigger");
	strcpy(createInfo.localizedActionName, "Right Trigger");
	XR_CheckResult(xrCreateAction(m_ingameSet, &createInfo, &m_trigger[1]), "creating movement action");

	createInfo.actionType = XR_ACTION_TYPE_VIBRATION_OUTPUT;
	strcpy(createInfo.actionName, "lhaptics");
	strcpy(createInfo.localizedActionName, "Left Haptics");
	XR_CheckResult(xrCreateAction(m_ingameSet, &createInfo, &m_haptics[0]), "creating haptics output action");
	strcpy(createInfo.actionName, "rhaptics");
	strcpy(createInfo.localizedActionName, "Right Haptics");
	XR_CheckResult(xrCreateAction(m_ingameSet, &createInfo, &m_haptics[1]), "creating haptics output action");
}

void OpenXRInput::SuggestBindings()
{
	SuggestedProfileBinding touch(m_instance, true);
	touch.AddBinding(m_primaryFire.handle, "/user/hand/<weapon>/input/trigger/value");
	touch.AddBinding(m_controller[0], "/user/hand/left/input/grip/pose");
	touch.AddBinding(m_controller[1], "/user/hand/right/input/grip/pose");
	touch.AddBinding(m_moveX, "/user/hand/<movement>/input/thumbstick/x");
	touch.AddBinding(m_moveY, "/user/hand/<movement>/input/thumbstick/y");
	touch.AddBinding(m_rotateYaw, "/user/hand/<!movement>/input/thumbstick/x");
	touch.AddBinding(m_jumpCrouch, "/user/hand/<!movement>/input/thumbstick/y");
	touch.AddBinding(m_rotatePitch, "/user/hand/<!movement>/input/thumbstick/y");
	touch.AddBinding(m_sprint.handle, "/user/hand/<movement>/input/thumbstick/click");
	touch.AddBinding(m_menu.handle, "/user/hand/<!weapon>/input/b/click");
	touch.AddBinding(m_reload.handle, "/user/hand/<weapon>/input/a/click");
	touch.AddBinding(m_suitMenu.handle, "/user/hand/<!movement>/input/thumbstick/click");
	touch.AddBinding(m_nextWeapon.handle, "/user/hand/<weapon>/input/squeeze/value");
	touch.AddBinding(m_use.handle, "/user/hand/<!weapon>/input/trigger/value");
	touch.AddBinding(m_gripUse.handle, "/user/hand/<!weapon>/input/squeeze/value");
	touch.AddBinding(m_binoculars.handle, "/user/hand/<!weapon>/input/a/click");
	touch.AddBinding(m_grenades.handle, "/user/hand/<weapon>/input/b/click");
	touch.AddBinding(m_menuClick.handle, "/user/hand/<weapon>/input/trigger/value");
	touch.AddBinding(m_menuClick.handle, "/user/hand/<weapon>/input/a/click");
	touch.AddBinding(m_menuBack.handle, "/user/hand/<weapon>/input/b/click");
	touch.AddBinding(m_dropWeapon.handle, "/user/hand/<movement>/input/a/click");
	touch.AddBinding(m_vecBoost.handle, "/user/hand/<movement>/input/squeeze/value");
	touch.AddBinding(m_vecAscend.handle, "/user/hand/<!movement>/input/a/click");
	touch.AddBinding(m_vecSwitchSeatView.handle, "/user/hand/<!movement>/input/b/click");
	touch.AddBinding(m_vecAfterburner.handle, "/user/hand/<movement>/input/squeeze/value");
	touch.AddBinding(m_vecSecondaryFire.handle, "/user/hand/<!weapon>/input/trigger/value");
	touch.AddBinding(m_vecHorn.handle, "/user/hand/<!movement>/input/thumbstick/click");
	touch.AddBinding(m_vecLights.handle, "/user/hand/<movement>/input/thumbstick/click");
	touch.AddBinding(m_vecExit.handle, "/user/hand/<movement>/input/a/click");
	touch.AddBinding(m_grip[0], "/user/hand/left/input/squeeze/value");
	touch.AddBinding(m_grip[1], "/user/hand/right/input/squeeze/value");
	touch.AddBinding(m_trigger[0], "/user/hand/left/input/trigger/value");
	touch.AddBinding(m_trigger[1], "/user/hand/right/input/trigger/value");
	touch.AddBinding(m_haptics[0], "/user/hand/left/output/haptic");
	touch.AddBinding(m_haptics[1], "/user/hand/right/output/haptic");
	touch.SuggestBindings("/interaction_profiles/oculus/touch_controller");
	if (gXR->HasReverbG2BindingsExtension())
		touch.SuggestBindings("/interaction_profiles/hp/mixed_reality_controller");

	SuggestedProfileBinding knuckles(m_instance, false);
	knuckles.AddBinding(m_primaryFire.handle, "/user/hand/<weapon>/input/trigger/value");
	knuckles.AddBinding(m_controller[0], "/user/hand/left/input/grip/pose");
	knuckles.AddBinding(m_controller[1], "/user/hand/right/input/grip/pose");
	knuckles.AddBinding(m_moveX, "/user/hand/<movement>/input/thumbstick/x");
	knuckles.AddBinding(m_moveY, "/user/hand/<movement>/input/thumbstick/y");
	knuckles.AddBinding(m_rotateYaw, "/user/hand/<!movement>/input/thumbstick/x");
	knuckles.AddBinding(m_jumpCrouch, "/user/hand/<!movement>/input/thumbstick/y");
	knuckles.AddBinding(m_rotatePitch, "/user/hand/<!movement>/input/thumbstick/y");
	knuckles.AddBinding(m_sprint.handle, "/user/hand/<movement>/input/thumbstick/click");
	knuckles.AddBinding(m_menu.handle, "/user/hand/<!weapon>/input/b/click");
	knuckles.AddBinding(m_reload.handle, "/user/hand/<weapon>/input/a/click");
	knuckles.AddBinding(m_suitMenu.handle, "/user/hand/<weapon>/input/trackpad/force");
	knuckles.AddBinding(m_nextWeapon.handle, "/user/hand/<weapon>/input/squeeze/force");
	knuckles.AddBinding(m_use.handle, "/user/hand/<!weapon>/input/trigger/value");
	knuckles.AddBinding(m_gripUse.handle, "/user/hand/<!weapon>/input/squeeze/force");
	knuckles.AddBinding(m_binoculars.handle, "/user/hand/<!weapon>/input/a/click");
	knuckles.AddBinding(m_grenades.handle, "/user/hand/<weapon>/input/b/click");
	knuckles.AddBinding(m_melee.handle, "/user/hand/<weapon>/input/thumbstick/click");
	knuckles.AddBinding(m_menuClick.handle, "/user/hand/<weapon>/input/trigger/value");
	knuckles.AddBinding(m_menuClick.handle, "/user/hand/<weapon>/input/a/click");
	knuckles.AddBinding(m_menuBack.handle, "/user/hand/<weapon>/input/b/click");
	knuckles.AddBinding(m_dropWeapon.handle, "/user/hand/<!weapon>/input/a/click");
	knuckles.AddBinding(m_vecBoost.handle, "/user/hand/<movement>/input/squeeze/force");
	knuckles.AddBinding(m_vecAscend.handle, "/user/hand/<!movement>/input/a/click");
	knuckles.AddBinding(m_vecSwitchSeatView.handle, "/user/hand/<!movement>/input/b/click");
	knuckles.AddBinding(m_vecAfterburner.handle, "/user/hand/<movement>/input/squeeze/force");
	knuckles.AddBinding(m_vecSecondaryFire.handle, "/user/hand/<!weapon>/input/trigger/value");
	knuckles.AddBinding(m_vecHorn.handle, "/user/hand/<!movement>/input/thumbstick/click");
	knuckles.AddBinding(m_vecLights.handle, "/user/hand/<movement>/input/thumbstick/click");
	knuckles.AddBinding(m_vecExit.handle, "/user/hand/<movement>/input/a/click");
	knuckles.AddBinding(m_grip[0], "/user/hand/left/input/squeeze/value");
	knuckles.AddBinding(m_grip[1], "/user/hand/right/input/squeeze/value");
	knuckles.AddBinding(m_trigger[0], "/user/hand/left/input/trigger/value");
	knuckles.AddBinding(m_trigger[1], "/user/hand/right/input/trigger/value");
	knuckles.AddBinding(m_haptics[0], "/user/hand/left/output/haptic");
	knuckles.AddBinding(m_haptics[1], "/user/hand/right/output/haptic");
	knuckles.SuggestBindings("/interaction_profiles/valve/index_controller");

	if (gXR->HasSteamFrameBindingsExtension())
	{
		SuggestedProfileBinding frame(m_instance, false);
		frame.AddBinding(m_primaryFire.handle, "/user/hand/<weapon>/input/trigger/value");
		frame.AddBinding(m_controller[0], "/user/hand/left/input/grip/pose");
		frame.AddBinding(m_controller[1], "/user/hand/right/input/grip/pose");
		frame.AddBinding(m_moveX, "/user/hand/<movement>/input/thumbstick/x");
		frame.AddBinding(m_moveY, "/user/hand/<movement>/input/thumbstick/y");
		frame.AddBinding(m_rotateYaw, "/user/hand/<!movement>/input/thumbstick/x");
		frame.AddBinding(m_jumpCrouch, "/user/hand/<!movement>/input/thumbstick/y");
		frame.AddBinding(m_rotatePitch, "/user/hand/<!movement>/input/thumbstick/y");
		frame.AddBinding(m_sprint.handle, "/user/hand/<movement>/input/thumbstick/click");
		frame.AddBinding(m_menu.handle, "/user/hand/<!weapon>/input/b/click");
		frame.AddBinding(m_reload.handle, "/user/hand/<weapon>/input/a/click");
		frame.AddBinding(m_suitMenu.handle, "/user/hand/<weapon>/input/trackpad/force");
		frame.AddBinding(m_nextWeapon.handle, "/user/hand/<weapon>/input/squeeze/force");
		frame.AddBinding(m_use.handle, "/user/hand/<!weapon>/input/trigger/value");
		frame.AddBinding(m_gripUse.handle, "/user/hand/<!weapon>/input/squeeze/force");
		frame.AddBinding(m_binoculars.handle, "/user/hand/<!weapon>/input/a/click");
		frame.AddBinding(m_grenades.handle, "/user/hand/<weapon>/input/b/click");
		frame.AddBinding(m_melee.handle, "/user/hand/<weapon>/input/thumbstick/click");
		frame.AddBinding(m_menuClick.handle, "/user/hand/<weapon>/input/trigger/value");
		frame.AddBinding(m_menuClick.handle, "/user/hand/<weapon>/input/a/click");
		frame.AddBinding(m_menuBack.handle, "/user/hand/<weapon>/input/b/click");
		frame.AddBinding(m_dropWeapon.handle, "/user/hand/<!weapon>/input/a/click");
		frame.AddBinding(m_vecBoost.handle, "/user/hand/<movement>/input/squeeze/force");
		frame.AddBinding(m_vecAscend.handle, "/user/hand/<!movement>/input/a/click");
		frame.AddBinding(m_vecSwitchSeatView.handle, "/user/hand/<!movement>/input/b/click");
		frame.AddBinding(m_vecAfterburner.handle, "/user/hand/<movement>/input/squeeze/force");
		frame.AddBinding(m_vecSecondaryFire.handle, "/user/hand/<!weapon>/input/trigger/value");
		frame.AddBinding(m_vecHorn.handle, "/user/hand/<!movement>/input/thumbstick/click");
		frame.AddBinding(m_vecLights.handle, "/user/hand/<movement>/input/thumbstick/click");
		frame.AddBinding(m_vecExit.handle, "/user/hand/<movement>/input/a/click");
		frame.AddBinding(m_grip[0], "/user/hand/left/input/squeeze/value");
		frame.AddBinding(m_grip[1], "/user/hand/right/input/squeeze/value");
		frame.AddBinding(m_trigger[0], "/user/hand/left/input/trigger/value");
		frame.AddBinding(m_trigger[1], "/user/hand/right/input/trigger/value");
		frame.AddBinding(m_haptics[0], "/user/hand/left/output/haptic");
		frame.AddBinding(m_haptics[1], "/user/hand/right/output/haptic");
		frame.SuggestBindings("/interaction_profiles/valve/frame_controller_valve");
	}
}

void OpenXRInput::AttachActionSets()
{
	XrSessionActionSetsAttachInfo attachInfo{ XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO };
	std::vector<XrActionSet> actionSets;
	actionSets.push_back(m_ingameSet);
	actionSets.push_back(m_menuSet);
	actionSets.push_back(m_vehicleSet);
	attachInfo.actionSets = actionSets.data();
	attachInfo.countActionSets = actionSets.size();
	XR_CheckResult(xrAttachSessionActionSets(m_session, &attachInfo), "attaching action set", m_instance);
}


void OpenXRInput::CreateBooleanAction(XrActionSet actionSet, BooleanAction& action, const char* name,
                                      const char* description, ActionId* onPress, ActionId* onLongPress, bool sendRelease, bool sendLongRelease, bool pressOnRelease, float longPressActivationTime)
{
	if (action.handle)
	{
		xrDestroyAction(action.handle);
	}

	XrActionCreateInfo createInfo{ XR_TYPE_ACTION_CREATE_INFO };
	createInfo.actionType = XR_ACTION_TYPE_BOOLEAN_INPUT;
	strcpy(createInfo.actionName, name);
	strcpy(createInfo.localizedActionName, description);
	XR_CheckResult(xrCreateAction(actionSet, &createInfo, &action.handle), "creating boolean action");

	action.onPress = onPress;
	action.onLongPress = onLongPress;
	action.sendRelease = sendRelease;
	action.sendLongRelease = sendLongRelease;
	action.pressOnRelease = pressOnRelease;
	action.longPressActivationTime = longPressActivationTime;
	action.longPressActive = false;
}

void OpenXRInput::UpdateMeleeAttacks()
{
	CPlayer* player = gVR->GetLocalPlayer();
	CWeapon* weapon = player ? player->GetWeapon(player->GetCurrentItemId()) : nullptr;
	if (!weapon || !weapon->CanMeleeAttack() || gVR->IsOffHandGrabbingWeapon())
		return;

	Vec3 hmdForward = gXR->GetHmdTransform().GetColumn(1);

	for (int i = 0; i < 2; ++i)
	{
		Vec3 controllerVelocity = GetControllerVelocity(i);
		// need to be holding grip and pass a certain velocity threshold to trigger melee attacks
		// also, only consider movements that are somewhat in the direction of where you look at
		if (GetGripAmount(i) >= .95f && controllerVelocity.GetLength() > g_pGameCVars->vr_melee_trigger_velocity && controllerVelocity.Dot(hmdForward) > 0.7f)
		{
			// check if the attack would actually hit something, otherwise don't perform one
			SMovementState info;
			player->GetMovementController()->GetMovementState(info);
			Vec3 pos = info.eyePosition;
			Vec3 dir = info.eyeDirection;
			CMelee* melee = static_cast<CMelee*>(weapon->GetMeleeFireMode());
			if (melee->PerformRayTest(pos, dir, 1.f, false, false) || melee->PerformCylinderTest(pos, dir, 1.f, false, false))
			{
				weapon->MeleeAttack();
				SendHapticEvent(i == 0 ? LEFT_HAND : RIGHT_HAND, 0.15f, 0.6f);
				gHaptics->TriggerBHapticsEffect(i == 0 ? "punch_l_vest" : "punch_r_vest", 0.6f);
			}
			break;
		}
	}
}

void OpenXRInput::UpdateIngameActions()
{
	UpdatePlayerMovement();
	UpdateGripAmount();
	UpdateWeaponScopes();
	UpdateMeleeAttacks();
	UpdateBooleanAction(m_sprint);
	UpdateBooleanAction(m_menu);
	UpdateBooleanAction(m_suitMenu);
	UpdateBooleanAction(m_primaryFire);
	UpdateBooleanAction(m_nextWeapon);
	UpdateBooleanAction(m_reload);
	UpdateBooleanAction(m_use);
	UpdateBooleanAction(m_gripUse);
	UpdateBooleanAction(m_binoculars);
	UpdateBooleanAction(m_nightvision);
	UpdateBooleanAction(m_melee);
	UpdateBooleanAction(m_grenades);
	UpdateBooleanAction(m_menuClick);
}

void OpenXRInput::UpdateVehicleActions()
{
	UpdatePlayerMovement();
	UpdateGripAmount();
	UpdateBooleanAction(m_menu);
	UpdateBooleanAction(m_primaryFire);
	UpdateBooleanAction(m_menuClick);
	UpdateBooleanAction(m_vecBoost);
	UpdateBooleanAction(m_vecAfterburner);
	UpdateBooleanAction(m_vecSecondaryFire);
	UpdateBooleanAction(m_vecExit);
	UpdateBooleanAction(m_vecSwitchSeatView);
	UpdateBooleanAction(m_vecHorn);
	UpdateBooleanAction(m_vecLights);
	UpdateBooleanAction(m_vecAscend);
}

void OpenXRInput::UpdateMenuActions()
{
	UpdateBooleanActionForMenu(m_menuClick, eDI_XI, eKI_XI_A);
	UpdateBooleanActionForMenu(m_menuBack, eDI_XI, eKI_XI_B);
	UpdateBooleanActionForMenu(m_menu, eDI_Keyboard, eKI_Escape);
	// this is needed to potentially debounce the suit menu and not have it active in the background still
	UpdateBooleanAction(m_suitMenu);

	XrActionStateFloat state{ XR_TYPE_ACTION_STATE_FLOAT };
	XrActionStateGetInfo getInfo{ XR_TYPE_ACTION_STATE_GET_INFO };
	getInfo.subactionPath = XR_NULL_PATH;
	getInfo.action = m_rotatePitch;
	GetFloatState(getInfo.action, state);
	float pitch = state.isActive ? state.currentState : 0;
	ImGui::GetIO().AddMouseWheelEvent(0, pitch * .25f);
}

void OpenXRInput::UpdateHUDActions()
{
	UpdatePlayerMovement();
	UpdateGripAmount();
	UpdateBooleanAction(m_menu);
	UpdateBooleanAction(m_suitMenu);
	UpdateBooleanAction(m_menuClick);
	if (m_quickMenuActive)
	{
		UpdateBooleanAction(m_dropWeapon);
	}
}

void OpenXRInput::UpdatePlayerMovement()
{
	CPlayer *pPlayer = static_cast<CPlayer *>(gEnv->pGame->GetIGameFramework()->GetClientActor());
	if (!pPlayer)
		return;
	IPlayerInput* input = pPlayer->GetPlayerInput();
	if (!input)
		return;

	bool inVehicle = pPlayer->GetLinkedVehicle() != nullptr;
	bool inHud = g_pGame->GetHUD() && g_pGame->GetHUD()->GetModalHUD() != nullptr;
	bool usingMountedGun = pPlayer->GetActorStats()->mountedWeaponID != 0;
	bool rendering2D = !gVRRenderer->ShouldRenderVR();
	bool usingBinoculars = gVRRenderer->AreBinocularsActive();
	CWeapon* weapon = pPlayer->GetWeapon(pPlayer->GetCurrentItemId());
	bool usingWeaponZoom = weapon && weapon->IsZoomed();
	bool isZoomed = usingBinoculars || usingWeaponZoom;

	XrActionStateFloat state{ XR_TYPE_ACTION_STATE_FLOAT };
	XrActionStateGetInfo getInfo{ XR_TYPE_ACTION_STATE_GET_INFO };
	getInfo.subactionPath = XR_NULL_PATH;

	getInfo.action = m_moveX;
	GetFloatState(getInfo.action, state);
	if (state.isActive)
	{
		input->OnAction(inVehicle ? g_pGameActions->xi_v_movex : g_pGameActions->xi_movex, eAAM_Always, state.currentState);
	}

	getInfo.action = m_moveY;
	GetFloatState(getInfo.action, state);
	if (state.isActive)
	{
		input->OnAction(inVehicle ? g_pGameActions->xi_v_movey : g_pGameActions->xi_movey, eAAM_Always, state.currentState);
	}

	getInfo.action = m_jumpCrouch;
	GetFloatState(getInfo.action, state);
	float jumpCrouch = state.isActive ? state.currentState : 0;

	getInfo.action = m_rotateYaw;
	GetFloatState(getInfo.action, state);
	float yaw = state.isActive ? state.currentState : 0;

	getInfo.action = m_rotatePitch;
	GetFloatState(getInfo.action, state);
	float pitch = state.isActive ? state.currentState : 0;

	if (inVehicle)
	{
		input->OnAction(g_pGameActions->xi_v_rotatepitch, eAAM_Always, pitch * (g_pGameCVars->vr_vehicle_invert_y ? -1 : 1));
		input->OnAction(g_pGameActions->xi_v_rotateyaw, eAAM_Always, yaw);
		return;
	}

	if (inHud || usingMountedGun || (rendering2D && !isZoomed))
	{
		if (m_quickMenuActive)
		{
			// we are using movement of the main controller for the menu input, instead
			Vec3 movementDiff = gVR->GetControllerTransform(g_pGameCVars->vr_suit_hand).GetTranslation() - m_quickMenuInitialHandPosition;
			Ang3 hmdAngles = Ang3(gVR->GetHMDQuat());
			Vec3 hmdRight = Matrix33::CreateRotationZ(hmdAngles.z).GetColumn0();
			Vec3 up = Vec3(0, 0, 1);

			const float MaxMove = 0.02f;
			yaw = clamp(movementDiff.Dot(hmdRight) / MaxMove, -1.f, 1.f);
			pitch = clamp(movementDiff.Dot(up) / MaxMove, -1.f, 1.f);

			SAFE_HUD_FUNC(OnAction(g_pGameActions->xi_rotatepitch, eAAM_Always, pitch));
			SAFE_HUD_FUNC(OnAction(g_pGameActions->xi_rotateyaw, eAAM_Always, yaw));
			return;
		}

		input->OnAction(g_pGameActions->xi_rotatepitch, eAAM_Always, pitch * (g_pGameCVars->vr_vehicle_invert_y ? -1 : 1));
		input->OnAction(g_pGameActions->xi_rotateyaw, eAAM_Always, yaw);
		return;
	}

	if (g_pGameCVars->vr_turn_mode == 0)
	{
		if (fabsf(yaw) >= g_pGameCVars->vr_controller_stick_zone_cutoff)
			input->OnAction(g_pGameActions->xi_rotateyaw, eAAM_Always, yaw);
		else
			input->OnAction(g_pGameActions->xi_rotateyaw, eAAM_Always, 0);
	}
	else
	{
		int snapTurnState = m_snapTurnState;
		if (yaw <= -0.75f)
			snapTurnState = -1;
		else if (yaw >= 0.75f)
			snapTurnState = 1;
		else if (fabsf(yaw) <= 0.25f)
			snapTurnState = 0;

		if (snapTurnState != m_snapTurnState)
		{
			if (snapTurnState < 0)
				input->OnSnapTurnLeft();
			else if (snapTurnState > 0)
				input->OnSnapTurnRight();
			m_snapTurnState = snapTurnState;
		}
	}

	if (jumpCrouch < g_pGameCVars->vr_controller_stick_action_threshold)
	{
		if (m_wasJumpActive && !isZoomed)
			input->OnAction(g_pGameActions->jump, eAAM_OnRelease, 0);
		m_wasJumpActive = false;
	}
	else
	{
		if (!m_wasJumpActive)
			input->OnAction(isZoomed ? g_pGameActions->zoom_in : g_pGameActions->jump, eAAM_OnPress, 1);
		m_wasJumpActive = true;
	}
	if (jumpCrouch > -g_pGameCVars->vr_controller_stick_action_threshold)
	{
		if (pPlayer->InZeroG() && m_wasCrouchActive)
			input->OnAction(g_pGameActions->crouch, eAAM_OnRelease, 0);
		m_wasCrouchActive = false;
	}
	else
	{
		if (!m_wasCrouchActive)
		{
			if (isZoomed)
			{
				input->OnAction(g_pGameActions->zoom_out, eAAM_OnPress, 1);
			}
			else if (pPlayer->InZeroG())
			{
				input->OnAction(g_pGameActions->crouch, eAAM_OnPress, 1);
			}
			else if (pPlayer->GetStance() == STANCE_CROUCH)
			{
				input->OnAction(g_pGameActions->crouch, eAAM_OnRelease, 0);
				input->OnAction(g_pGameActions->prone, eAAM_OnPress, 1);
			}
			else
			{
				// as prone is a toggle, might need to deactivate it
				if (pPlayer->GetStance() == STANCE_PRONE)
					input->OnAction(g_pGameActions->prone, eAAM_OnPress, 1);
				input->OnAction(g_pGameActions->crouch, eAAM_OnPress, 1);
			}
		}
		m_wasCrouchActive = true;
	}
}

void OpenXRInput::UpdateGripAmount()
{
	XrActionStateFloat state{ XR_TYPE_ACTION_STATE_FLOAT };
	XrActionStateGetInfo getInfo{ XR_TYPE_ACTION_STATE_GET_INFO };
	getInfo.subactionPath = XR_NULL_PATH;

	int offHand = 1 - g_pGameCVars->vr_weapon_hand;
	float prevOffHandGrip = m_gripAmount[offHand];
	for (int side = 0; side < 2; ++side)
	{
		getInfo.action = m_grip[side];
		GetFloatState(getInfo.action, state);
		m_gripAmount[side] = state.isActive ? state.currentState : 0;

		getInfo.action = m_trigger[side];
		GetFloatState(getInfo.action, state);
		m_triggerAmount[side] = state.isActive ? state.currentState : 0;
	}

	CPlayer *pPlayer = static_cast<CPlayer *>(gEnv->pGame->GetIGameFramework()->GetClientActor());
	if (!pPlayer)
		return;
	IPlayerInput* input = pPlayer->GetPlayerInput();
	if (!input)
		return;

	const float gripActivationAmount = 0.6f;
	if (m_gripAmount[offHand] >= gripActivationAmount && prevOffHandGrip < gripActivationAmount)
	{
		input->OnAction(g_pGameActions->off_hand_weapon_grab, eAAM_OnPress, 0);
	}
	else if (m_gripAmount[offHand] < gripActivationAmount && prevOffHandGrip >= gripActivationAmount)
	{
		input->OnAction(g_pGameActions->off_hand_weapon_grab, eAAM_OnRelease, 0);
	}
}

void OpenXRInput::UpdateWeaponScopes()
{
	CPlayer* player = gVR->GetLocalPlayer();
	if (!player)
		return;
	IPlayerInput* input = player->GetPlayerInput();
	if (!input)
		return;

	CWeapon* weapon = player->GetWeapon(player->GetCurrentItemId());
	if (!weapon || !weapon->HasScope())
		return;

	if (!weapon->IsZoomed() && !weapon->IsZooming())
	{
		// if scope is close to eyes, activate weapon zoom mode
		if (!player->IsSwimming() && !player->IsSprinting() && gVR->IsOffHandGrabbingWeapon() && gVR->IsHandNearHead(WEAPON_HAND, 0.25f))
		{
			input->OnAction(g_pGameActions->xi_zoom, eAAM_OnPress, 1);
		}
	}
	else
	{
		if (!gVR->IsOffHandGrabbingWeapon() || !gVR->IsHandNearHead(WEAPON_HAND, 0.35f))
		{
			// either moved away from eye or let go with off hand, so stop zooming
			input->OnAction(g_pGameActions->xi_zoom, eAAM_OnPress, 1);
		}
	}
}

void OpenXRInput::UpdateBooleanAction(BooleanAction& action)
{
	CPlayer *pPlayer = static_cast<CPlayer *>(gEnv->pGame->GetIGameFramework()->GetClientActor());
	if (!pPlayer)
		return;
	IPlayerInput* input = pPlayer->GetPlayerInput();
	if (!input)
		return;

	XrActionStateBoolean state{ XR_TYPE_ACTION_STATE_BOOLEAN };
	XrActionStateGetInfo getInfo{ XR_TYPE_ACTION_STATE_GET_INFO };
	getInfo.subactionPath = XR_NULL_PATH;
	getInfo.action = action.handle;
	GetBoolState(getInfo.action, state);

	if (!state.isActive || !action.onPress)
		return;

	if (action.onLongPress == nullptr)
	{
		if (state.changedSinceLastSync)
		{
			if ((state.currentState && !action.pressOnRelease) || (!state.currentState && action.pressOnRelease))
				input->OnAction(*action.onPress, eAAM_OnPress, 1.f);
			else if (!state.currentState && action.sendRelease)
				input->OnAction(*action.onPress, eAAM_OnRelease, 0.f);
		}
		return;
	}

	if (state.changedSinceLastSync && state.currentState == XR_TRUE)
	{
		action.timePressed = gEnv->pTimer->GetAsyncCurTime();
	}
	else if (state.changedSinceLastSync && state.currentState == XR_FALSE)
	{
		if (action.longPressActive && action.onLongPress && action.sendLongRelease)
		{
			input->OnAction(*action.onLongPress, eAAM_OnRelease, 0);
		}
		else if (action.onPress && !action.longPressActive)
		{
			// we are only activating on release to distinguish from a long press
			input->OnAction(*action.onPress, eAAM_OnPress, 1);
			if (action.sendRelease)
			{
				input->OnAction(*action.onPress, eAAM_OnRelease, 0);
			}
		}
		action.timePressed = -1;
		action.longPressActive = false;
	}

	if (state.currentState == XR_TRUE && action.timePressed >= 0 && !action.longPressActive)
	{
		float curTime = gEnv->pTimer->GetAsyncCurTime();
		if (curTime - action.timePressed >= action.longPressActivationTime)
		{
			action.longPressActive = true;
			input->OnAction(*action.onLongPress, eAAM_OnPress, 1);
		}
	}
}

void OpenXRInput::UpdateBooleanActionForMenu(BooleanAction& action, EDeviceId device, EKeyId key)
{
	action.timePressed = -1;

	CFlashMenuObject* menu = g_pGame->GetMenu();
	if (!menu || !menu->IsMenuActive())
		return;

	XrActionStateBoolean state{ XR_TYPE_ACTION_STATE_BOOLEAN };
	XrActionStateGetInfo getInfo{ XR_TYPE_ACTION_STATE_GET_INFO };
	getInfo.subactionPath = XR_NULL_PATH;
	getInfo.action = action.handle;
	if (m_usingWinlatorXR && g_pGameCVars->vr_winlatorxr_native_menu_pointer != 0)
	{
		// WinlatorXR emulates a mouse (trigger = left click) and Esc (menu button) from the controllers,
		// and those reach the Flash menu natively. Only the ImGui overlay needs feeding: mirror the real
		// mouse button into it (its position comes from the hardware mouse in Update()).
		if (key == eKI_XI_A)
		{
			bool down = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
			if (down != m_wxrMouseDown)
			{
				m_wxrMouseDown = down;
				ImGui::GetIO().AddMouseButtonEvent(0, down);
				if (down && !ImGui::GetIO().WantCaptureMouse)
					gVRRenderer->GuiClickedUnfocussed();
			}
		}
		return;
	}

	GetBoolState(getInfo.action, state);

	if (!state.isActive || !state.changedSinceLastSync)
		return;

	if (key == eKI_XI_A)
	{
		// forward mouse clicks to ImGui
		ImGui::GetIO().AddMouseButtonEvent(0, state.currentState);
	}
	if (!ImGui::GetIO().WantCaptureMouse)
	{
		SInputEvent event;
		event.deviceId = device;
		event.keyId = key;
		event.state = state.currentState ? eIS_Pressed : eIS_Released;
		menu->OnInputEvent(event);
		gVRRenderer->GuiClickedUnfocussed();
	}
}

void OpenXRInput::UpdateControllerPoses()
{
	for (int i = 0; i < 2; ++i)
	{
		Matrix34 controllerTransform;
		// the grip pose has a peculiar orientation that we need to fix
		Matrix33 correction = Matrix33::CreateRotationX(-gf_PI/2);
		if (m_usingWinlatorXR)
		{
			if (!m_wxrState.valid)
				continue;
			controllerTransform = GetWinlatorControllerPose(i) * correction;
		}
		else
		{
			XrSpaceLocation location{ XR_TYPE_SPACE_LOCATION };
			XR_CheckResult(xrLocateSpace(m_gripSpace[i], m_trackingSpace, gXR->GetNextFrameDisplayTime(), &location), "locating grip space", m_instance);
			controllerTransform = OpenXRToCrysis(location.pose.orientation, location.pose.position) * correction;
		}

		Vec3 controllerPos = controllerTransform.GetTranslation();
		Quat controllerRot = Quat(controllerTransform);

		if (!g_pGameCVars->vr_controller_smoothing)
		{
			m_controllerPos[i] = controllerPos;
			m_controllerRot[i] = controllerRot;
			continue;
		}

		if (m_controllerRot[i].GetLength() < .5f)
		{
			// probably uniniialized, so just set it
			m_controllerRot[i] = controllerRot;
			m_controllerPos[i] = controllerPos;
		}

		// smooth controller poses with SLERP
		float angleDiff = controllerRot | m_controllerRot[i];
		if (angleDiff < 0)
			angleDiff = -angleDiff;
		float distance = 100.f * max(0.01f, controllerPos.GetDistance(m_controllerPos[i]));

		float lerpSpeed = g_pGameCVars->vr_controller_smoothing_speed * gEnv->pTimer->GetFrameTime();
		m_controllerPos[i] = Vec3::CreateLerp(m_controllerPos[i], controllerPos, min(1.0f, lerpSpeed * distance));
		m_controllerRot[i] = Quat::CreateSlerp(m_controllerRot[i], controllerRot, min(1.0f, lerpSpeed * angleDiff));
	}
}

bool OpenXRInput::CalcControllerHudIntersection(int hand, float& x, float& y)
{
	hand = clamp_tpl(hand, 0, 1);

	// need to massage the pose orientation a little bit to work in our favour
	Matrix33 correction = Matrix33::CreateRotationXYZ(Ang3(-gf_PI/3, 0, 0));
	Matrix34 controllerTransform;
	if (m_usingWinlatorXR)
	{
		if (!m_wxrState.valid)
			return false;
		controllerTransform = GetWinlatorControllerPose(hand) * correction;
	}
	else
	{
		XrSpaceLocation location{ XR_TYPE_SPACE_LOCATION };
		XR_CheckResult(xrLocateSpace(m_gripSpace[hand], m_trackingSpace, gXR->GetNextFrameDisplayTime(), &location), "locating grip space", m_instance);
		controllerTransform = OpenXRToCrysis(location.pose.orientation, location.pose.position) * correction;
	}

	XrPosef hudPose = gXR->GetHudPose();
	Matrix34 hudTransform = OpenXRToCrysis(hudPose.orientation, hudPose.position);
	hudTransform.InvertFast();

	Matrix34 controllerInHudSpace = hudTransform * controllerTransform;

	Vec3 pos = controllerInHudSpace.GetTranslation();
	Vec3 dir = controllerInHudSpace.GetColumn1();
	Vec2 hudSize(gXR->GetHudWidth(), gXR->GetHudHeight());

	// WinlatorXR draws menus on a curved screen (see VRManager::DrawWinlatorCurvedPanel): intersect the
	// ray with that cylinder (axis along z, 'radius' in front of the panel) instead of the flat plane
	float curveRadius = 0.f;
	if (m_usingWinlatorXR && gXR->GetWinlatorModeVr() == 1 && g_pGame->GetMenu() && g_pGame->GetMenu()->IsMenuActive())
		curveRadius = gXR->GetWinlatorMenuCurveRadius();
	if (curveRadius > 0.f)
	{
		float qy = pos.y + curveRadius;
		float a = dir.x * dir.x + dir.y * dir.y;
		float b = 2.f * (pos.x * dir.x + qy * dir.y);
		float c = pos.x * pos.x + qy * qy - curveRadius * curveRadius;
		float disc = b * b - 4.f * a * c;
		if (a < 1e-6f || disc < 0.f)
			return false;
		// the viewer is inside the cylinder and looks at its inner side: the far root
		float t = (-b + sqrt(disc)) / (2.f * a);
		if (t < 0)
			return false;
		Vec3 hit = pos + t * dir;
		float theta = atan2(hit.x, hit.y + curveRadius);
		x = theta * curveRadius / hudSize.x + 0.5f;
		y = 0.5f - hit.z / hudSize.y;
		return (x >= 0 && x <= 1 && y >= 0 && y <= 1);
	}

	if (dir.y <= 0.01)
		return false;

	float t = -pos.y / dir.y;
	if (t < 0)
		return false;

	Vec2 intersection(pos.x + t * dir.x, pos.z + t * dir.z);
	Vec2 upperLeft = -.5f * hudSize;

	intersection -= upperLeft;
	intersection.x /= hudSize.x;
	intersection.y /= hudSize.y;
	x = intersection.x;
	y = 1 - intersection.y;
	return (x >= 0 && x <= 1 && y >= 0 && y <= 1);
}

void OpenXRInput::DestroyAction(XrAction& action)
{
	xrDestroyAction(action);
	action = XR_NULL_HANDLE;
}

// ---------------------------------------------------------------------------------------------------
// WinlatorXR backend
// ---------------------------------------------------------------------------------------------------

void OpenXRInput::InitWinlatorXR()
{
	m_usingWinlatorXR = true;

	// hand the gameplay code synthetic handles; GetBoolState/GetFloatState resolve them per frame
	m_primaryFire.handle = WxrHandle(WXR_PRIMARY_FIRE);
	m_sprint.handle = WxrHandle(WXR_SPRINT);
	m_reload.handle = WxrHandle(WXR_RELOAD);
	m_menu.handle = WxrHandle(WXR_MENU);
	m_suitMenu.handle = WxrHandle(WXR_SUIT_MENU);
	m_binoculars.handle = WxrHandle(WXR_BINOCULARS);
	m_nextWeapon.handle = WxrHandle(WXR_NEXT_WEAPON);
	m_use.handle = WxrHandle(WXR_USE);
	m_gripUse.handle = WxrHandle(WXR_GRIP_USE);
	m_nightvision.handle = WxrHandle(WXR_NIGHTVISION);
	m_melee.handle = WxrHandle(WXR_MELEE);
	m_grenades.handle = WxrHandle(WXR_GRENADES);
	m_menuClick.handle = WxrHandle(WXR_MENU_CLICK);
	m_menuBack.handle = WxrHandle(WXR_MENU_BACK);
	m_dropWeapon.handle = WxrHandle(WXR_DROP_WEAPON);
	m_vecBoost.handle = WxrHandle(WXR_VEC_BOOST);
	m_vecAfterburner.handle = WxrHandle(WXR_VEC_AFTERBURNER);
	m_vecSecondaryFire.handle = WxrHandle(WXR_VEC_SECONDARY_FIRE);
	m_vecAscend.handle = WxrHandle(WXR_VEC_ASCEND);
	m_vecHorn.handle = WxrHandle(WXR_VEC_HORN);
	m_vecLights.handle = WxrHandle(WXR_VEC_LIGHTS);
	m_vecExit.handle = WxrHandle(WXR_VEC_EXIT);
	m_vecSwitchSeatView.handle = WxrHandle(WXR_VEC_SWITCH);
	m_moveX = WxrHandle(WXR_MOVE_X);
	m_moveY = WxrHandle(WXR_MOVE_Y);
	m_rotateYaw = WxrHandle(WXR_ROTATE_YAW);
	m_rotatePitch = WxrHandle(WXR_ROTATE_PITCH);
	m_jumpCrouch = WxrHandle(WXR_JUMP_CROUCH);
	m_grip[0] = WxrHandle(WXR_LGRIP);
	m_grip[1] = WxrHandle(WXR_RGRIP);
	m_trigger[0] = WxrHandle(WXR_LTRIGGER);
	m_trigger[1] = WxrHandle(WXR_RTRIGGER);

	// the same press/long-press semantics as the OpenXR path (see CreateInputActions)
	auto setup = [](BooleanAction& action, ActionId* onPress, ActionId* onLongPress = nullptr, bool sendRelease = true, bool sendLongRelease = true, bool pressOnRelease = false, float longPressActivationTime = 0.25f)
	{
		action.onPress = onPress;
		action.onLongPress = onLongPress;
		action.sendRelease = sendRelease;
		action.sendLongRelease = sendLongRelease;
		action.pressOnRelease = pressOnRelease;
		action.longPressActivationTime = longPressActivationTime;
		action.longPressActive = false;
		action.timePressed = -1;
	};
	setup(m_primaryFire, &g_pGameActions->attack1);
	setup(m_suitMenu, &g_pGameActions->defensemode, &g_pGameActions->hud_suit_menu, false, true, false, 0.15f);
	setup(m_menu, &g_pGameActions->xi_hud_back);
	setup(m_sprint, &g_pGameActions->sprint);
	setup(m_reload, &g_pGameActions->reload, &g_pGameActions->firemode);
	setup(m_nextWeapon, &g_pGameActions->nextitem, nullptr, false);
	setup(m_use, &g_pGameActions->xi_use);
	setup(m_gripUse, &g_pGameActions->use);
	setup(m_binoculars, &g_pGameActions->xi_binoculars);
	setup(m_nightvision, &g_pGameActions->hud_night_vision);
	setup(m_melee, &g_pGameActions->special);
	setup(m_grenades, &g_pGameActions->handgrenade, &g_pGameActions->grenade);
	setup(m_menuClick, &g_pGameActions->hud_mouseclick);
	setup(m_menuBack, nullptr);
	setup(m_dropWeapon, &g_pGameActions->drop);
	setup(m_vecBoost, &g_pGameActions->v_boost);
	setup(m_vecAfterburner, &g_pGameActions->v_afterburner);
	setup(m_vecAscend, &g_pGameActions->v_moveup);
	setup(m_vecSecondaryFire, &g_pGameActions->zoom);
	setup(m_vecExit, &g_pGameActions->use, nullptr, false, false, true);
	setup(m_vecHorn, &g_pGameActions->v_horn);
	setup(m_vecLights, &g_pGameActions->v_lights);
	setup(m_vecSwitchSeatView, &g_pGameActions->v_changeseat, &g_pGameActions->v_changeview, false, false);

	for (int i = 0; i < 2; ++i)
	{
		m_wxrVelocity[i] = Vec3(0, 0, 0);
		m_controllerPos[i] = Vec3(0, 0, 0);
		m_controllerRot[i] = Quat::CreateIdentity();
	}

	CryLogAlways("[WinlatorXR] controller input backend initialised (Touch-style bindings)");
}

void OpenXRInput::SetWxrBool(WxrAction action, bool state)
{
	WxrBool& b = m_wxrBool[action];
	// 'state' persists across frames (only 'active'/'changed' are reset per frame), so this is a
	// proper edge detection like OpenXR's changedSinceLastSync
	b.changed = (b.state != state);
	b.state = state;
	b.active = true;
}

void OpenXRInput::SetWxrFloat(WxrAction action, float value)
{
	WxrFloat& f = m_wxrFloat[action];
	f.active = true;
	f.value = value;
}

void OpenXRInput::UpdateWinlatorXRActions()
{
	// Mark everything inactive first; SetWxr* re-activates what is bound. Unbound actions stay inactive
	// so the gameplay code takes the same fallbacks as for unbound OpenXR actions.
	for (int a = 0; a < WXR_ACTION_COUNT; ++a)
	{
		m_wxrBool[a].active = false;
		m_wxrBool[a].changed = false;
		m_wxrFloat[a].active = false;
	}

	WinlatorXR::InputState state = WinlatorXR::GetLatestState();
	if (!state.valid)
		return;

	// controller velocity: finite difference of the aim position between two distinct packets
	if (m_wxrState.valid && state.frameId != m_wxrState.frameId && state.receiveTimeMs > m_wxrState.receiveTimeMs)
	{
		float dt = (state.receiveTimeMs - m_wxrState.receiveTimeMs) * 0.001f;
		if (dt >= 0.004f && dt <= 0.5f)
		{
			const WinlatorXR::HandState* cur[2] = { &state.left, &state.right };
			const WinlatorXR::HandState* prev[2] = { &m_wxrState.left, &m_wxrState.right };
			for (int i = 0; i < 2; ++i)
			{
				XrVector3f delta = { cur[i]->posX - prev[i]->posX, cur[i]->posY - prev[i]->posY, cur[i]->posZ - prev[i]->posZ };
				m_wxrVelocity[i] = OpenXRToCrysis(delta) / dt;
			}
		}
	}
	m_wxrState = state;

	// Mirror the Touch bindings from SuggestBindings, with the same <weapon>/<movement> hand
	// substitution. Per hand, the "lower"/"upper" face buttons are A/B on the right and X/Y on the left.
	int w = clamp_tpl(g_pGameCVars->vr_weapon_hand, 0, 1);
	int nw = 1 - w;
	int m = clamp_tpl(g_pGameCVars->vr_movement_hand, 0, 1);
	int nm = 1 - m;
	bool lower[2] = { state.lButtonX, state.rButtonA };
	bool upper[2] = { state.lButtonY, state.rButtonB };
	bool trigger[2] = { state.lTrigger, state.rTrigger };
	bool grip[2] = { state.lGrip, state.rGrip };
	bool stickClick[2] = { state.lThumbstickPress, state.rThumbstickPress };
	float thumbX[2] = { state.left.thumbX, state.right.thumbX };
	float thumbY[2] = { state.left.thumbY, state.right.thumbY };

	// --- ingame ---
	SetWxrBool(WXR_PRIMARY_FIRE, trigger[w]);
	SetWxrFloat(WXR_MOVE_X, thumbX[m]);
	SetWxrFloat(WXR_MOVE_Y, thumbY[m]);
	SetWxrFloat(WXR_ROTATE_YAW, thumbX[nm]);
	SetWxrFloat(WXR_JUMP_CROUCH, thumbY[nm]);
	SetWxrFloat(WXR_ROTATE_PITCH, thumbY[nm]);
	SetWxrBool(WXR_SPRINT, stickClick[m]);
	// Deliberate differences to the Touch profile: WinlatorXR reserves the right thumbstick click for
	// its own menu and maps the left controller's menu button to Esc, so the game menu goes on that
	// menu button (it is our menu toggle in-game; in the flat menus WinlatorXR's Esc closes them) and
	// the suit menu moves from the stick click to the non-weapon upper face button (Y).
	SetWxrBool(WXR_MENU, state.lMenu);
	SetWxrBool(WXR_RELOAD, lower[w]);
	SetWxrBool(WXR_SUIT_MENU, upper[nw]);
	SetWxrBool(WXR_NEXT_WEAPON, grip[w]);
	SetWxrBool(WXR_USE, trigger[nw]);
	SetWxrBool(WXR_GRIP_USE, grip[nw]);
	SetWxrBool(WXR_BINOCULARS, lower[nw]);
	SetWxrBool(WXR_GRENADES, upper[w]);
	// WXR_NIGHTVISION / WXR_MELEE are unbound on Touch as well (melee is a physical swing)

	// --- menu ---
	SetWxrBool(WXR_MENU_CLICK, trigger[w] || lower[w]);
	SetWxrBool(WXR_MENU_BACK, upper[w]);
	SetWxrBool(WXR_DROP_WEAPON, lower[m]);

	// --- vehicles ---
	SetWxrBool(WXR_VEC_BOOST, grip[m]);
	SetWxrBool(WXR_VEC_AFTERBURNER, grip[m]);
	SetWxrBool(WXR_VEC_ASCEND, lower[nm]);
	SetWxrBool(WXR_VEC_SWITCH, upper[nm]);
	SetWxrBool(WXR_VEC_SECONDARY_FIRE, trigger[nw]);
	// horn is the non-movement stick click on Touch, which WinlatorXR reserves - left unbound
	SetWxrBool(WXR_VEC_LIGHTS, stickClick[m]);
	SetWxrBool(WXR_VEC_EXIT, lower[m]);

	// --- analog grips/triggers (digital in the packet) ---
	SetWxrFloat(WXR_LGRIP, state.lGrip ? 1.f : 0.f);
	SetWxrFloat(WXR_RGRIP, state.rGrip ? 1.f : 0.f);
	SetWxrFloat(WXR_LTRIGGER, state.lTrigger ? 1.f : 0.f);
	SetWxrFloat(WXR_RTRIGGER, state.rTrigger ? 1.f : 0.f);
}

Matrix34 OpenXRInput::GetWinlatorControllerPose(int hand) const
{
	// Position: the aim pose (a few cm in front of where OpenXR's grip pose sits - close enough).
	// Orientation: the grip pose when the protocol provides it, because that is what the OpenXR path
	// binds and what the mod's weapon/hand code is tuned for; otherwise fall back to the aim
	// orientation, which points the weapon along the controller instead of the handle.
	const WinlatorXR::HandState& h = hand == 0 ? m_wxrState.left : m_wxrState.right;
	XrQuaternionf q = { h.qx, h.qy, h.qz, h.qw };
	if (m_wxrState.hasGripOrientation)
	{
		q.x = h.gripQx; q.y = h.gripQy; q.z = h.gripQz; q.w = h.gripQw;
	}
	else if (!m_wxrWarnedNoGrip)
	{
		const_cast<OpenXRInput*>(this)->m_wxrWarnedNoGrip = true;
		CryLogAlways("[WinlatorXR] packets carry no grip orientation (protocol < 0.5?) - using aim pose for the hands");
	}
	float len = sqrtf(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
	if (len < 1e-4f)
		return Matrix34::CreateIdentity();
	q.x /= len; q.y /= len; q.z /= len; q.w /= len;

	// same floor-relative lift as the head pose (see OpenXRRuntime::UpdateWinlatorXRPose)
	XrVector3f pos = { h.posX, h.posY + gXR->GetWinlatorFloorOffset(), h.posZ };
	return OpenXRToCrysis(q, pos);
}

void OpenXRInput::GetBoolState(XrAction action, XrActionStateBoolean& out)
{
	if (!m_usingWinlatorXR)
	{
		XrActionStateGetInfo getInfo{ XR_TYPE_ACTION_STATE_GET_INFO };
		getInfo.subactionPath = XR_NULL_PATH;
		getInfo.action = action;
		XR_CheckResult(xrGetActionStateBoolean(m_session, &getInfo, &out), "getting boolean action state", m_instance);
		return;
	}

	out.isActive = XR_FALSE;
	out.currentState = XR_FALSE;
	out.changedSinceLastSync = XR_FALSE;
	out.lastChangeTime = 0;
	WxrAction id = WxrFromHandle(action);
	if (id == WXR_NONE)
		return;
	const WxrBool& b = m_wxrBool[id];
	out.isActive = b.active ? XR_TRUE : XR_FALSE;
	out.currentState = b.state ? XR_TRUE : XR_FALSE;
	out.changedSinceLastSync = b.changed ? XR_TRUE : XR_FALSE;
}

void OpenXRInput::GetFloatState(XrAction action, XrActionStateFloat& out)
{
	if (!m_usingWinlatorXR)
	{
		XrActionStateGetInfo getInfo{ XR_TYPE_ACTION_STATE_GET_INFO };
		getInfo.subactionPath = XR_NULL_PATH;
		getInfo.action = action;
		xrGetActionStateFloat(m_session, &getInfo, &out);
		return;
	}

	out.isActive = XR_FALSE;
	out.currentState = 0.f;
	out.changedSinceLastSync = XR_FALSE;
	out.lastChangeTime = 0;
	WxrAction id = WxrFromHandle(action);
	if (id == WXR_NONE)
		return;
	const WxrFloat& f = m_wxrFloat[id];
	out.isActive = f.active ? XR_TRUE : XR_FALSE;
	out.currentState = f.value;
}

float OpenXRInput::GetWinlatorHapticAmplitude(int side) const
{
	side = clamp_tpl(side, 0, 1);
	if (!m_usingWinlatorXR || m_wxrHapticAmplitude[side] <= 0.f)
		return 0.f;
	if (gEnv->pTimer->GetAsyncCurTime() >= m_wxrHapticEndTime[side])
		return 0.f;
	return m_wxrHapticAmplitude[side];
}
