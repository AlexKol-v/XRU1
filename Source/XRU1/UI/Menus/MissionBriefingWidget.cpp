#include "MissionBriefingWidget.h"

#include "MissionPointOfInterest.h"
#include "TacticalHUDStyleData.h"
#include "TacticalScenarioDataAsset.h"
#include "TacticsAudioSubsystem.h"
#include "XRU1Log.h"
#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"

void UMissionBriefingWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	// До SetupFromPOI вид сценария неизвестен; учебный брифинг — безопасный
	// стартовый арт, дальше его уточняет RefreshFromPOI.
	ScreenArtKind = EXRU1UIScreenArt::TutorialBriefing;

	if (Btn_Start) { Btn_Start->OnClicked.AddUniqueDynamic(this, &UMissionBriefingWidget::HandleStartClicked); RegisterButtonSounds(Btn_Start); }
	if (Btn_Back)  { Btn_Back->OnClicked.AddUniqueDynamic(this, &UMissionBriefingWidget::HandleBackClicked);   RegisterButtonSounds(Btn_Back); }
}

void UMissionBriefingWidget::SetupFromPOI(AMissionPointOfInterest* POI)
{
	PointOfInterest = POI;
	RefreshFromPOI();
	PlayBriefingVoice();
	OnBriefingReady(POI);
}

void UMissionBriefingWidget::PlayBriefingVoice()
{
	// Реплика принадлежит МИССИИ, а не экрану: брифинг один на все операции.
	const UTacticalScenarioDataAsset* Scenario = PointOfInterest ? PointOfInterest->Scenario : nullptr;
	if (!Scenario || Scenario->BriefingVoice.IsNull())
	{
		return; // реплика не записана — экран просто молчит
	}
	USoundBase* Line = Scenario->BriefingVoice.LoadSynchronous(); // экран не горячий путь
	if (UTacticsAudioSubsystem* Audio = GetAudioSubsystem())
	{
		Audio->PlayVoice2D(Line);
	}
}

void UMissionBriefingWidget::RefreshFromPOI()
{
	const UTacticalScenarioDataAsset* Scenario = PointOfInterest ? PointOfInterest->Scenario : nullptr;

	// Роль арта определяет ВИД сценария, а не имя точки: учебный полигон и
	// боевая операция должны выглядеть по-разному даже при одном маркере.
	ScreenArtKind = (Scenario && Scenario->Kind != ETacticalScenarioKind::Tutorial)
		? EXRU1UIScreenArt::MissionBriefing
		: EXRU1UIScreenArt::TutorialBriefing;
	// Экран уже активен к моменту SetupFromPOI, и NativeOnActivated с прежним
	// видом арта успел отработать — подставляем фон ещё раз, уже правильный.
	ApplyScreenArt();

	if (Txt_BriefTitle)
	{
		Txt_BriefTitle->SetText(PointOfInterest
			? PointOfInterest->GetDisplayTitle()
			: NSLOCTEXT("XRU1.Briefing", "NoMission", "МИССИЯ НЕ ВЫБРАНА"));
	}
	if (Txt_BriefText)
	{
		Txt_BriefText->SetText(PointOfInterest
			? PointOfInterest->GetDisplayDescription()
			: FText::GetEmpty());
	}

	const FText LockedReason = PointOfInterest ? PointOfInterest->GetLockedReason() : FText::GetEmpty();
	const bool bLocked = !LockedReason.IsEmpty();
	if (Txt_BriefStatus)
	{
		Txt_BriefStatus->SetText(bLocked
			? LockedReason
			: NSLOCTEXT("XRU1.Briefing", "Ready", "Отряд готов к переброске"));
	}
	if (Btn_Start)
	{
		Btn_Start->SetIsEnabled(PointOfInterest != nullptr && !bLocked);
	}

	// Отдельный Image арта нужен экранам, где брифинг рисуется не на весь
	// экран; при его отсутствии роль полотна играет фон из ApplyScreenArt.
	if (Img_BriefArt)
	{
		if (const UTacticalHUDStyleData* Theme = GetUITheme())
		{
			const FXRU1UIScreenArtwork Artwork = Theme->GetScreenArtwork(ScreenArtKind);
			if (UTexture2D* Texture = Artwork.Texture.LoadSynchronous())
			{
				Img_BriefArt->SetBrushFromTexture(Texture);
				Img_BriefArt->SetColorAndOpacity(Artwork.Tint);
				Img_BriefArt->SetVisibility(ESlateVisibility::HitTestInvisible);
			}
			else
			{
				Img_BriefArt->SetVisibility(ESlateVisibility::Collapsed);
			}
		}
	}

	UE_LOG(LogXRU1UI, Display, TEXT("[Briefing] точка '%s', сценарий '%s', %s"),
		PointOfInterest ? *PointOfInterest->GetActorNameOrLabel() : TEXT("<нет>"),
		*GetNameSafe(Scenario),
		bLocked ? *LockedReason.ToString() : TEXT("доступна"));
}

void UMissionBriefingWidget::HandleStartClicked()
{
	StartOperation();
}

void UMissionBriefingWidget::StartOperation()
{
	if (!PointOfInterest)
	{
		return;
	}
	// Запуск сценария заново не пишется: гейт, сохранение LastHubPointOfInterest
	// и StartCombatScenario уже живут в точке интереса.
	PointOfInterest->SelectPointOfInterest();
}
