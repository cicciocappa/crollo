// Level definitions and the builder used to assemble fortresses out of Box3D bodies.
#pragma once

#include "game.h"

struct LevelDef
{
	const char* name;
	const char* subtitle;
	const char* hint;
	int ammo[(int)Ammo::Count];
	int par; // shots for three stars
	Vector3 wind;
	void ( *build )( Builder& b );
};

const LevelDef& GetLevel( int index );
int LevelCount();

// Endless challenge: fortresses generated from a seed, harder every round.
struct ChallengePlan
{
	int round = 1;
	uint32_t seed = 1;
	int kings = 1;
	int structures = 1;
	bool satellite = false;
	bool tnt = false;
	int mechanism = 0; // 0 none, 1 windmill, 2 sliding shield, 3 balloon
	Vector3 islandPos{ 0, 0, 30 };
	float islandRadius = 8.0f;
	Vector3 wind{ 0, 0, 0 };
	int ammo[(int)Ammo::Count] = {};
	int par = 1;
};

ChallengePlan PlanChallenge( int round, uint32_t seed );
void BuildChallenge( Builder& b );

class Builder
{
public:
	Builder( Game& game, uint32_t seed );

	Game& game;
	Scene& scene;
	Rng rng;
	float homeY = 0.0f;
	Vector3 faceTarget{ 0, 0, 0 }; // kings look at the cannon
	const ChallengePlan* plan = nullptr; // set for procedural rounds

	// terrain
	Entity* Island( Vector3 top, float radius, float depth = 9.0f );
	void PlayerIsland( Vector3 pos = { 0, 0, 0 }, float yaw = 0.0f );

	// basic pieces
	Entity* Box( Vector3 center, Vector3 half, Mat mat, float yaw = 0.0f );
	Entity* Brick( Vector3 center, Mat mat, float yaw = 0.0f )
	{
		return Box( center, { 0.5f, 0.25f, 0.25f }, mat, yaw );
	}
	Entity* King( Vector3 feet, Color robe );
	Entity* Tnt( Vector3 center, float half = 0.35f );
	Entity* Barrel( Vector3 base, Mat mat );

	// structures (return the top height)
	float Tower( Vector3 base, int floors, float halfWidth, float floorHeight, Mat posts, Mat slabs );
	float Column( Vector3 base, int blocks, float half, Mat mat );
	float Wall( Vector3 start, bool alongX, int bricks, int rows, Mat mat );
	float Pyramid( Vector3 base, int levels, float half, Mat mat );
	float Hut( Vector3 base, float halfWidth, float height, Mat walls, Mat roof );

	// mechanisms
	Vector3 RopeBridge( Vector3 a, Vector3 b, int planks, float width, float sag );
	Entity* Pendulum( Vector3 pivot, float length, float ballRadius );
	Entity* Windmill( Vector3 base, float towerHeight, float bladeLength, float speed );
	Entity* Slider( Vector3 center, Vector3 half, Vector3 axis, float amplitude, float speed, float phase, Mat mat );
	Entity* BalloonBasket( Vector3 basketCenter, Color balloonColor, Color robe );
	// Heavy, sluggish bags that soak up hits and shrug off blasts (a quarter of the explosion push).
	Entity* Sandbag( Vector3 center, float yaw = 0.0f );
	float SandbagWall( Vector3 start, bool alongX, int bags, int rows );
	// A fixed rubber wall: shots bounce off it, so it can be used for bank shots.
	Entity* Bumper( Vector3 center, Vector3 half, float yaw = 0.0f );

	// A crystal wall that is solid for `onTime` seconds out of every `period`, shifted by `phase`.
	Entity* Shield( Vector3 center, Vector3 half, float yaw, float period, float onTime, float phase );

	// decoration
	void Trees( Vector3 center, float radius, int count, float minR );
	void Flag( Vector3 base, Color color, float scale = 1.0f );
	void Fortress( Vector3 center, float radius );
};
