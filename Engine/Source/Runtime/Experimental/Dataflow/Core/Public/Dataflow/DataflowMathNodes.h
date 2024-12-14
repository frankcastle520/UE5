// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ChaosLog.h"
#include "CoreMinimal.h"
#include "Dataflow/DataflowConnection.h"
#include "Dataflow/DataflowNodeParameters.h"
#include "Dataflow/DataflowNode.h"

#include "DataflowMathNodes.generated.h"

//--------------------------------------------------------------------------
//
// Trigonometric nodes
//
//--------------------------------------------------------------------------

#define DATAFLOW_MATH_NODES_CATEGORY "Math|Scalar"

/** One input operators base class */
USTRUCT()
struct FDataflowMathOneInputOperatorNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(EditAnywhere, Category = Operands, meta = (DataflowInput));
	FDataflowNumericTypes A;

	UPROPERTY(meta = (DataflowOutput))
	FDataflowNumericTypes Result;

public:
	FDataflowMathOneInputOperatorNode() {};
	FDataflowMathOneInputOperatorNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid());
	virtual void Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

protected:
	void RegisterInputsAndOutputs();
	virtual double ComputeResult(UE::Dataflow::FContext& Context, double InA) const { ensure(false); return 0.0; };
};

/** Two inputs operators base class */
USTRUCT()
struct FDataflowMathTwoInputsOperatorNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(EditAnywhere, Category = Operands, meta = (DataflowInput));
	FDataflowNumericTypes A;

	UPROPERTY(EditAnywhere, Category = Operands, meta = (DataflowInput));
	FDataflowNumericTypes B;

	UPROPERTY(meta = (DataflowOutput))
	FDataflowNumericTypes Result;

public:
	FDataflowMathTwoInputsOperatorNode() {};
	FDataflowMathTwoInputsOperatorNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid());
	virtual void Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

protected:
	void RegisterInputsAndOutputs();
	virtual double ComputeResult(UE::Dataflow::FContext& Context, double InA, double InB) const { ensure(false); return 0.0; };
};

/** Addition (A + B) */
USTRUCT()
struct FDataflowMathAddNode : public FDataflowMathTwoInputsOperatorNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FDataflowMathAddNode, "Add", DATAFLOW_MATH_NODES_CATEGORY, "")

public:
	FDataflowMathAddNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid());
	virtual double ComputeResult(UE::Dataflow::FContext& Context, double InA, double InB) const override;
};

/** Subtraction (A - B) */
USTRUCT()
struct FDataflowMathSubtractNode : public FDataflowMathTwoInputsOperatorNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FDataflowMathSubtractNode, "Subtract", DATAFLOW_MATH_NODES_CATEGORY, "")

public:
	FDataflowMathSubtractNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid());
	virtual double ComputeResult(UE::Dataflow::FContext& Context, double InA, double InB) const override;
};

/** Multiplication (A * B) */
USTRUCT()
struct FDataflowMathMultiplyNode : public FDataflowMathTwoInputsOperatorNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FDataflowMathMultiplyNode, "Multiply", DATAFLOW_MATH_NODES_CATEGORY, "")

public:
	FDataflowMathMultiplyNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid());
	virtual double ComputeResult(UE::Dataflow::FContext& Context, double InA, double InB) const override;
};

/**
* Division (A / B)
* if B is equal to 0, 0 is returned Fallback value
*/
USTRUCT()
struct FDataflowMathDivideNode : public FDataflowMathTwoInputsOperatorNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FDataflowMathDivideNode, "Divide", DATAFLOW_MATH_NODES_CATEGORY, "")

	UPROPERTY(EditAnywhere, Category = SafeDivide, meta = (DataflowInput));
	FDataflowNumericTypes Fallback;

public:
	FDataflowMathDivideNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid());
	virtual double ComputeResult(UE::Dataflow::FContext& Context, double InA, double InB) const override;
};

/** Minimum ( Min(A, B) ) */
USTRUCT()
struct FDataflowMathMinimumNode : public FDataflowMathTwoInputsOperatorNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FDataflowMathMinimumNode, "Minimum", DATAFLOW_MATH_NODES_CATEGORY, "")

public:
	FDataflowMathMinimumNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid());
	virtual double ComputeResult(UE::Dataflow::FContext& Context, double InA, double InB) const override;
};

/** Maximum ( Max(A, B) ) */
USTRUCT()
struct FDataflowMathMaximumNode : public FDataflowMathTwoInputsOperatorNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FDataflowMathMaximumNode, "Maximum", DATAFLOW_MATH_NODES_CATEGORY, "")

public:
	FDataflowMathMaximumNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid());
	virtual double ComputeResult(UE::Dataflow::FContext& Context, double InA, double InB) const override;
};

/** 
* Reciprocal( 1 / A )
* if A is equal to 0, returns Fallback
*/
USTRUCT()
struct FDataflowMathReciprocalNode : public FDataflowMathOneInputOperatorNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FDataflowMathReciprocalNode, "Reciprocal", DATAFLOW_MATH_NODES_CATEGORY, "")

	UPROPERTY(EditAnywhere, Category = SafeDivide, meta = (DataflowInput));
	FDataflowNumericTypes Fallback;

public:
	FDataflowMathReciprocalNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid());
	virtual double ComputeResult(UE::Dataflow::FContext& Context, double InA) const override;
};

/** Square ( A * A ) */
USTRUCT()
struct FDataflowMathSquareNode : public FDataflowMathOneInputOperatorNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FDataflowMathSquareNode, "Square", DATAFLOW_MATH_NODES_CATEGORY, "")

public:
	FDataflowMathSquareNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid());
	virtual double ComputeResult(UE::Dataflow::FContext& Context, double InA) const override;
};

/** Cube ( A * A * A ) */
USTRUCT()
struct FDataflowMathCubeNode : public FDataflowMathOneInputOperatorNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FDataflowMathCubeNode, "Cube", DATAFLOW_MATH_NODES_CATEGORY, "")

public:
	FDataflowMathCubeNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid());
	virtual double ComputeResult(UE::Dataflow::FContext& Context, double InA) const override;
};

/** Square Root ( sqrt(A) ) */
USTRUCT()
struct FDataflowMathSquareRootNode : public FDataflowMathOneInputOperatorNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FDataflowMathSquareRootNode, "SquareRoot", DATAFLOW_MATH_NODES_CATEGORY, "")

public:
	FDataflowMathSquareRootNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid());
	virtual double ComputeResult(UE::Dataflow::FContext& Context, double InA) const override;
};

/** 
* Inverse Square Root ( 1 / sqrt(A) ) 
* if A is equal to 0, returns Fallback
*/
USTRUCT()
struct FDataflowMathInverseSquareRootNode : public FDataflowMathOneInputOperatorNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FDataflowMathInverseSquareRootNode, "InverseSquareRoot", DATAFLOW_MATH_NODES_CATEGORY, "")

	UPROPERTY(EditAnywhere, Category = SafeDivide, meta = (DataflowInput));
	FDataflowNumericTypes Fallback;

public:
	FDataflowMathInverseSquareRootNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid());
	virtual double ComputeResult(UE::Dataflow::FContext& Context, double InA) const override;
};

/** Negate ( -A ) */
USTRUCT()
struct FDataflowMathNegateNode : public FDataflowMathOneInputOperatorNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FDataflowMathNegateNode, "Negate", DATAFLOW_MATH_NODES_CATEGORY, "")

	UPROPERTY(EditAnywhere, Category = SafeDivide, meta = (DataflowInput));
	FDataflowNumericTypes Fallback;

public:
	FDataflowMathNegateNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid());
	virtual double ComputeResult(UE::Dataflow::FContext& Context, double InA) const override;
};

/** Absolute value ( |A| ) */
USTRUCT()
struct FDataflowMathAbsNode : public FDataflowMathOneInputOperatorNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FDataflowMathAbsNode, "Abs", DATAFLOW_MATH_NODES_CATEGORY, "")

	UPROPERTY(EditAnywhere, Category = SafeDivide, meta = (DataflowInput));
	FDataflowNumericTypes Fallback;

public:
	FDataflowMathAbsNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid());
	virtual double ComputeResult(UE::Dataflow::FContext& Context, double InA) const override;
};

/** Floor ( 1.4 => 1.0 | 1.9 => 1.0 | -5.3 => -6.0 ) */
USTRUCT()
struct FDataflowMathFloorNode : public FDataflowMathOneInputOperatorNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FDataflowMathFloorNode, "Floor", DATAFLOW_MATH_NODES_CATEGORY, "")

public:
	FDataflowMathFloorNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid());
	virtual double ComputeResult(UE::Dataflow::FContext& Context, double InA) const override;
};

/** Ceil ( 1.4 => 2.0 | 1.9 => 2.0 | -5.3 => -5.0) */
USTRUCT()
struct FDataflowMathCeilNode : public FDataflowMathOneInputOperatorNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FDataflowMathCeilNode, "Ceil", DATAFLOW_MATH_NODES_CATEGORY, "")

public:
	FDataflowMathCeilNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid());
	virtual double ComputeResult(UE::Dataflow::FContext& Context, double InA) const override;
};

/** Round ( 1.4 => 1.0 | 1.9 => 2.0 | -5.3 => -5.0) */
USTRUCT()
struct FDataflowMathRoundNode : public FDataflowMathOneInputOperatorNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FDataflowMathRoundNode, "Round", DATAFLOW_MATH_NODES_CATEGORY, "")

public:
	FDataflowMathRoundNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid());
	virtual double ComputeResult(UE::Dataflow::FContext& Context, double InA) const override;
};

/** Trunc ( 1.4 => 1.0 | 1.9 => 1.0 | -5.3 => -5.0) */
USTRUCT()
struct FDataflowMathTruncNode : public FDataflowMathOneInputOperatorNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FDataflowMathTruncNode, "Trunc", DATAFLOW_MATH_NODES_CATEGORY, "")

public:
	FDataflowMathTruncNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid());
	virtual double ComputeResult(UE::Dataflow::FContext& Context, double InA) const override;
};

/** Frac ( 1.4 => 0.4 | 1.9 => 0.9 | -5.3 => 0.3 ) */
USTRUCT()
struct FDataflowMathFracNode : public FDataflowMathOneInputOperatorNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FDataflowMathFracNode, "Frac", DATAFLOW_MATH_NODES_CATEGORY, "")

public:
	FDataflowMathFracNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid());
	virtual double ComputeResult(UE::Dataflow::FContext& Context, double InA) const override;
};

/** power ( A ^ B) */
USTRUCT()
struct FDataflowMathPowNode : public FDataflowMathTwoInputsOperatorNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FDataflowMathPowNode, "Pow", DATAFLOW_MATH_NODES_CATEGORY, "")

public:
	FDataflowMathPowNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid());
	virtual double ComputeResult(UE::Dataflow::FContext& Context, double InA, double InB) const override;
};

/**
* Log for a specific base ( Log[Base](A) ) 
* If base is negative or zero returns 0
*/
USTRUCT()
struct FDataflowMathLogXNode : public FDataflowMathOneInputOperatorNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FDataflowMathLogXNode, "LogX", DATAFLOW_MATH_NODES_CATEGORY, "")

	UPROPERTY(EditAnywhere, Category = SafeDivide, meta = (DataflowInput));
	FDataflowNumericTypes Base;

public:
	FDataflowMathLogXNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid());
	virtual double ComputeResult(UE::Dataflow::FContext& Context, double InA) const override;
};

/** Natural log ( Log(A) ) */
USTRUCT()
struct FDataflowMathLogNode : public FDataflowMathOneInputOperatorNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FDataflowMathLogNode, "Log", DATAFLOW_MATH_NODES_CATEGORY, "")

public:
	FDataflowMathLogNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid());
	virtual double ComputeResult(UE::Dataflow::FContext& Context, double InA) const override;
};

/** Exponential ( Exp(A) ) */
USTRUCT()
struct FDataflowMathExpNode : public FDataflowMathOneInputOperatorNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FDataflowMathExpNode, "Exp", DATAFLOW_MATH_NODES_CATEGORY, "")

public:
	FDataflowMathExpNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid());
	virtual double ComputeResult(UE::Dataflow::FContext& Context, double InA) const override;
};

/** return -1, 0, +1 whether the input is respectively negative, zero or positive ( Sign(A) ) */
USTRUCT()
struct FDataflowMathSignNode : public FDataflowMathOneInputOperatorNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FDataflowMathSignNode, "Sign", DATAFLOW_MATH_NODES_CATEGORY, "")

public:
	FDataflowMathSignNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid());
	virtual double ComputeResult(UE::Dataflow::FContext& Context, double InA) const override;
};

/** One minus (1 - A) */
USTRUCT()
struct FDataflowMathOneMinusNode : public FDataflowMathOneInputOperatorNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FDataflowMathOneMinusNode, "OneMinus", DATAFLOW_MATH_NODES_CATEGORY, "")

public:
	FDataflowMathOneMinusNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid());
	virtual double ComputeResult(UE::Dataflow::FContext& Context, double InA) const override;
};

UENUM(BlueprintType)
enum class EDataflowMathConstantsEnum : uint8
{
	Dataflow_Math_Constants_Pi			UMETA(DisplayName = "Pi"),
	Dataflow_Math_Constants_HalfPi		UMETA(DisplayName = "HalfPi"),
	Dataflow_Math_Constants_TwoPi		UMETA(DisplayName = "TwoPi"),
	Dataflow_Math_Constants_FourPi		UMETA(DisplayName = "FourPi"),
	Dataflow_Math_Constants_InvPi		UMETA(DisplayName = "InvPi"),
	Dataflow_Math_Constants_InvTwoPi	UMETA(DisplayName = "InvTwoPi"),
	Dataflow_Math_Constants_Sqrt2		UMETA(DisplayName = "Sqrt2"),
	Dataflow_Math_Constants_InvSqrt2	UMETA(DisplayName = "InvSqrt2"),
	Dataflow_Math_Constants_Sqrt3		UMETA(DisplayName = "Sqrt3"),
	Dataflow_Math_Constants_InvSqrt3	UMETA(DisplayName = "InvSqrt3"),
	Dataflow_Math_Constants_E			UMETA(DisplayName = "e"),
	Dataflow_Math_Constants_Gamma		UMETA(DisplayName = "Gamma"),
	Dataflow_Math_Constants_GoldenRatio	UMETA(DisplayName = "GoldenRatio"),
	//~~~
	//256th entry
	Dataflow_Math_Constants_Max UMETA(Hidden)
};

/** Math constants ( see EDataflowMathConstantsEnum ) */
USTRUCT()
struct FDataflowMathConstantNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FDataflowMathConstantNode, "Constants", DATAFLOW_MATH_NODES_CATEGORY, "")

	/** Math constant to output */
	UPROPERTY(EditAnywhere, Category = "Constants");
	EDataflowMathConstantsEnum Constant = EDataflowMathConstantsEnum::Dataflow_Math_Constants_Pi;

	UPROPERTY(meta = (DataflowOutput))
	FDataflowNumericTypes Result;

public:
	FDataflowMathConstantNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid());
	virtual void Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

private:
	double GetConstant() const;
};

//--------------------------------------------------------------------------
//
// Trigonometric nodes
//
//--------------------------------------------------------------------------

#define DATAFLOW_MATH_TRIG_NODES_CATEGORY "Math|Trig"

/** Sin(A) with A in radians  */
USTRUCT()
struct FDataflowMathSinNode : public FDataflowMathOneInputOperatorNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FDataflowMathSinNode, "Sin", DATAFLOW_MATH_TRIG_NODES_CATEGORY, "")

public:
	FDataflowMathSinNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid());
	virtual double ComputeResult(UE::Dataflow::FContext& Context, double InA) const override;
};

/** Cos(A) with A in radians  */
USTRUCT()
struct FDataflowMathCosNode : public FDataflowMathOneInputOperatorNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FDataflowMathCosNode, "Cos", DATAFLOW_MATH_TRIG_NODES_CATEGORY, "")

public:
	FDataflowMathCosNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid());
	virtual double ComputeResult(UE::Dataflow::FContext& Context, double InA) const override;
};

/** Tan(A) with A in radians  */
USTRUCT()
struct FDataflowMathTanNode : public FDataflowMathOneInputOperatorNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FDataflowMathTanNode, "Tan", DATAFLOW_MATH_TRIG_NODES_CATEGORY, "")

public:
	FDataflowMathTanNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid());
	virtual double ComputeResult(UE::Dataflow::FContext& Context, double InA) const override;
};

/** ArcSin(A) returns a value in radians  */
USTRUCT()
struct FDataflowMathArcSinNode : public FDataflowMathOneInputOperatorNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FDataflowMathArcSinNode, "ArcSin", DATAFLOW_MATH_TRIG_NODES_CATEGORY, "")

public:
	FDataflowMathArcSinNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid());
	virtual double ComputeResult(UE::Dataflow::FContext& Context, double InA) const override;
};

/** ArcCos(A) returns a value in radians  */
USTRUCT()
struct FDataflowMathArcCosNode : public FDataflowMathOneInputOperatorNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FDataflowMathArcCosNode, "ArcCos", DATAFLOW_MATH_TRIG_NODES_CATEGORY, "")

public:
	FDataflowMathArcCosNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid());
	virtual double ComputeResult(UE::Dataflow::FContext& Context, double InA) const override;
};

/** ArcTan(A) returns a value in radians  */
USTRUCT()
struct FDataflowMathArcTanNode : public FDataflowMathOneInputOperatorNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FDataflowMathArcTanNode, "ArcTan", DATAFLOW_MATH_TRIG_NODES_CATEGORY, "")

public:
	FDataflowMathArcTanNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid());
	virtual double ComputeResult(UE::Dataflow::FContext& Context, double InA) const override;
};

/** ArcTan2(A, B) returns a value in radians  */
USTRUCT()
struct FDataflowMathArcTan2Node : public FDataflowMathTwoInputsOperatorNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FDataflowMathArcTan2Node, "ArcTan2", DATAFLOW_MATH_TRIG_NODES_CATEGORY, "")

public:
	FDataflowMathArcTan2Node(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid());
	virtual double ComputeResult(UE::Dataflow::FContext& Context, double InA, double InB) const override;
};


/** DegToRad(A) convert degrees to radians */
USTRUCT()
struct FDataflowMathDegToRadNode : public FDataflowMathOneInputOperatorNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FDataflowMathDegToRadNode, "DegToRad", DATAFLOW_MATH_TRIG_NODES_CATEGORY, "")

public:
	FDataflowMathDegToRadNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid());
	virtual double ComputeResult(UE::Dataflow::FContext& Context, double InA) const override;
};

/** RadToDeg(A) convert radians to degrees */
USTRUCT()
struct FDataflowMathRadToDegNode : public FDataflowMathOneInputOperatorNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FDataflowMathRadToDegNode, "RadToDeg", DATAFLOW_MATH_TRIG_NODES_CATEGORY, "")

public:
	FDataflowMathRadToDegNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid());
	virtual double ComputeResult(UE::Dataflow::FContext& Context, double InA) const override;
};

//--------------------------------------------------------------------------
//
// Trigonometric nodes
//
//--------------------------------------------------------------------------


/*
Nodes Left to be converted :
	FFloatMathExpressionDataflowNode);
	FMathExpressionDataflowNode);
	
	FClampDataflowNode);
	FFitDataflowNode);
	FEFitDataflowNode);
	
	FLerpDataflowNode);
	FWrapDataflowNode);



	// vectors - requires Vector any type ? 
	FNormalizeToRangeDataflowNode);
	FScaleVectorDataflowNode);
	FDotProductDataflowNode);
	FCrossProductDataflowNode);
	FNormalizeDataflowNode);
	FLengthDataflowNode);
	FDistanceDataflowNode);
	FIsNearlyZeroDataflowNode);

	// random 
	FRandomFloatDataflowNode);
	FRandomFloatInRangeDataflowNode);
	FRandomUnitVectorDataflowNode);
	FRandomUnitVectorInConeDataflowNode);

*/

namespace UE::Dataflow
{
	void RegisterDataflowMathNodes();
}
