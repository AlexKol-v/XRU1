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
			// CommonUI (migrated PrimaryGameLayout / GameUIManagerSubsystem + menu skeletons)
			"CommonUI",
			"CommonInput",
			// Intro media source stored in the global UI theme DataAsset
			"MediaAssets",
			// PCG nodes (hub landscape scatter/slope filter)
			"PCG",
			// Move-range zone fill (Tactics/MoveRangeVisualizer)
			"ProceduralMeshComponent"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { });

		PublicIncludePaths.AddRange(new string[] {
			"XRU1",
			"XRU1/Characters",
			"XRU1/UI",
			"XRU1/Interaction",
			"XRU1/PCG",
			"XRU1/Tactics",
			"XRU1/UI/Menus"
		});

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
