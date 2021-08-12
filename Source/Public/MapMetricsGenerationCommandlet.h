#pragma once

#include <Commandlets/Commandlet.h>

#include "MapMetricsGenerationCommandlet.generated.h"

UCLASS( CustomConstructor )
class MAPMETRICSGENERATION_API UMapMetricsGenerationCommandlet final : public UCommandlet
{
    GENERATED_BODY()

public:

    UMapMetricsGenerationCommandlet();

    int32 Main( const FString & params ) override;
};
