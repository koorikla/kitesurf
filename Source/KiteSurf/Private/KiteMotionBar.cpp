#include "KiteMotionBar.h"
#include "KiteSurf.h"

#if PLATFORM_LINUX
THIRD_PARTY_INCLUDES_START
#include <SDL3/SDL.h>
THIRD_PARTY_INCLUDES_END
#endif

namespace
{
	// "Up" for a pad lying face up, in the controller's axes (Y is up through its face).
	const FVector ControllerUp(0.0f, 1.0f, 0.0f);
}

void FMotionBarFilter::Reset()
{
	Up = ControllerUp;
	bInitialized = false;
}

void FMotionBarFilter::Update(const FKiteMotionSample& Sample, float DeltaTime)
{
	const float AccelSize = Sample.AccelG.Size();
	const bool bAccelUsable = AccelSize > KINDA_SMALL_NUMBER;
	if (!bInitialized)
	{
		if (!bAccelUsable)
		{
			return;
		}
		Up = Sample.AccelG / AccelSize;
		bInitialized = true;
		return;
	}

	// The pad turns under a fixed "up": seen from the pad, up turns the other way.
	Up += FVector::CrossProduct(Up, Sample.GyroRadS) * DeltaTime;
	Up = Up.GetSafeNormal();

	// Shaken or swung, the accelerometer reads more or less than 1 g and says little about down.
	if (bAccelUsable && FMath::Abs(AccelSize - 1.0f) <= AccelTrustBandG)
	{
		const float Blend = FMath::Clamp(DeltaTime / FMath::Max(AccelTimeConstantSeconds, KINDA_SMALL_NUMBER), 0.0f, 1.0f);
		Up = FMath::Lerp(Up, Sample.AccelG / AccelSize, Blend).GetSafeNormal();
	}
	if (Up.IsNearlyZero())
	{
		Up = ControllerUp;
	}
}

float FMotionBarFilter::GetRollDeg() const
{
	// Controller axes: X right, Y up through the face, Z towards the player. Rolled to the right,
	// the pad's right side points down, so "up" has a part along the pad's left.
	return FMath::RadiansToDegrees(FMath::Atan2(-Up.X, Up.Y));
}

float FMotionBarFilter::GetPitchDeg() const
{
	// Tipped with its top towards the player, the pad's near edge points down. Measured as the
	// angle of that edge below level, so it reads the same however far the pad is rolled.
	return FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(-Up.Z, -1.0f, 1.0f)));
}

void FMotionBarMapping::Calibrate(float RollDeg, float PitchDeg, float CurrentSheet)
{
	NeutralRollDeg = RollDeg;
	NeutralPitchDeg = PitchDeg;
	SheetAtNeutral = FMath::Clamp(CurrentSheet, 0.0f, 1.0f);
}

float FMotionBarMapping::GetSteer(float RollDeg) const
{
	const float Off = FMath::FindDeltaAngleDegrees(NeutralRollDeg, RollDeg);
	const float Beyond = FMath::Max(FMath::Abs(Off) - SteerDeadzoneDeg, 0.0f);
	const float Range = FMath::Max(SteerFullDeg - SteerDeadzoneDeg, 1.0f);
	return FMath::Clamp(Beyond / Range, 0.0f, 1.0f) * FMath::Sign(Off);
}

float FMotionBarMapping::GetSheet(float PitchDeg) const
{
	const float Off = FMath::FindDeltaAngleDegrees(NeutralPitchDeg, PitchDeg);
	return FMath::Clamp(SheetAtNeutral + Off / FMath::Max(SheetRangeDeg, 1.0f), 0.0f, 1.0f);
}

#if PLATFORM_LINUX
namespace
{
	/**
	 * Reads a gamepad's gyro and accelerometer through the SDL the engine already runs: the
	 * engine opens the pads and pumps their events, but does not pass motion data on.
	 */
	class FSDLMotionSource : public IKiteMotionSource
	{
	public:
		virtual bool Poll(FKiteMotionSample& OutSample) override
		{
			if (!Pad || !SDL_GamepadConnected(Pad))
			{
				Pad = nullptr;
				// Looking for a pad is not free: once a second is plenty.
				const double Now = FPlatformTime::Seconds();
				if (Now - LastSearchSeconds < 1.0)
				{
					return false;
				}
				LastSearchSeconds = Now;
				FindPad();
				if (!Pad)
				{
					return false;
				}
			}

			float Accel[3] = { 0.0f, 0.0f, 0.0f };
			float Gyro[3] = { 0.0f, 0.0f, 0.0f };
			if (!SDL_GetGamepadSensorData(Pad, SDL_SENSOR_ACCEL, Accel, 3) || !SDL_GetGamepadSensorData(Pad, SDL_SENSOR_GYRO, Gyro, 3))
			{
				return false;
			}
			OutSample.AccelG = FVector(Accel[0], Accel[1], Accel[2]) / SDL_STANDARD_GRAVITY;
			OutSample.GyroRadS = FVector(Gyro[0], Gyro[1], Gyro[2]);
			return true;
		}

		virtual FString GetDeviceName() const override
		{
			return DeviceName;
		}

	private:
		void FindPad()
		{
			DeviceName.Reset();
			if (!SDL_WasInit(SDL_INIT_GAMEPAD))
			{
				return;
			}
			int Count = 0;
			SDL_JoystickID* Ids = SDL_GetGamepads(&Count);
			for (int Index = 0; Ids && Index < Count && !Pad; ++Index)
			{
				// The engine has usually opened it already; opening again only adds a reference.
				SDL_Gamepad* Candidate = SDL_GetGamepadFromID(Ids[Index]);
				if (!Candidate)
				{
					Candidate = SDL_OpenGamepad(Ids[Index]);
				}
				if (Candidate && SDL_GamepadHasSensor(Candidate, SDL_SENSOR_ACCEL) && SDL_GamepadHasSensor(Candidate, SDL_SENSOR_GYRO)
					&& SDL_SetGamepadSensorEnabled(Candidate, SDL_SENSOR_ACCEL, true) && SDL_SetGamepadSensorEnabled(Candidate, SDL_SENSOR_GYRO, true))
				{
					Pad = Candidate;
					DeviceName = UTF8_TO_TCHAR(SDL_GetGamepadName(Candidate));
					UE_LOG(LogKiteSurf, Log, TEXT("Motion bar: reading the motion sensors of '%s'"), *DeviceName);
				}
			}
			if (Ids)
			{
				SDL_free(Ids);
			}
			if (!Pad && !bWarned)
			{
				bWarned = true;
				UE_LOG(LogKiteSurf, Log, TEXT("Motion bar: no controller with motion sensors found among %d gamepad(s)"), Count);
			}
		}

		SDL_Gamepad* Pad = nullptr;
		FString DeviceName;
		double LastSearchSeconds = -10.0;
		bool bWarned = false;
	};
}
#endif

TSharedPtr<IKiteMotionSource> KiteMotionBar::CreatePlatformSource()
{
#if PLATFORM_LINUX
	return MakeShared<FSDLMotionSource>();
#else
	return nullptr;
#endif
}
