// Scene: the Box3D world plus the game entities that mirror its bodies.
#pragma once

#include "box3d/box3d.h"
#include "math_util.h"

#include <utility>
#include <vector>

enum class Mat : uint8_t
{
	Plain,
	Wood,
	Stone,
	Ice,
	Tnt,
	Metal,
	Grass,
	Rock,
	Robe,
	Skin,
	Gold,
	Balloon,
	Dark,
	Shield,
	Rubber,
	Sand,
	Magic, // the magic barrier: a glowing lattice you can see through
	Orb,   // the magic orbs that pass through it
	Glass, // unbreakable glass: see-through, stops everything
	Count
};

enum class Kind : uint8_t
{
	Static,
	Block,
	King,
	Projectile,
	Shard,
	Crown,
	Mechanism,
	Balloon,
	Shield,
};

enum class Geo : uint8_t
{
	Box,
	Sphere,
	Capsule,
	Hull,
	Cylinder, // localPos is the centre of the base; size: radius x, height y
	Cone,	  // same as the cylinder, narrowing to a point
};

struct MatProps
{
	float density;
	float friction;
	float restitution;
	Color color;
	const char* name;
};

const MatProps& GetMatProps( Mat m );

// A renderable piece of an entity, in body local space.
struct Part
{
	Geo geo = Geo::Box;
	Vector3 localPos{ 0, 0, 0 };
	Quaternion localRot{ 0, 0, 0, 1 };
	Vector3 size{ 0.5f, 0.5f, 0.5f }; // box: half extents; sphere: radius in x; capsule: radius x, half segment y
	Mat mat = Mat::Plain;
	Color tint = WHITE;
	const b3HullData* hull = nullptr;
	bool visible = true;
};

enum class TreeKind : uint8_t
{
	Oak,
	Pine,
	Palm,
	Cactus,
};

// Trunk radius and bark colour of a tree of the given kind and size.
float TreeTrunkRadius( TreeKind kind, float scale );
Color TreeBark( TreeKind kind, Color leaf );
Color TreeCutColor( TreeKind kind );

struct Entity
{
	b3BodyId body = b3_nullBodyId;
	int serial = 0; // stable id, also stored as the body name so recordings can be mapped back
	Kind kind = Kind::Block;
	Mat mat = Mat::Plain; // primary material (for gameplay)
	std::vector<Part> parts;

	Vector3 prevPos{}, pos{};
	Quaternion prevRot{ 0, 0, 0, 1 }, rot{ 0, 0, 0, 1 };

	bool alive = true;
	bool isStatic = false;
	float mass = 0.0f;

	// gameplay
	float homeY = 0.0f;		  // top of the island it started on
	bool scoredFall = false;  // already counted as fallen off
	float age = 0.0f;
	float fuse = -1.0f;		  // tnt / bomb countdown
	bool breakQueued = false; // ice shatter
	bool lethal = false;	  // heavy stones, snowballs, collapsing roofs: knock down any king they strike
	bool reinforced = false;  // iron-banded masonry: fixed, and only the boulder breaks it
	float tree = 0.0f;		  // > 0: a standing tree of this size, fixed until a chain shot fells it
	TreeKind treeKind = TreeKind::Oak;
	Color leaf{};
	float ghost = 0.0f;		  // seconds before a felled tree starts colliding with shots again
	int trigger = -1;		  // a brass target: index of the trigger a shot sets off (Scene::triggers)
	float flash = 0.0f;		  // hit flash for rendering

	// king
	bool defeated = false;
	float tiltTimer = 0.0f;
	float defeatTime = 0.0f;
	int crownIndex = -1;

	// projectile
	int ammo = -1;
	bool stuck = false; // sticky bomb welded to what it hit
	bool specialUsed = false;
	bool hasHit = false;
	float restTimer = 0.0f;
	Entity* partner = nullptr; // chain shot partner
	b3Vec3 lastVel{};		   // velocity before the last step, so a shot that smashes through can carry on
};

// A visual rope between two bodies (usually backed by a distance joint).
struct Rope
{
	b3JointId joint = b3_nullJointId; // may be null for purely visual ropes
	b3BodyId bodyA = b3_nullBodyId;
	b3Vec3 localA{};
	b3BodyId bodyB = b3_nullBodyId;
	b3Vec3 localB{};
	float radius = 0.03f;
	Color color{ 120, 90, 60, 255 };
	bool breakable = false;
	float ropeLength = 0.0f; // > 0 for slack ropes, used to draw the sag in replays
	int serialA = 0;		 // entity serials of the two bodies (0 = static ground)
	int serialB = 0;
};

enum class MechType : uint8_t
{
	Windmill,
	Slider,
	Blinker, // a crystal shield that switches on and off on a fixed schedule
	Mover,	 // a kinematic body that travels `amplitude` metres along `axis` and back, pausing at each end
};

struct Mechanism
{
	MechType type;
	b3JointId joint = b3_nullJointId;
	float amplitude = 0.0f;
	float speed = 0.0f;
	float phase = 0.0f;
	Vector3 axis{ 1, 0, 0 }; // Slider: direction of travel

	// Blinker: on for `onTime` seconds out of every `period`, shifted by `phase`
	Entity* entity = nullptr;
	float period = 0.0f;
	float onTime = 0.0f;
	bool on = true;
	float untilToggle = 0.0f; // seconds to the next scheduled switch, for warnings and the HUD
	int timerSlot = 0;		  // where its HUD timer sits on the top edge: -1 left, 0 centre, +1 right
	int timerGroup = -1;	  // shields lined up behind each other share a group (and a combined window)
	int timerOrder = 0;		  // 1 = nearest to the cannon in its group, 2 = next...

	// Mover: `travel` seconds from one end to the other, `pause` seconds at each end; starts from `home`
	Vector3 home{};
	float travel = 0.0f;
	float pause = 0.0f;
	// Mover: it may also turn `turn` radians about `spinAxis` over each run (eased like the travel), and spin
	// steadily at `spin` rad/s, starting from the rotation `baseRot`
	Vector3 spinAxis{ 0, 1, 0 };
	float turn = 0.0f;
	float spin = 0.0f;
	Quaternion baseRot{ 0, 0, 0, 1 };
	// Mover: the area that carries its riders (a floating island), half extents in its own frame: x and z
	// across its top, y how far the body reaches down below its origin. Zero uses its first part.
	Vector3 carry{ 0, 0, 0 };
	float reach = 0.6f; // how far above its top a rider can stand (a king on a tower on the island)
	bool boxes = false; // Mover: every box it is made of is in the way (shields going round a king), not just the first
};

// What a brass target does when a shot strikes it: the joints it lets go of (a latch, a catch).
struct Trigger
{
	std::vector<b3JointId> joints;
	bool shatter = false; // the target itself flies to splinters (a wooden chock)
	bool fired = false;
};

// Where a mover is at simulation time t.
Vector3 MoverPosAt( const Mechanism& m, float t );
// How a mover is turned at simulation time t.
Quaternion MoverRotAt( const Mechanism& m, float t );

// A region of moving air (a fan, an updraft): shots in flight inside the box are pushed with `accel`.
struct AirCurrent
{
	Vector3 center{};
	Vector3 half{};
	Vector3 accel{};
	// a blowhole blows for `onTime` seconds out of every `period` (0: always), shifted by `phase`
	float period = 0.0f;
	float onTime = 0.0f;
	float phase = 0.0f;
	bool BlowsAt( float t ) const
	{
		if ( period <= 0.0f )
		{
			return true;
		}
		float c = fmodf( t + phase, period );
		return ( c < 0.0f ? c + period : c ) < onTime;
	}
};

// Where a blinking shield is in its cycle at simulation time t (ignores any delayed switch-on).
inline bool BlinkerOnAt( const Mechanism& m, float t )
{
	float c = fmodf( t + m.phase, m.period );
	if ( c < 0.0f )
	{
		c += m.period;
	}
	return c < m.onTime;
}

// What an entity looked like, kept after it dies so replays can still draw it.
struct VisualRecord
{
	std::vector<Part> parts;
	Kind kind = Kind::Block;
	int defeatStep = -1; // kings: the step they were knocked out
};

struct HitRecord
{
	Entity* a;
	Entity* b;
	Vector3 point;
	Vector3 normal;
	float speed;
	Mat matA, matB;
};

struct BeginTouchRecord
{
	Entity* a;
	Entity* b;
};

// Categories for collision filtering.
enum Category : uint64_t
{
	CatStatic = 1u << 0,
	CatBlock = 1u << 1,
	CatProjectile = 1u << 2,
	CatDebris = 1u << 3,
	CatKing = 1u << 4,
	CatShield = 1u << 5,
	CatBarrier = 1u << 6, // magic barriers: stop everything but the magic orbs
	CatAll = UINT64_MAX,
};

struct BodyOptions
{
	b3BodyType type = b3_dynamicBody;
	Vector3 velocity{ 0, 0, 0 };
	Vector3 angularVelocity{ 0, 0, 0 };
	float linearDamping = 0.0f;
	float angularDamping = 0.05f;
	float gravityScale = 1.0f;
	bool bullet = false;
	bool awake = true;
	bool allowFastRotation = false;
};

struct ShapeOptions
{
	float densityScale = 1.0f;
	float explosionScale = 1.0f; // how much b3World_Explode pushes this shape (sandbags: much less)
	float friction = -1.0f;	  // < 0 uses material default
	float restitution = -1.0f;
	float rollingResistance = 0.0f;
	uint64_t category = CatBlock;
	uint64_t mask = CatAll;
	int group = 0;
	bool hitEvents = true;
	bool contactEvents = false;
	bool visible = true;
};

class Scene
{
public:
	Scene() = default;
	~Scene();

	void Create( int workerCount );
	void Destroy();
	bool IsValid() const
	{
		return b3World_IsValid( m_worldId );
	}

	b3WorldId World() const
	{
		return m_worldId;
	}

	// One fixed simulation step. Fills hit/touch records and updates entity transforms.
	void Step( float dt, int subSteps );

	// Entities
	Entity* CreateEntity( Kind kind, Mat mat, Vector3 pos, b3Quat rot, const BodyOptions& body );
	void AddBox( Entity* e, Vector3 localPos, b3Quat localRot, Vector3 half, Mat mat, const ShapeOptions& opt = {} );
	void AddSphere( Entity* e, Vector3 localPos, float radius, Mat mat, const ShapeOptions& opt = {} );
	void AddCapsule( Entity* e, Vector3 localPos, float radius, float halfSegment, Mat mat, const ShapeOptions& opt = {} );
	void AddHull( Entity* e, Vector3 localPos, b3Quat localRot, const b3HullData* hull, Mat mat, const ShapeOptions& opt = {} );
	void AddVisual( Entity* e, const Part& part );
	void FinalizeEntity( Entity* e );

	void DestroyEntity( Entity* e ); // marks dead and destroys the body
	void CollectGarbage();			 // frees dead entities

	// Hull cache (owned by the scene, destroyed with it)
	const b3HullData* Cylinder( float height, float radius, float yOffset, int sides );
	const b3HullData* Cone( float height, float radiusBottom, float radiusTop, int slices );
	const b3HullData* RockHull( float radius, uint32_t seed );

	// Shapes and parts of a tree whose base is at the body origin, from height `cut` up
	// (0 = the whole tree; above 0 the trunk starts at the origin, for a tree felled at that height).
	// `probes` (optional) collects the collision shapes as point clouds with a radius, in body space.
	void AddTree( Entity* e, float scale, TreeKind kind, Color leaf, float cut, const ShapeOptions& opt,
				  std::vector<std::pair<std::vector<Vector3>, float>>* probes = nullptr );

	// Joints and ropes
	Rope& AddRope( b3BodyId a, Vector3 worldA, b3BodyId b, Vector3 worldB, float radius, Color color );

	std::vector<Entity*> entities;
	std::vector<Rope> ropes;
	std::vector<Mechanism> mechanisms;
	std::vector<AirCurrent> currents;
	std::vector<Trigger> triggers;
	std::vector<HitRecord> hits;
	std::vector<BeginTouchRecord> touches;
	std::vector<b3JointId> overloadedJoints;
	std::vector<VisualRecord> visuals; // indexed by serial
	b3BodyId groundBody = b3_nullBodyId; // static anchor at origin for joints
	float time = 0.0f;
	int stepCount = 0;

private:
	b3WorldId m_worldId = b3_nullWorldId;
	std::vector<b3HullData*> m_hulls;
	std::vector<Entity*> m_dead;
};

Entity* EntityFromShape( b3ShapeId shapeId );
