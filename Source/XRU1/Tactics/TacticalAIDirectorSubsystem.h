#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "TacticalAIDirectorSubsystem.generated.h"

class AUnitBase;

/** Откуда AI узнал о противнике. Источник влияет на достоверность контакта. */
UENUM(BlueprintType)
enum class EAIContactSource : uint8
{
	/** Увидел сам — единственный источник, дающий полную достоверность. */
	Sight,
	/** Сообщил союзник по поду. */
	Ally,
	/** По нам стреляли: направление известно точно, даже если стрелка не видно. */
	Damage,
	/** Услышал выстрел поблизости — известна лишь точка шума. */
	Noise,
	/** Рядом погиб союзник. */
	AllyDeath
};

/**
 * Известный контакт с противником.
 *
 * До этого AI знал только «вижу прямо сейчас»: `GetCurrentlyPerceivedActors`
 * пуст — и боец мгновенно забывал бойца, в которого секунду назад стрелял.
 * Отсюда враги, застывающие столбом, пока по ним ведут огонь с дистанции
 * больше их радиуса зрения.
 *
 * Контакт НЕ даёт права стрелять: выстрел по-прежнему проверяется реальными
 * дальностью и линией огня. Он даёт право ЗНАТЬ, что противник там был.
 */
USTRUCT(BlueprintType)
struct XRU1_API FAIContact
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "AI|Contact")
	TWeakObjectPtr<AActor> Target;

	UPROPERTY(BlueprintReadOnly, Category = "AI|Contact")
	FVector LastKnownLocation = FVector::ZeroVector;

	/** Номер хода, когда контакт последний раз подтверждался. */
	UPROPERTY(BlueprintReadOnly, Category = "AI|Contact")
	int32 LastUpdatedTurn = 0;

	UPROPERTY(BlueprintReadOnly, Category = "AI|Contact")
	EAIContactSource Source = EAIContactSource::Sight;

	/** 0..1. Затухает с числом прошедших ходов; обновление возвращает к максимуму. */
	UPROPERTY(BlueprintReadOnly, Category = "AI|Contact")
	float Confidence = 1.f;

	/**
	 * Актор противника ИЗВЕСТЕН. false — «подозрение»: знаем только точку.
	 *
	 * Так устроен шум: выстрел за стеной даёт место, но не имя стрелка. Хранить
	 * для него `Target` было бы прямым нарушением честности §7 — боец получил бы
	 * ссылку на актора, которого не видел, и любая будущая правка могла бы
	 * прочитать его ЖИВУЮ позицию вместо запомненной.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "AI|Contact")
	bool bActorKnown = true;

	bool IsValidContact() const
	{
		return Confidence > 0.f && (!bActorKnown || Target.IsValid());
	}
};

/** Состояние одной группы (пода) врагов. */
USTRUCT()
struct FAIPodState
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<TWeakObjectPtr<AUnitBase>> Members;

	UPROPERTY()
	TArray<FAIContact> Contacts;

	/** Под вступил в бой: его бойцы действуют, а не стоят на посту. */
	UPROPERTY()
	bool bActivated = false;

	/** Ход активации — для «разбегания» в первый ход после вскрытия. */
	UPROPERTY()
	int32 ActivatedTurn = 0;
};

/**
 * Групповая активация врагов и общая память контактов — XCOM-овские «поды».
 *
 * В XCOM 2 враги стоят группами: как только незамаскированный боец получает
 * линию видимости на ЛЮБОГО члена группы, активируется вся группа целиком, а
 * остальные группы на карте продолжают ничего не знать. Дополнительно поды
 * поднимаются по звуку боя и по виду погибшего союзника.
 *
 * Здесь воспроизведён тот же принцип. Ключевое:
 * знание принадлежит ПОДУ, а не отдельному бойцу. Иначе каждый враг узнаёт о
 * противнике только собственными глазами, и группа из четырёх бойцов ведёт себя
 * как четыре независимых слепых часовых.
 */
UCLASS()
class XRU1_API UTacticalAIDirectorSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	// --- Регистрация ---------------------------------------------------------

	/** Ставит юнита в его под. Пустой `PodId` = собственный под из одного бойца. */
	void RegisterUnit(AUnitBase* Unit);

	/** Идентификатор пода юнита с учётом правила «пустой = одиночный». */
	UFUNCTION(BlueprintPure, Category = "Tactics|AI|Pods")
	static FName ResolvePodId(const AUnitBase* Unit);

	// --- Триггеры активации --------------------------------------------------

	/** Боец пода увидел противника: под вскрыт, контакт становится общим. */
	UFUNCTION(BlueprintCallable, Category = "Tactics|AI|Pods")
	void NotifyEnemySpotted(AUnitBase* Spotter, AActor* Target);

	/**
	 * По бойцу попали. Активирует под безусловно: получить пулю и продолжить
	 * стоять на посту — самый заметный дефект «мёртвого» AI.
	 */
	UFUNCTION(BlueprintCallable, Category = "Tactics|AI|Pods")
	void NotifyUnitDamaged(AUnitBase* Victim, AActor* Instigator);

	/** Боец погиб: его под и соседние поды в радиусе поднимаются. */
	UFUNCTION(BlueprintCallable, Category = "Tactics|AI|Pods")
	void NotifyUnitKilled(AUnitBase* Victim, AActor* Instigator);

	/**
	 * Шум боя. По правилам XCOM звук ПОДНИМАЕТ группу (жёлтая тревога), но не
	 * вскрывает её: бойцы идут проверять точку, а не мгновенно знают стрелка.
	 */
	UFUNCTION(BlueprintCallable, Category = "Tactics|AI|Pods")
	void NotifyCombatNoise(AActor* Instigator, const FVector& Location, float Radius);

	// --- Запросы -------------------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "Tactics|AI|Pods")
	bool IsPodActivated(FName PodId) const;

	UFUNCTION(BlueprintPure, Category = "Tactics|AI|Pods")
	bool IsUnitPodActivated(const AUnitBase* Unit) const;

	/** Живые контакты пода, отсортированные по убыванию достоверности. */
	void GetPodContacts(FName PodId, TArray<FAIContact>& OutContacts) const;

	/** Самый достоверный живой контакт пода. */
	bool GetBestContact(const AUnitBase* Unit, FAIContact& OutContact) const;

	/** Забыть контакты старше `ContactMemoryTurns`. Зовётся на смене хода. */
	void AgeContacts(int32 CurrentTurn);

	// --- Резервации позиций -------------------------------------------
	//
	// ⚠️ ЗАЧЕМ ЭТО НУЖНО, ЕСЛИ ХОДЫ ПОСЛЕДОВАТЕЛЬНЫЕ.
	//
	// Диски занятости (`UTacticsCombatStatics::GetUnitObstacles`) защищают от
	// СТОЯЩИХ бойцов, но они намеренно не видят того, кто СЕЙЧАС В ПУТИ: иначе
	// бегущий блокировал бы сам себя. Пока враг ходит по одному, дыра невелика,
	// но она есть и уже наблюдаема:
	//
	//  - боец A выбрал точку и пошёл к ней двухходовым манёвром. Его активация
	//    продолжается на следующем шаге, а диск занятости он не ставит;
	//  - реакционный выстрел Overwatch может прервать A и передать ход дальше;
	//  - продолжение манёвра A на следующем ходу целится в ТУ ЖЕ точку, которую
	//    за это время мог занять B.
	//
	// Резервация закрывает ровно этот промежуток: точка помечена за бойцом с
	// момента ВЫБОРА и до фактического финиша или срыва. Это и есть минимальный
	// упрощённый координатор — без ролей и без бюджета, только «не выдавать
	// двум бойцам одну cover anchor».

	/**
	 * Закрепляет точку за юнитом. Прежняя резервация того же юнита снимается:
	 * одно намерение на бойца, иначе брошенные точки копились бы до конца боя.
	 */
	UFUNCTION(BlueprintCallable, Category = "Tactics|AI|Reservations")
	void ReservePosition(AUnitBase* Unit, const FVector& Point);

	/** Снимает резервацию юнита (финиш, срыв, смерть, конец хода). */
	UFUNCTION(BlueprintCallable, Category = "Tactics|AI|Reservations")
	void ReleaseReservation(const AUnitBase* Unit);

	/**
	 * Занята ли точка чужой резервацией. `Requester` исключается — свою же
	 * точку боец обязан считать свободной, иначе продолжение манёвра отвергло
	 * бы собственную цель.
	 */
	UFUNCTION(BlueprintPure, Category = "Tactics|AI|Reservations")
	bool IsPositionReserved(const AUnitBase* Requester, const FVector& Point, float Radius) const;

	/** Сбрасывает все резервации (смена фазы, конец боя). */
	void ClearReservations();

	// --- Тюнинг --------------------------------------------------------------

	/** Радиус, в котором смерть союзника поднимает соседние поды, см. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Tactics|AI|Pods")
	float DeathAlertRadius = 2500.f;

	/** Сколько ходов держится контакт без подтверждения. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Tactics|AI|Pods")
	int32 ContactMemoryTurns = 3;

	/**
	 * Радиус, в котором чужая резервация делает точку недоступной (см).
	 * По умолчанию — примерно два просвета `GetUnitClearance`: двое не должны
	 * целиться в одну и ту же сторону одного укрытия.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Tactics|AI|Reservations",
		meta = (ClampMin = "0"))
	float ReservationRadius = 200.f;

protected:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

private:
	/** Добавляет/обновляет контакт пода и возвращает true, если он новый. */
	bool AddContact(FName PodId, AActor* Target, const FVector& Location,
		EAIContactSource Source, float Confidence);

	/** Переводит под в бой и сообщает его бойцам. Возвращает true при смене состояния. */
	bool ActivatePod(FName PodId, const TCHAR* Reason);

	/**
	 * Есть ли в поде хоть один боец вражеской стороны. Регистрация идёт без
	 * фильтра команды (TeamId в OnPossess ещё не назначен), поэтому поды заводят
	 * и бойцы игрока — вскрывать и поднимать их нельзя.
	 */
	static bool IsEnemyPod(const struct FAIPodState& Pod);

	int32 GetCurrentTurn() const;

	UPROPERTY()
	TMap<FName, FAIPodState> Pods;

	/** Намерения бойцов: юнит → точка, куда он уже направляется. */
	TMap<TWeakObjectPtr<const AUnitBase>, FVector> Reservations;
};
