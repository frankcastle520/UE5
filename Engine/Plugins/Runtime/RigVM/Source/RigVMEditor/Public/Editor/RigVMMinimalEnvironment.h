// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RigVMModel/RigVMGraph.h"
#include "RigVMModel/RigVMController.h"
#include "RigVMModel/RigVMClient.h"
#include "EdGraph/RigVMEdGraph.h"
#include "EdGraph/RigVMEdGraphNode.h"
#include "UObject/StrongObjectPtr.h"

class RIGVMEDITOR_API FRigVMMinimalEnvironment : public TSharedFromThis<FRigVMMinimalEnvironment>
{
public:
	FRigVMMinimalEnvironment(const UClass* InRigVMBlueprintClass = nullptr);

	URigVMGraph* GetModel() const;
	URigVMController* GetController() const;
	URigVMNode* GetNode() const;
	URigVMEdGraph* GetEdGraph() const;
	URigVMEdGraphNode* GetEdGraphNode() const;

	void SetSchemata(const UClass* InRigVMBlueprintClass);
	void SetNode(URigVMNode* InModelNode);
	void SetFunctionNode(const FRigVMGraphFunctionIdentifier& InIdentifier);
	
	FSimpleDelegate& OnChanged();
	void Tick_GameThead(float InDeltaTime);

private:

	void HandleModified(ERigVMGraphNotifType InNotification, URigVMGraph* InGraph, UObject* InSubject);
	
	TStrongObjectPtr<URigVMGraph> ModelGraph;
	TStrongObjectPtr<URigVMController> ModelController;
	UClass* EdGraphClass;
	UClass* EdGraphNodeClass;
	TStrongObjectPtr<URigVMEdGraph> EdGraph;
	TWeakObjectPtr<URigVMNode> ModelNode;
	TWeakObjectPtr<URigVMEdGraphNode> EdGraphNode;
	std::atomic<int32> NumModifications;
	FSimpleDelegate ChangedDelegate;
	FDelegateHandle ModelHandle;
};
