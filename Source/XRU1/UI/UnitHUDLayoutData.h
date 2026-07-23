#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "UnitHUDLayoutData.generated.h"

class UUnitAttributeWidget;
class UUnitStatusIconWidget;

/**
 * Один слот в раскладке HUD'а юнита. Описывает, какой именно виджет атрибута
 * рисуется в HUD-контейнере и где (V/H индексы). Порядок: сверху вниз по
 * VerticalIndex, в пределах строки — слева направо по HorizontalIndex.
 *
 * Уникальность пары (VerticalIndex, HorizontalIndex) — на ответственности
 * программиста/дизайнера. Дубли логируются как ошибка валидации DataAsset'а
 * (см. UUnitHUDLayoutData::IsDataValid), но рантайм сам собой не падает.
 */
USTRUCT(BlueprintType)
struct FUnitHUDWidgetSlot
{
    GENERATED_BODY()

    /** Класс CommonUI-виджета атрибута (например, BP_HealthBarWidget). */
    UPROPERTY(EditAnywhere, Category = "Slot")
    TSubclassOf<UUnitAttributeWidget> WidgetClass;

    /** Желаемый размер слота в логических пикселях widget'а. */
    UPROPERTY(EditAnywhere, Category = "Slot")
    FVector2D Size = FVector2D(120.f, 16.f);

    /** Базовый цвет (например, красный для Health, зелёный для MoveSpeed). */
    UPROPERTY(EditAnywhere, Category = "Slot")
    FLinearColor Color = FLinearColor::White;

    /** Собственный внешний отступ слота внутри строки HUD. */
    UPROPERTY(EditAnywhere, Category = "Slot|Layout")
    FMargin Padding = FMargin(0.f);

    /** Индекс строки (0 — самая верхняя). */
    UPROPERTY(EditAnywhere, Category = "Slot|Layout", meta = (ClampMin = "0"))
    int32 VerticalIndex = 0;

    /** Индекс колонки внутри строки (0 — самая левая). */
    UPROPERTY(EditAnywhere, Category = "Slot|Layout", meta = (ClampMin = "0"))
    int32 HorizontalIndex = 0;
};

/**
 * DataAsset раскладки HUD'а над головой юнита. Назначается дизайнером
 * в BP-наследнике ATDCombatant (или индивидуально на инстансе).
 * Контейнер UUnitHUDWidget читает массив WidgetSlots, сортирует по
 * (V, H) и строит UVerticalBox из UHorizontalBox'ов.
 */
UCLASS(BlueprintType)
class XRU1_API UUnitHUDLayoutData : public UDataAsset
{
    GENERATED_BODY()

public:
    UUnitHUDLayoutData();

    /** Слоты HUD'а. Программист отвечает за уникальность пар (V, H). */
    UPROPERTY(EditDefaultsOnly, Category = "Layout")
    TArray<FUnitHUDWidgetSlot> WidgetSlots;

    /**
     * Автоматически добавить badge тактического статуса к последней строке.
     * Нужен, чтобы существующие DA_UnitHUD_Squad/Enemy получили статус без
     * хрупкого ручного редактирования массива после каждого добавления класса.
     */
    UPROPERTY(EditDefaultsOnly, Category = "Layout|Tactical Status")
    bool bShowTacticalStatusIcon = true;

    /** Класс badge. По умолчанию нативный UUnitStatusIconWidget. */
    UPROPERTY(EditDefaultsOnly, Category = "Layout|Tactical Status",
        meta = (EditCondition = "bShowTacticalStatusIcon"))
    TSubclassOf<UUnitStatusIconWidget> TacticalStatusWidgetClass;

    /**
     * Строка/колонка status badge. -1 означает: последняя существующая строка
     * и колонка сразу справа от всех её существующих слотов.
     */
    UPROPERTY(EditDefaultsOnly, Category = "Layout|Tactical Status",
        meta = (EditCondition = "bShowTacticalStatusIcon", ClampMin = "-1"))
    int32 TacticalStatusVerticalIndex = -1;

    UPROPERTY(EditDefaultsOnly, Category = "Layout|Tactical Status",
        meta = (EditCondition = "bShowTacticalStatusIcon", ClampMin = "-1"))
    int32 TacticalStatusHorizontalIndex = -1;

    /** Размер безопасного слота; сам glyph берёт размер из UITheme. */
    UPROPERTY(EditDefaultsOnly, Category = "Layout|Tactical Status",
        meta = (EditCondition = "bShowTacticalStatusIcon", ClampMin = "1"))
    FVector2D TacticalStatusSlotSize = FVector2D(28.f, 28.f);

    UPROPERTY(EditDefaultsOnly, Category = "Layout|Tactical Status",
        meta = (EditCondition = "bShowTacticalStatusIcon"))
    FMargin TacticalStatusSlotPadding = FMargin(2.f, 0.f, 0.f, 0.f);

    UPROPERTY(EditDefaultsOnly, Category = "Layout|Tactical Status",
        meta = (EditCondition = "bShowTacticalStatusIcon"))
    FLinearColor TacticalStatusColor = FLinearColor::White;

    /** Физический размер «холста» 3D-виджета над головой (для UWidgetComponent::DrawSize). */
    UPROPERTY(EditDefaultsOnly, Category = "Layout|Component", meta = (ClampMin = "16"))
    FVector2D DrawSize = FVector2D(200.f, 80.f);

    /** Смещение HUD-компонента относительно root'а персонажа (по умолчанию — над головой). */
    UPROPERTY(EditDefaultsOnly, Category = "Layout|Component")
    FVector RelativeLocation = FVector(0.f, 0.f, 110.f);

#if WITH_EDITOR
    virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif
};
