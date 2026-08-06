#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "XRU1LocalizationAuthoringLibrary.generated.h"

/**
 * Editor-библиотека: делает тексты ассетов ЛОКАЛИЗУЕМЫМИ.
 *
 * ── Зачем ─────────────────────────────────────────────────────────────────────
 * `FText` бывает двух видов. Введённый в редакторе получает namespace + key и
 * попадает в манифест при Gather. Полученный из голой строки —
 * culture-invariant: у него нет ключа, переводить его НЕЧЕМ, и Gather его не
 * видит вовсе (не «пропускает» — для системы локализации такого текста просто
 * не существует).
 *
 * Ровно это и случилось с XRU1. Дерево обучения, реплики боя и витрины миссий
 * собирались скриптом через T3D-импорт (`XRU1StateTreeAuthoringLibrary`,
 * `XRU1WidgetAuthoringLibrary`, Python), а в T3D обычная строка в поле FText
 * парсится как invariant — локализуемым её делает только форма
 * `NSLOCTEXT("ns","key","source")`. Поэтому после первого Gather манифест
 * содержал 161 запись при том, что одних только шагов обучения 26: весь
 * авторский текст сценариев был для локализации невидим.
 *
 * ── Что делает ────────────────────────────────────────────────────────────────
 * Обходит ВСЕ объекты пакета ассета и все их `FText`-свойства рекурсивно
 * (структуры, массивы, карты, множества, вложенные UObject — так достаётся
 * instance data задач внутри `UStateTreeState`), и каждому тексту без ключа
 * переприсваивает ключ через `FText::ChangeKey`. Ключ детерминированный — путь
 * свойства, — поэтому повторный прогон даёт ТЕ ЖЕ ключи и уже сделанные
 * переводы не теряются.
 *
 * ── Почему утилита, а не «перевбить руками» ───────────────────────────────────
 * Ручной ввод чинит текущий снимок, но не причину: следующая скриптовая заливка
 * снова принесёт invariant-тексты, причём молча. Утилита превращает это в
 * проверяемый шаг: `AuditAssetTexts` показывает дыры, `MakeAssetTextsLocalizable`
 * их закрывает.
 *
 * Ассет НЕ сохраняется и НЕ компилируется — это делает вызывающая сторона
 * (save_asset → reload_packages), как и у `XRU1StateTreeAuthoringLibrary`.
 */
UCLASS()
class XRU1_API UXRU1LocalizationAuthoringLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Отчёт по текстам ассета: строки вида
	 * `[LOC]|<путь свойства>|<текст>` — уже локализуемый (есть ключ),
	 * `[RAW]|<путь свойства>|<текст>` — culture-invariant, Gather его не увидит.
	 *
	 * Диагностика без побочных эффектов: ей проверяется и до, и после правки.
	 */
	UFUNCTION(BlueprintCallable, Category = "XRU1|Localization Authoring")
	static TArray<FString> AuditAssetTexts(const FString& AssetPath);

	/**
	 * Назначает ключ каждому тексту ассета, у которого его нет.
	 *
	 * `Namespace` — общий namespace ассета (например `XRU1.Tutorial`): он
	 * попадёт в манифест и по нему переводы группируются в архиве.
	 * `bDryRun` — только посчитать, ничего не менять.
	 *
	 * Возвращает число исправленных текстов (при `bDryRun` — сколько бы
	 * исправила). Пакет помечается dirty; сохранять — вызывающей стороне.
	 */
	UFUNCTION(BlueprintCallable, Category = "XRU1|Localization Authoring")
	static int32 MakeAssetTextsLocalizable(const FString& AssetPath, const FString& Namespace,
		bool bDryRun = false);

	/**
	 * То же по списку ассетов за один вызов; возвращает суммарное число правок.
	 * Namespace общий: тексты одного сценария логично держать вместе.
	 */
	UFUNCTION(BlueprintCallable, Category = "XRU1|Localization Authoring")
	static int32 MakeAssetsTextsLocalizable(const TArray<FString>& AssetPaths,
		const FString& Namespace, bool bDryRun = false);
};
