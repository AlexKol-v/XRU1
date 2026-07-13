// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class STQuestSystem : ModuleRules
{
	public STQuestSystem(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"NetCore",
				"GameplayTags",
				"StateTreeModule",
				"GameplayStateTreeModule",
				"GameplayMessageRuntime",
				"GameplayAbilities",
				"CommonUI",
				"UMG",
				"Slate",
				"SlateCore"
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
			}
		);
	}
}
