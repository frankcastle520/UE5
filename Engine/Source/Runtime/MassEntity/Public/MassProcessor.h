// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MassEntityManager.h"
#include "MassProcessingTypes.h"
#include "Async/TaskGraphInterfaces.h"
#include "MassCommandBuffer.h"
#include "MassProcessor.generated.h"


struct FMassProcessingPhaseConfig;
class UMassCompositeProcessor;
struct FMassDebugger;

enum class EProcessorCompletionStatus : uint8
{
	Invalid,
	Threaded,
	Postponed,
	Done
};

USTRUCT()
struct FMassProcessorExecutionOrder
{
	GENERATED_BODY()

	/** Determines which processing group this processor will be placed in. Leaving it empty ("None") means "top-most group for my ProcessingPhase" */
	UPROPERTY(EditAnywhere, Category = Processor, config)
	FName ExecuteInGroup = FName();

	UPROPERTY(EditAnywhere, Category = Processor, config)
	TArray<FName> ExecuteBefore;

	UPROPERTY(EditAnywhere, Category = Processor, config)
	TArray<FName> ExecuteAfter;
};


UCLASS(abstract, EditInlineNew, CollapseCategories, config = Mass, defaultconfig, ConfigDoNotCheckDefaults)
class MASSENTITY_API UMassProcessor : public UObject
{
	GENERATED_BODY()
public:
	UMassProcessor();
	explicit UMassProcessor(const FObjectInitializer& ObjectInitializer);

	bool IsInitialized() const;
	/** Called to initialize the processor. Override to perform custom steps. The super implementation has to be called. */
	virtual void Initialize(UObject& Owner);
	virtual FGraphEventRef DispatchProcessorTasks(const TSharedPtr<FMassEntityManager>& EntityManager, FMassExecutionContext& ExecutionContext, const FGraphEventArray& Prerequisites = FGraphEventArray());

	EProcessorExecutionFlags GetExecutionFlags() const;

	/** Whether this processor should execute according the CurrentExecutionFlags parameters */
	bool ShouldExecute(const EProcessorExecutionFlags CurrentExecutionFlags) const;
	void CallExecute(FMassEntityManager& EntityManager, FMassExecutionContext& Context);

	/** 
	 * Controls whether there can be multiple instances of a given class in a single FMassRuntimePipeline and during 
	 * dependency solving. 
	 */
	bool ShouldAllowMultipleInstances() const;

	void DebugOutputDescription(FOutputDevice& Ar) const;
	virtual void DebugOutputDescription(FOutputDevice& Ar, int32 Indent) const;
	virtual FString GetProcessorName() const;
	
	//----------------------------------------------------------------------//
	// Ordering functions 
	//----------------------------------------------------------------------//
	/** Indicates whether this processor can ever be pruned while considered for a phase processing graph. A processor
	 *  can get pruned if none of its registered queries interact with archetypes instantiated at the moment of graph
	 *  building. This can also happen for special processors that don't register any queries - if that's the case override 
	 *  this function to return an appropriate value
	 *  @param bRuntimeMode indicates whether the pruning is being done for game runtime (true) or editor-time presentation (false) */
	virtual bool ShouldAllowQueryBasedPruning(const bool bRuntimeMode = true) const;

	virtual EMassProcessingPhase GetProcessingPhase() const;
	virtual void SetProcessingPhase(EMassProcessingPhase Phase);
	bool DoesRequireGameThreadExecution() const;
	
	const FMassProcessorExecutionOrder& GetExecutionOrder() const;

	/** By default,  fetches requirements declared entity queries registered via RegisterQuery. Processors can override 
	 *	this function to supply additional requirements */
	virtual void ExportRequirements(FMassExecutionRequirements& OutRequirements) const;

	const FMassSubsystemRequirements& GetProcessorRequirements() const;

	/** Adds Query to RegisteredQueries list. Query is required to be a member variable of this processor. Not meeting
	 *  this requirement will cause check failure and the query won't be registered. */
	void RegisterQuery(FMassEntityQuery& Query);

	void MarkAsDynamic();
	bool IsDynamic() const;

	bool ShouldAutoAddToGlobalList() const;
#if WITH_EDITOR
	bool ShouldShowUpInSettings() const;
#endif // WITH_EDITOR

	/** Sets bAutoRegisterWithProcessingPhases. Setting it to true will result in this processor class being always 
	 * instantiated to be automatically evaluated every frame. @see FMassProcessingPhaseManager
	 * Note that calling this function is only valid on CDOs. Calling it on a regular instance will fail an ensure and 
	 * have no other effect, i.e. CDO's value won't change */
	void SetShouldAutoRegisterWithGlobalList(const bool bAutoRegister);

	void GetArchetypesMatchingOwnedQueries(const FMassEntityManager& EntityManager, TArray<FMassArchetypeHandle>& OutArchetype);
	bool DoesAnyArchetypeMatchOwnedQueries(const FMassEntityManager& EntityManager);
	
#if CPUPROFILERTRACE_ENABLED
	FString StatId;
#endif
	
protected:
	virtual void ConfigureQueries() PURE_VIRTUAL(UMassProcessor::ConfigureQueries);
	virtual void PostInitProperties() override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) PURE_VIRTUAL(UMassProcessor::Execute);

protected:
	/** Configures when this given processor can be executed in relation to other processors and processing groups, within its processing phase. */
	UPROPERTY(EditDefaultsOnly, Category = Processor, config)
	FMassProcessorExecutionOrder ExecutionOrder;

	/** Processing phase this processor will be automatically run as part of. Needs to be set before the processor gets
	 *  registered with MassProcessingPhaseManager, otherwise it will have no effect. This property is usually read via
	 *  a given class's CDO, so it's recommended to set it in the constructor. */
	UPROPERTY(EditDefaultsOnly, Category = Processor, config)
	EMassProcessingPhase ProcessingPhase = EMassProcessingPhase::PrePhysics;

	/** Whether this processor should be executed on StandAlone or Server or Client */
	UPROPERTY(EditAnywhere, Category = "Pipeline", meta = (Bitmask, BitmaskEnum = "/Script/MassEntity.EProcessorExecutionFlags"), config)
	uint8 ExecutionFlags;

	/** Configures whether this processor should be automatically included in the global list of processors executed every tick (see ProcessingPhase and ExecutionOrder). */
	UPROPERTY(EditDefaultsOnly, Category = Processor, config)
	uint8 bAutoRegisterWithProcessingPhases : 1 = true;

	/** Meant as a class property, make sure to set it in subclass' constructor. Controls whether there can be multiple
	 *  instances of a given class in a single FMassRuntimePipeline and during dependency solving. */
	uint8 bAllowMultipleInstances : 1 = false;

private:
	/**
	 * Gets set to true when an instance of the processor gets added to the phase processing as a "dynamic processor".
	 * Once set it's never expected to be cleared out to `false` thus the private visibility of the member variable.
	 * A "dynamic" processor is a one that has bAutoRegisterWithProcessingPhases == false, meaning it's not automatically
	 * added to the processing graph. Additionally, making processors dynamic allows one to have multiple instances
	 * of processors of the same class. 
	 * @see MarkAsDynamic()
	 * @see IsDynamic()
	 */
	uint8 bIsDynamic : 1 = false;

	/** Used to track whether Initialized has been called. */
	uint8 bInitialized : 1 = false;

protected:
	UPROPERTY(EditDefaultsOnly, Category = Processor, config)
	uint8 bRequiresGameThreadExecution : 1 = false;

#if WITH_EDITORONLY_DATA
	/** Used to permanently remove a given processor class from PipeSetting's listing. Used primarily for test-time 
	 *  processor classes, but can also be used by project-specific code to prune the processor list. */
	UPROPERTY(config)
	uint8 bCanShowUpInSettings : 1 = true;
#endif // WITH_EDITORONLY_DATA

	friend UMassCompositeProcessor;
	friend FMassDebugger;

	/** A query representing elements this processor is accessing in Execute function outside of query execution */
	FMassSubsystemRequirements ProcessorRequirements;

private:
	/** Stores processor's queries registered via RegisterQuery. 
	 *  @note that it's safe to store pointers here since RegisterQuery does verify that a given registered query is 
	 *  a member variable of a given processor */
	TArray<FMassEntityQuery*> OwnedQueries;
};


UCLASS()
class MASSENTITY_API UMassCompositeProcessor : public UMassProcessor
{
	GENERATED_BODY()

	friend FMassDebugger;
public:
	struct FDependencyNode
	{
		FName Name;
		UMassProcessor* Processor = nullptr;
		TArray<int32> Dependencies;
#if WITH_MASSENTITY_DEBUG
		int32 SequenceIndex = INDEX_NONE;
#endif // WITH_MASSENTITY_DEBUG
	};

public:
	UMassCompositeProcessor();

	void SetChildProcessors(TArray<UMassProcessor*>&& InProcessors);

	virtual void Initialize(UObject& Owner) override;
	virtual void DebugOutputDescription(FOutputDevice& Ar, int32 Indent = 0) const override;
	virtual void SetProcessingPhase(EMassProcessingPhase Phase) override;

	void SetGroupName(FName NewName);
	FName GetGroupName() const;

	virtual void SetProcessors(TArrayView<UMassProcessor*> InProcessorInstances, const TSharedPtr<FMassEntityManager>& EntityManager = nullptr);

	/** 
	 * Builds flat processing graph that's being used for multithreading execution of hosted processors.
	 */
	virtual void BuildFlatProcessingGraph(TConstArrayView<FMassProcessorOrderInfo> SortedProcessors);

	/**
	 * Adds processors in InOutOrderedProcessors to ChildPipeline. 
	 * Note that this operation is non-destructive for the existing processors - the ones of classes found in InOutOrderedProcessors 
	 * will be retained and used instead of the instances provided via InOutOrderedProcessors. Respective entries in InOutOrderedProcessors
	 * will be updated to reflect the reuse.
	 * The described behavior however is available only for processors with bAllowMultipleInstances == false.
	 */
	void UpdateProcessorsCollection(TArrayView<FMassProcessorOrderInfo> InOutOrderedProcessors, EProcessorExecutionFlags InWorldExecutionFlags = EProcessorExecutionFlags::None);

	/** adds SubProcessor to an appropriately named group. If RequestedGroupName == None then SubProcessor
	 *  will be added directly to ChildPipeline. If not then the indicated group will be searched for in ChildPipeline 
	 *  and if it's missing it will be created and AddGroupedProcessor will be called recursively */
	void AddGroupedProcessor(FName RequestedGroupName, UMassProcessor& SubProcessor);

	virtual FGraphEventRef DispatchProcessorTasks(const TSharedPtr<FMassEntityManager>& EntityManager, FMassExecutionContext& ExecutionContext, const FGraphEventArray& Prerequisites = FGraphEventArray()) override;

	bool IsEmpty() const;

	virtual FString GetProcessorName() const override;

protected:
	virtual void ConfigureQueries() override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

	/** RequestedGroupName can indicate a multi-level group name, like so: A.B.C
	 *  We need to extract the highest-level group name ('A' in the example), and see if it already exists. 
	 *  If not, create it. 
	 *  @param RequestedGroupName name of the group for which we want to find or create the processor.
	 *  @param OutRemainingGroupName contains the group name after cutting the high-level group. In the used example it
	 *    will contain "B.C". This value is then used to recursively create subgroups */
	UMassCompositeProcessor* FindOrAddGroupProcessor(FName RequestedGroupName, FString* OutRemainingGroupName = nullptr);

protected:
	UPROPERTY(VisibleAnywhere, Category=Mass)
	FMassRuntimePipeline ChildPipeline;

	/** Group name that will be used when resolving processor dependencies and grouping */
	UPROPERTY()
	FName GroupName;

	TArray<FDependencyNode> FlatProcessingGraph;

	struct FProcessorCompletion
	{
		FGraphEventRef CompletionEvent;
		EProcessorCompletionStatus Status = EProcessorCompletionStatus::Invalid;

		bool IsDone() const 
		{
			return Status == EProcessorCompletionStatus::Done || (CompletionEvent.IsValid() && CompletionEvent->IsComplete());
		}

		void Wait()
		{
			if (CompletionEvent.IsValid())
			{
				CompletionEvent->Wait();
			}
		}
	};
	TArray<FProcessorCompletion> CompletionStatus;

	//-----------------------------------------------------------------------------
	// DEPRECATED
	//-----------------------------------------------------------------------------
public:
	UE_DEPRECATED(5.3, "Populate is deprecated. Please use UpdateProcessorsCollection instead.")
	void Populate(TConstArrayView<FMassProcessorOrderInfo> OrderedProcessors);
};


//-----------------------------------------------------------------------------
// UMassProcessor inlines
//-----------------------------------------------------------------------------
inline bool UMassProcessor::IsInitialized() const
{
	return bInitialized;
}

inline EProcessorExecutionFlags UMassProcessor::GetExecutionFlags() const
{
	return static_cast<EProcessorExecutionFlags>(ExecutionFlags);
}

inline bool UMassProcessor::ShouldExecute(const EProcessorExecutionFlags CurrentExecutionFlags) const
{
	return (GetExecutionFlags() & CurrentExecutionFlags) != EProcessorExecutionFlags::None;
}

inline bool UMassProcessor::ShouldAllowMultipleInstances() const
{
	return bAllowMultipleInstances;
}

inline void UMassProcessor::DebugOutputDescription(FOutputDevice& Ar) const
{
	DebugOutputDescription(Ar, 0);
}

inline bool UMassProcessor::DoesRequireGameThreadExecution() const
{
	return bRequiresGameThreadExecution;
}
	
inline const FMassProcessorExecutionOrder& UMassProcessor::GetExecutionOrder() const
{
	return ExecutionOrder;
}

inline const FMassSubsystemRequirements& UMassProcessor::GetProcessorRequirements() const
{
	return ProcessorRequirements;
}

inline void UMassProcessor::MarkAsDynamic()
{
	bIsDynamic = true;
}

inline bool UMassProcessor::IsDynamic() const
{
	return bIsDynamic != 0;
}

inline bool UMassProcessor::ShouldAutoAddToGlobalList() const
{
	return bAutoRegisterWithProcessingPhases;
}

#if WITH_EDITOR
inline bool UMassProcessor::ShouldShowUpInSettings() const
{
	return ShouldAutoAddToGlobalList() || bCanShowUpInSettings;
}
#endif // WITH_EDITOR

//-----------------------------------------------------------------------------
// UMassCompositeProcessor inlines
//-----------------------------------------------------------------------------
inline FName UMassCompositeProcessor::GetGroupName() const
{
	return GroupName;
}

inline bool UMassCompositeProcessor::IsEmpty() const
{
	return ChildPipeline.IsEmpty();
}
