// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "QuestTypes.generated.h"

/** Состояние квеста в его жизненном цикле. */
UENUM(BlueprintType)
enum class EQuestState : uint8
{
    Inactive   UMETA(DisplayName = "Неактивен"),
    Available  UMETA(DisplayName = "Доступен"),
    Active     UMETA(DisplayName = "Активен"),
    Completed  UMETA(DisplayName = "Завершён"),
    Failed     UMETA(DisplayName = "Провален")
};

/** Состояние отдельной цели квеста. */
UENUM(BlueprintType)
enum class EObjectiveState : uint8
{
    NotStarted UMETA(DisplayName = "Не начата"),
    Active     UMETA(DisplayName = "Активна"),
    Completed  UMETA(DisplayName = "Завершена"),
    Failed     UMETA(DisplayName = "Провалена")
};

/** Категория квеста — для группировки и фильтрации в журнале. */
UENUM(BlueprintType)
enum class EQuestCategory : uint8
{
    Main       UMETA(DisplayName = "Основной"),
    Side       UMETA(DisplayName = "Побочный"),
    Daily      UMETA(DisplayName = "Ежедневный"),
    Repeatable UMETA(DisplayName = "Повторяемый"),
    Hidden     UMETA(DisplayName = "Скрытый")
};

/** Сложность квеста — для сортировки в журнале. */
UENUM(BlueprintType)
enum class EQuestDifficulty : uint8
{
    Trivial UMETA(DisplayName = "Тривиальная"),
    Easy    UMETA(DisplayName = "Лёгкая"),
    Normal  UMETA(DisplayName = "Обычная"),
    Hard    UMETA(DisplayName = "Сложная"),
    Epic    UMETA(DisplayName = "Эпическая")
};

/** Тип уведомления о событии квеста — для тостов UI. */
UENUM(BlueprintType)
enum class EQuestNotificationType : uint8
{
    Started   UMETA(DisplayName = "Начат"),
    Completed UMETA(DisplayName = "Завершён"),
    Failed    UMETA(DisplayName = "Провален")
};

/** Критерий сортировки квестов в журнале. */
UENUM(BlueprintType)
enum class EQuestSortMode : uint8
{
    ByState      UMETA(DisplayName = "По состоянию"),
    ByDifficulty UMETA(DisplayName = "По сложности"),
    ByDuration   UMETA(DisplayName = "По длительности")
};

/**
 * Прогресс одной цели квеста — лёгкий наблюдаемый и сохраняемый снимок.
 * Заполняется задачами State Tree и читается UI.
 */
USTRUCT(BlueprintType)
struct FObjectiveProgress
{
    GENERATED_BODY()

    /** Идентификатор цели. */
    UPROPERTY(BlueprintReadOnly, Category = "Quest")
    FGameplayTag ObjectiveId;

    /** Текущее состояние цели. */
    UPROPERTY(BlueprintReadOnly, Category = "Quest")
    EObjectiveState State = EObjectiveState::NotStarted;

    /** Текущее значение счётчика. */
    UPROPERTY(BlueprintReadOnly, Category = "Quest")
    int32 Current = 0;

    /** Требуемое значение счётчика для завершения цели. */
    UPROPERTY(BlueprintReadOnly, Category = "Quest")
    int32 Required = 1;

    /** Описание цели для журнала и трекера. */
    UPROPERTY(BlueprintReadOnly, Category = "Quest")
    FText Description;
};

/**
 * Прогресс одного квеста — лёгкий наблюдаемый и сохраняемый снимок.
 * Это «инстанс» в расколе Definition/Instance: определение неизменяемо,
 * прогресс мутабелен.
 */
USTRUCT(BlueprintType)
struct FQuestProgress
{
    GENERATED_BODY()

    /** Идентификатор квеста (совпадает с UQuestDefinition::QuestId). */
    UPROPERTY(BlueprintReadOnly, Category = "Quest")
    FGameplayTag QuestId;

    /** Текущее состояние квеста. */
    UPROPERTY(BlueprintReadOnly, Category = "Quest")
    EQuestState State = EQuestState::Inactive;

    /** Прогресс целей квеста. */
    UPROPERTY(BlueprintReadOnly, Category = "Quest")
    TArray<FObjectiveProgress> Objectives;

    /** Доля выполнения квеста в диапазоне 0..1. */
    UPROPERTY(BlueprintReadOnly, Category = "Quest")
    float CompletionPercent = 0.f;
};

/**
 * Сообщение квест-события, рассылаемое через GMS (UGameplayMessageSubsystem).
 * Геймплейный код броадкастит его на канале Quest.Event.* ; раннер квеста
 * ретранслирует его в State Tree, где задачи-цели считают эти события.
 */
USTRUCT(BlueprintType)
struct FQuestEventData
{
    GENERATED_BODY()

    /** Уточняющий тег события (например, тип убитого врага или собранного предмета). */
    UPROPERTY(BlueprintReadWrite, Category = "Quest")
    FGameplayTag EventTag;

    /** Вклад события в счётчик цели (по умолчанию 1). */
    UPROPERTY(BlueprintReadWrite, Category = "Quest")
    int32 Amount = 1;

    /** Источник события (актор или объект), опционально. */
    UPROPERTY(BlueprintReadWrite, Category = "Quest")
    TObjectPtr<UObject> Source = nullptr;
};

/**
 * Уведомление о событии квеста — доставляется делегатом подсистемы
 * OnQuestNotification и показывается виджетом-тостом.
 */
USTRUCT(BlueprintType)
struct FQuestNotification
{
    GENERATED_BODY()

    /** Тип события. */
    UPROPERTY(BlueprintReadOnly, Category = "Quest")
    EQuestNotificationType Type = EQuestNotificationType::Started;

    /** Квест, к которому относится уведомление. */
    UPROPERTY(BlueprintReadOnly, Category = "Quest")
    FGameplayTag QuestId;

    /** Текст уведомления. */
    UPROPERTY(BlueprintReadOnly, Category = "Quest")
    FText Message;
};

/**
 * Сводка данных квеста для отображения в UI — собирается подсистемой
 * из неизменяемого определения и снимка прогресса инстанса.
 */
USTRUCT(BlueprintType)
struct FQuestDisplayData
{
    GENERATED_BODY()

    /** Идентификатор квеста. */
    UPROPERTY(BlueprintReadOnly, Category = "Quest")
    FGameplayTag QuestId;

    /** Отображаемое имя квеста. */
    UPROPERTY(BlueprintReadOnly, Category = "Quest")
    FText DisplayName;

    /** Описание квеста. */
    UPROPERTY(BlueprintReadOnly, Category = "Quest")
    FText Description;

    /** Категория квеста. */
    UPROPERTY(BlueprintReadOnly, Category = "Quest")
    EQuestCategory Category = EQuestCategory::Side;

    /** Сложность квеста. */
    UPROPERTY(BlueprintReadOnly, Category = "Quest")
    EQuestDifficulty Difficulty = EQuestDifficulty::Normal;

    /** Ориентировочная длительность, минут. */
    UPROPERTY(BlueprintReadOnly, Category = "Quest")
    float EstimatedMinutes = 0.f;

    /** Текущее состояние квеста. */
    UPROPERTY(BlueprintReadOnly, Category = "Quest")
    EQuestState State = EQuestState::Inactive;

    /** Доля выполнения квеста в диапазоне 0..1. */
    UPROPERTY(BlueprintReadOnly, Category = "Quest")
    float CompletionPercent = 0.f;

    /** Снимок прогресса целей квеста. */
    UPROPERTY(BlueprintReadOnly, Category = "Quest")
    TArray<FObjectiveProgress> Objectives;
};
