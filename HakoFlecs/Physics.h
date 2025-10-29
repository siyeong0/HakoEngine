#pragma once
#include "flecs.h"
#include "Math/Math.h"

namespace comp
{
	struct RigidBody
	{
		float Mass;
		float Drag;
		float AngularDrag;
		bool UseGravity;
		bool IsKinematic;

		FVector3 Velocity;
		FVector3 AngularVelocity;
		FVector3 Force;
		FVector3 Torque;
	};
}