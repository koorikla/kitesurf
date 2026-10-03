#include "Tricks/FreestyleScoring.h"
#include "Tricks/TrickNaming.h"

EGkaGroup FreestyleScoring::GroupOf(EGkaFamily Family)
{
	switch (Family)
	{
	case EGkaFamily::RaleyBased:
	case EGkaFamily::KgbSlim:
	case EGkaFamily::HinterHeart:
	case EGkaFamily::Mobes:
		return EGkaGroup::Heelside;
	case EGkaFamily::Rewinds:
	case EGkaFamily::ToesideBlind:
	case EGkaFamily::Combos:
	case EGkaFamily::InvertedDoubles:
	case EGkaFamily::KiteLoopPasses:
		return EGkaGroup::Variety;
	default:
		return EGkaGroup::None;
	}
}

float FreestyleScoring::FreestyleTrickScore(const FTrickSignature& Signature, const FLandingVerdict& Verdict, float ApexM,
	const FTrickScoringSettings& Settings)
{
	const float Execution = TrickScoring::ExecutionFactor(Verdict.Grade, Settings);
	if (Verdict.Grade == ELandingGrade::Crash || Execution <= 0.0f)
	{
		return 0.0f;
	}
	float Difficulty = 0.0f;
	TrickNaming::FreestyleFamily(Signature, nullptr, &Difficulty);
	const float HeightFactor = FMath::Clamp(0.8f + 0.1f * FMath::Max(ApexM, 0.0f), 0.8f, 1.2f);
	return FMath::Clamp(2.0f * Difficulty * Execution * HeightFactor, 0.1f, 10.0f);
}

FScoredTrick FreestyleScoring::MakeScoredTrick(const FTrickSignature& Signature, const FLandingVerdict& Verdict, float ApexM,
	const FTrickScoringSettings& Settings)
{
	FScoredTrick Trick;
	TrickNaming::FreestyleFamily(Signature, &Trick.Family, nullptr);
	Trick.Score = FreestyleTrickScore(Signature, Verdict, ApexM, Settings);
	Trick.Name = TrickNaming::Name(Signature);
	return Trick;
}

FHeatResult FreestyleScoring::ScoreFreestyleHeat(TConstArrayView<FScoredTrick> Tricks, const FFreestyleHeatRules& Rules)
{
	FHeatResult Result;

	// 1. The best trick in each family.
	const int32 Considered = Rules.Attempts > 0 ? FMath::Min(Tricks.Num(), Rules.Attempts) : Tricks.Num();
	constexpr int32 FamilyCount = static_cast<int32>(EGkaFamily::None);
	int32 BestByFamily[FamilyCount];
	for (int32& Best : BestByFamily)
	{
		Best = INDEX_NONE;
	}
	for (int32 Index = 0; Index < Considered; ++Index)
	{
		const FScoredTrick& Trick = Tricks[Index];
		const int32 Family = static_cast<int32>(Trick.Family);
		if (Family < 0 || Family >= FamilyCount || GroupOf(Trick.Family) == EGkaGroup::None || !(Trick.Score > 0.0f))
		{
			continue;
		}
		if (BestByFamily[Family] == INDEX_NONE || Trick.Score > Tricks[BestByFamily[Family]].Score)
		{
			BestByFamily[Family] = Index;
		}
	}

	// 2. Sort each group, best first (ties: the earlier trick).
	TArray<int32> Heelside;
	TArray<int32> Variety;
	for (int32 Family = 0; Family < FamilyCount; ++Family)
	{
		if (BestByFamily[Family] != INDEX_NONE)
		{
			(GroupOf(static_cast<EGkaFamily>(Family)) == EGkaGroup::Heelside ? Heelside : Variety).Add(BestByFamily[Family]);
		}
	}
	const auto ByScore = [&Tricks](int32 A, int32 B)
	{
		return Tricks[A].Score != Tricks[B].Score ? Tricks[A].Score > Tricks[B].Score : A < B;
	};
	Heelside.Sort(ByScore);
	Variety.Sort(ByScore);

	// 3. The best split between the groups within the limits.
	const int32 Counting = FMath::Max(Rules.Counting, 0);
	float BestSum = -1.0f;
	int32 BestH = 0;
	int32 BestV = 0;
	const int32 MaxH = FMath::Min3(FMath::Max(Rules.MaxHeelside, 0), Heelside.Num(), Counting);
	for (int32 H = 0; H <= MaxH; ++H)
	{
		const int32 V = FMath::Min3(Counting - H, FMath::Max(Rules.MaxVariety, 0), Variety.Num());
		float Sum = 0.0f;
		for (int32 I = 0; I < H; ++I)
		{
			Sum += Tricks[Heelside[I]].Score;
		}
		for (int32 I = 0; I < V; ++I)
		{
			Sum += Tricks[Variety[I]].Score;
		}
		if (Sum > BestSum)
		{
			BestSum = Sum;
			BestH = H;
			BestV = V;
		}
	}

	for (int32 I = 0; I < BestH; ++I)
	{
		Result.CountingIdx.Add(Heelside[I]);
	}
	for (int32 I = 0; I < BestV; ++I)
	{
		Result.CountingIdx.Add(Variety[I]);
	}
	Result.CountingIdx.Sort(ByScore);
	Result.HeelsideCounted = BestH;
	Result.VarietyCounted = BestV;
	Result.TrickTotal = FMath::Max(BestSum, 0.0f);

	// 4. The variety bonus for the families counted (one trick per family, so one per counting trick).
	const int32 Families = Result.CountingIdx.Num();
	if (Rules.VarietyBonus.Num() > 0)
	{
		Result.VarietyBonus = Rules.VarietyBonus[FMath::Clamp(Families, 0, Rules.VarietyBonus.Num() - 1)];
	}
	Result.Total = Result.TrickTotal + Result.VarietyBonus;
	return Result;
}
