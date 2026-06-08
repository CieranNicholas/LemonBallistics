// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class LemonBallistics : ModuleRules
{
	public LemonBallistics(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		// Fully damage-system-agnostic: no GameplayAbilities/GAS dependency. A hit is reported via the
		// FLemonProjectileFireParams::OnHit delegate and the caller decides what it does.
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine"
		});

		// Niagara is private: public headers only forward-declare UNiagaraComponent / UNiagaraSystem;
		// the concrete Niagara API is used solely inside the .cpp.
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Niagara"
		});
	}
}
