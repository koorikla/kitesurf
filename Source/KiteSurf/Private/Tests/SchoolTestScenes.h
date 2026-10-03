#pragma once

#include "CoreMinimal.h"
#include "School/LessonEvaluator.h"
#include "School/LessonTelemetry.h"
#include "Tricks/JumpRecord.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

/**
 * Synthetic rides for the kite school tests: telemetry filled at 60 Hz from a function of time,
 * and a parametric jump with its record. Shared by SchoolLessonTests.cpp and SchoolCatalogTests.cpp.
 */
namespace SchoolTestScenes
{
	constexpr float Hz = 60.0f;

	/** Telemetry, the jump record and its extras: everything the evaluators read. */
	struct FSchoolScene
	{
		FLessonTelemetry Telemetry;
		FJumpRecord Jump;
		FLessonJumpExtras Extras;
	};

	/** Adds a sample every 1/60 s over [From, To], each from Make(time). */
	template <typename FMake>
	void Fill(FLessonTelemetry& Telemetry, float From, float To, FMake&& Make)
	{
		const int32 Steps = FMath::RoundToInt((To - From) * Hz);
		for (int32 I = 0; I <= Steps; ++I)
		{
			const float Time = From + static_cast<float>(I) / Hz;
			FLessonSample Sample = Make(Time);
			Sample.TimeSeconds = Time;
			Telemetry.Add(Sample);
		}
	}

	/** Planing at 8 m/s on tack +1, kite at 45 deg, nothing wrong. */
	inline FLessonSample Riding(float Time)
	{
		FLessonSample S;
		S.TimeSeconds = Time;
		S.BoardState = EBoardState::Planing;
		S.SpeedMS = 8.0f;
		S.HeadingDeg = 90.0f;
		S.Tack = 1;
		S.EdgeInput = 0.6f;
		S.KiteElevationDeg = 45.0f;
		S.KiteClockDeg = 45.0f;
		S.KiteDownwindM = 15.0f;
		S.KiteBearingDeg = 45.0f;
		S.TensionN = 800.0f;
		S.BarPosition = 0.5f;
		return S;
	}

	/** A jump from a ride, by its parameters. The defaults are a clean 1.5 m small jump (lesson B2). */
	struct FJumpScene
	{
		int32 Index = 0;
		float TakeoffT = 5.0f;
		float Airtime = 1.2f;
		float HeightM = 1.5f;
		float TakeoffSpeedMS = 8.0f;
		bool bPopped = false;
		ELandingGrade Grade = ELandingGrade::Clean;
		ELandingCause Cause = ELandingCause::None;
		float SinkMS = 2.0f;
		float LandingG = 2.0f;

		/** The kite climbs from KiteLowDeg to KiteTopDeg over 1.5 s from SendStartT. */
		float SendStartT = 3.0f;
		float KiteLowDeg = 45.0f;
		float KiteTopDeg = 85.0f;
		/** The landing dive starts this long before touchdown and ends at DiveToDeg at touchdown. */
		float DiveLeadS = 0.6f;
		float DiveToDeg = 60.0f;
		/** An extra rise of the kite from 0.4 s before take-off to 0.2 s after (deg). */
		float PopRiseDeg = 0.0f;

		/** Bar while the kite climbs during the send. */
		float BarClimbing = 0.2f;
		float EdgeBeforeTakeoff = 0.8f;
		/** Above 0: the edge falls to 0 this long before take-off. */
		float EdgeDropBeforeS = -1.0f;

		float ClockInAir = 0.0f;
		float ClockAtLanding = 0.0f;
		float KiteDownwindAtLanding = 5.0f;
		float HeadingAfter = 90.0f;

		/** At or above 0: the extras know the grab. */
		float GrabHoldS = -1.0f;
		/** Telemetry kept after touchdown (s). */
		float TrailS = 3.0f;
	};

	inline float LandingTime(const FJumpScene& P) { return P.TakeoffT + P.Airtime; }

	inline float KiteElevation(const FJumpScene& P, float T)
	{
		const float Landing = LandingTime(P);
		float E = P.KiteLowDeg;
		if (T >= P.SendStartT)
		{
			E = FMath::Lerp(P.KiteLowDeg, P.KiteTopDeg, FMath::Clamp((T - P.SendStartT) / 1.5f, 0.0f, 1.0f));
		}
		if (P.DiveLeadS > 0.0f && T >= Landing - P.DiveLeadS)
		{
			E = FMath::Lerp(P.KiteTopDeg, P.DiveToDeg, FMath::Clamp((T - (Landing - P.DiveLeadS)) / P.DiveLeadS, 0.0f, 1.0f));
		}
		if (P.PopRiseDeg > 0.0f)
		{
			E += P.PopRiseDeg * FMath::Clamp((T - (P.TakeoffT - 0.4f)) / 0.6f, 0.0f, 1.0f);
		}
		return E;
	}

	inline FSchoolScene BuildJump(const FJumpScene& P)
	{
		FSchoolScene Scene;
		const float Landing = LandingTime(P);
		Fill(Scene.Telemetry, 0.0f, Landing + P.TrailS, [&P, Landing](float T)
		{
			FLessonSample S = Riding(T);
			const bool bAir = T > P.TakeoffT && T < Landing;
			S.BoardState = bAir ? EBoardState::Airborne : EBoardState::Planing;
			S.SpeedMS = P.TakeoffSpeedMS;
			S.KiteElevationDeg = KiteElevation(P, T);
			const bool bClimbing = T >= P.SendStartT && T < P.SendStartT + 1.5f;
			S.BarPosition = bClimbing ? P.BarClimbing : (bAir ? 0.9f : 0.5f);
			if (T < P.TakeoffT)
			{
				const bool bDropped = P.EdgeDropBeforeS > 0.0f && T >= P.TakeoffT - P.EdgeDropBeforeS;
				S.EdgeInput = bDropped ? 0.0f : P.EdgeBeforeTakeoff;
				S.KiteClockDeg = 0.0f;
				S.HeadingDeg = 90.0f;
			}
			else if (bAir)
			{
				const float Air = (T - P.TakeoffT) / P.Airtime;
				S.EdgeInput = 0.0f;
				S.HeightM = 4.0f * P.HeightM * Air * (1.0f - Air);
				S.KiteClockDeg = T > Landing - 0.5f
					? FMath::Lerp(P.ClockInAir, P.ClockAtLanding, (T - (Landing - 0.5f)) / 0.5f)
					: P.ClockInAir;
				S.HeadingDeg = FMath::Lerp(90.0f, P.HeadingAfter, Air);
			}
			else
			{
				S.EdgeInput = 0.5f;
				S.KiteClockDeg = P.ClockAtLanding;
				S.HeadingDeg = P.HeadingAfter;
			}
			S.KiteDownwindM = FMath::Lerp(5.0f, P.KiteDownwindAtLanding, FMath::Clamp((T - (Landing - 0.5f)) / 0.5f, 0.0f, 1.0f));
			return S;
		});

		FJumpRecord& J = Scene.Jump;
		J.Index = P.Index;
		J.bPopped = P.bPopped;
		J.TakeoffTimeSeconds = P.TakeoffT;
		J.ApexTimeSeconds = P.TakeoffT + 0.5f * P.Airtime;
		J.LandingTimeSeconds = Landing;
		J.AirtimeSeconds = P.Airtime;
		J.ApexHeightCm = P.HeightM * 100.0f;
		J.TakeoffSpeedCmS = P.TakeoffSpeedMS * 100.0f;
		J.DistanceCm = P.TakeoffSpeedMS * P.Airtime * 100.0f;
		J.SinkRateCmS = P.SinkMS * 100.0f;
		J.LandingG = P.LandingG;
		J.KiteElevationAtLandingDeg = KiteElevation(P, Landing);
		J.MinKiteElevationDeg = FMath::Min(KiteElevation(P, P.TakeoffT), J.KiteElevationAtLandingDeg);
		J.Grade = P.Grade;
		J.Outcome = P.Grade == ELandingGrade::Crash ? EJumpOutcome::Crashed : EJumpOutcome::Landed;

		Scene.Extras.LandingCause = P.Cause;
		if (P.GrabHoldS >= 0.0f)
		{
			Scene.Extras.bHasGrab = true;
			Scene.Extras.GrabHoldSeconds = P.GrabHoldS;
		}
		return Scene;
	}

	/** A ride scene with no jump (the record's Index stays -1). */
	template <typename FMake>
	FSchoolScene BuildRide(float From, float To, FMake&& Make)
	{
		FSchoolScene Scene;
		Fill(Scene.Telemetry, From, To, Forward<FMake>(Make));
		return Scene;
	}

	/** An objective evaluated once from fresh progress. */
	inline FObjectiveResult EvaluateOnce(const FLessonObjective& Objective, const FSchoolScene& Scene)
	{
		const FLessonProgress Fresh;
		return LessonEval::EvaluateObjective(Objective, Fresh, Scene.Telemetry, &Scene.Jump, Scene.Extras);
	}

	/** One fault rule's own verdict on a scene. */
	inline bool RuleMatches(const FLessonFault& Fault, const FSchoolScene& Scene)
	{
		float Value = 0.0f;
		return LessonEval::ReadMeasure(Fault.Measure, Scene.Telemetry, &Scene.Jump, Scene.Extras, std::numeric_limits<float>::quiet_NaN(), Value)
			&& LessonEval::Compare(Fault.Compare, Value, Fault.Threshold);
	}
}

#endif
