#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "XRU1StateTreeAuthoringLibrary.generated.h"

/**
 * Editor-библиотека структурных правок StateTree-ассетов.
 *
 * Зачем: значения внутри состояний (задачи, переходы, политика) агент правит
 * из Python через export_text/import_text (см. AGENT_UNREAL_TOOLING §5.2.2), но
 * СОЗДАТЬ новое состояние оттуда нельзя — массивы `Children`/`SubTrees` не
 * помечены `EditDefaultsOnly` и Python их не пишет. А именно отдельное
 * состояние — единственный НАТИВНЫЙ для StateTree способ поставить паузу между
 * шагами: движковый Transition Delay на completion-переходах компилятор молча
 * сбрасывает («Completion transitions cannot have delay»), а задержка ВНУТРИ
 * задачи тормозит только свою задачу — соседние по состоянию (подсказка, зона,
 * gate) стартуют сразу, и игрок видит «пункты следующего этапа» в паузе.
 *
 * Состояние-пауза решает это по правилам движка: пока оно активно, СЛЕДУЮЩЕЕ
 * состояние не входит вообще, со всеми своими задачами. Тем же способом
 * ставятся будущие реплики: состояние с тактом держит шаг ровно столько,
 * сколько говорит голос.
 *
 * Функции работают только в editor-сборке. Ассет НЕ сохраняется и НЕ
 * компилируется — это делает вызывающая сторона (save_asset → reload_packages
 * → save_asset), чтобы отказ был откатываемым.
 */
UCLASS()
class XRU1_API UXRU1StateTreeAuthoringLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Вставляет ПЕРЕД состоянием `TargetStateName` новое состояние-паузу с
	 * движковой задачей «Delay Task» на `DelaySeconds` и переходом
	 * «On State Completed → TargetState». Все переходы дерева, которые вели в
	 * целевое состояние, перенаправляются на паузу — то есть шаг просто
	 * получает «вздох» перед собой, а остальной граф не трогается.
	 *
	 * Идемпотентна: если состояние с именем `NewStateName` уже есть, ничего не
	 * делает и возвращает true (повторный прогон скрипта безопасен).
	 */
	UFUNCTION(BlueprintCallable, Category = "XRU1|StateTree Authoring")
	static bool InsertPauseStateBefore(const FString& StateTreeAssetPath, FName TargetStateName,
		FName NewStateName, float DelaySeconds);

	/**
	 * Переносит задачи по индексам `TaskIndices` из состояния `FromStateName` в
	 * состояние `ToStateName` (в конец списка), сохраняя их настройки и ID.
	 * Нужно, чтобы разнести «презентацию» и «геймплей» шага по разным
	 * состояниям: такт и активации — в паузу, gate и цель — в рабочий шаг.
	 */
	UFUNCTION(BlueprintCallable, Category = "XRU1|StateTree Authoring")
	static bool MoveTasksBetweenStates(const FString& StateTreeAssetPath, FName FromStateName,
		FName ToStateName, const TArray<int32>& TaskIndices);

	/** Диагностика: имена состояний в порядке обхода и число задач у каждого. */
	UFUNCTION(BlueprintCallable, Category = "XRU1|StateTree Authoring")
	static TArray<FString> DescribeStates(const FString& StateTreeAssetPath);

	// --- Сборка дерева с нуля --------------------------------------------------
	//
	// Дерево миссии — это девять реплик по семь полей каждая плюс пять целей.
	// Заполнять их мышью — час кликов и ровно та работа, где опечатка в теге
	// канала не видна до прогона. Поэтому граф собирается скриптом, а редактор
	// остаётся местом, где его СМОТРЯТ и правят руками.

	/**
	 * Создаёт дочернее состояние `NewStateName` у `ParentStateName`
	 * (пустое имя родителя — новый корневой SubTree). `TasksCompletion` ставится
	 * `All`: дефолт движка `Any` закрывает состояние по первой же завершившейся
	 * задаче и «пролетает» дерево за кадр.
	 *
	 * Идемпотентна: состояние с таким именем уже есть — успех без изменений.
	 */
	UFUNCTION(BlueprintCallable, Category = "XRU1|StateTree Authoring")
	static bool AddChildState(const FString& StateTreeAssetPath, FName ParentStateName,
		FName NewStateName);

	/**
	 * Добавляет задачу в состояние. `TaskStructPath` — путь USTRUCT задачи
	 * (например `/Script/XRU1.TacticalTask_TutorialBeat`), `InstanceDataText` —
	 * значения instance data в формате T3D, ровно как их печатает экспорт
	 * ассета: `(Beat=(BeatId="X",Duration=3.5),TriggerEvent=(TagName="..."))`.
	 * Пустая строка — значения по умолчанию.
	 *
	 * `bConsideredForCompletion=false` делает задачу фоновой: она не решает,
	 * когда состояние закончится. Так живут реплики-реакции рядом с целью шага.
	 */
	UFUNCTION(BlueprintCallable, Category = "XRU1|StateTree Authoring")
	static bool AddTaskToState(const FString& StateTreeAssetPath, FName StateName,
		const FString& TaskStructPath, const FString& InstanceDataText,
		bool bConsideredForCompletion = true);

	/**
	 * Удаляет все задачи состояния. Нужна для перезаливки значений: инстанс-данные
	 * задачи правятся текстом при СОЗДАНИИ, а редактировать их на месте нечем.
	 */
	UFUNCTION(BlueprintCallable, Category = "XRU1|StateTree Authoring")
	static bool ClearStateTasks(const FString& StateTreeAssetPath, FName StateName);

	/**
	 * Переход «состояние завершилось успешно → перейти в `ToStateName`».
	 * Пустое `ToStateName` со `bFailureToRoot` — терминальный переход в Root
	 * по провалу (страховка уровня сценария, 11 §4.5).
	 */
	UFUNCTION(BlueprintCallable, Category = "XRU1|StateTree Authoring")
	static bool AddCompletionTransition(const FString& StateTreeAssetPath, FName FromStateName,
		FName ToStateName, bool bOnSuccessOnly = true);
};
