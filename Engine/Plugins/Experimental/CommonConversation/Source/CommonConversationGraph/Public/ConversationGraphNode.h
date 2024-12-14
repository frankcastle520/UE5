// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AIGraphNode.h"
#include "ConversationGraphNode.generated.h"

namespace ENodeTitleType { enum Type : int; }

class UConversationGraphNode_Knot;

UCLASS()
class COMMONCONVERSATIONGRAPH_API UConversationGraphNode : public UAIGraphNode
{
	GENERATED_UCLASS_BODY()

public:

	//~ Begin UEdGraphNode Interface
	virtual bool CanCreateUnderSpecifiedSchema(const UEdGraphSchema* DesiredSchema) const override;
	virtual void FindDiffs(UEdGraphNode* OtherNode, FDiffResults& Results) override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FLinearColor GetNodeBodyTintColor() const override;
	virtual UObject* GetJumpTargetForDoubleClick() const override;
	virtual bool CanJumpToDefinition() const override;
	virtual void JumpToDefinition() const override;
	virtual TSharedPtr<SGraphNode> CreateVisualWidget() override;
	//~ End UEdGraphNode Interface

	virtual FText GetDescription() const override;

	/** check if node can accept breakpoints */
	virtual bool CanPlaceBreakpoints() const { return false; }

	/** gets icon resource name for title bar */
	virtual FName GetNameIcon() const;

	bool IsOutBoundConnectionAllowed(const UConversationGraphNode* OtherNode, FText& OutErrorMessage) const;
	bool IsOutBoundConnectionAllowed(const UConversationGraphNode_Knot* KnotNode, FText& OutErrorMessage) const;

	template<class T>
	T* GetRuntimeNode() const
	{
		return Cast<T>(NodeInstance);
	}

protected:
	void RequestRebuildConversation();
};
