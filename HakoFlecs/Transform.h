#pragma once
#include <flecs.h>
#include "Common/Common.h"

namespace comp
{
	struct Position
	{
		float x;
		float y;
		float z;
	};

	struct Rotation
	{
		float x;
		float y;
		float z;
	};

	struct Scale
	{
		float x;
		float y;
		float z;
	};

	struct Transform
	{
		Matrix LocalToWorld;
	};
}
