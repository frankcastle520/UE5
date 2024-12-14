// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "AudioParameterControllerInterface.h"
#include "MetasoundEditorGraph.h"
#include "MetasoundEditorGraphBuilder.h"
#include "MetasoundEditorGraphNode.h"
#include "MetasoundVertex.h"
#include "Misc/Guid.h"
#include "Textures/SlateIcon.h"
#include "UObject/NoExportTypes.h"
#include "UObject/ScriptInterface.h"

#include "MetasoundEditorGraphInputNode.generated.h"


UCLASS()
class METASOUNDEDITOR_API UMetasoundEditorGraphInputNode : public UMetasoundEditorGraphMemberNode
{
	GENERATED_BODY()

public:
	virtual ~UMetasoundEditorGraphInputNode() = default;

	UPROPERTY()
	TObjectPtr<UMetasoundEditorGraphInput> Input;

	UPROPERTY()
	FGuid NodeID;

public:
	const FMetasoundEditorGraphVertexNodeBreadcrumb& GetBreadcrumb() const;

	virtual void CacheTitle() override;

	virtual void CacheBreadcrumb() override;
	virtual UMetasoundEditorGraphMember* GetMember() const override;
	virtual void ReconstructNode() override;

	virtual void UpdatePreviewInstance(const Metasound::FVertexName& InParameterName, TScriptInterface<IAudioParameterControllerInterface>& InParameterInterface) const;
	virtual FMetasoundFrontendClassName GetClassName() const;

	virtual FGuid GetNodeID() const override;
	virtual FLinearColor GetNodeTitleColor() const override;
	virtual FSlateIcon GetNodeTitleIcon() const override;
	virtual void GetPinHoverText(const UEdGraphPin& Pin, FString& OutHoverText) const override;
	virtual void Validate(Metasound::Editor::FGraphNodeValidationResult& OutResult) override;
	virtual bool EnableInteractWidgets() const override;
	virtual FText GetTooltipText() const override;

protected:
	// Breadcrumb used if associated FrontendNode cannot be found or has been unlinked
	UPROPERTY()
	FMetasoundEditorGraphVertexNodeBreadcrumb Breadcrumb;

	friend class Metasound::Editor::FGraphBuilder;
};
