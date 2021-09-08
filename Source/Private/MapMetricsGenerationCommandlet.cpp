#include "MapMetricsGenerationCommandlet.h"

#include "Chaos/AABB.h"
#include "Components/LightComponentBase.h"
#include "Dom/JsonObject.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraComponent.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

#include <Editor.h>
#include <Engine/LevelStreaming.h>
#include <Engine/World.h>
#include <Misc/PackageName.h>

// ReSharper disable once CppInconsistentNaming
DEFINE_LOG_CATEGORY_STATIC( LogMapMetricsGeneration, Verbose, All )

namespace
{
    struct FLevelLoader
    {
        explicit FLevelLoader( const FString & level_name ) :
            World( nullptr ),
            WorldContext( nullptr )
        {
            UE_LOG( LogMapMetricsGeneration, Log, TEXT( "Will process %s" ), *level_name );

            auto * package = LoadPackage( nullptr, *level_name, 0 );
            if ( package == nullptr )
            {
                UE_LOG( LogMapMetricsGeneration, Error, TEXT( "Cannot load package %s" ), *level_name );
                return;
            }

            World = UWorld::FindWorldInPackage( package );
            if ( World == nullptr )
            {
                UE_LOG( LogMapMetricsGeneration, Error, TEXT( "Cannot get a world in the package %s" ), *level_name );
                return;
            }

            World->WorldType = EWorldType::Editor;
            World->AddToRoot();
            if ( !World->bIsWorldInitialized )
            {
                UWorld::InitializationValues ivs;
                ivs.RequiresHitProxies( false );
                ivs.ShouldSimulatePhysics( false );
                ivs.EnableTraceCollision( false );
                ivs.CreateNavigation( false );
                ivs.CreateAISystem( false );
                ivs.AllowAudioPlayback( false );
                ivs.CreatePhysicsScene( true );

                World->InitWorld( ivs );
                World->PersistentLevel->UpdateModelComponents();
                World->UpdateWorldComponents( true, false );
            }

            WorldContext = &GEditor->GetEditorWorldContext( true );
            WorldContext->SetCurrentWorld( World );
            GWorld = World;

            World->LoadSecondaryLevels( true, nullptr );

            const auto & streaming_levels = World->GetStreamingLevels();

            for ( auto * streaming_level : streaming_levels )
            {
                streaming_level->SetShouldBeVisible( true );
                streaming_level->SetShouldBeLoaded( true );
            }

            UE_LOG( LogMapMetricsGeneration, Log, TEXT( "Load %i streaming levels for world %s" ), streaming_levels.Num(), *World->GetName() );

            World->FlushLevelStreaming( EFlushLevelStreamingType::Full );
        }

        ~FLevelLoader()
        {
            if ( World != nullptr )
            {
                World->RemoveFromRoot();
            }

            if ( WorldContext != nullptr )
            {
                WorldContext->SetCurrentWorld( nullptr );
            }

            GWorld = nullptr;
        }

        UWorld * GetWorld() const
        {
            return World;
        }

    private:
        UWorld * World;
        FWorldContext * WorldContext;
    };

    struct FMetrics : TSharedFromThis< FMetrics >
    {
        virtual ~FMetrics() = default;
        virtual void ProcessActor( AActor * actor ) = 0;

        void GenerateReport( FJsonObject & json_object )
        {
            UE_LOG( LogMapMetricsGeneration, Log, TEXT( "------------------------------" ) );
            UE_LOG( LogMapMetricsGeneration, Log, TEXT( "%s report:" ), *GetReportName() );

            json_object.SetField( GetReportName(), GenerateMetricsReport() );

            UE_LOG( LogMapMetricsGeneration, Log, TEXT( "------------------------------" ) );
            UE_LOG( LogMapMetricsGeneration, Log, TEXT( "" ) );
        }

    protected:
        virtual FString GetReportName() const = 0;
        virtual TSharedRef< FJsonValue > GenerateMetricsReport() = 0;
    };

    struct FLightMetrics final : public FMetrics
    {
        void ProcessActor( AActor * actor ) override
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
        }

    protected:
        FString GetReportName() const override
        {
            return "Lights";
        }

        TSharedRef< FJsonValue > GenerateMetricsReport() override
        {
            TSharedRef< FJsonObject > report_json = MakeShareable( new FJsonObject() );
            report_json->SetNumberField( "StaticLightCount", StaticLightCount );
            report_json->SetNumberField( "StationaryLightCount", StationaryLightCount );
            report_json->SetNumberField( "MoveableLightCount", MoveableLightCount );

            return MakeShareable( new FJsonValueObject( report_json ) );
        }

    private:
        int StaticLightCount = 0;
        int StationaryLightCount = 0;
        int MoveableLightCount = 0;
        TMap< FString, int > StaticLightComponentsMap, StationaryLightComponentsMap, MoveableLightComponentsMap;
    };

    struct FStaticMeshMetrics final : public FMetrics
    {
        void ProcessActor( AActor * actor ) override
        {
            TArray< UStaticMeshComponent * > sm_components;
            actor->GetComponents< UStaticMeshComponent >( sm_components );

            for ( auto * sm_component : sm_components )
            {
                if ( sm_component->GetStaticMesh()->GetNumLODs() == 1 )
                {
                    WithoutLODsCount++;
                }
                else
                {
                    WithLODsCount++;
                }

                MaterialCountMap.FindOrAdd( sm_component->GetNumMaterials() )++;
            }
        }

    protected:
        FString GetReportName() const override
        {
            return "StaticMeshes";
        }

        TSharedRef< FJsonValue > GenerateMetricsReport() override
        {
            TSharedRef< FJsonObject > report_json = MakeShareable( new FJsonObject() );
            report_json->SetNumberField( "WithLODsCount", WithLODsCount );
            report_json->SetNumberField( "WithoutLODsCount", WithoutLODsCount );

            TSharedRef< FJsonObject > material_count_report = MakeShareable( new FJsonObject() );

            for ( const auto & pair : MaterialCountMap )
            {
                material_count_report->SetNumberField( FString::Printf( TEXT( "%i_Materials" ), pair.Key ), pair.Value );
            }

            report_json->SetObjectField( "ByMaterialCount", material_count_report );

            return MakeShareable( new FJsonValueObject( report_json ) );
        }

    private:
        int WithLODsCount = 0;
        int WithoutLODsCount = 0;
        TMap< int, int > MaterialCountMap;
    };

    struct FSkeletalMeshMetrics final : FMetrics
    {
        void ProcessActor( AActor * actor ) override
        {
            TArray< USkeletalMeshComponent * > skeletal_components;
            actor->GetComponents< USkeletalMeshComponent >( skeletal_components );

            for ( auto * skeletal_component : skeletal_components )
            {
                if ( skeletal_component->GetNumLODs() == 1 )
                {
                    WithoutLODsCount++;
                }
                else
                {
                    WithLODsCount++;
                }

                MaterialCountMap.FindOrAdd( skeletal_component->GetNumMaterials() )++;
            }
        }

    protected:
        FString GetReportName() const override
        {
            return "SkeletalMeshes";
        }

        TSharedRef< FJsonValue > GenerateMetricsReport() override
        {
            TSharedRef< FJsonObject > report_json = MakeShareable( new FJsonObject() );
            report_json->SetNumberField( "WithLODsCount", WithLODsCount );
            report_json->SetNumberField( "WithoutLODsCount", WithoutLODsCount );

            TSharedRef< FJsonObject > material_count_report = MakeShareable( new FJsonObject() );

            for ( const auto & pair : MaterialCountMap )
            {
                material_count_report->SetNumberField( FString::Printf( TEXT( "%i_Materials" ), pair.Key ), pair.Value );
            }

            report_json->SetObjectField( "ByMaterialCount", material_count_report );

            return MakeShareable( new FJsonValueObject( report_json ) );
        }

    private:
        int WithLODsCount = 0;
        int WithoutLODsCount = 0;
        TMap< int, int > MaterialCountMap;
    };

    struct FActorMetrics final : FMetrics
    {
        void ProcessActor( AActor * actor ) override
        {
            ActorCount++;
            ActorMap.FindOrAdd( actor->GetClass() )++;
        }

    private:
        FString GetReportName() const override
        {
            return "Actors";
        }

        TSharedRef< FJsonValue > GenerateMetricsReport() override
        {
            TSharedRef< FJsonObject > report_json = MakeShareable( new FJsonObject() );
            report_json->SetNumberField( "ActorCount", ActorCount );

            TSharedRef< FJsonObject > actor_type_count_report = MakeShareable( new FJsonObject() );

            for ( const auto & pair : ActorMap )
            {
                actor_type_count_report->SetNumberField( *pair.Key->GetName(), pair.Value );
            }

            report_json->SetObjectField( "ByClass", actor_type_count_report );

            return MakeShareable( new FJsonValueObject( report_json ) );
        }

        int ActorCount = 0;
        TMap< UClass *, int > ActorMap;
    };

    struct FNiagaraMetrics final : FMetrics
    {
        void ProcessActor( AActor * actor ) override
        {
            TArray< UNiagaraComponent * > niagara_components;
            actor->GetComponents< UNiagaraComponent >( niagara_components );

            for ( auto * niagara_component : niagara_components )
            {
                if ( auto * asset = niagara_component->GetAsset() )
                {
                    if ( asset->HasAnyGPUEmitters() )
                    {
                        WithGPUEmitterCount++;
                    }
                    else
                    {
                        WithoutGPUEmitterCount++;
                    }

                    EmitterNumMap.FindOrAdd( asset->GetNumEmitters() )++;
                }
                else
                {
                    WithoutAssetCount++;
                }
            }
        }

    private:
        FString GetReportName() const override
        {
            return "Niagara";
        }

        TSharedRef< FJsonValue > GenerateMetricsReport() override
        {
            TSharedRef< FJsonObject > report_json = MakeShareable( new FJsonObject() );
            report_json->SetNumberField( "WithoutAssetCount", WithoutAssetCount );
            report_json->SetNumberField( "WithoutGPUEmitterCount", WithoutGPUEmitterCount );
            report_json->SetNumberField( "WithGPUEmitterCount", WithGPUEmitterCount );

            TSharedRef< FJsonObject > emitter_count_report = MakeShareable( new FJsonObject() );

            for ( const auto & pair : EmitterNumMap )
            {
                emitter_count_report->SetNumberField( FString::Printf( TEXT( "%i_Emitters" ), pair.Key ), pair.Value );
            }

            report_json->SetObjectField( "ByMaterialCount", emitter_count_report );

            return MakeShareable( new FJsonValueObject( report_json ) );
        }

        int WithoutAssetCount = 0;
        int WithoutGPUEmitterCount = 0;
        int WithGPUEmitterCount = 0;
        TMap< int, int > EmitterNumMap;
    };
}

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

    FString output_folder( TEXT( "MapMetrics" ) );

    FParse::Value( *params, TEXT( "-OUTPUT_FOLDER=" ), output_folder );

    TArray< FString > package_names;

    for ( const auto & param_key_pair : params_map )
    {
        if ( param_key_pair.Key == "Maps" )
        {
            const auto map_parameter_value = param_key_pair.Value;

            const auto add_package = [ &package_names ]( const FString & package_name ) {
                FString map_file;
                FPackageName::SearchForPackageOnDisk( package_name, nullptr, &map_file );

                if ( map_file.IsEmpty() )
                {
                    UE_LOG( LogMapMetricsGeneration, Error, TEXT( "Could not find package %s" ), *package_name );
                }
                else
                {
                    package_names.Add( *map_file );
                }
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
        FLevelLoader level_loader( package_name );

        auto * world = level_loader.GetWorld();

        if ( world == nullptr )
        {
            return 2;
        }

        TSharedPtr< FJsonObject > json_object = MakeShareable< FJsonObject >( new FJsonObject );

        TArray< AActor * > all_actors;
        UGameplayStatics::GetAllActorsOfClass( world, AActor::StaticClass(), all_actors );

        TArray< TSharedPtr< FMetrics > > all_metrics;

        all_metrics.Emplace( MakeShared< FLightMetrics >() );
        all_metrics.Emplace( MakeShared< FStaticMeshMetrics >() );
        all_metrics.Emplace( MakeShared< FSkeletalMeshMetrics >() );
        all_metrics.Emplace( MakeShared< FActorMetrics >() );
        all_metrics.Emplace( MakeShared< FNiagaraMetrics >() );

        for ( auto * actor : all_actors )
        {
            for ( const auto & metrics : all_metrics )
            {
                metrics->ProcessActor( actor );
            }
        }

        for ( const auto & metrics : all_metrics )
        {
            metrics->GenerateReport( *json_object );
        }

        FString output_file_path = FPaths::ProjectSavedDir() / output_folder / FPaths::GetBaseFilename( package_name ) + TEXT( ".json" );
        if ( FArchive * archive = IFileManager::Get().CreateFileWriter( *output_file_path ) )
        {
            auto writer = TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create( archive, 0 );
            FJsonSerializer::Serialize( json_object.ToSharedRef(), writer );

            delete archive;
        }

        {
            FString output_string;
            auto writer = TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create( &output_string );
            FJsonSerializer::Serialize( json_object.ToSharedRef(), writer );

            UE_LOG( LogMapMetricsGeneration, Log, TEXT( "%s" ), *output_string );
        }

        UE_LOG( LogMapMetricsGeneration, Log, TEXT( "Finished processing of %s" ), *package_name );
    }

    UE_LOG( LogMapMetricsGeneration, Log, TEXT( "Successfully finished running MapMetricsGeneration Commandlet" ) );
    UE_LOG( LogMapMetricsGeneration, Log, TEXT( "--------------------------------------------------------------------------------------------" ) );
    return 0;
}
