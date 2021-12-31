using System;
using UnrealBuildTool;

public class FModel : ModuleRules
{
	public FModel(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicIncludePaths.Add(EngineDirectory + "/Source/Runtime/Launch/Public");

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"AppFramework",
				"ApplicationCore",
				"Core",
				"EditorStyle",
				"OutputLog",
				"PakFile",
				"Projects",
				"Slate",
				"SlateCore",
				"SourceCodeAccess",
				"StandaloneRenderer",
			}
		);

		PrivateIncludePathModuleNames.AddRange(
			new string[] {
				"SlateReflector",
			}
		);

		DynamicallyLoadedModuleNames.AddRange(
			new string[] {
				"SlateReflector",
			}
		);

		if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			PrivateDependencyModuleNames.Add("XCodeSourceCodeAccess");
			AddEngineThirdPartyPrivateStaticDependencies(Target, "CEF3");
		}
		else if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			PrivateDependencyModuleNames.Add("VisualStudioSourceCodeAccess");
		}

		PrivateIncludePaths.Add(EngineDirectory + "/Source/Runtime/Launch/Private");		// For LaunchEngineLoop.cpp include

		if (Target.IsInPlatformGroup(UnrealPlatformGroup.Linux))
		{
			PrivateDependencyModuleNames.AddRange(
				new string[] {
					"UnixCommonStartup"
				}
			);
		}
	}
}
