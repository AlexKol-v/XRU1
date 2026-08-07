#include "FogOfWarSubsystem.h"

#include "FogRevealableComponent.h"
#include "TacticalQuestEvents.h" // первый визуальный контакт — доменное событие сценария
#include "TacticsCombatStatics.h"
#include "TacticsTypes.h"
#include "TurnManagerSubsystem.h"
#include "UnitBase.h"
#include "XRU1Log.h"

#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/IConsoleManager.h"

/**
 * Диагностика: подробный журнал каждого решения видимости.
 * `xru1.Fog.Explain 1` — печатать причину пересчёта и итог по каждому актору.
 */
static TAutoConsoleVariable<int32> CVarFogExplain(
	TEXT("xru1.Fog.Explain"), 0,
	TEXT("Туман войны: подробный разбор пересчётов и решений видимости (0/1)."),
	ECVF_Default);

/**
 * `xru1.Fog.Disable 1` — считать всех видимыми. ТОЛЬКО отладка: правило
 * «механические гейты работают даже при выключенной картинке» относится к
 * post-process, а не к этому переключателю.
 */
static TAutoConsoleVariable<int32> CVarFogDisable(
	TEXT("xru1.Fog.Disable"), 0,
	TEXT("Туман войны: выключить скрытие (отладка). 1 — все видимы."),
	ECVF_Cheat);

/**
 * Как часто переоценивать видимость, пока кто-то бежит. XCOM решает этот случай
 * дешёвым пер-тайловым запросом; у нас тайлов нет, поэтому берём полный, но
 * троттлённый пересчёт. 0.1 с — компромисс: реакция быстрее, чем игрок успевает
 * прочитать кадр, а работы в 10 раз меньше, чем «каждый кадр».
 */
static TAutoConsoleVariable<float> CVarFogMoveRecheck(
	TEXT("xru1.Fog.MoveRecheck"), 0.1f,
	TEXT("Туман войны: интервал переоценки видимости во время движения, сек."),
	ECVF_Default);

UFogOfWarSubsystem* UFogOfWarSubsystem::Get(const UObject* WorldContextObject)
{
	const UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	return World ? World->GetSubsystem<UFogOfWarSubsystem>() : nullptr;
}

bool UFogOfWarSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	// Только игровые миры: в редакторском мире юниты стоят как декорации, и
	// скрывать их туманом нельзя — дизайнер перестанет их видеть во вьюпорте.
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UFogOfWarSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UFogOfWarSubsystem, STATGROUP_Tickables);
}

// --- Регистрация ---------------------------------------------------------------

void UFogOfWarSubsystem::RegisterRevealable(UFogRevealableComponent* Component)
{
	if (!Component)
	{
		return;
	}
	Revealables.AddUnique(Component);
	// Новый скрываемый обязан получить своё состояние до первого кадра, в котором
	// его увидит игрок: враг, «мигнувший» один кадр перед скрытием, — это и есть
	// утечка позиции.
	MarkVisibilityDirty(Component);
}

void UFogOfWarSubsystem::UnregisterRevealable(UFogRevealableComponent* Component)
{
	if (!Component)
	{
		return;
	}
	Revealables.RemoveAllSwap([Component](const TWeakObjectPtr<UFogRevealableComponent>& Entry)
	{
		return !Entry.IsValid() || Entry.Get() == Component;
	});
	if (const AActor* Owner = Component->GetOwner())
	{
		VisibilityCache.Remove(Owner);
	}
}

// --- Запрос --------------------------------------------------------------------

bool UFogOfWarSubsystem::IsActorCurrentlyVisible(const AActor* Actor) const
{
	if (!Actor)
	{
		return false;
	}
	if (CVarFogDisable.GetValueOnGameThread() != 0)
	{
		return true;
	}

	if (const bool* Cached = VisibilityCache.Find(Actor))
	{
		return *Cached;
	}

	// ⚠️ Актор без `UFogRevealableComponent` считается ВИДИМЫМ. Это правило, а не
	// упрощение: туман скрывает только то, что ему поручили скрывать, и про всё
	// остальное обязан отвечать «видно». Так устроен и XCOM 2 — у него
	// `XComGameState_InteractiveObject::ForceModelVisible()` безусловно возвращает
	// `eForceVisible`, то есть цель миссии, дверь и терминал не прячутся никогда.
	//
	// Практический смысл для нас: бомба (`ABombObjective`), зона эвакуации
	// (`AEvacZone`) и зоны сценария — обычные акторы без этого компонента, значит
	// игрок ВСЕГДА видит, куда идти. Если бы предикат отвечал «не видно», любая
	// будущая подсветка цели, повешенная на него, молча бы погасла.
	return true;
}

TArray<AActor*> UFogOfWarSubsystem::GetCurrentlyVisibleEnemies() const
{
	TArray<AActor*> Result;
	Result.Reserve(VisibleEnemies.Num());
	for (const TWeakObjectPtr<AActor>& Enemy : VisibleEnemies)
	{
		if (AActor* Actor = Enemy.Get())
		{
			Result.Add(Actor);
		}
	}
	return Result;
}

int32 UFogOfWarSubsystem::GetCurrentlyVisibleEnemyCount() const
{
	// ⚠️ Счётчик поддерживается пересчётом, а не считается по запросу. Он висит на
	// биндинге HUD, то есть спрашивается каждый кадр: строить на каждый
	// такой запрос массив (кучная аллокация) и опрашивать кэш по всем
	// врагам слишком дорого. Здесь — обход уже готового списка без аллокаций.
	int32 Count = 0;
	for (const TWeakObjectPtr<AActor>& Enemy : VisibleEnemies)
	{
		Count += Enemy.IsValid() ? 1 : 0;
	}
	return Count;
}

// --- Жизненный цикл ------------------------------------------------------------

void UFogOfWarSubsystem::MarkVisibilityDirty(const UObject* Reason)
{
	if (!bVisibilityDirty)
	{
		// Причину запоминаем ПЕРВУЮ за кадр: она и есть настоящий триггер,
		// последующие события кадра — её следствия. Имя источника сохраняем
		// строкой: сам объект к моменту пересчёта может быть уже уничтожен
		// (типовой источник — погибший юнит), а журнал обязан назвать виновника.
		PendingReasonName = GetNameSafe(Reason);
		if (IsExplainEnabled())
		{
			// Display, а не Verbose: гейт — сам cvar. Verbose здесь означал бы,
			// что включённый разбор всё равно ничего не печатает.
			UE_LOG(LogXRU1Fog, Display, TEXT("[Fog] пересчёт запрошен: %s"), *PendingReasonName);
		}
	}
	bVisibilityDirty = true;
}

void UFogOfWarSubsystem::ResetForScenario(FName ScenarioId, int32 RunId)
{
	UE_LOG(LogXRU1Fog, Log, TEXT("[Fog] Reset: сценарий %s, run %d (было %s/%d)"),
		*ScenarioId.ToString(), RunId, *ActiveScenarioId.ToString(), ActiveRunId);

	ActiveScenarioId = ScenarioId;
	ActiveRunId = RunId;

	VisibilityCache.Reset();
	VisibleEnemies.Reset();
	// «Первый контакт» — факт ОДНОГО запуска: retry обязан проговорить реплику
	// заново, иначе второй заход по той же карте пройдёт молча.
	SpottedEnemies.Reset();
	Revealables.RemoveAllSwap([](const TWeakObjectPtr<UFogRevealableComponent>& Entry)
	{
		return !Entry.IsValid();
	});
	// Сценарные удержания показа НЕ имеют права пережить запуск. Оборванный
	// StateTree (abort, retry, выход из сценария) может не дойти до своего
	// `ExitState`, и повисшее удержание оставило бы врага видимым весь следующий
	// прогон — состояние прошлого `RunId`, ровно то, что этот сброс и убирает.
	for (const TWeakObjectPtr<UFogRevealableComponent>& Entry : Revealables)
	{
		if (UFogRevealableComponent* Component = Entry.Get())
		{
			Component->ClearScriptedRevealHolds();
		}
	}
	bWasAnyoneInTransit = false;
	bHasDeferredHides = false;
	LastRecomputeTime = 0.0;

	// Первый полный пересчёт — СРАЗУ, а не в ближайшем тике: управление игроку
	// отдаётся после старта сценария, и к этому моменту скрытые уже обязаны быть
	// скрыты. Иначе первый кадр боя показывает всю расстановку.
	RecomputeNow(TEXT("старт сценария"));

	// И ещё один в ближайшем тике: на момент сброса стороны боя ЕЩЁ НЕ собраны
	// (`ResetForScenario` зовётся до `StartCombat`), поэтому расчёт выше даёт
	// «никого не видно» — правильное безопасное начало, но не финальная картина.
	bVisibilityDirty = true;
	PendingReasonName = TEXT("состав сторон после старта");
}

// --- Тик -----------------------------------------------------------------------

void UFogOfWarSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	const UTurnManagerSubsystem* TurnManager = GetTurnManager();
	if (!TurnManager)
	{
		return;
	}
	const UWorld* World = GetWorld();

	// ⚠️ Проверки «идёт ли бой» здесь НЕТ намеренно. До старта боя стороны пусты,
	// значит расчёт даёт «никого не видно» — и враги оказываются скрыты ещё до
	// первого хода. Ранний выход по `IsInCombat` оставлял бы окно (стриминг
	// сублевела, пауза перед `StartMissionCombat`), в котором вся расстановка
	// видна. Направление ошибки выбрано в пользу скрытности.

	// 0. Отладочный выключатель переключили — это тоже событие. Без этого
	//    `xru1.Fog.Disable 1` менял бы ответы предиката, но не картинку (пересчёт
	//    ждал бы ближайшего движения), и выключатель выглядел бы сломанным.
	const bool bDisabledNow = CVarFogDisable.GetValueOnGameThread() != 0;
	if (bDisabledNow != bFogDisabledLastTick)
	{
		bFogDisabledLastTick = bDisabledNow;
		MarkVisibilityDirty(this);
	}

	// 1. Событие: пересчёт один раз за кадр, сколько бы событий ни пришло.
	if (bVisibilityDirty)
	{
		bVisibilityDirty = false;
		// Копия намеренно: `RecomputeNow` рассылает события, обработчик которых
		// вправе снова позвать `MarkVisibilityDirty` и переписать строку — тогда
		// указатель на её буфер повис бы прямо посреди вызова.
		const FString ReasonCopy = PendingReasonName;
		RecomputeNow(*ReasonCopy);
		return;
	}

	// 2. Кто-то в пути (или ждёт отложенного скрытия) — переоценка с троттлингом.
	const bool bAnyoneInTransit = IsAnyUnitInTransit();
	if (bAnyoneInTransit || bHasDeferredHides)
	{
		const double Now = World->GetTimeSeconds();
		if (Now - LastRecomputeTime >= FMath::Max(0.01f, CVarFogMoveRecheck.GetValueOnGameThread()))
		{
			RecomputeNow(bAnyoneInTransit ? TEXT("движение") : TEXT("отложенное скрытие"),
				/*bRoutine=*/true);
		}
		bWasAnyoneInTransit = bWasAnyoneInTransit || bAnyoneInTransit;
		return;
	}

	// 3. Движение только что закончилось — один точный пересчёт по финальным
	//    позициям. Троттлённый мог прийтись на середину последнего шага.
	if (bWasAnyoneInTransit)
	{
		bWasAnyoneInTransit = false;
		RecomputeNow(TEXT("движение завершено"));
		return;
	}

	// 4. Ничего не происходит — пересчётов ноль. Остаётся только проверка выше
	//    «бежит ли кто-нибудь»: обход уже имеющегося массива без аллокаций.
}

bool UFogOfWarSubsystem::IsAnyUnitInTransit() const
{
	// ⚠️ Обходим СВОЙ реестр, а не стороны боя. `GetPlayerSideUnits()` и
	// `GetEnemySideUnits()` возвращают массив ПО ЗНАЧЕНИЮ (конвертация из
	// `TObjectPtr`), то есть спрашивать их в тике значило бы две кучные
	// аллокации каждый кадр до конца боя. Реестр скрываемых содержит всех юнитов
	// обеих сторон (компонент — дефолтный субобъект `AUnitBase`), и этого
	// достаточно: видимость меняет и наш разведчик, и выходящий из-за угла враг.
	for (const TWeakObjectPtr<UFogRevealableComponent>& Entry : Revealables)
	{
		const UFogRevealableComponent* Component = Entry.Get();
		if (Component && UTacticsCombatStatics::IsUnitInTransit(Component->GetOwner()))
		{
			return true;
		}
	}
	return false;
}

// --- Расчёт --------------------------------------------------------------------

bool UFogOfWarSubsystem::IsExplainEnabled()
{
	return CVarFogExplain.GetValueOnGameThread() != 0;
}

bool UFogOfWarSubsystem::IsPlayerSideActor(const AActor* Actor)
{
	const AUnitBase* Unit = Cast<const AUnitBase>(Actor);
	return Unit && Unit->GetGenericTeamId().GetId() == TacticsTeamIds::Player;
}

bool UFogOfWarSubsystem::ComputeActorVisible(const AActor* Actor, const TArray<AActor*>& Viewers) const
{
	if (!Actor)
	{
		return false;
	}

	// Свои никогда не скрываются: игрок обязан видеть отряд, даже когда боец
	// один в дальнем углу карты и его никто не «видит».
	if (IsPlayerSideActor(Actor))
	{
		return true;
	}

	// Вся геометрия — в общем статике: у тумана и Squadsight одна реализация.
	return UTacticsCombatStatics::AnyUnitSees(Viewers, Actor);
}

void UFogOfWarSubsystem::RecomputeNow(const TCHAR* Reason, bool bRoutine)
{
	const UTurnManagerSubsystem* TurnManager = GetTurnManager();
	if (!TurnManager)
	{
		return;
	}

	const UWorld* World = GetWorld();
	LastRecomputeTime = World->GetTimeSeconds();
	const bool bExplain = CVarFogExplain.GetValueOnGameThread() != 0;
	const bool bDisabled = CVarFogDisable.GetValueOnGameThread() != 0;

	// Источники зрения собираются ОДИН раз на пересчёт. Это тот самый приём, из-за
	// отсутствия которого у Firaxis наивный путь стоил ~20 мс: список видимого
	// игроку нельзя перестраивать внутри цикла по целям.
	//
	// ⚠️ Точка расширения: когда появятся источники зрения без юнита (сканер,
	// начальный reveal сценария — у XCOM это `XComGameState_SquadViewer`), их
	// добавляют ЗДЕСЬ, и весь остальной слой не меняется.
	const TArray<AActor*> Viewers = TurnManager->GetPlayerSideUnits();

	bHasDeferredHides = false;

	// ⚠️ Изменения копятся и рассылаются ПОСЛЕ обхода реестра. Broadcast изнутри
	// цикла — заготовка для реентрантности: подписчик вправе уничтожить актора
	// (это `EndPlay` → `UnregisterRevealable` → перетряска того самого массива,
	// по которому мы идём). Сегодня подписчик один и безобидный, но такие вещи
	// ломаются ровно тогда, когда подписчиков станет двое.
	TArray<TPair<AActor*, bool>, TInlineAllocator<8>> PendingBroadcasts;

	for (int32 Index = Revealables.Num() - 1; Index >= 0; --Index)
	{
		UFogRevealableComponent* Component = Revealables[Index].Get();
		if (!Component)
		{
			Revealables.RemoveAtSwap(Index);
			continue;
		}
		AActor* Owner = Component->GetOwner();
		if (!Owner)
		{
			continue;
		}

		// Своих туман не ведёт вовсе: ни решения, ни применения. Их презентацией
		// владеет контроллер игрока (кадр выстрела, прицеливание, реакция), и
		// второй владелец там уже приводил к худам поверх кадра.
		bool bVisibleToPlayer = true;
		const bool bOwnSide = IsPlayerSideActor(Owner);
		if (!bOwnSide)
		{
			const bool bComputed = bDisabled || ComputeActorVisible(Owner, Viewers);
			const bool bDesired = Component->ResolveDesiredVisibility(bComputed);

			Component->ApplyVisibility(bDesired);
			if (Component->IsHideDeferred())
			{
				bHasDeferredHides = true;
			}

			// ⚠️ В кэш идёт ПРИМЕНЁННОЕ состояние, а не желаемое. Скрытие может быть
			// отложено (юнит доигрывает монтаж), и всё это время враг на экране.
			// Если бы кэш хранил «желаемое», счётчик врагов и камера уже считали бы
			// его скрытым, пока игрок его видит, — расхождение картинки и правил,
			// то есть ровно то, что весь этот слой должен исключать.
			bVisibleToPlayer = !Component->IsPresentationHidden();
		}

		const bool* Cached = VisibilityCache.Find(Owner);
		const bool bChangedThisActor = !Cached || *Cached != bVisibleToPlayer;
		if (bChangedThisActor)
		{
			// Кэш обновляем сразу (ниже по нему строится список видимых врагов),
			// а событие откладываем до конца обхода.
			VisibilityCache.Add(Owner, bVisibleToPlayer);
			PendingBroadcasts.Emplace(Owner, bVisibleToPlayer);
		}

		// ⚠️ В рутинном пересчёте (кто-то бежит) печатаем ТОЛЬКО изменившихся.
		// Полный разбор по всем акторам на каждом шаге движения — это восемь
		// строк сотни раз за бой; после подъёма уровня с Verbose до Display такой
		// журнал стал бы нечитаемым быстрее, чем принёс пользу.
		if (bExplain && (!bRoutine || bChangedThisActor))
		{
			// ⚠️ Уровень Display, а НЕ Verbose. Verbose-строки отфильтрованы
			// умолчанием категории (`Log`), поэтому включённый `xru1.Fog.Explain 1`
			// печатал только сводку, а сам разбор по акторам не доходил до
			// журнала — половина инструмента была мёртвой.
			// Гейт здесь — сам cvar, второй не нужен.
			//
			// У своих решение не принималось вовсе — печатать их последнюю
			// (возможно, ещё стартовую) причину значило бы врать в диагностике,
			// по которой разбирают «почему видно/не видно».
			UE_LOG(LogXRU1Fog, Display, TEXT("[Fog]   %s: виден=%d (%s)%s"),
				*GetNameSafe(Owner), bVisibleToPlayer ? 1 : 0,
				bOwnSide ? TEXT("сторона игрока — туманом не ведётся")
					: Component->GetLastDecisionReason(),
				Component->IsHideDeferred() ? TEXT(" [скрытие отложено]") : TEXT(""));
		}
	}

	// Список видимых врагов поддерживается здесь же: HUD спрашивает его каждый
	// кадр, и считать его по запросу — это ровно тот наивный путь, который у
	// Firaxis измерен в ~20 мс на вызов.
	VisibleEnemies.Reset();
	for (AActor* Enemy : TurnManager->GetEnemySideUnits())
	{
		if (Enemy && UTacticsCombatStatics::IsUnitAlive(Enemy) && IsActorCurrentlyVisible(Enemy))
		{
			VisibleEnemies.Add(Enemy);
		}
	}

	// ⚠️ Рутинный пересчёт без изменений НЕ логируется даже при включённом разборе.
	// Иначе журнал забивается строками «пересчёт «движение»: изменений=0», и
	// сигнал тонет в шуме. Значимое (старт, граница хода, событие,
	// любое изменение) печатается по-прежнему.
	if (PendingBroadcasts.Num() > 0 || (bExplain && !bRoutine))
	{
		UE_LOG(LogXRU1Fog, Log,
			TEXT("[Fog] пересчёт «%s»: источников=%d, скрываемых=%d, изменений=%d, видимых врагов=%d"),
			Reason, Viewers.Num(), Revealables.Num(), PendingBroadcasts.Num(), VisibleEnemies.Num());
	}

	// Реестр больше не обходится — теперь рассылать безопасно.
	for (const TPair<AActor*, bool>& Change : PendingBroadcasts)
	{
		OnActorVisibilityChanged.Broadcast(Change.Key, Change.Value);

		// Первый визуальный контакт с врагом — доменный факт сценария, а не
		// только смена картинки: на нём висит реплика «Нас увидели». Публикуем
		// ЗДЕСЬ, потому что здесь и принимается решение о видимости; квест
		// получает его из единственного источника правды, как и HUD.
		//
		// ⚠️ Только В БОЮ. Первый пересчёт идёт из `StartMissionCombat` ДО
		// `StartCombat`, и на нём КАЖДЫЙ актор меняет состояние с «неизвестно» на
		// фактическое. Это инициализация кэша, а не обнаружение: без гейта
		// реплика «Нас увидели» звучала на нулевом кадре миссии.
		const UTurnManagerSubsystem* Turns = GetWorld()
			? GetWorld()->GetSubsystem<UTurnManagerSubsystem>() : nullptr;
		const bool bCombatRunning = Turns && Turns->IsInCombat();

		if (Change.Value && Change.Key && bCombatRunning &&
			!SpottedEnemies.Contains(Change.Key))
		{
			const AUnitBase* Unit = Cast<AUnitBase>(Change.Key);
			if (Unit && Unit->GetGenericTeamId().GetId() == TacticsTeamIds::Enemy)
			{
				SpottedEnemies.Add(Change.Key);
				UTacticalQuestEvents::BroadcastQuestEvent(
					this, TacticalQuestTags::Event_Tactical_Combat_Enemy_Spotted, Change.Key);
			}
		}
	}

	// Пересчёт состоялся — даже если ни один актор не сменил видимость. Наблюдатели
	// могли сдвинуться, а это меняет картину МЕСТНОСТИ, и визуальный слой обязан
	// узнать об этом здесь, а не заводить собственное расписание.
	OnVisibilityRecomputed.Broadcast();
}

UTurnManagerSubsystem* UFogOfWarSubsystem::GetTurnManager() const
{
	const UWorld* World = GetWorld();
	return World ? World->GetSubsystem<UTurnManagerSubsystem>() : nullptr;
}
