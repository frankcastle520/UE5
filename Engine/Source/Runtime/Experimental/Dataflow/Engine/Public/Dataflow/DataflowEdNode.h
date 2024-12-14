// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dataflow/DataflowCore.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "GeometryCollection/GeometryCollection.h"
#include "Internationalization/Text.h"
#include "Misc/Build.h"
#include "Misc/Guid.h"
#include "Templates/SharedPointer.h"
#include "UObject/ObjectMacros.h"
#include "UObject/UObjectGlobals.h"

#include "DataflowEdNode.generated.h"

class FArchive;
class UEdGraphPin;
class UObject;
namespace UE::Dataflow { class FGraph; class FRenderingParameters; class IDataflowConstructionViewMode;  }
namespace GeometryCollection::Facades { class FRenderingFacade; }

UCLASS(MinimalAPI)
class UDataflowEdNode : public UEdGraphNode
{
	GENERATED_UCLASS_BODY()

	FGuid DataflowNodeGuid;
	TSharedPtr<UE::Dataflow::FGraph> DataflowGraph;

public:

	// UEdGraphNode interface
	DATAFLOWENGINE_API virtual void AllocateDefaultPins();
	DATAFLOWENGINE_API virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const;
#if WITH_EDITOR && !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	DATAFLOWENGINE_API virtual void PinConnectionListChanged(UEdGraphPin* Pin) override;
#endif // WITH_EDITOR && !UE_BUILD_SHIPPING
#if WITH_EDITOR
	DATAFLOWENGINE_API virtual FSlateIcon GetIconAndTint(FLinearColor& OutColor) const override;
	DATAFLOWENGINE_API virtual bool ShowPaletteIconOnNode() const override;
	DATAFLOWENGINE_API virtual FLinearColor GetNodeTitleColor() const override;
	DATAFLOWENGINE_API virtual FLinearColor GetNodeBodyTintColor() const override;
	DATAFLOWENGINE_API virtual FText GetTooltipText() const override;
	DATAFLOWENGINE_API virtual void GetPinHoverText(const UEdGraphPin& Pin, FString& HoverTextOut) const override;
	DATAFLOWENGINE_API virtual FText GetPinDisplayName(const UEdGraphPin* Pin) const override;
	DATAFLOWENGINE_API virtual void AutowireNewNode(UEdGraphPin* FromPin) override;
	DATAFLOWENGINE_API virtual void OnPinRemoved(UEdGraphPin* InRemovedPin) override;
	DATAFLOWENGINE_API virtual bool ShouldDrawNodeAsControlPointOnly(int32& OutInputPinIndex, int32& OutOutputPinIndex) const override;
	DATAFLOWENGINE_API virtual void PostEditUndo() override;
#endif // WITH_EDITOR
	// End of UEdGraphNode interface

	// UObject interface
	DATAFLOWENGINE_API void Serialize(FArchive& Ar);
	// End UObject interface

	bool IsBound() const { return DataflowGraph && DataflowNodeGuid.IsValid(); }

	TSharedPtr<UE::Dataflow::FGraph> GetDataflowGraph() { return DataflowGraph; }
	TSharedPtr<const UE::Dataflow::FGraph> GetDataflowGraph() const { return DataflowGraph; }
	void SetDataflowGraph(TSharedPtr<UE::Dataflow::FGraph> InDataflowGraph) { DataflowGraph = InDataflowGraph; }

	DATAFLOWENGINE_API void UpdatePinsFromDataflowNode();

	FGuid GetDataflowNodeGuid() const { return DataflowNodeGuid; }
	void SetDataflowNodeGuid(FGuid InGuid) { DataflowNodeGuid = InGuid; }

	DATAFLOWENGINE_API TSharedPtr<FDataflowNode> GetDataflowNode();
	DATAFLOWENGINE_API TSharedPtr<const FDataflowNode> GetDataflowNode() const;

	/** Add a new option pin if the underlying Dataflow node AddPin member is overriden. */
	DATAFLOWENGINE_API void AddOptionPin();
	/** Remove an option pin if the underlying Dataflow node RemovePin member is overriden. */
	DATAFLOWENGINE_API void RemoveOptionPin();

	DATAFLOWENGINE_API bool PinIsCompatibleWithType(const UEdGraphPin& Pin, const FEdGraphPinType& PinType) const;

#if WITH_EDITOR
	// Pin hiding
	DATAFLOWENGINE_API void HideAllInputPins();
	DATAFLOWENGINE_API void ShowAllInputPins();
	DATAFLOWENGINE_API void ToggleHideInputPin(FName PinName);
	DATAFLOWENGINE_API bool CanToggleHideInputPin(FName PinName) const;
	DATAFLOWENGINE_API bool IsInputPinShown(FName PinName) const;
#endif

	//
	// Node Rendering
	//
	DATAFLOWENGINE_API void SetShouldRenderNode(bool bInRender);
	bool ShouldRenderNode() const { return bRenderInAssetEditor; }

	DATAFLOWENGINE_API void SetShouldWireframeRenderNode(bool bInRender);
	bool ShouldWireframeRenderNode() const { return bRenderWireframeInAssetEditor; }

	DATAFLOWENGINE_API void SetCanEnableWireframeRenderNode(bool bInCanEnable);
	DATAFLOWENGINE_API bool CanEnableWireframeRenderNode() const;

	DATAFLOWENGINE_API TArray<UE::Dataflow::FRenderingParameter> GetRenderParameters() const;

private:

	UPROPERTY(Transient)
	bool bRenderInAssetEditor = false;

	UPROPERTY(Transient)
	bool bRenderWireframeInAssetEditor = false;

	UPROPERTY(Transient)
	bool bCanEnableRenderWireframe = true;

	void OnPinConnection(const UEdGraphPin& Pin, const FEdGraphPinType& Type);
	void OnPinDisconnection(const UEdGraphPin& Pin);
	void SetAnyTypePinsToType(const FEdGraphPinType& Type);

};

