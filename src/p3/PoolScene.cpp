#include "PoolScene.hpp"
#include "cudaScene.hpp"
#include "constants.hpp"
#include "math/random462.hpp"

#define L 1.7320508

Vector3 ball_initial_positions[SPHERES] = {
	Vector3(0.0, 1.0, 5.0),
	Vector3(-2.0, 1.0, -2.0 * L),
	Vector3(2.0, 1.0, -4.0 * L),
	Vector3(-1.0, 1.0, -3.0 * L),
	Vector3(-2.0, 1.0, -4.0 * L),
	Vector3(-4.0, 1.0, -4.0 * L),
	Vector3(1.0, 1.0, -3.0 * L),
	Vector3(1.0, 1.0, -1.0 * L),
	Vector3(0.0, 1.0, -2.0 * L),
	Vector3(0.0, 1.0, 0.0),
	Vector3(3.0, 1.0, -3.0 * L),
	Vector3(4.0, 1.0, -4.0 * L),
	Vector3(-1.0, 1.0, -L),
	Vector3(0.0, 1.0, -4.0 * L),
	Vector3(-3.0, 1.0, -3.0 * L),
	Vector3(2.0, 1.0, -2.0 * L)
};

Vector3 velocity_acc[SPHERES];
Vector3 position_acc[SPHERES];
int times[SPHERES];


void PoolScene::initialize()
{
	time = 0;
	for (int i = 0; i < SPHERES; i++) {
		bool flag = true;
		while (flag) {
			flag = false;
			balls[i].position = Vector3((2 * random_uniform() - 1) * (TABLE_WIDTH - 1), 1.0, (2 * random_uniform() - 1) * (TABLE_HEIGHT - 1));
			for (int j = 0; j < i; j++) {
				Vector3 dist = -balls[i].position + balls[j].position;
				if (length(dist) < 2.5)
					flag = true;
			}
		}
		balls[i].orientation = Quaternion::Identity();
	}
	//balls[2].velocity = Vector3(3.9, 0, -3.80);
	camera.fov = 0.785;
	camera.aspect = (WIDTH + 0.0) / (HEIGHT + 0.0);
	camera.near_clip = 0.01;
	camera.far_clip = 200.0;
}

static void VectorToFloat3(Vector3 v, float3 &f3)
{
	f3 = make_float3(v.x, v.y, v.z);
}

void PoolScene::toCudaScene(CudaScene &scene)
{
	float dist = tan(camera.fov / 2.0);
	VectorToFloat3(camera.orientation * Vector3(0, 0, -1), scene.dir);
	VectorToFloat3(camera.orientation * Vector3(0, dist, 0), scene.cU);
	VectorToFloat3(camera.orientation * Vector3(dist * camera.aspect, 0, 0), scene.ARcR);
	VectorToFloat3(camera.position, scene.cam_position); 
	for (int i = 0; i < SPHERES; i++) {
		Vector3 v = balls[i].position;
		scene.ball_position[i] = make_float3(v.x, v.y, v.z);
		Quaternion q = balls[i].orientation;
		scene.ball_orientation[i] = make_float4(q.x, q.y, q.z, q.w);
	}
}

void PoolScene::update (float delta_time)
{
	time += delta_time;
	float time_scale = 0.5;
	for (int i = 0; i < SPHERES; i++) {
		velocity_acc[i] = Vector3(0, 0, 0);
		position_acc[i] = Vector3(0, 0, 0);
		times[i] = 0;
	}

	// Collision between spheres
	for (int i = 0; i < SPHERES; i++) {
		for (int j = i + 1; j < SPHERES; j++) {
			Vector3 dist = -balls[i].position + balls[j].position;
			Vector3 vel1 = balls[i].velocity - balls[j].velocity;
			float len = length(dist);
			if (length(dist) < 2) {
				Vector3 vel2 = normalize(dist) * dot(normalize(dist), vel1);
				Vector3 u2 = balls[j].velocity + vel2;
				times[i] ++;
				times[j] ++;
				velocity_acc[i] += balls[i].velocity + balls[j].velocity - u2;
				velocity_acc[j] += u2;
				position_acc[i] -= (2 - len) / 2.0 * normalize(dist);
				position_acc[j] += (2 - len) / 2.0 * normalize(dist);
			}
		}
	}

	float width = TABLE_WIDTH - 1, height = TABLE_HEIGHT - 1;
	// Collision with walls
	for (int i = 0; i < SPHERES; i++) {
		if (balls[i].position.x < -width) {
			velocity_acc[i] += Vector3(fabs(balls[i].velocity.x), 0, balls[i].velocity.z);
			times[i] ++;
		}
		if (balls[i].position.x > width) {
			velocity_acc[i] += Vector3(-fabs(balls[i].velocity.x), 0, balls[i].velocity.z);
			times[i] ++;
		}
		if (balls[i].position.z < -height) {
			velocity_acc[i] += Vector3(balls[i].velocity.x, 0, fabs(balls[i].velocity.z));
			times[i] ++;
		}
		if (balls[i].position.z > height) {
			velocity_acc[i] += Vector3(balls[i].velocity.x, 0, -fabs(balls[i].velocity.z));
			times[i] ++;
		}
	}

	for (int i = 0; i < SPHERES; i++) {
		if (times[i] > 0) {
			balls[i].velocity = velocity_acc[i] / times[i];
			balls[i].position += position_acc[i] / times[i];
		}
	}
	// Update position & orientation;
	for (int i = 0; i < SPHERES; i++) {
		Vector3 distance = balls[i].velocity * delta_time;
		balls[i].position += distance;
		Vector3 axis = normalize(Vector3(balls[i].velocity.z, 0, -balls[i].velocity.x));
		Quaternion rotation = Quaternion(axis, length(distance));
		if (length(distance) == 0) {
			rotation = Quaternion::Identity();
		}
		balls[i].orientation = rotation * balls[i].orientation;
	}

}


	/*
	Vector3 pos(8 * sin(time * time_scale), 4, 8 * cos(time * time_scale));
	Vector3 dir = -normalize(pos);
	Vector3 look(0, 0, -1);
	Vector3 up(0, 1, 0);
	Vector3 v = dir + up * -dot(up, dir);
	Quaternion q = FromToRotation(look, v);
	Quaternion ret = FromToRotation(v, dir) * q;
	pos = Vector3(0, 10, 0);
	ret = Quaternion(-0.707, 0.707, 0, 0);
	pos.to_array(cscene.cam_position);
	ret.to_array(cscene.cam_orientation);
	*/
