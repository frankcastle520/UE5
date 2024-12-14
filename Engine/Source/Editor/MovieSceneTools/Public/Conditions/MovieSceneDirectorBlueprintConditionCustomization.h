// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MovieSceneDirectorBlueprintEndpointCustomization.h"
#include "IPropertyTypeCustomization.h"
#include "Types/SlateEnums.h"

class UK2Node;
class UBlueprint;
class UEdGraphPin;
class IPropertyHandle;
class FDetailWidgetRow;
class SWidget;

struct FMovieSceneDirectorBlueprintConditionData;

enum class ECheckBoxState : uint8;

/**
 * Customization for director blueprint condition endpoint picker
 */
class MOVIESCENETOOLS_API FMovieSceneDirectorBlueprintConditionCustomization : public FMovieSceneDirectorBlueprintEndpointCustomization
{
public:

	static TSharedRef<IPropertyTypeCustomization> MakeInstance();
	static TSharedRef<IPropertyTypeCustomization> MakeInstance(UMovieScene* InMovieScene);
	static TSharedRef<FMovieSceneDirectorBlueprintConditionCustomization> MakeInstance(UMovieScene* InMovieScene, TSharedPtr<IPropertyHandle> InPropertyHandle, TSharedPtr<IPropertyUtilities> InPropertyUtilities);

protected:

	virtual void GetPayloadVariables(UObject* EditObject, void* RawData, FPayloadVariableMap& OutPayloadVariables) const override;
	virtual bool SetPayloadVariable(UObject* EditObject, void* RawData, FName FieldName, const FMovieSceneDirectorBlueprintVariableValue& NewVariableValue) override;
	virtual UK2Node* FindEndpoint(UMovieSceneSequence* Sequence, UBlueprint* Blueprint, UObject* EditObject, void* RawData) const override;
	virtual void GetWellKnownParameterPinNames(UObject* EditObject, void* RawData, TArray<FName>& OutWellKnownParameters) const override;
	virtual void GetWellKnownParameterCandidates(UK2Node* Endpoint, TArray<FWellKnownParameterCandidates>& OutCandidates) const override;
	virtual bool SetWellKnownParameterPinName(UObject* EditObject, void* RawData, int32 ParameterIndex, FName BoundPinName) override;
	virtual FMovieSceneDirectorBlueprintEndpointDefinition GenerateEndpointDefinition(UMovieSceneSequence* Sequence) override;
	virtual void OnCreateEndpoint(UMovieSceneSequence* Sequence, UBlueprint* Blueprint, const TArray<UObject*> EditObjects, const TArray<void*> RawData, const FMovieSceneDirectorBlueprintEndpointDefinition& EndpointDefinition, UK2Node* NewEndpoint) override;
	virtual void OnSetEndpoint(UMovieSceneSequence* Sequence, UBlueprint* Blueprint, const TArray<UObject*> EditObjects, const TArray<void*> RawData, const FMovieSceneDirectorBlueprintEndpointDefinition& EndpointDefinition, UK2Node* NewEndpoint) override;
	virtual void GetEditObjects(TArray<UObject*>& OutObjects) const override;
	virtual void OnCollectQuickBindActions(UBlueprint* Blueprint, FBlueprintActionMenuBuilder& MenuBuilder) override;
	virtual bool CreateNewCategoryForPayloadVariables() const override { return false; }
private:

	void SetEndpointImpl(UMovieScene* MovieScene, FMovieSceneDirectorBlueprintConditionData* DynamicBinding, UBlueprint* Blueprint, UK2Node* NewEndpoint);
	void EnsureBlueprintExtensionCreated(UMovieScene* MovieScene, UBlueprint* Blueprint);

	void CollectConditionBindActions(UBlueprint* Blueprint, FBlueprintActionMenuBuilder& MenuBuilder, bool bIsRebinding);

private:

	UMovieScene* EditedMovieScene;
};

