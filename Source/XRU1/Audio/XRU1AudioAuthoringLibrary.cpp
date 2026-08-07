#include "XRU1AudioAuthoringLibrary.h"

#include "XRU1Log.h"

#include "Sound/SoundBase.h"
#include "Sound/SoundEffectSource.h"
#include "SourceEffects/SourceEffectBitCrusher.h"
#include "SourceEffects/SourceEffectFilter.h"
#include "UObject/Package.h"

#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#endif

namespace XRU1AudioAuthoring
{
	/** Создать (или вернуть существующий) ассет-объект в собственном пакете. */
	template <typename T>
	static T* FindOrCreateAsset(const FString& Folder, const FString& Name)
	{
#if WITH_EDITOR
		const FString PackageName = Folder / Name;
		if (T* Existing = LoadObject<T>(nullptr, *(PackageName + TEXT(".") + Name)))
		{
			return Existing;
		}
		UPackage* Package = CreatePackage(*PackageName);
		if (!Package)
		{
			return nullptr;
		}
		T* Asset = NewObject<T>(Package, *Name, RF_Public | RF_Standalone);
		FAssetRegistryModule::AssetCreated(Asset);
		Asset->MarkPackageDirty();
		return Asset;
#else
		return nullptr;
#endif
	}
}

FString UXRU1AudioAuthoringLibrary::CreateRadioEffectChain(const FString& ChainFolder)
{
#if WITH_EDITOR
	using namespace XRU1AudioAuthoring;

	// Телефонная/эфирная полоса — 300..3400 Гц. Один
	// BandPass 1.7 кГц не годится: полоса получается чрезмерно узкой и «зажатой». Правильная
	// классика обработки walkie-talkie — ДВА среза (HPF + LPF) вместо колокола:
	// внутри полосы речь остаётся ровной и разборчивой, режутся только края.
	USourceEffectFilterPreset* HighPass = FindOrCreateAsset<USourceEffectFilterPreset>(
		ChainFolder, TEXT("SFX_Radio_HighPass"));
	USourceEffectFilterPreset* LowPass = FindOrCreateAsset<USourceEffectFilterPreset>(
		ChainFolder, TEXT("SFX_Radio_LowPass"));
	// BitCrusher — цифровая деградация, узнаваемый «хруст» эфира.
	USourceEffectBitCrusherPreset* Crusher = FindOrCreateAsset<USourceEffectBitCrusherPreset>(
		ChainFolder, TEXT("SFX_Radio_BitCrusher"));
	USoundEffectSourcePresetChain* Chain = FindOrCreateAsset<USoundEffectSourcePresetChain>(
		ChainFolder, TEXT("Chain_RadioVoice"));
	if (!HighPass || !LowPass || !Crusher || !Chain)
	{
		UE_LOG(LogXRU1UI, Error, TEXT("[Audio] не удалось создать ассеты радио-цепочки в %s"),
			*ChainFolder);
		return FString();
	}

	{
		FSourceEffectFilterSettings Settings;
		Settings.FilterType = ESourceEffectFilterType::HighPass;
		Settings.CutoffFrequency = 350.f; // низы «в корпусе рации» не проходят
		Settings.FilterQ = 0.9f;          // без резонанса на срезе
		HighPass->SetSettings(Settings);
		HighPass->MarkPackageDirty();
	}
	{
		FSourceEffectFilterSettings Settings;
		Settings.FilterType = ESourceEffectFilterType::LowPass;
		Settings.CutoffFrequency = 3200.f; // верх эфирной полосы
		Settings.FilterQ = 0.9f;
		LowPass->SetSettings(Settings);
		LowPass->MarkPackageDirty();
	}
	{
		// Мягче первой версии (8 кГц/10 бит «пережёвывали» речь): деградация
		// слышна как эфирный шершавый край, но каждое слово читается.
		FSourceEffectBitCrusherBaseSettings Settings;
		Settings.SampleRate = 11025.f;
		Settings.BitDepth = 12.f;
		Crusher->SetSettings(Settings);
		Crusher->MarkPackageDirty();
	}

	Chain->Chain.Reset();
	for (USoundEffectSourcePreset* Preset :
		{ static_cast<USoundEffectSourcePreset*>(HighPass),
		  static_cast<USoundEffectSourcePreset*>(LowPass),
		  static_cast<USoundEffectSourcePreset*>(Crusher) })
	{
		FSourceEffectChainEntry& Entry = Chain->Chain.AddDefaulted_GetRef();
		Entry.Preset = Preset;
		Entry.bBypass = false;
	}
	Chain->MarkPackageDirty();

	const FString ChainPath = ChainFolder / TEXT("Chain_RadioVoice.Chain_RadioVoice");
	UE_LOG(LogXRU1UI, Display, TEXT("[Audio] радио-цепочка готова: %s"), *ChainPath);
	return ChainPath;
#else
	return FString();
#endif
}

int32 UXRU1AudioAuthoringLibrary::AssignSourceEffectChain(const TArray<FString>& SoundAssetPaths,
	const FString& ChainAssetPath)
{
	USoundEffectSourcePresetChain* Chain =
		LoadObject<USoundEffectSourcePresetChain>(nullptr, *ChainAssetPath);
	if (!Chain)
	{
		UE_LOG(LogXRU1UI, Error, TEXT("[Audio] цепочка не найдена: %s"), *ChainAssetPath);
		return 0;
	}

	int32 Changed = 0;
	for (const FString& Path : SoundAssetPaths)
	{
		USoundBase* Sound = LoadObject<USoundBase>(nullptr, *Path);
		if (!Sound)
		{
			UE_LOG(LogXRU1UI, Warning, TEXT("[Audio] звук не найден: %s"), *Path);
			continue;
		}
		if (Sound->SourceEffectChain == Chain)
		{
			continue;
		}
		Sound->SourceEffectChain = Chain;
		Sound->MarkPackageDirty();
		++Changed;
	}
	UE_LOG(LogXRU1UI, Display, TEXT("[Audio] цепочка %s назначена %d звукам"),
		*Chain->GetName(), Changed);
	return Changed;
}
