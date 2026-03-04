// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class fal3DDemo : ModuleRules
{
	public fal3DDemo(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput",
			"HTTP", "Json", "JsonUtilities",
			"UMG", "Slate", "SlateCore",
			"glTFRuntime"
		});
	}
}
