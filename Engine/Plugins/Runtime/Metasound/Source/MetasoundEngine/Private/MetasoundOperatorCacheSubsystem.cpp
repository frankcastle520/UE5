// Copyright Epic Games, Inc. All Rights Reserved.
#include "MetasoundOperatorCacheSubsystem.h"

#include "AudioMixerDevice.h"
#include "MetasoundGenerator.h"
#include "MetasoundGeneratorModule.h"
#include "MetasoundOperatorCache.h"
#include "Modules/ModuleManager.h"
#include "Misc/Optional.h"

namespace Metasound::OperatorCachePrivate
{
	static bool bOperatorPrecacheEnabled = true;
	static FAutoConsoleVariableRef CVarOperatorPrecacheEnabled(
		TEXT("au.MetaSound.OperatorCache.EnablePrecache"),
		bOperatorPrecacheEnabled,
		TEXT("If precaching metasound operators via the UMetaSoundCacheSubsystem is enabled.")
	);

	TOptional<FMetasoundGeneratorInitParams> CreateInitParams(UMetaSoundSource& InMetaSound, const FSoundGeneratorInitParams& InParams)
	{
		using namespace Metasound::Frontend;
		using namespace Metasound::Engine;
		using namespace Metasound::SourcePrivate;

		// Dynamic MetaSounds cannot be precached
		if (InMetaSound.IsDynamic())
		{
			return { };
		}

		FOperatorSettings InSettings = InMetaSound.GetOperatorSettings(static_cast<FSampleRate>(InParams.SampleRate));
		FMetasoundEnvironment Environment = InMetaSound.CreateEnvironment(InParams);

		FOperatorBuilderSettings BuilderSettings = FOperatorBuilderSettings::GetDefaultSettings();
		// Graph analyzer currently only enabled for preview sounds (but can theoretically be supported for all sounds)
		BuilderSettings.bPopulateInternalDataReferences = InParams.bIsPreviewSound;

		return FMetasoundGeneratorInitParams
		{
			InSettings,
			MoveTemp(BuilderSettings),
			{}, // Graph, retrieved from the FrontEnd Registry in FOperatorPool::BuildAndAddOperator()
			Environment,
			InMetaSound.GetName(),
			InMetaSound.GetOutputAudioChannelOrder(),
			{}, // DefaultParameters
			true, // bBuildSynchronous
			{} // DataChannel
		};
	}
} // namespace Metasound::OperatorCachePrivate

bool UMetaSoundCacheSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	return !IsRunningDedicatedServer();
}

void UMetaSoundCacheSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	using namespace Audio;
	using namespace Metasound;

	const FMixerDevice* MixerDevice = GetMixerDevice();

	if (ensure(MixerDevice))
	{
		BuildParams.AudioDeviceID = GetAudioDeviceHandle().GetDeviceID();
		BuildParams.SampleRate = MixerDevice->GetSampleRate();
		BuildParams.AudioMixerNumOutputFrames = MixerDevice->GetNumOutputFrames();
		BuildParams.NumChannels = MixerDevice->GetNumDeviceChannels();
		BuildParams.NumFramesPerCallback = 0;
		BuildParams.InstanceID = 0;
	}
}

void UMetaSoundCacheSubsystem::Update()
{
#if METASOUND_OPERATORCACHEPROFILER_ENABLED
	using namespace Metasound;
	if (TSharedPtr<FOperatorPool> OperatorPool = FModuleManager::GetModuleChecked<IMetasoundGeneratorModule>("MetasoundGenerator").GetOperatorPool())
	{
		OperatorPool->UpdateHitRateTracker();
	}
#endif // #if METASOUND_OPERATORCACHEPROFILER_ENABLED
}

void UMetaSoundCacheSubsystem::PrecacheMetaSoundInternal(UMetaSoundSource* InMetaSound, int32 InNumInstances, bool bTouchExisting)
{
	using namespace Audio;
	using namespace Metasound;

	if (!OperatorCachePrivate::bOperatorPrecacheEnabled)
	{
		UE_LOG(LogMetaSound, Log, TEXT("Ignoring PrecacheMetaSound request since au.MetaSound.OperatorCache.EnablePrecache is false."));
		return;
	}

	IMetasoundGeneratorModule* Module = FModuleManager::GetModulePtr<IMetasoundGeneratorModule>("MetasoundGenerator");
	if (!ensure(Module))
	{
		return;
	}

	TSharedPtr<FOperatorPool> OperatorPool = Module->GetOperatorPool();
	if (!ensure(OperatorPool))
	{
		return;
	}

	if (!InMetaSound)
	{
		UE_LOG(LogMetaSound, Error, TEXT("PrecacheMetaSound called without being provided a MetaSound, ignoring request"));
		return;
	}

	if (InNumInstances < 1)
	{
		UE_LOG(LogMetaSound, Error, TEXT("PrecacheMetaSound called with invalid NumInstances %i, ignoring request"), InNumInstances);
		return;
	}

	InMetaSound->InitResources();

	BuildParams.GraphName = InMetaSound->GetOwningAssetName();
	TOptional<FMetasoundGeneratorInitParams> InitParams = OperatorCachePrivate::CreateInitParams(*InMetaSound, BuildParams);
	if (!InitParams.IsSet())
	{
		return;
	}

	// Graph inflation may interact with cache. Need to find the same graph registry key
	// that is found when a MetaSound generator is created. 
	const UMetaSoundSource&	NoninflatableSource = InMetaSound->FindFirstNoninflatableSource(InitParams->Environment, [](const UMetaSoundSource&){});

	TUniquePtr<FOperatorBuildData> Data = MakeUnique<FOperatorBuildData>(
		  MoveTemp(InitParams.GetValue())
		, NoninflatableSource.GetGraphRegistryKey()
		, InMetaSound->AssetClassID
		, InNumInstances
		, bTouchExisting
	);

	OperatorPool->BuildAndAddOperator(MoveTemp(Data));
}

void UMetaSoundCacheSubsystem::PrecacheMetaSound(UMetaSoundSource* InMetaSound, int32 InNumInstances)
{
	constexpr bool bTouchExisting = false;
	PrecacheMetaSoundInternal(InMetaSound, InNumInstances, bTouchExisting);
}

void UMetaSoundCacheSubsystem::TouchOrPrecacheMetaSound(UMetaSoundSource* InMetaSound, int32 InNumInstances)
{
	constexpr bool bTouchExisting = true;
	PrecacheMetaSoundInternal(InMetaSound, InNumInstances, bTouchExisting);
}

void UMetaSoundCacheSubsystem::RemoveCachedOperatorsForMetaSound(UMetaSoundSource* InMetaSound)
{
	using namespace Metasound;

	// Note: we're not checking the bOperatorPrecacheEnabled cvar here in case it was disable after some sounds had already been cached.
	// If nothing is cached this will do very little.

	if (!InMetaSound)
	{
		UE_LOG(LogMetaSound, Warning, TEXT("Remove Cached Operators called without being provided a MetaSound, ignoring request"));
		return;
	}

	IMetasoundGeneratorModule* Module = FModuleManager::GetModulePtr<IMetasoundGeneratorModule>("MetasoundGenerator");
	if (!ensure(Module))
	{
		return;
	}

	if (TSharedPtr<FOperatorPool> OperatorPool = Module->GetOperatorPool())
	{
		OperatorPool->RemoveOperatorsWithAssetClassID(InMetaSound->AssetClassID);
	}
}
