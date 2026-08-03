#include "ObjectivePointerSubsystem.h"

#include "MissionObjectives.h"
#include "ScenarioActorRegistry.h"
#include "TacticalHUDStyleData.h"
#include "TacticalPlayerController.h"
#include "TacticsGameInstance.h"
#include "TurnManagerSubsystem.h"
#include "XRU1Log.h"

#include "Camera/PlayerCameraManager.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontMeasure.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"

UObjectivePointerSubsystem* UObjectivePointerSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = (WorldContext && GEngine)
		? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull)
		: nullptr;
	return World ? World->GetSubsystem<UObjectivePointerSubsystem>() : nullptr;
}

bool UObjectivePointerSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UObjectivePointerSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UObjectivePointerSubsystem, STATGROUP_Tickables);
}

const UTacticalHUDStyleData* UObjectivePointerSubsystem::ResolveTheme() const
{
	const UWorld* World = GetWorld();
	const UTacticsGameInstance* GameInstance = World
		? World->GetGameInstance<UTacticsGameInstance>() : nullptr;
	return GameInstance ? GameInstance->GetUITheme() : nullptr;
}

// --- Регистрация ---------------------------------------------------------------

void UObjectivePointerSubsystem::RegisterObjective(FName Id, AActor* Anchor, const FText& Label,
	EObjectivePointerRule Rule, EObjectivePointerTone Tone, bool bShowTurnsRemaining)
{
	if (Id.IsNone() || !Anchor)
	{
		return;
	}

	FObjectivePointerEntry Entry;
	Entry.Id = Id;
	Entry.Anchor = Anchor;
	Entry.Label = Label;
	Entry.Rule = Rule;
	Entry.Tone = Tone;
	Entry.bShowTurnsRemaining = bShowTurnsRemaining;

	// Замена по ключу, а не добавление: повторный старт сценария на той же карте
	// иначе накапливал бы дубли указателей на одну и ту же бомбу.
	if (FObjectivePointerEntry* Existing = Entries.FindByPredicate(
		[Id](const FObjectivePointerEntry& Other) { return Other.Id == Id; }))
	{
		*Existing = MoveTemp(Entry);
	}
	else
	{
		Entries.Add(MoveTemp(Entry));
	}

	UE_LOG(LogXRU1UI, Log, TEXT("[Objective] указатель «%s» → %s"),
		*Id.ToString(), *GetNameSafe(Anchor));
	EnsureOverlay();
}

void UObjectivePointerSubsystem::UnregisterObjective(FName Id)
{
	const int32 Removed = Entries.RemoveAll(
		[Id](const FObjectivePointerEntry& Entry) { return Entry.Id == Id; });
	if (Removed > 0)
	{
		UE_LOG(LogXRU1UI, Log, TEXT("[Objective] указатель «%s» снят"), *Id.ToString());
	}
}

void UObjectivePointerSubsystem::ClearAllObjectives()
{
	if (!Entries.IsEmpty())
	{
		UE_LOG(LogXRU1UI, Log, TEXT("[Objective] сняты все указатели (%d)"), Entries.Num());
	}
	Entries.Reset();
}

// --- Состояние -----------------------------------------------------------------

bool UObjectivePointerSubsystem::IsEntryActive(const FObjectivePointerEntry& Entry) const
{
	const AActor* Anchor = Entry.Anchor.Get();
	if (!Anchor)
	{
		return false;
	}

	// Сценарно выключенный актор (голограмма следующей секции, зона будущего
	// шага) не показывается: он физически стоит на карте, но в мире его ещё нет.
	if (!UTacticalScenarioSubsystem::IsActorScenarioActive(Anchor))
	{
		return false;
	}

	switch (Entry.Rule)
	{
	case EObjectivePointerRule::WhileBombArmed:
		if (const ABombObjective* Bomb = Cast<ABombObjective>(Anchor))
		{
			return !Bomb->IsDisarmed();
		}
		return false;

	case EObjectivePointerRule::WhileEvacActive:
		if (const AEvacZone* Zone = Cast<AEvacZone>(Anchor))
		{
			return Zone->IsActive();
		}
		return false;

	case EObjectivePointerRule::Always:
	default:
		return true;
	}
}

bool UObjectivePointerSubsystem::ResolveWorldPoint(const FObjectivePointerEntry& Entry,
	FVector& OutPoint) const
{
	if (!IsEntryActive(Entry))
	{
		return false;
	}
	const AActor* Anchor = Entry.Anchor.Get();
	const UTacticalHUDStyleData* Theme = ResolveTheme();
	const float ZOffset = Theme ? Theme->ObjectivePointerWorldZOffset : 150.f;
	OutPoint = Anchor->GetActorLocation() + FVector(0.f, 0.f, ZOffset);
	return true;
}

bool UObjectivePointerSubsystem::ShouldDrawNow() const
{
	const UWorld* World = GetWorld();
	if (!World || Entries.IsEmpty())
	{
		return false;
	}

	// XCOM прячет стрелки, пока поднято меню выстрела (`m_isMenuRaised`): кадр
	// в этот момент принадлежит прицеливанию. Наш эквивалент — режим цели и
	// любой удерживаемый кадр камеры (выстрел, реакция, режиссура такта).
	const ATacticalPlayerController* Controller =
		Cast<ATacticalPlayerController>(World->GetFirstPlayerController());
	return !Controller || !Controller->IsCameraBusyForOverlays();
}

int32 UObjectivePointerSubsystem::GetTurnsRemaining() const
{
	const UWorld* World = GetWorld();
	const UTurnManagerSubsystem* TurnManager = World
		? World->GetSubsystem<UTurnManagerSubsystem>() : nullptr;
	return TurnManager ? TurnManager->GetTurnsRemaining() : -1;
}

// --- Тик и жизненный цикл -------------------------------------------------------

void UObjectivePointerSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// Единственная работа тика — уборка записей, чей актор уничтожен (выгрузка
	// сценарного сублевела). Всё остальное решается в момент отрисовки, чтобы
	// состояние указателя не могло отстать от состояния цели ни на кадр.
	Entries.RemoveAll([](const FObjectivePointerEntry& Entry)
	{
		return !Entry.Anchor.IsValid();
	});

	// Оверлей мог не создаться в момент регистрации (viewport ещё не готов при
	// раннем старте боя) — досоздаём здесь. Повторный вызов бесплатен.
	if (!Entries.IsEmpty())
	{
		EnsureOverlay();
	}
}

void UObjectivePointerSubsystem::EnsureOverlay()
{
	if (Overlay.IsValid() || !GEngine || !GEngine->GameViewport)
	{
		return;
	}
	Overlay = SNew(SObjectivePointerOverlay)
		.OwnerSubsystem(TWeakObjectPtr<UObjectivePointerSubsystem>(this));
	// Лестница Slate-слоёв проекта (значение обязано быть УНИКАЛЬНЫМ, иначе
	// порядок решает случайный порядок создания):
	//   7 — указатели на цели (здесь),
	//   8 — трекер целей обучения (ATacticalPlayerController),
	//   9 — всплывающий боевой фидбек (UCombatFeedbackSubsystem),
	//  выше — субтитры (настройка UXRU1SubtitleSettings::DisplayZOrder).
	// Указатель самый нижний осознанно: подсказка обучения важнее и не должна
	// оказаться под стрелкой.
	GEngine->GameViewport->AddViewportWidgetContent(Overlay.ToSharedRef(), /*ZOrder=*/7);
}

void UObjectivePointerSubsystem::Deinitialize()
{
	if (Overlay.IsValid() && GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(Overlay.ToSharedRef());
	}
	Overlay.Reset();
	Entries.Empty();
	Super::Deinitialize();
}

// --- SObjectivePointerOverlay ---------------------------------------------------

void SObjectivePointerOverlay::Construct(const FArguments& InArgs)
{
	OwnerSubsystem = InArgs._OwnerSubsystem;
	SetVisibility(EVisibility::HitTestInvisible);
}

int32 SObjectivePointerOverlay::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId,
	const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const UObjectivePointerSubsystem* Subsystem = OwnerSubsystem.Get();
	if (!Subsystem || !Subsystem->ShouldDrawNow())
	{
		return LayerId;
	}
	const UWorld* World = Subsystem->GetWorld();
	APlayerController* PlayerController = World ? World->GetFirstPlayerController() : nullptr;
	if (!PlayerController)
	{
		return LayerId;
	}

	const UTacticalHUDStyleData* Theme = Subsystem->ResolveTheme();
	const float EdgePadding = Theme ? Theme->ObjectivePointerEdgePadding : 72.f;
	const float ArrowSize = Theme ? Theme->ObjectivePointerArrowSize : 22.f;
	const int32 FontSize = Theme ? Theme->ObjectivePointerFontSize : 13;
	const FLinearColor NormalColor = Theme
		? Theme->ObjectivePointerNormalColor : FLinearColor(0.35f, 0.85f, 1.f, 1.f);
	const FLinearColor UrgentColor = Theme
		? Theme->ObjectivePointerUrgentColor : FLinearColor(1.f, 0.62f, 0.15f, 1.f);

	const TSharedRef<FSlateFontMeasure> FontMeasure =
		FSlateApplication::Get().GetRenderer()->GetFontMeasureService();

	const float Scale = AllottedGeometry.Scale > 0.f ? AllottedGeometry.Scale : 1.f;
	const FVector2D ViewSize = AllottedGeometry.GetLocalSize();
	const FVector2D ViewCenter = ViewSize * 0.5f;
	// Прямоугольник, за который стрелка не заходит.
	const FVector2D SafeMin(EdgePadding, EdgePadding);
	const FVector2D SafeMax = ViewSize - FVector2D(EdgePadding, EdgePadding);

	// Ориентация камеры одинакова для всех указателей — считаем один раз.
	// ⚠️ Проекции доверяем ТОЛЬКО когда цель перед камерой. За камерой
	// `ProjectWorldLocationToScreen` возвращает false и вправе вообще не записать
	// результат — читать его в этом случае значит строить стрелку по мусору.
	// Поэтому направление для таких целей считается в пространстве камеры, а не
	// из экранных координат.
	const FVector CameraLocation = PlayerController->PlayerCameraManager
		? PlayerController->PlayerCameraManager->GetCameraLocation() : FVector::ZeroVector;
	const FRotator CameraRotation = PlayerController->PlayerCameraManager
		? PlayerController->PlayerCameraManager->GetCameraRotation() : FRotator::ZeroRotator;
	const FRotationMatrix CameraAxes(CameraRotation);
	const FVector CameraForward = CameraAxes.GetUnitAxis(EAxis::X);
	const FVector CameraRight = CameraAxes.GetUnitAxis(EAxis::Y);
	const FVector CameraUp = CameraAxes.GetUnitAxis(EAxis::Z);

	int32 Layer = LayerId;
	for (const FObjectivePointerEntry& Entry : Subsystem->GetEntries())
	{
		FVector WorldPoint;
		if (!Subsystem->ResolveWorldPoint(Entry, WorldPoint))
		{
			continue; // цель выполнена, выключена сценарием или уничтожена
		}

		const FVector ToTarget = WorldPoint - CameraLocation;
		const bool bInFront = FVector::DotProduct(ToTarget, CameraForward) > 0.f;

		FVector2D ScreenPos = FVector2D::ZeroVector;
		const bool bProjected = bInFront && UGameplayStatics::ProjectWorldToScreen(
			PlayerController, WorldPoint, ScreenPos, /*bPlayerViewportRelative=*/true);

		FVector2D LocalPos;
		if (bProjected)
		{
			LocalPos = ScreenPos / Scale;
		}
		else
		{
			// Направление на цель в осях камеры: X экрана — вправо, Y — вниз.
			const FVector2D CameraSpaceDir(
				FVector::DotProduct(ToTarget, CameraRight),
				-FVector::DotProduct(ToTarget, CameraUp));
			// Уносим точку заведомо за пределы кадра — ниже её прижмёт к краю.
			LocalPos = ViewCenter + CameraSpaceDir.GetSafeNormal() * (ViewSize.Size() + 1.f);
		}

		const bool bOnScreen = bProjected &&
			LocalPos.X >= SafeMin.X && LocalPos.X <= SafeMax.X &&
			LocalPos.Y >= SafeMin.Y && LocalPos.Y <= SafeMax.Y;

		const FLinearColor Color = (Entry.Tone == EObjectivePointerTone::Urgent)
			? UrgentColor : NormalColor;

		FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Bold", FontSize);
		Font.OutlineSettings = FFontOutlineSettings(2, FLinearColor(0.f, 0.f, 0.f, 0.9f));

		// Подпись: название цели и, если запрошено, остаток ходов — аналог
		// `CounterValue` у стрелки XCOM.
		FText Caption = Entry.Label;
		if (Entry.bShowTurnsRemaining)
		{
			const int32 Turns = Subsystem->GetTurnsRemaining();
			if (Turns >= 0)
			{
				Caption = FText::Format(
					NSLOCTEXT("XRU1.Objective", "PointerWithTurns", "{0} · {1}"),
					Entry.Label, FText::AsNumber(Turns));
			}
		}
		const FVector2D CaptionSize = FontMeasure->Measure(Caption, Font);

		if (bOnScreen)
		{
			// Цель в кадре — стрелка не нужна, достаточно подписи над ней.
			const FVector2D TextPos(LocalPos.X - CaptionSize.X * 0.5f, LocalPos.Y - CaptionSize.Y);
			FSlateDrawElement::MakeText(OutDrawElements, Layer + 1,
				AllottedGeometry.ToPaintGeometry(CaptionSize, FSlateLayoutTransform(TextPos)),
				Caption, Font, ESlateDrawEffect::None, Color);
			continue;
		}

		// Цель за кадром: прижимаем к краю безопасной области и разворачиваем
		// треугольник в её сторону.
		const FVector2D Direction = (LocalPos - ViewCenter).GetSafeNormal();
		if (Direction.IsNearlyZero())
		{
			continue;
		}
		FVector2D Clamped = LocalPos;
		Clamped.X = FMath::Clamp(Clamped.X, SafeMin.X, SafeMax.X);
		Clamped.Y = FMath::Clamp(Clamped.Y, SafeMin.Y, SafeMax.Y);

		// Треугольник строим вручную линиями: своей текстуры-стрелки в проекте
		// нет, а тянуть её ради указателя незачем — форма читается и линиями.
		const FVector2D Side(-Direction.Y, Direction.X);
		const FVector2D Tip = Clamped + Direction * ArrowSize * 0.5f;
		const FVector2D BaseA = Clamped - Direction * ArrowSize * 0.5f + Side * ArrowSize * 0.4f;
		const FVector2D BaseB = Clamped - Direction * ArrowSize * 0.5f - Side * ArrowSize * 0.4f;

		TArray<FVector2D> Triangle;
		Triangle.Reserve(4);
		Triangle.Add(Tip);
		Triangle.Add(BaseA);
		Triangle.Add(BaseB);
		Triangle.Add(Tip);
		FSlateDrawElement::MakeLines(OutDrawElements, Layer + 1,
			AllottedGeometry.ToPaintGeometry(), Triangle, ESlateDrawEffect::None, Color,
			/*bAntialias=*/true, /*Thickness=*/2.f);

		// Подпись — с внутренней стороны от стрелки, чтобы не уезжала за экран.
		const FVector2D TextAnchor = Clamped - Direction * (ArrowSize * 0.5f + CaptionSize.Y);
		const FVector2D TextPos(
			FMath::Clamp(TextAnchor.X - CaptionSize.X * 0.5f, 4.f, ViewSize.X - CaptionSize.X - 4.f),
			FMath::Clamp(TextAnchor.Y - CaptionSize.Y * 0.5f, 4.f, ViewSize.Y - CaptionSize.Y - 4.f));
		FSlateDrawElement::MakeText(OutDrawElements, Layer + 1,
			AllottedGeometry.ToPaintGeometry(CaptionSize, FSlateLayoutTransform(TextPos)),
			Caption, Font, ESlateDrawEffect::None, Color);
	}

	return Layer + 1;
}
