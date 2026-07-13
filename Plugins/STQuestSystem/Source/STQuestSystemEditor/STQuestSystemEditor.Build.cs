// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class STQuestSystemEditor : ModuleRules
{
	public STQuestSystemEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine"
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"UnrealEd",
				"AssetTools",
				"ClassViewer",
				"InputCore",
				"Slate",
				"SlateCore",
				"ToolMenus",
				"DataValidation",
				"GameplayTags",
				"GameplayTagsEditor",
				"STQuestSystem"
			}
		);
	}
}
