#include "TutorialPresentation.h"

#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "ScenarioActorRegistry.h"
#include "TacticalCameraPawn.h"

void UTutorialPresentationSubsystem::StartBeat(const FTacticalTutorialBeat& Beat)
{
	if (bBeatActive)
	{
		FinishBeat();
	}

	ActiveBeat = Beat;
	bBeatActive = true;

	// Камера наводится здесь, а не в BP: точка задаётся стабильным AnchorId и
	// потому переживает переименование актора в Outliner.
	if (!Beat.FocusAnchorId.IsNone())
	{
		if (const AActor* FocusActor =
			UTacticalScenarioSubsystem::FindScenarioActorInWorld(this, Beat.FocusAnchorId))
		{
			const APlayerController* PlayerController = GetWorld()
				? GetWorld()->GetFirstPlayerController() : nullptr;
			if (ATacticalCameraPawn* Camera = PlayerController
				? Cast<ATacticalCameraPawn>(PlayerController->GetPawn()) : nullptr)
			{
				Camera->FocusOnLocation(FocusActor->GetActorLocation());
			}
		}
	}

	OnBeatStarted.Broadcast(ActiveBeat);
}

void UTutorialPresentationSubsystem::FinishBeat()
{
	if (!bBeatActive)
	{
		return;
	}

	bBeatActive = false;
	const FTacticalTutorialBeat FinishedBeat = ActiveBeat;
	ActiveBeat = FTacticalTutorialBeat();
	OnBeatFinished.Broadcast(FinishedBeat);
}
