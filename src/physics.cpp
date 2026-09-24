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
	{ 2500.0f, 0.3f, 0.25f, { 205, 232, 240, 255 }, "vetro" },
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
	currents.clear();
	triggers.clear();
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

// How far along its run a mover is at time t: 0 at home, 1 at the far end, eased at both ends so whatever
// rides on it does not slide off.
static float MoverProgress( const Mechanism& m, float t )
{
	// out, rest, back, rest
	float cycle = 2.0f * ( m.travel + m.pause );
	if ( cycle <= 0.0f )
	{
		return 0.0f;
	}
	float u = fmodf( t + m.phase, cycle );
	if ( u < 0.0f )
	{
		u += cycle;
	}
	float f;
	if ( u < m.pause )
	{
		f = 0.0f;
	}
	else if ( u < m.pause + m.travel )
	{
		f = ( u - m.pause ) / m.travel;
	}
	else if ( u < 2.0f * m.pause + m.travel )
	{
		f = 1.0f;
	}
	else
	{
		f = 1.0f - ( u - 2.0f * m.pause - m.travel ) / m.travel;
	}
	return f * f * ( 3.0f - 2.0f * f );
}

Vector3 MoverPosAt( const Mechanism& m, float t )
{
	return Vector3Add( m.home, Vector3Scale( m.axis, m.amplitude * MoverProgress( m, t ) ) );
}

Quaternion MoverRotAt( const Mechanism& m, float t )
{
	if ( m.turn == 0.0f && m.spin == 0.0f )
	{
		return m.baseRot;
	}
	float angle = m.turn * MoverProgress( m, t ) + m.spin * ( t + m.phase );
	return QuaternionNormalize( QuaternionMultiply( QuaternionFromAxisAngle( m.spinAxis, angle ), m.baseRot ) );
}

float TreeTrunkRadius( TreeKind kind, float scale )
{
	switch ( kind )
	{
		case TreeKind::Pine:
			return 0.12f * scale;
		case TreeKind::Palm:
			return 0.15f * scale;
		case TreeKind::Cactus:
			return 0.22f * scale;
		default:
			return 0.14f * scale;
	}
}

Color TreeBark( TreeKind kind, Color leaf )
{
	switch ( kind )
	{
		case TreeKind::Pine:
			return Color{ 100, 70, 45, 255 };
		case TreeKind::Palm:
			return Color{ 130, 100, 64, 255 };
		case TreeKind::Cactus:
			return leaf;
		default:
			return Color{ 110, 76, 48, 255 };
	}
}

Color TreeCutColor( TreeKind kind )
{
	return kind == TreeKind::Cactus ? Color{ 200, 215, 150, 255 } : Color{ 222, 190, 140, 255 };
}

void Scene::AddTree( Entity* e, float scale, TreeKind kind, Color leaf, float cut, const ShapeOptions& opt,
					 std::vector<std::pair<std::vector<Vector3>, float>>* probes )
{
	float s = scale;
	float radius = TreeTrunkRadius( kind, s );
	Color bark = TreeBark( kind, leaf );
	auto at = [&]( float x, float y, float z ) { return Vector3{ x * s, y * s - cut, z * s }; };
	auto probe = [&]( std::vector<Vector3> points, float r ) {
		if ( probes )
		{
			probes->push_back( { points, r } );
		}
	};
	auto visual = [&]( Geo geo, Vector3 pos, Vector3 size, Mat mat, Color tint, Quaternion rot = { 0, 0, 0, 1 } ) {
		Part p;
		p.geo = geo;
		p.localPos = pos;
		p.localRot = rot;
		p.size = size;
		p.mat = mat;
		p.tint = tint;
		AddVisual( e, p );
	};
	ShapeOptions hidden = opt;
	hidden.visible = false;
	// the leaves are light
	ShapeOptions leaves = opt;
	leaves.densityScale = opt.densityScale * 0.12f;
	ShapeOptions hiddenLeaves = leaves;
	hiddenLeaves.visible = false;

	// a straight trunk: a capsule inside, a cylinder to look at (oaks, pines and the cactus column)
	auto straightTrunk = [&]( float height, Mat mat, const ShapeOptions& so ) {
		float length = height - cut;
		if ( length <= 0.05f )
		{
			return;
		}
		float half = std::max( 0.0f, length * 0.5f - radius );
		AddCapsule( e, { 0, length * 0.5f, 0 }, radius, half, mat, so );
		e->parts.back().visible = false;
		visual( Geo::Cylinder, { 0, 0, 0 }, { radius, length, radius }, mat, bark );
		probe( { { 0, radius, 0 }, { 0, length - radius, 0 } }, radius );
	};

	switch ( kind )
	{
		case TreeKind::Oak:
		{
			straightTrunk( 1.6f * s, Mat::Wood, hidden );
			const Vector3 centres[3] = { { 0, 2.0f, 0 }, { 0.55f, 1.65f, 0.2f }, { -0.45f, 1.75f, -0.3f } };
			const float radii[3] = { 0.9f, 0.6f, 0.62f };
			const float shade[3] = { 0.0f, -0.1f, 0.08f };
			for ( int i = 0; i < 3; ++i )
			{
				Vector3 c = at( centres[i].x, centres[i].y, centres[i].z );
				AddSphere( e, c, radii[i] * s, Mat::Plain, leaves );
				e->parts.back().tint = ColorBrightness( leaf, shade[i] );
				probe( { c }, radii[i] * s );
			}
			break;
		}
		case TreeKind::Pine:
		{
			straightTrunk( 0.8f * s, Mat::Wood, hidden );
			Vector3 base = at( 0, 0.6f, 0 );
			AddHull( e, base, b3Quat_identity, Cone( 2.4f * s, 0.95f * s, 0.06f * s, 10 ), Mat::Plain, hiddenLeaves );
			std::vector<Vector3> ring;
			for ( int k = 0; k < 8; ++k )
			{
				float a = k * PI / 4.0f;
				ring.push_back( Vector3Add( base, { cosf( a ) * 0.95f * s, 0, sinf( a ) * 0.95f * s } ) );
			}
			ring.push_back( at( 0, 3.0f, 0 ) );
			probe( ring, 0.0f );
			for ( int i = 0; i < 3; ++i )
			{
				float r = ( 0.95f - i * 0.25f ) * s;
				visual( Geo::Cone, at( 0, 0.6f + i * 0.65f, 0 ), { r, 1.1f * s, r }, Mat::Plain, ColorBrightness( leaf, -0.08f * i ) );
			}
			break;
		}
		case TreeKind::Palm:
		{
			// a trunk that bends away over its height, in four ringed segments getting thinner
			const float H = 3.2f, lean = 0.45f;
			auto trunkAt = [&]( float f ) { return at( lean * f * f, H * f, 0 ); };
			float f0 = std::min( 0.9f, cut / ( H * s ) );
			Vector3 foot = trunkAt( f0 ), top = trunkAt( 1.0f );
			Vector3 axis = Vector3Subtract( top, foot );
			float len = Vector3Length( axis );
			Quaternion tilt = QuaternionFromVector3ToVector3( { 0, 1, 0 }, Vector3Scale( axis, 1.0f / len ) );
			AddHull( e, foot, ToB3( tilt ), Cylinder( len, radius * 0.9f, 0.0f, 8 ), Mat::Wood, hidden );
			probe( { foot, top }, radius * 0.9f );
			const int segments = 4;
			for ( int i = 0; i < segments; ++i )
			{
				float fa = f0 + ( 1.0f - f0 ) * i / segments, fb = f0 + ( 1.0f - f0 ) * ( i + 1 ) / segments;
				Vector3 pa = trunkAt( fa ), pb = trunkAt( fb );
				Vector3 d = Vector3Subtract( pb, pa );
				float r = radius * ( 1.0f - 0.25f * fa );
				Quaternion q = QuaternionFromVector3ToVector3( { 0, 1, 0 }, Vector3Normalize( d ) );
				visual( Geo::Cylinder, pa, { r, Vector3Length( d ) + 0.02f, r }, Mat::Wood, ColorBrightness( bark, i % 2 ? -0.08f : 0.0f ), q );
			}
			// the crown: drooping fronds around a few coconuts, solid as a flat cone
			Vector3 crown = top;
			AddHull( e, Vector3Add( crown, { 0, -0.6f * s, 0 } ), b3Quat_identity, Cone( 0.7f * s, 1.6f * s, 0.45f * s, 8 ), Mat::Plain, hiddenLeaves );
			std::vector<Vector3> ring;
			for ( int k = 0; k < 8; ++k )
			{
				float a = k * PI / 4.0f;
				ring.push_back( Vector3Add( crown, { cosf( a ) * 1.6f * s, -0.6f * s, sinf( a ) * 1.6f * s } ) );
				ring.push_back( Vector3Add( crown, { cosf( a ) * 0.45f * s, 0.1f * s, sinf( a ) * 0.45f * s } ) );
			}
			probe( ring, 0.0f );
			for ( int k = 0; k < 7; ++k )
			{
				float a = k * 2.0f * PI / 7.0f + 0.3f;
				Vector3 dir{ cosf( a ), 0.0f, sinf( a ) };
				Quaternion q = QuaternionMultiply( QuaternionFromAxisAngle( { 0, 1, 0 }, -a ), QuaternionFromAxisAngle( { 0, 0, 1 }, -0.38f ) );
				visual( Geo::Sphere, Vector3Add( crown, Vector3Add( Vector3Scale( dir, 0.8f * s ), { 0, -0.28f * s, 0 } ) ),
						{ 0.95f * s, 0.05f * s, 0.26f * s }, Mat::Plain, ColorBrightness( leaf, k % 2 ? -0.1f : 0.05f ), q );
			}
			for ( int k = 0; k < 3; ++k )
			{
				float a = k * 2.0f * PI / 3.0f + 1.0f;
				Vector3 dir{ cosf( a ), 0.0f, sinf( a ) };
				Quaternion q = QuaternionMultiply( QuaternionFromAxisAngle( { 0, 1, 0 }, -a ), QuaternionFromAxisAngle( { 0, 0, 1 }, 0.5f ) );
				visual( Geo::Sphere, Vector3Add( crown, Vector3Add( Vector3Scale( dir, 0.45f * s ), { 0, 0.2f * s, 0 } ) ),
						{ 0.6f * s, 0.05f * s, 0.2f * s }, Mat::Plain, ColorBrightness( leaf, 0.12f ), q );
				visual( Geo::Sphere, Vector3Add( crown, Vector3Add( Vector3Scale( dir, 0.17f * s ), { 0, -0.2f * s, 0 } ) ),
						{ 0.11f * s, 0.11f * s, 0.11f * s }, Mat::Wood, Color{ 105, 75, 40, 255 } );
			}
			break;
		}
		case TreeKind::Cactus:
		{
			// a saguaro: a tall column with two arms, and a flower on top
			ShapeOptions flesh = hidden;
			flesh.densityScale = opt.densityScale * 0.5f;
			straightTrunk( 2.3f * s, Mat::Plain, flesh );
			float column = 2.3f * s - cut;
			if ( column > 0.05f )
			{
				visual( Geo::Sphere, { 0, column, 0 }, { radius, radius, radius }, Mat::Plain, bark );
				visual( Geo::Sphere, { 0, column + radius * 0.95f, 0 }, { 0.07f * s, 0.07f * s, 0.07f * s }, Mat::Plain, Color{ 245, 130, 165, 255 } );
			}
			float armR = 0.14f * s;
			auto arm = [&]( float side, float y, float reach, float up ) {
				Vector3 elbow = at( side * reach, y, 0 );
				Vector3 from = at( side * 0.1f, y, 0 );
				float halfX = fabsf( elbow.x - from.x ) * 0.5f;
				Vector3 mid = { ( elbow.x + from.x ) * 0.5f, elbow.y, 0 };
				AddBox( e, mid, b3Quat_identity, { halfX, armR * 0.9f, armR * 0.9f }, Mat::Plain, flesh );
				e->parts.back().visible = false;
				Quaternion q = QuaternionFromAxisAngle( { 0, 0, 1 }, side > 0.0f ? -PI * 0.5f : PI * 0.5f );
				visual( Geo::Cylinder, from, { armR, halfX * 2.0f, armR }, Mat::Plain, bark, q );
				float rise = up * s;
				AddCapsule( e, Vector3Add( elbow, { 0, rise * 0.5f, 0 } ), armR, rise * 0.5f, Mat::Plain, flesh );
				e->parts.back().visible = false;
				visual( Geo::Sphere, elbow, { armR, armR, armR }, Mat::Plain, bark );
				visual( Geo::Cylinder, elbow, { armR, rise, armR }, Mat::Plain, bark );
				visual( Geo::Sphere, Vector3Add( elbow, { 0, rise, 0 } ), { armR, armR, armR }, Mat::Plain, bark );
				probe( { from, elbow }, armR );
				probe( { elbow, Vector3Add( elbow, { 0, rise, 0 } ) }, armR );
			};
			arm( 1.0f, 0.95f, 0.58f, 0.7f );
			arm( -1.0f, 1.3f, 0.52f, 0.55f );
			break;
		}
	}
	if ( cut > 0.0f )
	{
		// the fresh cut at the bottom
		visual( Geo::Cylinder, { 0, -0.01f, 0 }, { radius * 0.8f, 0.01f, radius * 0.8f }, Mat::Plain, TreeCutColor( kind ) );
	}
	e->tree = scale;
	e->treeKind = kind;
	e->leaf = leaf;
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
