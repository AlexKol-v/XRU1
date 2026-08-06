// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class XRU1 : ModuleRules
{
	public XRU1(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"AIModule",
			"NavigationSystem",
			// EPhysicalSurface/UPhysicalMaterial: шаги выбирают звук по реальной
			// поверхности под ногой (Audio/AnimNotify_UnitFootstep).
			"PhysicsCore",
			"StateTreeModule",
			"GameplayStateTreeModule",
			"Niagara",
			"UMG",
			"Slate",
			"SlateCore",
			// GAS (migrated Characters/UI + tactical skeletons)
			"GameplayAbilities",
			"GameplayTags",
			"GameplayTasks",
			// Tutorial/mission objectives: StateTree quest runner + event bus.
			"STQuestSystem",
			"GameplayMessageRuntime",
			// CommonUI (migrated PrimaryGameLayout / GameUIManagerSubsystem + menu skeletons)
			"CommonUI",
			"CommonInput",
			// Intro media source stored in the global UI theme DataAsset
			"MediaAssets",
			// UMediaSoundComponent наследует USynthComponent, чей Start/Stop живёт
			// в AudioMixer: без этого модуля звук интро не линкуется.
			"AudioMixer",
			// UDeveloperSettings: проектные настройки слоя субтитров и языка
			// (Subtitles/SubtitleProjectSettings) живут в Project Settings.
			"DeveloperSettings",
			// PCG nodes (hub landscape scatter/slope filter)
			"PCG",
			// Move-range zone fill (Tactics/MoveRangeVisualizer)
			"ProceduralMeshComponent",
			// Поиск ассетов по классу в РАНТАЙМЕ: «Пропустить обучение» находит
			// все Tutorial-сценарии через AssetRegistry, без жёстких путей.
			"AssetRegistry",
			// Source Effects радио-обработки реплик (Audio/XRU1AudioAuthoringLibrary:
			// BandPass + BitCrusher из плагина Synthesis; сами цепочки — рантайм).
			"Synthesis"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { });

		// Editor-only: программная сборка вёрстки Widget Blueprint из кода
		// (UI/Editor/XRU1WidgetAuthoringLibrary) — MCP-мост не умеет WidgetTree,
		// поэтому агент строит экраны через собственную editor-библиотеку.
		// StateTreeEditorModule (UncookedOnly) — структурные правки квест-графа
		// (Tactics/Editor/XRU1StateTreeAuthoringLibrary): создать состояние из
		// Python нельзя, Children/SubTrees не экспонированы.
		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.AddRange(new string[] {
				"UnrealEd",
				"UMGEditor",
				"StateTreeEditorModule",
				// FStateTreeCompilerLog (перекомпиляция дерева после правки текстов
				// в Tactics/Editor/XRU1LocalizationAuthoringLibrary) держит в себе
				// FPropertyBindingBindableStructDescriptor — без модуля не линкуется.
				"PropertyBindingUtils"
			});
		}

		PublicIncludePaths.AddRange(new string[] {
			"XRU1",
			"XRU1/Audio",
			"XRU1/FX",
			"XRU1/Characters",
			"XRU1/UI",
			"XRU1/Hub",
			"XRU1/Interaction",
			"XRU1/PCG",
			"XRU1/Tactics",
			"XRU1/Subtitles",
			"XRU1/UI/Menus"
		});

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
