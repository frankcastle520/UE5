// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassProcessingTypes.h"
#if WITH_MASSENTITY_DEBUG
#include "Containers/ContainersFwd.h"
#include "MassEntityQuery.h"
#include "MassProcessor.h"
#include "Misc/SpinLock.h"
#include "StructUtils/InstancedStruct.h"
#include "Logging/TokenizedMessage.h"

class FOutputDevice;
class UMassProcessor;
struct FMassEntityQuery;
struct FMassEntityManager;
struct FMassArchetypeHandle;
struct FMassFragmentRequirements;
struct FMassFragmentRequirementDescription;
enum class EMassFragmentAccess : uint8;
enum class EMassFragmentPresence : uint8;
#endif // WITH_MASSENTITY_DEBUG
#include "MassDebugger.generated.h"

namespace UE::Mass::Debug
{
	struct FArchetypeStats
	{
		/** Number of active entities of the archetype. */
		int32 EntitiesCount = 0;
		/** Number of entities that fit per chunk. */
		int32 EntitiesCountPerChunk = 0;
		/** Number of allocated chunks. */
		int32 ChunksCount = 0;
		/** Total amount of memory taken by this archetype */
		SIZE_T AllocatedSize = 0;
		/** How much memory allocated for entities is being unused */
		SIZE_T WastedEntityMemory = 0;
		/** Total amount of memory needed by a single entity */
		SIZE_T BytesPerEntity = 0;
	};
} // namespace UE::Mass::Debug

USTRUCT()
struct MASSENTITY_API FMassGenericDebugEvent
{
	GENERATED_BODY()
	explicit FMassGenericDebugEvent(const UObject* InContext = nullptr)
#if WITH_EDITORONLY_DATA
		: Context(InContext)
#endif // WITH_EDITORONLY_DATA
	{
	}

#if WITH_EDITORONLY_DATA
	// note that it's not a uproperty since these events are only intended to be used instantly, never stored
	const UObject* Context = nullptr;
#endif // WITH_EDITORONLY_DATA
};

#if WITH_MASSENTITY_DEBUG

namespace UE::Mass::Debug
{
	extern MASSENTITY_API bool bAllowProceduralDebuggedEntitySelection;
	extern MASSENTITY_API bool bAllowBreakOnDebuggedEntity;
	extern MASSENTITY_API bool bTestSelectedEntityAgainstProcessorQueries;
} // namespace UE::Mass::Debug

#define MASS_IF_ENTITY_DEBUGGED(Manager, EntityHandle) (FMassDebugger::GetSelectedEntity(Manager) == EntityHandle)
#define MASS_BREAK_IF_ENTITY_DEBUGGED(Manager, EntityHandle) { if (UE::Mass::Debug::bAllowBreakOnDebuggedEntity && MASS_IF_ENTITY_DEBUGGED(Manager, EntityHandle)) { PLATFORM_BREAK();} }
#define MASS_BREAK_IF_ENTITY_INDEX(EntityHandle, InIndex) { if (UE::Mass::Debug::bAllowBreakOnDebuggedEntity && EntityHandle.Index == InIndex) { PLATFORM_BREAK();} }
#define MASS_SET_ENTITY_DEBUGGED(Manager, EntityHandle) { if (UE::Mass::Debug::bAllowProceduralDebuggedEntitySelection) {FMassDebugger::SelectEntity(Manager, EntityHandle); }}

enum class EMassDebugMessageSeverity : uint8
{
	Error,
	Warning,
	Info,
	// the following two need to remain last
	Default,
	MAX = Default
};

namespace UE::Mass::Debug
{
	struct MASSENTITY_API FQueryRequirementsView
	{
		TConstArrayView<FMassFragmentRequirementDescription> FragmentRequirements;
		TConstArrayView<FMassFragmentRequirementDescription> ChunkRequirements;
		TConstArrayView<FMassFragmentRequirementDescription> ConstSharedRequirements;
		TConstArrayView<FMassFragmentRequirementDescription> SharedRequirements;
		const FMassTagBitSet& RequiredAllTags;
		const FMassTagBitSet& RequiredAnyTags;
		const FMassTagBitSet& RequiredNoneTags;
		const FMassTagBitSet& RequiredOptionalTags;
		const FMassExternalSubsystemBitSet& RequiredConstSubsystems;
		const FMassExternalSubsystemBitSet& RequiredMutableSubsystems;
	};

	FString DebugGetFragmentAccessString(EMassFragmentAccess Access);
	MASSENTITY_API extern void DebugOutputDescription(TConstArrayView<UMassProcessor*> Processors, FOutputDevice& Ar);

	MASSENTITY_API extern bool HasDebugEntities();
	MASSENTITY_API extern bool IsDebuggingSingleEntity();

	/**
	 * Populates OutBegin and OutEnd with entity index ranges as set by mass.debug.SetDebugEntityRange or
	 * mass.debug.DebugEntity console commands.
	 * @return whether any range has been configured.
	 */
	MASSENTITY_API extern bool GetDebugEntitiesRange(int32& OutBegin, int32& OutEnd);
	MASSENTITY_API extern bool IsDebuggingEntity(FMassEntityHandle Entity, FColor* OutEntityColor = nullptr);
	MASSENTITY_API extern FColor GetEntityDebugColor(FMassEntityHandle Entity);

	inline EMessageSeverity::Type MassSeverityToMessageSeverity(EMessageSeverity::Type OriginalSeverity, EMassDebugMessageSeverity MassSeverity)
	{
		static constexpr EMessageSeverity::Type ConversionMap[int(EMassDebugMessageSeverity::MAX)] =
		{
			/*EMassDebugMessageSeverity::Error=*/EMessageSeverity::Error,
			/*EMassDebugMessageSeverity::Warning=*/EMessageSeverity::Warning,
			/*EMassDebugMessageSeverity::Info=*/EMessageSeverity::Info
		};
		return MassSeverity == EMassDebugMessageSeverity::Default 
			? OriginalSeverity
			: ConversionMap[int(MassSeverity)];
	}
} // namespace UE::Mass::Debug

struct MASSENTITY_API FMassDebugger
{
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnEntitySelected, const FMassEntityManager&, const FMassEntityHandle);
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnMassEntityManagerEvent, const FMassEntityManager&);
	DECLARE_MULTICAST_DELEGATE_ThreeParams(FOnDebugEvent, const FName /*EventName*/, FConstStructView /*Payload*/, const EMassDebugMessageSeverity /*SeverityOverride*/);

	struct FEnvironment
	{
		TWeakPtr<const FMassEntityManager> EntityManager;
		FMassEntityHandle SelectedEntity;

		explicit FEnvironment(const FMassEntityManager& InEntityManager)
			: EntityManager(InEntityManager.AsWeak())
		{}

		bool IsValid() const { return EntityManager.IsValid(); }
	};

	static TConstArrayView<FMassEntityQuery*> GetProcessorQueries(const UMassProcessor& Processor);
	/** fetches all queries registered for given Processor. Note that in order to get up to date information
	 *  FMassEntityQuery::CacheArchetypes will be called on each query */
	static TConstArrayView<FMassEntityQuery*> GetUpToDateProcessorQueries(const FMassEntityManager& EntitySubsystem, UMassProcessor& Processor);

	static UE::Mass::Debug::FQueryRequirementsView GetQueryRequirements(const FMassEntityQuery& Query);
	static void GetQueryExecutionRequirements(const FMassEntityQuery& Query, FMassExecutionRequirements& OutExecutionRequirements);

	static TArray<FMassArchetypeHandle> GetAllArchetypes(const FMassEntityManager& EntitySubsystem);
	static const FMassArchetypeCompositionDescriptor& GetArchetypeComposition(const FMassArchetypeHandle& ArchetypeHandle);

	static void GetArchetypeEntityStats(const FMassArchetypeHandle& ArchetypeHandle, UE::Mass::Debug::FArchetypeStats& OutStats);
	static const TConstArrayView<FName> GetArchetypeDebugNames(const FMassArchetypeHandle& ArchetypeHandle);

	static TConstArrayView<UMassCompositeProcessor::FDependencyNode> GetProcessingGraph(const UMassCompositeProcessor& GraphOwner);
	static TConstArrayView<TObjectPtr<UMassProcessor>> GetHostedProcessors(const UMassCompositeProcessor& GraphOwner);
	
	static FString GetSingleRequirementDescription(const FMassFragmentRequirementDescription& Requirement);
	static FString GetRequirementsDescription(const FMassFragmentRequirements& Requirements);
	static FString GetArchetypeRequirementCompatibilityDescription(const FMassFragmentRequirements& Requirements, const FMassArchetypeHandle& ArchetypeHandle);
	static FString GetArchetypeRequirementCompatibilityDescription(const FMassFragmentRequirements& Requirements, const FMassArchetypeCompositionDescriptor& ArchetypeComposition);

	static void OutputArchetypeDescription(FOutputDevice& Ar, const FMassArchetypeHandle& Archetype);
	static void OutputEntityDescription(FOutputDevice& Ar, const FMassEntityManager& EntityManager, const int32 EntityIndex, const TCHAR* InPrefix = TEXT(""));
	static void OutputEntityDescription(FOutputDevice& Ar, const FMassEntityManager& EntityManager, const FMassEntityHandle Entity, const TCHAR* InPrefix = TEXT(""));

	static void SelectEntity(const FMassEntityManager& EntityManager, const FMassEntityHandle EntityHandle);
	static FMassEntityHandle GetSelectedEntity(const FMassEntityManager& EntityManager);

	static FOnEntitySelected OnEntitySelectedDelegate;

	static FOnMassEntityManagerEvent OnEntityManagerInitialized;
	static FOnMassEntityManagerEvent OnEntityManagerDeinitialized;

	static FOnDebugEvent OnDebugEvent;
	
	static void DebugEvent(const FName EventName, FConstStructView Payload, const EMassDebugMessageSeverity SeverityOverride = EMassDebugMessageSeverity::Default)
	{
		OnDebugEvent.Broadcast(EventName, Payload, SeverityOverride);
	}

	template<typename TMessage, typename... TArgs>
	static void DebugEvent(TArgs&&... InArgs)
	{
		DebugEvent(TMessage::StaticStruct()->GetFName()
			, FConstStructView::Make(TMessage(Forward<TArgs>(InArgs)...)));
	}

	static void RegisterEntityManager(FMassEntityManager& EntityManager);
	static void UnregisterEntityManager(FMassEntityManager& EntityManager);
	static TConstArrayView<FEnvironment> GetEnvironments() { return ActiveEnvironments; }

	/**
	 * Determines whether given Archetype matches given Requirements. In case of a mismatch description of failed conditions will be added to OutputDevice.
	 */
	static bool DoesArchetypeMatchRequirements(const FMassArchetypeHandle& ArchetypeHandle, const FMassFragmentRequirements& Requirements, FOutputDevice& OutputDevice);

private:
	static TArray<FEnvironment> ActiveEnvironments;
	static UE::FSpinLock EntityManagerRegistrationLock;
};

#else

struct FMassArchetypeHandle;
struct FMassFragmentRequirements;
struct FMassFragmentRequirementDescription;
struct FMassArchetypeCompositionDescriptor;

struct MASSENTITY_API FMassDebugger
{
	static FString GetSingleRequirementDescription(const FMassFragmentRequirementDescription&) { return TEXT("[no debug information]"); }
	static FString GetRequirementsDescription(const FMassFragmentRequirements&) { return TEXT("[no debug information]"); }
	static FString GetArchetypeRequirementCompatibilityDescription(const FMassFragmentRequirements&, const FMassArchetypeHandle&) { return TEXT("[no debug information]"); }
	static FString GetArchetypeRequirementCompatibilityDescription(const FMassFragmentRequirements&, const FMassArchetypeCompositionDescriptor&) { return TEXT("[no debug information]"); }
};

#define MASS_IF_ENTITY_DEBUGGED(a, b) false
#define MASS_BREAK_IF_ENTITY_DEBUGGED(a, b)
#define MASS_BREAK_IF_ENTITY_INDEX(a, b)
#define MASS_SET_ENTITY_DEBUGGED(a, b)

#endif // WITH_MASSENTITY_DEBUG
