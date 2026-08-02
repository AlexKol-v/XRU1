#include "XRU1WidgetAuthoringLibrary.h"

#include "XRU1Log.h"

#if WITH_EDITOR

#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"

#include "TacticalHUDStyleData.h"

#include "Components/Border.h"
#include "Components/BorderSlot.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/CheckBox.h"
#include "Components/ComboBoxString.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/SizeBoxSlot.h"
#include "Components/Slider.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"

namespace
{

/** Палитра сборки: тема проекта либо её дефолты, если DataAsset не найден. */
struct FMenuPalette
{
	FLinearColor PanelBackground = FLinearColor(0.015f, 0.025f, 0.035f, 0.88f);
	FLinearColor PrimaryText = FLinearColor::White;
	FLinearColor MutedText = FLinearColor(0.55f, 0.65f, 0.69f, 1.f);
	FLinearColor Warning = FLinearColor(1.f, 0.72f, 0.08f, 1.f);
	FLinearColor ButtonBackground = FLinearColor(0.025f, 0.055f, 0.075f, 0.96f);
	FLinearColor ButtonForeground = FLinearColor(0.72f, 0.94f, 1.f, 1.f);
};

FMenuPalette LoadPalette()
{
	FMenuPalette Palette;
	// Цвета не хардкодятся мимо темы: сборка читает DA_TacticalHUDStyle
	// (09_UI_HUD §1); дефолты структуры совпадают с дефолтами C++-темы.
	if (const UTacticalHUDStyleData* Theme = LoadObject<UTacticalHUDStyleData>(nullptr,
		TEXT("/Game/XRU1Game/Data/DA_TacticalHUDStyle.DA_TacticalHUDStyle")))
	{
		Palette.PanelBackground = Theme->PanelBackgroundColor;
		Palette.PrimaryText = Theme->PrimaryTextColor;
		Palette.MutedText = Theme->MutedTextColor;
		Palette.Warning = Theme->WarningColor;
		Palette.ButtonBackground = Theme->ActionButtonPalette.NormalBackground;
		Palette.ButtonForeground = Theme->ActionButtonPalette.NormalForeground;
	}
	return Palette;
}

/**
 * Уникальное имя для служебного виджета вёрстки.
 *
 * Безымянные (`NAME_None`) виджеты компилятор UMG 5.7 встречает ensure'ом
 * «was added but did not get a GUID» — на каждый такой виджет в лог падает
 * полный стек. Имя дешевле, чем сотни килобайт шума.
 */
FName MakeAutoName(const TCHAR* Prefix)
{
	static int32 Counter = 0;
	return FName(*FString::Printf(TEXT("%s_%d"), Prefix, ++Counter));
}

/** "/Game/A/WBP_X" → "/Game/A/WBP_X.WBP_X"; уже полное имя не трогается. */
FString NormalizeAssetPath(const FString& AssetPath)
{
	if (AssetPath.Contains(TEXT(".")))
	{
		return AssetPath;
	}
	FString Short = AssetPath;
	int32 SlashIndex = INDEX_NONE;
	if (AssetPath.FindLastChar(TEXT('/'), SlashIndex))
	{
		Short = AssetPath.Mid(SlashIndex + 1);
	}
	return AssetPath + TEXT(".") + Short;
}

/** Загрузка WBP + защита от затирания рукотворной вёрстки. */
UWidgetBlueprint* LoadTargetBlueprint(const FString& AssetPath, bool bOverwriteExisting)
{
	UWidgetBlueprint* Blueprint = LoadObject<UWidgetBlueprint>(nullptr, *NormalizeAssetPath(AssetPath));
	if (!Blueprint)
	{
		UE_LOG(LogXRU1UI, Error, TEXT("WidgetAuthoring: не найден Widget Blueprint '%s'"), *AssetPath);
		return nullptr;
	}
	if (!Blueprint->WidgetTree)
	{
		UE_LOG(LogXRU1UI, Error, TEXT("WidgetAuthoring: у '%s' нет WidgetTree"), *AssetPath);
		return nullptr;
	}
	if (Blueprint->WidgetTree->RootWidget && !bOverwriteExisting)
	{
		UE_LOG(LogXRU1UI, Error,
			TEXT("WidgetAuthoring: '%s' уже содержит вёрстку; перезапись требует bOverwriteExisting=true"),
			*AssetPath);
		return nullptr;
	}
	return Blueprint;
}

UTextBlock* MakeText(UWidgetTree* Tree, FName Name, const FText& Text, int32 FontSize,
	const FLinearColor& Color, bool bAutoWrap = false)
{
	UTextBlock* Block = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),
		Name.IsNone() ? MakeAutoName(TEXT("Txt_Auto")) : Name);
	Block->SetText(Text);
	FSlateFontInfo Font = Block->GetFont();
	Font.Size = FontSize;
	Block->SetFont(Font);
	Block->SetColorAndOpacity(FSlateColor(Color));
	Block->SetAutoWrapText(bAutoWrap);
	return Block;
}

/** Кнопка с текстовой подписью; действие к ней привяжет C++ по имени. */
UButton* MakeTextButton(UWidgetTree* Tree, FName Name, const FText& Label, const FMenuPalette& Palette)
{
	UButton* Button = Tree->ConstructWidget<UButton>(UButton::StaticClass(), Name);
	Button->SetBackgroundColor(Palette.ButtonBackground);

	UTextBlock* Text = MakeText(Tree, FName(*(Name.ToString() + TEXT("_Label"))), Label, 20, Palette.ButtonForeground);
	Button->AddChild(Text);
	if (UButtonSlot* Slot = Cast<UButtonSlot>(Text->Slot))
	{
		Slot->SetPadding(FMargin(22.f, 10.f));
		Slot->SetHorizontalAlignment(HAlign_Center);
		Slot->SetVerticalAlignment(VAlign_Center);
	}
	return Button;
}

/**
 * Каркас полноэкранного экрана меню: Overlay-корень, полноэкранное затемнение,
 * центральная панель фиксированной ширины и заголовок. Возвращает VerticalBox
 * содержимого, куда экран добавляет свои контролы.
 */
UVerticalBox* MakeScreenScaffold(UWidgetBlueprint* Blueprint, const FText& Title,
	const FMenuPalette& Palette, float PanelWidth)
{
	UWidgetTree* Tree = Blueprint->WidgetTree;

	UOverlay* Root = Tree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("RootOverlay"));
	Tree->RootWidget = Root;

	// Полноэкранное затемнение, чтобы экран читался поверх 3D-сцены/предыдущего.
	UBorder* Dim = Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("BackgroundDim"));
	FLinearColor DimColor = Palette.PanelBackground;
	DimColor.A = 0.85f;
	Dim->SetBrushColor(DimColor);
	Root->AddChildToOverlay(Dim);
	if (UOverlaySlot* DimSlot = Cast<UOverlaySlot>(Dim->Slot))
	{
		DimSlot->SetHorizontalAlignment(HAlign_Fill);
		DimSlot->SetVerticalAlignment(VAlign_Fill);
	}

	USizeBox* Frame = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("ContentFrame"));
	Frame->SetWidthOverride(PanelWidth);
	Root->AddChildToOverlay(Frame);
	if (UOverlaySlot* FrameSlot = Cast<UOverlaySlot>(Frame->Slot))
	{
		FrameSlot->SetHorizontalAlignment(HAlign_Center);
		FrameSlot->SetVerticalAlignment(VAlign_Center);
	}

	UBorder* Panel = Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("ContentPanel"));
	Panel->SetBrushColor(Palette.PanelBackground);
	Frame->AddChild(Panel);
	if (USizeBoxSlot* PanelSlot = Cast<USizeBoxSlot>(Panel->Slot))
	{
		PanelSlot->SetPadding(FMargin(0.f));
	}

	UVerticalBox* Content = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("ContentBox"));
	Panel->AddChild(Content);
	if (UBorderSlot* ContentSlot = Cast<UBorderSlot>(Content->Slot))
	{
		ContentSlot->SetPadding(FMargin(36.f, 28.f));
	}

	UTextBlock* TitleText = MakeText(Tree, TEXT("Txt_Title"), Title, 32, Palette.PrimaryText);
	Content->AddChildToVerticalBox(TitleText);
	if (UVerticalBoxSlot* TitleSlot = Cast<UVerticalBoxSlot>(TitleText->Slot))
	{
		TitleSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 18.f));
		TitleSlot->SetHorizontalAlignment(HAlign_Center);
	}

	return Content;
}

/**
 * Строка «подпись слева — контрол справа» для экрана настроек.
 *
 * Подпись занимает всё оставшееся место и переносится по словам, а контрол
 * получает фиксированную колонку справа. Обратная схема (фиксированная подпись +
 * Fill-контрол) ломается на длинных названиях: «Вертикальная синхронизация»
 * вылезала из своей колонки прямо под чекбокс.
 */
void AddLabeledRow(UWidgetTree* Tree, UVerticalBox* Parent, const FText& Label,
	UWidget* Control, const FMenuPalette& Palette, float ControlWidth = 420.f)
{
	UHorizontalBox* Row = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), MakeAutoName(TEXT("Row")));
	Parent->AddChildToVerticalBox(Row);
	if (UVerticalBoxSlot* RowSlot = Cast<UVerticalBoxSlot>(Row->Slot))
	{
		RowSlot->SetPadding(FMargin(0.f, 7.f));
	}

	UTextBlock* LabelText = MakeText(Tree, NAME_None, Label, 18, Palette.PrimaryText, /*bAutoWrap=*/true);
	Row->AddChildToHorizontalBox(LabelText);
	if (UHorizontalBoxSlot* LabelSlot = Cast<UHorizontalBoxSlot>(LabelText->Slot))
	{
		LabelSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		LabelSlot->SetVerticalAlignment(VAlign_Center);
		LabelSlot->SetPadding(FMargin(0.f, 0.f, 24.f, 0.f));
	}

	// Колонка контролов одной ширины у всех строк: слайдеры и чекбоксы
	// выстраиваются по одной вертикали.
	USizeBox* ControlBox = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), MakeAutoName(TEXT("Box")));
	ControlBox->SetWidthOverride(ControlWidth);
	Row->AddChildToHorizontalBox(ControlBox);
	if (UHorizontalBoxSlot* ControlSlot = Cast<UHorizontalBoxSlot>(ControlBox->Slot))
	{
		ControlSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
		ControlSlot->SetVerticalAlignment(VAlign_Center);
	}

	ControlBox->AddChild(Control);
	if (USizeBoxSlot* InnerSlot = Cast<USizeBoxSlot>(Control->Slot))
	{
		// Чекбокс прижимается влево своей колонки, растягиваемые контролы
		// (слайдер, выпадающий список) занимают её целиком.
		const bool bStretch = !Control->IsA<UCheckBox>();
		InnerSlot->SetHorizontalAlignment(bStretch ? HAlign_Fill : HAlign_Left);
		InnerSlot->SetVerticalAlignment(VAlign_Center);
	}
}

/** Подзаголовок секции («ЗВУК», «ИЗОБРАЖЕНИЕ»). */
void AddSectionHeader(UWidgetTree* Tree, UVerticalBox* Parent, const FText& Text, const FMenuPalette& Palette)
{
	UTextBlock* Header = MakeText(Tree, NAME_None, Text, 20, Palette.MutedText);
	Parent->AddChildToVerticalBox(Header);
	if (UVerticalBoxSlot* Slot = Cast<UVerticalBoxSlot>(Header->Slot))
	{
		Slot->SetPadding(FMargin(0.f, 16.f, 0.f, 4.f));
	}
}

/** Нижний ряд кнопок экрана. */
void AddButtonRow(UWidgetTree* Tree, UVerticalBox* Parent,
	const TArray<TPair<FName, FText>>& Buttons, const FMenuPalette& Palette)
{
	UHorizontalBox* Row = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), MakeAutoName(TEXT("Row")));
	Parent->AddChildToVerticalBox(Row);
	if (UVerticalBoxSlot* RowSlot = Cast<UVerticalBoxSlot>(Row->Slot))
	{
		RowSlot->SetPadding(FMargin(0.f, 24.f, 0.f, 0.f));
		RowSlot->SetHorizontalAlignment(HAlign_Center);
	}

	for (const TPair<FName, FText>& Desc : Buttons)
	{
		UButton* Button = MakeTextButton(Tree, Desc.Key, Desc.Value, Palette);
		Row->AddChildToHorizontalBox(Button);
		if (UHorizontalBoxSlot* Slot = Cast<UHorizontalBoxSlot>(Button->Slot))
		{
			Slot->SetPadding(FMargin(8.f, 0.f));
		}
	}
}

/** Пометка и перекомпиляция после правки дерева; сохранение — на вызывающем. */
bool FinalizeBlueprint(UWidgetBlueprint* Blueprint)
{
	Blueprint->WidgetTree->Modify();
	Blueprint->Modify();
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	FKismetEditorUtilities::CompileBlueprint(Blueprint);

	const bool bOk = Blueprint->Status != BS_Error;
	UE_LOG(LogXRU1UI, Display, TEXT("WidgetAuthoring: '%s' собран, компиляция %s"),
		*Blueprint->GetName(), bOk ? TEXT("OK") : TEXT("ОШИБКА"));
	return bOk;
}

} // namespace

#endif // WITH_EDITOR

// --- Публичные функции ------------------------------------------------------

bool UXRU1WidgetAuthoringLibrary::BuildSettingsMenuLayout(const FString& AssetPath, bool bOverwriteExisting)
{
#if WITH_EDITOR
	UWidgetBlueprint* Blueprint = LoadTargetBlueprint(AssetPath, bOverwriteExisting);
	if (!Blueprint)
	{
		return false;
	}
	const FMenuPalette Palette = LoadPalette();
	UWidgetTree* Tree = Blueprint->WidgetTree;

	UVerticalBox* Content = MakeScreenScaffold(Blueprint,
		NSLOCTEXT("XRU1.Menu", "SettingsTitle", "НАСТРОЙКИ"), Palette, 900.f);

	// --- Звук: слышимый результат при перетаскивании, имена из STATUS §2.2 ---
	AddSectionHeader(Tree, Content, NSLOCTEXT("XRU1.Menu", "SettingsAudio", "ЗВУК"), Palette);

	const TPair<FName, FText> AudioSliders[] = {
		{ TEXT("Sld_Master"), NSLOCTEXT("XRU1.Menu", "VolMaster", "Общая") },
		{ TEXT("Sld_Music"),  NSLOCTEXT("XRU1.Menu", "VolMusic", "Музыка") },
		{ TEXT("Sld_Sfx"),    NSLOCTEXT("XRU1.Menu", "VolSfx", "Эффекты") },
		{ TEXT("Sld_UI"),     NSLOCTEXT("XRU1.Menu", "VolUI", "Интерфейс") },
		{ TEXT("Sld_Voice"),  NSLOCTEXT("XRU1.Menu", "VolVoice", "Голос") }
	};
	for (const TPair<FName, FText>& Desc : AudioSliders)
	{
		USlider* Slider = Tree->ConstructWidget<USlider>(USlider::StaticClass(), Desc.Key);
		Slider->SetMinValue(0.f);
		Slider->SetMaxValue(1.f);
		Slider->SetValue(1.f);
		AddLabeledRow(Tree, Content, Desc.Value, Slider, Palette);
	}

	// --- Изображение: применение по кнопке «Применить» -----------------------
	AddSectionHeader(Tree, Content, NSLOCTEXT("XRU1.Menu", "SettingsVideo", "ИЗОБРАЖЕНИЕ"), Palette);

	// Опции качества добавит NativeOnInitialized — порядок 0..3 задаёт C++.
	UComboBoxString* Quality = Tree->ConstructWidget<UComboBoxString>(
		UComboBoxString::StaticClass(), TEXT("Cmb_Quality"));
	AddLabeledRow(Tree, Content, NSLOCTEXT("XRU1.Menu", "VideoQuality", "Качество"), Quality, Palette);

	USlider* ResolutionScale = Tree->ConstructWidget<USlider>(
		USlider::StaticClass(), TEXT("Sld_ResolutionScale"));
	ResolutionScale->SetMinValue(0.25f);
	ResolutionScale->SetMaxValue(1.f);
	ResolutionScale->SetValue(1.f);
	AddLabeledRow(Tree, Content,
		NSLOCTEXT("XRU1.Menu", "VideoResScale", "Масштаб разрешения"), ResolutionScale, Palette);

	UCheckBox* Fullscreen = Tree->ConstructWidget<UCheckBox>(UCheckBox::StaticClass(), TEXT("Chk_Fullscreen"));
	AddLabeledRow(Tree, Content,
		NSLOCTEXT("XRU1.Menu", "VideoFullscreen", "Полноэкранный режим"), Fullscreen, Palette);

	UCheckBox* VSync = Tree->ConstructWidget<UCheckBox>(UCheckBox::StaticClass(), TEXT("Chk_VSync"));
	AddLabeledRow(Tree, Content,
		NSLOCTEXT("XRU1.Menu", "VideoVSync", "Вертикальная синхронизация"), VSync, Palette);

	AddButtonRow(Tree, Content, {
		{ TEXT("Btn_Apply"), NSLOCTEXT("XRU1.Menu", "Apply", "Применить") },
		{ TEXT("Btn_Reset"), NSLOCTEXT("XRU1.Menu", "Reset", "Сбросить") },
		{ TEXT("Btn_Back"),  NSLOCTEXT("XRU1.Menu", "Back", "Назад") }
	}, Palette);

	return FinalizeBlueprint(Blueprint);
#else
	return false;
#endif
}

bool UXRU1WidgetAuthoringLibrary::BuildAboutMenuLayout(const FString& AssetPath, bool bOverwriteExisting)
{
#if WITH_EDITOR
	UWidgetBlueprint* Blueprint = LoadTargetBlueprint(AssetPath, bOverwriteExisting);
	if (!Blueprint)
	{
		return false;
	}
	const FMenuPalette Palette = LoadPalette();
	UWidgetTree* Tree = Blueprint->WidgetTree;

	UVerticalBox* Content = MakeScreenScaffold(Blueprint,
		NSLOCTEXT("XRU1.Menu", "AboutTitle", "ОБ АВТОРЕ"), Palette, 720.f);

	// Тексты подставит NativePreConstruct из Class Defaults (AuthorName/ProjectInfo).
	UTextBlock* Author = MakeText(Tree, TEXT("Txt_Author"), FText::GetEmpty(), 22, Palette.PrimaryText);
	Content->AddChildToVerticalBox(Author);
	if (UVerticalBoxSlot* Slot = Cast<UVerticalBoxSlot>(Author->Slot))
	{
		Slot->SetPadding(FMargin(0.f, 6.f));
		Slot->SetHorizontalAlignment(HAlign_Center);
	}

	UTextBlock* Info = MakeText(Tree, TEXT("Txt_ProjectInfo"), FText::GetEmpty(), 18,
		Palette.MutedText, /*bAutoWrap=*/true);
	Content->AddChildToVerticalBox(Info);
	if (UVerticalBoxSlot* Slot = Cast<UVerticalBoxSlot>(Info->Slot))
	{
		Slot->SetPadding(FMargin(0.f, 6.f, 0.f, 10.f));
	}

	AddButtonRow(Tree, Content, {
		{ TEXT("Btn_Back"), NSLOCTEXT("XRU1.Menu", "Back", "Назад") }
	}, Palette);

	return FinalizeBlueprint(Blueprint);
#else
	return false;
#endif
}

bool UXRU1WidgetAuthoringLibrary::BuildPauseMenuLayout(const FString& AssetPath, bool bOverwriteExisting)
{
#if WITH_EDITOR
	UWidgetBlueprint* Blueprint = LoadTargetBlueprint(AssetPath, bOverwriteExisting);
	if (!Blueprint)
	{
		return false;
	}
	const FMenuPalette Palette = LoadPalette();
	UWidgetTree* Tree = Blueprint->WidgetTree;

	UVerticalBox* Content = MakeScreenScaffold(Blueprint,
		NSLOCTEXT("XRU1.Menu", "PauseTitle", "ПАУЗА"), Palette, 460.f);

	const TPair<FName, FText> Buttons[] = {
		{ TEXT("Btn_Resume"),       NSLOCTEXT("XRU1.Menu", "Resume", "Продолжить") },
		{ TEXT("Btn_Settings"),     NSLOCTEXT("XRU1.Menu", "Settings", "Настройки") },
		{ TEXT("Btn_ReturnToMenu"), NSLOCTEXT("XRU1.Menu", "ToMainMenu", "В главное меню") }
	};
	for (const TPair<FName, FText>& Desc : Buttons)
	{
		UButton* Button = MakeTextButton(Tree, Desc.Key, Desc.Value, Palette);
		Content->AddChildToVerticalBox(Button);
		if (UVerticalBoxSlot* Slot = Cast<UVerticalBoxSlot>(Button->Slot))
		{
			Slot->SetPadding(FMargin(0.f, 8.f));
			Slot->SetHorizontalAlignment(HAlign_Fill);
		}
	}

	return FinalizeBlueprint(Blueprint);
#else
	return false;
#endif
}

bool UXRU1WidgetAuthoringLibrary::BuildIntroPlayerLayout(const FString& AssetPath, bool bOverwriteExisting)
{
#if WITH_EDITOR
	UWidgetBlueprint* Blueprint = LoadTargetBlueprint(AssetPath, bOverwriteExisting);
	if (!Blueprint)
	{
		return false;
	}
	const FMenuPalette Palette = LoadPalette();
	UWidgetTree* Tree = Blueprint->WidgetTree;

	UOverlay* Root = Tree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("RootOverlay"));
	Tree->RootWidget = Root;

	// Чёрный фон; интро-арт/видео-материал назначается поверх из темы позже.
	UBorder* Backdrop = Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Backdrop"));
	Backdrop->SetBrushColor(FLinearColor::Black);
	Root->AddChildToOverlay(Backdrop);
	if (UOverlaySlot* Slot = Cast<UOverlaySlot>(Backdrop->Slot))
	{
		Slot->SetHorizontalAlignment(HAlign_Fill);
		Slot->SetVerticalAlignment(VAlign_Fill);
	}

	// Полотно интро (Image): IntroFallbackArt или материал с MediaTexture.
	UImage* IntroImage = Tree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("Img_Intro"));
	Root->AddChildToOverlay(IntroImage);
	if (UOverlaySlot* Slot = Cast<UOverlaySlot>(IntroImage->Slot))
	{
		Slot->SetHorizontalAlignment(HAlign_Fill);
		Slot->SetVerticalAlignment(VAlign_Fill);
	}

	// Полноэкранная прозрачная кнопка пропуска: клик в любом месте.
	UButton* Skip = Tree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("Btn_Skip"));
	Skip->SetBackgroundColor(FLinearColor(0.f, 0.f, 0.f, 0.f));
	Root->AddChildToOverlay(Skip);
	if (UOverlaySlot* Slot = Cast<UOverlaySlot>(Skip->Slot))
	{
		Slot->SetHorizontalAlignment(HAlign_Fill);
		Slot->SetVerticalAlignment(VAlign_Fill);
	}

	UTextBlock* Hint = MakeText(Tree, TEXT("Txt_SkipHint"),
		NSLOCTEXT("XRU1.Menu", "SkipHint", "Пропустить — клик"), 16, Palette.MutedText);
	Hint->SetVisibility(ESlateVisibility::HitTestInvisible);
	Root->AddChildToOverlay(Hint);
	if (UOverlaySlot* Slot = Cast<UOverlaySlot>(Hint->Slot))
	{
		Slot->SetHorizontalAlignment(HAlign_Center);
		Slot->SetVerticalAlignment(VAlign_Bottom);
		Slot->SetPadding(FMargin(0.f, 0.f, 0.f, 36.f));
	}

	return FinalizeBlueprint(Blueprint);
#else
	return false;
#endif
}

bool UXRU1WidgetAuthoringLibrary::BuildMissionResultLayout(const FString& AssetPath, bool bOverwriteExisting)
{
#if WITH_EDITOR
	UWidgetBlueprint* Blueprint = LoadTargetBlueprint(AssetPath, bOverwriteExisting);
	if (!Blueprint)
	{
		return false;
	}
	const FMenuPalette Palette = LoadPalette();
	UWidgetTree* Tree = Blueprint->WidgetTree;

	// Заголовок пустой: тексты/арт целиком заполняет UpdateResultVisuals().
	UVerticalBox* Content = MakeScreenScaffold(Blueprint, FText::GetEmpty(), Palette, 780.f);

	USizeBox* ArtFrame = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("ArtFrame"));
	ArtFrame->SetWidthOverride(640.f);
	ArtFrame->SetHeightOverride(320.f);
	Content->AddChildToVerticalBox(ArtFrame);
	if (UVerticalBoxSlot* Slot = Cast<UVerticalBoxSlot>(ArtFrame->Slot))
	{
		Slot->SetHorizontalAlignment(HAlign_Center);
		Slot->SetPadding(FMargin(0.f, 0.f, 0.f, 14.f));
	}
	UImage* Art = Tree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("Img_ResultArt"));
	ArtFrame->AddChild(Art);

	UTextBlock* Title = MakeText(Tree, TEXT("Txt_ResultTitle"), FText::GetEmpty(), 40, Palette.PrimaryText);
	Content->AddChildToVerticalBox(Title);
	if (UVerticalBoxSlot* Slot = Cast<UVerticalBoxSlot>(Title->Slot))
	{
		Slot->SetHorizontalAlignment(HAlign_Center);
		Slot->SetPadding(FMargin(0.f, 4.f));
	}

	UTextBlock* Subtitle = MakeText(Tree, TEXT("Txt_ResultSubtitle"), FText::GetEmpty(), 18,
		Palette.MutedText, /*bAutoWrap=*/true);
	Content->AddChildToVerticalBox(Subtitle);
	if (UVerticalBoxSlot* Slot = Cast<UVerticalBoxSlot>(Subtitle->Slot))
	{
		Slot->SetHorizontalAlignment(HAlign_Center);
		Slot->SetPadding(FMargin(0.f, 4.f));
	}

	AddButtonRow(Tree, Content, {
		{ TEXT("Btn_Retry"),  NSLOCTEXT("XRU1.Menu", "Retry", "Повторить") },
		{ TEXT("Btn_ToHub"),  NSLOCTEXT("XRU1.Menu", "ToHub", "На базу") },
		{ TEXT("Btn_ToMenu"), NSLOCTEXT("XRU1.Menu", "ToMenu", "В меню") }
	}, Palette);

	return FinalizeBlueprint(Blueprint);
#else
	return false;
#endif
}

bool UXRU1WidgetAuthoringLibrary::BuildHubHUDLayout(const FString& AssetPath, bool bOverwriteExisting)
{
#if WITH_EDITOR
	UWidgetBlueprint* Blueprint = LoadTargetBlueprint(AssetPath, bOverwriteExisting);
	if (!Blueprint)
	{
		return false;
	}
	const FMenuPalette Palette = LoadPalette();
	UWidgetTree* Tree = Blueprint->WidgetTree;

	// HUD, а не экран: затемнения нет, карта под ним должна оставаться видимой.
	UOverlay* Root = Tree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("RootOverlay"));
	Tree->RootWidget = Root;

	// --- Карточка выбранной точки (слева внизу) ------------------------------
	USizeBox* CardFrame = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("CardFrame"));
	CardFrame->SetWidthOverride(520.f);
	Root->AddChildToOverlay(CardFrame);
	if (UOverlaySlot* Slot = Cast<UOverlaySlot>(CardFrame->Slot))
	{
		Slot->SetHorizontalAlignment(HAlign_Left);
		Slot->SetVerticalAlignment(VAlign_Bottom);
		Slot->SetPadding(FMargin(48.f, 0.f, 0.f, 48.f));
	}

	UBorder* Card = Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("CardPanel"));
	Card->SetBrushColor(Palette.PanelBackground);
	CardFrame->AddChild(Card);

	UVerticalBox* CardBox = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("CardBox"));
	Card->AddChild(CardBox);
	if (UBorderSlot* Slot = Cast<UBorderSlot>(CardBox->Slot))
	{
		Slot->SetPadding(FMargin(26.f, 22.f));
	}

	UTextBlock* Title = MakeText(Tree, TEXT("Txt_POITitle"), FText::GetEmpty(), 26, Palette.PrimaryText);
	CardBox->AddChildToVerticalBox(Title);

	UTextBlock* Description = MakeText(Tree, TEXT("Txt_POIDescription"), FText::GetEmpty(), 16,
		Palette.MutedText, /*bAutoWrap=*/true);
	CardBox->AddChildToVerticalBox(Description);
	if (UVerticalBoxSlot* Slot = Cast<UVerticalBoxSlot>(Description->Slot))
	{
		Slot->SetPadding(FMargin(0.f, 10.f, 0.f, 0.f));
	}

	UTextBlock* Status = MakeText(Tree, TEXT("Txt_Status"), FText::GetEmpty(), 15, Palette.Warning);
	CardBox->AddChildToVerticalBox(Status);
	if (UVerticalBoxSlot* Slot = Cast<UVerticalBoxSlot>(Status->Slot))
	{
		Slot->SetPadding(FMargin(0.f, 10.f, 0.f, 0.f));
	}

	UButton* Start = MakeTextButton(Tree, TEXT("Btn_Start"),
		NSLOCTEXT("XRU1.Hub", "StartOperation", "Начать операцию"), Palette);
	CardBox->AddChildToVerticalBox(Start);
	if (UVerticalBoxSlot* Slot = Cast<UVerticalBoxSlot>(Start->Slot))
	{
		Slot->SetPadding(FMargin(0.f, 18.f, 0.f, 0.f));
		Slot->SetHorizontalAlignment(HAlign_Fill);
	}

	// --- Служебные кнопки (справа вверху) ------------------------------------
	UHorizontalBox* TopRight = Tree->ConstructWidget<UHorizontalBox>(
		UHorizontalBox::StaticClass(), TEXT("TopRightRow"));
	Root->AddChildToOverlay(TopRight);
	if (UOverlaySlot* Slot = Cast<UOverlaySlot>(TopRight->Slot))
	{
		Slot->SetHorizontalAlignment(HAlign_Right);
		Slot->SetVerticalAlignment(VAlign_Top);
		Slot->SetPadding(FMargin(0.f, 36.f, 48.f, 0.f));
	}

	const TPair<FName, FText> TopButtons[] = {
		{ TEXT("Btn_Settings"), NSLOCTEXT("XRU1.Menu", "Settings", "Настройки") },
		{ TEXT("Btn_ToMenu"),   NSLOCTEXT("XRU1.Menu", "ToMenu", "В меню") }
	};
	for (const TPair<FName, FText>& Desc : TopButtons)
	{
		UButton* Button = MakeTextButton(Tree, Desc.Key, Desc.Value, Palette);
		TopRight->AddChildToHorizontalBox(Button);
		if (UHorizontalBoxSlot* Slot = Cast<UHorizontalBoxSlot>(Button->Slot))
		{
			Slot->SetPadding(FMargin(8.f, 0.f, 0.f, 0.f));
		}
	}

	return FinalizeBlueprint(Blueprint);
#else
	return false;
#endif
}

bool UXRU1WidgetAuthoringLibrary::BuildPOIPopupLayout(const FString& AssetPath, bool bOverwriteExisting)
{
#if WITH_EDITOR
	UWidgetBlueprint* Blueprint = LoadTargetBlueprint(AssetPath, bOverwriteExisting);
	if (!Blueprint)
	{
		return false;
	}
	const FMenuPalette Palette = LoadPalette();
	UWidgetTree* Tree = Blueprint->WidgetTree;

	// World-space попап рисуется DrawAtDesiredSize — корень ограничивает ширину.
	// Ширины 380 не хватало: длинные строки («Недоступно: сначала пройдите …»)
	// вылезали за подложку, потому что текст ограничен не шириной SizeBox,
	// а собственным WrapTextAt.
	const float PopupWidth = 460.f;
	const float PopupTextWrap = PopupWidth - 44.f; // минус внутренние отступы Border

	USizeBox* Root = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("RootFrame"));
	Root->SetWidthOverride(PopupWidth);
	Tree->RootWidget = Root;

	UBorder* Panel = Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("ContentPanel"));
	Panel->SetBrushColor(Palette.PanelBackground);
	Root->AddChild(Panel);

	UVerticalBox* Content = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("ContentBox"));
	Panel->AddChild(Content);
	if (UBorderSlot* Slot = Cast<UBorderSlot>(Content->Slot))
	{
		Slot->SetPadding(FMargin(18.f, 14.f));
	}

	UTextBlock* Title = MakeText(Tree, TEXT("Txt_Title"), FText::GetEmpty(), 20,
		Palette.PrimaryText, /*bAutoWrap=*/true);
	Title->SetWrapTextAt(PopupTextWrap);
	Content->AddChildToVerticalBox(Title);

	UTextBlock* Description = MakeText(Tree, TEXT("Txt_Description"), FText::GetEmpty(), 15,
		Palette.MutedText, /*bAutoWrap=*/true);
	Description->SetWrapTextAt(PopupTextWrap);
	Content->AddChildToVerticalBox(Description);
	if (UVerticalBoxSlot* Slot = Cast<UVerticalBoxSlot>(Description->Slot))
	{
		Slot->SetPadding(FMargin(0.f, 6.f, 0.f, 0.f));
	}

	// Текст приходит из миссии и бывает длинным — перенос обязателен.
	UTextBlock* Locked = MakeText(Tree, TEXT("Txt_Locked"), FText::GetEmpty(), 15,
		Palette.Warning, /*bAutoWrap=*/true);
	Locked->SetWrapTextAt(PopupTextWrap);
	Content->AddChildToVerticalBox(Locked);
	if (UVerticalBoxSlot* Slot = Cast<UVerticalBoxSlot>(Locked->Slot))
	{
		Slot->SetPadding(FMargin(0.f, 8.f, 0.f, 0.f));
	}

	return FinalizeBlueprint(Blueprint);
#else
	return false;
#endif
}
