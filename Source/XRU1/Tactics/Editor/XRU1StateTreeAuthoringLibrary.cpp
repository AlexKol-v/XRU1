#include "XRU1StateTreeAuthoringLibrary.h"

#include "XRU1Log.h"

#if WITH_EDITOR
#include "StateTree.h"
#include "StateTreeEditorData.h"
#include "StateTreeState.h"
#include "StateTreeEditorNode.h"
#include "StateTreeTaskBase.h"
#include "StateTreeTasksStatus.h" // EStateTreeTaskCompletionType
#include "UObject/Package.h"
#endif

#if WITH_EDITOR
namespace XRU1StateTreeAuthoring
{
	/** Загрузить ассет и его editor-данные (без них структуру править нечем). */
	static UStateTreeEditorData* LoadEditorData(const FString& AssetPath, UStateTree*& OutTree)
	{
		OutTree = LoadObject<UStateTree>(nullptr, *AssetPath);
		if (!OutTree)
		{
			UE_LOG(LogXRU1Quest, Error, TEXT("[StateTreeAuthoring] Ассет не найден: %s"), *AssetPath);
			return nullptr;
		}
		UStateTreeEditorData* EditorData = Cast<UStateTreeEditorData>(OutTree->EditorData);
		if (!EditorData)
		{
			UE_LOG(LogXRU1Quest, Error, TEXT("[StateTreeAuthoring] У %s нет editor-данных"), *AssetPath);
		}
		return EditorData;
	}

	/** Обход всего дерева состояний сверху вниз. */
	static void VisitStates(UStateTreeState* State, TFunctionRef<void(UStateTreeState&)> Visitor)
	{
		if (!State)
		{
			return;
		}
		Visitor(*State);
		for (UStateTreeState* Child : State->Children)
		{
			VisitStates(Child, Visitor);
		}
	}

	static void VisitAllStates(UStateTreeEditorData& EditorData, TFunctionRef<void(UStateTreeState&)> Visitor)
	{
		for (UStateTreeState* SubTree : EditorData.SubTrees)
		{
			VisitStates(SubTree, Visitor);
		}
	}

	static UStateTreeState* FindStateByName(UStateTreeEditorData& EditorData, const FName Name)
	{
		UStateTreeState* Found = nullptr;
		VisitAllStates(EditorData, [&Found, Name](UStateTreeState& State)
		{
			if (!Found && State.Name == Name)
			{
				Found = &State;
			}
		});
		return Found;
	}

	/**
	 * Контейнер, в котором лежит состояние: список детей родителя либо корневой
	 * SubTrees. Возвращает его и индекс состояния в нём.
	 */
	static TArray<TObjectPtr<UStateTreeState>>* FindOwningArray(UStateTreeEditorData& EditorData,
		const UStateTreeState* State, int32& OutIndex, UStateTreeState*& OutParent)
	{
		OutIndex = INDEX_NONE;
		OutParent = nullptr;

		OutIndex = EditorData.SubTrees.IndexOfByKey(State);
		if (OutIndex != INDEX_NONE)
		{
			return &EditorData.SubTrees;
		}

		TArray<TObjectPtr<UStateTreeState>>* Result = nullptr;
		int32 FoundIndex = INDEX_NONE;
		UStateTreeState* FoundParent = nullptr;
		VisitAllStates(EditorData, [&](UStateTreeState& Candidate)
		{
			if (Result)
			{
				return;
			}
			const int32 Index = Candidate.Children.IndexOfByKey(State);
			if (Index != INDEX_NONE)
			{
				Result = &Candidate.Children;
				FoundIndex = Index;
				FoundParent = &Candidate;
			}
		});
		OutIndex = FoundIndex;
		OutParent = FoundParent;
		return Result;
	}
}
#endif // WITH_EDITOR

bool UXRU1StateTreeAuthoringLibrary::InsertPauseStateBefore(const FString& StateTreeAssetPath,
	FName TargetStateName, FName NewStateName, float DelaySeconds)
{
#if WITH_EDITOR
	using namespace XRU1StateTreeAuthoring;

	UStateTree* Tree = nullptr;
	UStateTreeEditorData* EditorData = LoadEditorData(StateTreeAssetPath, Tree);
	if (!EditorData)
	{
		return false;
	}

	// Идемпотентность: повторный прогон скрипта не должен плодить паузы.
	if (FindStateByName(*EditorData, NewStateName))
	{
		UE_LOG(LogXRU1Quest, Display,
			TEXT("[StateTreeAuthoring] Состояние %s уже существует — пропуск"),
			*NewStateName.ToString());
		return true;
	}

	UStateTreeState* Target = FindStateByName(*EditorData, TargetStateName);
	if (!Target)
	{
		UE_LOG(LogXRU1Quest, Error, TEXT("[StateTreeAuthoring] Состояние %s не найдено"),
			*TargetStateName.ToString());
		return false;
	}

	int32 TargetIndex = INDEX_NONE;
	UStateTreeState* Parent = nullptr;
	TArray<TObjectPtr<UStateTreeState>>* Owner = FindOwningArray(*EditorData, Target, TargetIndex, Parent);
	if (!Owner || TargetIndex == INDEX_NONE)
	{
		UE_LOG(LogXRU1Quest, Error, TEXT("[StateTreeAuthoring] Не найден контейнер состояния %s"),
			*TargetStateName.ToString());
		return false;
	}

	EditorData->Modify();
	if (Parent)
	{
		Parent->Modify();
	}

	// Новое состояние создаём тем же builder API, что и редактор (Outer, флаги,
	// Parent), а затем переставляем на позицию ПЕРЕД целевым.
	UStateTreeState* PauseState = nullptr;
	if (Parent)
	{
		PauseState = &Parent->AddChildState(NewStateName);
		Parent->Children.RemoveAt(Parent->Children.Num() - 1);
		Parent->Children.Insert(PauseState, TargetIndex);
	}
	else
	{
		PauseState = &EditorData->AddSubTree(NewStateName);
		EditorData->SubTrees.RemoveAt(EditorData->SubTrees.Num() - 1);
		EditorData->SubTrees.Insert(PauseState, TargetIndex);
	}
	PauseState->ID = FGuid::NewGuid();

	// ⚠️ Дефолт движка — `Any`: состояние закрывается, как только завершится
	// ЛЮБАЯ задача. Мгновенный Action Gate закрывал бы паузу в тот же кадр, и
	// задержка не работала бы вообще. Паузе
	// нужен `All`: ждём и Delay Task, и всё остальное.
	PauseState->TasksCompletion = EStateTreeTaskCompletionType::All;

	// Движковая задача «Delay Task» (StateTreeModule/Private) — по имени, потому
	// что заголовок приватный. Это НАТИВНЫЙ узел: пользователь увидит в
	// редакторе обычную ноду Delay Task с полем Duration.
	UScriptStruct* DelayStruct = FindObject<UScriptStruct>(nullptr,
		TEXT("/Script/StateTreeModule.StateTreeDelayTask"));
	if (!DelayStruct)
	{
		UE_LOG(LogXRU1Quest, Error, TEXT("[StateTreeAuthoring] Не найден FStateTreeDelayTask"));
		return false;
	}

	FStateTreeEditorNode& TaskNode = PauseState->Tasks.AddDefaulted_GetRef();
	TaskNode.ID = FGuid::NewGuid();
	TaskNode.Node.InitializeAs(DelayStruct);
	const FStateTreeNodeBase& NodeBase = TaskNode.Node.GetMutable<FStateTreeNodeBase>();
	const UScriptStruct* InstanceStruct = Cast<const UScriptStruct>(NodeBase.GetInstanceDataType());
	if (!InstanceStruct)
	{
		UE_LOG(LogXRU1Quest, Error, TEXT("[StateTreeAuthoring] У Delay Task нет instance data"));
		return false;
	}
	TaskNode.Instance.InitializeAs(InstanceStruct);
	if (FFloatProperty* DurationProperty =
		FindFProperty<FFloatProperty>(InstanceStruct, TEXT("Duration")))
	{
		DurationProperty->SetPropertyValue_InContainer(
			TaskNode.Instance.GetMutableMemory(), DelaySeconds);
	}

	// Пауза кончилась — идём в исходный шаг.
	PauseState->AddTransition(EStateTreeTransitionTrigger::OnStateCompleted,
		EStateTreeTransitionType::GotoState, Target);

	// Всё, что вело в целевой шаг, теперь ведёт в паузу перед ним. Собственный
	// переход паузы пропускаем, иначе получим петлю.
	const FGuid TargetID = Target->ID;
	int32 Rewired = 0;
	VisitAllStates(*EditorData, [&](UStateTreeState& State)
	{
		if (&State == PauseState)
		{
			return;
		}
		bool bModified = false;
		for (FStateTreeTransition& Transition : State.Transitions)
		{
			if (Transition.State.ID == TargetID &&
				Transition.State.LinkType == EStateTreeTransitionType::GotoState)
			{
				if (!bModified)
				{
					State.Modify();
					bModified = true;
				}
				Transition.State.ID = PauseState->ID;
				Transition.State.Name = PauseState->Name;
				++Rewired;
			}
		}
	});

	Tree->MarkPackageDirty();
	UE_LOG(LogXRU1Quest, Display,
		TEXT("[StateTreeAuthoring] Пауза %s (%.2f с) вставлена перед %s; перенаправлено переходов: %d"),
		*NewStateName.ToString(), DelaySeconds, *TargetStateName.ToString(), Rewired);
	return true;
#else
	return false;
#endif
}

bool UXRU1StateTreeAuthoringLibrary::MoveTasksBetweenStates(const FString& StateTreeAssetPath,
	FName FromStateName, FName ToStateName, const TArray<int32>& TaskIndices)
{
#if WITH_EDITOR
	using namespace XRU1StateTreeAuthoring;

	UStateTree* Tree = nullptr;
	UStateTreeEditorData* EditorData = LoadEditorData(StateTreeAssetPath, Tree);
	if (!EditorData)
	{
		return false;
	}

	UStateTreeState* From = FindStateByName(*EditorData, FromStateName);
	UStateTreeState* To = FindStateByName(*EditorData, ToStateName);
	if (!From || !To)
	{
		UE_LOG(LogXRU1Quest, Error, TEXT("[StateTreeAuthoring] Не найдено состояние %s / %s"),
			*FromStateName.ToString(), *ToStateName.ToString());
		return false;
	}

	// Удаляем с конца, иначе индексы «съезжают» после первого же переноса.
	TArray<int32> Sorted = TaskIndices;
	Sorted.Sort();
	for (int32 i = 0; i < Sorted.Num(); ++i)
	{
		if (!From->Tasks.IsValidIndex(Sorted[i]))
		{
			UE_LOG(LogXRU1Quest, Error, TEXT("[StateTreeAuthoring] У %s нет задачи %d"),
				*FromStateName.ToString(), Sorted[i]);
			return false;
		}
		// Дубликат индекса при удалении с конца снёс бы ЧУЖУЮ задачу (индексы
		// уже сместились) — молча испорченный граф, поэтому это ошибка.
		if (i > 0 && Sorted[i] == Sorted[i - 1])
		{
			UE_LOG(LogXRU1Quest, Error,
				TEXT("[StateTreeAuthoring] Индекс задачи %d указан дважды"), Sorted[i]);
			return false;
		}
	}

	From->Modify();
	To->Modify();
	for (int32 i = Sorted.Num() - 1; i >= 0; --i)
	{
		To->Tasks.Insert(From->Tasks[Sorted[i]], 0); // порядок исходного списка сохраняем
		From->Tasks.RemoveAt(Sorted[i]);
	}

	Tree->MarkPackageDirty();
	UE_LOG(LogXRU1Quest, Display, TEXT("[StateTreeAuthoring] Перенесено задач %d: %s → %s"),
		Sorted.Num(), *FromStateName.ToString(), *ToStateName.ToString());
	// Состояние без задач завершается в тот же кадр — обычно это не то, чего
	// хотели, когда «выносили презентацию в отдельный шаг».
	if (From->Tasks.Num() == 0)
	{
		UE_LOG(LogXRU1Quest, Warning,
			TEXT("[StateTreeAuthoring] У %s не осталось задач — состояние будет проскакиваться"),
			*FromStateName.ToString());
	}
	return true;
#else
	return false;
#endif
}

TArray<FString> UXRU1StateTreeAuthoringLibrary::DescribeStates(const FString& StateTreeAssetPath)
{
	TArray<FString> Result;
#if WITH_EDITOR
	using namespace XRU1StateTreeAuthoring;

	UStateTree* Tree = nullptr;
	UStateTreeEditorData* EditorData = LoadEditorData(StateTreeAssetPath, Tree);
	if (!EditorData)
	{
		return Result;
	}

	VisitAllStates(*EditorData, [&Result](UStateTreeState& State)
	{
		FString Tasks;
		for (const FStateTreeEditorNode& Task : State.Tasks)
		{
			if (const UScriptStruct* Struct = Task.Node.GetScriptStruct())
			{
				Tasks += (Tasks.IsEmpty() ? TEXT("") : TEXT(", "));
				Tasks += Struct->GetName();
			}
		}
		FString Transitions;
		for (const FStateTreeTransition& Transition : State.Transitions)
		{
			Transitions += (Transitions.IsEmpty() ? TEXT("") : TEXT(", "));
			Transitions += Transition.State.Name.ToString();
		}
		Result.Add(FString::Printf(TEXT("%s | tasks=[%s] | →[%s]"),
			*State.Name.ToString(), *Tasks, *Transitions));
	});
#endif
	return Result;
}

bool UXRU1StateTreeAuthoringLibrary::AddChildState(const FString& StateTreeAssetPath,
	const FName ParentStateName, const FName NewStateName)
{
#if WITH_EDITOR
	using namespace XRU1StateTreeAuthoring;

	UStateTree* Tree = nullptr;
	UStateTreeEditorData* EditorData = LoadEditorData(StateTreeAssetPath, Tree);
	if (!EditorData)
	{
		return false;
	}

	// Повторный прогон скрипта сборки не должен плодить дубли состояний.
	if (FindStateByName(*EditorData, NewStateName))
	{
		UE_LOG(LogXRU1Quest, Log, TEXT("[StateTreeAuthoring] Состояние %s уже есть — пропуск"),
			*NewStateName.ToString());
		return true;
	}

	UStateTreeState* Parent = ParentStateName.IsNone()
		? nullptr : FindStateByName(*EditorData, ParentStateName);
	if (!ParentStateName.IsNone() && !Parent)
	{
		UE_LOG(LogXRU1Quest, Error, TEXT("[StateTreeAuthoring] Родитель %s не найден"),
			*ParentStateName.ToString());
		return false;
	}

	Tree->Modify();
	EditorData->Modify();
	if (Parent)
	{
		Parent->Modify();
	}

	UStateTreeState* NewState = Parent
		? &Parent->AddChildState(NewStateName) : &EditorData->AddSubTree(NewStateName);
	NewState->ID = FGuid::NewGuid();
	// Дефолт движка `Any` закрывает состояние по первой завершившейся задаче —
	// шаг с целью и репликой «пролетал» бы за кадр.
	NewState->TasksCompletion = EStateTreeTaskCompletionType::All;

	UE_LOG(LogXRU1Quest, Log, TEXT("[StateTreeAuthoring] Создано состояние %s (родитель %s)"),
		*NewStateName.ToString(),
		Parent ? *ParentStateName.ToString() : TEXT("<корень>"));
	return true;
#else
	return false;
#endif
}

bool UXRU1StateTreeAuthoringLibrary::AddTaskToState(const FString& StateTreeAssetPath,
	const FName StateName, const FString& TaskStructPath, const FString& InstanceDataText,
	const bool bConsideredForCompletion)
{
#if WITH_EDITOR
	using namespace XRU1StateTreeAuthoring;

	UStateTree* Tree = nullptr;
	UStateTreeEditorData* EditorData = LoadEditorData(StateTreeAssetPath, Tree);
	if (!EditorData)
	{
		return false;
	}

	UStateTreeState* State = FindStateByName(*EditorData, StateName);
	if (!State)
	{
		UE_LOG(LogXRU1Quest, Error, TEXT("[StateTreeAuthoring] Состояние %s не найдено"),
			*StateName.ToString());
		return false;
	}

	UScriptStruct* TaskStruct = FindObject<UScriptStruct>(nullptr, *TaskStructPath);
	if (!TaskStruct)
	{
		UE_LOG(LogXRU1Quest, Error, TEXT("[StateTreeAuthoring] Тип задачи %s не найден"),
			*TaskStructPath);
		return false;
	}

	Tree->Modify();
	EditorData->Modify();
	State->Modify();

	FStateTreeEditorNode& TaskNode = State->Tasks.AddDefaulted_GetRef();
	TaskNode.ID = FGuid::NewGuid();
	TaskNode.Node.InitializeAs(TaskStruct);

	FStateTreeNodeBase& NodeBase = TaskNode.Node.GetMutable<FStateTreeNodeBase>();
	const UScriptStruct* InstanceStruct = Cast<const UScriptStruct>(NodeBase.GetInstanceDataType());
	if (!InstanceStruct)
	{
		UE_LOG(LogXRU1Quest, Error, TEXT("[StateTreeAuthoring] У %s нет instance data"), *TaskStructPath);
		State->Tasks.Pop();
		return false;
	}
	TaskNode.Instance.InitializeAs(InstanceStruct);

	// Значения задаются тем же текстом, каким ассет экспортируется: один формат
	// на чтение и на запись, поэтому эталон всегда под рукой — экспорт готового
	// дерева обучения.
	if (!InstanceDataText.IsEmpty())
	{
		class FAuthoringErrorPipe : public FOutputDevice
		{
		public:
			int32 NumErrors = 0;
			virtual void Serialize(const TCHAR* V, ELogVerbosity::Type, const FName&) override
			{
				++NumErrors;
				UE_LOG(LogXRU1Quest, Error, TEXT("[StateTreeAuthoring] ImportText: %s"), V);
			}
		} ErrorPipe;

		const TCHAR* Buffer = *InstanceDataText;
		InstanceStruct->ImportText(Buffer, TaskNode.Instance.GetMutableMemory(), nullptr,
			PPF_None, &ErrorPipe, InstanceStruct->GetName());
		if (ErrorPipe.NumErrors > 0)
		{
			State->Tasks.Pop();
			return false;
		}
	}

#if WITH_EDITORONLY_DATA
	if (FStateTreeTaskBase* Task = TaskNode.Node.GetMutablePtr<FStateTreeTaskBase>())
	{
		// Фоновая реплика не решает, когда закончится шаг: она ждёт своё событие
		// рядом с целью и молчит, если события не было.
		Task->bConsideredForCompletion = bConsideredForCompletion;
	}
#endif

	UE_LOG(LogXRU1Quest, Log, TEXT("[StateTreeAuthoring] %s: добавлена задача %s (completion=%d)"),
		*StateName.ToString(), *TaskStruct->GetName(), bConsideredForCompletion ? 1 : 0);
	return true;
#else
	return false;
#endif
}

bool UXRU1StateTreeAuthoringLibrary::AddCompletionTransition(const FString& StateTreeAssetPath,
	const FName FromStateName, const FName ToStateName, const bool bOnSuccessOnly)
{
#if WITH_EDITOR
	using namespace XRU1StateTreeAuthoring;

	UStateTree* Tree = nullptr;
	UStateTreeEditorData* EditorData = LoadEditorData(StateTreeAssetPath, Tree);
	if (!EditorData)
	{
		return false;
	}

	UStateTreeState* From = FindStateByName(*EditorData, FromStateName);
	if (!From)
	{
		UE_LOG(LogXRU1Quest, Error, TEXT("[StateTreeAuthoring] Состояние %s не найдено"),
			*FromStateName.ToString());
		return false;
	}

	Tree->Modify();
	EditorData->Modify();
	From->Modify();

	const EStateTreeTransitionTrigger Trigger = bOnSuccessOnly
		? EStateTreeTransitionTrigger::OnStateSucceeded
		: EStateTreeTransitionTrigger::OnStateCompleted;

	if (ToStateName.IsNone())
	{
		// Терминал сценария: провал уводит квест в Failed на любом шаге.
		From->AddTransition(EStateTreeTransitionTrigger::OnStateFailed,
			EStateTreeTransitionType::Failed, nullptr);
		UE_LOG(LogXRU1Quest, Log, TEXT("[StateTreeAuthoring] %s: переход по провалу → Failed"),
			*FromStateName.ToString());
		return true;
	}

	UStateTreeState* To = FindStateByName(*EditorData, ToStateName);
	if (!To)
	{
		UE_LOG(LogXRU1Quest, Error, TEXT("[StateTreeAuthoring] Цель перехода %s не найдена"),
			*ToStateName.ToString());
		return false;
	}

	From->AddTransition(Trigger, EStateTreeTransitionType::GotoState, To);
	UE_LOG(LogXRU1Quest, Log, TEXT("[StateTreeAuthoring] %s → %s (%s)"),
		*FromStateName.ToString(), *ToStateName.ToString(),
		bOnSuccessOnly ? TEXT("по успеху") : TEXT("по завершению"));
	return true;
#else
	return false;
#endif
}

bool UXRU1StateTreeAuthoringLibrary::ClearStateTasks(const FString& StateTreeAssetPath,
	const FName StateName)
{
#if WITH_EDITOR
	using namespace XRU1StateTreeAuthoring;

	UStateTree* Tree = nullptr;
	UStateTreeEditorData* EditorData = LoadEditorData(StateTreeAssetPath, Tree);
	if (!EditorData)
	{
		return false;
	}

	UStateTreeState* State = FindStateByName(*EditorData, StateName);
	if (!State)
	{
		UE_LOG(LogXRU1Quest, Error, TEXT("[StateTreeAuthoring] Состояние %s не найдено"),
			*StateName.ToString());
		return false;
	}

	Tree->Modify();
	EditorData->Modify();
	State->Modify();

	const int32 Removed = State->Tasks.Num();
	State->Tasks.Reset();
	UE_LOG(LogXRU1Quest, Log, TEXT("[StateTreeAuthoring] %s: удалено задач %d"),
		*StateName.ToString(), Removed);
	return true;
#else
	return false;
#endif
}
