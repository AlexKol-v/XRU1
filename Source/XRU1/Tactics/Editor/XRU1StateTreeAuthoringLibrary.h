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
};
