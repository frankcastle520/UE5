// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "StateTreeConsiderationBase.h"
#include "StateTreeNodeBlueprintBase.h"
#include "Templates/SubclassOf.h"
#include "StateTreeConsiderationBlueprintBase.generated.h"

struct FStateTreeExecutionContext;

/*
 * Base class for Blueprint based Considerations.
 */
UCLASS(Abstract, Blueprintable)
class STATETREEMODULE_API UStateTreeConsiderationBlueprintBase : public UStateTreeNodeBlueprintBase
{
	GENERATED_BODY()

public:
	UStateTreeConsiderationBlueprintBase(const FObjectInitializer& ObjectInitializer);

	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "GetScore"))
	float ReceiveGetScore() const;

protected:
	virtual float GetScore(FStateTreeExecutionContext& Context) const;

	friend struct FStateTreeBlueprintConsiderationWrapper;

	uint8 bHasGetScore : 1;
};

/**
 * Wrapper for Blueprint based Considerations.
 */
USTRUCT()
struct STATETREEMODULE_API FStateTreeBlueprintConsiderationWrapper : public FStateTreeConsiderationBase
{
	GENERATED_BODY()

	//~ Begin FStateTreeNodeBase Interface
	virtual const UStruct* GetInstanceDataType() const override { return ConsiderationClass; };
#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting = EStateTreeNodeFormatting::Text) const override;
	virtual FName GetIconName() const override;
	virtual FColor GetIconColor() const override;
#endif //WITH_EDITOR
	//~ End FStateTreeNodeBase Interface

protected:
	//~ Begin FStateTreeConsiderationBase Interface
	virtual float GetScore(FStateTreeExecutionContext& Context) const override;
	//~ End FStateTreeConsiderationBase Interface

public:
	UPROPERTY()
	TSubclassOf<UStateTreeConsiderationBlueprintBase> ConsiderationClass = nullptr;
};
