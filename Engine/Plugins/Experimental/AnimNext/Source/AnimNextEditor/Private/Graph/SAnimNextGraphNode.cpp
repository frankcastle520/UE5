// Copyright Epic Games, Inc. All Rights Reserved.

#include "Graph/SAnimNextGraphNode.h"
#include "AnimNextEdGraphNode.h"

#define LOCTEXT_NAMESPACE "SAnimNextGraphNode"

void SAnimNextGraphNode::Construct( const FArguments& InArgs )
{
	SRigVMGraphNode::Construct(SRigVMGraphNode::FArguments().GraphNodeObj(InArgs._GraphNodeObj));
}

#undef LOCTEXT_NAMESPACE
