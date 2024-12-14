// Copyright Epic Games, Inc. All Rights Reserved.
#include "SoundControlBusProxy.h"

#include "AudioDevice.h"
#include "AudioDeviceManager.h"
#include "AudioMixerTrace.h"
#include "AudioModulation.h"
#include "AudioModulationLogging.h"
#include "AudioModulationSystem.h"
#include "Engine/World.h"
#include "SoundModulationGenerator.h"

#if UE_AUDIO_PROFILERTRACE_ENABLED
UE_TRACE_EVENT_BEGIN(Audio, ControlBusActivate)
	UE_TRACE_EVENT_FIELD(uint32, DeviceId)
	UE_TRACE_EVENT_FIELD(uint32, ControlBusId)
	UE_TRACE_EVENT_FIELD(double, Timestamp)
	UE_TRACE_EVENT_FIELD(UE::Trace::WideString, Name)
	UE_TRACE_EVENT_FIELD(UE::Trace::WideString, ParamName)
UE_TRACE_EVENT_END()
#endif // UE_AUDIO_PROFILERTRACE_ENABLED

namespace AudioModulation
{
	const FBusId InvalidBusId = INDEX_NONE;

	Audio::FModulatorTypeId FControlBusSettings::Register(Audio::FModulatorHandleId HandleId, IAudioModulationManager& InModulation) const
	{
		FAudioModulationSystem& ModSystem = static_cast<FAudioModulationManager&>(InModulation).GetSystem();

#if UE_AUDIO_PROFILERTRACE_ENABLED
		UE_TRACE_LOG(Audio, ControlBusActivate, AudioChannel)
			<< ControlBusActivate.DeviceId(static_cast<uint32>(ModSystem.GetAudioDeviceId()))
			<< ControlBusActivate.ControlBusId(static_cast<uint32>(GetId()))
			<< ControlBusActivate.Timestamp(FPlatformTime::Cycles64())
			<< ControlBusActivate.Name(*GetName().ToString())
			<< ControlBusActivate.ParamName(*OutputParameter.ParameterName.ToString());
#endif

		return ModSystem.RegisterModulator(HandleId, *this);
	}

	FControlBusProxy::FControlBusProxy()
		: DefaultValue(0.0f)
		, GeneratorValue(1.0f)
		, MixValue(NAN)
		, bBypass(false)
	{
	}

	FControlBusProxy::FControlBusProxy(FControlBusSettings&& InSettings, FAudioModulationSystem& InModSystem)
		: TModulatorProxyRefType(InSettings.GetName(), InSettings.GetId(), InModSystem)
	{
		Init(MoveTemp(InSettings));
	}

	FControlBusProxy& FControlBusProxy::operator =(FControlBusSettings&& InSettings)
	{
		Init(MoveTemp(InSettings));
		return *this;
	}

	float FControlBusProxy::GetDefaultValue() const
	{
		return DefaultValue;
	}

	const TArray<FGeneratorHandle>& FControlBusProxy::GetGeneratorHandles() const
	{
		return GeneratorHandles;
	}

	float FControlBusProxy::GetGeneratorValue() const
	{
		return GeneratorValue;
	}

	float FControlBusProxy::GetMixValue() const
	{
		return MixValue;
	}

	float FControlBusProxy::GetValue() const
	{
		const float DefaultMixed = Mix(DefaultValue);
		return FMath::Clamp(DefaultMixed * GeneratorValue, 0.0f, 1.0f);
	}

	FName FControlBusProxy::GetParameterName() const
	{
#if UE_BUILD_SHIPPING
		static FName ParameterName;
#endif // !UE_BUILD_SHIPPING

		return ParameterName;
	}

	void FControlBusProxy::Init(FControlBusSettings&& InSettings)
	{
		check(ModSystem);

		GeneratorValue = 1.0f;
		MixValue = NAN;
		MixFunction = MoveTemp(InSettings.MixFunction);

#if !UE_BUILD_SHIPPING
		ParameterName = InSettings.OutputParameter.ParameterName;
#endif // !UE_BUILD_SHIPPING 

		DefaultValue = FMath::Clamp(InSettings.DefaultValue, 0.0f, 1.0f);
		bBypass = InSettings.bBypass;

		TArray<FGeneratorHandle> NewHandles;
		for (FModulationGeneratorSettings& GeneratorSettings : InSettings.GeneratorSettings)
		{
			NewHandles.Add(FGeneratorHandle::Create(MoveTemp(GeneratorSettings), ModSystem->RefProxies.Generators, *ModSystem));
		}

		// Move vs. reset and adding to original array to avoid potentially clearing handles (and thus current Generator state)
		// and destroying generators if function is called while reinitializing/updating the modulator
		GeneratorHandles = MoveTemp(NewHandles);
	}

	bool FControlBusProxy::IsBypassed() const
	{
		return bBypass;
	}

	float FControlBusProxy::Mix(float ValueA) const
	{
		// If mix value is NaN, it is uninitialized (effectively, the parent bus is inactive)
		// and therefore not mixable, so just return the second value.
		if (FMath::IsNaN(MixValue))
		{
			return ValueA;
		}

		float OutValue = MixValue;
		MixFunction(OutValue, ValueA);
		return OutValue;
	}

	void FControlBusProxy::MixIn(const float InValue)
	{
		MixValue = Mix(InValue);
	}

	void FControlBusProxy::MixGenerators()
	{
		for (const FGeneratorHandle& Handle: GeneratorHandles)
		{
			if (Handle.IsValid())
			{
				const FModulatorGeneratorProxy& GeneratorProxy = Handle.FindProxy();
				if (!GeneratorProxy.IsBypassed())
				{
					GeneratorValue *= GeneratorProxy.GetValue();
				}
			}
		}
	}

	void FControlBusProxy::Reset()
	{
		GeneratorValue = 1.0f;
		MixValue = NAN;
	}
} // namespace AudioModulation
