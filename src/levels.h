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
	Rng layout; // for Scatter(): changes with every attempt at the level
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
	// Iron-banded masonry: fixed like a ledge (blasts stop at it too), but the boulder smashes it to rubble.
	Entity* Reinforced( Vector3 center, Vector3 half, float yaw = 0.0f );
	// A magic barrier: fixed, stops cannonballs, blasts and blocks, but lets magic orbs through.
	Entity* MagicBarrier( Vector3 center, Vector3 half, float yaw = 0.0f );
	// A heavy glowing orb that passes through magic barriers and knocks down any king it strikes.
	Entity* MagicOrb( Vector3 center, float radius = 0.5f );
	// Unbreakable glass in a brass frame: the king behind it is in plain view, but nothing gets through, blasts
	// included. `moving` makes it kinematic, for Mover().
	Entity* GlassPane( Vector3 center, Vector3 half, float yaw = 0.0f, bool moving = false );
	// A kinematic slab, for Mover().
	Entity* Platform( Vector3 center, Vector3 half, Mat mat, Color tint = { 0, 0, 0, 0 }, float yaw = 0.0f );
	// A flying carpet (kinematic, 12 cm thick, centre at `center`), for Mover(). Kings stay on by friction.
	Entity* Carpet( Vector3 center, float halfX, float halfZ, Color color, float yaw = 0.0f );
	// Sends a kinematic entity `distance` metres along `axis` and back, `travel` seconds each way, resting
	// `pause` seconds at each end; `phase` (seconds) shifts it along the cycle. It is moved to where the cycle
	// puts it at time 0: place riders after this call, on top of e->pos.
	Entity* Mover( Entity* e, Vector3 axis, float distance, float travel, float pause, float phase = 0.0f );
	// A rubber wall: shots bounce off it, so it can be used for bank shots. `moving` makes it kinematic, for
	// Mover(), Turn() and Spin().
	Entity* Bumper( Vector3 center, Vector3 half, float yaw = 0.0f, bool moving = false );
	// Swings a kinematic entity `angle` radians about the vertical through its centre and back, `travel`
	// seconds each way, resting `pause` seconds at each end.
	Entity* Turn( Entity* e, float angle, float travel, float pause, float phase = 0.0f );
	// Keeps a kinematic entity turning about the vertical through its centre at `rate` rad/s.
	Entity* Spin( Entity* e, float rate, float phase = 0.0f );

	// Arcipelago delle Tempeste
	// An island that drifts `distance` metres along `axis` and back (Mover timing), carrying whatever is built
	// on it. `top` is the centre of its surface at rest; build on the returned entity's pos, after the call.
	Entity* FloatingIsland( Vector3 top, float radius, float depth, Vector3 axis, float distance, float travel, float pause, float phase = 0.0f );
	// A big fan on a stand, blowing along its yaw (0 = towards +z): shots in the `length` x `width` x `height`
	// stream ahead of it are pushed with `strength` m/s^2.
	void Fan( Vector3 base, float yaw, float length, float width, float height, float strength );
	// A grate that breathes a column of rising air: shots inside are lifted with `strength` m/s^2 (gravity is 10).
	// With a `period` it is a blowhole: it blows `onTime` seconds out of every `period`, shifted by `phase`.
	void Updraft( Vector3 base, float halfX, float halfZ, float height, float strength, float period = 0.0f, float onTime = 0.0f,
				  float phase = 0.0f );
	// A wooden turntable that keeps turning at `rate` rad/s, carrying what is built on it. `top` is the centre of
	// its surface.
	Entity* Carousel( Vector3 top, float radius, float rate );
	// A wall fixed on the turntable, going round with it: `local` is its centre from the middle of the top.
	void CarouselWall( Entity* carousel, Vector3 local, Vector3 half, float yaw, Mat mat, Color tint );

	// A crystal wall that is solid for `onTime` seconds out of every `period`, shifted by `phase`.
	Entity* Shield( Vector3 center, Vector3 half, float yaw, float period, float onTime, float phase );

	// Fucina del Vulcano: mechanisms set going by a shot. Parts meant to be struck are brass.
	// A lever on a stone fulcrum, lying along `along` (horizontal): the long arm of `kingArm` metres rests on a post
	// and ends in a flat seat, the short arm of `plateArm` ends in a brass plate. A heavy shot dropped on the plate
	// throws the long arm up until it hits its stop, and whoever sits on it flies. Returns the plank; `seat` is
	// where a king's feet go, `plate` the top of the plate.
	Entity* Lever( Vector3 fulcrum, Vector3 along, float kingArm, float plateArm, Vector3& seat, Vector3& plate );
	// A tilting dummy turning freely on a post: a brass shield at one end of its arm and an iron mace at the other,
	// `arm` metres from the post, at `height`. With yaw 0 the shield is on +x facing -z and the mace on -x: strike
	// the shield from the front and the mace swings round through -z. The mace (and the arm) pass through magic
	// barriers and knock down any king they meet. Returns the turning part.
	// `mirror` swaps the ends: shield on -x, mace on +x (it then swings the other way, still through -z).
	Entity* Quintain( Vector3 base, float height, float arm, float yaw, bool mirror = false );
	// A magic orb lying on the ground at `orb`, tied by a rope to a post at `post`: struck, it runs round the post
	// at the end of its rope. Returns the orb.
	Entity* Tether( Vector3 post, Vector3 orb, float radius = 0.45f );
	// A brass target on a post, facing the cannon: a shot that strikes it lets go of the joints handed to Trigger().
	Entity* Target( Vector3 base, float height );
	// Makes `target` let go of `joint` when struck (a target can hold several).
	void Trigger( Entity* target, b3JointId joint );
	// An iron weight hanging from a gantry by a rope, `drop` metres above the ground; the rope is tied to `target`:
	// strike the target and the weight falls. Returns the weight.
	// The gantry's posts stand either side along `across`.
	Entity* HangingWeight( Vector3 ground, float drop, Entity* target, Vector3 across = { 1, 0, 0 } );
	// Iron shields going round `centre`: `count` plates on a circle of `radius`, `height` tall, turning at `rate`.
	// With `roof`, an iron lid turns with them over the king: no lob drops in.
	Entity* OrbitShields( Vector3 centre, float radius, int count, float halfWidth, float height, float rate, float phase = 0.0f,
						  bool roof = false );

	// Picchi Gelati
	// Fixed scenery (ledges, walls, ramps, ice sheets): never moves and never breaks.
	Entity* Ledge( Vector3 center, Vector3 half, Mat mat, Quaternion rot = { 0, 0, 0, 1 }, Color tint = { 0, 0, 0, 0 } );
	// A ramp whose lower edge (top surface) runs along x through `lowEdge`, rising towards +z.
	Entity* Ramp( Vector3 lowEdge, float length, float halfWidth, float angle, Mat mat, Color tint );
	// A granite stone that slides a long way on ice; crushes kings it runs into.
	Entity* CurlingStone( Vector3 base );
	// A heavy ball of packed snow; crushes kings it runs into.
	Entity* Snowball( Vector3 center, float radius );
	// Looks like a snowball, weighs next to nothing: a ball of powder snow that harms no one.
	Entity* Puffball( Vector3 center, float radius );
	// A slab of ice welded between two fixed wooden posts: holds back whatever leans on it until it shatters.
	Entity* Gate( Vector3 center, Vector3 half );
	// A heavy roof of packed snow fringed with icicles, resting on three ice pillars (one in the middle of
	// the front, two behind): shatter the front one and the roof tips forward onto whoever is underneath.
	// Returns the front pillar.
	Entity* SnowShelter( Vector3 base, float halfX, float halfZ, float height );
	// `p` moved at random within +-rx, +-rz (on every attempt a new layout; unchanged in the tests), so the
	// player cannot just repeat the aim that worked last time. Move a king and all that belongs to him by it.
	Vector3 Scatter( Vector3 p, float rx, float rz );
	// Tells the autotest AI (and the demo) to go for `via` instead of `king` while `via` has not moved.
	// `lob` > 0 keeps it to high arcs: horizontal speeds up to `lob` m/s.
	void AimHint( Entity* king, Entity* via, Vector3 offset = { 0, 0, 0 }, float lob = 0.0f );
	// Tells the autotest AI that `king` is reached off the rubber: it searches for a bank shot.
	void BankHint( Entity* king );
	// The wind turns and changes strength after every shot, up to `strength` (the level's wind is the first one).
	void ShiftingWind( float strength );

	// A tree (oak, pine, palm or cactus): fixed, it stops every shot and every blast, but a chain shot fells it.
	// Returns nullptr, and plants nothing, where it would grow into a block or a king.
	Entity* Tree( Vector3 base, float scale, TreeKind kind, Color leaf, float yaw = 0.0f );
	// Trees scattered on a ring around `center`, between `minR` and `radius`.
	void Trees( Vector3 center, float radius, int count, float minR );

	// decoration
	void Flag( Vector3 base, Color color, float scale = 1.0f );
	void Fortress( Vector3 center, float radius );
};
