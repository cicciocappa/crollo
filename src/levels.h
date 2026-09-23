// Level definitions and the builder used to assemble fortresses out of Box3D bodies.
#pragma once

#include "render.h"

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
	const char* id; // stable name used by the save file, never changes once shipped
};

const LevelDef& GetLevel( int index );
int LevelCount();
int FindLevelById( const char* id );

// A campaign: one realm of the Kingdom Above, its king, its look and its levels.
struct Campaign
{
	const char* name;
	const char* king;
	Color robe;
	int biome;
	const char* intro; // read before the first level
	const char* outro; // read after the last one, when the fragment of the crown comes back
	std::vector<int> levels; // indices into the level table, in play order
};

const Campaign& GetCampaign( int index );
int CampaignCount();
int CampaignOfLevel( int levelIndex );	 // -1 if the level belongs to no campaign
int PositionInCampaign( int levelIndex ); // 0-based

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
	const Biome* biome = nullptr;		 // colors of trees, tufts and rocks (Prati Alti when null)
	const Biome& Look() const;

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

	// Picchi Gelati
	// Fixed scenery (ledges, walls, ramps, ice sheets): never moves and never breaks.
	Entity* Ledge( Vector3 center, Vector3 half, Mat mat, Quaternion rot = { 0, 0, 0, 1 }, Color tint = { 0, 0, 0, 0 } );
	// A ramp whose lower edge (top surface) runs along x through `lowEdge`, rising towards +z.
	Entity* Ramp( Vector3 lowEdge, float length, float halfWidth, float angle, Mat mat, Color tint );
	// A granite stone that slides a long way on ice; crushes kings it runs into.
	Entity* CurlingStone( Vector3 base );
	// A heavy ball of packed snow; crushes kings it runs into.
	Entity* Snowball( Vector3 center, float radius );
	// A slab of ice welded between two fixed wooden posts: holds back whatever leans on it until it shatters.
	Entity* Gate( Vector3 center, Vector3 half );
	// A heavy roof of packed snow fringed with icicles, resting on three ice pillars (one in the middle of
	// the front, two behind): shatter the front one and the roof tips forward onto whoever is underneath.
	// Returns the front pillar.
	Entity* SnowShelter( Vector3 base, float halfX, float halfZ, float height );
	// Tells the autotest AI (and the demo) to go for `via` instead of `king` while `via` has not moved.
	void AimHint( Entity* king, Entity* via, Vector3 offset = { 0, 0, 0 } );

	// decoration
	void Trees( Vector3 center, float radius, int count, float minR );
	void Flag( Vector3 base, Color color, float scale = 1.0f );
	void Fortress( Vector3 center, float radius );
};
