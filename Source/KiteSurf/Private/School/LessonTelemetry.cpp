#include "School/LessonTelemetry.h"

float FLessonSample::Get(ELessonChannel Channel) const
{
	switch (Channel)
	{
	case ELessonChannel::Speed:            return SpeedMS;
	case ELessonChannel::Heading:          return HeadingDeg;
	case ELessonChannel::Tack:             return static_cast<float>(Tack);
	case ELessonChannel::Edge:             return EdgeInput;
	case ELessonChannel::EdgeAbs:          return FMath::Abs(EdgeInput);
	case ELessonChannel::Height:           return HeightM;
	case ELessonChannel::VerticalSpeed:    return VerticalSpeedMS;
	case ELessonChannel::KiteClock:        return KiteClockDeg;
	case ELessonChannel::KiteClockAbs:     return FMath::Abs(KiteClockDeg);
	case ELessonChannel::KiteElevation:    return KiteElevationDeg;
	case ELessonChannel::KiteDownwind:     return KiteDownwindM;
	case ELessonChannel::KiteBearing:      return KiteBearingDeg;
	case ELessonChannel::Tension:          return TensionN;
	case ELessonChannel::Bar:              return BarPosition;
	case ELessonChannel::Steer:            return SteerInput;
	case ELessonChannel::Planing:          return BoardState == EBoardState::Planing ? 1.0f : 0.0f;
	case ELessonChannel::Airborne:         return BoardState == EBoardState::Airborne ? 1.0f : 0.0f;
	case ELessonChannel::Fallen:           return bFallen ? 1.0f : 0.0f;
	case ELessonChannel::Toeside:          return bToeside ? 1.0f : 0.0f;
	case ELessonChannel::CompletedLoops:   return static_cast<float>(CompletedLoops);
	case ELessonChannel::Upwind:           return UpwindM;
	case ELessonChannel::KiteClimbRate:    // needs the previous sample
	case ELessonChannel::BarWhileClimbing: // needs the previous sample
	default:                               return 0.0f;
	}
}

FLessonTelemetry::FLessonTelemetry(int32 InCapacity)
{
	Samples.SetNum(FMath::Max(InCapacity, 2));
}

bool FLessonTelemetry::Add(const FLessonSample& Sample)
{
	if (Count > 0 && !(Sample.TimeSeconds > Latest().TimeSeconds))
	{
		return false;
	}
	if (Count < Samples.Num())
	{
		Samples[Physical(Count)] = Sample;
		++Count;
	}
	else
	{
		Samples[Head] = Sample;
		Head = (Head + 1) % Samples.Num();
	}
	return true;
}

void FLessonTelemetry::Reset()
{
	Head = 0;
	Count = 0;
}

const FLessonSample& FLessonTelemetry::Get(int32 Index) const
{
	check(Index >= 0 && Index < Count);
	return Samples[Physical(Index)];
}

float FLessonTelemetry::ChannelAt(int32 Index, ELessonChannel Channel) const
{
	const FLessonSample& S = Get(Index);
	if (Channel != ELessonChannel::KiteClimbRate && Channel != ELessonChannel::BarWhileClimbing)
	{
		return S.Get(Channel);
	}

	float ClimbRate = 0.0f;
	if (Index > 0)
	{
		const FLessonSample& P = Get(Index - 1);
		const float Dt = S.TimeSeconds - P.TimeSeconds;
		ClimbRate = Dt > UE_SMALL_NUMBER ? (S.KiteElevationDeg - P.KiteElevationDeg) / Dt : 0.0f;
	}
	if (Channel == ELessonChannel::KiteClimbRate)
	{
		return ClimbRate;
	}
	const bool bClimbing = ClimbRate > LessonTelemetry::ClimbRateThresholdDegS && S.KiteElevationDeg < LessonTelemetry::ClimbCeilingDeg;
	return bClimbing ? S.BarPosition : 0.0f;
}

int32 FLessonTelemetry::FindIndexAtOrBefore(float TimeSeconds) const
{
	if (Count == 0 || TimeSeconds < Get(0).TimeSeconds)
	{
		return INDEX_NONE;
	}
	// Binary search for the last sample with time <= TimeSeconds.
	int32 Lo = 0;
	int32 Hi = Count - 1;
	while (Lo < Hi)
	{
		const int32 Mid = (Lo + Hi + 1) / 2;
		if (Get(Mid).TimeSeconds <= TimeSeconds)
		{
			Lo = Mid;
		}
		else
		{
			Hi = Mid - 1;
		}
	}
	return Lo;
}

bool FLessonTelemetry::ValueAt(ELessonChannel Channel, float TimeSeconds, float& OutValue) const
{
	if (Count == 0 || !FMath::IsFinite(TimeSeconds) || TimeSeconds > LatestTime())
	{
		return false;
	}
	const int32 I = FindIndexAtOrBefore(TimeSeconds);
	if (I == INDEX_NONE)
	{
		return false;
	}
	const float A = ChannelAt(I, Channel);
	if (I == Count - 1)
	{
		OutValue = A;
		return true;
	}
	const float T0 = Get(I).TimeSeconds;
	const float T1 = Get(I + 1).TimeSeconds;
	const float Alpha = FMath::Clamp((TimeSeconds - T0) / FMath::Max(T1 - T0, UE_SMALL_NUMBER), 0.0f, 1.0f);
	const float B = ChannelAt(I + 1, Channel);
	if (Channel == ELessonChannel::Heading)
	{
		OutValue = A + FMath::FindDeltaAngleDegrees(A, B) * Alpha;
	}
	else
	{
		OutValue = FMath::Lerp(A, B, Alpha);
	}
	return true;
}

FLessonWindowStats FLessonTelemetry::Window(ELessonChannel Channel, float FromSeconds, float ToSeconds) const
{
	FLessonWindowStats Stats;
	if (Count == 0 || !(ToSeconds >= FromSeconds))
	{
		return Stats;
	}
	double Sum = 0.0;
	auto Accumulate = [&Stats, &Sum](float Value)
	{
		if (Stats.Count == 0)
		{
			Stats.Min = Value;
			Stats.Max = Value;
		}
		else
		{
			Stats.Min = FMath::Min(Stats.Min, Value);
			Stats.Max = FMath::Max(Stats.Max, Value);
		}
		Sum += Value;
		++Stats.Count;
	};

	float Value = 0.0f;
	if (ValueAt(Channel, FromSeconds, Value))
	{
		Accumulate(Value);
	}
	const int32 First = FMath::Max(FindIndexAtOrBefore(FromSeconds) + 1, 0);
	for (int32 I = First; I < Count && Get(I).TimeSeconds < ToSeconds; ++I)
	{
		if (Get(I).TimeSeconds > FromSeconds)
		{
			Accumulate(ChannelAt(I, Channel));
		}
	}
	if (ToSeconds > FromSeconds && ValueAt(Channel, ToSeconds, Value))
	{
		Accumulate(Value);
	}
	if (Stats.Count > 0)
	{
		Stats.Mean = static_cast<float>(Sum / Stats.Count);
	}
	return Stats;
}

float FLessonTelemetry::TimeInBand(ELessonChannel Channel, float Min, float Max, float FromSeconds, float ToSeconds) const
{
	float Total = 0.0f;
	for (int32 I = 0; I + 1 < Count; ++I)
	{
		const float T0 = FMath::Max(Get(I).TimeSeconds, FromSeconds);
		const float T1 = FMath::Min(Get(I + 1).TimeSeconds, ToSeconds);
		if (T1 <= T0)
		{
			continue;
		}
		const float V = ChannelAt(I, Channel);
		if (V >= Min && V <= Max)
		{
			Total += T1 - T0;
		}
	}
	return Total;
}

float FLessonTelemetry::TrailingTimeWhere(TFunctionRef<bool(int32 Index)> Test, float NotBeforeSeconds) const
{
	if (Count == 0)
	{
		return 0.0f;
	}
	int32 First = INDEX_NONE;
	for (int32 I = Count - 1; I >= 0; --I)
	{
		if (Get(I).TimeSeconds < NotBeforeSeconds || !Test(I))
		{
			break;
		}
		First = I;
	}
	return First == INDEX_NONE ? 0.0f : LatestTime() - Get(First).TimeSeconds;
}

float FLessonTelemetry::TrailingTimeInBand(ELessonChannel Channel, float Min, float Max, float NotBeforeSeconds) const
{
	return TrailingTimeWhere([this, Channel, Min, Max](int32 I)
	{
		const float V = ChannelAt(I, Channel);
		return V >= Min && V <= Max;
	}, NotBeforeSeconds);
}

TArray<float> FLessonTelemetry::FindTackChanges() const
{
	TArray<float> Changes;
	int32 LastTack = 0;
	for (int32 I = 0; I < Count; ++I)
	{
		const int32 Tack = Get(I).Tack;
		if (Tack == 0)
		{
			continue;
		}
		if (LastTack != 0 && Tack != LastTack)
		{
			Changes.Add(Get(I).TimeSeconds);
		}
		LastTack = Tack;
	}
	return Changes;
}
