// Copyright Epic Games, Inc. All Rights Reserved.

#include "Physics/ImmediatePhysics/ImmediatePhysicsChaos/ImmediatePhysicsJointHandle_Chaos.h"
#include "Physics/ImmediatePhysics/ImmediatePhysicsChaos/ImmediatePhysicsActorHandle_Chaos.h"

#include "Chaos/Particle/ParticleUtilities.h"
#include "Chaos/PBDJointConstraints.h"
#include "Chaos/ChaosConstraintSettings.h"

#include "PhysicsEngine/ConstraintInstance.h"

//UE_DISABLE_OPTIMIZATION

static_assert((int32)Chaos::EJointMotionType::Free == (int32)EAngularConstraintMotion::ACM_Free, "Chaos::EJointMotionType and EAngularConstraintMotion mismatch");
static_assert((int32)Chaos::EJointMotionType::Limited == (int32)EAngularConstraintMotion::ACM_Limited, "Chaos::EJointMotionType and EAngularConstraintMotion mismatch");
static_assert((int32)Chaos::EJointMotionType::Locked == (int32)EAngularConstraintMotion::ACM_Locked, "Chaos::EJointMotionType and EAngularConstraintMotion mismatch");

// NOTE: Hard dependence on EJointAngularConstraintIndex - the following will break if we change the order (but can be easily fixed). See FJointHandle::FJointHandle
static_assert((int32)Chaos::EJointAngularConstraintIndex::Twist == 0, "Angular drive targets have hard dependency on constraint order");
static_assert((int32)Chaos::EJointAngularConstraintIndex::Swing1 == 2, "Angular drive targets have hard dependency on constraint order");

namespace ImmediatePhysics_Chaos
{
	static Chaos::EJointMotionType ConvertToJointMotionType(ELinearConstraintMotion InType)
	{
		switch (InType)
		{
		case ELinearConstraintMotion::LCM_Free: return Chaos::EJointMotionType::Free;
		case ELinearConstraintMotion::LCM_Limited: return Chaos::EJointMotionType::Limited;
		case ELinearConstraintMotion::LCM_Locked: return Chaos::EJointMotionType::Locked;
		default: ensure(false); return Chaos::EJointMotionType::Locked;
		}
	};

	static Chaos::EJointMotionType ConvertToJointMotionType(EAngularConstraintMotion InType)
	{
		switch (InType)
		{
		case EAngularConstraintMotion::ACM_Free: return Chaos::EJointMotionType::Free;
		case EAngularConstraintMotion::ACM_Limited: return Chaos::EJointMotionType::Limited;
		case EAngularConstraintMotion::ACM_Locked: return Chaos::EJointMotionType::Locked;
		default: ensure(false); return Chaos::EJointMotionType::Locked;
		}
	};

	static Chaos::EPlasticityType ConvertToPlasticityType(EConstraintPlasticityType InType)
	{
		switch (InType)
		{
		case EConstraintPlasticityType::CCPT_Free: return Chaos::EPlasticityType::Free;
		case EConstraintPlasticityType::CCPT_Shrink: return Chaos::EPlasticityType::Shrink;
		case EConstraintPlasticityType::CCPT_Grow: return Chaos::EPlasticityType::Grow;
		default: return Chaos::EPlasticityType::Free;
		}
	}

	// Copies data from a profile into settings
	void UpdateJointSettingsFromConstraintProfile(const FConstraintProfileProperties& Profile, Chaos::FPBDJointSettings& JointSettings)
	{
		using namespace Chaos;

		JointSettings.Stiffness = ConstraintSettings::JointStiffness();
		JointSettings.LinearProjection = Profile.bEnableProjection ? Profile.ProjectionLinearAlpha : 0.0f;
		JointSettings.AngularProjection = Profile.bEnableProjection ? Profile.ProjectionAngularAlpha : 0.0f;
		JointSettings.ShockPropagation = Profile.bEnableShockPropagation ? Profile.ShockPropagationAlpha : 0.0f;
		JointSettings.TeleportDistance = Profile.bEnableProjection ? Profile.ProjectionLinearTolerance : -1.0f;
		JointSettings.TeleportAngle = Profile.bEnableProjection ?
			FMath::DegreesToRadians(Profile.ProjectionAngularTolerance) : -1.0f;
		JointSettings.ParentInvMassScale = Profile.bParentDominates ? (FReal)0 : (FReal)1;

		JointSettings.bCollisionEnabled = !Profile.bDisableCollision;
		JointSettings.bProjectionEnabled = Profile.bEnableProjection;
		JointSettings.bShockPropagationEnabled = Profile.bEnableShockPropagation;
		JointSettings.bMassConditioningEnabled = Profile.bEnableMassConditioning;

		JointSettings.LinearMotionTypes[0] = ConvertToJointMotionType(Profile.LinearLimit.XMotion);
		JointSettings.LinearMotionTypes[1] = ConvertToJointMotionType(Profile.LinearLimit.YMotion);
		JointSettings.LinearMotionTypes[2] = ConvertToJointMotionType(Profile.LinearLimit.ZMotion);

		JointSettings.LinearLimit = Profile.LinearLimit.Limit;

		// Order is twist, swing1, swing2 and in degrees
		JointSettings.AngularMotionTypes[(int32)Chaos::EJointAngularConstraintIndex::Twist] = ConvertToJointMotionType(Profile.TwistLimit.TwistMotion);
		JointSettings.AngularMotionTypes[(int32)Chaos::EJointAngularConstraintIndex::Swing1] = ConvertToJointMotionType(Profile.ConeLimit.Swing1Motion);
		JointSettings.AngularMotionTypes[(int32)Chaos::EJointAngularConstraintIndex::Swing2] = ConvertToJointMotionType(Profile.ConeLimit.Swing2Motion);

		JointSettings.AngularLimits[(int32)Chaos::EJointAngularConstraintIndex::Twist] = FMath::DegreesToRadians(Profile.TwistLimit.TwistLimitDegrees);
		JointSettings.AngularLimits[(int32)Chaos::EJointAngularConstraintIndex::Swing1] = FMath::DegreesToRadians(Profile.ConeLimit.Swing1LimitDegrees);
		JointSettings.AngularLimits[(int32)Chaos::EJointAngularConstraintIndex::Swing2] = FMath::DegreesToRadians(Profile.ConeLimit.Swing2LimitDegrees);

		JointSettings.bSoftLinearLimitsEnabled = Profile.LinearLimit.bSoftConstraint;
		JointSettings.bSoftTwistLimitsEnabled = Profile.TwistLimit.bSoftConstraint;
		JointSettings.bSoftSwingLimitsEnabled = Profile.ConeLimit.bSoftConstraint;

		JointSettings.LinearSoftForceMode = (ConstraintSettings::SoftLinearForceMode() == 0) ?
			EJointForceMode::Acceleration : EJointForceMode::Force;
		JointSettings.AngularSoftForceMode = (ConstraintSettings::SoftAngularForceMode() == 0) ?
			EJointForceMode::Acceleration : EJointForceMode::Force;

		JointSettings.SoftLinearStiffness =
			Chaos::ConstraintSettings::SoftLinearStiffnessScale() * Profile.LinearLimit.Stiffness;
		JointSettings.SoftLinearDamping =
			Chaos::ConstraintSettings::SoftLinearDampingScale() * Profile.LinearLimit.Damping;
		JointSettings.SoftTwistStiffness =
			Chaos::ConstraintSettings::SoftAngularStiffnessScale() * Profile.TwistLimit.Stiffness;
		JointSettings.SoftTwistDamping =
			Chaos::ConstraintSettings::SoftAngularDampingScale() * Profile.TwistLimit.Damping;
		JointSettings.SoftSwingStiffness =
			Chaos::ConstraintSettings::SoftAngularStiffnessScale() * Profile.ConeLimit.Stiffness;
		JointSettings.SoftSwingDamping =
			Chaos::ConstraintSettings::SoftAngularDampingScale() * Profile.ConeLimit.Damping;

		JointSettings.LinearRestitution = Profile.LinearLimit.Restitution;
		JointSettings.TwistRestitution = Profile.TwistLimit.Restitution;
		JointSettings.SwingRestitution = Profile.ConeLimit.Restitution;

		JointSettings.LinearContactDistance = Profile.LinearLimit.ContactDistance;
		JointSettings.TwistContactDistance = Profile.TwistLimit.ContactDistance;
		JointSettings.SwingContactDistance = Profile.ConeLimit.ContactDistance;

		JointSettings.LinearDrivePositionTarget = Profile.LinearDrive.PositionTarget;
		JointSettings.LinearDriveVelocityTarget = Profile.LinearDrive.VelocityTarget;
		JointSettings.bLinearPositionDriveEnabled[0] = Profile.LinearDrive.XDrive.bEnablePositionDrive;
		JointSettings.bLinearPositionDriveEnabled[1] = Profile.LinearDrive.YDrive.bEnablePositionDrive;
		JointSettings.bLinearPositionDriveEnabled[2] = Profile.LinearDrive.ZDrive.bEnablePositionDrive;
		JointSettings.bLinearVelocityDriveEnabled[0] = Profile.LinearDrive.XDrive.bEnableVelocityDrive;
		JointSettings.bLinearVelocityDriveEnabled[1] = Profile.LinearDrive.YDrive.bEnableVelocityDrive;
		JointSettings.bLinearVelocityDriveEnabled[2] = Profile.LinearDrive.ZDrive.bEnableVelocityDrive;

		JointSettings.LinearDriveForceMode = EJointForceMode::Acceleration; // hardcoded!
		JointSettings.LinearDriveStiffness = Chaos::ConstraintSettings::LinearDriveStiffnessScale() * FVec3(
			Profile.LinearDrive.XDrive.Stiffness,
			Profile.LinearDrive.YDrive.Stiffness,
			Profile.LinearDrive.ZDrive.Stiffness);
		JointSettings.LinearDriveDamping = Chaos::ConstraintSettings::LinearDriveDampingScale() * FVec3(
			Profile.LinearDrive.XDrive.Damping,
			Profile.LinearDrive.YDrive.Damping,
			Profile.LinearDrive.ZDrive.Damping);
		JointSettings.LinearDriveMaxForce[0] = Profile.LinearDrive.XDrive.MaxForce;
		JointSettings.LinearDriveMaxForce[1] = Profile.LinearDrive.YDrive.MaxForce;
		JointSettings.LinearDriveMaxForce[2] = Profile.LinearDrive.ZDrive.MaxForce;

		JointSettings.AngularDrivePositionTarget = FQuat(Profile.AngularDrive.OrientationTarget);
		JointSettings.AngularDriveVelocityTarget = Profile.AngularDrive.AngularVelocityTarget * UE_TWO_PI; // rev/s to rad/s

		JointSettings.AngularDriveForceMode = EJointForceMode::Acceleration; // hardcoded!
		if (Profile.AngularDrive.AngularDriveMode == EAngularDriveMode::SLERP)
		{
			JointSettings.AngularDriveStiffness = FVec3(
				ConstraintSettings::AngularDriveStiffnessScale() * Profile.AngularDrive.SlerpDrive.Stiffness);
			JointSettings.AngularDriveDamping = FVec3(
				ConstraintSettings::AngularDriveDampingScale() * Profile.AngularDrive.SlerpDrive.Damping);
			JointSettings.AngularDriveMaxTorque = FVec3(
				Profile.AngularDrive.SlerpDrive.MaxForce);
			JointSettings.bAngularSLerpPositionDriveEnabled = Profile.AngularDrive.SlerpDrive.bEnablePositionDrive;
			JointSettings.bAngularSLerpVelocityDriveEnabled = Profile.AngularDrive.SlerpDrive.bEnableVelocityDrive;
			JointSettings.bAngularTwistPositionDriveEnabled = false;
			JointSettings.bAngularTwistVelocityDriveEnabled = false;
			JointSettings.bAngularSwingPositionDriveEnabled = false;
			JointSettings.bAngularSwingVelocityDriveEnabled = false;
		}
		else
		{
			JointSettings.AngularDriveStiffness = ConstraintSettings::AngularDriveStiffnessScale() * FVec3(
				Profile.AngularDrive.TwistDrive.Stiffness,
				Profile.AngularDrive.SwingDrive.Stiffness,
				Profile.AngularDrive.SwingDrive.Stiffness);
			JointSettings.AngularDriveDamping = ConstraintSettings::AngularDriveDampingScale() * FVec3(
				Profile.AngularDrive.TwistDrive.Damping,
				Profile.AngularDrive.SwingDrive.Damping,
				Profile.AngularDrive.SwingDrive.Damping);
			JointSettings.AngularDriveMaxTorque[0] = Profile.AngularDrive.TwistDrive.MaxForce;
			JointSettings.AngularDriveMaxTorque[1] = Profile.AngularDrive.SwingDrive.MaxForce;
			JointSettings.AngularDriveMaxTorque[2] = Profile.AngularDrive.SwingDrive.MaxForce;
			JointSettings.bAngularSLerpPositionDriveEnabled = false;
			JointSettings.bAngularSLerpVelocityDriveEnabled = false;
			JointSettings.bAngularTwistPositionDriveEnabled = Profile.AngularDrive.TwistDrive.bEnablePositionDrive;
			JointSettings.bAngularTwistVelocityDriveEnabled = Profile.AngularDrive.TwistDrive.bEnableVelocityDrive;
			JointSettings.bAngularSwingPositionDriveEnabled = Profile.AngularDrive.SwingDrive.bEnablePositionDrive;
			JointSettings.bAngularSwingVelocityDriveEnabled = Profile.AngularDrive.SwingDrive.bEnableVelocityDrive;
		}

		JointSettings.LinearBreakForce = Profile.bLinearBreakable ?
			Chaos::ConstraintSettings::LinearBreakScale() * Profile.LinearBreakThreshold : FLT_MAX;
		JointSettings.LinearPlasticityLimit = Profile.bLinearPlasticity ?
			FMath::Clamp((float)Profile.LinearPlasticityThreshold, 0.0f, 1.0f) : FLT_MAX;
		JointSettings.LinearPlasticityType = ConvertToPlasticityType(Profile.LinearPlasticityType);
		// JointSettings.LinearPlasticityInitialDistanceSquared = ; // What do we do with this?

		JointSettings.AngularBreakTorque = Profile.bAngularBreakable ?
			Chaos::ConstraintSettings::AngularBreakScale() * Profile.AngularBreakThreshold : FLT_MAX;
		JointSettings.AngularPlasticityLimit = Profile.bAngularPlasticity ?
			FMath::Clamp((float)Profile.AngularPlasticityThreshold, 0.0f, 1.0f) : FLT_MAX;;

		JointSettings.ContactTransferScale = Profile.ContactTransferScale;


		// UE Disables Soft Limits when the Limit is less than some threshold. This is not necessary in Chaos but for now we also do it for parity's sake (See FLinearConstraint::UpdateLinearLimit_AssumesLocked).
		if (JointSettings.LinearLimit < RB_MinSizeToLockDOF)
		{
			for (int32 AxisIndex = 0; AxisIndex < 3; ++AxisIndex)
			{
				if (JointSettings.LinearMotionTypes[AxisIndex] == EJointMotionType::Limited)
				{
					JointSettings.LinearMotionTypes[AxisIndex] = EJointMotionType::Locked;
				}
			}
		}
	}

	FJointHandle::FJointHandle(FChaosConstraintContainer* InConstraints, FConstraintInstance* ConstraintInstance, FActorHandle* Actor1, FActorHandle* Actor2)
		: ActorHandles({ Actor1, Actor2 })
		, Constraints(InConstraints)
	{
		using namespace Chaos;

		FPBDJointSettings ConstraintSettings;

		if (ConstraintInstance != nullptr)
		{
			// BodyInstance/PhysX has the constraint locations in actor-space, but we need them in Center-of-Mass space
			UpdateJointSettingsFromConstraintProfile(ConstraintInstance->ProfileInstance, ConstraintSettings);
			FReal JointScale = ConstraintInstance->GetLastKnownScale();
			ConstraintSettings.ConnectorTransforms[0] = FParticleUtilities::ActorLocalToParticleLocal(FGenericParticleHandle(Actor1->GetParticle()), ConstraintInstance->GetRefFrame(EConstraintFrame::Frame1));
			ConstraintSettings.ConnectorTransforms[1] = FParticleUtilities::ActorLocalToParticleLocal(FGenericParticleHandle(Actor2->GetParticle()), ConstraintInstance->GetRefFrame(EConstraintFrame::Frame2));
			ConstraintSettings.ConnectorTransforms[0].ScaleTranslation(JointScale);
			ConstraintSettings.ConnectorTransforms[1].ScaleTranslation(JointScale);
		}
		else
		{
			// TEMP: all creation with null ConstraintIndex for PhAt handles
			ConstraintSettings.ConnectorTransforms[0] = Actor2->GetWorldTransform().GetRelativeTransform(Actor1->GetWorldTransform());
			ConstraintSettings.ConnectorTransforms[1] = FRigidTransform3();
			ConstraintSettings.LinearMotionTypes = { EJointMotionType::Limited, EJointMotionType::Limited, EJointMotionType::Limited };
			ConstraintSettings.LinearLimit = 0.1f;
			ConstraintSettings.SoftLinearStiffness = 500.0f;
			ConstraintSettings.SoftLinearDamping = 100.0f;
			ConstraintSettings.bSoftLinearLimitsEnabled = true;
			ConstraintSettings.LinearSoftForceMode = EJointForceMode::Acceleration;
			ConstraintSettings.LinearProjection = 0.0f;
			ConstraintSettings.AngularProjection = 0.0f;
			ConstraintSettings.TeleportDistance = -1.0f;
			ConstraintSettings.TeleportAngle = -1.0f;
		}

		ConstraintSettings.Sanitize();

		ConstraintHandle = Constraints->AddConstraint({ Actor1->ParticleHandle, Actor2->ParticleHandle }, ConstraintSettings);

		SetActorInertiaConditioningDirty();
	}

	FJointHandle::FJointHandle(FChaosConstraintContainer* InConstraints, const FPBDJointSettings& ConstraintSettings, FActorHandle* const Actor1, FActorHandle* const Actor2)
		: ActorHandles({ Actor1, Actor2 })
		, Constraints(InConstraints)
	{
		ConstraintHandle = Constraints->AddConstraint({ Actor1->ParticleHandle, Actor2->ParticleHandle }, ConstraintSettings);

		SetActorInertiaConditioningDirty();
	}

	FJointHandle::~FJointHandle()
	{
		ConstraintHandle->SetConstraintEnabled(false);
		ConstraintHandle->RemoveConstraint();
	}

	typename FJointHandle::FChaosConstraintHandle* FJointHandle::GetConstraint()
	{
		return ConstraintHandle;
	}
	
	const typename FJointHandle::FChaosConstraintHandle* FJointHandle::GetConstraint() const
	{
		return ConstraintHandle;
	}

	const Chaos::TVec2<FActorHandle*>& FJointHandle::GetActorHandles()
	{
		return ActorHandles;
	}

	const Chaos::TVec2<const FActorHandle*>& FJointHandle::GetActorHandles() const
	{
		return reinterpret_cast<const Chaos::TVec2<const FActorHandle*>&>(ActorHandles);
	}

	void FJointHandle::SetSoftLinearSettings(bool bLinearSoft, FReal LinearStiffness, FReal LinearDamping)
	{
		using namespace Chaos;
		FPBDJointSettings JointSettings = ConstraintHandle->GetSettings();
		JointSettings.bSoftLinearLimitsEnabled = bLinearSoft;
		JointSettings.SoftLinearStiffness = bLinearSoft ? LinearStiffness : 0.0f;
		JointSettings.SoftLinearDamping = bLinearSoft ? LinearDamping : 0.0f;
		ConstraintHandle->SetSettings(JointSettings);
	}

	void FJointHandle::SetActorInertiaConditioningDirty()
	{
		using namespace Chaos;

		if (ActorHandles[0]->ParticleHandle != nullptr)
		{
			FGenericParticleHandle(ActorHandles[0]->ParticleHandle)->SetInertiaConditioningDirty();
		}

		if (ActorHandles[1]->ParticleHandle != nullptr)
		{
			FGenericParticleHandle(ActorHandles[1]->ParticleHandle)->SetInertiaConditioningDirty();
		}
	}
}

