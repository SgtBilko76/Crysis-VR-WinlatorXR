#pragma once
#include "EVRHand.h"

class VRHaptics
{
public:
	void Init();
	void Shutdown();

	// bHaptics vest / ProTubeVR are PC peripherals; under WinlatorXR (standalone headset) their SDKs
	// are never initialised and every entry point below becomes a no-op.
	bool AreExternalHapticsReady() const { return m_externalHapticsReady; }

	void RegisterBHapticsEffect(const char* key, const char* file);

	void TriggerBHapticsEffect(const char* key, float intensity = 1.0f, float offsetAngleX = 0, float offsetY = 0);
	void TriggerBHapticsEffect(const char* key, float intensity, const Vec3& pos, const Vec3& dir);
	void TriggerBHapticsEffectForSide(EVRHand hand, const char* keyLeft, const char* keyRight, float intensity = 1.0f);
	bool IsBHapticsEffectPlaying(const char* key) const;
	void StopBHapticsEffect(const char* key);
	void TriggerProtubeEffect(float kickPower, float rumblePower, float rumbleSeconds, bool offHand = false);
	void TriggerProtubeEffectWeapon(float kickPower, float rumblePower, float rumbleSeconds);

private:
	bool m_externalHapticsReady = false;

	void InitEffects();
	void ProtubeKick(float power, bool offHand = false);
	void ProtubeRumble(float power, float seconds, bool offHand = false);
	void ProtubeShot(float kickPower, float rumblePower, float rumbleSeconds, bool offHand = false);
};

extern VRHaptics* gHaptics;
