// Copyright Epic Games, Inc. All Rights Reserved.

#include "STGameplayTagLibrary.h"

FText USTGameplayTagLibrary::GetTagLeafName(FGameplayTag Tag)
{
    if (!Tag.IsValid())
    {
        return FText::GetEmpty();
    }

    const FString Full = Tag.GetTagName().ToString();
    int32 LastDotIndex = INDEX_NONE;
    if (Full.FindLastChar(TEXT('.'), LastDotIndex))
    {
        return FText::FromString(Full.RightChop(LastDotIndex + 1));
    }
    return FText::FromString(Full);
}
