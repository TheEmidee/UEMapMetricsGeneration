namespace UnrealBuildTool.Rules
{
    public class MapMetricsGeneration : ModuleRules
    {
        public MapMetricsGeneration( ReadOnlyTargetRules Target )
            : base( Target )
        {
            PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
            
            
            PublicDependencyModuleNames.AddRange(
                new string[] { 
                    "Core",
                    "CoreUObject",
                    "Engine",
                    "TargetPlatform",
                    "AssetRegistry",
                    "Json",
                    "Niagara"
                }
            );

            PrivateDependencyModuleNames.AddRange(
                new string[] {
                    "Slate",
                    "SlateCore",
                    "UnrealEd",
                    "AssetRegistry",
                    "EditorStyle",
                    "Blutility"
                }
            );
        }
    }
}
