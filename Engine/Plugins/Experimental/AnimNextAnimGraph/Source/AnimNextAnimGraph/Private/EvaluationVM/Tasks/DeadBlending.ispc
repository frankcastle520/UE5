// Copyright Epic Games, Inc. All Rights Reserved.

#include "Math/Vector.isph"
#include "Math/Quat.isph"

#define UE_LN2 (0.69314718056f)
#define UE_SMALL_NUMBER (1.e-8f)
#define UE_KINDA_SMALL_NUMBER (1.e-4f)

inline FVector CastToVector(const FVector3f& V)
{
	const FVector Out = { {
		V.V[0],
		V.V[1],
		V.V[2],
	} };

	return Out;
}

inline FVector3f CastToVector3f(const FVector& V)
{
	const FVector3f Out = { {
		V.V[0],
		V.V[1],
		V.V[2],
	} };
    
	return Out;
}

inline FVector4f CastToVector4f(const FVector4& V)
{
	const FVector4f Out = { {
		V.V[0],
		V.V[1],
		V.V[2],
		V.V[3],
	} };
    
	return Out;
}

inline FVector4 CastToVector4(const FVector4f& V)
{
	const FVector4 Out = { {
		V.V[0],
		V.V[1],
		V.V[2],
		V.V[3],
	} };
    
	return Out;
}

inline FVector3f VectorDivMax(const float S, const FVector3f& W, const uniform float Epsilon = UE_SMALL_NUMBER)
{
	const FVector3f Out = { {
		S / max(W.V[0], Epsilon),
		S / max(W.V[1], Epsilon),
		S / max(W.V[2], Epsilon),
	} };
    
	return Out;
}

inline FVector3f VectorDivMax(const FVector3f& V, const FVector3f& W, const uniform float Epsilon = UE_SMALL_NUMBER)
{
	const FVector3f Out = { {
		V.V[0] / max(W.V[0], Epsilon),
		V.V[1] / max(W.V[1], Epsilon),
		V.V[2] / max(W.V[2], Epsilon),
	} };
    
	return Out;
}

inline FVector VectorDivMax(const FVector& V, const FVector& W, const uniform double Epsilon = UE_SMALL_NUMBER)
{
	const FVector Out = { {
		V.V[0] / max(W.V[0], Epsilon),
		V.V[1] / max(W.V[1], Epsilon),
		V.V[2] / max(W.V[2], Epsilon),
	} };
    
	return Out;
}

inline FVector3f VectorInvExpApprox(const FVector3f& V)
{
	const uniform float A = 1.00746054f;
	const uniform float B = 0.45053901f;
	const uniform float C = 0.25724632f;

	const FVector3f Out = { {
		1.0f / (1.0f + A * V.V[0] + B * V.V[0] * V.V[0] + C * V.V[0] * V.V[0] * V.V[0]),
		1.0f / (1.0f + A * V.V[1] + B * V.V[1] * V.V[1] + C * V.V[1] * V.V[1] * V.V[1]),
		1.0f / (1.0f + A * V.V[2] + B * V.V[2] * V.V[2] + C * V.V[2] * V.V[2] * V.V[2]),
	} };

	return Out;
}

inline FVector VectorEerp(const FVector& V, const FVector& W, const uniform double Alpha, const uniform double Epsilon = UE_SMALL_NUMBER)
{
	const FVector Out = { {
        pow(max(V.V[0], Epsilon), (1.0 - Alpha)) * pow(max(W.V[0], Epsilon), Alpha),
        pow(max(V.V[1], Epsilon), (1.0 - Alpha)) * pow(max(W.V[0], Epsilon), Alpha),
        pow(max(V.V[2], Epsilon), (1.0 - Alpha)) * pow(max(W.V[0], Epsilon), Alpha),
	} };

	return Out;
}

inline FVector VectorExp(const FVector& V)
{
	const FVector Out = { {
		exp(V.V[0]),
		exp(V.V[1]),
		exp(V.V[2]),
	} };

	return Out;
}

inline FVector VectorLogMax(const FVector& V, const uniform double Epsilon = UE_SMALL_NUMBER)
{
	const FVector Out = { {
		log(max(V.V[0], Epsilon)),
		log(max(V.V[1], Epsilon)),
		log(max(V.V[2], Epsilon)),
	} };
    
	return Out;
}

inline FVector VectorClampToMaxSize(const FVector V, const uniform double MaxSize)
{
	const double L = VectorDot(V, V);
    
	return L > (MaxSize * MaxSize) ? V * (MaxSize * rsqrt(L)) : V;
}

inline FVector4 QuatExp(const FVector& V, const uniform double Epsilon = UE_SMALL_NUMBER)
{
	const double HalfAngleSq = VectorDot(V, V);

	cif (HalfAngleSq < Epsilon)
	{
		const FVector4 O = { {
			V.V[0],
			V.V[1],
			V.V[2],
			1.0,
		} };

		return VectorNormalizeQuaternion(O);
	}
	else
	{
		const double HalfAngle = sqrt(HalfAngleSq);
		const double C = cos(HalfAngle);
		const double S = sin(HalfAngle) / HalfAngle;

		const FVector4 O = { {
			S * V.V[0],
			S * V.V[1],
			S * V.V[2],
			C,
		} };

		return O;
	}
}

inline FVector QuatLog(const FVector4& Q, const uniform double Epsilon = UE_SMALL_NUMBER)
{
	const double LengthSq = VectorDot(Q, Q);

	cif (LengthSq < Epsilon)
	{
		const FVector O = { { Q.V[0], Q.V[1], Q.V[2] } };
		return O;
	}
	else
	{
		const double Length = sqrt(LengthSq);
		const double HalfAngle = acos(clamp(Q.V[3], -1.0d, 1.0d));

		const FVector O = { {
			HalfAngle * (Q.V[0] / Length),
			HalfAngle * (Q.V[1] / Length),
			HalfAngle * (Q.V[2] / Length),
		} };

		return O;
	}
}

inline FVector4 QuatFromRotationVector(const FVector& V, const uniform double Epsilon = UE_SMALL_NUMBER)
{
	return QuatExp(0.5 * V, Epsilon);
}

inline FVector QuatToRotationVector(const FVector4& Q, const uniform double Epsilon = UE_SMALL_NUMBER)
{
	return 2.0 * QuatLog(Q, Epsilon);
}

inline FVector4 VectorNegate(const FVector4& Q)
{
	const FVector4 O = { { -Q.V[0], -Q.V[1], -Q.V[2], -Q.V[3] } };
	return O;
}

inline FVector4 VectorQuaternionEnforceShortestArcWith(const FVector4 Q, const FVector4 P)
{
	return VectorDot(Q, P) > 0.0 ? Q : VectorNegate(Q);
}

inline FVector4 QuatAbs(const FVector4 Q)
{
	return Q.V[3] > 0.0 ? Q : VectorNegate(Q);
}

inline FVector ExtrapolateTranslation(
	const FVector& Translation,
	const FVector3f& Velocity,
	const FVector3f& DecayHalflife,
	const uniform float Time,
	const uniform float Epsilon = UE_SMALL_NUMBER)
{
	const FVector3f C = VectorDivMax(UE_LN2, DecayHalflife, Epsilon);
	return Translation + CastToVector(VectorDivMax(Velocity, C, Epsilon) * (FloatOneVector - VectorInvExpApprox(C * Time)));
}

inline FVector4 ExtrapolateRotation(
	const FVector4& Rotation,
	const FVector3f& Velocity,
	const FVector3f& DecayHalflife,
	const uniform float Time,
	const uniform float Epsilon = UE_SMALL_NUMBER)
{
	const FVector3f C = VectorDivMax(UE_LN2, DecayHalflife, Epsilon);
	return VectorQuaternionMultiply2(QuatFromRotationVector(CastToVector(VectorDivMax(Velocity, C, Epsilon) * (FloatOneVector - VectorInvExpApprox(C * Time)))), Rotation);
}

inline FVector ExtrapolateScale(
	const FVector& Scale,
	const FVector3f& Velocity,
	const FVector3f& DecayHalflife,
	const uniform float Time,
	const uniform float Epsilon = UE_SMALL_NUMBER)
{
	const FVector3f C = VectorDivMax(UE_LN2, DecayHalflife, Epsilon);
	return VectorExp(CastToVector(VectorDivMax(Velocity, C, Epsilon) * (FloatOneVector - VectorInvExpApprox(C * Time)))) * Scale;
}

inline float ClipMagnitudeToGreaterThanEpsilon(const float X, const uniform float Epsilon = UE_KINDA_SMALL_NUMBER)
{
	return
		X >= 0.0f && X < +Epsilon ? +Epsilon :
		X <  0.0f && X > -Epsilon ? -Epsilon : X;
}

inline float ComputeDecayHalfLifeFromDiffAndVelocity(
	const float SrcDstDiff,
	const float SrcVelocity,
	const uniform float HalfLife,
	const uniform float HalfLifeMin,
	const uniform float HalfLifeMax,
	const uniform float Epsilon = UE_KINDA_SMALL_NUMBER)
{
	return clamp(HalfLife * (SrcDstDiff / ClipMagnitudeToGreaterThanEpsilon(SrcVelocity, Epsilon)), HalfLifeMin, HalfLifeMax);
}

inline FVector3f ComputeDecayHalfLifeFromDiffAndVelocity(
	const FVector SrcDstDiff,
	const FVector3f SrcVelocity,
	const uniform float HalfLife,
	const uniform float HalfLifeMin,
	const uniform float HalfLifeMax,
	const uniform float Epsilon = UE_KINDA_SMALL_NUMBER)
{
	const FVector3f O = { {
		ComputeDecayHalfLifeFromDiffAndVelocity((float)SrcDstDiff.V[0], SrcVelocity.V[0], HalfLife, HalfLifeMin, HalfLifeMax, Epsilon),
		ComputeDecayHalfLifeFromDiffAndVelocity((float)SrcDstDiff.V[1], SrcVelocity.V[1], HalfLife, HalfLifeMin, HalfLifeMax, Epsilon),
		ComputeDecayHalfLifeFromDiffAndVelocity((float)SrcDstDiff.V[2], SrcVelocity.V[2], HalfLife, HalfLifeMin, HalfLifeMax, Epsilon),
	} };

	return O;
}

export void AnimNextDeadBlendingTransition(
	uniform FVector4f BoneRotationDirections[],
	uniform FVector SourceBoneTranslations[],
	uniform FVector4 SourceBoneRotations[],
	uniform FVector SourceBoneScales3D[],
	uniform FVector3f SourceBoneTranslationVelocities[],
	uniform FVector3f SourceBoneRotationVelocities[],
	uniform FVector3f SourceBoneScaleVelocities[],
	uniform FVector3f SourceBoneTranslationDecayHalfLives[],
	uniform FVector3f SourceBoneRotationDecayHalfLives[],
	uniform FVector3f SourceBoneScaleDecayHalfLives[],
	const uniform FVector DestBoneTranslations[],
	const uniform FVector4 DestBoneRotations[],
	const uniform FVector DestBoneScales3D[],
	const uniform FVector CurrBoneTranslations[],
	const uniform FVector4 CurrBoneRotations[],
	const uniform FVector CurrBoneScales3D[],
	const uniform FVector PrevBoneTranslations[],
	const uniform FVector4 PrevBoneRotations[],
	const uniform FVector PrevBoneScales3D[],
	const uniform int LODBoneNum,
	const uniform float DeltaTime,
	const uniform float ExtrapolationHalfLife,
	const uniform float ExtrapolationHalfLifeMin,
	const uniform float ExtrapolationHalfLifeMax,
	const uniform float MaximumTranslationVelocity,
	const uniform float MaximumRotationVelocity,
	const uniform float MaximumScaleVelocity)
{
	const FVector4f IdentityRotation = Float0001;

	foreach(i = 0 ... LODBoneNum)
	{
		const uniform int Index = extract(i, 0);

		VectorStore(&BoneRotationDirections[Index], IdentityRotation);

		const FVector SrcTranslationCurr = VectorLoad(&CurrBoneTranslations[Index]);
		const FVector4 SrcRotationCurr = VectorLoad(&CurrBoneRotations[Index]);
		const FVector SrcScaleCurr = VectorLoad(&CurrBoneScales3D[Index]);

		VectorStore(&SourceBoneTranslations[Index], SrcTranslationCurr);
		VectorStore(&SourceBoneRotations[Index], SrcRotationCurr);
		VectorStore(&SourceBoneScales3D[Index], SrcScaleCurr);

		const FVector SrcTranslationPrev = VectorLoad(&PrevBoneTranslations[Index]);
		const FVector4 SrcRotationPrev = VectorLoad(&PrevBoneRotations[Index]);
		const FVector SrcScalePrev = VectorLoad(&PrevBoneScales3D[Index]);

		const FVector TranslationDiff = SrcTranslationCurr - SrcTranslationPrev;
		const FVector4 RotationDiff = QuatAbs(VectorQuaternionMultiply2(SrcRotationCurr, QuatInverse(SrcRotationPrev)));
		const FVector ScaleDiff = VectorDivMax(SrcScaleCurr, SrcScalePrev);

		VectorStore(&SourceBoneTranslationVelocities[Index], CastToVector3f(VectorClampToMaxSize(TranslationDiff / DeltaTime, MaximumTranslationVelocity)));
		VectorStore(&SourceBoneRotationVelocities[Index], CastToVector3f(VectorClampToMaxSize(QuatToRotationVector(RotationDiff) / DeltaTime, MaximumRotationVelocity)));
		VectorStore(&SourceBoneScaleVelocities[Index], CastToVector3f(VectorClampToMaxSize(VectorLogMax(ScaleDiff) / DeltaTime, MaximumScaleVelocity)));

		const FVector DstTranslation = VectorLoad(&DestBoneTranslations[Index]);
		const FVector4 DstRotation = VectorLoad(&DestBoneRotations[Index]);
		const FVector DstScale = VectorLoad(&DestBoneScales3D[Index]);

		const FVector TranslationSrcDstDiff = DstTranslation - SrcTranslationCurr;
		const FVector4 RotationSrcDstDiff = QuatAbs(VectorQuaternionMultiply2(DstRotation, QuatInverse(SrcRotationCurr)));
		const FVector ScaleSrcDstDiff = VectorDivMax(DstScale, SrcScaleCurr);

		VectorStore(&SourceBoneTranslationDecayHalfLives[Index], ComputeDecayHalfLifeFromDiffAndVelocity(
			TranslationSrcDstDiff,
			VectorLoad(&SourceBoneTranslationVelocities[Index]),
			ExtrapolationHalfLife,
			ExtrapolationHalfLifeMin,
			ExtrapolationHalfLifeMax));

		VectorStore(&SourceBoneRotationDecayHalfLives[Index], ComputeDecayHalfLifeFromDiffAndVelocity(
			QuatToRotationVector(RotationSrcDstDiff),
			VectorLoad(&SourceBoneRotationVelocities[Index]),
			ExtrapolationHalfLife,
			ExtrapolationHalfLifeMin,
			ExtrapolationHalfLifeMax));

		VectorStore(&SourceBoneScaleDecayHalfLives[Index], ComputeDecayHalfLifeFromDiffAndVelocity(
			ScaleSrcDstDiff,
			VectorLoad(&SourceBoneScaleVelocities[Index]),
			ExtrapolationHalfLife,
			ExtrapolationHalfLifeMin,
			ExtrapolationHalfLifeMax));
	}
}


export void AnimNextDeadBlendingTransitionStatic(
	uniform FVector4f BoneRotationDirections[],
	uniform FVector SourceBoneTranslations[],
	uniform FVector4 SourceBoneRotations[],
	uniform FVector SourceBoneScales3D[],
	uniform FVector3f SourceBoneTranslationVelocities[],
	uniform FVector3f SourceBoneRotationVelocities[],
	uniform FVector3f SourceBoneScaleVelocities[],
	uniform FVector3f SourceBoneTranslationDecayHalfLives[],
	uniform FVector3f SourceBoneRotationDecayHalfLives[],
	uniform FVector3f SourceBoneScaleDecayHalfLives[],
	const uniform FVector CurrBoneTranslations[],
	const uniform FVector4 CurrBoneRotations[],
	const uniform FVector CurrBoneScales[],
	const uniform int LODBoneNum,
	const uniform float ExtrapolationHalfLifeMin)
{
	const FVector4f IdentityRotation = Float0001;
	const FVector3f VelocityZeroVector = FloatZeroVector;
	const FVector3f HalfLifeVector = { { ExtrapolationHalfLifeMin, ExtrapolationHalfLifeMin, ExtrapolationHalfLifeMin } };

	foreach(i = 0 ... LODBoneNum)
	{
		const uniform int Index = extract(i, 0);

		VectorStore(&BoneRotationDirections[Index], IdentityRotation);
		VectorStore(&SourceBoneTranslations[Index], VectorLoad(&CurrBoneTranslations[Index]));
		VectorStore(&SourceBoneRotations[Index], VectorLoad(&CurrBoneRotations[Index]));
		VectorStore(&SourceBoneScales3D[Index], VectorLoad(&CurrBoneScales[Index]));

		VectorStore(&SourceBoneTranslationVelocities[Index], VelocityZeroVector);
		VectorStore(&SourceBoneRotationVelocities[Index], VelocityZeroVector);
		VectorStore(&SourceBoneScaleVelocities[Index], VelocityZeroVector);
		VectorStore(&SourceBoneTranslationDecayHalfLives[Index], HalfLifeVector);
		VectorStore(&SourceBoneRotationDecayHalfLives[Index], HalfLifeVector);
		VectorStore(&SourceBoneScaleDecayHalfLives[Index], HalfLifeVector);
	}
}

export void AnimNextDeadBlendingApply(
	uniform FVector DestTranslations[],
	uniform FVector4 DestRotations[],
	uniform FVector DestScales3D[],
	uniform FVector4f BoneRotationDirections[],
	const uniform FVector SourceTranslations[],
	const uniform FVector4 SourceRotations[],
	const uniform FVector SourceScales3D[],
	const uniform FVector3f SourceBoneTranslationVelocities[],
	const uniform FVector3f SourceBoneRotationVelocities[],
	const uniform FVector3f SourceBoneScaleVelocities[],
	const uniform FVector3f SourceBoneTranslationDecayHalfLives[],
	const uniform FVector3f SourceBoneRotationDecayHalfLives[],
	const uniform FVector3f SourceBoneScaleDecayHalfLives[],
	const uniform int LODBoneNum,
	const uniform float Alpha,
	const uniform float TimeSinceTransition)
{
	foreach (i = 0 ... LODBoneNum)
	{
		const uniform int Index = extract(i, 0);

		const FVector ExtrapolatedTranslation = ExtrapolateTranslation(
			VectorLoad(&SourceTranslations[Index]),
			VectorLoad(&SourceBoneTranslationVelocities[Index]),
			VectorLoad(&SourceBoneTranslationDecayHalfLives[Index]),
			TimeSinceTransition);

		const FVector4 ExtrapolatedRotation = ExtrapolateRotation(
			VectorLoad(&SourceRotations[Index]),
			VectorLoad(&SourceBoneRotationVelocities[Index]),
			VectorLoad(&SourceBoneRotationDecayHalfLives[Index]),
			TimeSinceTransition);

		const FVector ExtrapolatedScale = ExtrapolateScale(
			VectorLoad(&SourceScales3D[Index]),
			VectorLoad(&SourceBoneScaleVelocities[Index]),
			VectorLoad(&SourceBoneScaleDecayHalfLives[Index]),
			TimeSinceTransition);

		const FVector4 RotationDiff = VectorQuaternionEnforceShortestArcWith(
			VectorQuaternionMultiply2(ExtrapolatedRotation, QuatInverse(VectorLoad(&DestRotations[Index]))),
			CastToVector4(VectorLoad(&BoneRotationDirections[Index])));

		VectorStore(&BoneRotationDirections[Index], CastToVector4f(RotationDiff));

		VectorStore(&DestTranslations[Index], VectorLerp(VectorLoad(&DestTranslations[Index]), ExtrapolatedTranslation, Alpha));

		VectorStore(&DestRotations[Index],
			VectorQuaternionMultiply2(
				QuatFromRotationVector(QuatToRotationVector(RotationDiff) * Alpha),
				VectorLoad(&DestRotations[Index])));

		VectorStore(&DestScales3D[Index], VectorEerp(VectorLoad(&DestScales3D[Index]), ExtrapolatedScale, Alpha));
	}
}