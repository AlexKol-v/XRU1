#pragma once

#include "CoreMinimal.h"
#include "Layout/Margin.h"
#include "SubtitleTypes.generated.h"

/**
 * Канал субтитра.
 *
 * Сейчас в проекте живёт только речь. `Caption` (подписи к неречевым звукам —
 * «выстрел справа») намеренно объявлен, но не обслуживается: его наличие в
 * модели данных стоит ноль, а появление второго канала потом не потребует
 * трогать ни источники, ни подсистему — только добавить вторую ячейку показа.
 * Номенклатура повторяет движковый `ESubtitleType` (плагин
 * SubtitlesAndClosedCaptions), чтобы переход на него был механическим.
 */
UENUM(BlueprintType)
enum class EXRU1SubtitleChannel : uint8
{
	/** Речь: реплики «Купола», бойцов, брифинга, ролика. */
	Dialogue UMETA(DisplayName = "Речь"),
	/** Зарезервировано: подписи к неречевым звукам. Не обслуживается. */
	Caption  UMETA(DisplayName = "Подписи к звукам (резерв)")
};

/** Ступень размера текста субтитра (выбор игрока). */
UENUM(BlueprintType)
enum class EXRU1SubtitleTextSize : uint8
{
	Normal     UMETA(DisplayName = "Обычный"),
	Large      UMETA(DisplayName = "Крупный"),
	ExtraLarge UMETA(DisplayName = "Очень крупный")
};

/** Плотность подложки под субтитром (выбор игрока). */
UENUM(BlueprintType)
enum class EXRU1SubtitleBackdrop : uint8
{
	None   UMETA(DisplayName = "Без подложки"),
	Soft   UMETA(DisplayName = "Полупрозрачная"),
	Solid  UMETA(DisplayName = "Плотная")
};

/**
 * Одна строка субтитра — единица данных всего слоя.
 *
 * Говорящий хранится ОТДЕЛЬНО от текста: имя показывается по настройке игрока,
 * переводится независимо и не мешает переводчику. Вклеивать «Купол: …» в текст
 * нельзя — это ровно та ошибка, из-за которой имя невозможно отключить.
 *
 * Времени в строке нет намеренно: жизнью строки управляет тот, кто её показал
 * (см. `UXRU1SubtitleSubsystem` — три режима показа). Строка не знает ни про
 * виджеты, ни про стиль, ни про язык.
 */
USTRUCT(BlueprintType)
struct XRU1_API FXRU1SubtitleLine
{
	GENERATED_BODY()

	/** Кто говорит. Пусто — реплика без атрибуции (диктор, ролик). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Субтитры")
	FText Speaker;

	/** Текст реплики. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Субтитры", meta = (MultiLine = "true"))
	FText Text;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Субтитры")
	EXRU1SubtitleChannel Channel = EXRU1SubtitleChannel::Dialogue;

	/**
	 * Реплику можно пропустить вводом. Подсказку рисует слой, а не автор текста:
	 * в переводимую строку речи UI-аффорданс попадать не должен.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Субтитры")
	bool bSkippable = false;

	/** Кто выдал строку (BeatId, имя ассета, «Intro») — только для логов. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Субтитры")
	FName SourceId;

	bool IsEmpty() const { return Text.IsEmpty(); }
};

/**
 * Дескриптор показанной строки.
 *
 * Нужен ровно для одного: снять строку имеет право только тот, кто её показал.
 * Строка на экране одна и новая вытесняет предыдущую, поэтому запоздалый
 * `HideLine` прошлого владельца обязан быть проигнорирован — иначе конец
 * прошлой реплики гасит уже начавшуюся следующую.
 */
USTRUCT(BlueprintType)
struct XRU1_API FXRU1SubtitleHandle
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Субтитры")
	int32 Id = 0;

	bool IsValid() const { return Id != 0; }
	bool operator==(const FXRU1SubtitleHandle& Other) const { return Id == Other.Id; }
	bool operator!=(const FXRU1SubtitleHandle& Other) const { return Id != Other.Id; }
};

/**
 * Пользовательские настройки субтитров. Хранятся в `UTacticsUserSettings`
 * (файл `GameUserSettings.ini`) — там же, где звук, изображение и камера, и
 * там же, где движок держит выбранный язык (`[Internationalization]`).
 *
 * Цвета в настройки не вынесены: они принадлежат теме UI (`DA_TacticalHUDStyle`).
 */
USTRUCT(BlueprintType)
struct XRU1_API FTacticsSubtitleSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Субтитры")
	bool bEnabled = true;

	/** Показывать имя говорящего перед репликой. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Субтитры")
	bool bShowSpeakerNames = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Субтитры")
	EXRU1SubtitleTextSize TextSize = EXRU1SubtitleTextSize::Normal;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Субтитры")
	EXRU1SubtitleBackdrop Backdrop = EXRU1SubtitleBackdrop::Soft;
};

/**
 * Готовый к отрисовке стиль: тема UI + настройки игрока + выбранный якорь
 * позиции, сведённые в одну структуру.
 *
 * Существует затем, чтобы РИСУЮЩИЙ виджет был глупым. Пока он ничего не решает
 * сам, его можно заменить (Slate → WBP) не трогая ни подсистему, ни источники.
 */
USTRUCT(BlueprintType)
struct XRU1_API FXRU1SubtitleStyle
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Субтитры") int32 LineFontSize = 22;
	UPROPERTY(BlueprintReadOnly, Category = "Субтитры") int32 SpeakerFontSize = 15;
	UPROPERTY(BlueprintReadOnly, Category = "Субтитры") int32 HintFontSize = 12;

	UPROPERTY(BlueprintReadOnly, Category = "Субтитры") FLinearColor LineColor = FLinearColor(0.96f, 0.97f, 1.f, 1.f);
	UPROPERTY(BlueprintReadOnly, Category = "Субтитры") FLinearColor SpeakerColor = FLinearColor(0.55f, 0.82f, 0.95f, 1.f);
	UPROPERTY(BlueprintReadOnly, Category = "Субтитры") FLinearColor HintColor = FLinearColor(0.62f, 0.68f, 0.72f, 1.f);

	/** Цвет подложки уже с учётом выбранной плотности. */
	UPROPERTY(BlueprintReadOnly, Category = "Субтитры") FLinearColor BackdropColor = FLinearColor(0.f, 0.f, 0.f, 0.6f);

	UPROPERTY(BlueprintReadOnly, Category = "Субтитры") FMargin BackdropPadding = FMargin(22.f, 12.f);

	/** Ширина переноса строки, px. */
	UPROPERTY(BlueprintReadOnly, Category = "Субтитры") float WrapWidth = 900.f;

	/** Показывать имя говорящего (настройка игрока, уже применённая). */
	UPROPERTY(BlueprintReadOnly, Category = "Субтитры") bool bShowSpeaker = true;

	/** Отступ от нижнего края экрана, px (уже выбранный пресет). */
	UPROPERTY(BlueprintReadOnly, Category = "Субтитры") float BottomOffset = 90.f;

	/** Рисовать подсказку пропуска у строк с `bSkippable`. */
	UPROPERTY(BlueprintReadOnly, Category = "Субтитры") bool bShowSkipHint = false;
};
