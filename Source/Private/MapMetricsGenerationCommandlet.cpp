#include "MapMetricsGenerationCommandlet.h"

#include "Components/LightComponentBase.h"
#include "Kismet/GameplayStatics.h"

#include <Editor.h>
#include <Engine/LevelStreaming.h>
#include <Engine/World.h>
#include <Misc/PackageName.h>

// ReSharper disable once CppInconsistentNaming
DEFINE_LOG_CATEGORY_STATIC( LogMapMetricsGeneration, Verbose, All )

UMapMetricsGenerationCommandlet::UMapMetricsGenerationCommandlet()
{
    LogToConsole = false;
}

int32 UMapMetricsGenerationCommandlet::Main( const FString & params )
{
    UE_LOG( LogMapMetricsGeneration, Log, TEXT( "--------------------------------------------------------------------------------------------" ) );
    UE_LOG( LogMapMetricsGeneration, Log, TEXT( "Running MapMetricsGeneration Commandlet" ) );
    TArray< FString > tokens;
    TArray< FString > switches;
    TMap< FString, FString > params_map;
    ParseCommandLine( *params, tokens, switches, params_map );

    TArray< FString > package_names;

    for ( const auto & param_key_pair : params_map )
    {
        if ( param_key_pair.Key == "Maps" )
        {
            auto map_parameter_value = param_key_pair.Value;

            const auto add_package = [ &package_names ]( const FString & package_name ) {
                FString map_file;
                FPackageName::SearchForPackageOnDisk( package_name, nullptr, &map_file );

                if ( map_file.IsEmpty() )
                {
                    UE_LOG( LogMapMetricsGeneration, Error, TEXT( "Could not find package %s" ), *package_name );
                }
                else
                    package_names.Add( *map_file );
            };

            // Allow support for -Map=Value1+Value2+Value3
            TArray< FString > maps_package_names;
            map_parameter_value.ParseIntoArray( maps_package_names, TEXT( "," ) );

            if ( maps_package_names.Num() > 0 )
            {
                for ( const auto & map_package_name : maps_package_names )
                {
                    add_package( map_package_name );
                }
            }
            else
            {
                add_package( map_parameter_value );
            }
        }
    }

    if ( package_names.Num() == 0 )
    {
        UE_LOG( LogMapMetricsGeneration, Error, TEXT( "No maps were checked" ) );
        return 2;
    }

    for ( const auto & package_name : package_names )
    {
        UE_LOG( LogMapMetricsGeneration, Log, TEXT( "Will process %s" ), *package_name );

        auto * package = LoadPackage( nullptr, *package_name, 0 );
        if ( package == nullptr )
        {
            UE_LOG( LogMapMetricsGeneration, Error, TEXT( "Cannot load package %s" ), *package_name );
            return 2;
        }

        auto * world = UWorld::FindWorldInPackage( package );
        if ( world == nullptr )
        {
            UE_LOG( LogMapMetricsGeneration, Error, TEXT( "Cannot get a world in the package %s" ), *package_name );
            return 2;
        }

        world->WorldType = EWorldType::Editor;
        world->AddToRoot();
        if ( !world->bIsWorldInitialized )
        {
            UWorld::InitializationValues ivs;
            ivs.RequiresHitProxies( false );
            ivs.ShouldSimulatePhysics( false );
            ivs.EnableTraceCollision( false );
            ivs.CreateNavigation( false );
            ivs.CreateAISystem( false );
            ivs.AllowAudioPlayback( false );
            ivs.CreatePhysicsScene( true );

            world->InitWorld( ivs );
            world->PersistentLevel->UpdateModelComponents();
            world->UpdateWorldComponents( true, false );
        }

        auto & world_context = GEditor->GetEditorWorldContext( true );
        world_context.SetCurrentWorld( world );
        GWorld = world;

        world->LoadSecondaryLevels( true, nullptr );

        const auto & streaming_levels = world->GetStreamingLevels();

        for ( auto * streaming_level : streaming_levels )
        {
            streaming_level->SetShouldBeVisible( true );
            streaming_level->SetShouldBeLoaded( true );
        }

        UE_LOG( LogMapMetricsGeneration, Log, TEXT( "Load %i streaming levels for world %s" ), streaming_levels.Num(), *world->GetName() );

        world->FlushLevelStreaming( EFlushLevelStreamingType::Full );

        TArray< AActor * > all_actors;
        UGameplayStatics::GetAllActorsOfClass( world, AActor::StaticClass(), all_actors );

        int StaticLightCount = 0;
        int StationaryLightCount = 0;
        int MoveableLightCount = 0;
        TMap< FString, int > StaticLightComponentsMap, StationaryLightComponentsMap, MoveableLightComponentsMap;

        int SMWithLODsCount = 0;
        int SMWithoutLODsCount = 0;
        //TMap< FString, int > StaticLightComponentsMap, StationaryLightComponentsMap, MoveableLightComponentsMap;

        for ( auto * actor : all_actors )
        {
            TArray< ULightComponentBase * > light_components;
            actor->GetComponents< ULightComponentBase >( light_components );

            for ( auto * light_component : light_components )
            {
                TMap< FString, int > * target_map = nullptr;

                switch ( light_component->Mobility )
                {
                    case EComponentMobility::Movable:
                    {
                        target_map = &MoveableLightComponentsMap;
                        MoveableLightCount++;
                    }
                    break;
                    case EComponentMobility::Static:
                    {
                        target_map = &StaticLightComponentsMap;
                        StaticLightCount++;
                    }
                    break;
                    case EComponentMobility::Stationary:
                    {
                        target_map = &StationaryLightComponentsMap;
                        StationaryLightCount++;
                    }
                    break;
                    default:
                    {
                        checkNoEntry();
                    }
                    break;
                }

                target_map->FindOrAdd( actor->GetName() )++;
            }

            TArray< UStaticMeshComponent * > sm_components;
            actor->GetComponents< UStaticMeshComponent >( sm_components );

            for ( auto * sm_component : sm_components )
            {
                if ( sm_component->GetStaticMesh()->GetNumLODs() == 1 )
                {
                    SMWithoutLODsCount++;
                }
                else
                {
                    SMWithLODsCount++;
                }
            }
        }

        UE_LOG( LogMapMetricsGeneration, Log, TEXT( "------------------------------" ) );
        UE_LOG( LogMapMetricsGeneration, Log, TEXT( "Lights report:" ) );
        UE_LOG( LogMapMetricsGeneration, Log, TEXT( "Static lights: %i" ), StaticLightCount );
        UE_LOG( LogMapMetricsGeneration, Log, TEXT( "Stationary lights: %i" ), StationaryLightCount );
        UE_LOG( LogMapMetricsGeneration, Log, TEXT( "Moveable lights: %i" ), MoveableLightCount );
        UE_LOG( LogMapMetricsGeneration, Log, TEXT( "------------------------------" ) );

        UE_LOG( LogMapMetricsGeneration, Log, TEXT( "" ) );

        UE_LOG( LogMapMetricsGeneration, Log, TEXT( "------------------------------" ) );
        UE_LOG( LogMapMetricsGeneration, Log, TEXT( "Static Meshes report:" ) );
        UE_LOG( LogMapMetricsGeneration, Log, TEXT( "With LODs: %i" ), SMWithLODsCount );
        UE_LOG( LogMapMetricsGeneration, Log, TEXT( "Without LODs: %i" ), SMWithoutLODsCount );
        UE_LOG( LogMapMetricsGeneration, Log, TEXT( "------------------------------" ) );

        world->RemoveFromRoot();

        world_context.SetCurrentWorld( nullptr );
        GWorld = nullptr;

        UE_LOG( LogMapMetricsGeneration, Log, TEXT( "Finished processing of %s" ), *package_name );
    }

    UE_LOG( LogMapMetricsGeneration, Log, TEXT( "Successfully finished running MapCheckValidation Commandlet" ) );
    UE_LOG( LogMapMetricsGeneration, Log, TEXT( "--------------------------------------------------------------------------------------------" ) );
    return 0;
}
