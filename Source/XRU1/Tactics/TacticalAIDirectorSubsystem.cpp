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

bool UTacticalAIDirectorSubsystem::IsEnemyPod(const FAIPodState& Pod)
{
	// ⚠️ РЕГИСТРАЦИЯ идёт без фильтра команды (см. RegisterUnit: в OnPossess
	// TeamId ещё не назначен), поэтому бойцы игрока тоже заводят себе поды.
	// Механика подов — вражеская: под игрока нельзя ни вскрыть, ни поднять.
	// Без этой проверки лог прогона 2026-08-04 пестрел строками вида
	// «Под BP_Unit_Sniper_C_0 вскрыт (рядом погиб союзник): поднято бойцов 0».
	for (const TWeakObjectPtr<AUnitBase>& Member : Pod.Members)
	{
		const AUnitBase* Unit = Member.Get();
		if (Unit && Unit->GetGenericTeamId().GetId() == TacticsTeamIds::Enemy)
		{
			return true;
		}
	}
	return false;
}

bool UTacticalAIDirectorSubsystem::ActivatePod(FName PodId, const TCHAR* Reason)
{
	FAIPodState* Pod = Pods.Find(PodId);
	if (!Pod || Pod->bActivated)
	{
		return false;
	}
	if (!IsEnemyPod(*Pod))
	{
		return false; // под стороны игрока: вскрывать нечего и логировать нечего
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
		if (Pair.Key == VictimPod || Pair.Value.bActivated || !IsEnemyPod(Pair.Value))
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

	// ⚠️ ШУМ СЛЫШИТ БОЕЦ — РЕАГИРУЕТ ГРУППА.
	//
	// Так это устроено в XCOM 2: `eAC_DetectedSound` — «подозрительная» причина
	// (жёлтая тревога), и она применяется ко ВСЕЙ группе через
	// `XComGameState_AIGroup::ApplyAlertAbilityToGroup`, а не к тому, кто
	// оказался в радиусе. У нас же тревогу получал ровно тот, кто попал в круг,
	// — поэтому напарник за стеной, в двух шагах от перестрелки, продолжал
	// стоять на посту (жалоба по прогону 2026-08-04). Радиус решает, УСЛЫШАЛА ли
	// группа; дальше знание общее, как и память контактов пода.
	//
	// Вскрытия при этом по-прежнему нет: под поднимается в жёлтую тревогу и идёт
	// проверять точку, а не получает готовую цель.
	const float RadiusSq = FMath::Square(FMath::Max(0.f, Radius));
	TSet<FName> HeardByPods;
	for (AActor* Enemy : TurnManager->GetOpposingUnits(Instigator))
	{
		const AUnitBase* Unit = Cast<AUnitBase>(Enemy);
		// Механика подов — вражеская. Выстрел врага не имеет права «поднимать»
		// бойцов игрока: у них тот же AUnitAIController (он нужен перцепции для
		// Overwatch), и без фильтра им честно ставился жёлтый alert.
		if (!Unit || Unit->GetGenericTeamId().GetId() != TacticsTeamIds::Enemy ||
			!UTacticsCombatStatics::IsUnitAlive(Unit))
		{
			continue;
		}
		if (FVector::DistSquared(Unit->GetActorLocation(), Location) > RadiusSq)
		{
			continue;
		}
		HeardByPods.Add(ResolvePodId(Unit));
	}

	for (const FName PodId : HeardByPods)
	{
		const bool bNewContact = AddContact(PodId, Instigator, Location,
			EAIContactSource::Noise, 0.4f);

		FAIPodState* Pod = Pods.Find(PodId);
		if (!Pod)
		{
			continue;
		}

		int32 Alerted = 0;
		for (const TWeakObjectPtr<AUnitBase>& Member : Pod->Members)
		{
			AUnitBase* Unit = Member.Get();
			if (!Unit || !UTacticsCombatStatics::IsUnitAlive(Unit) ||
				Unit->GetGenericTeamId().GetId() != TacticsTeamIds::Enemy)
			{
				continue;
			}
			// Staged-голограмма обучения в бой не введена — поднимать её нельзя
			// (то же правило, что в ActivatePod).
			if (!UTacticalScenarioSubsystem::IsActorScenarioActive(Unit))
			{
				continue;
			}
			if (AUnitAIController* AI = Cast<AUnitAIController>(Unit->GetController()))
			{
				AI->NotifyNoiseHeard(Location);
				++Alerted;
			}
		}

		// Печатаем только НОВЫЙ контакт: шум идёт с каждого выстрела, и лог
		// «под слышит стрельбу» на каждый из шестидесяти выстрелов боя
		// перестал бы читаться. Первое поднятие — это событие, повторы — нет.
		if (bNewContact && Alerted > 0)
		{
			UE_LOG(LogXRU1AI, Log,
				TEXT("[AI] Под %s поднят по шуму боя (%.0f, %.0f): бойцов %d — идут проверять"),
				*PodId.ToString(), Location.X, Location.Y, Alerted);
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

// --- Резервации позиций (AI-5) ----------------------------------------------

void UTacticalAIDirectorSubsystem::ReservePosition(AUnitBase* Unit, const FVector& Point)
{
	if (!Unit)
	{
		return;
	}
	// Одно намерение на бойца: старая запись перетирается, а не копится.
	Reservations.Add(Unit, Point);
}

void UTacticalAIDirectorSubsystem::ReleaseReservation(const AUnitBase* Unit)
{
	if (Unit)
	{
		Reservations.Remove(Unit);
	}
}

bool UTacticalAIDirectorSubsystem::IsPositionReserved(const AUnitBase* Requester,
	const FVector& Point, float Radius) const
{
	if (Radius <= 0.f)
	{
		return false;
	}
	const float RadiusSq = FMath::Square(Radius);
	for (const TPair<TWeakObjectPtr<const AUnitBase>, FVector>& Pair : Reservations)
	{
		const AUnitBase* Owner = Pair.Key.Get();
		// Протухшая запись (боец погиб) резервацию не держит: чистим лениво,
		// отдельный проход по смерти каждого юнита того не стоит.
		if (!Owner || Owner == Requester)
		{
			continue;
		}
		if (!UTacticsCombatStatics::IsUnitAlive(Owner))
		{
			continue;
		}
		if (FVector::DistSquared2D(Pair.Value, Point) < RadiusSq)
		{
			return true;
		}
	}
	return false;
}

void UTacticalAIDirectorSubsystem::ClearReservations()
{
	Reservations.Reset();
}

void UTacticalAIDirectorSubsystem::AgeContacts(int32 CurrentTurn)
{
	// Смена хода — естественная граница жизни намерений: маршруты прошлого хода
	// уже исполнены или сорваны, держать их дальше незачем.
	ClearReservations();

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
