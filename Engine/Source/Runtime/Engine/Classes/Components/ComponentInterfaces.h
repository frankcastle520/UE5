// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Containers/Array.h"
#include "PrimitiveComponentId.h"
#include "Templates/RefCounting.h"
#include "Templates/Function.h"

class UWorld;
class FSceneInterface;
class FRegisterComponentContext;
class UStaticMesh;
class FPrimitiveSceneProxy;
class UMaterialInterface;
class HHitProxy;

struct FPrimitiveLODStats
{
	int32 LODIndex = 0;
	uint32 Sections = 1;
	uint32 Triangles = 0;
	bool bIsOptionalLOD = false;
	bool bIsAvailable = true;
	SIZE_T TotalResourceSize = 0;
	TArray<uint16> MaterialIndices;
	
	FPrimitiveLODStats(int32 InLOD) :
		LODIndex(InLOD)
	{
	}

	FPrimitiveLODStats(const FPrimitiveLODStats& Other) = default;
	FPrimitiveLODStats(FPrimitiveLODStats&& Other) = default;

	FPrimitiveLODStats& operator=(const FPrimitiveLODStats& RHS) = default;
	FPrimitiveLODStats& operator=(FPrimitiveLODStats&& RHS) = default;

	FORCEINLINE int32 GetDrawCount() const
	{
		return Sections * MaterialIndices.Num();
	}
};

/** 
* Structure used to report some primitive stats in debugging tools
*/
struct FPrimitiveStats
{
	TArray<FPrimitiveLODStats> LODStats;
};

struct FComponentInterfaceImplementation
{
	UClass*	Class;
	TFunction<void*(UObject*)> Resolver;
};

class IPrimitiveComponent
{
public:
	virtual bool IsRenderStateCreated() const = 0;
	virtual bool IsRenderStateDirty() const = 0;	
	virtual bool ShouldCreateRenderState() const = 0;
	virtual bool IsRegistered() const = 0;
	virtual bool IsUnreachable() const = 0;
	virtual UWorld* GetWorld() const = 0;
	virtual FSceneInterface* GetScene() const = 0;
	virtual FPrimitiveSceneProxy* GetSceneProxy() const = 0;
	virtual void GetUsedMaterials(TArray<UMaterialInterface*>& OutMaterials, bool bGetDebugMaterials = false) const = 0;		
	virtual void MarkRenderStateDirty() = 0;
	virtual void DestroyRenderState() = 0;
	virtual void CreateRenderState(FRegisterComponentContext* Context) = 0;	
	virtual FString GetName() const = 0;
	virtual FString GetFullName() const = 0;
	virtual FTransform GetTransform() const = 0;
	virtual FBoxSphereBounds GetBounds() const = 0;
	virtual float GetLastRenderTimeOnScreen() const = 0;
	virtual void GetPrimitiveStats(FPrimitiveStats& PrimitiveStats) const = 0;
	virtual UObject*	GetUObject() = 0;
	virtual const UObject*	GetUObject() const = 0;
	virtual void PrecachePSOs() = 0;

	// helper to obtain typed UObjects 
	template<class T> 
	inline const T* GetUObject() const { return Cast<T>(GetUObject()); }

	template<class T> 
	inline T* GetUObject() { return Cast<T>(GetUObject()); }

	virtual UObject* GetOwner() const = 0;

	// helper to have typed owners
	template<class T> 
	inline T* GetOwner() { return Cast<T>(GetOwner()); }

	virtual FString GetOwnerName() const = 0;
	virtual FPrimitiveSceneProxy* CreateSceneProxy() = 0;
#if WITH_EDITOR
	virtual HHitProxy* CreateMeshHitProxy(int32 SectionIndex, int32 MaterialIndex) = 0;
#endif
	virtual HHitProxy* CreatePrimitiveHitProxies(TArray<TRefCountPtr<HHitProxy> >& OutHitProxies) = 0;
	
	ENGINE_API static void AddImplementer(const FComponentInterfaceImplementation& Implementer);

protected:

	template<class T> 
	friend class TComponentInterfaceIterator;

	ENGINE_API static TArray<FComponentInterfaceImplementation> Implementers;
};

class IStaticMeshComponent
{
public:
#if WITH_EDITOR
	virtual void OnMeshRebuild(bool bRenderDataChanged) = 0;
	virtual void PreStaticMeshCompilation() = 0;
	virtual void PostStaticMeshCompilation() = 0;
#endif
	virtual UStaticMesh* GetStaticMesh() const = 0;

	virtual IPrimitiveComponent* GetPrimitiveComponentInterface() = 0;		
	inline const IPrimitiveComponent* GetPrimitiveComponentInterface() const
	{
		// use the non-const version and return it as a const object to avoid duplicating the code in implementers
		return (const_cast<IStaticMeshComponent*>(this))->GetPrimitiveComponentInterface();
	}
	
	ENGINE_API static void AddImplementer(const FComponentInterfaceImplementation& Implementer);

protected:

	template<class T> 
	friend class TComponentInterfaceIterator;

	ENGINE_API static TArray<FComponentInterfaceImplementation> Implementers;
	
};


#pragma region HelperMacros

// These macros are intended to allow implementing an interface with same memory footprint/performance as inheriting from 
// the abstract base class, but without mixing the interface methods with the class methods. 
//
// It declares a member for the interface and utility functions in the host class to get the host ptr from the interface instance. 
// And thus allow declaration of the interface inside a class without requiring the interface to host a back ptr to it's owner. 
// 
// 
// Example...
// 
//		class FActorSomeInteface : ISomeInterface
//		{
//			// Nothing but the overrides
//			virtual void OverrideSomething() override;
//		};
//		
//		class UHostClass
//		{
//			UE_ComponentInterfaceDeclaration(SomeInterface, FActor);
//		}
// 
//	    void FActorSomeInterface::OverrideSomething()
//		{
//			UHostClass::GetHostClass(this)->OverrideSomethingImplementer();
//		}
//



#define UE_DECLARE_COMPONENT_INTERFACE_INTERNAL(actorcomponenttype, componentinterfacetype, actorcomponentinterfacetype, actorcomponentinterfacemember, interfacename )\
	public:\
		componentinterfacetype* Get##interfacename ##Interface() const { return (componentinterfacetype*)&actorcomponentinterfacemember; } \
	protected: \
		actorcomponentinterfacetype	actorcomponentinterfacemember;\
		\
		static actorcomponenttype* Get##interfacename(actorcomponentinterfacetype* InImpl)\
		{\
			return (actorcomponenttype*)(((size_t)InImpl) - offsetof(actorcomponenttype, actorcomponentinterfacemember));\
		}\
		static const actorcomponenttype* Get##interfacename(const actorcomponentinterfacetype* InImpl)\
		{\
			return (const actorcomponenttype*)(((size_t)InImpl) - offsetof(actorcomponenttype, actorcomponentinterfacemember));\
		}\
		friend class actorcomponentinterfacetype;


#define UE_DECLARE_COMPONENT_INTERFACE(name, baseprefix)  UE_DECLARE_COMPONENT_INTERFACE_INTERNAL(U##name, I##name , PREPROCESSOR_JOIN(PREPROCESSOR_JOIN(baseprefix, name), Interface), PREPROCESSOR_JOIN(name, Interface), name)

#define UE_DECLARE_COMPONENT_ACTOR_INTERFACE(name)  UE_DECLARE_COMPONENT_INTERFACE(name, FActor)

#pragma endregion