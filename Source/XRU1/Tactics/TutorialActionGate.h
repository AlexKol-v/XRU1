#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "TutorialActionGate.generated.h"

class AActor;

/**
 * Команды игрока с точки зрения обучения. Это НЕ дубликат
 * ETacticalPlayerCommand: gate ограничивает намерение («сейчас можно только
 * двигать Медика»), а контроллер по-прежнему сам проверяет AP, заряды и цели.
 */
UENUM(BlueprintType)
enum class ETutorialAction : uint8
{
	/** Смена выбранного бойца. */
	Select,
	/** Панорама/поворот/зум камеры — gate их никогда не блокирует. */
	Camera,
	Move,
	EndTurn,
	Attack,
	ClassAbility,
	Overwatch,
	Hunker,
	Interact
};

/**
 * Политика одного шага обучения. Пустой список = «не ограничиваю»: политика
 * задаёт ТОЛЬКО то, что важно шагу, и не обязана перечислять всё остальное.
 */
USTRUCT(BlueprintType)
struct XRU1_API FTutorialActionPolicy
{
	GENERATED_BODY()

	/** Разрешённые команды. Пусто — разрешены все (кроме bLockGameplayInput). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tutorial")
	TArray<ETutorialAction> AllowedActions;

	/** AnchorId бойцов, которым разрешено действовать. Пусто — любой. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tutorial")
	TArray<FName> AllowedUnitAnchors;

	/** AnchorId допустимых целей атаки/способности. Пусто — любая. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tutorial")
	TArray<FName> AllowedTargetAnchors;

	/** AnchorId допустимых точек назначения перемещения. Пусто — любая точка. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tutorial")
	TArray<FName> AllowedDestinationAnchors;

	/**
	 * Владелец точки: якорь точки → якорь бойца, которому она предназначена.
	 * Точка без записи — общая. Танк не может занять точку Осы, а маркеры
	 * показывают выбранному бойцу только его маршрут.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tutorial")
	TMap<FName, FName> DestinationOwners;

	/** Радиус приёмки точки назначения вокруг якоря, см. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tutorial", meta = (ClampMin = "50"))
	float DestinationTolerance = 300.f;

	/**
	 * Точки маршрута открываются по очереди: разрешена только первая, куда боец
	 * ещё не приходил. Без этого шаг «два перемещения» подсвечивает обе точки
	 * сразу, игрок уходит на дальнюю одним рывком, и порядок маршрута теряется.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tutorial")
	bool bSequentialDestinations = false;

	/**
	 * Владелец ДЕЙСТВИЯ: действие → якорь бойца, которому оно разрешено.
	 * Действие без записи подчиняется общим правилам. Решает «Кадет нажал
	 * Наблюдение вместо Танка»: в C0 Overwatch→Танк, Hunker→Кадет.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tutorial")
	TMap<ETutorialAction, FName> ActionOwners;

	/**
	 * Scripted-действие: весь геймплейный ввод закрыт. Камера и пауза остаются —
	 * игрок должен иметь возможность рассмотреть сцену и выйти в меню.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tutorial")
	bool bLockGameplayInput = false;

	/**
	 * «Сначала займи позицию»: пока у бойца остаются открытые точки шага (его
	 * личные или общие), способности и атака ему запрещены — только Move/Select/
	 * EndTurn. Танк, нажавший Провокацию посреди поля, ломал постановку B4:
	 * голограмма не видела его с точки выстрела.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tutorial")
	bool bRequirePositionBeforeActions = true;

	/** Короткая причина отказа для HUD («Сейчас действует только Медик»). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tutorial")
	FText DenialReason;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnTutorialPolicyChanged);

/**
 * Единственный арбитр «что сейчас разрешено» в обучении (docs/03_ARCHITECTURE.md §9).
 *
 * StateTree описывает, ЧЕГО мы ждём, но не должен становиться системой ввода:
 * hotkey и кнопка HUD спрашивают один и тот же CanIssueAction ДО изменения мира,
 * поэтому отказ не тратит AP, не запускает montage и не публикует quest-событие.
 * Серость кнопок — представление того же ответа, а не отдельные BP-условия.
 *
 * WorldSubsystem: политика умирает вместе с World, поэтому retry и travel не
 * могут оставить наглухо закрытый ввод в новом запуске.
 */
UCLASS()
class XRU1_API UTutorialActionGateSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/**
	 * Ставит политику текущего шага и возвращает её токен. Токен нужен, чтобы
	 * ExitState уже вытесненной задачи не снял политику следующего шага.
	 */
	UFUNCTION(BlueprintCallable, Category = "Tutorial|Gate")
	int32 ApplyPolicy(const FTutorialActionPolicy& NewPolicy);

	/** Снимает политику, только если токен всё ещё актуален. */
	UFUNCTION(BlueprintCallable, Category = "Tutorial|Gate")
	bool ClearPolicy(int32 PolicyToken);

	/** Аварийный сброс (конец сценария, переход в Hub). */
	UFUNCTION(BlueprintCallable, Category = "Tutorial|Gate")
	void ClearAllPolicies();

	UFUNCTION(BlueprintPure, Category = "Tutorial|Gate")
	bool IsGateActive() const { return bPolicyActive; }

	/**
	 * Шаг требует, чтобы бойца выбрал именно игрок: Select разрешён и сужен до
	 * конкретных якорей. Автовыбор в начале фазы в таком шаге запрещён — иначе
	 * он сам поставит нужного бойца, повторный клик по нему уже ничего не
	 * изменит, и подтверждённое `Unit.Selected` не придёт никогда.
	 */
	UFUNCTION(BlueprintPure, Category = "Tutorial|Gate")
	bool RequiresExplicitUnitSelection() const;

	/** Политика шага сменилась (применена или снята). */
	UPROPERTY(BlueprintAssignable, Category = "Tutorial|Gate")
	FOnTutorialPolicyChanged OnPolicyChanged;

	/**
	 * Изменился набор открытых точек внутри ТОГО ЖЕ шага. Отдельный делегат:
	 * смена политики снимает автоматический выбор бойца, а прохождение точки
	 * маршрута — не должно, иначе шаг с Select терял бы выбор на каждом шаге.
	 */
	UPROPERTY(BlueprintAssignable, Category = "Tutorial|Gate")
	FOnTutorialPolicyChanged OnDestinationsChanged;

	/** Причина последнего отказа — для подсказки HUD. */
	UFUNCTION(BlueprintPure, Category = "Tutorial|Gate")
	FText GetDenialReason() const { return ActivePolicy.DenialReason; }

	/** Активная политика (валидна только при IsGateActive) — для маркеров HUD. */
	const FTutorialActionPolicy& GetActivePolicy() const { return ActivePolicy; }

	/**
	 * Разрешена ли команда этому бойцу. Unit == nullptr означает «проверить
	 * только сам тип действия» (серость кнопки без выбранного бойца).
	 */
	UFUNCTION(BlueprintPure, Category = "Tutorial|Gate")
	bool IsActionAllowed(ETutorialAction Action, const AActor* Unit) const;

	/** Разрешена ли цель атаки/способности. */
	UFUNCTION(BlueprintPure, Category = "Tutorial|Gate")
	bool IsTargetAllowed(const AActor* Target) const;

	/** Разрешена ли точка назначения перемещения ЭТОМУ бойцу (nullptr — любому). */
	UFUNCTION(BlueprintPure, Category = "Tutorial|Gate")
	bool IsDestinationAllowed(const FVector& Destination, const AActor* Unit) const;

	/**
	 * AnchorId точек, куда сейчас разрешено идти — для маркеров HUD.
	 * ForUnit сужает список до его личных и общих точек; nullptr — все открытые.
	 */
	UFUNCTION(BlueprintPure, Category = "Tutorial|Gate")
	TArray<FName> GetOpenDestinationAnchors(const AActor* ForUnit) const;

	/**
	 * Боец подтверждённо встал в одну из разрешённых точек — она «израсходована».
	 * Зовёт единственная финализация перемещения, та же, что публикует
	 * Movement.Settled.*: до неё точка не считается пройденной. Чужая точка
	 * приходом мимо проходящего бойца не гасится.
	 */
	UFUNCTION(BlueprintCallable, Category = "Tutorial|Gate")
	void NotifyDestinationReached(const FVector& Location, const AActor* Unit);

	/** Удобный статический доступ из контроллера/HUD/задач. */
	static UTutorialActionGateSubsystem* Get(const UObject* WorldContextObject);

	/** Сокращение: gate отсутствует или разрешает действие. */
	static bool AllowsAction(const UObject* WorldContextObject,
		ETutorialAction Action, const AActor* Unit);

private:
	/** Есть ли AnchorId актора в списке; пустой список означает «любой». */
	bool MatchesAnchorList(const TArray<FName>& Anchors, const AActor* Actor) const;

	/** Ближайший к точке разрешённый якорь и расстояние до него. */
	FName FindNearestAllowedAnchor(const FVector& Location, float& OutDistance) const;

	FTutorialActionPolicy ActivePolicy;
	int32 ActivePolicyToken = 0;
	bool bPolicyActive = false;

	/** Якоря шага, куда боец уже приходил: повторно они не разрешаются. */
	TArray<FName> ConsumedDestinations;
};
