// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "AudioAnalyzer.h"
#include "AudioSynesthesia.generated.h"

/** UAudioSynesthesiaSettings
 *
 * Defines asset actions for derived UAudioSynthesiaSettings subclasses.
 */
UCLASS(Abstract, Blueprintable, MinimalAPI)
class UAudioSynesthesiaSettings : public UAudioAnalyzerSettings
{
	GENERATED_BODY()

public:

#if WITH_EDITOR
	AUDIOSYNESTHESIA_API const TArray<FText>& GetAssetActionSubmenus() const;
	AUDIOSYNESTHESIA_API FColor GetTypeColor() const override;
#endif
};