#include "TacticalAIDirectorSubsystem.h"

#include "Engine/World.h"
#include "ScenarioActorRegistry.h"
#include "TacticsCombatStatics.h"
#include "TacticsDebug.h"
#include "TurnManagerSubsystem.h"
#include "UnitAIController.h"
#include "UnitBase.h"
#include "XRU1Log.h"

void UTacticalAIDirectorSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Pods.Reset();
}

int32 UTacticalAIDirectorSubsystem::GetCurrentTurn() const
{
	const UWorld* World = GetWorld();
	const UTurnManagerSubsystem* TurnManager = World
		? World->GetSubsystem<UTurnManagerSubsystem>() : nullptr;
	return TurnManager ? TurnManager->GetTurnNumber() : 0;
}

FName UTacticalAIDirectorSubsystem::ResolvePodId(const AUnitBase* Unit)
{
	if (!Unit)
	{
		return NAME_None;
	}
	// Боец без явной группы образует собственный под из одного себя. Так
	// поведение остаётся осмысленным на картах, где группы ещё не расставлены.
	return Unit->PodId.IsNone() ? Unit->GetFName() : Unit->PodId;
}

void UTacticalAIDirectorSubsystem::RegisterUnit(AUnitBase* Unit)
{
	// ⚠️ БЕЗ фильтра по команде. Регистрация идёт из OnPossess, а TeamId к этому
	// моменту ещё может быть не назначен — фильтр здесь оставлял поды ПУСТЫМИ,
	// и активация поднимала «бойцов 0». Сторону проверяют Notify*-триггеры, где
	// команда уже гарантированно известна.
	if (!Unit)
	{
		return;
	}
	FAIPodState& Pod = Pods.FindOrAdd(ResolvePodId(Unit));
	Pod.Members.AddUnique(Unit);
}

void UTacticalAIDirectorSubsystem::UnregisterUnit(AUnitBase* Unit)
{
	if (!Unit)
	{
		return;
	}
	if (FAIPodState* Pod = Pods.Find(ResolvePodId(Unit)))
	{
		Pod->Members.Remove(Unit);
	}
}

bool UTacticalAIDirectorSubsystem::AddContact(FName PodId, AActor* Target,
	const FVector& Location, EAIContactSource Source, float Confidence)
{
	if (PodId.IsNone() || !Target)
	{
		return false;
	}

	FAIPodState& Pod = Pods.FindOrAdd(PodId);
	const int32 Turn = GetCurrentTurn();

	for (FAIContact& Existing : Pod.Contacts)
	{
		if (Existing.Target.Get() == Target)
		{
			// Более достоверный источник не должен быть перебит менее достоверным
			// в том же ходу: увиденный своими глазами контакт важнее слуха.
			if (Confidence >= Existing.Confidence || Existing.LastUpdatedTurn < Turn)
			{
				Existing.LastKnownLocation = Location;
				Existing.Source = Source;
				Existing.Confidence = FMath::Max(Existing.Confidence, Confidence);
			}
			Existing.LastUpdatedTurn = Turn;
			return false;
		}
	}

	FAIContact Contact;
	Contact.Target = Target;
	Contact.LastKnownLocation = Location;
	Contact.LastUpdatedTurn = Turn;
	Contact.Source = Source;
	Contact.Confidence = Confidence;
	Pod.Contacts.Add(Contact);
	return true;
}

bool UTacticalAIDirectorSubsystem::ActivatePod(FName PodId, const TCHAR* Reason)
{
	FAIPodState* Pod = Pods.Find(PodId);
	if (!Pod || Pod->bActivated)
	{
		return false;
	}

	Pod->bActivated = true;
	Pod->ActivatedTurn = GetCurrentTurn();

	int32 Awakened = 0;
	for (const TWeakObjectPtr<AUnitBase>& Member : Pod->Members)
	{
		AUnitBase* Unit = Member.Get();
		if (!Unit || !UTacticsCombatStatics::IsUnitAlive(Unit))
		{
			continue;
		}
		// Сторона проверяется здесь: к моменту активации команда уже назначена.
		if (Unit->GetGenericTeamId().GetId() != TacticsTeamIds::Enemy)
		{
			continue;
		}
		// Staged-голограмма следующей секции обучения физически стоит на карте,
		// но в бой не введена. Поднимать её по чужому контакту нельзя: она начнёт
		// стрелять в секции, которой ещё не наступила.
		if (!UTacticalScenarioSubsystem::IsActorScenarioActive(Unit))
		{
			continue;
		}
		if (AUnitAIController* AI = Cast<AUnitAIController>(Unit->GetController()))
		{
			AI->NotifyPodActivated();
			++Awakened;
		}
	}

	// Это событие уровня «бой начался», поэтому оно печатается без cvar: если
	// под не активировался, разбирать проще всего именно по его отсутствию.
	UE_LOG(LogXRU1AI, Log, TEXT("[AI] Под %s вскрыт (%s): поднято бойцов %d"),
		*PodId.ToString(), Reason, Awakened);
	return true;
}

void UTacticalAIDirectorSubsystem::NotifyEnemySpotted(AUnitBase* Spotter, AActor* Target)
{
	if (!Spotter || !Target || !UTacticsCombatStatics::IsUnitAlive(Target))
	{
		return;
	}

	const FName PodId = ResolvePodId(Spotter);
	AddContact(PodId, Target, Target->GetActorLocation(), EAIContactSource::Sight, 1.f);
	ActivatePod(PodId, TEXT("визуальный контакт"));
}

void UTacticalAIDirectorSubsystem::NotifyUnitDamaged(AUnitBase* Victim, AActor* Instigator)
{
	// Только вражеская сторона: под игрока не существует (см. RegisterUnit).
	if (!Victim || Victim->GetGenericTeamId().GetId() != TacticsTeamIds::Enemy)
	{
		return;
	}

	const FName PodId = ResolvePodId(Victim);
	if (Instigator)
	{
		// Стрелка может быть не видно, но направление известно точно: попадание —
		// это подтверждённый факт присутствия противника в той точке.
		AddContact(PodId, Instigator, Instigator->GetActorLocation(),
			EAIContactSource::Damage, 1.f);
	}
	ActivatePod(PodId, TEXT("получен урон"));
}

void UTacticalAIDirectorSubsystem::NotifyUnitKilled(AUnitBase* Victim, AActor* Instigator)
{
	// Только вражеская сторона: под игрока не существует (см. RegisterUnit).
	if (!Victim || Victim->GetGenericTeamId().GetId() != TacticsTeamIds::Enemy)
	{
		return;
	}

	const FName VictimPod = ResolvePodId(Victim);
	if (Instigator)
	{
		AddContact(VictimPod, Instigator, Instigator->GetActorLocation(),
			EAIContactSource::Damage, 1.f);
	}
	ActivatePod(VictimPod, TEXT("боец пода погиб"));

	// Соседние поды видят падающего союзника и поднимаются. Радиус, а не вся
	// карта: иначе первый же выстрел вскрывал бы всех врагов уровня разом.
	const FVector DeathLocation = Victim->GetActorLocation();
	const float RadiusSq = FMath::Square(FMath::Max(0.f, DeathAlertRadius));
	for (TPair<FName, FAIPodState>& Pair : Pods)
	{
		if (Pair.Key == VictimPod || Pair.Value.bActivated)
		{
			continue;
		}
		for (const TWeakObjectPtr<AUnitBase>& Member : Pair.Value.Members)
		{
			const AUnitBase* Unit = Member.Get();
			if (!Unit || FVector::DistSquared(Unit->GetActorLocation(), DeathLocation) > RadiusSq)
			{
				continue;
			}
			if (Instigator)
			{
				AddContact(Pair.Key, Instigator, Instigator->GetActorLocation(),
					EAIContactSource::AllyDeath, 0.7f);
			}
			ActivatePod(Pair.Key, TEXT("рядом погиб союзник"));
			break;
		}
	}
}

void UTacticalAIDirectorSubsystem::NotifyCombatNoise(AActor* Instigator,
	const FVector& Location, float Radius)
{
	const UWorld* World = GetWorld();
	const UTurnManagerSubsystem* TurnManager = World
		? World->GetSubsystem<UTurnManagerSubsystem>() : nullptr;
	if (!Instigator || !TurnManager)
	{
		return;
	}

	const float RadiusSq = FMath::Square(FMath::Max(0.f, Radius));
	for (AActor* Enemy : TurnManager->GetOpposingUnits(Instigator))
	{
		AUnitBase* Unit = Cast<AUnitBase>(Enemy);
		if (!Unit || FVector::DistSquared(Unit->GetActorLocation(), Location) > RadiusSq)
		{
			continue;
		}

		// Шум ПОДНИМАЕТ, но не вскрывает: боец идёт проверить точку, а не
		// получает готовую цель. Это правило XCOM и оно важно для темпа боя —
		// иначе один выстрел мгновенно ставил бы на уши половину карты.
		AddContact(ResolvePodId(Unit), Instigator, Location, EAIContactSource::Noise, 0.4f);
		if (AUnitAIController* AI = Cast<AUnitAIController>(Unit->GetController()))
		{
			AI->NotifyNoiseHeard(Location);
		}
	}
}

bool UTacticalAIDirectorSubsystem::IsPodActivated(FName PodId) const
{
	const FAIPodState* Pod = Pods.Find(PodId);
	return Pod && Pod->bActivated;
}

bool UTacticalAIDirectorSubsystem::IsUnitPodActivated(const AUnitBase* Unit) const
{
	return Unit && IsPodActivated(ResolvePodId(Unit));
}

void UTacticalAIDirectorSubsystem::GetPodContacts(FName PodId, TArray<FAIContact>& OutContacts) const
{
	OutContacts.Reset();
	const FAIPodState* Pod = Pods.Find(PodId);
	if (!Pod)
	{
		return;
	}

	for (const FAIContact& Contact : Pod->Contacts)
	{
		if (Contact.IsValidContact() && UTacticsCombatStatics::IsUnitAlive(Contact.Target.Get()))
		{
			OutContacts.Add(Contact);
		}
	}
	OutContacts.Sort([](const FAIContact& A, const FAIContact& B)
	{
		return A.Confidence > B.Confidence;
	});
}

bool UTacticalAIDirectorSubsystem::GetBestContact(const AUnitBase* Unit, FAIContact& OutContact) const
{
	if (!Unit)
	{
		return false;
	}

	TArray<FAIContact> Contacts;
	GetPodContacts(ResolvePodId(Unit), Contacts);
	if (Contacts.Num() == 0)
	{
		return false;
	}
	OutContact = Contacts[0];
	return true;
}

void UTacticalAIDirectorSubsystem::AgeContacts(int32 CurrentTurn)
{
	const int32 MemoryTurns = FMath::Max(1, ContactMemoryTurns);
	for (TPair<FName, FAIPodState>& Pair : Pods)
	{
		Pair.Value.Contacts.RemoveAll([CurrentTurn, MemoryTurns](const FAIContact& Contact)
		{
			return !Contact.Target.IsValid() ||
				(CurrentTurn - Contact.LastUpdatedTurn) > MemoryTurns;
		});

		// Достоверность падает линейно по возрасту: свежий контакт ведёт в бой,
		// старый — только в разведку.
		for (FAIContact& Contact : Pair.Value.Contacts)
		{
			const int32 Age = FMath::Max(0, CurrentTurn - Contact.LastUpdatedTurn);
			Contact.Confidence = FMath::Clamp(
				1.f - static_cast<float>(Age) / static_cast<float>(MemoryTurns), 0.05f, 1.f);
		}
	}
}
