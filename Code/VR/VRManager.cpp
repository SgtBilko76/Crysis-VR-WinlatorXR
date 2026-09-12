#include "StdAfx.h"
#include "VRManager.h"

#include "Cry_Camera.h"
#include "Fists.h"
#include "GameCVars.h"
#include "GameUtils.h"
#include "HandPoses.h"
#include "Hooks.h"
#include "IPlayerInput.h"
#include "OpenXRRuntime.h"
#include "VRHaptics.h"
#include "VRRenderer.h"
#include "VRRenderUtils.h"
#include "Weapon.h"
#include "Menus/FlashMenuObject.h"
#include "WinlatorXR.h"
#include <imgui.h>

VRManager s_VRManager;
VRManager* gVR = &s_VRManager;

#ifndef _WIN64
int FASTCALL AI_SmartObjectEvent_Hook(IAISystem* self, void* notUsed, const char* event, IEntity*& user, IEntity*& object, const Vec3* pExtraPoint, bool bHighPriority)
#else
int AI_SmartObjectEvent_Hook(IAISystem* self, const char* event, IEntity*& user, IEntity*& object, const Vec3* pExtraPoint, bool bHighPriority)
#endif
{
	CPlayer* player = gVR->GetLocalPlayer();
	if (player && (player->GetEntity() == user || player->GetEntity() == object))
	{
		if (strcmp("OnUsed", event) == 0 || strcmp("OnUsedRelease", event) == 0)
		{
			player->SignalUsedEntity();
		}
	}
#ifndef _WIN64
	return hooks::CallOriginal(AI_SmartObjectEvent_Hook)(self, notUsed, event, user, object, pExtraPoint, bHighPriority);
#else
	return hooks::CallOriginal(AI_SmartObjectEvent_Hook)(self, event, user, object, pExtraPoint, bHighPriority);
#endif
}

#ifndef _WIN64
void FASTCALL I3DEngine_SetPostEffectParam_Hook(I3DEngine* self, void* notUsed, const char *pParam, float fValue)
#else
void I3DEngine_SetPostEffectParam_Hook(I3DEngine* self, const char *pParam, float fValue)
#endif
{
	if (strcmp("AlienInterference_Amount", pParam) == 0)
	{
		gHaptics->TriggerBHapticsEffect("shake_vest", 0.5f * fValue);
	}
#ifndef _WIN64
	hooks::CallOriginal(I3DEngine_SetPostEffectParam_Hook)(self, notUsed, pParam, fValue);
#else
	hooks::CallOriginal(I3DEngine_SetPostEffectParam_Hook)(self, pParam, fValue);
#endif
}

VRManager::~VRManager()
{
	// if Shutdown isn't properly called, we will get an infinite hang when trying to dispose of our D3D resources after
	// the game already shut down. So just let go here to avoid that
	for (int eye = 0; eye < 2; ++eye)
	{
		m_eyeViews[eye].Detach();
		m_eyeTextures[eye].Detach();
		m_eyeTextures11[eye].Detach();
	}
	m_hudView.Detach();
	m_hudTexture.Detach();
	m_hudTexture11.Detach();
	m_swapchain.Detach();
	m_device.Detach();
	m_context11.Detach();
	m_device11.Detach();
}

bool VRManager::Init()
{
	if (m_initialized)
		return true;

	hooks::InstallVirtualFunctionHook("SmartObjectEvent", gEnv->pAISystem, &IAISystem::SmartObjectEvent, &AI_SmartObjectEvent_Hook);
	hooks::InstallVirtualFunctionHook("SetPostEffectParamFloat", gEnv->p3DEngine, &I3DEngine::SetPostEffectParam, &I3DEngine_SetPostEffectParam_Hook);

	m_initialized = true;
	return true;
}

void VRManager::Shutdown()
{
	CryLogAlways("Shutting down VRManager...");

	gVRRenderUtils->Shutdown();

	for (int eye = 0; eye < 2; ++eye)
	{
		m_eyeViews[eye].Reset();
		m_eyeTextures[eye].Reset();
		m_eyeTextures11[eye].Reset();
	}
	m_hudView.Reset();
	m_hudTexture.Reset();
	m_hudTexture11.Reset();
	m_swapchain.Reset();
	m_device.Reset();
	m_context11.Reset();
	m_device11.Reset();

	m_initialized = false;
}

void VRManager::ReinitializeXR()
{
	gXR->Shutdown();
	gXR->Init();
	LUID requiredAdapterLuid;
	D3D_FEATURE_LEVEL requiredLevel;
	// need to call this, otherwise creating session results in error
	gXR->GetD3D11Requirements(&requiredAdapterLuid, &requiredLevel);
	gXR->CreateSession(m_device11.Get());
}

void VRManager::AwaitFrame()
{
	if (!m_initialized)
		return;

	gXR->AwaitFrame();
	m_prevViewYaw = m_updatedViewYaw;
}

void VRManager::CaptureEye(int eye)
{
	if (!m_swapchain)
		return;

	if (!m_device)
		InitDevice(m_swapchain.Get());

	if (!m_eyeTextures[eye])
	{
		CreateEyeTexture(eye);
		if (!m_eyeTextures[eye])
			return;
	}

	D3D10_TEXTURE2D_DESC desc;
	m_eyeTextures[eye]->GetDesc(&desc);
	Vec2i expectedSize = GetRenderSize();
	if (desc.Width != expectedSize.x || desc.Height != expectedSize.y)
	{
		// recreate with new resolution
		CreateEyeTexture(eye);
		if (!m_eyeTextures[eye])
			return;
	}

	// acquire and copy the current swap chain buffer to the eye texture
	CopyBackbufferToTexture(m_eyeTextures[eye].Get());
}

void VRManager::CaptureHUD()
{
	if (!m_swapchain)
		return;

	if (!m_device)
		InitDevice(m_swapchain.Get());

	if (!m_hudTexture)
	{
		CreateHUDTexture();
		if (!m_hudTexture)
			return;
	}

	D3D10_TEXTURE2D_DESC desc;
	m_hudTexture->GetDesc(&desc);
	Vec2i expectedSize = GetRenderSize();
	if (desc.Width != expectedSize.x || desc.Height != expectedSize.y)
	{
		// recreate with new resolution
		CreateHUDTexture();
		if (!m_hudTexture)
			return;
	}

	// acquire and copy the current swap chain buffer to the HUD texture
	CopyBackbufferToTexture(m_hudTexture.Get());

	if (gXR->IsUsingWinlatorXR())
	{
		// the back buffer *is* the headset image under WinlatorXR, so instead of a desktop mirror we
		// compose the actual stereo frame here
		ComposeWinlatorXRFrame();
		return;
	}

	// mirror the current eye texture to the backbuffer
	int mirrorEye = g_pGameCVars->vr_mirror_eye == 1 ? 1 : 0;
	RectF bounds = GetEffectiveRenderLimits(mirrorEye);
	if (!g_pGameCVars->vr_enable_frustum_tweaks)
		bounds = RectF();

	if (gVRRenderer->GetRenderMode() != RM_2D)
		gVRRenderUtils->CopyEyeToScreenMirror(m_eyeViews[mirrorEye].Get(), bounds);
}

void VRManager::SetSwapChain(IDXGISwapChain *swapchain)
{
	if (swapchain != m_swapchain.Get())
	{
		m_device.Reset();
	}

	m_swapchain = swapchain;
	if (!m_device)
	  InitDevice(swapchain);
}

void VRManager::FinishFrame(bool didRenderThisFrame)
{
	if (gXR->IsUsingWinlatorXR())
	{
		// the frame was composed into the back buffer by ComposeWinlatorXRFrame (pre-present); all that
		// is left is telling WinlatorXR how to display it
		if (!m_initialized)
			return;
		if (!didRenderThisFrame)
			gXR->AwaitFrame();
		gXR->FinishFrame();
		if (didRenderThisFrame)
			UpdateSmoothedPlayerHeight();
		return;
	}

	if (!m_initialized || !m_device || !m_device11 || !m_hudTexture11)
		return;

	if (!didRenderThisFrame)
	{
		// bit late technically, but need to do this to be able to update the HUD
		gXR->AwaitFrame();
	}

	ReleaseTextureSync(m_hudTexture.Get(), 1);

	AcquireTextureSync(m_hudTexture11.Get(), 1);
	gXR->SubmitHud(m_hudTexture11.Get());
	ReleaseTextureSync(m_hudTexture11.Get(), 0);

	if (!didRenderThisFrame)
	{
		gXR->FinishFrame();
		return;
	}

	ReleaseTextureSync(m_eyeTextures[0].Get(), 1);
	ReleaseTextureSync(m_eyeTextures[1].Get(), 1);
	AcquireTextureSync(m_eyeTextures11[0].Get(), 1);
	AcquireTextureSync(m_eyeTextures11[1].Get(), 1);
	// game is currently using symmetric projection, we need to cut off the texture accordingly
	RectF leftBounds = GetEffectiveRenderLimits(0);
	RectF rightBounds = GetEffectiveRenderLimits(1);
	gXR->SubmitEyes(m_eyeTextures11[0].Get(), leftBounds, m_eyeTextures11[1].Get(), rightBounds);
	ReleaseTextureSync(m_eyeTextures11[0].Get(), 0);
	ReleaseTextureSync(m_eyeTextures11[1].Get(), 0);

	gXR->FinishFrame();

	UpdateSmoothedPlayerHeight();
}

Vec2i VRManager::GetRenderSize() const
{
	float ll, lr, lt, lb, rl, rr, rt, rb;
	gXR->GetFov(0, ll, lr, lt, lb);
	gXR->GetFov(1, rl, rr, rt, rb);
	if (ll == 0)
	{
		// XR is not running, yet
		return Vec2i(gEnv->pRenderer->GetWidth(), gEnv->pRenderer->GetHeight());
	}
	float verticalFov = max(max(fabsf(lt), fabsf(lb)), max(fabsf(rt), fabsf(rb)));
	float horizontalFov = max(max(fabsf(ll), fabsf(lr)), max(fabsf(rl), fabsf(rr)));
	float vertRenderScale = 2.f * verticalFov / min(fabsf(lt) + fabsf(lb), fabsf(rt) + fabsf(rb));

	Vec2i renderSize = gXR->GetRecommendedRenderSize();
	renderSize.y *= vertRenderScale;
	renderSize.x = renderSize.y * horizontalFov / verticalFov;
	return renderSize;
}

Vec3 VRManager::EstimateShoulderPosition(int side, const Vec3& handPos, float minDistance, float maxDistance)
{
	CPlayer *player = static_cast<CPlayer *>(gEnv->pGame->GetIGameFramework()->GetClientActor());
	if (!player)
		return Vec3(0, 0, 0);

	CCamera view = gVRRenderer->GetCurrentViewCamera();
	ModifyViewCamera(side, view);

	Vec3 estimatedShoulderPos = view.GetMatrix() * Vec3((-1.f + 2.f * side) * 0.2f, +0.05f, -0.3f);
	float distance = handPos.GetDistance(estimatedShoulderPos);
	Vec3 handToShoulderDir = (estimatedShoulderPos - handPos).GetNormalized();

	if (distance < minDistance)
	{
		// we'll need to adjust the shoulder position, but be careful that we don't accidentally put the shoulder
		// in the player's face if the hand is behind the head
		// move shoulder back from head
		Vec3 fwd = view.GetMatrix().GetColumn1();
		float angle = cry_acosf(fwd.Dot(handToShoulderDir));
		float moveAmount = cry_cosf(angle) * distance + cry_sqrtf_fast(minDistance * minDistance - distance * distance * cry_sinf(angle) * cry_sinf(angle));
		estimatedShoulderPos -= moveAmount * fwd;

		distance = handPos.GetDistance(estimatedShoulderPos);
	}

	if (distance > maxDistance)
	{
		estimatedShoulderPos = handPos + handToShoulderDir * maxDistance;
	}

	return estimatedShoulderPos;
}

void VRManager::ModifyViewCamera(int eye, CCamera& cam)
{
	if (IsEquivalent(cam.GetPosition(), Vec3(0, 0, 0), VEC_EPSILON))
	{
		// no valid camera set, leave it
		return;
	}

	if (!m_initialized)
	{
		if (eye == 1)
		{
			Vec3 pos = cam.GetPosition();
			pos.x += 0.1f;
			cam.SetPosition(pos);
		}
		return;
	}

	Matrix34 viewMat = GetBaseVRTransform(true);

	Matrix34 eyeMat = GetEyeTransform(eye);
	Vec3 eyePos = eyeMat.GetTranslation();
	eyeMat.SetTranslation(eyePos);
	viewMat = viewMat * eyeMat;

	cam.SetMatrix(viewMat);

	// we don't have obvious access to the projection matrix, and the camera code is written with symmetric projection in mind
	// for now, set up a symmetric FOV and cut off parts of the image during submission
	float tanl, tanr, tant, tanb;
	gXR->GetFov(eye, tanl, tanr, tant, tanb);
	float verticalFov = max(fabsf(tant), fabsf(tanb));
	float vertFov = atanf(verticalFov) * 2;
	Vec2i renderSize = GetRenderSize();
	cam.SetFrustum(renderSize.x, renderSize.y, vertFov, cam.GetNearPlane(), cam.GetFarPlane());

	if (g_pGameCVars->vr_enable_frustum_tweaks)
	{
		// but we can set up frustum planes for our asymmetric projection, which should help culling accuracy.
		cam.UpdateFrustumFromVRRaw(tanl, tanr, tanb, tant);
	}
}

void VRManager::ModifyViewCameraFor3DCinema(int eye, CCamera& cam)
{
	Matrix34 transform = cam.GetMatrix();
	Vec3 pos = transform.GetTranslation();
	pos += g_pGameCVars->vr_cinema_3d_eye_dist * transform.GetColumn0().GetNormalized() * (eye == 0 ? -1 : 1);
	transform.SetTranslation(pos);
	cam.SetMatrix(transform);
}

void VRManager::ModifyViewForBinoculars(SViewParams& view)
{
	float vr_binocular_size = 0.8f;
	bool leftHanded = g_pGameCVars->vr_weapon_hand == 0;

	Matrix34 controllerTransform = GetWorldControllerTransform(GetHandSide(OFF_HAND));
	Vec3 forward = controllerTransform.GetColumn1();
	controllerTransform.SetTranslation(controllerTransform.GetTranslation() + 0.5f * forward);
	controllerTransform = controllerTransform * Matrix34::CreateTranslationMat(Vec3((leftHanded ? 1 : -1) * vr_binocular_size / 2, 0, vr_binocular_size / 2));

	view.rotation = GetQuatFromMat33((Matrix33)controllerTransform);
	view.position = controllerTransform.GetTranslation();
}

void VRManager::ModifyCameraFor2D(CCamera& cam)
{
	if (gVRRenderer->AreBinocularsActive())
		return;

	CPlayer* player = GetLocalPlayer();
	CWeapon* weapon = player ? player->GetWeapon(player->GetCurrentItemId()) : nullptr;
	if (!weapon || !(weapon->IsZoomed() || weapon->IsZooming()) || player->GetLinkedVehicle())
		return;

	Vec3 scopePos;
	const SPlayerStats &stats = *static_cast<const SPlayerStats*>(player->GetActorStats());
	Ang3 angles = stats.FPWeaponAngles;
	angles.y = 0;
	Matrix34 weaponView = Matrix34::CreateRotationXYZ(angles);
	if (!weapon->GetScopePosition(scopePos))
	{
		scopePos = stats.FPWeaponPos;
	}
	else
	{
		scopePos -= 0.1f * weaponView.GetColumn1();
	}
	weaponView.SetTranslation(scopePos);
	cam.SetMatrix(weaponView);
}

inline float GetAngleDifference360( float a1, float a2 )
{
	float res = a1-a2;
	if (res > gf_PI)
		res = res - gf_PI2;
	else if (res < -gf_PI)
		res = gf_PI2 + res;
	return res;
}

void VRManager::ModifyWeaponPosition(CPlayer* player, Ang3& weaponAngles, Vec3& weaponPosition, bool slave)
{
	if (g_pGame->GetMenu()->IsMenuActive())
		return;

	CWeapon* weapon = player->GetWeapon(player->GetCurrentItemId());
	if (!weapon || weapon->IsModifying() || (!gVRRenderer->ShouldRenderVR() && !weapon->IsZoomed() && !weapon->IsZooming()))
		return;

	if (slave)
	{
		if (!weapon->GetDualWieldSlave())
			return;
		weapon = static_cast<CWeapon*>(weapon->GetDualWieldSlave()->GetIWeapon());
	}

	int weaponHand = g_pGameCVars->vr_weapon_hand;
	if (weapon->IsDualWieldMaster())
		weaponHand = 1;
	if (weapon->IsDualWieldSlave())
		weaponHand = 0;

	Matrix34 adjustedControllerTransform = GetWorldControllerWeaponTransform(weaponHand);
	// if we are two-handing the weapon and it's not a pistol, apply a two hand orientation
	if (IsOffHandGrabbingWeapon() && weapon->GetEntity()->GetClass() != CItem::sSOCOMClass)
	{
		adjustedControllerTransform = GetTwoHandWeaponTransform();
		if (weapon->GetTwoHandYawOffset() != 0)
		{
			float dir = g_pGameCVars->vr_weapon_hand == 1 ? 1 : -1;
			adjustedControllerTransform = adjustedControllerTransform * Matrix34::CreateRotationY(dir * weapon->GetTwoHandYawOffset() * gf_PI / 180.f);
		}
	}

	Matrix34 inverseWeaponGripTransform = weapon->GetInverseGripTransform();
	Matrix34 trackedTransform = adjustedControllerTransform * inverseWeaponGripTransform;
	weaponPosition = trackedTransform.GetTranslation();
	weaponAngles = Ang3(trackedTransform);

	if (weapon->IsZoomed())
	{
		weaponAngles.y = 0;
		// smooth weapon orientation with exponential decay, since otherwise zoom is extremely unstable
		IZoomMode* zm = weapon->GetZoomMode(weapon->GetCurrentZoomMode());
		float factor = 0.03f * cry_powf(1.f / zm->GetZoomFoVScale(zm->GetCurrentStep()), 1.5f);
		Ang3 smoothedAngles = weaponAngles;
		float yawPitchDecay = powf(2.f, -gEnv->pSystem->GetITimer()->GetFrameTime() / factor);
		smoothedAngles.z = weaponAngles.z + GetAngleDifference360(m_smoothedWeaponAngles.z, weaponAngles.z) * yawPitchDecay;
		smoothedAngles.x = weaponAngles.x + GetAngleDifference360(m_smoothedWeaponAngles.x, weaponAngles.x) * yawPitchDecay;
		weaponAngles = smoothedAngles;
	}

	m_smoothedWeaponAngles = weaponAngles;
}

Matrix34 VRManager::GetControllerTransform(int side)
{
	Matrix34 controllerTransform = gXR->GetInput()->GetControllerTransform(side);
	Matrix33 refTransform = GetReferenceTransform();
	Matrix34 adjustedControllerTransform = refTransform * (Matrix33)controllerTransform;
	adjustedControllerTransform.SetTranslation(refTransform * (controllerTransform.GetTranslation() - m_referencePosition));
	return adjustedControllerTransform;
}

Matrix34 VRManager::GetWorldControllerTransform(int side)
{
	Matrix34 controllerTransform = GetControllerTransform(side);
	Matrix34 baseTransform = GetBaseVRTransform(true);
	return baseTransform * controllerTransform;
}

Matrix34 VRManager::GetControllerWeaponTransform(int side)
{
	Matrix34 controllerTransform = gXR->GetInput()->GetControllerWeaponTransform(side);
	Matrix33 refTransform = GetReferenceTransform();
	Matrix34 adjustedControllerTransform = refTransform * (Matrix33)controllerTransform;
	adjustedControllerTransform.SetTranslation(refTransform * (controllerTransform.GetTranslation() - m_referencePosition));
	return adjustedControllerTransform;
}

Matrix34 VRManager::GetTwoHandWeaponTransform()
{
	int weaponHand = g_pGameCVars->vr_weapon_hand;
	int offHand = 1 - weaponHand;

	Matrix34 mainHandTransform = GetWorldControllerTransform(weaponHand);
	Matrix34 offHandTransform = GetWorldControllerTransform(offHand);

	// build rotation from main hand towards off hand
	Vec3 fwd = offHandTransform.GetTranslation() - mainHandTransform.GetTranslation();
	fwd.Normalize();
	Vec3 right = mainHandTransform.GetColumn0().GetNormalized();
	Vec3 up = right.Cross(fwd).GetNormalized();
	right = fwd.Cross(up).GetNormalized();
	mainHandTransform.SetFromVectors(right, fwd, up, mainHandTransform.GetTranslation());

	// Weapon bones are offset to what our grip pose is, so we need to rotate the pose a bit
	Matrix33 correction = Matrix33::CreateRotationX(-gf_PI/2) * Matrix33::CreateRotationY(-gf_PI/2);

	return mainHandTransform * correction;
}

Matrix34 VRManager::GetWorldControllerWeaponTransform(int side)
{
	Matrix34 controllerTransform = GetControllerWeaponTransform(side);
	Matrix34 view = GetBaseVRTransform(true);
	return view * controllerTransform;
}

Quat VRManager::GetMovementControllerQuat()
{
	Matrix34 controllerTransform = GetWorldControllerTransform(g_pGameCVars->vr_movement_hand);
	return Quat(controllerTransform);
}

Vec3 VRManager::GetControllerVelocity(int side)
{
	Vec3 controllerVelocity = gXR->GetInput()->GetControllerVelocity(side);
	Matrix33 refTransform = GetReferenceTransform();
	return refTransform.TransformVector(controllerVelocity);
}

Vec3 VRManager::GetControllerWorldVelocity(int side)
{
	Matrix34 view = GetBaseVRTransform(true);
	return view.TransformVector(GetControllerVelocity(side));
}

void VRManager::ModifyPlayerEye(CPlayer* pPlayer, Vec3& eyePosition, Vec3& eyeDirection)
{
	if (g_pGame->GetMenu()->IsMenuActive()
		|| g_pGame->GetHUD()->GetModalHUD()
		|| !gVRRenderer->ShouldRenderVR())
	{
		return;
	}

	if (pPlayer->GetActorStats()->mountedWeaponID || pPlayer->GetLinkedVehicle())
		return;

	CCamera left = gVRRenderer->GetCurrentViewCamera();
	ModifyViewCamera(0, left);
	CCamera right = gVRRenderer->GetCurrentViewCamera();
	ModifyViewCamera(1, right);

	eyePosition = 0.5f * (left.GetPosition() + right.GetPosition());
	eyeDirection = 0.5f * (left.GetViewdir() + right.GetViewdir());
}

Quat VRManager::GetHMDQuat()
{
	return 0.5f * (Quat(GetEyeTransform(0)) + Quat(GetEyeTransform(1)));
}

RectF VRManager::GetEffectiveRenderLimits(int eye)
{
	float l, r, t, b;
	gXR->GetFov(eye, l, r, t, b);
	float verticalFov = max(fabsf(t), fabsf(b));
	auto renderSize = GetRenderSize();
	float horizontalFov = verticalFov * renderSize.x / renderSize.y; //max(fabsf(l), fabsf(r));
	RectF result;
	if (verticalFov > 0)
	{
		result.x = 0.5f + 0.5f * l / horizontalFov;
		result.y = 0.5f - 0.5f * t / verticalFov;
		result.w = 0.5f + 0.5f * r / horizontalFov - result.x;
		result.h = 0.5f - 0.5f * b / verticalFov - result.y;
	}
	else
	{
		result.x = 0;
		result.y = 0;
		result.w = 1;
		result.h = 1;
	}
	return result;
}

Matrix33 VRManager::GetReferenceTransform() const
{
	Ang3 refAngles(0, 0, m_referenceYaw);
	return Matrix33::CreateRotationXYZ(refAngles).GetTransposed();
}

Vec3 VRManager::GetHmdOffset() const
{
	Vec3 position = gXR->GetHmdTransform().GetTranslation();
	position.z = 0;
	return GetReferenceTransform() * (position - m_referencePosition);
}

float VRManager::GetHmdYawOffset() const
{
	Ang3 angles(gXR->GetHmdTransform());
	return angles.z - m_referenceYaw;
}

Matrix34 VRManager::GetBaseVRTransform(bool smooth) const
{
	IViewSystem* system = g_pGame->GetIGameFramework()->GetIViewSystem();
	bool isCutscene = system && system->IsPlayingCutScene();
	CPlayer *pPlayer = GetLocalPlayer();
	bool isNonPlayerView = pPlayer && system && system->GetActiveView() && system->GetActiveView()->GetLinkedId() != pPlayer->GetEntityId();
	bool inVehicle = pPlayer && pPlayer->GetLinkedVehicle();
	bool mountedWeapon = pPlayer && pPlayer->GetActorStats()->mountedWeaponID;

	if (isCutscene || isNonPlayerView || inVehicle || !pPlayer || mountedWeapon)
	{
		CCamera cam = gVRRenderer->GetCurrentViewCamera();
		Ang3 angles = cam.GetAngles();
		Vec3 position = cam.GetPosition();
		position.z -= m_hmdReferenceHeight;

		// eliminate pitch and roll
		// switch around, because these functions do not agree on which angle is what...
		angles.z = angles.x;
		angles.y = 0;
		angles.x = 0;

		// in cutscenes, do not follow the camera rotation; only snap to new yaw angle if the difference is too big
		m_updatedViewYaw = m_prevViewYaw;

		float yawDiff = angles.z - m_updatedViewYaw;
		if (yawDiff < -gf_PI)
			yawDiff += 2 * gf_PI;
		else if (yawDiff > gf_PI)
			yawDiff -= 2 * gf_PI;

		float maxDiff = g_pGameCVars->vr_cutscenes_angle_snap * gf_PI / 180.f;
		if (g_pGameCVars->vr_cutscenes_2d)
			maxDiff = 0;
		if (yawDiff > maxDiff || yawDiff < -maxDiff)
			m_updatedViewYaw = angles.z;

		if (inVehicle || mountedWeapon)
		{
			// don't use this while in a vehicle, it feels off
			m_updatedViewYaw = angles.z;
		}

		angles.z = m_updatedViewYaw;
		position.z += g_pGameCVars->vr_height_offset;

		Matrix34 viewMat;
		viewMat.SetRotationXYZ(angles, position);
		return viewMat;
	}

	// use player position and orientation for our base
	Vec3 pos = pPlayer->GetEntity()->GetWorldPos();
	if (smooth)
		pos.z = m_smoothedHeight;
	Ang3 ang = pPlayer->GetEntity()->GetWorldAngles();
	ang.x = ang.y = 0;

	// offset by stance as needed
	EStance physicalStance = pPlayer->GetPhysicalStance();
	const SStanceInfo* curStance = pPlayer->GetStanceInfo(pPlayer->GetStance());
	const SStanceInfo* refStance = pPlayer->GetStanceInfo(STANCE_STAND);
	if (g_pGameCVars->vr_seated_mode)
	{
		pos.z += curStance->viewOffset.z - m_hmdReferenceHeight;
	}
	else if (physicalStance == STANCE_PRONE)
	{
		// nothing to offset here, we'll take the camera height as is
	}
	else if (physicalStance == STANCE_CROUCH)
	{
		if (pPlayer->GetStance() == STANCE_PRONE)
		{
			pos.z -= (pPlayer->GetStanceInfo(STANCE_CROUCH)->viewOffset.z - pPlayer->GetStanceInfo(STANCE_PRONE)->viewOffset.z);
		}
	}
	else if (curStance != refStance)
	{
		pos.z -= (m_hmdReferenceHeight - curStance->viewOffset.z);
	}

	pos.z += g_pGameCVars->vr_height_offset;

	Matrix34 baseMat = Matrix34::CreateRotationXYZ(ang, pos);
	return baseMat;
}

void VRManager::UpdateReferenceOffset(const Vec3& offset)
{
	// added offset is in player space, convert back to raw HMD space
	Vec3 rawOffset = GetReferenceTransform().GetTransposed() * offset;
	rawOffset.z = 0;
	m_referencePosition += rawOffset;
}

void VRManager::UpdateReferenceYaw(float yaw)
{
	m_referenceYaw += yaw;
}

Matrix34 VRManager::GetEyeTransform(int eye) const
{
	Matrix34 rawEye = gXR->GetRenderEyeTransform(eye);
	Vec3 position = GetReferenceTransform() * (rawEye.GetTranslation() - m_referencePosition);
	Ang3 angles(rawEye);
	angles.z -= m_referenceYaw;
	return Matrix34::CreateRotationXYZ(angles, position);
}

EStance VRManager::GetPhysicalStance() const
{
	CPlayer *pPlayer = GetLocalPlayer();
	if (!pPlayer || g_pGameCVars->vr_seated_mode)
		return STANCE_STAND;

	float physicalHeight = gXR->GetHmdTransform().GetTranslation().z;
	if (physicalHeight <= pPlayer->GetStanceInfo(STANCE_PRONE)->viewOffset.z + 0.15f)
		return STANCE_PRONE;
	if (physicalHeight <= pPlayer->GetStanceInfo(STANCE_CROUCH)->viewOffset.z + 0.2f)
		return STANCE_CROUCH;
	return STANCE_STAND;
}

void VRManager::Update()
{
	if (gXR->IsUsingWinlatorXR())
	{
		EnsureWinlatorXRWindow();
		UpdateDesktopInputBlock();
	}

	UpdateOffHandRayQuery();

	gXR->SetHudVisibility(true);

	if ((g_pGame->GetMenu()->IsMenuActive() || gEnv->pConsole->IsOpened()) || g_pGame->GetMenu()->IsLoadingScreenActive())
	{
		if (!m_wasInMenu)
		{
			m_wasInMenu = true;
			RecalibrateView();
		}
		SetHudInFrontOfPlayer();
		return;
	}

	if (m_wasInMenu)
	{
		m_wasInMenu = false;
		RecalibrateView();
	}

	bool showHudFixed= g_pGame->GetHUD() && g_pGame->GetHUD()->ShouldDisplayHUDFixed();
	CPlayer* player = GetLocalPlayer();
	CWeapon* weapon = player ? player->GetWeapon(player->GetCurrentItemId()) : nullptr;
	bool isWeaponZoom = weapon && (weapon->IsZoomed() || weapon->IsZooming());
	if (gVRRenderer->AreBinocularsActive())
	{
		if (g_pGame->GetIGameFramework()->GetIViewSystem()->IsPlayingCutScene())
			SetHudInFrontOfPlayer();
		else
			SetHudAttachedToOffHand();
	}
	else if (isWeaponZoom)
		SetHudInFrontOfPlayer();
	else if (gVRRenderer->ShouldRenderVR() && !showHudFixed)
	{
		gXR->SetHudVisibility(g_pGameCVars->vr_hide_hud == 0);
		if (player && (player->GetLinkedVehicle() || player->GetActorStats()->mountedWeaponID))
			SetVehicleHud();
		else
			SetHudAttachedToHead();
	}
	else
		SetHudInFrontOfPlayer();
}

bool VRManager::RecalibrateView()
{
	if (!gXR->ArePosesValid())
		return false;

	Matrix34 hmdTransform = Matrix34(gXR->GetHmdTransform());

	m_referencePosition = hmdTransform.GetTranslation();
	m_referencePosition.z = 0;
	m_hmdReferenceHeight = hmdTransform.GetTranslation().z;

	Ang3 angles;
	angles.SetAnglesXYZ((Matrix33)hmdTransform);
	m_referenceYaw = angles.z;

	// recalibrate menu positioning
	angles.x = angles.y = 0;
	//angles.z += gf_PI;
	m_fixedHudTransform.SetRotationXYZ(angles, hmdTransform.GetTranslation());
	Vec3 dir = m_fixedHudTransform.GetColumn1();
	Vec3 pos = m_fixedHudTransform.GetTranslation() + 5 * dir;
	m_fixedHudTransform.SetTranslation(pos);

	return true;
}

extern XrPosef CrysisToOpenXR(const Matrix34& transform);
extern Matrix34 OpenXRToCrysis(const XrQuaternionf& a, const XrVector3f& b);

void VRManager::SetHudInFrontOfPlayer()
{
	if (!m_fixedPositionInitialized)
	{
		if (RecalibrateView())
			m_fixedPositionInitialized = true;
	}

	gXR->SetHudPose(CrysisToOpenXR(m_fixedHudTransform));

	Vec2i renderSize = GetRenderSize();
	gXR->SetHudSize(4.f, 4.f * renderSize.y / renderSize.x);
}

void VRManager::SetHudAttachedToHead()
{
	m_fixedPositionInitialized = false;

	Matrix34 hudTransform = Matrix34(gXR->GetHmdTransform());
	Vec3 pos = hudTransform.GetTranslation();
	Ang3 angles;
	angles.SetAnglesXYZ((Matrix33)hudTransform);
	hudTransform.SetRotationXYZ(angles, pos);
	Vec3 forward = hudTransform.GetColumn1();
	hudTransform.SetTranslation(hudTransform.GetTranslation() + 2 * forward);
	gXR->SetHudPose(CrysisToOpenXR(hudTransform));

	Vec2i renderSize = GetRenderSize();
	gXR->SetHudSize(2.f, 2.f * renderSize.y / renderSize.x);
}

void VRManager::SetHudAttachedToOffHand()
{
	float vr_binocular_size = 0.8f;
	m_fixedPositionInitialized = false;
	bool leftHanded = g_pGameCVars->vr_weapon_hand == 0;
	Matrix34 transform = gXR->GetInput()->GetControllerTransform(leftHanded ? 1 : 0);

	Vec3 pos = transform.GetTranslation();
	Ang3 angles;
	angles.SetAnglesXYZ((Matrix33)transform);
	transform.SetRotationXYZ(angles, pos);

	Vec3 forward = transform.GetColumn1();
	transform.SetTranslation(transform.GetTranslation() + 0.5f * forward);
	transform = transform * Matrix34::CreateTranslationMat(Vec3((leftHanded ? -1 : 1) * vr_binocular_size / 2, 0, vr_binocular_size / 2));

	XrPosef hudTransform = CrysisToOpenXR(transform);
	gXR->SetHudPose(hudTransform);

	Vec2i renderSize = GetRenderSize();
	gXR->SetHudSize(vr_binocular_size, vr_binocular_size * renderSize.y / renderSize.x);
}

void VRManager::SetHudAttachedToWeaponHand()
{
	float vr_scope_size = 0.25f;
	m_fixedPositionInitialized = false;
	Matrix34 transform = gXR->GetInput()->GetControllerTransform(GetHandSide(WEAPON_HAND));
	Vec3 headPos = gXR->GetHmdTransform().GetTranslation() - Vec3(0, 0, vr_scope_size/2);
	Vec3 fwd = transform.GetTranslation() - headPos;
	Vec3 up = Vec3(0, 0, 1);
	Vec3 right = fwd.Cross(up).GetNormalized();
	up = right.Cross(fwd).GetNormalized();

	transform.SetFromVectors(right, fwd, up, transform.GetTranslation());
	Ang3 angles = Ang3::GetAnglesXYZ((Matrix33)transform);
	angles.y = 0;
	transform.SetRotationXYZ(angles, transform.GetTranslation());
	transform = transform * Matrix34::CreateTranslationMat(Vec3(0, 0.1f, vr_scope_size / 2));

	angles.SetAnglesXYZ((Matrix33)transform);
	Vec3 pos = transform.GetTranslation();
	transform.SetRotationXYZ(angles, pos);

	XrPosef hudTransform = CrysisToOpenXR(transform);
	gXR->SetHudPose(hudTransform);

	Vec2i renderSize = GetRenderSize();
	gXR->SetHudSize(vr_scope_size, vr_scope_size * renderSize.y / renderSize.x);
}

void VRManager::SetVehicleHud()
{
	m_fixedPositionInitialized = false;

	CPlayer* player = GetLocalPlayer();
	if (!player)
	{
		SetHudAttachedToHead();
		return;
	}

	float hudSize = 12.f;

	SMovementState state;
	player->GetMovementController()->GetMovementState(state);
	Vec3 crosshairPos = state.weaponPosition;
	Vec3 dir = state.aimDirection;
	crosshairPos += 20.f * dir;
	if (player->IsThirdPerson())
	{
		crosshairPos += 15.f * dir;
		hudSize = 20.f;
	}

	Matrix34 hudTransform = Matrix33::CreateRotationVDir(dir);
	hudTransform.SetTranslation(crosshairPos);
	hudTransform = GetBaseVRTransform(true).GetInvertedFast() * hudTransform;

	Matrix34 refTransform = GetReferenceTransform().GetInverted();
	refTransform.SetTranslation(GetHmdOffset());

	gXR->SetHudPose(CrysisToOpenXR(refTransform * hudTransform));
	Vec2i renderSize = GetRenderSize();
	gXR->SetHudSize(hudSize, hudSize * renderSize.y / renderSize.x);
}

void TwoBoneIKSolve(QuatT& a, QuatT& b, QuatT& c, const Vec3& t)
{
	QuatT wa = a;
	QuatT wb = wa * b;
	QuatT wc = wb * c;

	Vec3 a2b = wb.t - wa.t;
	Vec3 a2c = wc.t - wa.t;
	Vec3 b2c = wc.t - wb.t;
	Vec3 a2t = t - wa.t;

	float lab = a2b.GetLength();
	float lbc = b2c.GetLength();
	float lac = a2c.GetLength();
	float lat = clamp(a2t.GetLength(), 0, lab + lbc);

	// get current interior angles
	float ac_ab_0 = cry_acosf(a2c.Dot(a2b) / lab / lac);
	float ba_bc_0 = cry_acosf((-a2b).Dot(b2c) / lab / lbc);
	float ac_at_0 = cry_acosf(a2c.Dot(a2t) / lac / a2t.GetLength());

	// desired angles based on the cosine rule
	float ac_ab_1 = cry_acosf((lab*lab + lat*lat - lbc*lbc) / (2*lab*lat));
	float ba_bc_1 = cry_acosf((lab*lab + lbc*lbc - lat*lat) / (2*lab*lbc));

	// apply angles locally to the joints
	Vec3 axis0 = a2c.Cross(a2b).GetNormalized();
	Vec3 axis1 = a2c.Cross(a2t).GetNormalized();
	Quat r0 = Quat::CreateRotationAA(ac_ab_1 - ac_ab_0, wa.q.GetInverted() * axis0);
	Quat r1 = Quat::CreateRotationAA(ba_bc_1 - ba_bc_0, wb.q.GetInverted() * axis0);
	Quat r2 = Quat::CreateRotationAA(ac_at_0, wa.q.GetInverted() * axis1);
	a.q = a.q * (r0 * r2);
	b.q = b.q * r1;
}

void VRManager::CalcWeaponArmIK(int side, ISkeletonPose* skeleton, CWeapon* weapon)
{
	// the way this works is that the weapon and thus the hands are already positioned as intended
	// we merely set a new base position for the shoulder joint and then IK solve such that the hand joint
	// ends up in its previous position

	int16 shoulderJointId = skeleton->GetJointIDByName(side == 1 ? "upperarm_R" : "upperarm_L");
	int16 elbowJointId = skeleton->GetJointIDByName(side == 1 ? "forearm_R" : "forearm_L");
	int16 handJointId = skeleton->GetJointIDByName(side == 1 ? "hand_R" : "hand_L");

	QuatT shoulderJoint = skeleton->GetAbsJointByID(shoulderJointId);
	QuatT elbowJoint = skeleton->GetDefaultRelJointByID(elbowJointId);
	QuatT handJoint = skeleton->GetDefaultRelJointByID(handJointId);
	QuatT target = skeleton->GetAbsJointByID(handJointId);

	bool offHandFollowsGun = m_offHandFollowsWeapon || weapon->IsDualWield() || weapon->IsMounted();
	bool isOffHandClass = weapon->GetEntity()->GetClass() == CItem::sOffHandClass;
	if ((side != g_pGameCVars->vr_weapon_hand && !offHandFollowsGun) || isOffHandClass)
	{
		// hand is detached from weapon, so position it towards the controller, instead
		Quat origHandRot = target.q;
		target = weapon->CalcHandFromControllerInWeapon(side);
		Quat rotDiff = target.q * origHandRot.GetInverted();
		shoulderJoint.q = rotDiff * shoulderJoint.q;
	}

	// if actual hand is too far from the estimated shoulder position, we need to move the whole arm forward
	float maxDistance = 0.99f * (elbowJoint.t.GetLength() + handJoint.t.GetLength());
	// also require a min distance / angle of > 90° at the elbow, since otherwise the IK can look really off
	// better to just move back the whole arm then
	float minDistance = 0.8f * cry_sqrtf_fast(elbowJoint.t.GetLengthSquared() + handJoint.t.GetLengthSquared());

	Vec3 shoulderWorldPos = gVR->EstimateShoulderPosition(side, weapon->GetEntity()->GetWorldTM().TransformPoint(target.t), minDistance, maxDistance);
	Matrix34 invEntityTrans = weapon->GetEntity()->GetWorldTM().GetInvertedFast();
	Vec3 shoulderInWeaponPos = invEntityTrans.TransformPoint(shoulderWorldPos);
	shoulderJoint.t = shoulderInWeaponPos;

	TwoBoneIKSolve(shoulderJoint, elbowJoint, handJoint, target.t);

	// make sure the hand joint really ends up in the same position and orientation, no matter what
	handJoint.q = (shoulderJoint.q * elbowJoint.q).GetInverted() * target.q;
	Vec3 diffToTarget = target.t - (shoulderJoint * elbowJoint * handJoint).t;
	shoulderJoint.t += diffToTarget;

	int16 parent = skeleton->GetParentIDByID(shoulderJointId);
	shoulderJoint = skeleton->GetAbsJointByID(parent).GetInverted() * shoulderJoint;
	skeleton->SetPostProcessQuat(shoulderJointId, shoulderJoint);
	skeleton->SetPostProcessQuat(elbowJointId, elbowJoint);
	skeleton->SetPostProcessQuat(handJointId, handJoint);

	if (side != g_pGameCVars->vr_weapon_hand && !m_offHandFollowsWeapon && !weapon->IsDualWield() && !weapon->IsMounted() && (side != 0 || !isOffHandClass))
	{
		ApplyHandPose(side, skeleton, gXR->GetInput()->GetGripAmount(side));
	}
	if (side == g_pGameCVars->vr_weapon_hand && (weapon->GetEntity()->GetClass() == CItem::sFistsClass || isOffHandClass) && !weapon->IsDualWield() && (side != 0 || !isOffHandClass))
	{
		ApplyHandPose(side, skeleton, gXR->GetInput()->GetGripAmount(side));
	}
}

void VRManager::TryGrabWeaponWithOffHand()
{
	CPlayer* player = GetLocalPlayer();
	CWeapon* weapon = player->GetWeapon(player->GetCurrentItemId());
	if (!weapon)
		return;
	if (weapon->IsDualWield() || weapon->GetEntity()->GetClass() == CItem::sFistsClass || weapon->GetEntity()->GetClass() == CItem::sOffHandClass)
		return;

	Vec3 controllerPos = GetWorldControllerWeaponTransform(1 - g_pGameCVars->vr_weapon_hand).GetTranslation();
	float controllerFromWeapon = weapon->GetOffHandGrabLocation().GetDistance(controllerPos);

	if (controllerFromWeapon <= 0.3f)
	{
		m_offHandFollowsWeapon = true;

		// apply benefits of the flat game's iron sights zoom for recoil and spread
		//weapon->SetCurrentZoomMode(0);
		if (IZoomMode* zm = weapon->GetZoomMode(0))
		{
			zm->ApplyZoomMod(weapon->GetFireMode(weapon->GetCurrentFireMode()));
		}
	}
}

void VRManager::DetachOffHandFromWeapon()
{
	m_offHandFollowsWeapon = false;

	CPlayer* player = GetLocalPlayer();
	CWeapon* weapon = player->GetWeapon(player->GetCurrentItemId());
	if (!weapon)
		return;

	//weapon->SetCurrentZoomMode(0);
	IFireMode* fm = weapon->GetFireMode(weapon->GetCurrentFireMode());
	if (fm)
	{
		fm->ResetRecoilMod();
		fm->ResetSpreadMod();
	}
}

CPlayer* VRManager::GetLocalPlayer() const
{
	return static_cast<CPlayer *>(gEnv->pGame->GetIGameFramework()->GetClientActor());
}

int VRManager::GetHandSide(EVRHand hand) const
{
	switch (hand)
	{
	case LEFT_HAND: return 0;
	case RIGHT_HAND: return 1;
	case WEAPON_HAND: return g_pGameCVars->vr_weapon_hand;
	case OFF_HAND: return 1 - g_pGameCVars->vr_weapon_hand;
	case MOVEMENT_HAND: return g_pGameCVars->vr_movement_hand;
	case ROTATION_HAND: return 1 - g_pGameCVars->vr_movement_hand;
	}

	return 0;
}

bool VRManager::IsHandNearHead(EVRHand hand, float maxDist)
{
	Vec3 headPosition = gXR->GetHmdTransform().GetTranslation();
	Vec3 handPosition = gXR->GetInput()->GetControllerTransform(GetHandSide(hand)).GetTranslation();

	return handPosition.GetDistance(headPosition) < maxDist;
}

bool VRManager::IsHandNearShoulder(EVRHand hand)
{
	int handSide = GetHandSide(hand);
	Vec3 handLocation = gXR->GetInput()->GetControllerTransform(handSide).GetTranslation();
	Matrix44 headTransform = gXR->GetHmdTransform();
	Vec3 headLocation = headTransform.GetTranslation();

	Ang3 headRot = Ang3::GetAnglesXYZ((Matrix33)headTransform);
	headRot.x = headRot.y = 0;
	Quat headQuat = Quat::CreateRotationXYZ(headRot);
	Vec3 headFwd = headQuat.GetColumn1();
	Vec3 headUp = headQuat.GetColumn2();
	Vec3 headRight = headQuat.GetColumn0();

	Vec3 fromHead = handLocation - headLocation;
	float fwd = fromHead.Dot(headFwd);
	float up = fromHead.Dot(headUp);
	float right = fromHead.Dot(headRight);

	if (fwd > .15f || up < -.15f)
	{
		return false;
	}

	if (handSide == 0)
		return right < 0;
	else
		return right > 0;
}

bool VRManager::IsHandNearHip(EVRHand hand)
{
	int handSide = GetHandSide(hand);
	Matrix44 headTransform = gXR->GetHmdTransform();
	Matrix34 handTransform = gXR->GetInput()->GetControllerTransform(handSide);

	// check for hip: at least some way down from head position and forward pointing mostly down
	float headHeight = headTransform.GetTranslation().z;
	float handHeight = handTransform.GetTranslation().z;
	return handHeight <= .7f * headHeight && Vec3(0, 0, -1).Dot(handTransform.GetColumn1()) > .7f;
}

bool VRManager::IsHandNearChest(EVRHand hand)
{
	int handSide = GetHandSide(hand);
	Matrix44 headTransform = gXR->GetHmdTransform();
	Matrix34 handTransform = gXR->GetInput()->GetControllerTransform(handSide);

	Ang3 headAngles = Ang3::GetAnglesXYZ((Matrix33)headTransform);
	headAngles.x = headAngles.y = 0;
	Matrix33 headRot(headAngles);

	// for chest, check that we are near the camera height, in front of the head and generally not too far away in 2D
	Vec3 handToHead = headTransform.GetTranslation() - handTransform.GetTranslation();
	handToHead.z = 0;
	float headHeight = headTransform.GetTranslation().z;
	float handHeight = handTransform.GetTranslation().z;
	return handHeight >= .75f * headHeight && handToHead.Dot(headRot.GetColumn(1)) < 0.f && fabsf(handTransform.GetColumn1().Dot(headRot.GetColumn0())) >= 0.7f && handToHead.GetLength2D() <= .2f;
}

const ray_hit* VRManager::GetPointAtPoint(float maxDist) const
{
	if (m_offHandRayHitAny)
	{
		if (maxDist > 0 && m_offHandRayHit.dist <= maxDist)
			return &m_offHandRayHit;
	}

	return nullptr;
}

bool VRManager::IsHandBehindBack(EVRHand hand)
{
	int handSide = GetHandSide(hand);
	Matrix44 headTransform = gXR->GetHmdTransform();
	Matrix34 handTransform = gXR->GetInput()->GetControllerTransform(handSide);

	Ang3 headAngles = Ang3::GetAnglesXYZ((Matrix33)headTransform);
	headAngles.x = headAngles.y = 0;
	Matrix33 headRot(headAngles);

	// for backwards, check that we are at least behind camera and that hand fwd is not pointing downwards
	Vec3 handToHead = headTransform.GetTranslation() - handTransform.GetTranslation();
	handToHead.z = 0;
	return handToHead.Dot(headRot.GetColumn(1)) > 0.f && handTransform.GetColumn1().Dot(Vec3(0, 0, -1)) <= 0.5f;
}

void VRManager::UpdateSmoothedPlayerHeight()
{
	//smooth out the view elevation
	CPlayer* player = GetLocalPlayer();
	if (!player)
		return;

	float playerHeight = player->GetEntity()->GetWorldPos().z;

	const SPlayerStats& stats = *static_cast<SPlayerStats*>(player->GetActorStats());
	if (stats.inAir < 0.1f && !stats.flyMode && !stats.spectatorMode)
	{
		if (m_smoothHeightType != 1)
		{
			m_smoothedHeight = playerHeight;
			m_smoothHeightType = 1;
		}

		Interpolate(m_smoothedHeight, playerHeight,15.0f,gEnv->pSystem->GetITimer()->GetFrameTime());
	}
	else
	{
		if (m_smoothHeightType == 1)
		{
			m_smoothedHeightOffset = m_smoothedHeight - playerHeight;
			m_smoothHeightType = 2;
		}

		Interpolate(m_smoothedHeightOffset,0.0f,15.0f,gEnv->pSystem->GetITimer()->GetFrameTime());
		m_smoothedHeight = playerHeight + m_smoothedHeightOffset;
	}
}

void VRManager::InitDevice(IDXGISwapChain* swapchain)
{
	m_hudTexture.Reset();
	m_eyeViews[0].Reset();
	m_eyeViews[1].Reset();
	m_eyeTextures[0].Reset();
	m_eyeTextures[1].Reset();

	CryLogAlways("Acquiring device...");
	swapchain->GetDevice(__uuidof(ID3D10Device1), (void**)m_device.ReleaseAndGetAddressOf());
	if (!m_device)
	{
		CryLogAlways("Failed to get game's D3D10.1 device!");
		return;
	}
	if (m_device->GetFeatureLevel() != D3D10_FEATURE_LEVEL_10_1)
	{
		CryLogAlways("Device only has feature level %i", m_device->GetFeatureLevel());
	}

	ComPtr<IDXGIDevice> dxgiDevice;
	m_device->QueryInterface(__uuidof(IDXGIDevice), (void**)dxgiDevice.GetAddressOf());
	if (dxgiDevice)
	{
		ComPtr<IDXGIAdapter> d3d10Adapter;
		dxgiDevice->GetAdapter(d3d10Adapter.GetAddressOf());
		DXGI_ADAPTER_DESC desc;
		d3d10Adapter->GetDesc(&desc);
		CryLogAlways("Game is rendering to device %ls", desc.Description);
	}

	if (gXR->IsUsingWinlatorXR())
	{
		// no D3D11 interop / OpenXR session: the stereo frame is composed straight into this device's
		// back buffer (see ComposeWinlatorXRFrame)
		gVRRenderUtils->Shutdown();
		gVRRenderUtils->Init(m_device.Get());
		EnsureWinlatorXRWindow();
		return;
	}

	CryLogAlways("Creating D3D11 device");
	LUID requiredAdapterLuid;
	D3D_FEATURE_LEVEL requiredLevel;
	gXR->GetD3D11Requirements(&requiredAdapterLuid, &requiredLevel);
	ComPtr<IDXGIFactory1> factory;
	HRESULT hr = CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)factory.GetAddressOf());
	if (hr != S_OK)
	{
		CryLogAlways("Failed to create DXGI factory: %i", hr);
		return;
	}
	ComPtr<IDXGIAdapter1> adapter;
	for (UINT idx = 0; factory->EnumAdapters1(idx, adapter.ReleaseAndGetAddressOf()) == S_OK; ++idx)
	{
		DXGI_ADAPTER_DESC1 desc;
		adapter->GetDesc1(&desc);
		if (desc.AdapterLuid.HighPart == requiredAdapterLuid.HighPart && desc.AdapterLuid.LowPart == requiredAdapterLuid.LowPart)
		{
			CryLogAlways("Found adapter for XR rendering: %ls", desc.Description);
			break;
		}
	}

	hr = D3D11CreateDevice(adapter.Get(), adapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE, nullptr,
		D3D11_CREATE_DEVICE_SINGLETHREADED, &requiredLevel, 1, D3D11_SDK_VERSION,
		m_device11.ReleaseAndGetAddressOf(), nullptr, m_context11.ReleaseAndGetAddressOf());
	if (hr != S_OK)
	{
		CryLogAlways("Failed to create D3D11 device: %i", hr);
	}

	gXR->CreateSession(m_device11.Get());

	gVRRenderUtils->Shutdown();
	gVRRenderUtils->Init(m_device.Get());
}

void VRManager::CreateEyeTexture(int eye)
{
	Vec2i size = GetRenderSize();
	CryLogAlways("Creating eye texture %i: %i x %i", eye, size.x, size.y);
	CreateSharedTexture(m_eyeTextures[eye], m_eyeTextures11[eye], size.x, size.y);

	D3D10_SHADER_RESOURCE_VIEW_DESC srvDesc;
	srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	srvDesc.ViewDimension = D3D10_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = 1;
	srvDesc.Texture2D.MostDetailedMip = 0;
	CHECK_D3D10(m_device->CreateShaderResourceView(m_eyeTextures[eye].Get(), &srvDesc, m_eyeViews[eye].ReleaseAndGetAddressOf()));
}

void VRManager::CreateHUDTexture()
{
	Vec2i size = GetRenderSize();
	CryLogAlways("Creating HUD texture: %i x %i", size.x, size.y);
	CreateSharedTexture(m_hudTexture, m_hudTexture11, size.x, size.y);
	m_hudView.Reset();
	if (m_hudTexture)
	{
		D3D10_SHADER_RESOURCE_VIEW_DESC srvDesc;
		srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		srvDesc.ViewDimension = D3D10_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MipLevels = 1;
		srvDesc.Texture2D.MostDetailedMip = 0;
		CHECK_D3D10(m_device->CreateShaderResourceView(m_hudTexture.Get(), &srvDesc, m_hudView.ReleaseAndGetAddressOf()));
	}
}

void VRManager::CreateSharedTexture(ComPtr<ID3D10Texture2D> &texture, ComPtr<ID3D11Texture2D> &texture11, int width, int height)
{
	bool shared = !gXR->IsUsingWinlatorXR();
	if (!m_device || (shared && !m_device11))
		return;

	D3D10_TEXTURE2D_DESC desc = {};
	desc.Width = width;
	desc.Height = height;
	desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	desc.SampleDesc.Count = 1;
	desc.ArraySize = 1;
	desc.MipLevels = 1;
	desc.Usage = D3D10_USAGE_DEFAULT;
	desc.BindFlags = D3D10_BIND_SHADER_RESOURCE;
	// under WinlatorXR the texture only ever lives on the game's D3D10 device (no D3D11 interop)
	desc.MiscFlags = shared ? D3D10_RESOURCE_MISC_SHARED_KEYEDMUTEX : 0;
	HRESULT hr = m_device->CreateTexture2D(&desc, nullptr, texture.ReleaseAndGetAddressOf());
	if (hr != S_OK)
	{
		CryLogAlways("D3D10 CreateTexture2D failed: %i", hr);
		return;
	}
	if (!shared)
	{
		texture11.Reset();
		return;
	}

	CryLogAlways("Sharing texture with D3D11...");
	ComPtr<IDXGIResource> dxgiRes;
	texture->QueryInterface(__uuidof(IDXGIResource), (void**)dxgiRes.GetAddressOf());
	HANDLE handle;
	dxgiRes->GetSharedHandle(&handle);
	hr = m_device11->OpenSharedResource(handle, __uuidof(ID3D11Texture2D), (void**)texture11.ReleaseAndGetAddressOf());
	if (hr != S_OK)
	{
		CryLogAlways("D3D11 OpenSharedResource failed: %i", hr);
	}
}

void VRManager::CopyBackbufferToTexture(ID3D10Texture2D *target)
{
	// acquire and copy the current swap chain buffer to the HUD texture
	ComPtr<ID3D10Texture2D> backbuffer;
	m_swapchain->GetBuffer(0, __uuidof(ID3D10Texture2D), (void**)backbuffer.GetAddressOf());
	if (!backbuffer)
	{
		CryLogAlways("Error: failed to acquire current swapchain buffer");
		return;
	}

	AcquireTextureSync(target, 0);

	D3D10_TEXTURE2D_DESC rtDesc;
	backbuffer->GetDesc(&rtDesc);
	if (rtDesc.SampleDesc.Count > 1)
	{
		m_device->ResolveSubresource(target, 0, backbuffer.Get(), 0, DXGI_FORMAT_R8G8B8A8_UNORM);
	}
	else
	{
		m_device->CopyResource(target, backbuffer.Get());
	}
}

void VRManager::AcquireTextureSync(ID3D10Texture2D* target, int key)
{
	if (target == nullptr) return;
	ComPtr<IDXGIKeyedMutex> mutex;
	target->QueryInterface(__uuidof(IDXGIKeyedMutex), (void**)mutex.GetAddressOf());
	if (!mutex) return;
	HRESULT hr = mutex->AcquireSync(key, 100);
	if (FAILED(hr))
	{
		CryLogAlways("Failed to sync D3D10 textures: %i", hr);
		++m_numFailedSyncs;
		if (m_numFailedSyncs > 200 && !m_shownSyncFailureMessage)
		{
			MessageBoxA(nullptr, "Cannot sync textures between the game and the VR runtime. If you are using MSI Afterburner / Rivatuner or similar overlays, disable them and restart the game.", "Render error", MB_OK);
			m_shownSyncFailureMessage = true;
		}
	}
}

void VRManager::AcquireTextureSync(ID3D11Texture2D* target, int key)
{
	if (target == nullptr) return;
	ComPtr<IDXGIKeyedMutex> mutex;
	target->QueryInterface(__uuidof(IDXGIKeyedMutex), (void**)mutex.GetAddressOf());
	if (!mutex) return;
	HRESULT hr = mutex->AcquireSync(key, 100);
	if (FAILED(hr))
	{
		CryLogAlways("Failed to sync D3D11 textures: %i", hr);
		++m_numFailedSyncs;
		if (m_numFailedSyncs > 200 && !m_shownSyncFailureMessage)
		{
			MessageBoxA(nullptr, "Cannot sync textures between the game and the VR runtime. If you are using MSI Afterburner / Rivatuner or similar overlays, disable them and restart the game.", "Render error", MB_OK);
			m_shownSyncFailureMessage = true;
		}
	}
}

void VRManager::ReleaseTextureSync(ID3D10Texture2D* target, int key)
{
	if (target == nullptr) return;
	ComPtr<IDXGIKeyedMutex> mutex;
	target->QueryInterface(__uuidof(IDXGIKeyedMutex), (void**)mutex.GetAddressOf());
	if (!mutex) return;
	CHECK_D3D10(mutex->ReleaseSync(key));
}

void VRManager::ReleaseTextureSync(ID3D11Texture2D* target, int key)
{
	if (target == nullptr) return;
	ComPtr<IDXGIKeyedMutex> mutex;
	target->QueryInterface(__uuidof(IDXGIKeyedMutex), (void**)mutex.GetAddressOf());
	if (!mutex) return;
	CHECK_D3D10(mutex->ReleaseSync(key));
}

void VRManager::UpdateOffHandRayQuery()
{
	CPlayer* player = GetLocalPlayer();
	if (!player)
		return;
	IPhysicalEntity* physEnt = player->GetEntity()->GetPhysics();

	static const int obj_types = ent_all;
	static const unsigned int flags = rwi_stop_at_pierceable|rwi_colltype_any;

	Matrix34 handTransform = gVR->GetWorldControllerTransform(GetHandSide(OFF_HAND));
	Vec3 worldPos = handTransform.GetTranslation();
	Vec3 dir = handTransform.GetColumn1();
	m_offHandRayHitAny = gEnv->pPhysicalWorld->RayWorldIntersection(worldPos, 300.f * dir, obj_types, flags, &m_offHandRayHit, 1, physEnt) != 0;
	if (m_offHandRayHitAny)
	{
		IEntity* pointAt = gEnv->pEntitySystem->GetEntityFromPhysics(m_offHandRayHit.pCollider);
		m_pointAtEntityId = pointAt ? pointAt->GetId() : 0;
	}
	else
	{
		m_pointAtEntityId = 0;
	}
}

// ---------------------------------------------------------------------------------------------------
// WinlatorXR backend
// ---------------------------------------------------------------------------------------------------

void VRManager::ComposeWinlatorXRFrame()
{
	if (!m_device || !m_swapchain)
		return;

	ComPtr<ID3D10Texture2D> backbuffer;
	m_swapchain->GetBuffer(0, __uuidof(ID3D10Texture2D), (void**)backbuffer.GetAddressOf());
	if (!backbuffer)
		return;
	D3D10_TEXTURE2D_DESC bbDesc;
	backbuffer->GetDesc(&bbDesc);
	int width = (int)bbDesc.Width;
	int height = (int)bbDesc.Height;
	if (width <= 0 || height <= 0)
		return;

	ComPtr<ID3D10RenderTargetView> rtv;
	if (FAILED(m_device->CreateRenderTargetView(backbuffer.Get(), nullptr, rtv.ReleaseAndGetAddressOf())) || !rtv)
		return;

	// Decide what this frame is. The back buffer currently holds whatever the engine rendered last: in
	// VR mode that is the HUD over a transparent clear (the eyes live in m_eyeTextures), in the 2D modes
	// (binoculars, scopes, 2D cinema, menus, loading screens) it is the flat game image plus HUD.
	CFlashMenuObject* menu = g_pGame ? g_pGame->GetMenu() : nullptr;
	bool inMenu = menu && (menu->IsMenuActive() || menu->IsLoadingScreenActive());
	VRRenderMode renderMode = gVRRenderer->GetRenderMode();
	bool haveEyes = m_eyeViews[0].Get() != nullptr && m_eyeViews[1].Get() != nullptr;
	bool aer = gXR->UseWinlatorAER();
	int aerEye = gXR->CurrentAerEye();
	bool vrWorld = !inMenu && renderMode == RM_VR && gXR->ArePosesValid() && (aer ? m_eyeViews[aerEye].Get() != nullptr : haveEyes);
	bool stereoPlane = !inMenu && renderMode == RM_3D && haveEyes;

	D3D10StateGuard stateGuard(m_device.Get());
	ID3D10RenderTargetView* rtvs[1] = { rtv.Get() };
	m_device->OMSetRenderTargets(1, rtvs, nullptr);

	VRRect full(0, 0, width, height);
	int syncId = gXR->GetWinlatorFrameSyncId();

	if (vrWorld && aer)
	{
		// Alternate-eye: the whole frame is the eye rendered this frame, at full resolution. The
		// frame-sync marker's blue channel tells WinlatorXR which of its two eye framebuffers to
		// update (B > 0 = right), R selects the pose the frame was rendered with.
		gVRRenderUtils->DrawTextureRect(m_eyeViews[aerEye].Get(), full, nullptr, VRRenderUtils::RB_OPAQUE);
		DrawWinlatorHud(aerEye, full);
		gVRRenderUtils->FillRect(VRRect(0, 0, 8, 8), ColorF(syncId / 255.f, 0.f, aerEye == 1 ? 1.f : 0.f, 1.f), false);
		gXR->SetWinlatorFrameMode(1, 2);
	}
	else if (vrWorld || stereoPlane)
	{
		// Side-by-side: left eye in the left half, right eye in the right half. WinlatorXR stretches
		// each half over the FOV we render with, so squeezing the eye images into the halves is
		// exactly undone on the headset. (The 3D cinema plane uses the same layout on the virtual screen.)
		int halfWidth = width / 2;
		for (int eye = 0; eye < 2; ++eye)
		{
			VRRect region(eye * halfWidth, 0, halfWidth, height);
			gVRRenderUtils->DrawTextureRect(m_eyeViews[eye].Get(), region, nullptr, VRRenderUtils::RB_OPAQUE);
			DrawWinlatorHud(eye, region);
		}
		if (vrWorld)
		{
			// Frame-sync marker: WinlatorXR samples screen pixel (0,0) and, if G == 0 and A > 0, uses R as
			// the index of the head pose our frame was rendered with. A small block (rather than a single
			// pixel) survives the scaling from back buffer to X screen.
			gVRRenderUtils->FillRect(VRRect(0, 0, 8, 8), ColorF(syncId / 255.f, 0.f, 0.f, 1.f), false);
			gXR->SetWinlatorFrameMode(1, 1);
		}
		else
		{
			gXR->SetWinlatorFrameMode(2, 1);
		}
	}
	else
	{
		// flat frame (menu, loading screen, binoculars, weapon scope, 2D cinema): let WinlatorXR show
		// the back buffer as-is on its virtual screen
		gXR->SetWinlatorFrameMode(2, 0);
	}

	// WinlatorXR composites our frame with source alpha, and the engine leaves the back buffer alpha at
	// whatever its transparent HUD clear produced - force it to fully opaque
	gVRRenderUtils->FillRect(full, ColorF(0.f, 0.f, 0.f, 1.f), true);
}

void VRManager::DrawWinlatorHud(int eye, const VRRect& region)
{
	if (!m_hudView || !gXR->IsHudVisible())
		return;

	// The HUD quad pose/size are maintained in gXR exactly like for the OpenXR quad layer (see
	// SetHudAttachedToHead etc.). Project the quad centre into head space and draw it as a
	// fronto-parallel rectangle at that distance - exact for the head-locked HUD, a good approximation
	// for the vehicle HUD.
	XrPosef hudPose = gXR->GetHudPose();
	Matrix34 hud = OpenXRToCrysis(hudPose.orientation, hudPose.position);
	Matrix34 head = Matrix34(gXR->GetHmdTransform());
	Matrix34 headInv = head.GetInvertedFast();
	Vec3 c = headInv.TransformPoint(hud.GetTranslation());
	float dist = c.y;
	if (dist < 0.05f)
		return;

	float tanl, tanr, tant, tanb;
	gXR->GetFov(eye, tanl, tanr, tant, tanb);
	float tanH = max(fabsf(tanl), fabsf(tanr));
	float tanV = max(fabsf(tant), fabsf(tanb));
	if (tanH <= 0.f || tanV <= 0.f)
		return;

	// everything below is in tangent space: the eye image spans [-tanH, +tanH] horizontally and
	// [+tanV, -tanV] vertically
	float halfW = 0.5f * gXR->GetHudWidth() / dist;
	float halfH = 0.5f * gXR->GetHudHeight() / dist;
	// stereo convergence: an object straight ahead is seen shifted towards the nose in each eye, i.e.
	// to the right in the left eye's image and to the left in the right eye's image
	float shift = (eye == 0 ? 1.f : -1.f) * 0.5f * gXR->GetWinlatorEyeSeparation() / dist;
	float cx = c.x / dist + shift;
	float cz = c.z / dist;

	float x0 = region.x + region.w * (0.5f + (cx - halfW) / (2.f * tanH));
	float x1 = region.x + region.w * (0.5f + (cx + halfW) / (2.f * tanH));
	float y0 = region.y + region.h * (0.5f - (cz + halfH) / (2.f * tanV));
	float y1 = region.y + region.h * (0.5f - (cz - halfH) / (2.f * tanV));

	VRRect dest((int)floorf(x0 + 0.5f), (int)floorf(y0 + 0.5f), (int)floorf(x1 - x0 + 0.5f), (int)floorf(y1 - y0 + 0.5f));
	// never bleed outside this eye's region (matters for side-by-side)
	gVRRenderUtils->DrawTextureRect(m_hudView.Get(), dest, &region, VRRenderUtils::RB_ALPHA);
}

void VRManager::EnsureWinlatorXRWindow()
{
	HWND hWnd = (HWND)m_winlatorWindow;
	if (!hWnd)
	{
		hWnd = gEnv->pRenderer ? (HWND)gEnv->pRenderer->GetHWND() : nullptr;
		if (!hWnd && m_swapchain)
		{
			DXGI_SWAP_CHAIN_DESC desc;
			if (SUCCEEDED(m_swapchain->GetDesc(&desc)))
				hWnd = desc.OutputWindow;
		}
		if (!hWnd)
		{
			if (m_winlatorWindowRetries++ == 0)
				CryLogAlways("[WinlatorXR] game window not known yet - will keep looking");
			return;
		}
		m_winlatorWindow = hWnd;
		CryLogAlways("[WinlatorXR] game window 0x%p", (void*)hWnd);
	}

	int width = GetSystemMetrics(SM_CXSCREEN);
	int height = GetSystemMetrics(SM_CYSCREEN);
	if (width <= 0 || height <= 0)
		return;

	// borderless popup: strip the title bar / frame / system menu so nothing offsets the client area
	LONG_PTR style = GetWindowLongPtrA(hWnd, GWL_STYLE);
	LONG_PTR wantedStyle = (style & ~(WS_CAPTION | WS_THICKFRAME | WS_BORDER | WS_DLGFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX)) | WS_POPUP | WS_VISIBLE;
	bool styleChanged = wantedStyle != style;
	if (styleChanged)
		SetWindowLongPtrA(hWnd, GWL_STYLE, wantedStyle);

	LONG_PTR exStyle = GetWindowLongPtrA(hWnd, GWL_EXSTYLE);
	LONG_PTR wantedEx = exStyle & ~(WS_EX_CLIENTEDGE | WS_EX_WINDOWEDGE | WS_EX_DLGMODALFRAME | WS_EX_STATICEDGE);
	if (wantedEx != exStyle)
	{
		SetWindowLongPtrA(hWnd, GWL_EXSTYLE, wantedEx);
		styleChanged = true;
	}

	RECT r;
	bool geometryWrong = !GetWindowRect(hWnd, &r) || r.left != 0 || r.top != 0
		|| (r.right - r.left) != width || (r.bottom - r.top) != height;
	if (styleChanged || geometryWrong)
	{
		// bypass the mod's SetWindowPos hook (it may be in "ignore window size changes" mode)
		SetWindowPos(hWnd, HWND_TOP, 0, 0, width, height, SWP_FRAMECHANGED | SWP_NOZORDER | SWP_NOACTIVATE);
		CryLogAlways("[WinlatorXR] forced game window borderless at (0,0) %dx%d (was %ld,%ld %ldx%ld style=0x%llx)",
			width, height, (long)r.left, (long)r.top, (long)(r.right - r.left), (long)(r.bottom - r.top), (unsigned long long)style);
	}
}

void VRManager::UpdateDesktopInputBlock()
{
	if (!gEnv->pInput)
		return;

	CFlashMenuObject* menu = g_pGame ? g_pGame->GetMenu() : nullptr;
	bool inMenu = (menu && (menu->IsMenuActive() || menu->IsLoadingScreenActive())) || gEnv->pConsole->IsOpened();
	float now = gEnv->pTimer->GetAsyncCurTime();
	if (inMenu)
	{
		if (m_menuEnterTime < 0.f)
			m_menuEnterTime = now;
	}
	else
	{
		m_menuEnterTime = -1.f;
	}

	bool blockAll = g_pGameCVars->vr_winlatorxr_block_desktop_input != 0 && !inMenu;
	// Briefly keep the keyboard blocked after entering a menu: the Esc that WinlatorXR emulates from the
	// same menu-button press we used to open the menu would otherwise close it again.
	bool menuGrace = inMenu && m_menuEnterTime >= 0.f && (now - m_menuEnterTime) < 0.5f;
	bool blockKeyboard = blockAll || menuGrace;
	// while the ImGui settings overlay owns the pointer, keep WinlatorXR's emulated click away from the
	// Flash menu underneath (ImGui gets the click straight from the Win32 button state)
	bool imguiWantsMouse = ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().WantCaptureMouse;
	bool blockMouse = blockAll || (inMenu && imguiWantsMouse);

	if (blockKeyboard != m_keyboardBlocked)
	{
		gEnv->pInput->EnableDevice(eDI_Keyboard, !blockKeyboard);
		m_keyboardBlocked = blockKeyboard;
	}
	if (blockMouse != m_mouseBlocked)
	{
		gEnv->pInput->EnableDevice(eDI_Mouse, !blockMouse);
		m_mouseBlocked = blockMouse;
	}
}
