#ifndef __CUDA_SCENE
#define __CUDA_SCENE

#include "constants.hpp"
#include <curand.h>
#include <curand_kernel.h>
#include <cuda_runtime.h>
#include <cuda.h>

struct CudaScene
{
	// Sphere position & orientation
	float4 ball_orientation[SPHERES];
	float3 ball_position[SPHERES];

	// Camera
	float3 cam_position;
	float3 dir;
	float3 cU;
	float3 ARcR;

	int y0; // render offset
	int render_height;
};

#endif

