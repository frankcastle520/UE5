// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DataInterface/AnimNextDataInterfaceInstance.h"
#include "HAL/CriticalSection.h"
#include "TraitCore/TraitEvent.h"
#include "TraitCore/TraitEventList.h"
#include "TraitCore/TraitPtr.h"
#include "RigVMCore/RigVMExecuteContext.h"

#include "AnimNextGraphInstance.generated.h"

class FReferenceCollector;

struct FAnimNextGraphInstancePtr;
struct FRigUnit_AnimNextGraphEvaluator;
class UAnimNextAnimationGraph;
class UAnimNextModule;
struct FAnimNextModuleInstance;
class FRigVMTraitScope;
struct FRigVMExtendedExecuteContext;
struct FAnimNextExecuteContext;

namespace UE::AnimNext
{
	class IDataInterfaceHost;
	struct FExecutionContext;
	struct FGraphInstanceComponent;
	struct FLatentPropertyHandle;
	struct FTraitStackBinding;
}

using GraphInstanceComponentMapType = TMap<FName, TSharedPtr<UE::AnimNext::FGraphInstanceComponent>>;

// Represents an instance of an AnimNext graph
// This struct uses UE reflection because we wish for the GC to keep the graph
// alive while we own a reference to it. It is not intended to be serialized on disk with a live instance.
USTRUCT()
struct ANIMNEXT_API FAnimNextGraphInstance : public FAnimNextDataInterfaceInstance
{
	GENERATED_BODY()

	// Creates an empty graph instance that doesn't reference anything
	FAnimNextGraphInstance();

	// No copying, no moving
	FAnimNextGraphInstance(const FAnimNextGraphInstance&) = delete;
	FAnimNextGraphInstance& operator=(const FAnimNextGraphInstance&) = delete;

	// If the graph instance is allocated, we release it during destruction
	~FAnimNextGraphInstance();

	// Releases the graph instance and frees all corresponding memory
	void Release();

	// Returns true if we have a live graph instance, false otherwise
	bool IsValid() const;

	// Returns the animation graph used by this instance or nullptr if the instance is invalid
	const UAnimNextAnimationGraph* GetAnimationGraph() const;

	// Returns the entry point in Graph that this instance corresponds to 
	FName GetEntryPoint() const;
	
	// Returns a weak handle to the root trait instance
	UE::AnimNext::FWeakTraitPtr GetGraphRootPtr() const;

	// Returns the module instance that owns us or nullptr if we are invalid
	FAnimNextModuleInstance* GetModuleInstance() const;

	// Returns the parent graph instance that owns us or nullptr for the root graph instance or if we are invalid
	FAnimNextGraphInstance* GetParentGraphInstance() const;

	// Returns the root graph instance that owns us and the components or nullptr if we are invalid
	FAnimNextGraphInstance* GetRootGraphInstance() const;

	// Check to see if this instance data matches the provided animation graph
	bool UsesAnimationGraph(const UAnimNextAnimationGraph* InAnimationGraph) const;

	// Check to see if this instance data matches the provided graph entry point
	bool UsesEntryPoint(FName InEntryPoint) const;
	
	// Returns whether or not this graph instance is the root graph instance or false otherwise
	bool IsRoot() const;

	// Returns whether or not this graph instance has updated at least once
	bool HasUpdated() const;

	// Adds strong/hard object references during GC
	void AddStructReferencedObjects(class FReferenceCollector& Collector);

	// Returns a typed graph instance component, creating it lazily the first time it is queried
	template<class ComponentType>
	ComponentType& GetComponent();

	// Returns a typed graph instance component pointer if found or nullptr otherwise
	template<class ComponentType>
	ComponentType* TryGetComponent();

	// Returns a typed graph instance component pointer if found or nullptr otherwise
	template<class ComponentType>
	const ComponentType* TryGetComponent() const;

	// Returns const iterators to the graph instance component container
	GraphInstanceComponentMapType::TConstIterator GetComponentIterator() const;

	// Called each time the graph updates
	void Update();

	// Get the hosting instance, if any, that owns us
	FAnimNextDataInterfaceInstance* GetHost() const;

	FInstancedPropertyBag& GetVariables() { return Variables; }
private:
	// Returns a pointer to the specified component, or nullptr if not found
	UE::AnimNext::FGraphInstanceComponent* TryGetComponent(int32 ComponentNameHash, FName ComponentName) const;

	// Adds the specified component and returns a reference to it
	UE::AnimNext::FGraphInstanceComponent& AddComponent(int32 ComponentNameHash, FName ComponentName, TSharedPtr<UE::AnimNext::FGraphInstanceComponent>&& Component);

	// Executes a list of latent RigVM pins and writes the result into the destination pointer (latent handle offsets are using the destination as base)
	// When frozen, latent handles that can freeze are skipped, all others will execute
	void ExecuteLatentPins(const TConstArrayView<UE::AnimNext::FLatentPropertyHandle>& LatentHandles, void* DestinationBasePtr, bool bIsFrozen);

#if WITH_EDITORONLY_DATA
	// During graph compilation, if we have existing graph instances, we freeze them by releasing their memory before thawing them
	// Freezing is a partial release of resources that retains the necessary information to re-create things safely
	void Freeze();

	// During graph compilation, once compilation is done we thaw existing graph instances to reallocate their memory
	void Thaw();

	// Hook into module compilation
	void OnModuleCompiled(UAnimNextModule* InModule);
#endif

	template<typename HostType>
	bool BindToHostHelper(const HostType& InHost, bool bInAutoBind);

	// Bind the variables in the supplied traits in scope to their respective public variables, so they point at host memory
	void BindPublicVariables(TConstArrayView<UE::AnimNext::IDataInterfaceHost*> InHosts);

	// Unbind any public variables that were pointing at host memory and re-point them at the internal defaults
	void UnbindPublicVariables();

	// The entry point in Graph that this instance corresponds to 
	FName EntryPoint;

	// Hard reference to the graph instance data, we own it
	UE::AnimNext::FTraitPtr GraphInstancePtr;

	// The module instance that owns the root, us and the components
	FAnimNextModuleInstance* ModuleInstance = nullptr;

	// The graph instance that owns us
	FAnimNextGraphInstance* ParentGraphInstance = nullptr;

	// The root graph instance that owns us and the components
	FAnimNextGraphInstance* RootGraphInstance = nullptr;

#if WITH_EDITORONLY_DATA
	struct FCachedVariableBinding
	{
		FName VariableName;
		uint8* Memory = nullptr;
	};

	// Cached public variable bindings used to correctly thaw instances with input pin bindings
	TArray<FCachedVariableBinding> CachedVariableBindings;
#endif

	// Graph instance components that persist from update to update
	GraphInstanceComponentMapType Components;

	// The current state of public variable bindings to the host
	enum class EPublicVariablesState : uint8
	{
		None,		// No public variables present
		Unbound,	// Present, but currently unbound
		Bound		// Present and bound
	};

	EPublicVariablesState PublicVariablesState;

	// Whether or not this graph has updated once
	bool bHasUpdatedOnce : 1 = false;

	friend UAnimNextAnimationGraph;			// The graph is the one that allocates instances
	friend FRigUnit_AnimNextGraphEvaluator;	// We evaluate the instance
	friend UE::AnimNext::FExecutionContext;
	friend FAnimNextGraphInstancePtr;
	friend UE::AnimNext::FTraitStackBinding;
};

template<>
struct TStructOpsTypeTraits<FAnimNextGraphInstance> : public TStructOpsTypeTraitsBase2<FAnimNextGraphInstance>
{
	enum
	{
		WithAddStructReferencedObjects = true,
		WithCopy = false,
	};
};

//////////////////////////////////////////////////////////////////////////

template<class ComponentType>
ComponentType& FAnimNextGraphInstance::GetComponent()
{
	const FName ComponentName = ComponentType::StaticComponentName();
	const int32 ComponentNameHash = GetTypeHash(ComponentName);

	if (UE::AnimNext::FGraphInstanceComponent* Component = TryGetComponent(ComponentNameHash, ComponentName))
	{
		return *static_cast<ComponentType*>(Component);
	}

	return static_cast<ComponentType&>(AddComponent(ComponentNameHash, ComponentName, MakeShared<ComponentType>(*this)));
}

template<class ComponentType>
ComponentType* FAnimNextGraphInstance::TryGetComponent()
{
	const FName ComponentName = ComponentType::StaticComponentName();
	const int32 ComponentNameHash = GetTypeHash(ComponentName);

	return static_cast<ComponentType*>(TryGetComponent(ComponentNameHash, ComponentName));
}

template<class ComponentType>
const ComponentType* FAnimNextGraphInstance::TryGetComponent() const
{
	const FName ComponentName = ComponentType::StaticComponentName();
	const int32 ComponentNameHash = GetTypeHash(ComponentName);

	return static_cast<ComponentType*>(TryGetComponent(ComponentNameHash, ComponentName));
}
