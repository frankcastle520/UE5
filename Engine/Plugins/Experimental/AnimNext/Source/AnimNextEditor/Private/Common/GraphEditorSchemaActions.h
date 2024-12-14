// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraphSchema.h"
#include "Editor/RigVMEditorStyle.h"
#include "RigVMCore/RigVMExternalVariable.h"
#include "RigVMCore/RigVMGraphFunctionDefinition.h"
#include "Styling/AppStyle.h"
#include "GraphEditorSchemaActions.generated.h"

struct FSlateBrush;
class URigVMLibraryNode;

USTRUCT()
struct FAnimNextSchemaAction : public FEdGraphSchemaAction
{
	GENERATED_BODY()

	FAnimNextSchemaAction() = default;
	
	FAnimNextSchemaAction(FText InNodeCategory, FText InMenuDesc, FText InToolTip, FText InKeywords = FText::GetEmpty())
		: FEdGraphSchemaAction(MoveTemp(InNodeCategory), MoveTemp(InMenuDesc), MoveTemp(InToolTip), 0, MoveTemp(InKeywords))
	{
	}

	virtual const FSlateBrush* GetIconBrush() const
	{
		return FRigVMEditorStyle::Get().GetBrush("RigVM.Unit");
	}

	virtual const FLinearColor& GetIconColor() const
	{
		return FLinearColor::White;
	}
};

USTRUCT()
struct FAnimNextSchemaAction_RigUnit : public FAnimNextSchemaAction
{
	GENERATED_BODY()

	FAnimNextSchemaAction_RigUnit() = default;
	
	FAnimNextSchemaAction_RigUnit(UScriptStruct* InStructTemplate, FText InNodeCategory, FText InMenuDesc, FText InToolTip, FText InKeywords = FText::GetEmpty())
		: FAnimNextSchemaAction(InNodeCategory, InMenuDesc, InToolTip, InKeywords)
		, StructTemplate(InStructTemplate)
	{}

	// FEdGraphSchemaAction Interface
	virtual UEdGraphNode* PerformAction(UEdGraph* ParentGraph, TArray<UEdGraphPin*>& FromPins, const FVector2D Location, bool bSelectNewNode = true) override;
	virtual UEdGraphNode* PerformAction(UEdGraph* ParentGraph, UEdGraphPin* FromPin, const FVector2D Location, bool bSelectNewNode = true) { return nullptr; }

private:
	// The script struct for our rig unit
	UScriptStruct* StructTemplate = nullptr;
};


USTRUCT()
struct FAnimNextSchemaAction_DispatchFactory : public FAnimNextSchemaAction
{
	GENERATED_BODY()

	FAnimNextSchemaAction_DispatchFactory() = default;

	FAnimNextSchemaAction_DispatchFactory(FName InNotation, FText InNodeCategory, FText InMenuDesc, FText InToolTip, FText InKeywords = FText::GetEmpty())
		: FAnimNextSchemaAction(InNodeCategory, InMenuDesc, InToolTip, InKeywords)
		, Notation(InNotation)
	{}

	virtual const FSlateBrush* GetIconBrush() const
	{
		return FRigVMEditorStyle::Get().GetBrush("RigVM.Template");
	}
	
	// FEdGraphSchemaAction Interface
	virtual UEdGraphNode* PerformAction(UEdGraph* ParentGraph, TArray<UEdGraphPin*>& FromPins, const FVector2D Location, bool bSelectNewNode = true) override;
	virtual UEdGraphNode* PerformAction(UEdGraph* ParentGraph, UEdGraphPin* FromPin, const FVector2D Location, bool bSelectNewNode = true) { return nullptr; }

private:
	// Notation for dispatch factory
	FName Notation;
};


USTRUCT()
struct FAnimNextSchemaAction_Variable : public FAnimNextSchemaAction
{
	GENERATED_BODY()

	FAnimNextSchemaAction_Variable() = default;

	FAnimNextSchemaAction_Variable(const FRigVMExternalVariable& InExternalVariable, bool bInIsGetter = true);

	// FEdGraphSchemaAction Interface
	virtual UEdGraphNode* PerformAction(UEdGraph* ParentGraph, TArray<UEdGraphPin*>& FromPins, const FVector2D Location, bool bSelectNewNode = true) override;
	virtual UEdGraphNode* PerformAction(UEdGraph* ParentGraph, UEdGraphPin* FromPin, const FVector2D Location, bool bSelectNewNode = true) { return nullptr; }

	virtual const FSlateBrush* GetIconBrush() const override
	{
		return  FAppStyle::GetBrush(TEXT("Kismet.VariableList.TypeIcon"));
	}

	virtual const FLinearColor& GetIconColor() const override
	{
		return VariableColor;
	}

private:
	FRigVMExternalVariable ExternalVariable;
	bool bIsGetter = false;
	FLinearColor VariableColor;
};

USTRUCT()
struct FAnimNextSchemaAction_AddComment : public FAnimNextSchemaAction
{
	GENERATED_BODY()

	FAnimNextSchemaAction_AddComment();

	// FEdGraphSchemaAction Interface
	virtual UEdGraphNode* PerformAction(UEdGraph* ParentGraph, TArray<UEdGraphPin*>& FromPins, const FVector2D Location, bool bSelectNewNode = true) override;
	virtual UEdGraphNode* PerformAction(UEdGraph* ParentGraph, UEdGraphPin* FromPin, const FVector2D Location, bool bSelectNewNode = true) { return nullptr; }

	virtual const FSlateBrush* GetIconBrush() const
	{
		return FAppStyle::Get().GetBrush("Icons.Comment");
	}
};

USTRUCT()
struct FAnimNextSchemaAction_Function : public FAnimNextSchemaAction
{
	GENERATED_BODY()

	FAnimNextSchemaAction_Function() = default;

	FAnimNextSchemaAction_Function(const FRigVMGraphFunctionHeader& InReferencedPublicFunctionHeader, const FText& InNodeCategory, const FText& InMenuDesc, const FText& InToolTip, const FText& InKeywords = FText::GetEmpty());
	FAnimNextSchemaAction_Function(const URigVMLibraryNode* InFunctionLibraryNode, const FText& InNodeCategory, const FText& InMenuDesc, const FText& InToolTip, const FText& InKeywords = FText::GetEmpty());

	virtual const FSlateBrush* GetIconBrush() const;
	
	// FEdGraphSchemaAction Interface
	virtual UEdGraphNode* PerformAction(UEdGraph* ParentGraph, TArray<UEdGraphPin*>& FromPins, const FVector2D Location, bool bSelectNewNode = true) override;
	virtual UEdGraphNode* PerformAction(UEdGraph* ParentGraph, UEdGraphPin* FromPin, const FVector2D Location, bool bSelectNewNode = true) { return nullptr; }

private:
	/** The public function definition we will spawn from [optional] */
	UPROPERTY(Transient)
	FRigVMGraphFunctionHeader ReferencedPublicFunctionHeader;

	/** Marked as true for local function definitions */
	UPROPERTY(Transient)
	bool bIsLocalFunction = false;

	/** Holds the node type that this spawner will instantiate. */
	UPROPERTY(Transient)
	TSubclassOf<UEdGraphNode> NodeClass;
};