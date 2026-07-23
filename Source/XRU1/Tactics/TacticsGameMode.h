#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "TacticsTypes.h"
#include "TacticsGameMode.generated.h"

class AUnitBase;
class ABombObjective;
class AEvacZone;
class UMissionResultWidget;
class UCommonActivatableWidget;

/** Параметры вражеской стороны для одного уровня сложности (GDD §10). */
USTRUCT(BlueprintType)
struct FTacticsDifficultyParams
{
	GENERATED_BODY()

	/** HP врага (переопределяет атрибут при старте). 0 = не менять. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Difficulty", meta = (ClampMin = "0"))
	float EnemyHealth = 100.f;

	/** Точность врага (BaseAim юнита). 0 = не менять. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Difficulty", meta = (ClampMin = "0", ClampMax = "100"))
	float EnemyAim = 65.f;

	/** Лимит ходов игрока до взрыва бомбы (0 = таймера нет). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Difficulty", meta = (ClampMin = "0"))
	int32 TurnLimit = 10;
};

/**
 * GameMode боевого уровня (туториал/миссия). Обязанности:
 *  - собрать юнитов карты по TacticsTeamIds и запустить бой;
 *  - применить сложность из сейва к врагам (HP/aim, таймер ходов при наличии
 *    бомбы) — GDD §10 (отсев части врагов по сложности НЕ реализован —
 *    состав врагов на карте фиксирован, сложность правит только их статы);
 *  - следить за целями: бомба обезврежена → снять таймер, включить зоны
 *    эвакуации; все живые эвакуированы → победа (GDD §5.7);
 *  - по концу боя показать экран результата и записать прогресс кампании.
 */
UCLASS(Blueprintable)
class XRU1_API ATacticsGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ATacticsGameMode();

	/** Id миссии для прогресса кампании («Tutorial» / «Mission01»). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Mission")
	FName MissionId = NAME_None;

	/**
	 * Победа при уничтожении всех врагов. Для миссий «цель + эвакуация»
	 * выключить: бой закончится только полной эвакуацией отряда.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Mission")
	bool bWinWhenAllEnemiesDead = true;

	/** Параметры сложности (заполняются в BP; ключи Easy/Medium/Hard). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Mission")
	TMap<EDifficultyLevel, FTacticsDifficultyParams> DifficultyParams;

	/** Экран результата (WBP от UMissionResultWidget). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|UI")
	TSubclassOf<UMissionResultWidget> MissionResultWidgetClass;

	/** Боевой HUD (WBP от UTacticalHUDWidget), пушится на слой Game. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|UI")
	TSubclassOf<UCommonActivatableWidget> TacticalHUDClass;

	/** Задержка перед StartCombat, чтобы BeginPlay юнитов/навмеша завершился. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Mission", meta = (ClampMin = "0"))
	float CombatStartDelay = 0.3f;

	/** Активирует все зоны эвакуации уровня (зовёт и скрипт туториала). */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Mission")
	void ActivateEvacuation();

	/** Проиграна ли миссия по таймеру (для текста экрана поражения). */
	UFUNCTION(BlueprintPure, Category = "Tactics|Mission")
	bool WasDefeatByTimeout() const { return bDefeatByTimeout; }

protected:
	virtual void BeginPlay() override;

	/** Сбор сторон, сложность, запуск боя. */
	void StartMissionCombat();

	/** Применяет параметры сложности к вражескому юниту. */
	void ApplyDifficultyToEnemy(AUnitBase* Enemy, const FTacticsDifficultyParams& Params);

	UFUNCTION()
	void HandleBombDisarmed();

	UFUNCTION()
	void HandleUnitEvacuated(AUnitBase* Unit);

	UFUNCTION()
	void HandleTurnLimitExpired();

	UFUNCTION()
	void HandleCombatEnded(bool bPlayerWon);

	/** Все живые юниты отряда эвакуированы? (Downed не блокируют победу — GDD §5.7.) */
	bool AreAllLivingPlayersEvacuated() const;

	/** Текущая сложность из сейва (Medium, если сейва нет — прямой запуск PIE). */
	EDifficultyLevel ResolveDifficulty() const;

	FTimerHandle StartCombatTimerHandle;
	bool bDefeatByTimeout = false;

	/** Отряд игрока на карте (кэш для проверки эвакуации). */
	UPROPERTY(Transient)
	TArray<TObjectPtr<AUnitBase>> PlayerUnits;
};
