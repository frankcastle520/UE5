// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "LiveLinkSubjectSettings.h"

#include "LiveLinkHubSubjectSettings.generated.h"

class FLiveLinkHubClient;
class ULiveLinkRole;

/** Settings object for a livelinkhub subject. */
UCLASS()
class ULiveLinkHubSubjectSettings : public ULiveLinkSubjectSettings
{
	GENERATED_BODY()
public:

	//~ Begin ULiveLinkSubjectSettings interface
	virtual void Initialize(FLiveLinkSubjectKey InSubjectKey) override;

	virtual FName GetRebroadcastName() const override
	{
		return *OutboundName;
	}
	//~ End ULiveLinkSubjectSettings interface

	//~ Begin UObject interface
	virtual void PreEditChange(FProperty* PropertyAboutToChange) override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	//~ End UObject interface

public:
	/** Name of this subject. */
	UPROPERTY(VisibleAnywhere, Category = "LiveLink")
	FString SubjectName;

	/** Name override that will be transmitted to clients instead of the subject name. */
	UPROPERTY(EditAnywhere, Category = "LiveLink")
	FString OutboundName;

	/** Source that contains the subject. */
	UPROPERTY(VisibleAnywhere, Category = "LiveLink")
	FString Source;

	/** Proxy property used edit the translators. */
	UPROPERTY(EditAnywhere, Instanced, Category = "LiveLink", DisplayName = "Translator")
	TObjectPtr<ULiveLinkFrameTranslator> TranslatorsProxy;

private:
	/* Previous outbound name to be used for noticing clients to remove this entry from their subject list. */
	FName PreviousOutboundName;

	struct FNameChangeInfo
	{
		UClass* SubjectRole = nullptr;
		FLiveLinkStaticDataStruct LastStaticData;
	};

	friend class FLiveLinkHubSubjectSettingsDetailCustomization;
};
