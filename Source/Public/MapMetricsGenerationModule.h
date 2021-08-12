#pragma once

#include <CoreMinimal.h>
#include <Modules/ModuleInterface.h>
#include <Modules/ModuleManager.h>
#include <Stats/Stats.h>

class MAPMETRICSGENERATION_API IMapMetricsGenerationModule : public IModuleInterface
{

public:
    static IMapMetricsGenerationModule & Get()
    {
        QUICK_SCOPE_CYCLE_COUNTER( STAT_IMapMetricsGenerationModule_Get );
        static auto & singleton = FModuleManager::LoadModuleChecked< IMapMetricsGenerationModule >( "MapMetricsGeneration" );
        return singleton;
    }

    static bool IsAvailable()
    {
        QUICK_SCOPE_CYCLE_COUNTER( STAT_IMapMetricsGenerationModule_IsAvailable );
        return FModuleManager::Get().IsModuleLoaded( "MapMetricsGeneration" );
    }
};
