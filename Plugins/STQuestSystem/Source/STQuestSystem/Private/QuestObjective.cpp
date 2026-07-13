// Copyright Epic Games, Inc. All Rights Reserved.

#include "QuestObjective.h"

void UQuestObjective::HandleEventTag(FGameplayTag EventTag)
{
    if (State != EObjectiveState::Active)
    {
        return;
    }

    if (EventTag.MatchesTag(EventChannel))
    {
        ++CurrentCount;
        if (IsSatisfied())
        {
            State = EObjectiveState::Completed;
        }
    }
}

FObjectiveProgress UQuestObjective::MakeProgress() const
{
    FObjectiveProgress Progress;
    Progress.ObjectiveId = ObjectiveId;
    Progress.State = State;
    Progress.Current = CurrentCount;
    Progress.Required = RequiredCount;
    Progress.Description = Description;
    return Progress;
}
