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
	// Pulled in like a bar, the pad tips so that its near edge rises: "up" gains a part along
	// the axis that points at the player. (Checked on a DualSense: the opposite sign had the
	// bar letting out when it was pulled.) Measured as the angle of that edge above level, so
	// it reads the same however far the pad is rolled.
	return FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(Up.Z, -1.0f, 1.0f)));
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

float FMotionBarMapping::GetSheetFromStroke(float StrokeCm) const
{
	return FMath::Clamp(SheetAtNeutral + StrokeCm / FMath::Max(MoveSheetFullStrokeCm, 1.0f), 0.0f, 1.0f);
}

float FMotionBarMapping::GetStrokeMinCm() const
{
	return -SheetAtNeutral * FMath::Max(MoveSheetFullStrokeCm, 1.0f);
}

float FMotionBarMapping::GetStrokeMaxCm() const
{
	return (1.0f - SheetAtNeutral) * FMath::Max(MoveSheetFullStrokeCm, 1.0f);
}

void FMotionBarStroke::Reset()
{
	VelocityMS = 0.0f;
	DisplacementM = 0.0f;
	StillTimeSeconds = 0.0f;
	QuietTimeSeconds = 0.0f;
	RecentAlong.Reset();
}

void FMotionBarStroke::Update(const FKiteMotionSample& Sample, const FVector& Up, float DeltaTime)
{
	if (DeltaTime <= 0.0f)
	{
		return;
	}
	constexpr float StandardGravityMS2 = 9.80665f;
	const FVector UpDir = Up.GetSafeNormal();
	if (UpDir.IsNearlyZero())
	{
		return;
	}

	// What is left of the accelerometer's reading once gravity is off: the pad's own acceleration (g),
	// along the pull: down, and towards the player. "Towards" is the level part of the pad's own
	// towards-the-player and face axes, which works whether it is held flat or upright.
	const FVector Residual = Sample.AccelG - UpDir;
	const FVector Linear = Residual - BiasG;
	const FVector ControllerTowards(0.0f, 1.0f, 1.0f);
	const FVector Level = ControllerTowards - UpDir * FVector::DotProduct(ControllerTowards, UpDir);
	const FVector TowardsDir = Level.Size() > 0.2f ? Level.GetSafeNormal() : FVector::ZeroVector;
	float AlongG = -static_cast<float>(FVector::DotProduct(Linear, UpDir)) + TowardsWeight * static_cast<float>(FVector::DotProduct(Linear, TowardsDir));
	const bool bQuiet = FMath::Abs(AlongG) < DeadbandG;
	AlongG = bQuiet ? 0.0f : AlongG - FMath::Sign(AlongG) * DeadbandG;
	// Only a lasting quiet is a drift to kill: the acceleration passes through zero in the middle of
	// every stroke too.
	QuietTimeSeconds = bQuiet ? QuietTimeSeconds + DeltaTime : 0.0f;

	// The zero-velocity update: a pad that is not turning and feels only gravity is not moving. It
	// takes StillSeconds of quiet readings to call the pad still, and then only a reading well past
	// the thresholds (the start of a stroke) ends it, so a hand's tremor does not keep breaking it.
	const float AccelOffG = FMath::Abs(static_cast<float>(Sample.AccelG.Size()) - 1.0f);
	const float GyroRadS = static_cast<float>(Sample.GyroRadS.Size());
	const bool bWasStill = IsStill();
	if (bWasStill)
	{
		if (AccelOffG > StillBreakAccelG || GyroRadS > StillBreakGyroRadS)
		{
			StillTimeSeconds = 0.0f;
		}
	}
	else
	{
		const bool bQuietSensors = GyroRadS < StillGyroRadS && AccelOffG < StillAccelG;
		StillTimeSeconds = bQuietSensors ? StillTimeSeconds + DeltaTime : 0.0f;
	}

	if (IsStill())
	{
		// Still, the residual is the sensor's bias (and the filter's last error): learn it.
		// Kept briefly: if this turns out to be the start of a stroke, it counts, and is not bias.
		RecentAlong.Add({ AlongG, DeltaTime, BiasG });
		const float Blend = FMath::Clamp(DeltaTime / FMath::Max(BiasLearnSeconds, KINDA_SMALL_NUMBER), 0.0f, 1.0f);
		BiasG = FMath::Lerp(BiasG, Residual, Blend);
		VelocityMS = 0.0f;
		float Span = 0.0f;
		int32 Keep = 0;
		for (int32 Index = RecentAlong.Num() - 1; Index >= 0 && Span < StillLookbackSeconds; --Index)
		{
			Span += RecentAlong[Index].DeltaTime;
			++Keep;
		}
		if (Keep < RecentAlong.Num())
		{
			RecentAlong.RemoveAt(0, RecentAlong.Num() - Keep);
		}
	}
	else
	{
		if (bWasStill)
		{
			// A stroke has broken the stillness: the start of it, read while it was still too small to
			// break it, is taken back from the last readings that pushed the same way.
			for (int32 Index = RecentAlong.Num() - 1; Index >= 0; --Index)
			{
				const FRecentAlong& Earlier = RecentAlong[Index];
				if (Earlier.AlongG == 0.0f || FMath::Sign(Earlier.AlongG) != FMath::Sign(AlongG))
				{
					break;
				}
				VelocityMS += Earlier.AlongG * StandardGravityMS2 * StrokeGain * Earlier.DeltaTime;
				BiasG = Earlier.BiasBefore;
			}
		}
		RecentAlong.Reset();

		VelocityMS += AlongG * StandardGravityMS2 * StrokeGain * DeltaTime;
		const float LeakSeconds = QuietTimeSeconds >= QuietHoldSeconds ? QuietLeakSeconds : VelocityLeakSeconds;
		VelocityMS -= VelocityMS * FMath::Clamp(DeltaTime / FMath::Max(LeakSeconds, KINDA_SMALL_NUMBER), 0.0f, 1.0f);
	}

	DisplacementM += VelocityMS * DeltaTime;
	if (RelaxSeconds > 0.0f)
	{
		const float Pull = 1.0f - FMath::Exp(-DeltaTime / RelaxSeconds);
		DisplacementM += (RelaxTargetCm * 0.01f - DisplacementM) * Pull;
	}

	// The bar's stops: travel goes no more than StopSlackCm past one, so the movement back moves the
	// bar almost at once, and the little the leaks leave of a stroke's speed (which runs it back a
	// centimetre or two as the pad comes to rest) is taken up by that slack instead of the bar.
	const float SlackM = FMath::Max(StopSlackCm, 0.0f) * 0.01f;
	DisplacementM = FMath::Clamp(DisplacementM, GetMinM() - SlackM, GetMaxM() + SlackM);
}

float FMotionBarStroke::GetDisplacementCm() const
{
	return FMath::Clamp(DisplacementM, GetMinM(), GetMaxM()) * 100.0f;
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
