// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimNextModuleImpl.h"
#include "AnimNextConfig.h"
#include "DataRegistry.h"
#include "IUniversalObjectLocatorModule.h"
#include "RigVMRuntimeDataRegistry.h"
#include "UniversalObjectLocator.h"
#include "Animation/AnimSequence.h"
#include "Animation/BlendProfile.h"
#include "Component/AnimNextComponent.h"
#include "Curves/CurveFloat.h"
#include "Graph/AnimNextAnimationGraph.h"
#include "Graph/AnimNext_LODPose.h"
#include "Graph/RigVMTrait_AnimNextPublicVariables.h"
#include "Module/AnimNextModuleInstance.h"
#include "Modules/ModuleManager.h"
#include "Param/AnimNextActorLocatorFragment.h"
#include "Param/AnimNextComponentLocatorFragment.h"
#include "Param/AnimNextObjectCastLocatorFragment.h"
#include "Param/AnimNextObjectFunctionLocatorFragment.h"
#include "Param/AnimNextObjectPropertyLocatorFragment.h"
#include "Param/AnimNextTag.h"
#include "RigVMCore/RigVMRegistry.h"
#include "TraitCore/NodeTemplateRegistry.h"
#include "TraitCore/TraitRegistry.h"
#include "TraitCore/TraitInterfaceRegistry.h"
#include "Variables/AnimNextFieldPath.h"
#include "Variables/AnimNextSoftFunctionPtr.h"

#if WITH_ANIMNEXT_CONSOLE_COMMANDS
#include "HAL/IConsoleManager.h"
#include "UObject/UObjectIterator.h"

#include "TraitCore/NodeDescription.h"
#include "TraitCore/NodeTemplate.h"
#include "TraitCore/TraitTemplate.h"
#endif

#define LOCTEXT_NAMESPACE "AnimNextModule"

namespace UE::AnimNext
{
	void FAnimNextModuleImpl::StartupModule()
	{
		GetMutableDefault<UAnimNextConfig>()->LoadConfig();

		FRigVMRegistry& RigVMRegistry = FRigVMRegistry::Get();
		static TPair<UClass*, FRigVMRegistry::ERegisterObjectOperation> const AllowedObjectTypes[] =
		{
			{ UAnimSequence::StaticClass(), FRigVMRegistry::ERegisterObjectOperation::Class },
			{ UScriptStruct::StaticClass(), FRigVMRegistry::ERegisterObjectOperation::Class },
			{ UBlendProfile::StaticClass(), FRigVMRegistry::ERegisterObjectOperation::Class },
			{ UCurveFloat::StaticClass(), FRigVMRegistry::ERegisterObjectOperation::Class },
			{ UAnimNextAnimationGraph::StaticClass(), FRigVMRegistry::ERegisterObjectOperation::Class },
			{ UAnimNextComponent::StaticClass(), FRigVMRegistry::ERegisterObjectOperation::Class },
		};

		RigVMRegistry.RegisterObjectTypes(AllowedObjectTypes);

		static UScriptStruct* const AllowedStructTypes[] =
		{
			FAnimNextScope::StaticStruct(),
			FAnimNextEntryPoint::StaticStruct(),
			FUniversalObjectLocator::StaticStruct(),
			FAnimNextGraphReferencePose::StaticStruct(),
			FAnimNextFieldPath::StaticStruct(),
			FAnimNextSoftFunctionPtr::StaticStruct(),
			FRigVMTrait_AnimNextPublicVariables::StaticStruct()
		};

		RigVMRegistry.RegisterStructTypes(AllowedStructTypes);

		Private::CacheAllModuleEvents();
		FDataRegistry::Init();
		FTraitRegistry::Init();
		FTraitInterfaceRegistry::Init();
		FNodeTemplateRegistry::Init();
		FRigVMRuntimeDataRegistry::Init();

		UE::UniversalObjectLocator::IUniversalObjectLocatorModule& UolModule = FModuleManager::Get().LoadModuleChecked<UE::UniversalObjectLocator::IUniversalObjectLocatorModule>("UniversalObjectLocator");
		FDelayedAutoRegisterHelper(EDelayedRegisterRunPhase::ObjectSystemReady,
			[&UolModule]
			{
				{
					UE::UniversalObjectLocator::FFragmentTypeParameters FragmentTypeParams("animobjfunc", LOCTEXT("AnimNextObjectFunctionFragment", "Function"));
					FragmentTypeParams.PrimaryEditorType = "AnimNextObjectFunction";
					FAnimNextObjectFunctionLocatorFragment::FragmentType = UolModule.RegisterFragmentType<FAnimNextObjectFunctionLocatorFragment>(FragmentTypeParams);
				}
				{
					UE::UniversalObjectLocator::FFragmentTypeParameters FragmentTypeParams("animobjprop", LOCTEXT("AnimNextObjectPropertyFragment", "Property"));
					FragmentTypeParams.PrimaryEditorType = "AnimNextObjectProperty";
					FAnimNextObjectPropertyLocatorFragment::FragmentType = UolModule.RegisterFragmentType<FAnimNextObjectPropertyLocatorFragment>(FragmentTypeParams);
				}
				{
					UE::UniversalObjectLocator::FFragmentTypeParameters FragmentTypeParams("animobjcast", LOCTEXT("AnimNextCastFragment", "Cast"));
					FragmentTypeParams.PrimaryEditorType = "AnimNextObjectCast";
					FAnimNextObjectCastLocatorFragment::FragmentType = UolModule.RegisterFragmentType<FAnimNextObjectCastLocatorFragment>(FragmentTypeParams);
				}
				{
					UE::UniversalObjectLocator::FFragmentTypeParameters FragmentTypeParams("animcomp", LOCTEXT("AnimNextComponentFragment", "AnimNextComponent"));
					FragmentTypeParams.PrimaryEditorType = "AnimNextComponent";
					FAnimNextComponentLocatorFragment::FragmentType = UolModule.RegisterFragmentType<FAnimNextComponentLocatorFragment>(FragmentTypeParams);
				}
				{
					UE::UniversalObjectLocator::FFragmentTypeParameters FragmentTypeParams("animactor", LOCTEXT("AnimNextActorFragment", "AnimNextActor"));
					FragmentTypeParams.PrimaryEditorType = "AnimNextActor";
					FAnimNextActorLocatorFragment::FragmentType = UolModule.RegisterFragmentType<FAnimNextActorLocatorFragment>(FragmentTypeParams);
				}
			});
#if WITH_ANIMNEXT_CONSOLE_COMMANDS
		if (!IsRunningCommandlet())
		{
			ConsoleCommands.Add(IConsoleManager::Get().RegisterConsoleCommand(
				TEXT("AnimNext.ListNodeTemplates"),
				TEXT("Dumps statistics about node templates to the log."),
				FConsoleCommandWithArgsDelegate::CreateRaw(this, &FAnimNextModuleImpl::ListNodeTemplates),
				ECVF_Default
			));
			ConsoleCommands.Add(IConsoleManager::Get().RegisterConsoleCommand(
				TEXT("AnimNext.Module"),
				TEXT("Dumps statistics about modules to the log."),
				FConsoleCommandWithArgsDelegate::CreateRaw(this, &FAnimNextModuleImpl::ListAnimationGraphs),
				ECVF_Default
			));
		}
#endif
	}

	void FAnimNextModuleImpl::ShutdownModule()
	{
		FRigVMRuntimeDataRegistry::Destroy();
		FNodeTemplateRegistry::Destroy();
		FTraitInterfaceRegistry::Destroy();
		FTraitRegistry::Destroy();
		FDataRegistry::Destroy();

#if WITH_ANIMNEXT_CONSOLE_COMMANDS
		for (IConsoleObject* Cmd : ConsoleCommands)
		{
			IConsoleManager::Get().UnregisterConsoleObject(Cmd);
		}
		ConsoleCommands.Empty();
#endif
	}

	const IAnimNextAnimGraph* AnimGraphImpl = nullptr;

	void FAnimNextModuleImpl::RegisterAnimNextAnimGraph(const IAnimNextAnimGraph& InAnimGraphImpl)
	{
		AnimGraphImpl = &InAnimGraphImpl;
	}

	void FAnimNextModuleImpl::UnregisterAnimNextAnimGraph()
	{
		AnimGraphImpl = nullptr;
	}

	void FAnimNextModuleImpl::UpdateGraph(const FAnimNextGraphInstancePtr& GraphInstance, float DeltaTime, FTraitEventList& InputEventList, FTraitEventList& OutputEventList)
	{
		if (AnimGraphImpl != nullptr)
		{
			AnimGraphImpl->UpdateGraph(GraphInstance, DeltaTime, InputEventList, OutputEventList);
		}
	}

	void FAnimNextModuleImpl::EvaluateGraph(const FAnimNextGraphInstancePtr& GraphInstance, const UE::AnimNext::FReferencePose& RefPose, int32 GraphLODLevel, FAnimNextGraphLODPose& OutputPose) const
	{
		if (AnimGraphImpl != nullptr)
		{
			AnimGraphImpl->EvaluateGraph(GraphInstance, RefPose, GraphLODLevel, OutputPose);
		}
	}

#if WITH_ANIMNEXT_CONSOLE_COMMANDS
	void FAnimNextModuleImpl::ListNodeTemplates(const TArray<FString>& Args)
	{
		// Turn off log times to make diff-ing easier
		TGuardValue<ELogTimes::Type> DisableLogTimes(GPrintLogTimes, ELogTimes::None);

		// Make sure to log everything
		const ELogVerbosity::Type OldVerbosity = LogAnimation.GetVerbosity();
		LogAnimation.SetVerbosity(ELogVerbosity::All);

		const FNodeTemplateRegistry& NodeTemplateRegistry = FNodeTemplateRegistry::Get();
		const FTraitRegistry& TraitRegistry = FTraitRegistry::Get();

		UE_LOG(LogAnimation, Log, TEXT("===== AnimNext Node Templates ====="));
		UE_LOG(LogAnimation, Log, TEXT("Template Buffer Size: %u bytes"), NodeTemplateRegistry.TemplateBuffer.GetAllocatedSize());

		for (auto It = NodeTemplateRegistry.TemplateUIDToHandleMap.CreateConstIterator(); It; ++It)
		{
			const FNodeTemplateRegistryHandle Handle = It.Value();
			const FNodeTemplate* NodeTemplate = NodeTemplateRegistry.Find(Handle);

			const uint32 NumTraits = NodeTemplate->GetNumTraits();

			UE_LOG(LogAnimation, Log, TEXT("[%x] has %u traits ..."), NodeTemplate->GetUID(), NumTraits);
			UE_LOG(LogAnimation, Log, TEXT("    Template Size: %u bytes"), NodeTemplate->GetNodeTemplateSize());
			UE_LOG(LogAnimation, Log, TEXT("    Shared Data Size: %u bytes"), NodeTemplate->GetNodeSharedDataSize());
			UE_LOG(LogAnimation, Log, TEXT("    Instance Data Size: %u bytes"), NodeTemplate->GetNodeInstanceDataSize());
			UE_LOG(LogAnimation, Log, TEXT("    Traits ..."));

			const FTraitTemplate* TraitTemplates = NodeTemplate->GetTraits();
			for (uint32 TraitIndex = 0; TraitIndex < NumTraits; ++TraitIndex)
			{
				const FTraitTemplate* TraitTemplate = TraitTemplates + TraitIndex;
				const FTrait* Trait = TraitRegistry.Find(TraitTemplate->GetRegistryHandle());
				const FString TraitName = Trait != nullptr ? Trait->GetTraitName() : TEXT("<Unknown>");

				const uint32 NextTraitIndex = TraitIndex + 1;
				const uint32 EndOfNextTraitSharedData = NextTraitIndex < NumTraits ? TraitTemplates[NextTraitIndex].GetNodeSharedOffset() : NodeTemplate->GetNodeSharedDataSize();
				const uint32 TraitSharedDataSize = EndOfNextTraitSharedData - TraitTemplate->GetNodeSharedOffset();

				const uint32 EndOfNextTraitInstanceData = NextTraitIndex < NumTraits ? TraitTemplates[NextTraitIndex].GetNodeInstanceOffset() : NodeTemplate->GetNodeInstanceDataSize();
				const uint32 TraitInstanceDataSize = EndOfNextTraitInstanceData - TraitTemplate->GetNodeInstanceOffset();

				UE_LOG(LogAnimation, Log, TEXT("            %u: [%x] %s (%s)"), TraitIndex, TraitTemplate->GetUID().GetUID(), *TraitName, TraitTemplate->GetMode() == ETraitMode::Base ? TEXT("Base") : TEXT("Additive"));
				UE_LOG(LogAnimation, Log, TEXT("                Shared Data: [Offset: %u bytes, Size: %u bytes]"), TraitTemplate->GetNodeSharedOffset(), TraitSharedDataSize);
				if (TraitTemplate->HasLatentProperties() && Trait != nullptr)
				{
					UE_LOG(LogAnimation, Log, TEXT("                Shared Data Latent Property Handles: [Offset: %u bytes, Count: %u]"), TraitTemplate->GetNodeSharedLatentPropertyHandlesOffset(), Trait->GetNumLatentTraitProperties());
				}
				UE_LOG(LogAnimation, Log, TEXT("                Instance Data: [Offset: %u bytes, Size: %u bytes]"), TraitTemplate->GetNodeInstanceOffset(), TraitInstanceDataSize);
			}
		}

		LogAnimation.SetVerbosity(OldVerbosity);
	}

	void FAnimNextModuleImpl::ListAnimationGraphs(const TArray<FString>& Args)
	{
		// Turn off log times to make diff-ing easier
		TGuardValue<ELogTimes::Type> DisableLogTimes(GPrintLogTimes, ELogTimes::None);

		// Make sure to log everything
		const ELogVerbosity::Type OldVerbosity = LogAnimation.GetVerbosity();
		LogAnimation.SetVerbosity(ELogVerbosity::All);

		TArray<const UAnimNextAnimationGraph*> AnimationGraphs;

		for (TObjectIterator<UAnimNextAnimationGraph> It; It; ++It)
		{
			AnimationGraphs.Add(*It);
		}

		struct FCompareObjectNames
		{
			FORCEINLINE bool operator()(const UAnimNextAnimationGraph& Lhs, const UAnimNextAnimationGraph& Rhs) const
			{
				return Lhs.GetPathName().Compare(Rhs.GetPathName()) < 0;
			}
		};
		AnimationGraphs.Sort(FCompareObjectNames());

		const FNodeTemplateRegistry& NodeTemplateRegistry = FNodeTemplateRegistry::Get();
		const FTraitRegistry& TraitRegistry = FTraitRegistry::Get();
		const bool bDetailedOutput = true;

		UE_LOG(LogAnimation, Log, TEXT("===== AnimNext Modules ====="));
		UE_LOG(LogAnimation, Log, TEXT("Num Graphs: %u"), AnimationGraphs.Num());

		for (const UAnimNextAnimationGraph* AnimationGraph : AnimationGraphs)
		{
			uint32 TotalInstanceSize = 0;
			uint32 NumNodes = 0;
			{
				// We always have a node at offset 0
				int32 NodeOffset = 0;

				while (NodeOffset < AnimationGraph->SharedDataBuffer.Num())
				{
					const FNodeDescription* NodeDesc = reinterpret_cast<const FNodeDescription*>(&AnimationGraph->SharedDataBuffer[NodeOffset]);

					TotalInstanceSize += NodeDesc->GetNodeInstanceDataSize();
					NumNodes++;

					const FNodeTemplate* NodeTemplate = NodeTemplateRegistry.Find(NodeDesc->GetTemplateHandle());
					NodeOffset += NodeTemplate->GetNodeSharedDataSize();
				}
			}

			UE_LOG(LogAnimation, Log, TEXT("    %s ..."), *AnimationGraph->GetPathName());
			UE_LOG(LogAnimation, Log, TEXT("        Shared Data Size: %.2f KB"), double(AnimationGraph->SharedDataBuffer.Num()) / 1024.0);
			UE_LOG(LogAnimation, Log, TEXT("        Max Instance Data Size: %.2f KB"), double(TotalInstanceSize) / 1024.0);
			UE_LOG(LogAnimation, Log, TEXT("        Num Nodes: %u"), NumNodes);

			if (bDetailedOutput)
			{
				// We always have a node at offset 0
				int32 NodeOffset = 0;

				while (NodeOffset < AnimationGraph->SharedDataBuffer.Num())
				{
					const FNodeDescription* NodeDesc = reinterpret_cast<const FNodeDescription*>(&AnimationGraph->SharedDataBuffer[NodeOffset]);
					const FNodeTemplate* NodeTemplate = NodeTemplateRegistry.Find(NodeDesc->GetTemplateHandle());

					const uint32 NumTraits = NodeTemplate->GetNumTraits();

					UE_LOG(LogAnimation, Log, TEXT("        Node %u: [Template %x with %u traits]"), NodeDesc->GetUID().GetNodeIndex(), NodeTemplate->GetUID(), NumTraits);
					UE_LOG(LogAnimation, Log, TEXT("            Shared Data: [Offset: %u bytes, Size: %u bytes]"), NodeOffset, NodeTemplate->GetNodeSharedDataSize());
					UE_LOG(LogAnimation, Log, TEXT("            Instance Data Size: %u bytes"), NodeDesc->GetNodeInstanceDataSize());
					UE_LOG(LogAnimation, Log, TEXT("            Traits ..."));

					const FTraitTemplate* TraitTemplates = NodeTemplate->GetTraits();
					for (uint32 TraitIndex = 0; TraitIndex < NumTraits; ++TraitIndex)
					{
						const FTraitTemplate* TraitTemplate = TraitTemplates + TraitIndex;
						const FTrait* Trait = TraitRegistry.Find(TraitTemplate->GetRegistryHandle());
						const FString TraitName = Trait != nullptr ? Trait->GetTraitName() : TEXT("<Unknown>");

						const uint32 NextTraitIndex = TraitIndex + 1;
						const uint32 EndOfNextTraitSharedData = NextTraitIndex < NumTraits ? TraitTemplates[NextTraitIndex].GetNodeSharedOffset() : NodeTemplate->GetNodeSharedDataSize();
						const uint32 TraitSharedDataSize = EndOfNextTraitSharedData - TraitTemplate->GetNodeSharedOffset();

						const uint32 EndOfNextTraitInstanceData = NextTraitIndex < NumTraits ? TraitTemplates[NextTraitIndex].GetNodeInstanceOffset() : NodeTemplate->GetNodeInstanceDataSize();
						const uint32 TraitInstanceDataSize = EndOfNextTraitInstanceData - TraitTemplate->GetNodeInstanceOffset();

						UE_LOG(LogAnimation, Log, TEXT("                    %u: [%x] %s (%s)"), TraitIndex, TraitTemplate->GetUID().GetUID(), *TraitName, TraitTemplate->GetMode() == ETraitMode::Base ? TEXT("Base") : TEXT("Additive"));
						UE_LOG(LogAnimation, Log, TEXT("                        Shared Data: [Offset: %u bytes, Size: %u bytes]"), TraitTemplate->GetNodeSharedOffset(), TraitSharedDataSize);
						if (TraitTemplate->HasLatentProperties() && Trait != nullptr)
						{
							UE_LOG(LogAnimation, Log, TEXT("                        Shared Data Latent Property Handles: [Offset: %u bytes, Count: %u]"), TraitTemplate->GetNodeSharedLatentPropertyHandlesOffset(), Trait->GetNumLatentTraitProperties());
						}
						UE_LOG(LogAnimation, Log, TEXT("                        Instance Data: [Offset: %u bytes, Size: %u bytes]"), TraitTemplate->GetNodeInstanceOffset(), TraitInstanceDataSize);
					}

					NodeOffset += NodeTemplate->GetNodeSharedDataSize();
				}
			}
		}

		LogAnimation.SetVerbosity(OldVerbosity);
	}
#endif

	IAnimNextModuleInterface& IAnimNextModuleInterface::Get()
	{
		return FModuleManager::LoadModuleChecked<IAnimNextModuleInterface>(TEXT("AnimNext"));
	}

}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(UE::AnimNext::FAnimNextModuleImpl, AnimNext)
