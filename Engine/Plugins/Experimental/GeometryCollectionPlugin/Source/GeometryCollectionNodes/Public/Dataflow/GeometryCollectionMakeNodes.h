// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once 

#include "CoreMinimal.h"
#include "Dataflow/DataflowEngine.h"
#include "GeometryCollection/ManagedArrayCollection.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "UDynamicMesh.h"
#include "Dataflow/DataflowSelection.h"
#include "Math/MathFwd.h"
#include "DynamicMesh/DynamicMesh3.h"

#include "GeometryCollectionMakeNodes.generated.h"

#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_5
namespace Dataflow = UE::Dataflow;
#else
namespace UE_DEPRECATED(5.5, "Use UE::Dataflow instead.") Dataflow {}
#endif

/**
 *
 * Description for this node
 *
 */
USTRUCT()
struct FMakeLiteralStringDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FMakeLiteralStringDataflowNode, "MakeLiteralString", "Utilities|String", "")

public:
	UPROPERTY(EditAnywhere, Category = "String");
	FString Value = FString("");

	UPROPERTY(meta = (DataflowOutput))
	FString String;

	FMakeLiteralStringDataflowNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterOutputConnection(&String);
	}

	virtual void Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};

/**
 *
 * Description for this node
 *
 */
USTRUCT()
struct FMakePointsDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FMakePointsDataflowNode, "MakePoints", "Generators|Point", "")

public:
	UPROPERTY(EditAnywhere, Category = "Point")
	TArray<FVector> Point;

	UPROPERTY(meta = (DataflowOutput))
	TArray<FVector> Points;

	FMakePointsDataflowNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterOutputConnection(&Points);
	}

	virtual void Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};


UENUM(BlueprintType)
enum class EMakeBoxDataTypeEnum : uint8
{
	Dataflow_MakeBox_DataType_MinMax UMETA(DisplayName = "Min/Max"),
	Dataflow_MakeBox_DataType_CenterSize UMETA(DisplayName = "Center/Size"),
	//~~~
	//256th entry
	Dataflow_Max                UMETA(Hidden)
};

/**
 *
 * Description for this node
 *
 */
USTRUCT()
struct FMakeBoxDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FMakeBoxDataflowNode, "MakeBox", "Generators|Box", "")
	DATAFLOW_NODE_RENDER_TYPE("SurfaceRender",FName("FBox"), "Box")

public:
	UPROPERTY(EditAnywhere, Category = "Box", meta = (DisplayName = "Input Data Type"));
	EMakeBoxDataTypeEnum DataType = EMakeBoxDataTypeEnum::Dataflow_MakeBox_DataType_MinMax;

	UPROPERTY(EditAnywhere, Category = "Box", meta = (DataflowInput, EditCondition = "DataType == EMakeBoxDataTypeEnum::Dataflow_MakeBox_DataType_MinMax", EditConditionHides));
	FVector Min = FVector(0.0);

	UPROPERTY(EditAnywhere, Category = "Box", meta = (DataflowInput, EditCondition = "DataType == EMakeBoxDataTypeEnum::Dataflow_MakeBox_DataType_MinMax", EditConditionHides));
	FVector Max = FVector(10.0);

	UPROPERTY(EditAnywhere, Category = "Box", meta = (DataflowInput, EditCondition = "DataType == EMakeBoxDataTypeEnum::Dataflow_MakeBox_DataType_CenterSize", EditConditionHides));
	FVector Center = FVector(0.0);

	UPROPERTY(EditAnywhere, Category = "Box", meta = (DataflowInput, EditCondition = "DataType == EMakeBoxDataTypeEnum::Dataflow_MakeBox_DataType_CenterSize", EditConditionHides));
	FVector Size = FVector(10.0);

	UPROPERTY(meta = (DataflowOutput));
	FBox Box = FBox(ForceInit);

	FMakeBoxDataflowNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&Min);
		RegisterInputConnection(&Max);
		RegisterInputConnection(&Center);
		RegisterInputConnection(&Size);
		RegisterOutputConnection(&Box);
	}

	virtual void Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};


/**
 *
 * Description for this node
 *
 */
USTRUCT()
struct FMakeSphereDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FMakeSphereDataflowNode, "MakeSphere", "Generators|Sphere", "")

public:
	UPROPERTY(EditAnywhere, Category = "Sphere", meta = (DataflowInput));
	FVector Center = FVector(0.f);

	UPROPERTY(EditAnywhere, Category = "Sphere", meta = (DataflowInput));
	float Radius = 10.f;

	UPROPERTY(meta = (DataflowOutput));
	FSphere Sphere = FSphere(ForceInit);

	FMakeSphereDataflowNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&Center);
		RegisterInputConnection(&Radius);
		RegisterOutputConnection(&Sphere);
	}

	virtual void Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};


/**
 *
 * Description for this node
 *
 */
USTRUCT()
struct FMakeLiteralFloatDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FMakeLiteralFloatDataflowNode, "MakeLiteralFloat", "Math|Float", "")

public:
	UPROPERTY(EditAnywhere, Category = "Float");
	float Value = 0.f;

	UPROPERTY(meta = (DataflowOutput))
	float Float = 0.f;

	FMakeLiteralFloatDataflowNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterOutputConnection(&Float);
	}

	virtual void Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};


/**
 *
 * Description for this node
 *
 */
USTRUCT()
struct FMakeLiteralIntDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FMakeLiteralIntDataflowNode, "MakeLiteralInt", "Math|Int", "")

public:
	UPROPERTY(EditAnywhere, Category = "Int");
	int32 Value = 0;

	UPROPERTY(meta = (DataflowOutput, DisplayName = "Int"))
	int32 Int = 0;

	FMakeLiteralIntDataflowNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterOutputConnection(&Int);
	}

	virtual void Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};

/**
 *
 * Description for this node
 *
 */
USTRUCT()
struct FMakeLiteralBoolDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FMakeLiteralBoolDataflowNode, "MakeLiteralBool", "Math|Boolean", "")

public:
	UPROPERTY(EditAnywhere, Category = "Bool");
	bool Value = false;

	UPROPERTY(meta = (DataflowOutput))
	bool Bool = false;

	FMakeLiteralBoolDataflowNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterOutputConnection(&Bool);
	}

	virtual void Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};


/**
 *
 *
 *
 */
USTRUCT()
struct FMakeLiteralVectorDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FMakeLiteralVectorDataflowNode, "MakeLiteralVector", "Math|Vector", "")

public:
	UPROPERTY(EditAnywhere, Category = "Vector", meta = (DataflowInput));
	float X = float(0.0);

	UPROPERTY(EditAnywhere, Category = "Vector", meta = (DataflowInput));
	float Y = float(0.0);

	UPROPERTY(EditAnywhere, Category = "Vector", meta = (DataflowInput));
	float Z = float(0.0);

	UPROPERTY(meta = (DataflowOutput, DisplayName = "Vector"))
	FVector Vector = FVector(0.0);

	FMakeLiteralVectorDataflowNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&X);
		RegisterInputConnection(&Y);
		RegisterInputConnection(&Z);
		RegisterOutputConnection(&Vector);
	}

	virtual void Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};

/**
 *
 * Description for this node
 *
 */
USTRUCT()
struct FMakeTransformDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FMakeTransformDataflowNode, "MakeTransform", "Generators|Transform", "")

public:

	UPROPERTY(EditAnywhere, Category = "Transform", meta = (DataflowInput, DisplayName = "Translation"));
	FVector InTranslation = FVector(0, 0, 0);

	UPROPERTY(EditAnywhere, Category = "Transform", meta = (DataflowInput, DisplayName = "Rotation"));
	FVector InRotation = FVector(0, 0, 0);

	UPROPERTY(EditAnywhere, Category = "Transform", meta = (DataflowInput, DisplayName = "Scale"));
	FVector InScale = FVector(1, 1, 1);

	UPROPERTY(meta = (DataflowOutput, DisplayName = "Transform"));
	FTransform OutTransform = FTransform::Identity;

	FMakeTransformDataflowNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&InTranslation);
		RegisterInputConnection(&InRotation);
		RegisterInputConnection(&InScale);
		RegisterOutputConnection(&OutTransform);
	}

	virtual void Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};

/**
 *
 *
 *
 */
USTRUCT()
struct FMakeQuaternionDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FMakeQuaternionDataflowNode, "MakeQuaternion", "Math|Vector", "")

public:
	UPROPERTY(EditAnywhere, Category = "Quaternion ", meta = (DataflowInput));
	float X = float(0.0);

	UPROPERTY(EditAnywhere, Category = "Quaternion", meta = (DataflowInput));
	float Y = float(0.0);

	UPROPERTY(EditAnywhere, Category = "Quaternion", meta = (DataflowInput));
	float Z = float(0.0);

	UPROPERTY(EditAnywhere, Category = "Quaternion", meta = (DataflowInput));
	float W = float(0.0);

	UPROPERTY(meta = (DataflowOutput, DisplayName = "Quaternion"))
	FQuat Quaternion = FQuat(ForceInitToZero);

	FMakeQuaternionDataflowNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&X);
		RegisterInputConnection(&Y);
		RegisterInputConnection(&Z);
		RegisterInputConnection(&W);
		RegisterOutputConnection(&Quaternion);
	}

	virtual void Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};

/**
 *
 * Description for this node
 *
 */
USTRUCT()
struct FMakeFloatArrayDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FMakeFloatArrayDataflowNode, "MakeFloatArray", "Math|Float", "")

public:
	/** Number of elements of the array */
	UPROPERTY(EditAnywhere, Category = "Float", meta = (DataflowInput, DisplayName = "Number of Elements", UIMin = "0"));
	int32 NumElements = 1;

	/** Value to initialize the array with */
	UPROPERTY(EditAnywhere, Category = "Float", meta = (DataflowInput));
	float Value = 0.f;

	/** Output float array */
	UPROPERTY(meta = (DataflowOutput))
	TArray<float> FloatArray;

	FMakeFloatArrayDataflowNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&NumElements);
		RegisterInputConnection(&Value);
		RegisterOutputConnection(&FloatArray);
	}

	virtual void Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};

namespace UE::Dataflow
{
	void GeometryCollectionMakeNodes();
}
