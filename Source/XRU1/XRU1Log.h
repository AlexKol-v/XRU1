#pragma once

#include "CoreMinimal.h"
#include "Logging/LogMacros.h"

/**
 * Категории логов проекта.
 *
 * До этого весь тактический код писал в `LogTemp`. На финальном тесте это
 * означает, что решение AI, событие квеста, старт сценария и ошибка звука лежат
 * вперемешку с логами движка и не фильтруются ничем. Отдельная категория на
 * подсистему даёт `Log LogXRU1AI Verbose` в консоли и фильтр в Output Log —
 * это и есть разница между «поищу в логе» и «вижу только то, что отлаживаю».
 *
 * Уровень по умолчанию `Log`: шумную детализацию каждая подсистема включает
 * своим cvar (см. `TacticsDebug.h`), а не глобальной пересборкой.
 */

/** Бой: выстрелы, урон, укрытия, LOS, конец боя. */
XRU1_API DECLARE_LOG_CATEGORY_EXTERN(LogXRU1Combat, Log, All);

/** Решения и исполнение тактического AI. */
XRU1_API DECLARE_LOG_CATEGORY_EXTERN(LogXRU1AI, Log, All);

/** Ходы, фазы, состав сторон. */
XRU1_API DECLARE_LOG_CATEGORY_EXTERN(LogXRU1Turns, Log, All);

/** Сценарии общей карты: streaming, старт/финализация, retry. */
XRU1_API DECLARE_LOG_CATEGORY_EXTERN(LogXRU1Scenario, Log, All);

/** Квест-события, objective, Action Gate обучения. */
XRU1_API DECLARE_LOG_CATEGORY_EXTERN(LogXRU1Quest, Log, All);

/** Звук: применение громкостей, незаполненные реплики. */
XRU1_API DECLARE_LOG_CATEGORY_EXTERN(LogXRU1Audio, Log, All);

/** HUD, меню, экраны. */
XRU1_API DECLARE_LOG_CATEGORY_EXTERN(LogXRU1UI, Log, All);

/**
 * Камера и режиссура кадра: фокус, следование, кадр выстрела, возвраты.
 * Отдельная категория, потому что «камера улетела» — типовой класс багов,
 * и без журнала владельца взгляда его причину не восстановить: уводить камеру
 * имеют право десяток мест (выбор бойца, смена фазы, кадр выстрела, ручной
 * ввод), а виден только итоговый рывок.
 */
XRU1_API DECLARE_LOG_CATEGORY_EXTERN(LogXRU1Camera, Log, All);
