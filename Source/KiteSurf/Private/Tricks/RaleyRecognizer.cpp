#include "Tricks/RaleyRecognizer.h"

void FRaleyRecognizer::Begin(float InSigma)
{
	bBegun = true;
	Sigma = InSigma >= 0.0f ? 1.0f : -1.0f;
	MaxTiltDeg = 0.0f;
	SwingFromKiteDeg = 180.0f;
	bArmsOutPastMinTilt = false;
	LineSpinRad = 0.0;
}

float FRaleyRecognizer::TiltDeg(const FQuat& Body)
{
	const double UpZ = FMath::Clamp(Body.GetNormalized().GetAxisZ().Z, -1.0, 1.0);
	return FMath::RadiansToDegrees(static_cast<float>(FMath::Acos(UpZ)));
}

float FRaleyRecognizer::LeanFromLineDeg(const FQuat& Body, const FVector& LineDirWorld)
{
	const FVector Up = Body.GetNormalized().GetAxisZ();
	const FVector Lean = FVector(Up.X, Up.Y, 0.0f).GetSafeNormal();
	const FVector Line = FVector(LineDirWorld.X, LineDirWorld.Y, 0.0f).GetSafeNormal();
	if (Lean.IsZero() || Line.IsZero())
	{
		return 180.0f;
	}
	return FMath::RadiansToDegrees(static_cast<float>(FMath::Acos(FMath::Clamp(Lean | Line, -1.0, 1.0))));
}

void FRaleyRecognizer::Step(const FQuat& Body, const FVector& OmegaW, const FVector& LineDirWorld, bool bRaleyArms, float Dt)
{
	if (!bBegun || Dt <= 0.0f || Body.ContainsNaN() || OmegaW.ContainsNaN())
	{
		return;
	}
	const float Tilt = TiltDeg(Body);
	if (Tilt > MaxTiltDeg)
	{
		if (MaxTiltDeg < Settings.RaleyMinTiltDeg && Tilt >= Settings.RaleyMinTiltDeg)
		{
			bArmsOutPastMinTilt = bRaleyArms;
		}
		MaxTiltDeg = Tilt;
		SwingFromKiteDeg = LeanFromLineDeg(Body, LineDirWorld);
	}
	const FVector Line = LineDirWorld.GetSafeNormal();
	if (bRaleyArms && !Line.IsZero())
	{
		LineSpinRad += (OmegaW | Line) * Dt;
	}
}

FRaleyResult FRaleyRecognizer::Get(bool bUnhooked, int32 InversionCount, int32 PassCount) const
{
	FRaleyResult Result;
	if (!bBegun)
	{
		return Result;
	}
	Result.MaxTiltDeg = MaxTiltDeg;
	Result.SwingFromKiteDeg = SwingFromKiteDeg;
	Result.bArmsOutPastMinTilt = bArmsOutPastMinTilt;
	// Backside turns as a back roll does: about -Sigma x the line (FRotationRecognizer's spin sense).
	Result.LineSpinDeg = -Sigma * FMath::RadiansToDegrees(static_cast<float>(LineSpinRad));
	if (!bUnhooked || MaxTiltDeg < Settings.RaleyMinTiltDeg)
	{
		return Result;
	}
	if (PassCount == 0 && FMath::Abs(Result.LineSpinDeg) >= Settings.SBendMinLineSpinDeg)
	{
		Result.bSBend = true;
		Result.SBendSense = Result.LineSpinDeg > 0.0f ? ETrickSense::Backside : ETrickSense::Frontside;
		return Result;
	}
	Result.bRaley = InversionCount == 0 && SwingFromKiteDeg <= Settings.SwingToKiteMaxDeg && bArmsOutPastMinTilt;
	return Result;
}
