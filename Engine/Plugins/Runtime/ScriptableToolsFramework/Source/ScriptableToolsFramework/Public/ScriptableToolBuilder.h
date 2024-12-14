// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "InteractiveToolBuilder.h"
#include "ScriptableToolBuilder.generated.h"


namespace ScriptableToolBuilderHelpers
{

}

class UScriptableInteractiveTool;

/**
 * UBaseScriptableToolBuilder is a trivial base UInteractiveToolBuilder for any UScriptableInteractiveTool subclass.
 * CanBuildTool will return true as long as the ToolClass is a valid UClass.
 */
UCLASS()
class SCRIPTABLETOOLSFRAMEWORK_API UBaseScriptableToolBuilder : public UInteractiveToolBuilder
{
	GENERATED_BODY()
public:

	TWeakObjectPtr<UClass> ToolClass;

	virtual bool CanBuildTool(const FToolBuilderState& SceneState) const override;
	virtual UInteractiveTool* BuildTool(const FToolBuilderState& SceneState) const override;
};

UINTERFACE(MinimalAPI, BlueprintType)
class UCustomScriptableToolBuilderBaseInterface : public UInterface
{
	GENERATED_BODY()
};

class ICustomScriptableToolBuilderBaseInterface
{
	GENERATED_BODY()

public:

	virtual bool CanBuildTool(const FToolBuilderState& SceneState) const = 0;
	virtual void SetupTool(const FToolBuilderState& SceneState, UInteractiveTool* Tool) const = 0;
};

UCLASS()
class SCRIPTABLETOOLSFRAMEWORK_API UCustomScriptableToolBuilderComponentBase : public UObject
{
	GENERATED_BODY()

public:

};


UCLASS(Transient, Blueprintable, Hidden)
class SCRIPTABLETOOLSFRAMEWORK_API UCustomScriptableToolBuilderContainer : public UBaseScriptableToolBuilder
{
	GENERATED_BODY()


public:

	void Initialize(TObjectPtr<UCustomScriptableToolBuilderComponentBase> BuilderInstanceIn);

	virtual bool CanBuildTool(const FToolBuilderState& SceneState) const override;
	virtual UInteractiveTool* BuildTool(const FToolBuilderState& SceneState) const override;

private:

	UPROPERTY()
	TObjectPtr<UCustomScriptableToolBuilderComponentBase> BuilderInstance;
};

/*
*
*   Tool Builders for custom builder logic
*
*/

UCLASS(Transient, Blueprintable, Abstract)
class SCRIPTABLETOOLSFRAMEWORK_API UCustomScriptableToolBuilder : public UCustomScriptableToolBuilderComponentBase, public ICustomScriptableToolBuilderBaseInterface
{
	GENERATED_BODY()

public:

	UFUNCTION(BlueprintNativeEvent, Category = "ScriptableToolBuilder|Events")
	bool OnCanBuildTool(const TArray<AActor*>& SelectedActors, const TArray<UActorComponent*>& SelectedComponents) const;

	bool OnCanBuildTool_Implementation(const TArray<AActor*>& SelectedActors, const TArray<UActorComponent*>& SelectedComponents) const;

	UFUNCTION(BlueprintNativeEvent, Category = "ScriptableToolBuilder|Events")
	void OnSetupTool(UScriptableInteractiveTool* Tool, const TArray<AActor*>& SelectedActors, const TArray<UActorComponent*>& SelectedComponents) const;

	void OnSetupTool_Implementation(UScriptableInteractiveTool* Tool, const TArray<AActor*>& SelectedActors, const TArray<UActorComponent*>& SelectedComponents) const;

	virtual bool CanBuildTool(const FToolBuilderState& SceneState) const override;
	virtual void SetupTool(const FToolBuilderState& SceneState, UInteractiveTool* Tool) const override;
};


/*
*
*   Tool Builders for Tool Target support
* 
*/

UCLASS(Blueprintable)
class SCRIPTABLETOOLSFRAMEWORK_API UScriptableToolTargetRequirements : public UObject
{
	GENERATED_BODY()
public:

	UFUNCTION(BlueprintCallable, Category = "ScriptableToolBuilder|ToolTargets")
	static UPARAM(DisplayName = "New Target Requirements") UScriptableToolTargetRequirements*
	BuildToolTargetRequirements(TArray<UClass*> RequirementInterfaces);

	const FToolTargetTypeRequirements& GetRequirements() const { return Requirements; };

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Requirements")
	int MinMatchingTargets = 0;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Requirements")
	int MaxMatchingTargets = 1;

private:

	FToolTargetTypeRequirements Requirements;

};

UCLASS(Transient, Blueprintable, Abstract)
class SCRIPTABLETOOLSFRAMEWORK_API UToolTargetScriptableToolBuilder: public UCustomScriptableToolBuilderComponentBase, public ICustomScriptableToolBuilderBaseInterface
{
	GENERATED_BODY()

public:

	void Initialize();

	UFUNCTION(BlueprintNativeEvent, Category = "Tool Targets")
	UScriptableToolTargetRequirements* GetToolTargetRequirements() const;

	virtual UScriptableToolTargetRequirements* GetToolTargetRequirements_Implementation() const;

	UFUNCTION(BlueprintNativeEvent, Category = "ScriptableToolBuilder|Events")
	void OnSetupTool(UScriptableInteractiveTool* Tool) const;

	void OnSetupTool_Implementation(UScriptableInteractiveTool* Tool) const;

	virtual bool CanBuildTool(const FToolBuilderState& SceneState) const override;
	virtual void SetupTool(const FToolBuilderState& SceneState, UInteractiveTool* Tool) const override;
	
private:

	UPROPERTY()
	TObjectPtr<UScriptableToolTargetRequirements> Requirements;

};