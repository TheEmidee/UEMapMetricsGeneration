#include "MapMetricsGenerationModule.h"

class FMapMetricsGenerationModule final : public IMapMetricsGenerationModule
{
public:
    void StartupModule() override;
    void ShutdownModule() override;
};

IMPLEMENT_MODULE( FMapMetricsGenerationModule, MapMetricsGeneration )

void FMapMetricsGenerationModule::StartupModule()
{
}

void FMapMetricsGenerationModule::ShutdownModule()
{
}