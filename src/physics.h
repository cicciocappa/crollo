// Scene: the Box3D world plus the game entities that mirror its bodies.
#pragma once

#include "box3d/box3d.h"
#include "math_util.h"

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
	float flash = 0.0f;		  // hit flash for rendering

	// king
	bool defeated = false;
	float tiltTimer = 0.0f;
	float defeatTime = 0.0f;
	int crownIndex = -1;

	// projectile
	int ammo = -1;
	bool specialUsed = false;
	bool hasHit = false;
	float restTimer = 0.0f;
	Entity* partner = nullptr; // chain shot partner
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
};

struct Mechanism
{
	MechType type;
	b3JointId joint = b3_nullJointId;
	float amplitude = 0.0f;
	float speed = 0.0f;
	float phase = 0.0f;

	// Blinker: on for `onTime` seconds out of every `period`, shifted by `phase`
	Entity* entity = nullptr;
	float period = 0.0f;
	float onTime = 0.0f;
	bool on = true;
	float untilToggle = 0.0f; // seconds to the next scheduled switch, for warnings and the HUD
	int timerSlot = 0;		  // where its HUD timer sits on the top edge: -1 left, 0 centre, +1 right
	int timerGroup = -1;	  // shields lined up behind each other share a group (and a combined window)
	int timerOrder = 0;		  // 1 = nearest to the cannon in its group, 2 = next...
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

	// Joints and ropes
	Rope& AddRope( b3BodyId a, Vector3 worldA, b3BodyId b, Vector3 worldB, float radius, Color color );

	std::vector<Entity*> entities;
	std::vector<Rope> ropes;
	std::vector<Mechanism> mechanisms;
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
