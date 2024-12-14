// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "UObject/ObjectMacros.h"
#include "RBF/RBFSolver.h"

#include "DNACommon.generated.h"

UENUM(BlueprintType)
enum class EArchetype: uint8
{
	Asian,
	Black,
	Caucasian,
	Hispanic,
	Alien,
	Other
};

UENUM(BlueprintType)
enum class EGender: uint8
{
	Male,
	Female,
	Other
};

UENUM(BlueprintType)
enum class ETranslationUnit: uint8
{
	CM,
	M
};

UENUM(BlueprintType)
enum class ERotationUnit: uint8
{
	Degrees,
	Radians
};

UENUM(BlueprintType)
enum class EDirection: uint8
{
	Left,
	Right,
	Up,
	Down,
	Front,
	Back
};

UENUM(BlueprintType)
enum class ETranslationRepresentation : uint8
{
	Vector
};

UENUM(BlueprintType)
enum class ERotationRepresentation : uint8
{
	EulerAngles,
	Quaternion
};

UENUM(BlueprintType)
enum class EScaleRepresentation : uint8
{
	Vector
};

/*
UENUM(BlueprintType)
enum class ERBFSolverType : uint8
{
	Additive,
	Interpolative
};

UENUM(BlueprintType)
enum class ERBFFunctionType : uint8
{
	Gaussian,
	Exponential,
	Linear,
	Cubic,
	Quintic,
};

UENUM(BlueprintType)
enum class ERBFDistanceMethod : uint8
{
	Euclidean,
	Quaternion,
	SwingAngle,
	TwistAngle,
};

UENUM(BlueprintType)
enum class ERBFNormalizeMethod : uint8
{
	OnlyNormalizeAboveOne,
	AlwaysNormalize
};
*/

UENUM(BlueprintType)
enum class EAutomaticRadius : uint8
{
	On,
	Off
};

UENUM(BlueprintType)
enum class ETwistAxis : uint8
{
	X,
	Y,
	Z
};

UENUM(BlueprintType)
enum class EDNADataLayer : uint8
{
	None,
	Descriptor = 1,
	Definition = 2 | Descriptor,  // Implicitly loads Descriptor
	Behavior = 4 | Definition,  // Implicitly loads Descriptor and Definition
	Geometry = 8 | Definition,  // Implicitly loads Descriptor and Definition
	GeometryWithoutBlendShapes = 16 | Definition,  // Implicitly loads Descriptor and Definition
	MachineLearnedBehavior = 32 | Definition,  // Implicitly loads Definition
	RBFBehavior = 64 | Behavior,  // Implicitly loads Behavior and all body-rig related layers
	All = RBFBehavior | Geometry | MachineLearnedBehavior
};
ENUM_CLASS_FLAGS(EDNADataLayer);

UENUM(BlueprintType)
enum class EActivationFunction : uint8
{
	Linear,
	ReLU,
	LeakyReLU,
	Tanh,
	Sigmoid
};

USTRUCT(BlueprintType)
struct FCoordinateSystem
{
	GENERATED_BODY()

	FCoordinateSystem() : XAxis(), YAxis(), ZAxis()
	{
	}

	FCoordinateSystem(EDirection XAxis, EDirection YAxis, EDirection ZAxis) : XAxis(XAxis), YAxis(YAxis), ZAxis(ZAxis)
	{
	}

	UPROPERTY(BlueprintReadOnly, Category = "RigLogic")
	EDirection XAxis;
	UPROPERTY(BlueprintReadOnly, Category = "RigLogic")
	EDirection YAxis;
	UPROPERTY(BlueprintReadOnly, Category = "RigLogic")
	EDirection ZAxis;
};

USTRUCT(BlueprintType)
struct FMeshBlendShapeChannelMapping
{
	GENERATED_BODY()

	FMeshBlendShapeChannelMapping() : MeshIndex(), BlendShapeChannelIndex()
	{
	}

	FMeshBlendShapeChannelMapping(int32 MeshIndex, int32 BlendShapeChannelIndex) : MeshIndex(MeshIndex), BlendShapeChannelIndex(BlendShapeChannelIndex)
	{
	}

	UPROPERTY(BlueprintReadOnly, Category = "RigLogic")
	int32 MeshIndex;
	UPROPERTY(BlueprintReadOnly, Category = "RigLogic")
	int32 BlendShapeChannelIndex;
};

USTRUCT(BlueprintType)
struct FTextureCoordinate
{
	GENERATED_BODY()

	FTextureCoordinate() : U(), V()
	{
	}

	FTextureCoordinate(float U, float V) : U(U), V(V)
	{
	}

	UPROPERTY(BlueprintReadOnly, Category = "RigLogic")
	float U;
	UPROPERTY(BlueprintReadOnly, Category = "RigLogic")
	float V;
};

USTRUCT(BlueprintType)
struct FVertexLayout
{
	GENERATED_BODY()

	FVertexLayout() : Position(), TextureCoordinate(), Normal()
	{
	}

	FVertexLayout(int32 Position, int32 TextureCoordinate, int32 Normal) : Position(Position), TextureCoordinate(TextureCoordinate), Normal(Normal)
	{
	}

	UPROPERTY(BlueprintReadOnly, Category = "RigLogic")
	int32 Position;
	UPROPERTY(BlueprintReadOnly, Category = "RigLogic")
	int32 TextureCoordinate;
	UPROPERTY(BlueprintReadOnly, Category = "RigLogic")
	int32 Normal;
};
