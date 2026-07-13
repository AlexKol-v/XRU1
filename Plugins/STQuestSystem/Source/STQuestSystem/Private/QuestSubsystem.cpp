// Copyright Epic Games, Inc. All Rights Reserved.

#include "QuestSubsystem.h"

#include "Engine/AssetManager.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameplayTagAssetInterface.h"
#include "Kismet/GameplayStatics.h"
#include "QuestDefinition.h"
#include "QuestInstance.h"
#include "QuestObjective.h"
#include "QuestReward.h"
#include "QuestRunnerActor.h"
#include "QuestSaveGame.h"

void UQuestSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    BuildQuestRegistry();
}

void UQuestSubsystem::BuildQuestRegistry()
{
    QuestRegistry.Empty();

    // Asset Manager создаётся при старте движка; на ранних стадиях его может не быть.
    if (!UAssetManager::IsInitialized())
    {
        return;
    }

    UAssetManager& AssetManager = UAssetManager::Get();
    const FPrimaryAssetType QuestType(TEXT("Quest"));

    TArray<FPrimaryAssetId> QuestIds;
    AssetManager.GetPrimaryAssetIdList(QuestType, QuestIds);
    for (const FPrimaryAssetId& AssetId : QuestIds)
    {
        const FSoftObjectPath AssetPath = AssetManager.GetPrimaryAssetPath(AssetId);
        UQuestDefinition* Definition = Cast<UQuestDefinition>(AssetPath.TryLoad());
        if (Definition && Definition->QuestId.IsValid())
        {
            QuestRegistry.Add(Definition->QuestId, Definition);
        }
    }
}

UQuestInstance* UQuestSubsystem::GetOrCreateInstance(UQuestDefinition* Definition)
{
    if (!Definition || !Definition->QuestId.IsValid())
    {
        return nullptr;
    }

    if (const TObjectPtr<UQuestInstance>* Existing = ActiveQuests.Find(Definition->QuestId))
    {
        return *Existing;
    }

    UQuestInstance* Instance = NewObject<UQuestInstance>(this);
    Instance->Definition = Definition;
    Instance->Progress.QuestId = Definition->QuestId;
    Instance->Progress.State = EQuestState::Inactive;
    ActiveQuests.Add(Definition->QuestId, Instance);
    return Instance;
}

UQuestInstance* UQuestSubsystem::StartQuest(UQuestDefinition* Definition)
{
    UQuestInstance* Instance = GetOrCreateInstance(Definition);
    if (!Instance)
    {
        return nullptr;
    }

    // Старт допустим только из Inactive или Available — активный не перезапускаем.
    const EQuestState State = Instance->Progress.State;
    if (State != EQuestState::Inactive && State != EQuestState::Available)
    {
        return Instance;
    }

    const UGameInstance* GameInstance = GetGameInstance();
    UWorld* World = GameInstance ? GameInstance->GetWorld() : nullptr;
    if (!World)
    {
        return Instance;
    }

    AQuestRunnerActor* Runner = World->SpawnActor<AQuestRunnerActor>();
    Instance->Runner = Runner;

    // Переводим квест в Active и запускаем исполнение его State Tree.
    SetQuestState(Instance, EQuestState::Active);
    if (Runner)
    {
        Runner->StartQuest(Instance);
    }

    return Instance;
}

UQuestInstance* UQuestSubsystem::StartQuestById(FGameplayTag QuestId)
{
    return StartQuest(GetQuestDefinition(QuestId));
}

void UQuestSubsystem::MakeQuestAvailable(FGameplayTag QuestId, AActor* Owner)
{
    UQuestDefinition* Definition = GetQuestDefinition(QuestId);
    if (!Definition)
    {
        return;
    }

    // Пререквизиты — гейт доступности квеста.
    if (!AreRequirementsMet(Definition->Prerequisites, Owner))
    {
        return;
    }

    UQuestInstance* Instance = GetOrCreateInstance(Definition);
    if (Instance && Instance->Progress.State == EQuestState::Inactive)
    {
        Instance->Owner = Owner;
        SetQuestState(Instance, EQuestState::Available);
    }
}

void UQuestSubsystem::CompleteQuest(FGameplayTag QuestId)
{
    if (const TObjectPtr<UQuestInstance>* Found = ActiveQuests.Find(QuestId))
    {
        NotifyQuestFinished(*Found, /*bSuccess*/ true);
    }
}

void UQuestSubsystem::FailQuest(FGameplayTag QuestId)
{
    if (const TObjectPtr<UQuestInstance>* Found = ActiveQuests.Find(QuestId))
    {
        NotifyQuestFinished(*Found, /*bSuccess*/ false);
    }
}

EQuestState UQuestSubsystem::GetQuestState(FGameplayTag QuestId) const
{
    if (const TObjectPtr<UQuestInstance>* Found = ActiveQuests.Find(QuestId))
    {
        return (*Found)->Progress.State;
    }
    return EQuestState::Inactive;
}

bool UQuestSubsystem::IsQuestInState(FGameplayTag QuestId, EQuestState State) const
{
    return GetQuestState(QuestId) == State;
}

UQuestInstance* UQuestSubsystem::GetQuestInstance(FGameplayTag QuestId) const
{
    const TObjectPtr<UQuestInstance>* Found = ActiveQuests.Find(QuestId);
    return Found ? *Found : nullptr;
}

UQuestDefinition* UQuestSubsystem::GetQuestDefinition(FGameplayTag QuestId) const
{
    const TObjectPtr<UQuestDefinition>* Found = QuestRegistry.Find(QuestId);
    return Found ? *Found : nullptr;
}

TArray<UQuestDefinition*> UQuestSubsystem::GetAllKnownQuests() const
{
    TArray<UQuestDefinition*> Result;
    Result.Reserve(QuestRegistry.Num());
    for (const TPair<FGameplayTag, TObjectPtr<UQuestDefinition>>& Pair : QuestRegistry)
    {
        Result.Add(Pair.Value);
    }
    return Result;
}

TArray<UQuestDefinition*> UQuestSubsystem::GetQuestsInState(EQuestState State) const
{
    TArray<UQuestDefinition*> Result;
    for (const TPair<FGameplayTag, TObjectPtr<UQuestDefinition>>& Pair : QuestRegistry)
    {
        if (GetQuestState(Pair.Key) == State)
        {
            Result.Add(Pair.Value);
        }
    }
    return Result;
}

TArray<UQuestInstance*> UQuestSubsystem::GetAllInstances() const
{
    TArray<UQuestInstance*> Result;
    Result.Reserve(ActiveQuests.Num());
    for (const TPair<FGameplayTag, TObjectPtr<UQuestInstance>>& Pair : ActiveQuests)
    {
        if (Pair.Value)
        {
            Result.Add(Pair.Value);
        }
    }
    return Result;
}

TArray<UQuestInstance*> UQuestSubsystem::GetQuestsForOwner(AActor* Owner) const
{
    TArray<UQuestInstance*> Result;
    for (const TPair<FGameplayTag, TObjectPtr<UQuestInstance>>& Pair : ActiveQuests)
    {
        if (Pair.Value && Pair.Value->Owner.Get() == Owner)
        {
            Result.Add(Pair.Value);
        }
    }
    return Result;
}

bool UQuestSubsystem::IsObjectiveActive(FGameplayTag ObjectiveId) const
{
    if (!ObjectiveId.IsValid())
    {
        return false;
    }

    // Цель активна, если есть активный квест с активной целью данного ObjectiveId.
    for (const UQuestDefinition* Definition : GetQuestsInState(EQuestState::Active))
    {
        const UQuestInstance* Instance = Definition ? GetQuestInstance(Definition->QuestId) : nullptr;
        if (!Instance)
        {
            continue;
        }
        for (const FObjectiveProgress& Objective : Instance->Progress.Objectives)
        {
            if (Objective.State == EObjectiveState::Active && Objective.ObjectiveId == ObjectiveId)
            {
                return true;
            }
        }
    }
    return false;
}

bool UQuestSubsystem::AreRequirementsMet(const TArray<FQuestRequirement>& Requirements, AActor* Owner) const
{
    for (const FQuestRequirement& Requirement : Requirements)
    {
        switch (Requirement.Type)
        {
        case EQuestRequirementType::QuestState:
            if (GetQuestState(Requirement.RequiredQuestId) != Requirement.RequiredQuestState)
            {
                return false;
            }
            break;

        case EQuestRequirementType::OwnerHasTag:
        {
            const IGameplayTagAssetInterface* TagOwner = Cast<IGameplayTagAssetInterface>(Owner);
            if (!TagOwner || !TagOwner->HasMatchingGameplayTag(Requirement.RequiredTag))
            {
                return false;
            }
            break;
        }

        default:
            break;
        }
    }
    return true;
}

void UQuestSubsystem::NotifyQuestFinished(UQuestInstance* Instance, bool bSuccess)
{
    if (!Instance)
    {
        return;
    }

    SetQuestState(Instance, bSuccess ? EQuestState::Completed : EQuestState::Failed);

    // Награды выдаются только за успешное завершение квеста.
    if (bSuccess)
    {
        GrantRewards(Instance);
    }

    // State Tree отыграл своё — освобождаем актор-раннер.
    if (Instance->Runner)
    {
        Instance->Runner->StopQuest();
        Instance->Runner->Destroy();
        Instance->Runner = nullptr;
    }

    // Цепочка квестов: открываем квесты, связанные с завершённым.
    ResolveChainLinks(Instance, bSuccess);
}

void UQuestSubsystem::ResolveChainLinks(UQuestInstance* Instance, bool bSuccess)
{
    if (!Instance || !Instance->Definition)
    {
        return;
    }

    const FQuestChainLinks& Links = Instance->Definition->ChainLinks;
    const TArray<FGameplayTag>& QuestsToUnlock = bSuccess ? Links.UnlockOnComplete : Links.UnlockOnFail;
    AActor* Owner = Instance->Owner.Get();
    for (const FGameplayTag& NextQuestId : QuestsToUnlock)
    {
        MakeQuestAvailable(NextQuestId, Owner);
    }
}

void UQuestSubsystem::GrantRewards(UQuestInstance* Instance)
{
    if (!Instance || !Instance->Definition)
    {
        return;
    }

    AActor* Recipient = Instance->Owner.Get();
    for (UQuestReward* Reward : Instance->Definition->Rewards)
    {
        if (Reward)
        {
            Reward->GrantTo(Recipient);
        }
    }
    for (UQuestReward* Reward : Instance->DynamicRewards)
    {
        if (Reward)
        {
            Reward->GrantTo(Recipient);
        }
    }
}

void UQuestSubsystem::NotifyObjectiveProgress(FGameplayTag QuestId)
{
    OnObjectiveProgressChanged.Broadcast(QuestId);
}

void UQuestSubsystem::AddObjective(FGameplayTag QuestId, UQuestObjective* Objective)
{
    UQuestInstance* Instance = GetQuestInstance(QuestId);
    if (!Instance || !Objective)
    {
        return;
    }

    Instance->DynamicObjectives.Add(Objective);

    // Если квест исполняется — сразу учитываем цель в снимке через раннер.
    if (Instance->Runner)
    {
        Instance->Runner->ReportObjectiveProgress(Objective->MakeProgress());
    }
    else
    {
        NotifyObjectiveProgress(QuestId);
    }
}

void UQuestSubsystem::RemoveObjective(FGameplayTag QuestId, FGameplayTag ObjectiveId)
{
    UQuestInstance* Instance = GetQuestInstance(QuestId);
    if (!Instance)
    {
        return;
    }

    Instance->DynamicObjectives.RemoveAll([ObjectiveId](const UQuestObjective* Objective)
    {
        return Objective && Objective->ObjectiveId == ObjectiveId;
    });
    Instance->Progress.Objectives.RemoveAll([ObjectiveId](const FObjectiveProgress& Progress)
    {
        return Progress.ObjectiveId == ObjectiveId;
    });
    NotifyObjectiveProgress(QuestId);
}

void UQuestSubsystem::AddReward(FGameplayTag QuestId, UQuestReward* Reward)
{
    if (UQuestInstance* Instance = GetQuestInstance(QuestId))
    {
        if (Reward)
        {
            Instance->DynamicRewards.Add(Reward);
        }
    }
}

void UQuestSubsystem::RemoveReward(FGameplayTag QuestId, UQuestReward* Reward)
{
    if (UQuestInstance* Instance = GetQuestInstance(QuestId))
    {
        Instance->DynamicRewards.Remove(Reward);
    }
}

void UQuestSubsystem::AddRequirement(FGameplayTag QuestId, const FQuestRequirement& Requirement)
{
    if (UQuestInstance* Instance = GetQuestInstance(QuestId))
    {
        Instance->DynamicRequirements.Add(Requirement);
    }
}

void UQuestSubsystem::RemoveRequirement(FGameplayTag QuestId, int32 RequirementIndex)
{
    if (UQuestInstance* Instance = GetQuestInstance(QuestId))
    {
        if (Instance->DynamicRequirements.IsValidIndex(RequirementIndex))
        {
            Instance->DynamicRequirements.RemoveAt(RequirementIndex);
        }
    }
}

void UQuestSubsystem::TransferQuest(FGameplayTag QuestId, AActor* From, AActor* To)
{
    UQuestInstance* Instance = GetQuestInstance(QuestId);
    if (!Instance)
    {
        return;
    }

    // Перенос имеет смысл только от текущего владельца квеста.
    if (Instance->Owner.Get() != From)
    {
        return;
    }

    Instance->Owner = To;

    // Раннер переподписывается на события — на случай смены контекста владельца.
    if (Instance->Runner)
    {
        Instance->Runner->RefreshEventSubscription();
    }
}

void UQuestSubsystem::SetQuestState(UQuestInstance* Instance, EQuestState NewState)
{
    if (!Instance || Instance->Progress.State == NewState)
    {
        return;
    }

    Instance->Progress.State = NewState;
    const FGameplayTag QuestId = Instance->Progress.QuestId;
    OnQuestStateChanged.Broadcast(QuestId, NewState);
    EmitStateNotification(QuestId, NewState);
}

void UQuestSubsystem::EmitStateNotification(FGameplayTag QuestId, EQuestState NewState)
{
    FQuestNotification Notification;
    Notification.QuestId = QuestId;
    switch (NewState)
    {
    case EQuestState::Active:
        Notification.Type = EQuestNotificationType::Started;
        Notification.Message = NSLOCTEXT("Quest", "QuestStarted", "Квест начат");
        break;
    case EQuestState::Completed:
        Notification.Type = EQuestNotificationType::Completed;
        Notification.Message = NSLOCTEXT("Quest", "QuestCompleted", "Квест завершён");
        break;
    case EQuestState::Failed:
        Notification.Type = EQuestNotificationType::Failed;
        Notification.Message = NSLOCTEXT("Quest", "QuestFailed", "Квест провален");
        break;
    default:
        return;
    }
    OnQuestNotification.Broadcast(Notification);
}

void UQuestSubsystem::SetTrackedQuest(FGameplayTag QuestId)
{
    if (TrackedQuestId == QuestId)
    {
        return;
    }

    TrackedQuestId = QuestId;
    OnTrackedQuestChanged.Broadcast(TrackedQuestId);
}

void UQuestSubsystem::AbandonQuest(FGameplayTag QuestId)
{
    UQuestInstance* Instance = GetQuestInstance(QuestId);
    if (!Instance || Instance->Progress.State != EQuestState::Active)
    {
        return;
    }

    // Останавливаем исполнение и освобождаем актор-раннер.
    if (Instance->Runner)
    {
        Instance->Runner->StopQuest();
        Instance->Runner->Destroy();
        Instance->Runner = nullptr;
    }

    // Сбрасываем прогресс и возвращаем квест в доступные — его можно взять заново.
    Instance->Progress.Objectives.Reset();
    Instance->Progress.CompletionPercent = 0.f;
    SetQuestState(Instance, EQuestState::Available);

    if (TrackedQuestId == QuestId)
    {
        SetTrackedQuest(FGameplayTag());
    }
}

FQuestDisplayData UQuestSubsystem::GetQuestDisplayData(FGameplayTag QuestId) const
{
    FQuestDisplayData Data;
    Data.QuestId = QuestId;

    if (const UQuestDefinition* Definition = GetQuestDefinition(QuestId))
    {
        Data.DisplayName = Definition->DisplayName;
        Data.Description = Definition->Description;
        Data.Category = Definition->Category;
        Data.Difficulty = Definition->Difficulty;
        Data.EstimatedMinutes = Definition->EstimatedMinutes;
    }

    Data.State = GetQuestState(QuestId);
    if (const UQuestInstance* Instance = GetQuestInstance(QuestId))
    {
        Data.CompletionPercent = Instance->Progress.CompletionPercent;
        Data.Objectives = Instance->Progress.Objectives;
    }
    return Data;
}

void UQuestSubsystem::CollectProgress(TArray<FQuestProgress>& OutSnapshots) const
{
    OutSnapshots.Reset();
    OutSnapshots.Reserve(ActiveQuests.Num());
    for (const TPair<FGameplayTag, TObjectPtr<UQuestInstance>>& Pair : ActiveQuests)
    {
        if (Pair.Value)
        {
            OutSnapshots.Add(Pair.Value->Progress);
        }
    }
}

bool UQuestSubsystem::SaveToSlot(const FString& SlotName, int32 UserIndex)
{
    UQuestSaveGame* SaveGame = Cast<UQuestSaveGame>(
        UGameplayStatics::CreateSaveGameObject(UQuestSaveGame::StaticClass()));
    if (!SaveGame)
    {
        return false;
    }

    CollectProgress(SaveGame->QuestSnapshots);
    SaveGame->TrackedQuestId = TrackedQuestId;
    SaveGame->SlotName = SlotName;
    SaveGame->UserIndex = UserIndex;

    return UGameplayStatics::SaveGameToSlot(SaveGame, SlotName, UserIndex);
}

bool UQuestSubsystem::LoadFromSlot(const FString& SlotName, int32 UserIndex)
{
    if (!UGameplayStatics::DoesSaveGameExist(SlotName, UserIndex))
    {
        return false;
    }

    UQuestSaveGame* SaveGame = Cast<UQuestSaveGame>(
        UGameplayStatics::LoadGameFromSlot(SlotName, UserIndex));
    if (!SaveGame)
    {
        return false;
    }

    RestoreProgress(SaveGame->QuestSnapshots);
    SetTrackedQuest(SaveGame->TrackedQuestId);
    return true;
}

void UQuestSubsystem::RestoreProgress(const TArray<FQuestProgress>& Snapshots)
{
    // Сносим текущие инстансы и их раннеры — загрузка заменяет весь прогресс.
    for (const TPair<FGameplayTag, TObjectPtr<UQuestInstance>>& Pair : ActiveQuests)
    {
        if (Pair.Value && Pair.Value->Runner)
        {
            Pair.Value->Runner->StopQuest();
            Pair.Value->Runner->Destroy();
            Pair.Value->Runner = nullptr;
        }
    }
    ActiveQuests.Empty();

    const UGameInstance* GameInstance = GetGameInstance();
    UWorld* World = GameInstance ? GameInstance->GetWorld() : nullptr;

    for (const FQuestProgress& Snapshot : Snapshots)
    {
        UQuestDefinition* Definition = GetQuestDefinition(Snapshot.QuestId);
        if (!Definition)
        {
            continue;
        }

        UQuestInstance* Instance = NewObject<UQuestInstance>(this);
        Instance->Definition = Definition;
        Instance->Progress = Snapshot;
        ActiveQuests.Add(Snapshot.QuestId, Instance);

        // Активные квесты пересоздают State Tree и сидят счётчики из снимка;
        // завершённые/проваленные/доступные восстанавливаются только данными.
        if (Snapshot.State == EQuestState::Active && World)
        {
            AQuestRunnerActor* Runner = World->SpawnActor<AQuestRunnerActor>();
            Instance->Runner = Runner;
            if (Runner)
            {
                Runner->StartQuestRestoring(Instance, Snapshot);
            }
        }

        // Обновляем UI без тостов: журнал/трекер перечитывают состояние.
        OnQuestStateChanged.Broadcast(Snapshot.QuestId, Snapshot.State);
        OnObjectiveProgressChanged.Broadcast(Snapshot.QuestId);
    }
}
