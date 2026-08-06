#include "XRU1LocalizationAuthoringLibrary.h"

#include "XRU1Log.h"

#if WITH_EDITOR
#include "Internationalization/TextPackageNamespaceUtil.h"
#include "StateTree.h"
#include "StateTreeCompilerLog.h"
#include "StateTreeEditingSubsystem.h"
#include "StructUtils/InstancedStruct.h"
#include "UObject/Package.h"
#include "UObject/UObjectIterator.h"
#endif

#if WITH_EDITOR
namespace XRU1LocalizationAuthoring
{
	/**
	 * Ключ живёт в архиве перевода как идентификатор строки, поэтому в нём
	 * оставлены только безопасные символы. Важнее косметики другое: ключ
	 * ДЕТЕРМИНИРОВАННЫЙ (путь свойства), значит повторный прогон утилиты даёт тот
	 * же ключ и уже сделанный перевод к строке остаётся привязан.
	 */
	static FString SanitizeKey(const FString& In)
	{
		FString Out;
		Out.Reserve(In.Len());
		for (const TCHAR Ch : In)
		{
			const bool bSafe = (Ch >= TEXT('A') && Ch <= TEXT('Z'))
				|| (Ch >= TEXT('a') && Ch <= TEXT('z'))
				|| (Ch >= TEXT('0') && Ch <= TEXT('9'))
				|| Ch == TEXT('_') || Ch == TEXT('.') || Ch == TEXT('-');
			Out.AppendChar(bSafe ? Ch : TEXT('_'));
		}
		return Out;
	}

	/** Текст уже участвует в локализации: у него есть ключ или он из String Table. */
	static bool IsAlreadyLocalizable(const FText& Text)
	{
		if (Text.IsFromStringTable())
		{
			return true;
		}
		return FTextInspector::GetKey(Text).IsSet();
	}

	using FTextVisitor = TFunctionRef<void(FText& /*Text*/, const FString& /*PropertyPath*/)>;

	static void VisitStruct(const UStruct* Struct, void* Container, const FString& Path,
		const FTextVisitor& Visitor);

	/** Одно значение произвольного типа: текст, структура или контейнер. */
	static void VisitValue(const FProperty* Property, void* ValuePtr, const FString& Path,
		const FTextVisitor& Visitor)
	{
		if (!Property || !ValuePtr)
		{
			return;
		}

		if (Property->IsA<FTextProperty>())
		{
			Visitor(*static_cast<FText*>(ValuePtr), Path);
			return;
		}

		if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
		{
			// FInstancedStruct хранит данные за указателем, а не полями: именно так
			// живёт instance data задач StateTree (реплики, подсказки, описания
			// целей). Без этой ветки весь авторский текст обучения остаётся
			// недостижимым для обхода.
			if (StructProperty->Struct == TBaseStructure<FInstancedStruct>::Get()
				|| StructProperty->Struct == FInstancedStruct::StaticStruct())
			{
				FInstancedStruct& Instanced = *static_cast<FInstancedStruct*>(ValuePtr);
				if (const UScriptStruct* Inner = Instanced.GetScriptStruct())
				{
					VisitStruct(Inner, Instanced.GetMutableMemory(), Path, Visitor);
				}
				return;
			}
			VisitStruct(StructProperty->Struct, ValuePtr, Path, Visitor);
			return;
		}

		if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
		{
			FScriptArrayHelper Helper(ArrayProperty, ValuePtr);
			for (int32 Index = 0; Index < Helper.Num(); ++Index)
			{
				VisitValue(ArrayProperty->Inner, Helper.GetRawPtr(Index),
					FString::Printf(TEXT("%s.%d"), *Path, Index), Visitor);
			}
			return;
		}

		if (const FSetProperty* SetProperty = CastField<FSetProperty>(Property))
		{
			FScriptSetHelper Helper(SetProperty, ValuePtr);
			for (FScriptSetHelper::FIterator It(Helper); It; ++It)
			{
				VisitValue(SetProperty->ElementProp, Helper.GetElementPtr(It.GetInternalIndex()),
					FString::Printf(TEXT("%s.%d"), *Path, It.GetLogicalIndex()), Visitor);
			}
			return;
		}

		if (const FMapProperty* MapProperty = CastField<FMapProperty>(Property))
		{
			FScriptMapHelper Helper(MapProperty, ValuePtr);
			for (FScriptMapHelper::FIterator It(Helper); It; ++It)
			{
				const FString ElementPath = FString::Printf(TEXT("%s.%d"), *Path, It.GetLogicalIndex());
				VisitValue(MapProperty->KeyProp, Helper.GetKeyPtr(It.GetInternalIndex()),
					ElementPath + TEXT(".Key"), Visitor);
				VisitValue(MapProperty->ValueProp, Helper.GetValuePtr(It.GetInternalIndex()),
					ElementPath + TEXT(".Value"), Visitor);
			}
			return;
		}

		// Объектные ссылки не разворачиваем: вложенные объекты пакета обходятся
		// отдельно (ForEachObjectWithOuter), иначе один и тот же объект правился бы
		// дважды, а ссылка на чужой пакет увела бы обход в весь контент проекта.
	}

	static void VisitStruct(const UStruct* Struct, void* Container, const FString& Path,
		const FTextVisitor& Visitor)
	{
		if (!Struct || !Container)
		{
			return;
		}
		for (TFieldIterator<FProperty> It(Struct); It; ++It)
		{
			const FProperty* Property = *It;

			// Editor-only свойства пропускаем: Gather настроен на
			// `ShouldGatherFromEditorOnlyData=false` и в манифест их не берёт, а
			// ключи на них — мусор в ассете. Практический пример —
			// `UWidget::PaletteCategory` («Common UI») в каждом WBP: 24 текста,
			// которые игрок не увидит никогда.
			if (Property->HasAnyPropertyFlags(CPF_EditorOnly))
			{
				continue;
			}

			for (int32 ArrayIndex = 0; ArrayIndex < Property->ArrayDim; ++ArrayIndex)
			{
				const FString ChildPath = Property->ArrayDim > 1
					? FString::Printf(TEXT("%s.%s_%d"), *Path, *Property->GetName(), ArrayIndex)
					: FString::Printf(TEXT("%s.%s"), *Path, *Property->GetName());
				VisitValue(Property,
					Property->ContainerPtrToValuePtr<void>(Container, ArrayIndex),
					ChildPath, Visitor);
			}
		}
	}

	/** Ассет + все объекты его пакета: у StateTree текст лежит в состояниях-объектах. */
	static bool CollectPackageObjects(const FString& AssetPath, UObject*& OutAsset,
		TArray<UObject*>& OutObjects)
	{
		OutAsset = LoadObject<UObject>(nullptr, *AssetPath);
		if (!OutAsset)
		{
			UE_LOG(LogXRU1UI, Error, TEXT("[LocAuthoring] Ассет не найден: %s"), *AssetPath);
			return false;
		}

		UPackage* Package = OutAsset->GetOutermost();
		OutObjects.Add(OutAsset);
		ForEachObjectWithOuter(Package, [&OutObjects](UObject* Object)
		{
			if (IsValid(Object))
			{
				OutObjects.AddUnique(Object);
			}
		}, /*bIncludeNestedObjects*/ true);
		return true;
	}

	/** Имя объекта относительно пакета — стабильная часть ключа. */
	static FString MakeObjectPathPrefix(const UObject* Object, const UObject* Asset)
	{
		if (Object == Asset)
		{
			return Object->GetName();
		}
		FString Path = Object->GetPathName(Object->GetOutermost());
		Path.RemoveFromStart(TEXT("."));
		return Path;
	}

	/**
	 * Скомпилированные данные StateTree живут в контейнере с собственной
	 * сериализацией, до которого обход по свойствам не достаёт. Поэтому после
	 * правки editor-данных дерево ПЕРЕСОБИРАЕТСЯ: компилятор переносит ключи в
	 * runtime-данные, а Gather смотрит именно на них
	 * (`ShouldGatherFromEditorOnlyData=false`).
	 */
	static void RecompileIfStateTree(UObject* Asset)
	{
		UStateTree* StateTree = Cast<UStateTree>(Asset);
		if (!StateTree)
		{
			return;
		}
		FStateTreeCompilerLog Log;
		const bool bCompiled = UStateTreeEditingSubsystem::CompileStateTree(StateTree, Log);
		UE_LOG(LogXRU1UI, Display, TEXT("[LocAuthoring] StateTree %s перекомпилирован: %s"),
			*StateTree->GetName(), bCompiled ? TEXT("успешно") : TEXT("С ОШИБКАМИ"));
	}
}
#endif // WITH_EDITOR

TArray<FString> UXRU1LocalizationAuthoringLibrary::AuditAssetTexts(const FString& AssetPath)
{
	TArray<FString> Report;
#if WITH_EDITOR
	using namespace XRU1LocalizationAuthoring;

	UObject* Asset = nullptr;
	TArray<UObject*> Objects;
	if (!CollectPackageObjects(AssetPath, Asset, Objects))
	{
		return Report;
	}

	for (UObject* Object : Objects)
	{
		const FString Prefix = MakeObjectPathPrefix(Object, Asset);
		VisitStruct(Object->GetClass(), Object, Prefix,
			[&Report](FText& Text, const FString& PropertyPath)
			{
				if (Text.IsEmpty())
				{
					return;
				}
				Report.Add(FString::Printf(TEXT("%s|%s|%s"),
					IsAlreadyLocalizable(Text) ? TEXT("[LOC]") : TEXT("[RAW]"),
					*PropertyPath, *Text.ToString()));
			});
	}
#endif
	return Report;
}

int32 UXRU1LocalizationAuthoringLibrary::MakeAssetTextsLocalizable(const FString& AssetPath,
	const FString& Namespace, bool bDryRun)
{
	int32 Fixed = 0;
#if WITH_EDITOR
	using namespace XRU1LocalizationAuthoring;

	UObject* Asset = nullptr;
	TArray<UObject*> Objects;
	if (!CollectPackageObjects(AssetPath, Asset, Objects))
	{
		return 0;
	}

	const FString EffectiveNamespace = Namespace.IsEmpty() ? TEXT("XRU1.Assets") : Namespace;

	for (UObject* Object : Objects)
	{
		const FString Prefix = MakeObjectPathPrefix(Object, Asset);
		bool bObjectChanged = false;

		VisitStruct(Object->GetClass(), Object, Prefix,
			[&Fixed, &bObjectChanged, &EffectiveNamespace, bDryRun]
			(FText& Text, const FString& PropertyPath)
			{
				if (Text.IsEmpty() || IsAlreadyLocalizable(Text))
				{
					return;
				}
				++Fixed;
				if (bDryRun)
				{
					return;
				}
				Text = FText::ChangeKey(FTextKey(EffectiveNamespace),
					FTextKey(SanitizeKey(PropertyPath)), Text);
				bObjectChanged = true;
			});

		if (bObjectChanged)
		{
			Object->Modify();
			Object->MarkPackageDirty();
		}
	}

	if (Fixed > 0 && !bDryRun)
	{
		RecompileIfStateTree(Asset);
	}

	UE_LOG(LogXRU1UI, Display,
		TEXT("[LocAuthoring] %s: %s %d текст(ов) без ключа (namespace '%s')"),
		*AssetPath, bDryRun ? TEXT("найдено") : TEXT("исправлено"), Fixed, *EffectiveNamespace);
#endif
	return Fixed;
}

int32 UXRU1LocalizationAuthoringLibrary::MakeAssetsTextsLocalizable(const TArray<FString>& AssetPaths,
	const FString& Namespace, bool bDryRun)
{
	int32 Total = 0;
	for (const FString& Path : AssetPaths)
	{
		Total += MakeAssetTextsLocalizable(Path, Namespace, bDryRun);
	}
	return Total;
}
