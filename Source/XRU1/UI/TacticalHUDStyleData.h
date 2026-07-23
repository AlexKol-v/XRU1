#pragma once

#include "CoreMinimal.h"
#include "CommonButtonBase.h"
#include "CommonTextBlock.h"
#include "CoverTypes.h"
#include "Engine/DataAsset.h"
#include "TacticalHUDStyleData.generated.h"

class AUnitBase;
class UMaterialInterface;
class UMediaSource;
class UTexture2D;

/** Экран, для которого запрашивается крупный арт из общей UI-темы. */
UENUM(BlueprintType)
enum class EXRU1UIScreenArt : uint8
{
	MainMenu,
	Difficulty,
	Settings,
	About,
	Pause,
	TutorialBriefing,
	MissionBriefing,
	VictoryResult,
	DefeatResult,
	DemoComplete,
	IntroFallback
};

/** Смысловая иконка состояния юнита. */
UENUM(BlueprintType)
enum class EXRU1UIStatusIcon : uint8
{
	None,
	Overwatch,
	HunkerDown,
	Taunt,
	Downed,
	Evacuated,
	Moving
};

/**
 * Размер и внутренние отступы самостоятельного блока интерфейса.
 * DesiredSize = (0, 0) означает, что размер определяет сам UMG-контейнер.
 * ItemSpacing применяет WBP к слотам Horizontal/VerticalBox: у панелей нет
 * единого свойства spacing.
 */
USTRUCT(BlueprintType)
struct XRU1_API FXRU1UIBlockLayout
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Разметка", meta = (ClampMin = "0"))
	FVector2D DesiredSize = FVector2D::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Разметка")
	FMargin Padding = FMargin(0.f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Разметка")
	FMargin ItemSpacing = FMargin(0.f);
};

/** Цвета фона и содержимого интерактивной кнопки во всех состояниях. */
USTRUCT(BlueprintType)
struct XRU1_API FXRU1UIButtonPalette
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Фон")
	FLinearColor NormalBackground = FLinearColor(0.025f, 0.055f, 0.075f, 0.96f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Фон")
	FLinearColor HoveredBackground = FLinearColor(0.025f, 0.20f, 0.24f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Фон")
	FLinearColor PressedBackground = FLinearColor(0.015f, 0.32f, 0.34f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Фон")
	FLinearColor DisabledBackground = FLinearColor(0.025f, 0.035f, 0.04f, 0.72f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Иконка и текст")
	FLinearColor NormalForeground = FLinearColor(0.72f, 0.94f, 1.f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Иконка и текст")
	FLinearColor HoveredForeground = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Иконка и текст")
	FLinearColor PressedForeground = FLinearColor(0.42f, 1.f, 0.94f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Иконка и текст")
	FLinearColor DisabledForeground = FLinearColor(0.24f, 0.31f, 0.33f, 0.72f);
};

/**
 * Крупное изображение экрана. Soft-reference не заставляет боевой HUD держать
 * в памяти все фоны 1920x1080; WBP грузит только свой арт при активации.
 */
USTRUCT(BlueprintType)
struct XRU1_API FXRU1UIScreenArtwork
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Арт")
	TSoftObjectPtr<UTexture2D> Texture;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Арт")
	FLinearColor Tint = FLinearColor::White;

	/** (0, 0) = Fill/размер контейнера; ненулевой размер подаётся в SizeBox. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Арт", meta = (ClampMin = "0"))
	FVector2D DesiredSize = FVector2D::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Арт")
	FMargin Padding = FMargin(0.f);
};

/**
 * Единая UI-тема проекта: боевой HUD, портреты/статусы, меню, брифинги,
 * результаты и интро. Ассет назначается один раз в UTacticsGameInstance;
 * локальный Style в WBP_TacticalHUD остаётся fallback и источником превью.
 * Маленькие HUD-текстуры — hard references, крупный арт/видео — soft references.
 * Незаданная текстура не перетирает brush, который оставлен в Designer.
 */
UCLASS(BlueprintType)
class XRU1_API UTacticalHUDStyleData : public UDataAsset
{
	GENERATED_BODY()

public:
	// --- Кнопки действий (ActionsPanel + EndTurn) ----------------------------

	/** Размер иконки внутри кнопок панели действий. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "05. Разметка|Кнопки действий", meta = (ClampMin = "1"))
	FVector2D ActionIconSize = FVector2D(48.f, 48.f);

	/**
	 * Приравнять Pressed-отступ кнопки к Normal: у стандартного стиля UButton
	 * контент при нажатии сдвигается (кнопка «прыгает») — это убирает сдвиг.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "05. Разметка|Кнопки действий")
	bool bUnifyPressedPadding = true;

	/**
	 * Единый отступ (Normal/Pressed) кнопок ActionsPanel — кнопки создавались
	 * в Designer в разные заходы и получили разный NormalPadding, из-за чего
	 * итоговый размер кнопки (иконка + отступ) визуально «прыгал» между
	 * кнопками несмотря на одинаковый ActionIconSize. Принудительно
	 * выставляется в ApplyStyle() на все 6 кнопок ActionsPanel.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "05. Разметка|Кнопки действий")
	FMargin ActionButtonPadding = FMargin(10.f);

	/** Используется только при выключенном bUnifyPressedPadding. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "05. Разметка|Кнопки действий")
	FMargin ActionButtonPressedPadding = FMargin(10.f);

	// --- Текстуры иконок кнопок ----------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "01. Иконки|Действия")
	TObjectPtr<UTexture2D> AttackIcon;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "01. Иконки|Действия")
	TObjectPtr<UTexture2D> OverwatchIcon;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "01. Иконки|Действия")
	TObjectPtr<UTexture2D> HunkerIcon;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "01. Иконки|Действия")
	TObjectPtr<UTexture2D> AbilityIcon;

	/** Иконка кнопки F в режиме «Обезвредить» (и по умолчанию). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "01. Иконки|Действия")
	TObjectPtr<UTexture2D> InteractDefuseIcon;

	/** Иконка кнопки F в режиме «Эвакуация». */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "01. Иконки|Действия")
	TObjectPtr<UTexture2D> InteractEvacIcon;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "01. Иконки|Действия")
	TObjectPtr<UTexture2D> SkipIcon;

	// --- Кнопка «Завершить ход» ----------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "01. Иконки|Действия")
	TObjectPtr<UTexture2D> EndTurnIcon;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "05. Разметка|Завершить ход", meta = (ClampMin = "1"))
	FVector2D EndTurnIconSize = FVector2D(40.f, 40.f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "05. Разметка|Завершить ход")
	FMargin EndTurnButtonPadding = FMargin(14.f, 10.f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "05. Разметка|Завершить ход")
	FMargin EndTurnButtonPressedPadding = FMargin(14.f, 10.f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "05. Разметка|Завершить ход")
	bool bUnifyEndTurnPressedPadding = true;

	// --- Панель цели (щит укрытия, как в XCOM 2) ------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "02. Статусы|Укрытия")
	TObjectPtr<UTexture2D> HalfCoverIcon;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "02. Статусы|Укрытия")
	TObjectPtr<UTexture2D> FullCoverIcon;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "05. Разметка|Панель цели", meta = (ClampMin = "1"))
	FVector2D TargetCoverIconSize = FVector2D(32.f, 32.f);

	// --- Дополнительные иконки HUD ------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "01. Иконки|Действия")
	TObjectPtr<UTexture2D> MoveIcon;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "01. Иконки|Счётчики")
	TObjectPtr<UTexture2D> EnemyCountIcon;

	/**
	 * Опциональная белая/светло-серая alpha-mask подложки счётчика врагов.
	 * None использует однотонный brush самого Border; цвет всегда задаётся
	 * EnemyCounterBackgroundColor.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "01. Иконки|Счётчики")
	TObjectPtr<UTexture2D> EnemyCounterBackgroundTexture;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "02. Статусы|Юнит")
	TObjectPtr<UTexture2D> StatusOverwatchIcon;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "02. Статусы|Юнит")
	TObjectPtr<UTexture2D> StatusHunkerIcon;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "02. Статусы|Юнит")
	TObjectPtr<UTexture2D> StatusTauntIcon;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "02. Статусы|Юнит")
	TObjectPtr<UTexture2D> StatusDownedIcon;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "02. Статусы|Юнит")
	TObjectPtr<UTexture2D> StatusEvacuatedIcon;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "02. Статусы|Юнит")
	TObjectPtr<UTexture2D> StatusMovingIcon;

	/**
	 * Опциональная текстура подложки world-space status badge.
	 * None использует встроенную однотонную подложку, поэтому отдельный арт
	 * не обязателен. Для tint-схемы нужна белая/светло-серая PNG alpha-mask:
	 * её итоговый цвет задаёт UnitOverheadStatusBackgroundColor.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "02. Статусы|HUD над юнитом")
	TObjectPtr<UTexture2D> UnitOverheadStatusBackgroundTexture;

	/** Белая маска рамки; итоговый цвет задаёт SelectionColor. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "02. Статусы|Выбор")
	TObjectPtr<UTexture2D> SelectionFrameTexture;

	// --- Портреты, классы и их способности ----------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "03. Юниты|Портреты")
	TObjectPtr<UTexture2D> AssaultPortrait;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "03. Юниты|Портреты")
	TObjectPtr<UTexture2D> SniperPortrait;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "03. Юниты|Портреты")
	TObjectPtr<UTexture2D> MedicPortrait;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "03. Юниты|Портреты")
	TObjectPtr<UTexture2D> TankPortrait;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "03. Юниты|Портреты")
	TObjectPtr<UTexture2D> MarauderPortrait;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "03. Юниты|Иконки классов")
	TObjectPtr<UTexture2D> AssaultClassIcon;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "03. Юниты|Иконки классов")
	TObjectPtr<UTexture2D> SniperClassIcon;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "03. Юниты|Иконки классов")
	TObjectPtr<UTexture2D> MedicClassIcon;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "03. Юниты|Иконки классов")
	TObjectPtr<UTexture2D> TankClassIcon;

	/** Опционально: отдельная классовая иконка врага, если появится. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "03. Юниты|Иконки классов")
	TObjectPtr<UTexture2D> MarauderClassIcon;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "03. Юниты|Способности классов")
	TObjectPtr<UTexture2D> AssaultAbilityIcon;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "03. Юниты|Способности классов")
	TObjectPtr<UTexture2D> SniperAbilityIcon;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "03. Юниты|Способности классов")
	TObjectPtr<UTexture2D> MedicAbilityIcon;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "03. Юниты|Способности классов")
	TObjectPtr<UTexture2D> TankAbilityIcon;

	// --- Крупный арт экранов (soft references) ------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "04. Экраны|Меню")
	FXRU1UIScreenArtwork MainMenuArt;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "04. Экраны|Меню")
	FXRU1UIScreenArtwork DifficultyArt;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "04. Экраны|Меню")
	FXRU1UIScreenArtwork SettingsArt;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "04. Экраны|Меню")
	FXRU1UIScreenArtwork AboutArt;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "04. Экраны|Меню")
	FXRU1UIScreenArtwork PauseArt;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "04. Экраны|Брифинги")
	FXRU1UIScreenArtwork TutorialBriefingArt;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "04. Экраны|Брифинги")
	FXRU1UIScreenArtwork MissionBriefingArt;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "04. Экраны|Результаты")
	FXRU1UIScreenArtwork VictoryResultArt;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "04. Экраны|Результаты")
	FXRU1UIScreenArtwork DefeatResultArt;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "04. Экраны|Результаты")
	FXRU1UIScreenArtwork DemoCompleteArt;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "04. Экраны|Интро")
	FXRU1UIScreenArtwork IntroFallbackArt;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "04. Экраны|Интро")
	TSoftObjectPtr<UMediaSource> IntroMediaSource;

	/** Материал с MediaTexture для Image в WBP_IntroPlayer. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "04. Экраны|Интро")
	TSoftObjectPtr<UMaterialInterface> IntroVideoMaterial;

	// --- Размеры отдельных элементов ----------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "05. Разметка|Портрет", meta = (ClampMin = "1"))
	FVector2D PortraitImageSize = FVector2D(104.f, 104.f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "05. Разметка|Портрет", meta = (ClampMin = "1"))
	FVector2D PortraitClassIconSize = FVector2D(28.f, 28.f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "05. Разметка|Портрет", meta = (ClampMin = "1"))
	FVector2D PortraitStatusIconSize = FVector2D(24.f, 24.f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "05. Разметка|Портрет", meta = (ClampMin = "1"))
	FVector2D PortraitCoverIconSize = FVector2D(26.f, 26.f);

	/** Независимый отступ gameplay-status внутри HeaderRow карточки. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "05. Разметка|Портрет")
	FMargin PortraitStatusIconPadding = FMargin(4.f, 0.f, 0.f, 0.f);

	/** Независимый отступ cover badge внутри HeaderRow карточки. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "05. Разметка|Портрет")
	FMargin PortraitCoverIconPadding = FMargin(4.f, 0.f, 0.f, 0.f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "05. Разметка|Портрет")
	FMargin SelectionFramePadding = FMargin(2.f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "05. Разметка|Счётчик врагов", meta = (ClampMin = "1"))
	FVector2D EnemyCounterIconSize = FVector2D(32.f, 32.f);

	/** Внутренний отступ от края подложки до PNG-иконки и числа. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "05. Разметка|Счётчик врагов")
	FMargin EnemyCounterBackgroundPadding = FMargin(8.f, 4.f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "05. Разметка|HUD над юнитом", meta = (ClampMin = "1"))
	FVector2D UnitOverheadCoverIconSize = FVector2D(24.f, 24.f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "05. Разметка|HUD над юнитом", meta = (ClampMin = "1"))
	FVector2D UnitOverheadStatusIconSize = FVector2D(24.f, 24.f);

	/**
	 * Внутренний отступ между прозрачным PNG-glyph и подложкой.
	 * При стандартных IconSize 24×24 и Padding 2 итоговый badge занимает 28×28
	 * и точно помещается в TacticalStatusSlotSize по умолчанию.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "05. Разметка|HUD над юнитом")
	FMargin UnitOverheadStatusBackgroundPadding = FMargin(2.f);

	/**
	 * Тёмная нейтральная подложка обеспечивает стабильный контраст glyph
	 * с произвольным светлым/тёмным фоном 3D-уровня.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "06. Цвета|HUD над юнитом")
	FLinearColor UnitOverheadStatusBackgroundColor =
		FLinearColor(0.01f, 0.025f, 0.04f, 0.92f);

	/** Тёмная подложка сохраняет контраст прозрачной PNG-иконки счётчика. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "06. Цвета|Счётчик врагов")
	FLinearColor EnemyCounterBackgroundColor =
		FLinearColor(0.01f, 0.025f, 0.04f, 0.88f);

	// --- Независимые размеры/отступы блоков ---------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "05. Разметка|Блоки")
	FXRU1UIBlockLayout ActionsPanelLayout;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "05. Разметка|Блоки")
	FXRU1UIBlockLayout EndTurnLayout;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "05. Разметка|Блоки")
	FXRU1UIBlockLayout SquadPanelLayout;

	/** Для карточки оба компонента DesiredSize обязательны и должны быть > 0. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "05. Разметка|Блоки")
	FXRU1UIBlockLayout PortraitCardLayout;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "05. Разметка|Блоки")
	FXRU1UIBlockLayout TargetPanelLayout;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "05. Разметка|Блоки")
	FXRU1UIBlockLayout TurnPhaseLayout;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "05. Разметка|Блоки")
	FXRU1UIBlockLayout EnemyCounterLayout;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "05. Разметка|Блоки")
	FXRU1UIBlockLayout TargetingBannerLayout;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "05. Разметка|Блоки")
	FXRU1UIBlockLayout UnitOverheadLayout;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "05. Разметка|Экраны")
	FXRU1UIBlockLayout MainMenuContentLayout;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "05. Разметка|Экраны")
	FXRU1UIBlockLayout DifficultyContentLayout;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "05. Разметка|Экраны")
	FXRU1UIBlockLayout SettingsContentLayout;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "05. Разметка|Экраны")
	FXRU1UIBlockLayout AboutContentLayout;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "05. Разметка|Экраны")
	FXRU1UIBlockLayout PauseContentLayout;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "05. Разметка|Экраны")
	FXRU1UIBlockLayout BriefingContentLayout;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "05. Разметка|Экраны")
	FXRU1UIBlockLayout ResultContentLayout;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "05. Разметка|Экраны")
	FXRU1UIBlockLayout IntroOverlayLayout;

	// --- Палитра -------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "06. Палитра|Кнопки")
	FXRU1UIButtonPalette ActionButtonPalette;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "06. Палитра|Кнопки")
	FXRU1UIButtonPalette EndTurnButtonPalette;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "06. Палитра|Смысловые цвета")
	FLinearColor FriendlyColor = FLinearColor(0.02f, 0.8f, 0.9f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "06. Палитра|Смысловые цвета")
	FLinearColor EnemyColor = FLinearColor(0.85f, 0.035f, 0.025f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "06. Палитра|Смысловые цвета")
	FLinearColor WarningColor = FLinearColor(1.f, 0.72f, 0.08f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "06. Палитра|Смысловые цвета")
	FLinearColor SuccessColor = FLinearColor(0.08f, 0.72f, 0.36f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "06. Палитра|Смысловые цвета")
	FLinearColor SelectionColor = FLinearColor(0.12f, 0.95f, 1.f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "06. Палитра|Панели")
	FLinearColor PanelBackgroundColor = FLinearColor(0.015f, 0.025f, 0.035f, 0.88f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "06. Палитра|Панели")
	FLinearColor PrimaryTextColor = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "06. Палитра|Панели")
	FLinearColor MutedTextColor = FLinearColor(0.55f, 0.65f, 0.69f, 1.f);

	// --- CommonUI style assets для меню -------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "07. CommonUI|Кнопки")
	TSubclassOf<UCommonButtonStyle> PrimaryMenuButtonStyle;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "07. CommonUI|Кнопки")
	TSubclassOf<UCommonButtonStyle> SecondaryMenuButtonStyle;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "07. CommonUI|Кнопки")
	TSubclassOf<UCommonButtonStyle> DangerMenuButtonStyle;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "07. CommonUI|Текст")
	TSubclassOf<UCommonTextStyle> TitleTextStyle;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "07. CommonUI|Текст")
	TSubclassOf<UCommonTextStyle> BodyTextStyle;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "07. CommonUI|Текст")
	TSubclassOf<UCommonTextStyle> CaptionTextStyle;

	/** Портрет с учётом конкретного C++-класса юнита. */
	UFUNCTION(BlueprintPure, Category = "UI|Theme")
	UTexture2D* GetPortraitForUnit(const AUnitBase* Unit) const;

	/** Иконка класса с учётом конкретного C++-класса юнита. */
	UFUNCTION(BlueprintPure, Category = "UI|Theme")
	UTexture2D* GetClassIconForUnit(const AUnitBase* Unit) const;

	/** Иконка классовой способности; незаполненное поле использует AbilityIcon. */
	UFUNCTION(BlueprintPure, Category = "UI|Theme")
	UTexture2D* GetAbilityIconForUnit(const AUnitBase* Unit) const;

	/** Half/Full cover → текстура щита; None → nullptr. */
	UFUNCTION(BlueprintPure, Category = "UI|Theme")
	UTexture2D* GetCoverIcon(ECoverType Cover) const;

	/**
	 * Текущее локальное укрытие юнита (BestCoverAround), одинаковое для
	 * карточки и overhead HUD. Это не направленное укрытие цели против стрелка.
	 */
	UFUNCTION(BlueprintPure, Category = "UI|Theme")
	UTexture2D* GetCoverIconForUnit(const AUnitBase* Unit) const;

	/** Единый маппинг смыслового статуса на текстуру. */
	UFUNCTION(BlueprintPure, Category = "UI|Theme")
	UTexture2D* GetStatusIcon(EXRU1UIStatusIcon Status) const;

	/**
	 * Главный визуальный статус юнита. Приоритет един для всех WBP:
	 * Downed → Evacuated → Taunt → HunkerDown → Overwatch → Moving.
	 */
	UFUNCTION(BlueprintPure, Category = "UI|Theme")
	EXRU1UIStatusIcon GetStatusForUnit(const AUnitBase* Unit) const;

	/** Готовая статусная текстура юнита; nullptr означает, что Image надо скрыть. */
	UFUNCTION(BlueprintPure, Category = "UI|Theme")
	UTexture2D* GetStatusIconForUnit(const AUnitBase* Unit) const;

	/** Параметры крупного арта выбранного экрана (soft reference + tint/layout). */
	UFUNCTION(BlueprintPure, Category = "UI|Theme")
	FXRU1UIScreenArtwork GetScreenArtwork(EXRU1UIScreenArt Screen) const;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif
};
