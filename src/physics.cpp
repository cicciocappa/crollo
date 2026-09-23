#include "physics.h"

#include <algorithm>
#include <cstdlib>

Rng& FxRng()
{
	static Rng rng( 0xC0FFEEu );
	return rng;
}

static const MatProps s_matProps[(int)Mat::Count] = {
	// density, friction, restitution, color, name
	{ 1000.0f, 0.6f, 0.0f, { 200, 200, 200, 255 }, "plain" },
	{ 450.0f, 0.75f, 0.05f, { 186, 128, 74, 255 }, "legno" },
	{ 2000.0f, 0.85f, 0.0f, { 158, 156, 164, 255 }, "pietra" },
	{ 850.0f, 0.12f, 0.05f, { 170, 226, 255, 255 }, "ghiaccio" },
	{ 600.0f, 0.6f, 0.0f, { 206, 48, 38, 255 }, "tnt" },
	{ 7000.0f, 0.35f, 0.05f, { 64, 66, 74, 255 }, "ferro" },
	{ 2000.0f, 0.9f, 0.0f, { 104, 168, 72, 255 }, "erba" },
	{ 2500.0f, 0.8f, 0.0f, { 120, 104, 92, 255 }, "roccia" },
	{ 600.0f, 0.7f, 0.0f, { 128, 40, 160, 255 }, "tunica" },
	{ 600.0f, 0.7f, 0.0f, { 255, 214, 170, 255 }, "pelle" },
	{ 1500.0f, 0.5f, 0.1f, { 255, 200, 40, 255 }, "oro" },
	{ 1.0f, 0.4f, 0.4f, { 230, 60, 80, 255 }, "pallone" },
	{ 1000.0f, 0.6f, 0.0f, { 40, 36, 40, 255 }, "scuro" },
	{ 1000.0f, 0.3f, 0.3f, { 110, 215, 255, 255 }, "cristallo" },
	{ 1100.0f, 0.9f, 0.9f, { 225, 85, 60, 255 }, "gomma" },
	{ 2600.0f, 1.0f, 0.0f, { 196, 170, 122, 255 }, "sabbia" },
	{ 1000.0f, 0.3f, 0.2f, { 170, 110, 255, 255 }, "magia" },
	{ 1500.0f, 0.2f, 0.1f, { 190, 140, 255, 255 }, "sfera magica" },
};

const MatProps& GetMatProps( Mat m )
{
	return s_matProps[(int)m];
}

Entity* EntityFromShape( b3ShapeId shapeId )
{
	if ( b3Shape_IsValid( shapeId ) == false )
	{
		return nullptr;
	}
	return (Entity*)b3Shape_GetUserData( shapeId );
}

Scene::~Scene()
{
	Destroy();
}

void Scene::Create( int workerCount )
{
	Destroy();

	b3WorldDef def = b3DefaultWorldDef();
	def.gravity = { 0.0f, -10.0f, 0.0f };
	def.hitEventThreshold = 1.2f;
	def.workerCount = (uint32_t)std::max( 1, workerCount );
	def.enableSleep = true;
	def.enableContinuous = true;
	m_worldId = b3CreateWorld( &def );

	b3BodyDef bodyDef = b3DefaultBodyDef();
	groundBody = b3CreateBody( m_worldId, &bodyDef );

	time = 0.0f;
	stepCount = 0;
}

void Scene::Destroy()
{
	for ( Entity* e : entities )
	{
		delete e;
	}
	entities.clear();
	for ( Entity* e : m_dead )
	{
		delete e;
	}
	m_dead.clear();
	ropes.clear();
	mechanisms.clear();
	visuals.clear();
	hits.clear();
	touches.clear();
	overloadedJoints.clear();

	if ( b3World_IsValid( m_worldId ) )
	{
		b3DestroyWorld( m_worldId );
	}
	m_worldId = b3_nullWorldId;
	groundBody = b3_nullBodyId;

	for ( b3HullData* h : m_hulls )
	{
		b3DestroyHull( h );
	}
	m_hulls.clear();
}

void Scene::Step( float dt, int subSteps )
{
	hits.clear();
	touches.clear();
	overloadedJoints.clear();

	for ( Entity* e : entities )
	{
		e->prevPos = e->pos;
		e->prevRot = e->rot;
	}

	b3World_Step( m_worldId, dt, subSteps );
	time += dt;
	stepCount += 1;

	// Body move events only report bodies that actually moved, which is the cheap way to sync transforms.
	b3BodyEvents bodyEvents = b3World_GetBodyEvents( m_worldId );
	for ( int i = 0; i < bodyEvents.moveCount; ++i )
	{
		const b3BodyMoveEvent& ev = bodyEvents.moveEvents[i];
		Entity* e = (Entity*)ev.userData;
		if ( e == nullptr || e->alive == false )
		{
			continue;
		}
		e->pos = ToRl( ev.transform.p );
		e->rot = ToRl( ev.transform.q );
	}

	b3ContactEvents contactEvents = b3World_GetContactEvents( m_worldId );
	for ( int i = 0; i < contactEvents.hitCount; ++i )
	{
		const b3ContactHitEvent& ev = contactEvents.hitEvents[i];
		Entity* a = EntityFromShape( ev.shapeIdA );
		Entity* b = EntityFromShape( ev.shapeIdB );
		if ( a == nullptr || b == nullptr )
		{
			continue;
		}
		HitRecord rec;
		rec.a = a;
		rec.b = b;
		rec.point = ToRl( ev.point );
		rec.normal = ToRl( ev.normal );
		rec.speed = ev.approachSpeed;
		rec.matA = (Mat)ev.userMaterialIdA;
		rec.matB = (Mat)ev.userMaterialIdB;
		hits.push_back( rec );
	}

	for ( int i = 0; i < contactEvents.beginCount; ++i )
	{
		const b3ContactBeginTouchEvent& ev = contactEvents.beginEvents[i];
		Entity* a = EntityFromShape( ev.shapeIdA );
		Entity* b = EntityFromShape( ev.shapeIdB );
		if ( a == nullptr || b == nullptr )
		{
			continue;
		}
		touches.push_back( { a, b } );
	}

	// Joints that exceeded their force threshold. Ropes use this to snap.
	b3JointEvents jointEvents = b3World_GetJointEvents( m_worldId );
	for ( int i = 0; i < jointEvents.count; ++i )
	{
		overloadedJoints.push_back( jointEvents.jointEvents[i].jointId );
	}
}

Entity* Scene::CreateEntity( Kind kind, Mat mat, Vector3 pos, b3Quat rot, const BodyOptions& opt )
{
	Entity* e = new Entity();
	if ( visuals.empty() )
	{
		visuals.emplace_back(); // serial 0 is reserved for "no entity"
	}
	e->serial = (int)visuals.size();
	visuals.emplace_back();
	e->kind = kind;
	e->mat = mat;
	e->pos = e->prevPos = pos;
	e->rot = e->prevRot = ToRl( rot );
	e->isStatic = opt.type == b3_staticBody;
	e->homeY = pos.y;

	b3BodyDef bodyDef = b3DefaultBodyDef();
	bodyDef.type = opt.type;
	bodyDef.position = ToB3( pos );
	bodyDef.rotation = rot;
	bodyDef.linearVelocity = ToB3( opt.velocity );
	bodyDef.angularVelocity = ToB3( opt.angularVelocity );
	bodyDef.linearDamping = opt.linearDamping;
	bodyDef.angularDamping = opt.angularDamping;
	bodyDef.gravityScale = opt.gravityScale;
	bodyDef.isBullet = opt.bullet;
	bodyDef.isAwake = opt.awake;
	bodyDef.allowFastRotation = opt.allowFastRotation;
	bodyDef.userData = e;
	bodyDef.name = TextFormat( "%d", e->serial );
	e->body = b3CreateBody( m_worldId, &bodyDef );

	entities.push_back( e );
	return e;
}

static b3ShapeDef MakeShapeDef( Entity* e, Mat mat, const ShapeOptions& opt )
{
	const MatProps& props = GetMatProps( mat );
	b3ShapeDef def = b3DefaultShapeDef();
	def.density = props.density * opt.densityScale;
	def.explosionScale = opt.explosionScale;
	def.baseMaterial.friction = opt.friction >= 0.0f ? opt.friction : props.friction;
	def.baseMaterial.restitution = opt.restitution >= 0.0f ? opt.restitution : props.restitution;
	def.baseMaterial.rollingResistance = opt.rollingResistance;
	def.baseMaterial.userMaterialId = (uint64_t)mat;
	def.filter.categoryBits = opt.category;
	def.filter.maskBits = opt.mask;
	def.filter.groupIndex = opt.group;
	def.enableHitEvents = opt.hitEvents;
	def.enableContactEvents = opt.contactEvents;
	def.userData = e;
	return def;
}

void Scene::AddBox( Entity* e, Vector3 localPos, b3Quat localRot, Vector3 half, Mat mat, const ShapeOptions& opt )
{
	b3ShapeDef def = MakeShapeDef( e, mat, opt );
	b3Transform xf{ ToB3( localPos ), localRot };
	b3BoxHull box = b3MakeTransformedBoxHull( half.x, half.y, half.z, xf );
	b3CreateHullShape( e->body, &def, &box.base );

	Part p;
	p.geo = Geo::Box;
	p.localPos = localPos;
	p.localRot = ToRl( localRot );
	p.size = half;
	p.mat = mat;
	p.tint = GetMatProps( mat ).color;
	p.visible = opt.visible;
	e->parts.push_back( p );
}

void Scene::AddSphere( Entity* e, Vector3 localPos, float radius, Mat mat, const ShapeOptions& opt )
{
	b3ShapeDef def = MakeShapeDef( e, mat, opt );
	b3Sphere sphere{ ToB3( localPos ), radius };
	b3CreateSphereShape( e->body, &def, &sphere );

	Part p;
	p.geo = Geo::Sphere;
	p.localPos = localPos;
	p.size = { radius, radius, radius };
	p.mat = mat;
	p.tint = GetMatProps( mat ).color;
	p.visible = opt.visible;
	e->parts.push_back( p );
}

void Scene::AddCapsule( Entity* e, Vector3 localPos, float radius, float halfSegment, Mat mat, const ShapeOptions& opt )
{
	b3ShapeDef def = MakeShapeDef( e, mat, opt );
	b3Capsule capsule{ ToB3( Vector3Add( localPos, { 0, -halfSegment, 0 } ) ), ToB3( Vector3Add( localPos, { 0, halfSegment, 0 } ) ),
					   radius };
	b3CreateCapsuleShape( e->body, &def, &capsule );

	Part p;
	p.geo = Geo::Capsule;
	p.localPos = localPos;
	p.size = { radius, halfSegment, radius };
	p.mat = mat;
	p.tint = GetMatProps( mat ).color;
	p.visible = opt.visible;
	e->parts.push_back( p );
}

void Scene::AddHull( Entity* e, Vector3 localPos, b3Quat localRot, const b3HullData* hull, Mat mat, const ShapeOptions& opt )
{
	b3ShapeDef def = MakeShapeDef( e, mat, opt );
	b3Transform xf{ ToB3( localPos ), localRot };
	b3CreateTransformedHullShape( e->body, &def, hull, xf, b3Vec3_one );

	Part p;
	p.geo = Geo::Hull;
	p.localPos = localPos;
	p.localRot = ToRl( localRot );
	p.size = { 1, 1, 1 };
	p.mat = mat;
	p.tint = GetMatProps( mat ).color;
	p.hull = hull;
	p.visible = opt.visible;
	e->parts.push_back( p );
}

void Scene::AddVisual( Entity* e, const Part& part )
{
	e->parts.push_back( part );
}

void Scene::FinalizeEntity( Entity* e )
{
	VisualRecord& vr = visuals[e->serial];
	vr.parts = e->parts;
	vr.kind = e->kind;

	if ( e->isStatic == false )
	{
		e->mass = b3Body_GetMass( e->body );
	}
	b3WorldTransform xf = b3Body_GetTransform( e->body );
	e->pos = e->prevPos = ToRl( xf.p );
	e->rot = e->prevRot = ToRl( xf.q );
}

void Scene::DestroyEntity( Entity* e )
{
	if ( e == nullptr || e->alive == false )
	{
		return;
	}
	e->alive = false;
	if ( b3Body_IsValid( e->body ) )
	{
		b3DestroyBody( e->body );
	}
	e->body = b3_nullBodyId;
}

void Scene::CollectGarbage()
{
	// Free entities that died on a previous frame. Deferring one frame keeps pointers held
	// by event records valid while they are being processed.
	for ( Entity* e : m_dead )
	{
		delete e;
	}
	m_dead.clear();

	auto it = std::remove_if( entities.begin(), entities.end(), [this]( Entity* e ) {
		if ( e->alive == false )
		{
			m_dead.push_back( e );
			return true;
		}
		return false;
	} );
	entities.erase( it, entities.end() );

	for ( Entity* e : entities )
	{
		if ( e->partner != nullptr && e->partner->alive == false )
		{
			e->partner = nullptr;
		}
	}
}

const b3HullData* Scene::Cylinder( float height, float radius, float yOffset, int sides )
{
	b3HullData* h = b3CreateCylinder( height, radius, yOffset, sides );
	m_hulls.push_back( h );
	return h;
}

const b3HullData* Scene::Cone( float height, float radiusBottom, float radiusTop, int slices )
{
	b3HullData* h = b3CreateCone( height, radiusBottom, radiusTop, slices );
	m_hulls.push_back( h );
	return h;
}

const b3HullData* Scene::RockHull( float radius, uint32_t seed )
{
	// A lumpy rock: points on a jittered sphere, wrapped with a convex hull.
	Rng rng( seed * 7919u + 17u );
	b3Vec3 points[24];
	int count = 20;
	for ( int i = 0; i < count; ++i )
	{
		Vector3 d = rng.OnSphere();
		float r = radius * rng.Range( 0.75f, 1.05f );
		points[i] = { d.x * r, d.y * r * 0.85f, d.z * r };
	}
	b3HullData* h = b3CreateHull( points, count, count );
	if ( h == nullptr )
	{
		h = b3CreateRock( radius );
	}
	m_hulls.push_back( h );
	return h;
}

Rope& Scene::AddRope( b3BodyId a, Vector3 worldA, b3BodyId b, Vector3 worldB, float radius, Color color )
{
	Rope r;
	r.bodyA = a;
	r.bodyB = b;
	r.localA = b3Body_GetLocalPoint( a, ToB3( worldA ) );
	r.localB = b3Body_GetLocalPoint( b, ToB3( worldB ) );
	r.radius = radius;
	r.color = color;
	r.serialA = atoi( b3Body_GetName( a ) );
	r.serialB = atoi( b3Body_GetName( b ) );
	ropes.push_back( r );
	return ropes.back();
}
