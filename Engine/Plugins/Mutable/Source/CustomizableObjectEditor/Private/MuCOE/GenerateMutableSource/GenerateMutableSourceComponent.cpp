// Copyright Epic Games, Inc. All Rights Reserved.

#include "GenerateMutableSourceComponent.h"

#include "GenerateMutableSourceModifier.h"
#include "Factories/FbxMeshImportData.h"
#include "MuCOE/GenerateMutableSource/GenerateMutableSource.h"

#include "MuCOE/CustomizableObjectCompiler.h"
#include "MuCOE/GenerateMutableSource/GenerateMutableSourceMesh.h"
#include "MuCOE/GenerateMutableSource/GenerateMutableSourceSurface.h"
#include "MuCOE/GenerateMutableSource/GenerateMutableSourceFloat.h"
#include "MuCOE/GraphTraversal.h"
#include "MuCOE/MutableUtils.h"
#include "MuCOE/Nodes/CustomizableObjectNodeComponentMesh.h"
#include "MuCOE/Nodes/CustomizableObjectNodeComponentMeshAddTo.h"
#include "MuCOE/Nodes/CustomizableObjectNodeComponentPassthroughMesh.h"
#include "MuCOE/Nodes/CustomizableObjectNodeComponentSwitch.h"
#include "MuCOE/Nodes/CustomizableObjectNodeComponentVariation.h"
#include "MuCOE/Nodes/CustomizableObjectNodeModifierBase.h"
#include "MuT/NodeComponent.h"
#include "MuT/NodeComponentEdit.h"
#include "MuT/NodeComponentNew.h"
#include "MuT/NodeComponentSwitch.h"
#include "MuT/NodeComponentVariation.h"
#include "MuT/NodeLOD.h"
#include "MuT/NodeSurfaceNew.h"
#include "MuT/NodeMeshConstant.h"
#include "MuT/NodeObjectNew.h"

#include "Rendering/SkeletalMeshLODModel.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Interfaces/ITargetPlatform.h"


#define LOCTEXT_NAMESPACE "CustomizableObjectEditor"


/** Generate LOD pins of the given NodeComponentBase (NodeComponent, NodeComponentExtend...).
 * @param TypedComponentMesh Given component node.
 * @param NodeComponent Core node to connect LOD generated pins. */
void GenerateMutableSourceComponentMesh(FMutableGraphGenerationContext& GenerationContext, const UCustomizableObjectNodeComponentMeshBase& TypedComponentMesh, mu::Ptr<mu::NodeComponent> NodeComponent)
{
	int32 FirstLOD = -1;

	const int32 NumLODsInRoot = GenerationContext.NumLODsInRoot;
	for (int32 CurrentLOD = 0; CurrentLOD < NumLODsInRoot; ++CurrentLOD)
	{
		GenerationContext.CurrentLOD = CurrentLOD;

		if (!NodeComponent->LODs.IsValidIndex(CurrentLOD))
		{
			NodeComponent->LODs.Add(new mu::NodeLOD());
		}

		mu::Ptr<mu::NodeLOD> LODNode = NodeComponent->LODs[CurrentLOD];

		LODNode->SetMessageContext(&TypedComponentMesh);

		const int32 NumLODs = TypedComponentMesh.LODPins.Num();

		const bool bUseAutomaticLods = GenerationContext.CurrentAutoLODStrategy == ECustomizableObjectAutomaticLODStrategy::AutomaticFromMesh;
		FirstLOD = (CurrentLOD < NumLODs) && (FirstLOD == INDEX_NONE || !bUseAutomaticLods) ? CurrentLOD : FirstLOD;

		if (FirstLOD < 0)
		{
			continue;
		}

		if (GenerationContext.CurrentLOD < GenerationContext.FirstLODAvailable)
		{
			continue;
		} 
	
		// Generate all relevant LODs for this object up until the current LODIndex.
		for (int32 LODIndex = FirstLOD; LODIndex <= CurrentLOD; ++LODIndex)
		{
			if (!TypedComponentMesh.LODPins.IsValidIndex(LODIndex))
			{
				continue;
			}
			
			const UEdGraphPin* LODPin = TypedComponentMesh.LODPins[LODIndex].Get();
			check(LODPin);
			
			GenerationContext.FromLOD = LODIndex;

			TArray<UEdGraphPin*> ConnectedLODPins = FollowInputPinArray(*LODPin);

			// Proccess non modifier nodes.
			for (UEdGraphPin* const ChildNodePin : ConnectedLODPins)
			{
				// Modifiers are shared for all components and are processed per LOD and not component.
				if (Cast<UCustomizableObjectNodeModifierBase>(ChildNodePin->GetOwningNode()))
				{
					FString Msg = FString::Printf(TEXT("The object has legacy modifier connections that cannot be generated. Their connections should be updated."));
					GenerationContext.Log(FText::FromString(Msg), &TypedComponentMesh, EMessageSeverity::Warning);
					continue;
				}
				
				mu::Ptr<mu::NodeSurface> SurfaceNode = GenerateMutableSourceSurface(ChildNodePin, GenerationContext);
				LODNode->Surfaces.Add(SurfaceNode);
			}
		}
	}

	// Clear the context state for LODs
	GenerationContext.CurrentLOD = 0;
	GenerationContext.FromLOD = 0;
}


mu::Ptr<mu::NodeComponent> GenerateMutableSourceComponent(const UEdGraphPin* Pin, FMutableGraphGenerationContext& GenerationContext)
{
	check(Pin)
	RETURN_ON_CYCLE(*Pin, GenerationContext)

	CheckNumOutputs(*Pin, GenerationContext);
	
	UCustomizableObjectNode* Node = CastChecked<UCustomizableObjectNode>(Pin->GetOwningNode());

	const FGeneratedKey Key(reinterpret_cast<void*>(&GenerateMutableSourceComponent), *Pin, *Node, GenerationContext, false);
	if (const FGeneratedData* Generated = GenerationContext.Generated.Find(Key))
	{
		return static_cast<mu::NodeComponent*>(Generated->Node.get());
	}
	
	mu::Ptr<mu::NodeComponent> Result;
	
	if (const UCustomizableObjectNodeComponentMesh* TypedComponentMesh = Cast<UCustomizableObjectNodeComponentMesh>(Node))
	{
		if (!GenerationContext.ComponentInfos.ContainsByPredicate([&](const FMutableComponentInfo& ComponentInfo)
		{
			return ComponentInfo.Node == TypedComponentMesh;
		}))
		{
			return nullptr; // Not generated in the first pass.
		}
		
		mu::Ptr<mu::NodeComponentNew> NodeComponentNew = new mu::NodeComponentNew();
		NodeComponentNew->Id = GenerationContext.ComponentNames.Find(TypedComponentMesh->ComponentName);
		NodeComponentNew->SetMessageContext(Node);
		
		Result = NodeComponentNew;

		GenerationContext.CurrentMeshComponent = TypedComponentMesh->ComponentName;
		GenerationContext.CurrentAutoLODStrategy = TypedComponentMesh->AutoLODStrategy;
		
		GenerateMutableSourceComponentMesh(GenerationContext, *TypedComponentMesh, NodeComponentNew);

		GenerationContext.CurrentMeshComponent = {};
		GenerationContext.CurrentAutoLODStrategy = {};
	}

	else if (const UCustomizableObjectNodeComponentMeshAddTo* TypedComponentMeshExtend = Cast<UCustomizableObjectNodeComponentMeshAddTo>(Node))
	{
		if (FMutableComponentInfo* FindResult = GenerationContext.ComponentInfos.FindByPredicate([&](const FMutableComponentInfo& Element)
		{
			return Element.ComponentName == TypedComponentMeshExtend->ParentComponentName;
		}))
		{
			UCustomizableObjectNodeComponentMesh* TypedParentComponentMesh = FindResult->Node;

			if (TypedComponentMeshExtend->NumLODs > TypedParentComponentMesh->NumLODs)
			{
				FText Msg = FText::Format(LOCTEXT("ExtendMeshComponentLODs", "Add To Mesh Component can not have more LODs than its parent Mesh Component [{0}]."), FText::FromName(TypedComponentMeshExtend->ParentComponentName));
				GenerationContext.Log(Msg, TypedComponentMeshExtend, EMessageSeverity::Warning);
			}

			mu::Ptr<mu::NodeComponent> ParentNodeComponent = GenerateMutableSourceComponent(TypedParentComponentMesh->OutputPin.Get(), GenerationContext);

			mu::Ptr<mu::NodeComponentEdit> NodeComponentEdit = new mu::NodeComponentEdit();
			NodeComponentEdit->SetParent(ParentNodeComponent.get());
			NodeComponentEdit->SetMessageContext(TypedComponentMeshExtend);
		
			GenerationContext.CurrentMeshComponent = TypedParentComponentMesh->ComponentName;
			GenerationContext.CurrentAutoLODStrategy = TypedComponentMeshExtend->AutoLODStrategy == ECustomizableObjectAutomaticLODStrategy::Inherited ?
				TypedParentComponentMesh->AutoLODStrategy :
				TypedComponentMeshExtend->AutoLODStrategy;
			
			GenerateMutableSourceComponentMesh(GenerationContext, *TypedComponentMeshExtend, NodeComponentEdit);

			GenerationContext.CurrentMeshComponent = {};
			GenerationContext.CurrentAutoLODStrategy = {};
			
			Result = NodeComponentEdit;	
		}
		else
		{
			FText Msg = FText::Format(LOCTEXT("ExtendMeshComponent", "Can not find parent Mesh Component {0}."), FText::FromName(TypedComponentMeshExtend->ParentComponentName));
			GenerationContext.Log(Msg, TypedComponentMeshExtend, EMessageSeverity::Error);
		}
	}
	
	else if (const UCustomizableObjectNodeComponentPassthroughMesh* TypedComponentPassthroughMesh = Cast<UCustomizableObjectNodeComponentPassthroughMesh>(Node))
	{
		GenerationContext.CurrentMeshComponent = TypedComponentPassthroughMesh->ComponentName;

		if (TypedComponentPassthroughMesh->ComponentName.IsNone())
		{
			FString Msg = FString::Printf(TEXT("Invalid Component Name."));
			GenerationContext.Log(FText::FromString(Msg), TypedComponentPassthroughMesh, EMessageSeverity::Warning);
			return nullptr;
		}

		if (!TypedComponentPassthroughMesh->Mesh.IsValid())
		{
			FString Msg = FString::Printf(TEXT("No mesh set for component node."));
			GenerationContext.Log(FText::FromString(Msg), TypedComponentPassthroughMesh, EMessageSeverity::Warning);
			return nullptr;
		}

		USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(TypedComponentPassthroughMesh->Mesh.TryLoad());
		if (!SkeletalMesh)
		{
			FString Msg = FString::Printf(TEXT("Only SkeletalMeshes are supported in this node, for now."));
			GenerationContext.Log(FText::FromString(Msg), TypedComponentPassthroughMesh, EMessageSeverity::Warning);
			return nullptr;
		}

		// Create the referenced mesh node.
		mu::Ptr<mu::NodeMeshConstant> MeshNode;
		{
			MeshNode = new mu::NodeMeshConstant();

			FString MeshUniqueTags;
			constexpr bool bIsReference = true;
			TSoftClassPtr<UAnimInstance> AnimInstance;
			mu::Ptr<mu::Mesh> MutableMesh = GenerateMutableMesh(SkeletalMesh, AnimInstance, 0, 0, 0, 0, MeshUniqueTags, 0, 
																GenerationContext, TypedComponentPassthroughMesh, nullptr, bIsReference);

			MeshNode->SetValue(MutableMesh);
		}

		// Create the component node
		mu::Ptr<mu::NodeComponentNew> ComponentNode = new mu::NodeComponentNew;
		if (GenerationContext.ComponentNames.Contains(TypedComponentPassthroughMesh->ComponentName))
		{
			FString Msg = FString::Printf(TEXT("More than one component with the same name [%s] have been found. This is not supported."), *TypedComponentPassthroughMesh->ComponentName.ToString());
			GenerationContext.Log(FText::FromString(Msg), TypedComponentPassthroughMesh, EMessageSeverity::Warning);
		}
		ComponentNode->Id = GenerationContext.ComponentNames.Add(TypedComponentPassthroughMesh->ComponentName);

		// While we don't handle the LODs per component, make sure we have some LODs for the passthrough mesh to appear.
		if (GenerationContext.NumLODsInRoot == 0)
		{
			GenerationContext.NumLODsInRoot = SkeletalMesh->GetLODNum();
			GenerationContext.NumMaxLODsToStream = FMath::Clamp(GenerationContext.NumMaxLODsToStream, 0, GenerationContext.NumLODsInRoot - 1);
			GenerationContext.FirstLODAvailable = FMath::Clamp(GenerationContext.FirstLODAvailable, 0, GenerationContext.NumLODsInRoot - 1);
		}

		// Create a LOD for each pass-through mesh LOD.
		const FSkeletalMeshModel* Model = SkeletalMesh->GetImportedModel();
		int32 SkeletalMeshLODCount = Model->LODModels.Num();
		for (int32 LODIndex=0; LODIndex<SkeletalMeshLODCount; ++LODIndex)
		{
			mu::Ptr<mu::NodeLOD> LODNode = new mu::NodeLOD;
			ComponentNode->LODs.Add(LODNode);

			const FSkeletalMeshLODModel& LODModel = Model->LODModels[LODIndex];
			int32 SectionCount = LODModel.Sections.Num();
			for (int32 SectionIndex = 0; SectionIndex < SectionCount; ++SectionIndex)
			{
				// Is there a pin in the unreal node for this section?
				if (UEdGraphPin* InMaterialPin = TypedComponentPassthroughMesh->GetMaterialPin(LODIndex,SectionIndex))
				{
					if (UEdGraphPin* ConnectedMaterialPin = FollowInputPin(*InMaterialPin))
					{
						GenerationContext.ComponentMeshOverride = MeshNode;
					
						mu::Ptr<mu::NodeSurface> SurfaceNode = GenerateMutableSourceSurface(ConnectedMaterialPin, GenerationContext);
						LODNode->Surfaces.Add(SurfaceNode);

						GenerationContext.ComponentMeshOverride = nullptr;
					}
					else
					{
						// Add an empty surface node anyway.
						mu::Ptr<mu::NodeSurfaceNew> SurfaceNode = new mu::NodeSurfaceNew;
						SurfaceNode->Mesh = MeshNode;
						LODNode->Surfaces.Add(SurfaceNode);
					}
				}
			}
		}

		GenerationContext.CurrentMeshComponent = FName();
		Result = ComponentNode;
	}
	
	else if (const UCustomizableObjectNodeComponentSwitch* TypedNodeSwitch = Cast<UCustomizableObjectNodeComponentSwitch>(Node))
	{
		// Using a lambda so control flow is easier to manage.
		Result = [&]()
			{
				const UEdGraphPin* SwitchParameter = TypedNodeSwitch->SwitchParameter();

				// Check Switch Parameter arity preconditions.
				if (const UEdGraphPin* EnumPin = FollowInputPin(*SwitchParameter))
				{
					mu::Ptr<mu::NodeScalar> SwitchParam = GenerateMutableSourceFloat(EnumPin, GenerationContext);

					// Switch Param not generated
					if (!SwitchParam)
					{
						// Warn about a failure.
						if (EnumPin)
						{
							const FText Message = LOCTEXT("FailedToGenerateSwitchParam", "Could not generate switch enum parameter. Please refesh the switch node and connect an enum.");
							GenerationContext.Log(Message, Node);
						}

						return Result;
					}

					if (SwitchParam->GetType() != mu::NodeScalarEnumParameter::GetStaticType())
					{
						const FText Message = LOCTEXT("WrongSwitchParamType", "Switch parameter of incorrect type.");
						GenerationContext.Log(Message, Node);

						return Result;
					}

					const int32 NumSwitchOptions = TypedNodeSwitch->GetNumElements();

					mu::NodeScalarEnumParameter* EnumParameter = static_cast<mu::NodeScalarEnumParameter*>(SwitchParam.get());
					if (NumSwitchOptions != EnumParameter->GetValueCount())
					{
						const FText Message = LOCTEXT("MismatchedSwitch", "Switch enum and switch node have different number of options. Please refresh the switch node to make sure the outcomes are labeled properly.");
						GenerationContext.Log(Message, Node);
					}

					mu::Ptr<mu::NodeComponentSwitch> SwitchNode = new mu::NodeComponentSwitch;
					SwitchNode->Parameter = SwitchParam;
					SwitchNode->Options.SetNum(NumSwitchOptions);

					for (int32 SelectorIndex = 0; SelectorIndex < NumSwitchOptions; ++SelectorIndex)
					{
						if (const UEdGraphPin* ConnectedPin = FollowInputPin(*TypedNodeSwitch->GetElementPin(SelectorIndex)))
						{
							mu::Ptr<mu::NodeComponent> ChildNode = GenerateMutableSourceComponent(ConnectedPin, GenerationContext);
							if (ChildNode)
							{
								SwitchNode->Options[SelectorIndex] = ChildNode;
							}
							else
							{
								// Probably ok
							}
						}
					}

					Result = SwitchNode;
					return Result;
				}
				else
				{
					GenerationContext.Log(LOCTEXT("NoEnumParamInSwitch", "Switch nodes must have an enum switch parameter. Please connect an enum and refesh the switch node."), Node);
					return Result;
				}
			}(); // invoke lambda.
	}

	else if (const UCustomizableObjectNodeComponentVariation* TypedNodeVar = Cast<UCustomizableObjectNodeComponentVariation>(Node))
	{
		mu::Ptr<mu::NodeComponentVariation> SurfNode = new mu::NodeComponentVariation();
		Result = SurfNode;

		for (const UEdGraphPin* ConnectedPin : FollowInputPinArray(*TypedNodeVar->DefaultPin()))
		{
			mu::Ptr<mu::NodeComponent> ChildNode = GenerateMutableSourceComponent(ConnectedPin, GenerationContext);
			if (ChildNode)
			{
				SurfNode->DefaultComponent = ChildNode;
			}
			else
			{
				GenerationContext.Log(LOCTEXT("ComponentFailed", "Component generation failed."), Node);
			}
		}

		const int32 NumVariations = TypedNodeVar->GetNumVariations();
		SurfNode->Variations.SetNum(NumVariations);
		for (int VariationIndex = 0; VariationIndex < NumVariations; ++VariationIndex)
		{
			mu::NodeSurfacePtr VariationSurfaceNode;

			if (UEdGraphPin* VariationPin = TypedNodeVar->VariationPin(VariationIndex))
			{
				SurfNode->Variations[VariationIndex].Tag = TypedNodeVar->GetVariation(VariationIndex).Tag;
				for (const UEdGraphPin* ConnectedPin : FollowInputPinArray(*VariationPin))
				{
					// Is it a modifier?
					mu::Ptr<mu::NodeComponent> ChildNode = GenerateMutableSourceComponent(ConnectedPin, GenerationContext);
					if (ChildNode)
					{
						SurfNode->Variations[VariationIndex].Component = ChildNode;
					}
					else
					{
						GenerationContext.Log(LOCTEXT("ComponentFailed", "Component generation failed."), Node);
					}
				}
			}
		}
	}

	else
	{
		GenerationContext.Log(LOCTEXT("UnimplementedNode", "Node type not implemented yet."), Node);
		ensure(false);
	}

	GenerationContext.Generated.Add(Key, FGeneratedData(Node, Result));
	GenerationContext.GeneratedNodes.Add(Node);
	
	return Result;
}


void FirstPass(UCustomizableObjectNodeComponentMesh& Node, FMutableGraphGenerationContext& GenerationContext)
{
	if (Node.ComponentName.IsNone())
	{
		GenerationContext.Log(LOCTEXT("EmptyComponentNameError", "Error! Missing name in a component of the Customizable Object."), &Node, EMessageSeverity::Error);
		return;
	}
	
	if (FMutableComponentInfo* Result = GenerationContext.ComponentInfos.FindByPredicate([&](const FMutableComponentInfo& Element)
	{
		return Element.ComponentName == Node.ComponentName;
	}))
	{
		FText Msg = FText::Format(LOCTEXT("ComponentNodeWithSameNameExists", "Error! It already exists a Mesh Component node with the same name in Customizable Object [{0}]"), FText::FromString(GetRootObject(*Result->Node)->GetName()));
		GenerationContext.Log(Msg, &Node, EMessageSeverity::Error);
		return;
	}
	
	USkeletalMesh* RefSkeletalMesh = Node.ReferenceSkeletalMesh;
	if (!RefSkeletalMesh)
	{
		GenerationContext.Log(LOCTEXT("NoReferenceMeshObjectTab", "Error! Missing reference Skeletal Mesh"), &Node, EMessageSeverity::Error);
		return;
	}
	
	USkeleton* RefSkeleton = RefSkeletalMesh->GetSkeleton();
	if (!RefSkeleton)
	{
		FText Msg = FText::Format(LOCTEXT("NoReferenceSkeleton", "Error! Missing skeleton in the reference mesh [{0}]"), FText::FromString(GenerationContext.CustomizableObjectWithCycle->GetPathName()));

		GenerationContext.Log(Msg, &Node, EMessageSeverity::Error);
		return;
	}
	
	// Ensure that the CO has a valid AutoLODStrategy on the Component node.
	if (Node.AutoLODStrategy == ECustomizableObjectAutomaticLODStrategy::Inherited)
	{
		GenerationContext.Log(LOCTEXT("RootInheritsFromParent", "Error! Component LOD Strategy can't be set to 'Inherit from parent object'"), &Node, EMessageSeverity::Error);
		return;
	}

	// Fill the basic LOD Settings
	const int32 NumLODs = Node.LODPins.Num();

	if (!GenerationContext.NumLODsInRoot)
	{
		// NumLODsInRoot
		int32 MaxRefMeshLODs = Node.ReferenceSkeletalMesh->GetLODNum();
		if (MaxRefMeshLODs < NumLODs)
		{
			FString Msg = FString::Printf(TEXT("The object has %d LODs but the reference mesh only %d. Resulting objects will have %d LODs."),
				NumLODs, MaxRefMeshLODs, MaxRefMeshLODs);
			GenerationContext.Log(FText::FromString(Msg), &Node, EMessageSeverity::Warning);
			GenerationContext.NumLODsInRoot = MaxRefMeshLODs;
		}
		else
		{
			GenerationContext.NumLODsInRoot = FMath::Max(GenerationContext.NumLODsInRoot, static_cast<uint8>(NumLODs));
		}
	
		const FMutableLODSettings& LODSettings = GenerationContext.Object->LODSettings;

		// Find the MinLOD available for the target platform
		if (RefSkeletalMesh->IsMinLodQualityLevelEnable())
		{
			FSupportedQualityLevelArray SupportedQualityLevels = LODSettings.MinQualityLevelLOD.GetSupportedQualityLevels(*GenerationContext.Options.TargetPlatform->GetPlatformInfo().IniPlatformName.ToString());
			
			int32 MinValue = GenerationContext.NumLODsInRoot - 1;
			for (int32& QL : SupportedQualityLevels)
			{
				// check if have data for the supported quality level or set to default.
				if (LODSettings.MinQualityLevelLOD.IsQualityLevelValid(QL))
				{
					MinValue = FMath::Min(LODSettings.MinQualityLevelLOD.GetValueForQualityLevel(QL), MinValue);
				}
				else 
				{
					MinValue = LODSettings.MinQualityLevelLOD.GetDefault();
					break;
				}
			}

			GenerationContext.FirstLODAvailable = FMath::Max(0, MinValue);
		}
		else
		{
			GenerationContext.FirstLODAvailable = LODSettings.MinLOD.GetValueForPlatform(*GenerationContext.Options.TargetPlatform->IniPlatformName());
		}

		GenerationContext.FirstLODAvailable = FMath::Clamp(GenerationContext.FirstLODAvailable, 0, GenerationContext.NumLODsInRoot - 1);

		// Find the streaming settings for the target platform
		if (LODSettings.bOverrideLODStreamingSettings)
		{
			GenerationContext.bEnableLODStreaming = LODSettings.bEnableLODStreaming.GetValueForPlatform(*GenerationContext.Options.TargetPlatform->IniPlatformName());
			GenerationContext.NumMaxLODsToStream = LODSettings.NumMaxStreamedLODs.GetValueForPlatform(*GenerationContext.Options.TargetPlatform->IniPlatformName());
		}
		else
		{
			for (int32 MeshIndex = 0; MeshIndex < GenerationContext.ComponentInfos.Num(); ++MeshIndex)
			{
				RefSkeletalMesh = GenerationContext.ComponentInfos[MeshIndex].RefSkeletalMesh;
				check(RefSkeletalMesh);

				GenerationContext.bEnableLODStreaming = GenerationContext.bEnableLODStreaming &&
					RefSkeletalMesh->GetEnableLODStreaming(GenerationContext.Options.TargetPlatform);

				GenerationContext.NumMaxLODsToStream = FMath::Min(static_cast<int32>(GenerationContext.NumMaxLODsToStream),
					RefSkeletalMesh->GetMaxNumStreamedLODs(GenerationContext.Options.TargetPlatform));
			}
		}

		GenerationContext.NumMaxLODsToStream = FMath::Clamp(GenerationContext.NumMaxLODsToStream, 0, GenerationContext.NumLODsInRoot - 1);
	}
	
	// Add a new entry to the list of Component Infos
	FMutableComponentInfo ComponentInfo(Node.ComponentName, RefSkeletalMesh);
	ComponentInfo.Node = &Node;
	ComponentInfo.AccumulateBonesToRemovePerLOD(Node.LODReductionSettings, Node.NumLODs);

	GenerationContext.ComponentInfos.Add(ComponentInfo);

	// Make sure the Skeleton from the reference mesh is added to the list of referenced Skeletons.
	GenerationContext.ReferencedSkeletons.Add(RefSkeleton);

	GenerationContext.ComponentNames.Add(Node.ComponentName);

	// Add reference meshes to the participating objects
	GenerationContext.AddParticipatingObject(*RefSkeletalMesh);
}


#undef LOCTEXT_NAMESPACE

