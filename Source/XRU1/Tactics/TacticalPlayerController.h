#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "TacticsTypes.h"
#include "TacticalPlayerController.generated.h"

class AUnitBase;
class ATacticalCameraPawn;
class AMoveRangeVisualizer;
class UInputMappingContext;
class UInputAction;
struct FInputActionValue;
class UMenuScreenBase;
class UPrimaryGameLayout;
class ABombObjective;
class AEvacZone;
struct FMoveOrderPlan;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSelectedUnitChanged, AUnitBase*, NewSelected);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnHoveredUnitChanged, AUnitBase*, NewHovered);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnAvailableActionsChanged);

/**
 * ЕДИНЫЙ ИСТОЧНИК ПРАВДЫ о режиме взаимодействия игрока в свою фазу. Раньше
 * это были два независимых булевых флага (bAwaitingAttackTarget/AbilityTarget)
 * с ручными условиями входа/выхода в каждой точке — отсюда «камера зависла»
 * (один из путей выхода забывал её вернуть). Теперь ЛЮБОЙ переход идёт через
 * SetTargetingMode → ExitTargetingMode(старый) + EnterTargetingMode(новый),
 * поэтому откат побочных эффектов (камера/подсветка/баннер) не может быть
 * пропущен. Новое состояние = новый case в двух switch'ах, а не новый флаг
 * с россыпью условий по всему классу.
 *
 * Фаза боя (Player/Enemy) и «юнит в движении» сюда НЕ входят — их источник
 * TurnManager и AIController соответственно (не дублируем истину).
 */
UENUM(BlueprintType)
enum class EPlayerTargetingMode : uint8
{
	/** Обычный тактический режим: выбор юнита, приказ движения. */
	None,
	/** Прицеливание атаки: камера «из-за плеча», взятая цель, баннер. */
	Attack,
	/** Выбор цели способности (медик ждёт клика по союзнику). */
	Ability
};

/**
 * Команды, конкурирующие за одну тактическую активацию выбранного юнита.
 * Их доступность вычисляет один арбитр CanIssueCommand: HUD и исполнение
 * используют один и тот же ответ, а не поддерживают параллельные наборы if.
 */
UENUM(BlueprintType)
enum class ETacticalPlayerCommand : uint8
{
	Move,
	Attack,
	Overwatch,
	HunkerDown,
	ClassAbility,
	Interact,
	SkipUnitTurn
};

/**
 * Контроллер игрока в тактическом бою (GDD §11): выбор юнита (ЛКМ/1–4/Tab),
 * приказ перемещения (ПКМ, бюджет по длине пути навмеша), атака (ЛКМ по врагу
 * или кнопка «Огонь» → GameplayEvent Event.Attack), хоткеи действий
 * (Y/X/R/F/Backspace/Enter), камера (WASD/QE/колесо) и пауза (Esc). Ввод —
 * Enhanced Input (IMC задаётся в BP-наследнике). В фазу врага приказы
 * заблокированы.
 *
 * Также владеет визуализатором зоны хода (AMoveRangeVisualizer) и поднимает
 * корневой UI-layout через UGameUIManagerSubsystem (как ACSTPlayerController).
 */
UCLASS(Abstract)
class XRU1_API ATacticalPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	ATacticalPlayerController();

	/** Текущий выбранный юнит (nullptr — никто). */
	UFUNCTION(BlueprintPure, Category = "Tactics|Control")
	AUnitBase* GetSelectedUnit() const { return SelectedUnit; }

	/**
	 * Выбирает юнита (или снимает выбор при nullptr). Зовётся и из HUD (клик по
	 * портрету). Это ПОЛЬЗОВАТЕЛЬСКИЙ выбор: он проходит Action Gate обучения и
	 * публикует `Unit.Selected` после фактической смены выбранного бойца.
	 */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Control")
	void SelectUnit(AUnitBase* Unit);

	/** Выбор юнита отряда по слоту 1–4 (порядок — по списку стороны игрока). */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Control")
	void SelectUnitBySlot(int32 SlotIndex);

	/** Следующий юнит отряда с оставшимися AP (Tab). Нет таких — выбор не меняется. */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Control")
	void SelectNextUnit();

	/**
	 * Автовыбор бойца (XCOM: активный боец есть всегда): первый живой с AP,
	 * начиная со следующего за текущим (или с начала отряда, если выбора нет);
	 * нет никого с AP — любой живой; отряд выбит — выбор снимается.
	 */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Control")
	void SelectNextAvailableUnit();

	/** Юниты отряда (сторона игрока, живые) — для портретов HUD. */
	UFUNCTION(BlueprintPure, Category = "Tactics|Control")
	TArray<AUnitBase*> GetSquad() const;

	/** Служебный общий планировщик пути для AI без показа зоны хода. */
	bool PlanMoveForUnit(AUnitBase* Unit, const FVector& Goal, int32 MaxActionPoints,
		FMoveOrderPlan& OutPlan);

	/** Кнопка/хоткей «Завершить ход». */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Control")
	void RequestEndTurn();

	/**
	 * Кнопка «Огонь»: первое нажатие включает прицеливание, повторное либо
	 * ЛКМ по уже выбранной цели подтверждает выстрел. Прямой ЛКМ по врагу без
	 * вооружённого режима только обновляет hover/прогноз и не стреляет.
	 */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Control")
	void RequestAttack();

	/** Хоткей/кнопка Overwatch (Y). */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Control")
	void RequestOverwatch();

	/** Хоткей/кнопка глухой обороны (X). */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Control")
	void RequestHunkerDown();

	/** Хоткей/кнопка способности класса (R). Для медика включает режим выбора цели. */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Control")
	void RequestClassAbility();

	/** Хоткей/кнопка «Пропустить ход» юнита (Backspace). */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Control")
	void RequestSkipUnitTurn();

	/** Контекстное действие F: обезвредить бомбу рядом / эвакуация в зоне. */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Control")
	void RequestInteract();

	/** Пауза (Esc): пуш экрана паузы на слой Menu. */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Control")
	void RequestPause();

	/**
	 * Команда отклонена. Игрок обязан получить обратную связь: «ничего не
	 * произошло» неотличимо от зависшей игры, и именно этим обычно читается
	 * закрытый Action Gate обучения.
	 */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Control")
	void NotifyCommandDenied();

	// --- Подсказки обучения (данные для STutorialHintOverlay) -----------------

	/** DisplayName активного tracked quest; пусто — трекер скрыт. */
	FText GetTutorialQuestTitle() const;

	/** Активные цели с Description и прогрессом; фолбэк — DenialReason политики. */
	FText GetTutorialObjectiveLines() const;

	/** Причина последнего отказа gate; живёт 3 секунды. */
	FText GetTutorialDenialText() const;

	/**
	 * Субтитр активного такта в формате «Купол: реплика». Пусто, если такт не
	 * идёт или у него нет текста. Голос играет presentation-подсистема; здесь
	 * только текстовая дорожка, поэтому реплика читается и без озвучки.
	 */
	FText GetTutorialBeatSubtitle() const;

	/**
	 * Сценарий поставил стартовую камеру (InitialCameraAnchorId): первый
	 * автофокус на центр отряда при старте боя пропускается, ракурс — за
	 * режиссурой. Зовёт ATacticalScenarioDirector после телепорта камеры.
	 */
	void NotifyScenarioCameraPlaced() { bScenarioCameraPlaced = true; }

	/**
	 * Сообщить UI, что набор доступных действий мог измениться извне (открылась
	 * зона эвакуации, скрипт туториала сдвинул мир). Зовут GameMode и level BP —
	 * HUD пересчитает серость кнопок без ожидания следующего события боя.
	 */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Control")
	void NotifyAvailableActionsChanged() { OnAvailableActionsChanged.Broadcast(); }

	/**
	 * Юнит закончил перемещение (зовёт AUnitAIController из OnMoveCompleted).
	 * Если это НЕ выбранный юнит — его диск занятости встал на новое место,
	 * зона хода выбранного пересчитывается немедленно.
	 */
	void NotifyUnitMoveFinished(AUnitBase* Unit);

	/**
	 * Сейчас фаза игрока и бой идёт — ЕДИНЫЙ признак «игрок может действовать».
	 * По нему гейтятся и приказы, и активность кнопок/панели прицеливания HUD.
	 */
	UFUNCTION(BlueprintPure, Category = "Tactics|Control")
	bool IsPlayerPhase() const;

	/** Играется ли сейчас реакционный выстрел (модальное окно камеры). */
	bool IsReactionShotPlaying() const { return bReactionPlaying; }

	/**
	 * Единый арбитр команд: фаза, выбранный юнит, движение, модальный targeting,
	 * AP/заряды/цели и GAS-блокировка выполняющейся способности. Этим же API
	 * пользуются Request* и серость кнопок HUD.
	 */
	UFUNCTION(BlueprintPure, Category = "Tactics|Control")
	bool CanIssueCommand(ETacticalPlayerCommand Command) const;

	/** Текущий режим взаимодействия (единый источник правды, см. EPlayerTargetingMode). */
	UFUNCTION(BlueprintPure, Category = "Tactics|Control")
	EPlayerTargetingMode GetTargetingMode() const { return TargetingMode; }

	/** В режиме ли выбора цели способности (медик ждёт клика по союзнику). */
	UFUNCTION(BlueprintPure, Category = "Tactics|Control")
	bool IsTargetingAbility() const { return TargetingMode == EPlayerTargetingMode::Ability; }

	/** В режиме ли прицеливания атаки (нажата кнопка «Огонь», ждём клика по врагу). */
	UFUNCTION(BlueprintPure, Category = "Tactics|Control")
	bool IsTargetingAttack() const { return TargetingMode == EPlayerTargetingMode::Attack; }

	/** Юнит под курсором (с обводкой; nullptr — пусто). Для панели цели HUD. */
	UFUNCTION(BlueprintPure, Category = "Tactics|Control")
	AUnitBase* GetHoveredUnit() const { return HoveredUnit.Get(); }

	// --- Прицеливание по-XCOM'овски: список целей, Tab, кадр камеры -----------

	/**
	 * Враги, по которым выбранный боец МОЖЕТ выстрелить прямо сейчас
	 * (`UGA_Attack::CanTargetActor` — дальность + LOS/Squadsight), отсортированы
	 * по дальности. Ровно этот список листает Tab и показывает HUD.
	 */
	UFUNCTION(BlueprintPure, Category = "Tactics|Control")
	TArray<AUnitBase*> GetAttackTargets() const;

	/**
	 * Взятая на прицел цель (в режиме «Огонь»). HUD показывает шанс именно по
	 * ней, а не по случайному наведению мышью, — как в XCOM, где панель цели
	 * привязана к ВЫБРАННОЙ цели, а не к курсору.
	 */
	UFUNCTION(BlueprintPure, Category = "Tactics|Control")
	AUnitBase* GetCurrentAttackTarget() const { return CurrentAttackTarget.Get(); }

	/**
	 * Переключить цель (Tab / Q-E): Direction > 0 — следующая, < 0 — предыдущая.
	 * Камера берёт новую цель в кадр. Вне режима прицеливания ничего не делает.
	 */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Control")
	void CycleAttackTarget(int32 Direction = 1);

	/** Выйти из режима прицеливания (ПКМ/Esc/клик мимо) и вернуть камеру бойцу. */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Control")
	void CancelTargeting();

	/** Выстрелить по взятой на прицел цели (подтверждение: ЛКМ по ней или Enter). */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Control")
	void ConfirmAttack();

	/**
	 * Показать выстрел камерой (кадр «из-за плеча» на время выстрела). Зовут и
	 * атака игрока, и AI врага — иначе игрок не видит, в кого стреляет враг.
	 */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Control")
	void NotifyShotFired(AActor* Shooter, AActor* Target);

	/** Закрыть кадр обычного выстрела после terminal callback fire-action. */
	void EndShotPresentation();

	/**
	 * РЕАКЦИОННЫЙ выстрел (наблюдение) — кадр «из-за плеча» ПЛЮС замедление мира.
	 * В XCOM 2 срабатывание овервотча — отдельный кинематографический момент:
	 * время замедляется, камера показывает стрелка и цель. Без замедления
	 * реакция сливается с обычным ходом и игрок не успевает понять, что вообще
	 * произошло, — ровно эта претензия и была.
	 *
	 * Множитель — verbatim `XComCamera.ini`:
	 * `[XComGame.X2ReactionFireSequencer] ReactionFireWorldSloMoRate = 0.66`.
	 */
	// ⚠️ `NotifyReactionShotFired` УДАЛЕНА (аудит цикла 25). Она ставила слоу-мо
	// и кадр, но НЕ брала бронь очереди и НЕ ставила бегущего на паузу. Два
	// входа в один кинематографический момент неизбежно рассогласуются: вызов
	// из BP давал бы замедление без паузы и без очереди. Вход теперь один —
	// `TryBeginReactionShot`.

	/**
	 * ЗАБРОНИРОВАТЬ окно реакционного выстрела. Возвращает false, если реакция
	 * уже играется, — тогда наблюдатель НЕ стреляет и попробует снова через
	 * `ReactionCheckInterval` (его периодический опрос и есть очередь).
	 *
	 * ⚠️ Так устроен полный цикл наблюдения в XCOM 2, и это три отдельных
	 * свойства, которых у нас не было:
	 *  1. Реакция ПРЕРЫВАЕТ движение цели — бегущий останавливается, выстрел
	 *     разыгрывается, и только потом он бежит дальше. Раньше он продолжал
	 *     бежать, и кадр гонялся за уезжающей спиной — отсюда «некрасиво».
	 *  2. Наблюдатели стреляют ПО ОЧЕРЕДИ, а не залпом; каждый следующий
	 *     перевыбирает цель, если предыдущий её убил.
	 *  3. Мир замедляется на время реакции (`ReactionFireSloMoRate`).
	 */
	bool TryBeginReactionShot(AActor* Shooter, AActor* Target);

	/** Досрочно закрыть только camera/slow-mo presentation реакции. Движением владеет GA. */
	void EndReactionShotPresentation();

	/**
	 * Замедление мира на время кадра реакционного выстрела (XCOM: 0.66).
	 * 1.0 — выключить эффект.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Camera", meta = (ClampMin = "0.05", ClampMax = "1"))
	float ReactionFireSloMoRate = 0.66f;

	/**
	 * Какая контекстная интеракция (F) доступна выбранному юниту прямо сейчас:
	 * бомба рядом → эвакуация в зоне → ничего. Для текста/серости кнопки HUD.
	 */
	UFUNCTION(BlueprintPure, Category = "Tactics|Control")
	EInteractionKind GetAvailableInteraction() const;

	UPROPERTY(BlueprintAssignable, Category = "Tactics|Control")
	FOnSelectedUnitChanged OnSelectedUnitChanged;

	/** Смена юнита под курсором (показ/скрытие панели цели с шансом попадания). */
	UPROPERTY(BlueprintAssignable, Category = "Tactics|Control")
	FOnHoveredUnitChanged OnHoveredUnitChanged;

	/**
	 * Набор доступных действий выбранного бойца изменился НЕ из-за смены выбора
	 * или трат AP, а из-за его новой позиции (добежал до бомбы/зоны эвакуации,
	 * сменился шанс попадания по цели под курсором). HUD пересчитывает по нему
	 * серость кнопок и панель цели: иначе кнопка F оставалась бы серой у самой
	 * бомбы — AP списываются в момент приказа, а доехал юнит куда позже.
	 */
	UPROPERTY(BlueprintAssignable, Category = "Tactics|Control")
	FOnAvailableActionsChanged OnAvailableActionsChanged;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void SetupInputComponent() override;
	virtual void PlayerTick(float DeltaTime) override;

	// --- Обработчики Enhanced Input -------------------------------------------

	void HandleSelectPressed();                       // ЛКМ
	void HandleCommandPressed();                      // ПКМ
	void HandleCameraPan(const FInputActionValue& Value);
	void HandleCameraRotate(const FInputActionValue& Value);
	void HandleCameraZoom(const FInputActionValue& Value);

	// --- Логика приказов -------------------------------------------------------

	/**
	 * Общая реализация смены выбора. bPlayerInitiated отличает клик/хоткей от
	 * автовыбора XCOM: только пользовательский выбор проходит Action Gate и
	 * публикует quest-событие, иначе шаг A1 закрывался бы сам собой.
	 */
	void SelectUnitInternal(AUnitBase* Unit, bool bPlayerInitiated);

	/** Новая политика шага могла запретить автоматический выбор бойца. */
	UFUNCTION()
	void HandleTutorialPolicyChanged();

	/** Точка маршрута шага пройдена — перерисовать маркеры, не трогая выбор. */
	UFUNCTION()
	void HandleTutorialDestinationsChanged();

	/** Декали-маркеры разрешённых точек перемещения активного шага обучения. */
	void RefreshTutorialDestinationMarkers();

	/** Разрешает ли Action Gate сделать этого бойца выбранным. */
	bool IsUnitSelectableByGate(const AUnitBase* Unit) const;

	/** Приказ перемещения в точку (валидация бюджета пути, списание AP). */
	void TryMoveSelectedUnit(const FVector& Goal);

	/** Приказ атаки цели (шлёт Event.Attack; GA сама валидирует и платит). */
	void TryAttackTarget(AActor* Target);

	/** Клик в режиме таргетинга способности (медик выбирает союзника). */
	void HandleAbilityTargetClick(AActor* ClickedActor);

	/**
	 * Обработать клик по врагу в Attack-targeting. true = клик полностью
	 * потреблён; false = это не вражеский юнит, режим нужно отменить.
	 */
	bool HandleAttackTargetClick(AActor* ClickedActor);

	/**
	 * ЕДИНСТВЕННОЕ место с приоритетом интеракций (бомба рядом → эвакуация в зоне):
	 * возвращает вид и найденный объект. Зовут GetAvailableInteraction (для HUD)
	 * и RequestInteract (исполнение) — порядок не может разъехаться.
	 */
	EInteractionKind FindAvailableInteraction(ABombObjective*& OutBomb, AEvacZone*& OutZone) const;

	/** Идёт бой и сейчас фаза врага (наш выбор/ховер своих скрываем). */
	bool IsEnemyPhaseNow() const;

	/** Обновить видимость кольца выбранного юнита по текущей фазе. */
	void RefreshSelectionHighlight();

	/**
	 * Обновить зону хода под выбранного юнита (или спрятать). Синхронно и сразу:
	 * навмеш статичен, занятость юнитов — дисками на уровне запросов
	 * (UTacticsCombatStatics::GetUnitObstacles), гонок с перестройкой тайлов нет.
	 */
	void RefreshMoveRange();

	/** Превью пути к точке под курсором (лента; троттлинг по сдвигу курсора). */
	void UpdatePathPreviewUnderCursor();

	/** Обводка юнита под курсором (Custom Depth; трейс по Pawn-каналу каждый тик). */
	void UpdateHoverHighlight();

	/**
	 * Что под курсором, с приоритетом ЮНИТА над геометрией.
	 *
	 * ⚠️ Двух трейсов не избежать: у павших (раненый/труп) капсула снята с канала
	 * `Pawn`, чтобы живые пробегали сквозь тело, и одиночный трейс по `Pawn`
	 * пролетает мимо — в пол. Тогда по упавшему бойцу нельзя ни навестись, ни
	 * кликнуть, то есть медик не может выбрать его для подъёма. Второй трейс идёт
	 * по `Visibility`, где павший остаётся блокирующим.
	 */
	bool TraceUnderCursor(FHitResult& OutHit) const;

	/**
	 * Отладка укрытия (`xru1.LOS.Debug 1`): стрелка на стену + границы защитной
	 * дуги. Всё, что вне сектора, — фланг. Без этого «как идёт укрытие» на глаз
	 * не понять, а именно от ориентации стены зависит жёлтый щит.
	 */
	void DrawCoverSidesDebug(const AActor* Unit) const;

	/** Панорама камеры мышью у края экрана (XCOM edge scrolling). */
	void UpdateEdgeScroll();

	/** Выполняет ли выбранный юнит приказ перемещения прямо сейчас. */
	bool IsSelectedUnitMoving() const;

	/** Колбэк смены фазы: блокировка/разблокировка, сброс выбора. */
	UFUNCTION()
	void HandleTurnStarted(ETurnPhase Phase);

	/** Колбэк начала хода вражеского юнита: наводим на него камеру (как в XCOM). */
	UFUNCTION()
	void HandleEnemyUnitActivated(AActor* Unit);

	/** Видит ли ХОТЬ ОДИН живой боец отряда этого актора (порог + линия огня). */
	bool IsVisibleToSquad(const AActor* Unit) const;

public:
	/**
	 * ПРЕВЬЮ ТОЧКИ (XCOM): что выбранный боец получит, встав сюда — сколько
	 * врагов простреливает точку, от скольких укрытие работает, каким оно будет
	 * и скольких он оттуда фланкирует.
	 *
	 * ⚠️ Считается ТЕМ ЖЕ кодом, что и настоящий бой, — иначе превью врало бы.
	 * Зовётся из HUD при наведении; стоимость — по врагу трейс укрытия плюс
	 * линия огня, поэтому вызывать по смене точки, а не каждый кадр.
	 */
	UFUNCTION(BlueprintPure, Category = "Tactics|Control")
	FTacticalMovePreview GetMovePreviewAt(const FVector& Location) const;

protected:

	/**
	 * Действующий враг, которого отряд ПОКА не видит. XCOM показывает вражеский
	 * ход не «если видели в момент активации», а как только враг попал в поле
	 * зрения — выбежал из-за угла, и камера подхватила его на бегу. Прежняя
	 * одноразовая проверка в `HandleEnemyUnitActivated` этот случай теряла
	 * целиком: враг весь ход оставался за кадром, хотя игрок его уже видел.
	 * Дожимается в `PlayerTick` с тем же троттлингом, что и дебаг LOS.
	 */
	TWeakObjectPtr<AActor> PendingEnemyCameraUnit;
	float LastEnemyVisibilityCheckTime = -1000.f;

	/** Реакционный выстрел сейчас играется — второй наблюдатель ждёт своей очереди. */
	bool bReactionPlaying = false;

	/** Time dilation до входа в reaction-window: чужая presentation-система могла уже изменить время. */
	float TimeDilationBeforeReaction = 1.f;

	/** Завершение presentation-окна реакции: вернуть время и снять camera-slot. */
	void EndReactionWindow();

	/** Навести камеру на центр живого отряда (без выбора юнита). */
	void FocusCameraOnSquad(bool bInstant = false);

	/** Колбэк трат AP выбранного юнита — перестроить зону хода. */
	UFUNCTION()
	void HandleSelectedUnitAPChanged(int32 NewCurrent, int32 Max);

	/** Колбэк смены состояния выбранного юнита: погиб/эвакуирован — снять выбор. */
	UFUNCTION()
	void HandleSelectedUnitStateChanged();

	// --- Настройки (BP-наследник) ----------------------------------------------

	/** Контекст ввода тактического боя (IMC_Tactical). */
	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputMappingContext> TacticalInputContext;

	UPROPERTY(EditDefaultsOnly, Category = "Input") TObjectPtr<UInputAction> SelectAction;      // ЛКМ
	UPROPERTY(EditDefaultsOnly, Category = "Input") TObjectPtr<UInputAction> CommandAction;     // ПКМ
	UPROPERTY(EditDefaultsOnly, Category = "Input") TObjectPtr<UInputAction> EndTurnAction;     // Enter
	UPROPERTY(EditDefaultsOnly, Category = "Input") TObjectPtr<UInputAction> AttackAction;      // Пробел: вход/выход прицеливания (toggle)
	UPROPERTY(EditDefaultsOnly, Category = "Input") TObjectPtr<UInputAction> OverwatchAction;   // Y
	UPROPERTY(EditDefaultsOnly, Category = "Input") TObjectPtr<UInputAction> HunkerAction;      // X
	UPROPERTY(EditDefaultsOnly, Category = "Input") TObjectPtr<UInputAction> ClassAbilityAction;// R
	UPROPERTY(EditDefaultsOnly, Category = "Input") TObjectPtr<UInputAction> InteractAction;    // F
	UPROPERTY(EditDefaultsOnly, Category = "Input") TObjectPtr<UInputAction> SkipTurnAction;    // Backspace
	UPROPERTY(EditDefaultsOnly, Category = "Input") TObjectPtr<UInputAction> NextUnitAction;    // Tab
	UPROPERTY(EditDefaultsOnly, Category = "Input") TObjectPtr<UInputAction> PauseAction;       // Esc
	UPROPERTY(EditDefaultsOnly, Category = "Input") TObjectPtr<UInputAction> CameraPanAction;   // WASD (Axis2D)
	UPROPERTY(EditDefaultsOnly, Category = "Input") TObjectPtr<UInputAction> CameraRotateAction;// Q/E (Axis1D)
	UPROPERTY(EditDefaultsOnly, Category = "Input") TObjectPtr<UInputAction> CameraZoomAction;  // колесо

	/** Слоты выбора юнитов (клавиши 1–4): элемент i выбирает юнита слота i. */
	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TArray<TObjectPtr<UInputAction>> SelectSlotActions;

	/** Класс корневого UI-слоя (создаётся в BeginPlay через UIManager). */
	UPROPERTY(EditDefaultsOnly, Category = "UI")
	TSubclassOf<UPrimaryGameLayout> RootLayoutClass;

	/** Экран паузы (пушится на слой Menu по Esc). */
	UPROPERTY(EditDefaultsOnly, Category = "UI")
	TSubclassOf<UMenuScreenBase> PauseMenuClass;

	/** Класс визуализатора зоны хода (BP с материалами зон). */
	UPROPERTY(EditDefaultsOnly, Category = "Tactics|Control")
	TSubclassOf<AMoveRangeVisualizer> MoveRangeVisualizerClass;

	/** Панорама мышью у края экрана (XCOM). Выключается в BP при желании. */
	UPROPERTY(EditDefaultsOnly, Category = "Tactics|Control")
	bool bEdgeScrollEnabled = true;

	/**
	 * XCOM-автовыбор бойцов: активный боец есть всегда. Включает: автовыбор в
	 * начале фазы игрока, переход к следующему бойцу после смерти/эвакуации
	 * выбранного и после исчерпания его AP (действие завершило активацию).
	 * Выключается в BP при желании — тогда выбор полностью ручной.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Tactics|Control")
	bool bAutoSelectUnits = true;

	/**
	 * Текущего бойца выбрал игрок, а не автоматика. Шаг обучения «выбери такого-то»
	 * отличает одно от другого: автовыбор не должен закрывать его за игрока.
	 */
	bool bSelectionByPlayer = false;

	/** Slate-оверлей подсказок обучения (без WBP). */
	TSharedPtr<class STutorialHintOverlay> TutorialHintOverlay;

	/** Живые декали-маркеры точек назначения текущего шага. */
	TArray<TWeakObjectPtr<class UDecalComponent>> TutorialDestinationMarkers;

	/** Круг дальности способности на время Ability-targeting. */
	TWeakObjectPtr<class UDecalComponent> AbilityRangeDecal;

	/** Цели, подсвеченные кольцом на время Ability-targeting. */
	TArray<TWeakObjectPtr<AUnitBase>> AbilityHighlightedTargets;

	/** Круг радиуса + подсветка валидных целей при входе в Ability-режим. */
	void BeginAbilityTargetingVisuals();
	void ClearAbilityTargetingVisuals();

	/** Кольца «встань сюда» вокруг Downed союзников, пока выбран медик. */
	TArray<TWeakObjectPtr<class UDecalComponent>> ReviveRingDecals;

	/**
	 * Показ/скрытие колец радиуса подъёма у лежащих союзников. Игрок в A9 видит
	 * при ДВИЖЕНИИ медика, куда именно нужно дойти (радиус лечения 200 см), а не
	 * угадывает «вплотную». Перестраивается при смене выбора и политики шага.
	 */
	void RefreshDownedReviveRings();

	/** Последний отказ Action Gate — оверлей показывает его 3 секунды. */
	FText LastDenialReason;
	float LastDenialTimeSeconds = -100.f;

	/** Стартовую камеру поставил сценарий — не перебивать автофокусом отряда. */
	bool bScenarioCameraPlaced = false;

	/**
	 * Прошлая политика шага была lock-постановкой. Переход lock → не-lock
	 * возвращает камеру игроку (к выбранному бойцу или отряду): во время
	 * постановки все автовозвраты фокуса подавлены, и без этого перехода
	 * камера оставалась бы там, где закончилась режиссура.
	 */
	bool bTutorialPolicyWasLock = false;

	/**
	 * Автоматически завершать ход игрока, когда AP не осталось НИ У КОГО из
	 * отряда (XCOM-темп: не заставляем жать «Завершить ход» вручную). Выключить
	 * в BP, если нужен явный контроль конца хода.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Tactics|Control")
	bool bAutoEndTurnWhenExhausted = true;

	/** Ширина зоны edge scroll от края вьюпорта, px. */
	UPROPERTY(EditDefaultsOnly, Category = "Tactics|Control", meta = (ClampMin = "2", ClampMax = "100"))
	float EdgeScrollMarginPx = 16.f;

	// --- Состояние ---------------------------------------------------------------

	UPROPERTY(Transient)
	TObjectPtr<AUnitBase> SelectedUnit;

	UPROPERTY(Transient)
	TObjectPtr<AMoveRangeVisualizer> MoveRangeVisualizer;

	/** Юнит под курсором (с обводкой). Weak: юнит может умереть/исчезнуть между тиками. */
	TWeakObjectPtr<AUnitBase> HoveredUnit;

	/** ЕДИНЫЙ источник правды о режиме взаимодействия (см. EPlayerTargetingMode). */
	EPlayerTargetingMode TargetingMode = EPlayerTargetingMode::None;

	/** Взятая на прицел цель в режиме Attack (Tab листает, HUD показывает шанс по ней). */
	TWeakObjectPtr<AUnitBase> CurrentAttackTarget;

	/**
	 * ЕДИНСТВЕННАЯ точка смены режима: откатывает побочные эффекты старого
	 * (ExitTargetingMode) и включает новые (EnterTargetingMode). Все переходы
	 * обязаны идти через неё — тогда камера/подсветка/баннер не «зависнут».
	 */
	void SetTargetingMode(EPlayerTargetingMode NewMode);

	/** Откат побочных эффектов покидаемого режима (камера, подсветка, баннер). */
	void ExitTargetingMode(EPlayerTargetingMode OldMode);

	/**
	 * Показ/скрытие оверхед-худов живого отряда (не трогает Downed/врагов).
	 * Прицеливание атаки прячет их: полосы своего бойца загораживали прицел.
	 */
	void SetSquadOverheadHUDVisible(bool bVisible);

	/** Декларативный владелец видимости худов отряда (PlayerTick). */
	void UpdateSquadOverheadVisibility();

	/** Текущее применённое состояние скрытия худов отряда. */
	bool bSquadOverheadHidden = false;

	/** Установка побочных эффектов входимого режима. */
	void EnterTargetingMode(EPlayerTargetingMode NewMode);

	/**
	 * Общий вход в режим прицеливания: собирает цели, берёт первую (или ближайшую
	 * к курсору) на прицел, наводит камеру. false — целей нет (в режим не входим,
	 * кнопка «Огонь» гаснет). Зовут кнопка «Огонь» и хоткей.
	 */
	bool BeginAttackTargeting();

	/** Взять КОНКРЕТНУЮ цель на прицел: запомнить, подсветить, навести кадр камеры. */
	void SetAttackTarget(AUnitBase* Target);

	/** Снять прицел и его подсветку (не трогая режим/камеру — это делают вызывающие). */
	void ClearAttackTarget();

	/**
	 * Если AP не осталось ни у кого из отряда и никто не бежит — завершить ход
	 * (авто-переход к врагу). Зовётся из точек, где AP только что кончились.
	 * Ничего не делает, пока хоть один боец может действовать или ещё в пути.
	 */
	void TryAutoEndTurn();

	/** Активна ли у юнита обычная fire action или вложенная reaction subaction. */
	bool IsUnitActionInProgress(const AUnitBase* Unit, FGuid* OutActionId = nullptr) const;

	/** Есть ли незавершённая fire/reaction action у любого живого бойца отряда. */
	bool IsAnySquadActionInProgress() const;

	/** Играет ли камера кадр выстрела прямо сейчас (автопереходы ждут его конца). */
	bool IsCameraFramingShot() const;

	/**
	 * Автопереход (следующий боец / конец хода) отложен до конца кадра выстрела:
	 * AP падают в ноль В МОМЕНТ выстрела, и мгновенный SelectNextUnit увёл бы
	 * камеру с кинематографичного кадра в тот же тик. Разрешается в PlayerTick.
	 */
	bool bPendingAutoAdvance = false;

	/**
	 * Номер кадра, на котором взведён bPendingAutoAdvance. PlayerTick НЕ разрешает
	 * переход в тот же кадр: кадр выстрела стартует синхронно чуть ПОЗЖE списания
	 * AP (ResolveShot после CommitAbility), и без этой метки отложенный переход
	 * успел бы сработать до начала кинематографа, снова оборвав его.
	 */
	uint64 PendingAutoAdvanceFrame = 0;

	/** Последняя точка превью пути (троттлинг перзапроса FindPath). */
	FVector LastPathPreviewGoal = FVector(TNumericLimits<float>::Max());

	/** Двигался ли выбранный юнит в прошлый тик (ловим остановку → перестроить зону). */
	bool bSelectedUnitWasMoving = false;

	/**
	 * Троттлинг непрерывной дебаг-отрисовки LOS (xru1.LOS.Debug): раз в
	 * LOSDebugInterval, а не каждый кадр — иначе лог (см. UE_LOG внутри
	 * HasLineOfSightFromLocation) захлёстывает при 60 FPS.
	 */
	float LastLOSDebugTime = -1000.f;
	static constexpr float LOSDebugInterval = 0.25f;


	/** Стартовый фокус камеры на отряд уже выполнен (не повторять каждый ход). */
	bool bInitialSquadFocusDone = false;
};
