#include "TutorialActionGate.h"

#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "ScenarioActorRegistry.h"
#include "TacticsDebug.h"
#include "XRU1Log.h"

int32 UTutorialActionGateSubsystem::ApplyPolicy(const FTutorialActionPolicy& NewPolicy)
{
	ActivePolicy = NewPolicy;
	bPolicyActive = true;
	// Пройденные точки принадлежат шагу: новый шаг открывает свой маршрут с нуля.
	ConsumedDestinations.Reset();
	// Токен монотонный: он же отличает «снять свою политику» от «затереть чужую».
	++ActivePolicyToken;

	if (TacticsDebug::IsGateLogEnabled())
	{
		FString Actions;
		for (const ETutorialAction Action : ActivePolicy.AllowedActions)
		{
			Actions += FString::Printf(TEXT("%d "), static_cast<int32>(Action));
		}
		UE_LOG(LogXRU1Quest, Display,
			TEXT("Gate #%d: lock=%d actions=[%s] units=%d targets=%d destinations=%d"),
			ActivePolicyToken, ActivePolicy.bLockGameplayInput ? 1 : 0, *Actions,
			ActivePolicy.AllowedUnitAnchors.Num(), ActivePolicy.AllowedTargetAnchors.Num(),
			ActivePolicy.AllowedDestinationAnchors.Num());
	}
	OnPolicyChanged.Broadcast();
	return ActivePolicyToken;
}

bool UTutorialActionGateSubsystem::ClearPolicy(int32 PolicyToken)
{
	if (!bPolicyActive || PolicyToken != ActivePolicyToken)
	{
		return false;
	}
	ActivePolicy = FTutorialActionPolicy();
	bPolicyActive = false;
	ConsumedDestinations.Reset();
	OnPolicyChanged.Broadcast();
	return true;
}

void UTutorialActionGateSubsystem::ClearAllPolicies()
{
	ActivePolicy = FTutorialActionPolicy();
	bPolicyActive = false;
	++ActivePolicyToken;
	ConsumedDestinations.Reset();
	OnPolicyChanged.Broadcast();
}

bool UTutorialActionGateSubsystem::RequiresExplicitUnitSelection() const
{
	return bPolicyActive &&
		ActivePolicy.AllowedUnitAnchors.Num() > 0 &&
		!ActivePolicy.bLockGameplayInput &&
		ActivePolicy.AllowedActions.Contains(ETutorialAction::Select);
}

bool UTutorialActionGateSubsystem::MatchesAnchorList(
	const TArray<FName>& Anchors, const AActor* Actor) const
{
	if (Anchors.Num() == 0)
	{
		return true;
	}
	if (!Actor)
	{
		return false;
	}
	return Anchors.Contains(UTacticalScenarioSubsystem::GetScenarioAnchorId(Actor));
}

bool UTutorialActionGateSubsystem::IsActionAllowed(ETutorialAction Action, const AActor* Unit) const
{
	if (!bPolicyActive)
	{
		return true;
	}

	// Камера и пауза не относятся к доменному действию: игрок обязан иметь
	// возможность рассмотреть сцену даже во время scripted-выстрела.
	if (Action == ETutorialAction::Camera)
	{
		return true;
	}
	if (ActivePolicy.bLockGameplayInput)
	{
		return false;
	}
	if (ActivePolicy.AllowedActions.Num() > 0 && !ActivePolicy.AllowedActions.Contains(Action))
	{
		return false;
	}
	// Конец хода — действие СТОРОНЫ, а не конкретного бойца. Если проверять его
	// по AllowedUnitAnchors, то шаг с ограничением по юниту заблокирует и ручное,
	// и автоматическое завершение хода — сценарный выстрел следующего шага тогда
	// никогда не состоится, и обучение встанет намертво.
	if (Action == ETutorialAction::EndTurn)
	{
		return true;
	}

	return MatchesAnchorList(ActivePolicy.AllowedUnitAnchors, Unit);
}

bool UTutorialActionGateSubsystem::IsTargetAllowed(const AActor* Target) const
{
	if (!bPolicyActive)
	{
		return true;
	}
	if (ActivePolicy.bLockGameplayInput)
	{
		return false;
	}
	return MatchesAnchorList(ActivePolicy.AllowedTargetAnchors, Target);
}

TArray<FName> UTutorialActionGateSubsystem::GetOpenDestinationAnchors() const
{
	TArray<FName> Open;
	if (!bPolicyActive)
	{
		return Open;
	}
	for (const FName& AnchorId : ActivePolicy.AllowedDestinationAnchors)
	{
		if (ConsumedDestinations.Contains(AnchorId))
		{
			continue;
		}
		Open.Add(AnchorId);
		// Последовательный маршрут: открыта ровно одна следующая точка.
		if (ActivePolicy.bSequentialDestinations)
		{
			break;
		}
	}
	return Open;
}

FName UTutorialActionGateSubsystem::FindNearestAllowedAnchor(
	const FVector& Location, float& OutDistance) const
{
	OutDistance = TNumericLimits<float>::Max();
	const UTacticalScenarioSubsystem* Registry = GetWorld()
		? GetWorld()->GetSubsystem<UTacticalScenarioSubsystem>() : nullptr;
	if (!Registry)
	{
		return NAME_None;
	}

	FName Best = NAME_None;
	for (const FName& AnchorId : ActivePolicy.AllowedDestinationAnchors)
	{
		for (const AActor* Anchor : Registry->FindScenarioActors(AnchorId))
		{
			if (!Anchor)
			{
				continue;
			}
			// 2D: якорь ставится на пол, а точка приказа берётся из трейса по
			// геометрии — разница по Z не должна отклонять корректный приказ.
			const float Distance = FVector::Dist2D(Anchor->GetActorLocation(), Location);
			if (Distance < OutDistance)
			{
				OutDistance = Distance;
				Best = AnchorId;
			}
		}
	}
	return Best;
}

bool UTutorialActionGateSubsystem::IsDestinationAllowed(const FVector& Destination) const
{
	if (!bPolicyActive || ActivePolicy.AllowedDestinationAnchors.Num() == 0)
	{
		return true;
	}

	const float Tolerance = FMath::Max(50.f, ActivePolicy.DestinationTolerance);
	const TArray<FName> Open = GetOpenDestinationAnchors();

	float Distance = 0.f;
	const FName Nearest = FindNearestAllowedAnchor(Destination, Distance);
	return Nearest != NAME_None && Distance <= Tolerance && Open.Contains(Nearest);
}

void UTutorialActionGateSubsystem::NotifyDestinationReached(const FVector& Location)
{
	if (!bPolicyActive || ActivePolicy.AllowedDestinationAnchors.Num() == 0)
	{
		return;
	}

	float Distance = 0.f;
	const FName Nearest = FindNearestAllowedAnchor(Location, Distance);
	if (Nearest == NAME_None ||
		Distance > FMath::Max(50.f, ActivePolicy.DestinationTolerance) ||
		ConsumedDestinations.Contains(Nearest))
	{
		return;
	}

	ConsumedDestinations.Add(Nearest);
	// Открылась следующая точка — HUD перерисовывает маркеры. Политику это не
	// меняет, поэтому OnPolicyChanged не трогаем: он снимает выбор бойца.
	OnDestinationsChanged.Broadcast();
}

UTutorialActionGateSubsystem* UTutorialActionGateSubsystem::Get(const UObject* WorldContextObject)
{
	const UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	return World ? World->GetSubsystem<UTutorialActionGateSubsystem>() : nullptr;
}

bool UTutorialActionGateSubsystem::AllowsAction(
	const UObject* WorldContextObject, ETutorialAction Action, const AActor* Unit)
{
	const UTutorialActionGateSubsystem* Gate = Get(WorldContextObject);
	return !Gate || Gate->IsActionAllowed(Action, Unit);
}
