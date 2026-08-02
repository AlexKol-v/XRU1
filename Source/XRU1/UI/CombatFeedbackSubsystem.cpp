#include "CombatFeedbackSubsystem.h"

#include "TacticalHUDStyleData.h"
#include "TacticsGameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"

// --- UCombatFeedbackSubsystem ------------------------------------------------

UCombatFeedbackSubsystem* UCombatFeedbackSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = (WorldContext && GEngine)
		? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull)
		: nullptr;
	return World ? World->GetSubsystem<UCombatFeedbackSubsystem>() : nullptr;
}

bool UCombatFeedbackSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UCombatFeedbackSubsystem::ShowDamage(AActor* Target, float Amount)
{
	const int32 Rounded = FMath::Max(1, FMath::RoundToInt(FMath::Abs(Amount)));
	PushEntry(Target, FText::AsNumber(Rounded), ECombatFeedbackKind::Damage);
}

void UCombatFeedbackSubsystem::ShowHeal(AActor* Target, float Amount)
{
	const int32 Rounded = FMath::Max(1, FMath::RoundToInt(FMath::Abs(Amount)));
	PushEntry(Target,
		FText::Format(NSLOCTEXT("XRU1.Feedback", "HealFmt", "+{0}"), FText::AsNumber(Rounded)),
		ECombatFeedbackKind::Heal);
}

void UCombatFeedbackSubsystem::ShowMiss(AActor* Target)
{
	PushEntry(Target, NSLOCTEXT("XRU1.Feedback", "Miss", "ПРОМАХ"), ECombatFeedbackKind::Miss);
}

void UCombatFeedbackSubsystem::ShowStatusText(AActor* Target, const FText& Text)
{
	PushEntry(Target, Text, ECombatFeedbackKind::Status);
}

const UTacticalHUDStyleData* UCombatFeedbackSubsystem::ResolveTheme() const
{
	const UWorld* World = GetWorld();
	const UTacticsGameInstance* GameInstance = World
		? World->GetGameInstance<UTacticsGameInstance>() : nullptr;
	return GameInstance ? GameInstance->GetUITheme() : nullptr;
}

void UCombatFeedbackSubsystem::PushEntry(AActor* Target, const FText& Text, ECombatFeedbackKind Kind)
{
	UWorld* World = GetWorld();
	if (!World || !Target || Text.IsEmpty())
	{
		return;
	}

	const UTacticalHUDStyleData* Theme = ResolveTheme();
	// Дефолты совпадают с дефолтами полей темы: без DataAsset фидбек всё равно виден.
	const float Duration = Theme ? Theme->FloatingTextDuration : 1.4f;
	const float ZOffset = Theme ? Theme->FloatingTextWorldZOffset : 120.f;

	FCombatFloatingTextEntry Entry;
	Entry.Text = Text;
	Entry.SpawnTime = World->GetTimeSeconds();
	Entry.Duration = FMath::Max(0.2f, Duration);
	Entry.Anchor = Target;
	Entry.LastWorldPos = Target->GetActorLocation() + FVector(0.f, 0.f, ZOffset);
	// Чередующийся сдвиг, чтобы залп из нескольких надписей не сливался в кашу.
	Entry.JitterX = float((SpawnCounter++ % 3) - 1) * 22.f;

	switch (Kind)
	{
	case ECombatFeedbackKind::Damage:
		Entry.Color = Theme ? Theme->FloatingDamageColor : FLinearColor(1.f, 0.25f, 0.2f, 1.f);
		Entry.FontSize = Theme ? Theme->FloatingDamageFontSize : 20;
		break;
	case ECombatFeedbackKind::Heal:
		Entry.Color = Theme ? Theme->FloatingHealColor : FLinearColor(0.08f, 0.72f, 0.36f, 1.f);
		Entry.FontSize = Theme ? Theme->FloatingDamageFontSize : 20;
		break;
	case ECombatFeedbackKind::Miss:
		Entry.Color = Theme ? Theme->FloatingMissColor : FLinearColor(0.85f, 0.9f, 0.95f, 1.f);
		Entry.FontSize = Theme ? Theme->FloatingTextFontSize : 16;
		break;
	case ECombatFeedbackKind::Status:
	default:
		Entry.Color = Theme ? Theme->FloatingStatusColor : FLinearColor(0.72f, 0.94f, 1.f, 1.f);
		Entry.FontSize = Theme ? Theme->FloatingTextFontSize : 16;
		break;
	}

	Entries.Add(MoveTemp(Entry));
	EnsureOverlay();
}

void UCombatFeedbackSubsystem::EnsureOverlay()
{
	if (Overlay.IsValid() || !GEngine || !GEngine->GameViewport)
	{
		return;
	}
	Overlay = SNew(SCombatFloatingTextOverlay)
		.OwnerSubsystem(TWeakObjectPtr<UCombatFeedbackSubsystem>(this));
	// ZOrder 9: над Slate-трекером обучения (8), под системными попапами.
	GEngine->GameViewport->AddViewportWidgetContent(Overlay.ToSharedRef(), /*ZOrder=*/9);
}

void UCombatFeedbackSubsystem::Tick(float DeltaTime)
{
	// Надписей нет почти всё время боя — выходим до любой работы, включая
	// поиск темы через GameInstance.
	if (Entries.IsEmpty())
	{
		return;
	}

	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	const double Now = World->GetTimeSeconds();

	const UTacticalHUDStyleData* Theme = ResolveTheme();
	const float ZOffset = Theme ? Theme->FloatingTextWorldZOffset : 120.f;

	for (int32 Index = Entries.Num() - 1; Index >= 0; --Index)
	{
		FCombatFloatingTextEntry& Entry = Entries[Index];
		if (Now - Entry.SpawnTime >= Entry.Duration)
		{
			Entries.RemoveAtSwap(Index);
			continue;
		}
		// Слежение за живым якорем; после смерти/эвакуации позиция замирает.
		if (AActor* Anchor = Entry.Anchor.Get())
		{
			Entry.LastWorldPos = Anchor->GetActorLocation() + FVector(0.f, 0.f, ZOffset);
		}
	}
}

TStatId UCombatFeedbackSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UCombatFeedbackSubsystem, STATGROUP_Tickables);
}

void UCombatFeedbackSubsystem::Deinitialize()
{
	if (Overlay.IsValid() && GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(Overlay.ToSharedRef());
	}
	Overlay.Reset();
	Entries.Empty();
	Super::Deinitialize();
}

// --- SCombatFloatingTextOverlay ----------------------------------------------

void SCombatFloatingTextOverlay::Construct(const FArguments& InArgs)
{
	OwnerSubsystem = InArgs._OwnerSubsystem;
	// Оверлей чисто визуальный: клики проходят в игру насквозь.
	SetVisibility(EVisibility::HitTestInvisible);
}

int32 SCombatFloatingTextOverlay::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId,
	const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const UCombatFeedbackSubsystem* Subsystem = OwnerSubsystem.Get();
	if (!Subsystem || Subsystem->GetEntries().IsEmpty())
	{
		return LayerId;
	}
	const UWorld* World = Subsystem->GetWorld();
	APlayerController* PlayerController = World ? World->GetFirstPlayerController() : nullptr;
	if (!PlayerController)
	{
		return LayerId;
	}

	const double Now = World->GetTimeSeconds();
	const UTacticsGameInstance* GameInstance = World->GetGameInstance<UTacticsGameInstance>();
	const UTacticalHUDStyleData* Theme = GameInstance ? GameInstance->GetUITheme() : nullptr;
	const float RiseSpeed = Theme ? Theme->FloatingTextRiseSpeed : 42.f;

	const TSharedRef<FSlateFontMeasure> FontMeasure =
		FSlateApplication::Get().GetRenderer()->GetFontMeasureService();

	for (const FCombatFloatingTextEntry& Entry : Subsystem->GetEntries())
	{
		FVector2D ScreenPos;
		if (!UGameplayStatics::ProjectWorldToScreen(PlayerController, Entry.LastWorldPos,
			ScreenPos, /*bPlayerViewportRelative=*/true))
		{
			continue; // за спиной камеры
		}

		const float Age = float(Now - Entry.SpawnTime);
		const float Alpha01 = FMath::Clamp(Age / Entry.Duration, 0.f, 1.f);
		// Быстрое появление, плавное затухание в последней трети жизни.
		const float FadeIn = FMath::Clamp(Age / 0.12f, 0.f, 1.f);
		const float FadeOut = 1.f - FMath::Clamp((Alpha01 - 0.66f) / 0.34f, 0.f, 1.f);
		FLinearColor Color = Entry.Color;
		Color.A *= FadeIn * FadeOut;

		FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Bold", Entry.FontSize);
		Font.OutlineSettings = FFontOutlineSettings(2, FLinearColor(0.f, 0.f, 0.f, 0.85f * Color.A));

		// Экранные пиксели viewport → локальные slate-координаты оверлея.
		const float Scale = AllottedGeometry.Scale > 0.f ? AllottedGeometry.Scale : 1.f;
		const FVector2D TextSize = FontMeasure->Measure(Entry.Text, Font);
		FVector2D LocalPos = ScreenPos / Scale;
		LocalPos.X += Entry.JitterX - TextSize.X * 0.5f;
		LocalPos.Y -= Age * RiseSpeed + TextSize.Y;

		FSlateDrawElement::MakeText(
			OutDrawElements,
			LayerId + 1,
			AllottedGeometry.ToPaintGeometry(TextSize, FSlateLayoutTransform(LocalPos)),
			Entry.Text,
			Font,
			ESlateDrawEffect::None,
			Color);
	}

	return LayerId + 1;
}
