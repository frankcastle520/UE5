// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuCOE/Nodes/CustomizableObjectNodeMaterialBase.h"
#include "MuCOE/RemapPins/CustomizableObjectNodeRemapPinsByName.h"

#include "CustomizableObjectNodeMaterial.generated.h"

enum class EMaterialParameterType : uint8;
namespace ENodeTitleType { enum Type : int; }

class FArchive;
class SGraphNode;
class SWidget;
class UCustomizableObjectNodeRemapPins;
class UEdGraphPin;
class UMaterial;
class UMaterialInterface;
class UObject;
class UTexture2D;
struct FCustomizableObjectNodeMaterialImage;
struct FCustomizableObjectNodeMaterialScalar;
struct FCustomizableObjectNodeMaterialVector;
struct FEdGraphPinReference;
struct FFrame;
struct FPropertyChangedEvent;


/** Custom remap pins by name action.
 *
 * Remap pins by Texture Parameter Id. */
UCLASS()
class UCustomizableObjectNodeMaterialRemapPinsByName : public UCustomizableObjectNodeRemapPinsByName
{
	GENERATED_BODY()
public:
	virtual bool Equal(const UCustomizableObjectNode& Node, const UEdGraphPin& OldPin, const UEdGraphPin& NewPin) const override;

	virtual void RemapPins(const UCustomizableObjectNode& Node, const TArray<UEdGraphPin*>& OldPins, const TArray<UEdGraphPin*>& NewPins, TMap<UEdGraphPin*, UEdGraphPin*>& PinsToRemap, TArray<UEdGraphPin*>& PinsToOrphan) override;

	bool HasSavedPinData(const UCustomizableObjectNode& Node, const UEdGraphPin &Pin) const;
};


/** Base class for all Material Parameters. */
UCLASS()
class CUSTOMIZABLEOBJECTEDITOR_API UCustomizableObjectNodeMaterialPinDataParameter : public UCustomizableObjectNodePinData
{
	GENERATED_BODY()

public:

	/** Parameter id + layer index */
	UPROPERTY()
	FNodeMaterialParameterId MaterialParameterId;

	/** Texture Parameter Id. */
	UPROPERTY()
	FGuid ParameterId_DEPRECATED;

	/** Returns true if all properties are in its default state. */
	virtual bool IsDefault() const;
};


/** Node pin mode. All pins set to EPinMode::Default will use this this mode. */
UENUM()
enum class ENodePinMode
{
	Mutable UMETA(ToolTip = "All Material Texture Parameters go through Mutable."),
	Passthrough UMETA(ToolTip = "All Material Texture Parameters are not modified by Mutable.")
};


/** Image pin, pin mode. */
UENUM()
enum class EPinMode
{
	Default UMETA(DisplayName = "Node Defined", ToolTip = "Use node's \"Default Texture Parameter Mode\"."),
	Mutable UMETA(ToolTip = "The Material Texture Parameters goes through Mutable."),
	Passthrough UMETA(ToolTip = "The Material Texture Parameters is not modified by Mutable.")
};


/** Image Pin, UV Layout Mode. */
UENUM()
enum class EUVLayoutMode
{
	/** Does not override the UV Index specified in the Material. */
	FromMaterial,
	/* Texture should not be transformed by any layout. These textures will not be reduced automatically for LODs. */
	Ignore,
	/** User specified UV Index. */
	Index
};


/** Enum to FText. */
FText EPinModeToText(EPinMode PinMode);

UCLASS()
class CUSTOMIZABLEOBJECTEDITOR_API UCustomizableObjectNodeMaterial : public UCustomizableObjectNodeMaterialBase
{
public:
	GENERATED_BODY()
	
	// UObject interface.
	virtual void Serialize(FArchive& Ar) override;
	virtual void PostLoad() override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;

	// EdGraphNode interface
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FLinearColor GetNodeTitleColor() const override;
	virtual FText GetTooltipText() const override;
	virtual TSharedPtr<SGraphNode> CreateVisualWidget() override;
	virtual void PinConnectionListChanged(UEdGraphPin* Pin) override;
	virtual void PostPasteNode() override;
	virtual bool CanConnect(const UEdGraphPin* InOwnedInputPin, const UEdGraphPin* InOutputPin, bool& bOutIsOtherNodeBlocklisted, bool& bOutArePinsCompatible) const override;

	// UCustomizableObjectNode interface
	virtual void BackwardsCompatibleFixup(int32 CustomizableObjectCustomVersion) override;
	virtual void AllocateDefaultPins(UCustomizableObjectNodeRemapPins* RemapPins) override;
	virtual bool CanPinBeHidden(const UEdGraphPin& Pin) const override;
	virtual bool HasPinViewer() const override;
	virtual UCustomizableObjectNodeRemapPinsByName* CreateRemapPinsDefault() const override;
	virtual bool ProvidesCustomPinRelevancyTest() const override { return true; }
	virtual bool IsPinRelevant(const UEdGraphPin* Pin) const override;
	virtual bool CustomRemovePin(UEdGraphPin& Pin) override;
	virtual void BreakExistingConnectionsPostConnection(UEdGraphPin* InputPin, UEdGraphPin* OutputPin) override;
	virtual bool IsNodeOutDatedAndNeedsRefresh() override;
	virtual FString GetRefreshMessage() const override;
	virtual TSharedPtr<IDetailsView> CustomizePinDetails(const UEdGraphPin& Pin) const override;
	virtual TArray<FString>* GetEnableTags() override;
	virtual FString GetInternalTagDisplayName() override;
	
	// UCustomizableObjectNodeMaterialBase interface
	virtual TArray<UCustomizableObjectLayout*> GetLayouts() const override;
	virtual UEdGraphPin* OutputPin() const override;
	virtual UMaterialInterface* GetMaterial() const override;
	virtual bool IsReuseMaterialBetweenLODs() const override;
	virtual int32 GetNumParameters(EMaterialParameterType Type) const override;
	virtual FNodeMaterialParameterId GetParameterId(EMaterialParameterType Type, int32 ParameterIndex) const override;
	virtual FName GetParameterName(EMaterialParameterType Type, int32 ParameterIndex) const override;
	virtual int32 GetParameterLayerIndex(EMaterialParameterType Type, int32 ParameterIndex) const override;
	virtual FText GetParameterLayerName(EMaterialParameterType Type, int32 ParameterIndex) const override;
	virtual bool HasParameter(const FNodeMaterialParameterId& ParameterId) const override;
	virtual UEdGraphPin* GetParameterPin(EMaterialParameterType Type, int32 ParameterIndex) const override;
	virtual UEdGraphPin* GetParameterPin(const FNodeMaterialParameterId& ParameterId) const override;
	virtual bool IsImageMutableMode(int32 ImageIndex) const override;
	virtual bool IsImageMutableMode(const UEdGraphPin& Pin) const override;
	virtual UTexture2D* GetImageReferenceTexture(int32 ImageIndex) const override;
	virtual UTexture2D* GetImageValue(int32 ImageIndex) const override;
	virtual int32 GetImageUVLayout(int32 ImageIndex) const override;
	virtual UEdGraphPin* GetMeshPin() const override;
	virtual UEdGraphPin* GetMaterialAssetPin() const override;
	virtual const UCustomizableObjectNodeMaterial* GetMaterialNode() const override;
	virtual bool RealMaterialDataHasChanged() const override;
	virtual FPostImagePinModeChangedDelegate* GetPostImagePinModeChangedDelegate() override;
	
	// Own Interface

	void SetMaterial(UMaterialInterface* InMaterial);

	static bool HasParameter(const UMaterialInterface* InMaterial, const FNodeMaterialParameterId& ParameterId);
	static int32 GetParameterLayerIndex(const UMaterialInterface* InMaterial, EMaterialParameterType Type, int32 ParameterIndex);

	UPROPERTY(EditAnywhere, Category = Tags)
	TArray<FString> Tags;

private:
	/** Set the Pin Mode of a Texture Parameter Pin. */
	void SetImagePinMode(UEdGraphPin& Pin, EPinMode PinMode) const;
	
	/** Delegate called when a Texture Parameter Pin Mode changes. */
	FPostImagePinModeChangedDelegate PostImagePinModeChangedDelegate;

	UPROPERTY(EditAnywhere, Category=CustomizableObject)
	TObjectPtr<UMaterialInterface> Material = nullptr;

	UPROPERTY(EditAnywhere, Category=CustomizableObject, DisplayName = "Default Texture Parameter Mode", Meta = (ToolTip = "All Mateiral Texture Parameters set to \"Node Defined\" will use this mode."))
	ENodePinMode TextureParametersMode = ENodePinMode::Passthrough;

	UPROPERTY()
	int32 MeshComponentIndex_DEPRECATED = 0;

public:
	/** Selects which Mesh component of the Instance this material belongs to */
	UPROPERTY()
	FName MeshComponentName_DEPRECATED;

private:
	/** Materials will be reused between LODs when possible unless explicitly disabled. */
	UPROPERTY(EditAnywhere, Category = CustomizableObject)
	bool bReuseMaterialBetweenLODs = true;

	/** Last static or skeletal mesh connected. Used to remove the callback once disconnected. */
	TWeakObjectPtr<UCustomizableObjectNode> LastMeshNodeConnected;

	/** List of material parameter types that are actually relevant to mutable. */
	static const TArray<EMaterialParameterType> ParameterTypes;

	/** Relates a Parameter id (key) (and layer if is a layered material) to a Pin (value). Only used to improve performance.
	  * If a deprecated pin and a non-deprecated pin have the same Parameter id, this the non-deprecated one prevails. */
	UPROPERTY()
	TMap<FNodeMaterialParameterId, FEdGraphPinReference> PinsParameterMap;
	
	/** Create the pin data of the given parameter type. */
	UCustomizableObjectNodeMaterialPinDataParameter* CreatePinData(EMaterialParameterType Type, int32 ParameterIndex);

	/** Allocate a pin for each parameter of the given type. */
	void AllocateDefaultParameterPins(EMaterialParameterType Type);

	/** Set the default Material from the connected static or skeletal mesh. */
	void SetDefaultMaterial();

	/** Connected NodeStaticMesh or NodeSkeletalMesh Mesh UPROPERTY changed callback function. Sets the default material. */
	UFUNCTION()
	void MeshPostEditChangeProperty(FPostEditChangePropertyDelegateParameters& Parameters);

	/** Format pin name. */
	FName GetPinName(EMaterialParameterType Type, int32 ParameterIndex) const;

	/** Returns the texture coordinate of the given Material Expression. Returns -1 if not found. */
	static int32 GetExpressionTextureCoordinate(UMaterial* Material, const FGuid &ImageId);

	/** Return the Pin Category given the node NodePinMode. */
	static FName NodePinModeToImagePinMode(ENodePinMode NodePinMode);

	/** Return the Pin Category given a PinMode. */
	FName GetImagePinMode(EPinMode PinMode) const;

	/** Return the Pin Category given a Pin. */
	FName GetImagePinMode(const UEdGraphPin& Pin) const;

	/** Get the UV Layout Index defined in the Material. */
	int32 GetImageUVLayoutFromMaterial(int32 ImageIndex) const;
	
	// Deprecated properties
	/** Set all pins to Mutable mode. Even so, each pin can override its behaviour. */
	UPROPERTY()
	bool bDefaultPinModeMutable_DEPRECATED = false;
	
	UPROPERTY()
	TArray<FCustomizableObjectNodeMaterialImage> Images_DEPRECATED;
	
	UPROPERTY()
	TArray<FCustomizableObjectNodeMaterialVector> VectorParams_DEPRECATED;

	UPROPERTY()
	TArray<FCustomizableObjectNodeMaterialScalar> ScalarParams_DEPRECATED;

	UPROPERTY()
	TMap<FGuid, FEdGraphPinReference> PinsParameter_DEPRECATED;
};


/** Additional data for a Material Texture Parameter pin. */
UCLASS()
class CUSTOMIZABLEOBJECTEDITOR_API UCustomizableObjectNodeMaterialPinDataImage : public UCustomizableObjectNodeMaterialPinDataParameter
{
	GENERATED_BODY()

	friend void UCustomizableObjectNodeMaterial::BackwardsCompatibleFixup(int32 CustomizableObjectCustomVersion);
	
public:
	// UObject interface
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual bool CanEditChange(const FProperty* InProperty) const override;

	// UCustomizableObjectNodePinData interface
	virtual void BackwardsCompatibleFixup(int32 CustomizableObjectCustomVersion) override;

	// NodePinDataParameter interface
	/** Virtual function used to copy pin data when remapping pins. */
	virtual void Copy(const UCustomizableObjectNodePinData& Other) override;
	
	// UCustomizableObjectNodeMaterialPinParameter interface
	virtual bool IsDefault() const override;

	// Own interface
	/** Constructor parameters. Should always be called after a NewObject. */
	void Init(UCustomizableObjectNodeMaterial& InNodeMaterial);

	EPinMode GetPinMode() const;

	void SetPinMode(EPinMode InPinMode);
	
private:
	/** Image pin mode. If is not default, overrides the defined node behaviour. */
	UPROPERTY(EditAnywhere, Category = NoCategory, DisplayName = "Texture Parameter Mode")
	EPinMode PinMode = EPinMode::Default;

public:
	constexpr static int32 UV_LAYOUT_IGNORE = -1;

	UPROPERTY(EditAnywhere, Category = NoCategory)
	EUVLayoutMode UVLayoutMode = EUVLayoutMode::FromMaterial;
	
	/** Index of the UV channel that will be used with this image. It is necessary to apply the proper layout transformations to it. */
	UPROPERTY(EditAnywhere, Category = NoCategory, meta = (EditCondition = "UVLayoutMode == EUVLayoutMode::Index", EditConditionHides))
	int32 UVLayout = -2;

	/** Reference Texture used to decide the texture properties of the mutable-generated textures
	* connected to this material. If null, it will try to be guessed at compile time from
	* the graph. */
	UPROPERTY(EditAnywhere, Category = NoCategory) // Required to be EditAnywhere for the selector to work.
	TObjectPtr<UTexture2D> ReferenceTexture = nullptr;

private:
	UPROPERTY()
	TObjectPtr<UCustomizableObjectNodeMaterial> NodeMaterial = nullptr;
};


/** Additional data for a Material Vector Parameter pin. */
UCLASS()
class CUSTOMIZABLEOBJECTEDITOR_API UCustomizableObjectNodeMaterialPinDataVector : public UCustomizableObjectNodeMaterialPinDataParameter
{
	GENERATED_BODY()
};


/** Additional data for a Material Float Parameter pin. */
UCLASS()
class CUSTOMIZABLEOBJECTEDITOR_API UCustomizableObjectNodeMaterialPinDataScalar : public UCustomizableObjectNodeMaterialPinDataParameter
{
	GENERATED_BODY()
};

#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_2
#include "MaterialTypes.h"
#include "MuCOE/CustomizableObjectEditor_Deprecated.h"
#endif
