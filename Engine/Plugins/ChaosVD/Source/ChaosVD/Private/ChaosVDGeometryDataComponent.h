// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "ChaosVDExtractedGeometryDataHandle.h"
#include "InstancedStaticMeshDelegates.h"
#include "Chaos/ImplicitFwd.h"
#include "Containers/Array.h"
#include "Containers/Map.h"
#include "Chaos/ImplicitObject.h"
#include "DataWrappers/ChaosVDCollisionDataWrappers.h"
#include "HAL/Platform.h"
#include "UObject/Interface.h"
#include "UObject/StrongObjectPtr.h"
#include "Templates/SharedPointer.h"
#include "Components/MeshComponent.h"

#include "ChaosVDGeometryDataComponent.generated.h"

class FChaosVDGeometryBuilder;
class FChaosVDScene;
class IChaosVDGeometryComponent;
class UChaosVDInstancedStaticMeshComponent;
class UChaosVDStaticMeshComponent;
class UMaterialInstanceDynamic;

struct FChaosVDExtractedGeometryDataHandle;
struct FChaosVDParticleDataWrapper;

namespace Chaos
{
	class FImplicitObject;
}

DECLARE_MULTICAST_DELEGATE_OneParam(FChaosVDMeshReadyDelegate, IChaosVDGeometryComponent&)
DECLARE_MULTICAST_DELEGATE_OneParam(FChaosVDMeshComponentEmptyDelegate, UMeshComponent*)

UENUM()
enum class EChaosVDMaterialType
{
	SMOpaque,
	SMTranslucent,
	ISMCOpaque,
	ISMCTranslucent
};

UENUM()
enum class EChaosVDMeshComponent
{
	Invalid,
	Static,
	InstancedStatic,
	Dynamic
};

/** Struct holding the a minimum amount of data about a Implicit object to be shown in the details panel */
USTRUCT()
struct FChaosVDImplicitObjectBasicView
{
	GENERATED_BODY()

	/** Geometry type name*/
	UPROPERTY(VisibleAnywhere, Category="Recorded GeometryData")
	FName ImplicitObjectType;

	/** Index of the Shape Instance data for this geometry in the Shape Instance data array */
	UPROPERTY(VisibleAnywhere, Category="Recorded GeometryData")
	int32 ShapeInstanceIndex = INDEX_NONE;

	/** True if this is the root implicit object */
	UPROPERTY(VisibleAnywhere, Category="Recorded GeometryData")
	bool bIsRootObject = false;

	/** If this is a transformed implicit, this will contain the recorded relative transform */
	UPROPERTY(VisibleAnywhere, Category="Recorded GeometryData")
	FTransform RelativeTransform;
};

/** Struct holding the state of a mesh instance - Is separated from the Mesh instance class so we can show the data in the Details panel */
USTRUCT()
struct FChaosVDMeshDataInstanceState
{
	GENERATED_BODY()

	/** Recorded Shape instance Data */
	UPROPERTY(VisibleAnywhere, Category="Recorded GeometryData")
	FChaosVDShapeCollisionData CollisionData;

	/** Minimum set of data about the recorded implicit object */
	UPROPERTY(VisibleAnywhere, Category="Recorded GeometryData")
	FChaosVDImplicitObjectBasicView ImplicitObjectInfo;

	/* CVD Debug - Current world transform used to render this Mesh */
	UPROPERTY(VisibleAnywhere, Category="CVD GeometryData", meta=(EditCondition=bShowCVDDebugData, EditConditionHides))
	FTransform CurrentWorldTransform;

	/* CVD Debug - Current mesh component type to render this Mesh */
	UPROPERTY(VisibleAnywhere, Category="CVD GeometryData", meta=(EditCondition=bShowCVDDebugData, EditConditionHides))
	EChaosVDMeshComponent MeshComponentType = EChaosVDMeshComponent::Invalid;

	/* CVD Debug - Pointer to the mesh component used to render this Mesh */
	UPROPERTY()
	TObjectPtr<UMeshComponent> MeshComponent;

	/* CVD Debug - Instance index of mesh component used to render this Mesh */
	UPROPERTY(VisibleAnywhere, Category="CVD GeometryData", meta=(EditCondition=bShowCVDDebugData, EditConditionHides))
	int32 MeshInstanceIndex = INDEX_NONE;

	/* CVD Debug - Color used to render this mesh */
	UPROPERTY(VisibleAnywhere, Category="CVD GeometryData", meta=(EditCondition=bShowCVDDebugData, EditConditionHides))
	FLinearColor CurrentGeometryColor = FLinearColor(ForceInitToZero);

	/* CVD Debug - Id of the particle this geometry belongs */
	UPROPERTY(VisibleAnywhere, Category="CVD GeometryData", meta=(EditCondition=bShowCVDDebugData, EditConditionHides))
	int32 OwningParticleID = INDEX_NONE;

	/* CVD Debug - Id of the solver this geometry belongs */
	UPROPERTY(VisibleAnywhere, Category="CVD GeometryData", meta=(EditCondition=bShowCVDDebugData, EditConditionHides))
	int32 OwningSolverID = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, Category="CVD GeometryData", meta=(EditCondition=bShowCVDDebugData, EditConditionHides))
	bool bIsVisible = true;

	UPROPERTY(VisibleAnywhere, Category="CVD GeometryData", meta=(EditCondition=bShowCVDDebugData, EditConditionHides))
	bool bIsSelected = false;

	UPROPERTY(EditAnywhere, Category="CVD GeometryData")
	bool bShowCVDDebugData = false;
};

/** Handle that provides access to a specific mesh instance on a CVD Mesh component (instanced or static)*/
class FChaosVDMeshDataInstanceHandle : public TSharedFromThis<FChaosVDMeshDataInstanceHandle>
{
public:
	explicit FChaosVDMeshDataInstanceHandle(int32 InInstanceIndex, UMeshComponent* InMeshComponent, int32 InParticleID, int32 InSolverID);

	/** Returns the Particle ID of the particle owning this mesh instance */
	int32 GetOwningParticleID() const { return InstanceState.OwningParticleID; }

	/** Returns the Solver ID of the particle owning this mesh instance */
	int32 GetOwningSolverID() const { return InstanceState.OwningSolverID; }

	/** Applies the provided world transform to the mesh instance this handle represents
	 * @param InTransform Transform to apply
	 */
	void SetWorldTransform(const FTransform& InTransform);

	/** Returns the world transform of the mesh instance this handle represents */
	const FTransform& GetWorldTransform() const { return InstanceState.CurrentWorldTransform; }

	/** Sets the Geometry Handle used to create the mesh instance this handle represents */
	void SetGeometryHandle(const TSharedPtr<FChaosVDExtractedGeometryDataHandle>& InHandle);

	/** Returns the geometry handle used to create the mesh instance this handle represents */
	const TSharedPtr<FChaosVDExtractedGeometryDataHandle>& GetGeometryHandle() const { return ExtractedGeometryHandle; }

	/** Applies to the provided color to the mesh instance this handle represents */
	void SetInstanceColor(const FLinearColor& NewColor);

	/** Returns the current color of the mesh instance this handle represents */
	FLinearColor GetInstanceColor() const { return InstanceState.CurrentGeometryColor; }
	
	/** Applies to the provided shape collision data to the mesh instance this handle represents */
	void UpdateMeshComponentForCollisionData(const FChaosVDShapeCollisionData& InCollisionData);

	/** Returns the mesh component used to render the mesh instance this handle represents */
	UMeshComponent* GetMeshComponent() const { return InstanceState.MeshComponent; }

	/** Returns the instance index of the mesh instance this handle represents */
	int32 GetMeshInstanceIndex() const { return InstanceState.MeshInstanceIndex; }

	/** Returns the type of the component used to render the mesh instance this handle represents */
	EChaosVDMeshComponent GetMeshComponentType() const { return InstanceState.MeshComponentType; }

	/** Sets a Ptr to the geometry builder used to generate and manage the geometry/mesh components this handle represents */
	void SetGeometryBuilder(const TWeakPtr<FChaosVDGeometryBuilder>& InGeometryBuilder) { GeometryBuilderInstance = InGeometryBuilder; }
	TWeakPtr<FChaosVDGeometryBuilder> GetGeometryBuilder() { return GeometryBuilderInstance; }

	/** Marks this mesh instance as selected. Used to handle Selection in Editor */
	void SetIsSelected(bool bInIsSelected);

	/** Sets the visibility of this mesh instance */
	void SetVisibility(bool bInIsVisible);

	/** Returns the current visibility state this mesh instance */
	bool GetVisibility() const { return InstanceState.bIsVisible; }

	/** Applies a new shape collision data to this mesh instance */
	void SetGeometryCollisionData(const FChaosVDShapeCollisionData& InCollisionData);

	/** Returns the current shape collision data to this mesh instance */
	FChaosVDShapeCollisionData& GetGeometryCollisionData() { return InstanceState.CollisionData; }

	/** Handles a mesh instance index update reported by the mesh component used to render this mesh instance */
	void HandleInstanceIndexUpdated(TArrayView<const FInstancedStaticMeshDelegates::FInstanceIndexUpdateData> InIndexUpdates);

	FChaosVDMeshDataInstanceState& GetState() { return InstanceState; }

	friend uint32 GetTypeHash(const FChaosVDMeshDataInstanceHandle& Handle)
	{
		const uint32 GeometryHandleHash = Handle.ExtractedGeometryHandle ? GetTypeHash(Handle.ExtractedGeometryHandle->GetGeometryKey()) : 0;
		const uint32 MeshComponentHandleHash = HashCombine(HashCombine(Handle.GetOwningParticleID(), Handle.GetMeshInstanceIndex()), static_cast<uint32>(Handle.GetMeshComponentType()));
		return HashCombine(GeometryHandleHash, MeshComponentHandleHash);
	}

	/** Used only for debugging purposes - It will be set to true if we received new Shape Instance data but the Shape Index for the implicit object we represent is not valid */
	bool bFailedToUpdateShapeInstanceData = false; 

private:

	/** Sets the mesh component used to render the mesh instance this handle represents */
	void SetMeshComponent(UMeshComponent* NewComponent) { InstanceState.MeshComponent = NewComponent; }

	/** Sets the mesh instance index of the mesh instance this handle represents */
	void SetMeshInstanceIndex(int32 NewIndex) { InstanceState.MeshInstanceIndex = NewIndex; }

	FChaosVDMeshDataInstanceState InstanceState;

	TSharedPtr<FChaosVDExtractedGeometryDataHandle> ExtractedGeometryHandle;

	TWeakPtr<FChaosVDGeometryBuilder> GeometryBuilderInstance = nullptr;

	friend UChaosVDInstancedStaticMeshComponent;
	friend UChaosVDStaticMeshComponent;
};

enum class EChaosVDMeshAttributesFlags : uint8
{
	None = 0,
	MirroredGeometry = 1 << 0,
	TranslucentGeometry = 1 << 1
};

ENUM_CLASS_FLAGS(EChaosVDMeshAttributesFlags)

UINTERFACE()
class UChaosVDGeometryComponent : public UInterface
{
	GENERATED_BODY()
};

/**
 * Interface with a default implementation for any Geometry component that
 * contains CVD data
 */
class IChaosVDGeometryComponent
{
	GENERATED_BODY()

public:

	/** Returns the Geometry Handle used to identify the geometry data this component represents */
	virtual uint32 GetGeometryKey() const PURE_VIRTUAL(IChaosVDGeometryDataComponent::GetGeometryKey, return 0;);

	/** Returns the CVD Mesh Data Instance handle for the provided Instance index */
	virtual TSharedPtr<FChaosVDMeshDataInstanceHandle> GetMeshDataInstanceHandle(int32 InstanceIndex) const PURE_VIRTUAL(IChaosVDGeometryDataComponent::GetMeshDataInstanceHandle, return nullptr;);

	/** Returns all the CVD Mesh Data Instance handles this component is rendering */
	virtual TArrayView<TSharedPtr<FChaosVDMeshDataInstanceHandle>> GetMeshDataInstanceHandles() PURE_VIRTUAL(IChaosVDGeometryDataComponent::GetMeshDataInstanceHandles, return TArrayView<TSharedPtr<FChaosVDMeshDataInstanceHandle>>(););

	/**
	 * Add a new instance to this mesh component
	 * @param InstanceTransform The transform the new instance will use
	 * @param bIsWorldSpace True if the provide transform is in world space
	 * @param InGeometryHandle The CVD Geometry Handle with data about the geometry instantiate
	 * @param ParticleID Particle ID owning this geometry
	 * @param SolverID Solver ID of the particle owning this geometry
	 * @return CVD Mesh instance handle that provides access to this component and specific instance, allowing manipulation of it
	 */
	virtual TSharedPtr<FChaosVDMeshDataInstanceHandle> AddMeshInstance(const FTransform InstanceTransform, bool bIsWorldSpace, const TSharedPtr<FChaosVDExtractedGeometryDataHandle>& InGeometryHandle, int32 ParticleID, int32 SolverID) PURE_VIRTUAL(IChaosVDGeometryDataComponent::AddMeshInstance, return nullptr;);

	/**
	 * Adds a new instance to this mesh component, but using an existing Mesh Data Handle instead of creating a new one
	 * @param MeshDataHandle Handle that will provide access to this mesh instance
	 * @param InstanceTransform The transform the new instance will use
	 * @param bIsWorldSpace True if the provide transform is in world space
	 * @param InGeometryHandle The CVD Geometry Handle with data about the geometry instantiate
	 * @param ParticleID Particle ID owning this geometry
	 * @param SolverID Solver ID of the particle owning this geometry
	 */
	virtual void AddMeshInstanceForHandle(TSharedPtr<FChaosVDMeshDataInstanceHandle> MeshDataHandle, const FTransform InstanceTransform, bool bIsWorldSpace, const TSharedPtr<FChaosVDExtractedGeometryDataHandle>& InGeometryHandle, int32 ParticleID, int32 SolverID) PURE_VIRTUAL(IChaosVDGeometryDataComponent::AddMeshInstanceForHandle);

	/**
	 * Removes the instance the provided handle represents
	 * @param InHandleToRemove 
	 */
	virtual void RemoveMeshInstance(TSharedPtr<FChaosVDMeshDataInstanceHandle> InHandleToRemove) PURE_VIRTUAL(IChaosVDGeometryDataComponent::RemoveMeshInstance);

	/** True if the mesh this component represents is ready for use */
	virtual bool IsMeshReady() const  PURE_VIRTUAL(IChaosVDGeometryDataComponent::IsMeshReady, return false;);

	/** Sets if the mesh this component represents is ready for use or not */
	virtual void SetIsMeshReady(bool bIsReady) PURE_VIRTUAL(IChaosVDGeometryDataComponent::SetIsMeshReady);
	
	/** Triggers when the mesh this component represents is ready */
	virtual FChaosVDMeshReadyDelegate* OnMeshReady() PURE_VIRTUAL(IChaosVDGeometryDataComponent::OnMeshReady, return nullptr;);

	/** Triggers when the component does not have any instance to render. Used to allow it to return to the mesh component tool for future re-use*/
	virtual FChaosVDMeshComponentEmptyDelegate* OnComponentEmpty() PURE_VIRTUAL(IChaosVDGeometryDataComponent::OnComponentEmpty, return nullptr;);

	/** Updates the visibility of this component based on the stored CVD data*/
	virtual void UpdateInstanceVisibility(const TSharedPtr<FChaosVDMeshDataInstanceHandle>& InInstanceHandle, bool bIsVisible) PURE_VIRTUAL(IChaosVDGeometryDataComponent::UpdateInstanceVisibility);

	/** Changes the selection state of the provided instance - Used for Selection in Editor */
	virtual void SetIsSelected(const TSharedPtr<FChaosVDMeshDataInstanceHandle>& InInstanceHandle, bool bIsSelected) PURE_VIRTUAL(IChaosVDGeometryDataComponent::SetIsSelected);

	/** Updates the colors of this component based on the stored CVD data */
	virtual void UpdateInstanceColor(const TSharedPtr<FChaosVDMeshDataInstanceHandle>& InInstanceHandle, FLinearColor NewColor) PURE_VIRTUAL(IChaosVDGeometryDataComponent::UpdateInstanceColor);

	/** Updates the colors of this component based on the stored CVD data */
	virtual void UpdateInstanceWorldTransform(const TSharedPtr<FChaosVDMeshDataInstanceHandle>& InInstanceHandle, const FTransform& InTransform) PURE_VIRTUAL(IChaosVDGeometryDataComponent::UpdateInstanceWorldTransform);

	/** Sets the CVD Mesh Attribute flags this component is compatible with*/
	virtual void SetMeshComponentAttributeFlags(EChaosVDMeshAttributesFlags Flags) PURE_VIRTUAL(IChaosVDGeometryDataComponent::SetMeshComponentAttributeFlags);
	
	/** Returns the CVD Mesh Attribute flags this component is compatible with*/
	virtual EChaosVDMeshAttributesFlags GetMeshComponentAttributeFlags() const PURE_VIRTUAL(IChaosVDGeometryDataComponent::GetMeshComponentAttributeFlags, return EChaosVDMeshAttributesFlags::None;);

	/** Resets the state of this mesh component, so it can be re-used later on */
	virtual void Reset() PURE_VIRTUAL(IChaosVDGeometryDataComponent::Reset);

	virtual void Initialize() PURE_VIRTUAL(IChaosVDGeometryDataComponent::Initialize);
	
	/** Sets a Ptr to the geometry builder used to generate and manage the geometry/mesh components */
	virtual void SetGeometryBuilder(TWeakPtr<FChaosVDGeometryBuilder> GeometryBuilder) PURE_VIRTUAL(IChaosVDGeometryDataComponent::SetGeometryBuilder);

	virtual EChaosVDMaterialType GetMaterialType() const PURE_VIRTUAL(IChaosVDGeometryDataComponent::GetMaterialType,  return EChaosVDMaterialType::SMOpaque;);
};

class FChaosVDGeometryComponentUtils
{
public:
	/** Finds and updates the Shape data using the provided array as source*/
	static void UpdateCollisionDataFromShapeArray(const TArray<FChaosVDShapeCollisionData>& InShapeArray, const TSharedRef<FChaosVDMeshDataInstanceHandle>& InInstanceHandle);

	/** Calculates and updates the color used to render the mesh represented by the provided handle, based on the particle state */
	static void UpdateMeshColor(const TSharedPtr<FChaosVDMeshDataInstanceHandle>& InInstanceHandle, const FChaosVDParticleDataWrapper& InParticleData, bool bIsServer);

	/** Calculates the correct visibility state based on the particle state, and applies it to the mesh instance the provided handle represents */
	static void UpdateMeshVisibility(const TSharedPtr<FChaosVDMeshDataInstanceHandle>& InInstanceHandle, const FChaosVDParticleDataWrapper& InParticleData, bool bIsActive);

	/** Returns the material to use as a base to create material instances for the provided type */
	static UMaterialInterface* GetBaseMaterialForType(EChaosVDMaterialType Type);

	/** Returns the correct material type to use based on the provided Component type and Mesh Attributes */
	template<typename TComponent>
	static EChaosVDMaterialType GetMaterialTypeForComponent(EChaosVDMeshAttributesFlags MeshAttributes);

private:
	/** Returns the color that needs to be used to present the provided particle data based on its state and current selected options */
	static FLinearColor GetGeometryParticleColor(const TSharedPtr<FChaosVDExtractedGeometryDataHandle>& InGeometryHandle, const FChaosVDParticleDataWrapper& InParticleData, bool bIsServer);
};


template <typename TComponent>
EChaosVDMaterialType FChaosVDGeometryComponentUtils::GetMaterialTypeForComponent(EChaosVDMeshAttributesFlags MeshAttributes)
{
	constexpr bool bIsInstancedMeshComponent = std::is_base_of_v<UInstancedStaticMeshComponent, TComponent>;
	if (EnumHasAnyFlags(MeshAttributes, EChaosVDMeshAttributesFlags::TranslucentGeometry))
	{
		return bIsInstancedMeshComponent ? EChaosVDMaterialType::ISMCTranslucent : EChaosVDMaterialType::SMTranslucent;
	}
	else
	{
		return bIsInstancedMeshComponent ? EChaosVDMaterialType::ISMCOpaque : EChaosVDMaterialType::SMOpaque;
	}
}

UINTERFACE()
class UChaosVDGeometryOwnerInterface : public UInterface
{
	GENERATED_BODY()
};

/**
 * Interface with for any CVD object that owns and handles Geometry generated from recorded data.
 */
class IChaosVDGeometryOwnerInterface
{
	GENERATED_BODY()
public:
	virtual void SetSelectedMeshInstance(const TWeakPtr<FChaosVDMeshDataInstanceHandle>& InMeshInstanceToSelect) PURE_VIRTUAL(IChaosVDGeometryOwnerInterface::SetSelectedMeshInstance);
	virtual TWeakPtr<FChaosVDMeshDataInstanceHandle> GetSelectedMeshInstance() const PURE_VIRTUAL(IChaosVDGeometryOwnerInterface::GetSelectedMeshInstance, return nullptr;);
	virtual TConstArrayView<TSharedPtr<FChaosVDMeshDataInstanceHandle>> GetMeshInstances() const PURE_VIRTUAL(IChaosVDGeometryOwnerInterface::GetMeshInstances, return TConstArrayView<TSharedPtr<FChaosVDMeshDataInstanceHandle>>(););
	virtual void HandleNewGeometryLoaded(uint32 GeometryID, const Chaos::FConstImplicitObjectPtr& InGeometryData) PURE_VIRTUAL(IChaosVDGeometryOwnerInterface::HandleGeometryUpdated);
};

namespace Chaos::VisualDebugger
{
	void SelectParticleWithGeometryInstance(const TSharedRef<FChaosVDScene>& InScene, IChaosVDGeometryOwnerInterface* GeometryOwner, const TSharedPtr<FChaosVDMeshDataInstanceHandle>& InMeshDataHandle);
}
