#include "levels.h"

#include <algorithm>
#include <cstring>
#include <cfloat>

// Royal robe palette
static const Color kPurple{ 128, 40, 160, 255 };
static const Color kCrimson{ 180, 30, 50, 255 };
static const Color kBlue{ 40, 80, 190, 255 };
static const Color kGreen{ 30, 130, 70, 255 };
static const Color kOrange{ 220, 120, 30, 255 };
static const Color kTeal{ 20, 140, 150, 255 };

// ---------------------------------------------------------------------------------------------
// Builder
// ---------------------------------------------------------------------------------------------

Builder::Builder( Game& g, uint32_t seed )
	: game( g )
	, scene( g.GetScene() )
	, rng( seed )
{
}

const Biome& Builder::Look() const
{
	return biome ? *biome : GetBiome( 0 );
}

Entity* Builder::Island( Vector3 top, float radius, float depth )
{
	BodyOptions bo;
	bo.type = b3_staticBody;
	Entity* e = scene.CreateEntity( Kind::Static, Mat::Grass, top, b3Quat_identity, bo );

	ShapeOptions so;
	so.category = CatStatic;
	so.hitEvents = false;
	const float slab = 1.4f;
	scene.AddHull( e, { 0, 0, 0 }, b3Quat_identity, scene.Cylinder( slab, radius, -slab, 24 ), Mat::Grass, so );
	scene.AddHull( e, { 0, -slab - depth, 0 }, b3Quat_identity, scene.Cone( depth, radius * 0.18f, radius * 0.94f, 14 ), Mat::Rock, so );

	// a couple of hanging rocky lumps for a less regular silhouette
	for ( int i = 0; i < 3; ++i )
	{
		float a = rng.Range( 0.0f, 2.0f * PI );
		float r = radius * rng.Range( 0.3f, 0.55f );
		float h = depth * rng.Range( 0.35f, 0.6f );
		Vector3 p{ cosf( a ) * r, -slab - h, sinf( a ) * r };
		scene.AddHull( e, p, b3Quat_identity, scene.Cone( h, radius * 0.08f, radius * rng.Range( 0.3f, 0.45f ), 10 ), Mat::Rock, so );
	}
	for ( Part& part : e->parts )
	{
		if ( part.mat == Mat::Rock )
		{
			part.tint = Look().rock;
		}
	}
	scene.FinalizeEntity( e );

	// scattered grass tufts and pebbles
	int tufts = (int)( radius * radius * 0.35f );
	for ( int i = 0; i < tufts; ++i )
	{
		float a = rng.Range( 0.0f, 2.0f * PI );
		float r = radius * sqrtf( rng.Float() ) * 0.95f;
		Decoration d;
		d.type = Decoration::Grass;
		d.pos = { top.x + cosf( a ) * r, top.y, top.z + sinf( a ) * r };
		d.scale = rng.Range( 0.7f, 1.3f );
		d.rot = rng.Range( 0.0f, 6.28f );
		d.color = ColorMix( Look().tuftA, Look().tuftB, rng.Float() );
		game.AddDecoration( d );
	}
	for ( int i = 0; i < (int)( radius * 0.6f ); ++i )
	{
		float a = rng.Range( 0.0f, 2.0f * PI );
		float r = radius * rng.Range( 0.75f, 0.95f );
		Decoration d;
		d.type = Decoration::Rock;
		d.pos = { top.x + cosf( a ) * r, top.y, top.z + sinf( a ) * r };
		d.scale = rng.Range( 0.15f, 0.35f );
		d.rot = rng.Range( 0.0f, 6.28f );
		d.color = ColorBrightness( Look().rock, 0.15f );
		game.AddDecoration( d );
	}
	return e;
}

void Builder::PlayerIsland( Vector3 pos, float yaw )
{
	Island( pos, 6.0f, 7.0f );
	game.SetCannon( pos, yaw );

	// trees and a banner behind the cannon
	Vector3 back = Vector3RotateByQuaternion( { 0, 0, -1 }, QuaternionFromAxisAngle( { 0, 1, 0 }, yaw ) );
	Vector3 side = Vector3RotateByQuaternion( { 1, 0, 0 }, QuaternionFromAxisAngle( { 0, 1, 0 }, yaw ) );
	auto place = [&]( float s, float b, Decoration::Type t, float scale, Color c ) {
		Decoration d;
		d.type = t;
		d.pos = Vector3Add( pos, Vector3Add( Vector3Scale( side, s ), Vector3Scale( back, b ) ) );
		d.scale = scale;
		d.rot = rng.Range( 0.0f, 6.28f );
		d.color = c;
		game.AddDecoration( d );
	};
	// keep the trees out of the aiming camera's view: far to the sides or behind it
	const Biome& look = Look();
	Color pine = look.pinesOnly ? look.leafA : Color{ 50, 110, 60, 255 };
	if ( biome && biome != &GetBiome( 0 ) && look.pinesOnly == false )
	{
		pine = ColorMix( look.leafA, Color{ 50, 110, 60, 255 }, 0.35f );
	}
	Color rock = ColorBrightness( look.rock, 0.15f );
	place( -4.6f, 3.0f, Decoration::Pine, 1.3f, pine );
	place( 4.7f, 2.4f, look.pinesOnly ? Decoration::Pine : Decoration::Tree, 1.1f, look.pinesOnly ? look.leafB : ColorMix( look.leafA, look.leafB, 0.6f ) );
	place( -3.4f, 4.9f, Decoration::Pine, 1.0f, ColorBrightness( pine, -0.08f ) );
	place( 3.3f, 5.1f, Decoration::Pine, 1.2f, ColorBrightness( pine, 0.05f ) );
	place( 4.2f, -1.5f, Decoration::Rock, 0.4f, rock );
	place( -4.0f, -2.5f, Decoration::Rock, 0.3f, rock );
	Flag( Vector3Add( pos, Vector3Add( Vector3Scale( side, 4.2f ), Vector3Scale( back, 0.5f ) ) ), Color{ 40, 90, 200, 255 }, 0.9f );
}

Entity* Builder::Box( Vector3 center, Vector3 half, Mat mat, float yaw )
{
	BodyOptions bo;
	Entity* e = scene.CreateEntity( Kind::Block, mat, center, QuatYaw( yaw ), bo );
	scene.AddBox( e, { 0, 0, 0 }, b3Quat_identity, half, mat );
	e->homeY = homeY;
	scene.FinalizeEntity( e );
	return e;
}

Entity* Builder::King( Vector3 feet, Color robe )
{
	float yaw = atan2f( faceTarget.x - feet.x, faceTarget.z - feet.z );
	BodyOptions bo;
	bo.angularDamping = 0.1f;
	Entity* e = scene.CreateEntity( Kind::King, Mat::Robe, feet, QuatYaw( yaw ), bo );

	ShapeOptions so;
	so.category = CatKing;
	scene.AddHull( e, { 0, 0, 0 }, b3Quat_identity, scene.Cone( 0.9f, 0.34f, 0.19f, 16 ), Mat::Robe, so );
	e->parts.back().tint = robe;
	scene.AddSphere( e, { 0, 1.08f, 0 }, 0.22f, Mat::Skin, so );

	Part beard;
	beard.geo = Geo::Sphere;
	beard.localPos = { 0, 0.97f, 0.13f };
	beard.size = { 0.15f, 0.13f, 0.12f };
	beard.mat = Mat::Plain;
	beard.tint = Color{ 240, 240, 235, 255 };
	scene.AddVisual( e, beard );

	for ( int s = -1; s <= 1; s += 2 )
	{
		Part eye;
		eye.geo = Geo::Sphere;
		eye.localPos = { 0.075f * s, 1.13f, 0.195f };
		eye.size = { 0.035f, 0.045f, 0.03f };
		eye.mat = Mat::Dark;
		eye.tint = Color{ 30, 25, 30, 255 };
		scene.AddVisual( e, eye );
	}

	Part nose;
	nose.geo = Geo::Sphere;
	nose.localPos = { 0.0f, 1.07f, 0.22f };
	nose.size = { 0.05f, 0.05f, 0.05f };
	nose.mat = Mat::Skin;
	nose.tint = Color{ 240, 170, 140, 255 };
	scene.AddVisual( e, nose );

	// ermine collar
	Part collar;
	collar.geo = Geo::Sphere;
	collar.localPos = { 0.0f, 0.86f, 0.0f };
	collar.size = { 0.24f, 0.08f, 0.24f };
	collar.mat = Mat::Plain;
	collar.tint = Color{ 250, 250, 245, 255 };
	scene.AddVisual( e, collar );

	Part crown;
	crown.geo = Geo::Hull;
	crown.hull = scene.Cone( 0.2f, 0.15f, 0.21f, 10 );
	crown.localPos = { 0.0f, 1.23f, 0.0f };
	crown.mat = Mat::Gold;
	crown.tint = GetMatProps( Mat::Gold ).color;
	scene.AddVisual( e, crown );
	e->crownIndex = (int)e->parts.size() - 1;

	e->homeY = feet.y;
	scene.FinalizeEntity( e );
	game.RegisterKing( e );
	return e;
}

Entity* Builder::Tnt( Vector3 center, float half )
{
	Entity* e = Box( center, { half, half, half }, Mat::Tnt );
	return e;
}

Entity* Builder::Barrel( Vector3 base, Mat mat )
{
	BodyOptions bo;
	Entity* e = scene.CreateEntity( Kind::Block, mat, base, b3Quat_identity, bo );
	scene.AddHull( e, { 0, 0, 0 }, b3Quat_identity, scene.Cylinder( 0.9f, 0.32f, 0.0f, 14 ), mat );
	e->homeY = homeY;
	scene.FinalizeEntity( e );
	return e;
}

float Builder::Tower( Vector3 base, int floors, float hw, float fh, Mat posts, Mat slabs )
{
	float y = base.y;
	const float post = 0.15f;
	for ( int f = 0; f < floors; ++f )
	{
		float ph = fh * 0.5f;
		for ( int sx = -1; sx <= 1; sx += 2 )
		{
			for ( int sz = -1; sz <= 1; sz += 2 )
			{
				Box( { base.x + sx * ( hw - post ), y + ph, base.z + sz * ( hw - post ) }, { post, ph, post }, posts );
			}
		}
		y += fh;
		Box( { base.x, y + 0.12f, base.z }, { hw + 0.05f, 0.12f, hw + 0.05f }, slabs );
		y += 0.24f;
	}
	return y;
}

float Builder::Column( Vector3 base, int blocks, float half, Mat mat )
{
	float y = base.y;
	for ( int i = 0; i < blocks; ++i )
	{
		Box( { base.x, y + half, base.z }, { half, half, half }, mat, ( i % 2 ) * 0.0f );
		y += 2.0f * half;
	}
	return y;
}

float Builder::Wall( Vector3 start, bool alongX, int bricks, int rows, Mat mat )
{
	Vector3 dir = alongX ? Vector3{ 1, 0, 0 } : Vector3{ 0, 0, 1 };
	float yaw = alongX ? 0.0f : PI * 0.5f;
	float y = start.y;
	for ( int r = 0; r < rows; ++r )
	{
		float cy = y + 0.25f;
		if ( r % 2 == 0 )
		{
			for ( int i = 0; i < bricks; ++i )
			{
				Vector3 c = Vector3Add( start, Vector3Scale( dir, 0.5f + i ) );
				c.y = cy;
				Brick( c, mat, yaw );
			}
		}
		else
		{
			// offset row: half bricks at the ends
			Vector3 c0 = Vector3Add( start, Vector3Scale( dir, 0.25f ) );
			c0.y = cy;
			Box( c0, { 0.25f, 0.25f, 0.25f }, mat, yaw );
			for ( int i = 0; i < bricks - 1; ++i )
			{
				Vector3 c = Vector3Add( start, Vector3Scale( dir, 1.0f + i ) );
				c.y = cy;
				Brick( c, mat, yaw );
			}
			Vector3 c1 = Vector3Add( start, Vector3Scale( dir, bricks - 0.25f ) );
			c1.y = cy;
			Box( c1, { 0.25f, 0.25f, 0.25f }, mat, yaw );
		}
		y += 0.5f;
	}
	return y;
}

float Builder::Pyramid( Vector3 base, int levels, float half, Mat mat )
{
	float y = base.y;
	for ( int l = 0; l < levels; ++l )
	{
		int n = levels - l;
		float start = -( n - 1 ) * half;
		for ( int i = 0; i < n; ++i )
		{
			for ( int k = 0; k < n; ++k )
			{
				Box( { base.x + start + 2.0f * half * i, y + half, base.z + start + 2.0f * half * k }, { half, half, half }, mat );
			}
		}
		y += 2.0f * half;
	}
	return y;
}

float Builder::Hut( Vector3 base, float hw, float height, Mat walls, Mat roof )
{
	float t = 0.1f;
	float hh = height * 0.5f;
	Box( { base.x, base.y + hh, base.z - hw + t }, { hw, hh, t }, walls );
	Box( { base.x, base.y + hh, base.z + hw - t }, { hw, hh, t }, walls );
	Box( { base.x - hw + t, base.y + hh, base.z }, { t, hh, hw - 2.0f * t }, walls );
	Box( { base.x + hw - t, base.y + hh, base.z }, { t, hh, hw - 2.0f * t }, walls );
	Box( { base.x, base.y + height + 0.12f, base.z }, { hw + 0.2f, 0.12f, hw + 0.2f }, roof );
	return base.y + height + 0.24f;
}

static b3JointId MakeRopeJoint( b3WorldId world, b3BodyId a, Vector3 worldA, b3BodyId b, Vector3 worldB, float slack )
{
	b3DistanceJointDef jd = b3DefaultDistanceJointDef();
	jd.base.bodyIdA = a;
	jd.base.bodyIdB = b;
	jd.base.localFrameA.p = b3Body_GetLocalPoint( a, ToB3( worldA ) );
	jd.base.localFrameB.p = b3Body_GetLocalPoint( b, ToB3( worldB ) );
	float len = Vector3Distance( worldA, worldB ) * slack;
	jd.length = len;
	// a rope: no spring force, only an upper length limit
	jd.enableSpring = true;
	jd.hertz = 0.0f;
	jd.dampingRatio = 0.0f;
	jd.enableLimit = true;
	jd.minLength = 0.0f;
	jd.maxLength = len;
	jd.base.collideConnected = true;
	return b3CreateDistanceJoint( world, &jd );
}

Vector3 Builder::RopeBridge( Vector3 a, Vector3 b, int planks, float width, float sag )
{
	b3WorldId world = scene.World();
	Vector3 d = Vector3Subtract( b, a );
	float len = Vector3Length( d );
	Vector3 dir = Vector3Scale( d, 1.0f / len );
	Vector3 side = Vector3Normalize( Vector3CrossProduct( { 0, 1, 0 }, dir ) );
	float yaw = atan2f( dir.x, dir.z ) + PI * 0.5f; // plank long axis (x) across the bridge

	// anchor posts
	for ( int end = 0; end < 2; ++end )
	{
		Vector3 p = end == 0 ? Vector3Subtract( a, Vector3Scale( dir, 0.2f ) ) : Vector3Add( b, Vector3Scale( dir, 0.2f ) );
		for ( int s = -1; s <= 1; s += 2 )
		{
			Vector3 c = Vector3Add( p, Vector3Scale( side, s * ( width + 0.12f ) ) );
			BodyOptions bo;
			bo.type = b3_staticBody;
			Entity* post = scene.CreateEntity( Kind::Static, Mat::Wood, { c.x, c.y + 0.5f, c.z }, b3Quat_identity, bo );
			ShapeOptions so;
			so.category = CatStatic;
			so.hitEvents = false;
			scene.AddBox( post, { 0, 0, 0 }, b3Quat_identity, { 0.1f, 0.6f, 0.1f }, Mat::Wood, so );
			scene.FinalizeEntity( post );
		}
	}

	b3SphericalJointDef jd = b3DefaultSphericalJointDef();
	jd.base.forceThreshold = 45000.0f; // hard hits snap the ropes
	jd.enableSpring = true;
	jd.hertz = 1.0f;
	jd.dampingRatio = 0.5f;

	// A taut chain carrying a load would need infinite tension, so the bridge is built with a sag.
	auto curve = [&]( float t ) {
		Vector3 p = Vector3Add( a, Vector3Scale( d, t ) );
		p.y -= sag * 4.0f * t * ( 1.0f - t );
		return p;
	};
	(void)yaw;

	b3BodyId prev = scene.groundBody;
	for ( int i = 0; i <= planks; ++i )
	{
		Vector3 pivot = curve( (float)i / planks );
		pivot.y -= 0.02f;
		b3BodyId next = scene.groundBody;
		if ( i < planks )
		{
			Vector3 p0 = curve( (float)i / planks );
			Vector3 p1 = curve( (float)( i + 1 ) / planks );
			Vector3 chord = Vector3Subtract( p1, p0 );
			float chordLen = Vector3Length( chord );
			chord = Vector3Scale( chord, 1.0f / chordLen );
			Vector3 c = Vector3Lerp( p0, p1, 0.5f );
			c.y -= 0.1f;
			b3Quat q = b3MulQuat( QuatYaw( atan2f( dir.x, dir.z ) ), QuatAxisAngle( { 1, 0, 0 }, -asinf( chord.y ) ) );
			BodyOptions bo;
			bo.linearDamping = 0.1f;
			bo.angularDamping = 0.3f;
			Entity* plank = scene.CreateEntity( Kind::Block, Mat::Wood, c, q, bo );
			scene.AddBox( plank, { 0, 0, 0 }, b3Quat_identity, { width, 0.08f, chordLen * 0.5f - 0.03f }, Mat::Wood );
			plank->homeY = homeY;
			plank->parts.back().tint = ColorBrightness( GetMatProps( Mat::Wood ).color, rng.Range( -0.2f, 0.05f ) );
			scene.FinalizeEntity( plank );
			next = plank->body;
		}

		for ( int s = -1; s <= 1; s += 2 )
		{
			Vector3 p = Vector3Add( pivot, Vector3Scale( side, s * width * 0.95f ) );
			jd.base.bodyIdA = prev;
			jd.base.bodyIdB = next;
			jd.base.localFrameA.p = b3Body_GetLocalPoint( prev, ToB3( p ) );
			jd.base.localFrameB.p = b3Body_GetLocalPoint( next, ToB3( p ) );
			b3JointId j = b3CreateSphericalJoint( world, &jd );

			// visual rope along the edge, tied to the joint so it vanishes when the joint snaps
			if ( i > 0 )
			{
				Vector3 q = Vector3Add( curve( (float)( i - 1 ) / planks ), Vector3Scale( side, s * width * 0.95f ) );
				q.y -= 0.02f;
				Rope& r = scene.AddRope( prev, q, prev, p, 0.035f, Color{ 150, 120, 80, 255 } );
				r.joint = j;
				r.breakable = true;
			}
		}
		prev = next;
	}
	return curve( 0.5f );
}

Entity* Builder::Pendulum( Vector3 pivot, float length, float ballRadius )
{
	// gantry
	BodyOptions so;
	so.type = b3_staticBody;
	Entity* g = scene.CreateEntity( Kind::Static, Mat::Wood, pivot, b3Quat_identity, so );
	ShapeOptions sh;
	sh.category = CatStatic;
	sh.hitEvents = false;
	float h = pivot.y - homeY;
	scene.AddBox( g, { -3.2f, -h * 0.5f + 0.3f, 0 }, b3Quat_identity, { 0.25f, h * 0.5f + 0.3f, 0.25f }, Mat::Wood, sh );
	scene.AddBox( g, { 3.2f, -h * 0.5f + 0.3f, 0 }, b3Quat_identity, { 0.25f, h * 0.5f + 0.3f, 0.25f }, Mat::Wood, sh );
	scene.AddBox( g, { 0, 0.45f, 0 }, b3Quat_identity, { 3.6f, 0.2f, 0.3f }, Mat::Wood, sh );
	scene.AddBox( g, { -2.6f, -0.3f, 0 }, QuatAxisAngle( { 0, 0, 1 }, 0.785f ), { 0.12f, 0.8f, 0.12f }, Mat::Wood, sh );
	scene.AddBox( g, { 2.6f, -0.3f, 0 }, QuatAxisAngle( { 0, 0, 1 }, -0.785f ), { 0.12f, 0.8f, 0.12f }, Mat::Wood, sh );
	scene.FinalizeEntity( g );

	Vector3 ballPos{ pivot.x, pivot.y - length, pivot.z };
	BodyOptions bo;
	bo.angularDamping = 0.2f;
	Entity* ball = scene.CreateEntity( Kind::Mechanism, Mat::Metal, ballPos, b3Quat_identity, bo );
	ShapeOptions bs;
	bs.densityScale = 0.45f;
	scene.AddSphere( ball, { 0, 0, 0 }, ballRadius, Mat::Metal, bs );
	scene.FinalizeEntity( ball );
	ball->homeY = homeY;

	b3DistanceJointDef jd = b3DefaultDistanceJointDef();
	jd.base.bodyIdA = scene.groundBody;
	jd.base.bodyIdB = ball->body;
	jd.base.localFrameA.p = ToB3( pivot );
	jd.base.localFrameB.p = { 0, ballRadius, 0 };
	jd.length = length - ballRadius;
	b3JointId j = b3CreateDistanceJoint( scene.World(), &jd );

	Rope& r = scene.AddRope( scene.groundBody, pivot, ball->body, { ballPos.x, ballPos.y + ballRadius, ballPos.z }, 0.07f,
							 Color{ 60, 60, 66, 255 } );
	r.joint = j;
	return ball;
}

Entity* Builder::Windmill( Vector3 base, float towerHeight, float bladeLength, float speed )
{
	BodyOptions so;
	so.type = b3_staticBody;
	Entity* tower = scene.CreateEntity( Kind::Static, Mat::Stone, base, b3Quat_identity, so );
	ShapeOptions sh;
	sh.category = CatStatic;
	sh.hitEvents = false;
	scene.AddHull( tower, { 0, 0, 0 }, b3Quat_identity, scene.Cone( towerHeight, 1.2f, 0.75f, 12 ), Mat::Stone, sh );
	scene.AddHull( tower, { 0, towerHeight, 0 }, b3Quat_identity, scene.Cone( 1.4f, 1.0f, 0.1f, 12 ), Mat::Wood, sh );
	scene.FinalizeEntity( tower );

	Vector3 hub{ base.x, base.y + towerHeight - 0.2f, base.z - 1.05f };
	BodyOptions bo;
	bo.angularDamping = 0.0f;
	Entity* blades = scene.CreateEntity( Kind::Mechanism, Mat::Wood, hub, b3Quat_identity, bo );
	ShapeOptions bs;
	bs.category = CatBlock;
	scene.AddBox( blades, { 0, 0, 0 }, b3Quat_identity, { 0.3f, 0.3f, 0.25f }, Mat::Wood, bs );
	for ( int k = 0; k < 4; ++k )
	{
		float ang = k * PI * 0.5f;
		b3Quat q = QuatAxisAngle( { 0, 0, 1 }, ang );
		Vector3 off = Vector3RotateByQuaternion( { 0, 0.3f + bladeLength * 0.5f, 0 }, ToRl( q ) );
		scene.AddBox( blades, off, q, { 0.12f, bladeLength * 0.5f, 0.06f }, Mat::Wood, bs );
		Vector3 sail = Vector3RotateByQuaternion( { 0.42f, 0.5f + bladeLength * 0.5f, -0.02f }, ToRl( q ) );
		scene.AddBox( blades, sail, q, { 0.3f, bladeLength * 0.42f, 0.03f }, Mat::Plain, bs );
		blades->parts.back().tint = Color{ 235, 225, 200, 255 };
	}
	scene.FinalizeEntity( blades );
	b3Body_EnableSleep( blades->body, false );

	b3RevoluteJointDef rj = b3DefaultRevoluteJointDef();
	rj.base.bodyIdA = scene.groundBody;
	rj.base.bodyIdB = blades->body;
	rj.base.localFrameA.p = ToB3( hub );
	rj.base.localFrameB.p = { 0, 0, 0 };
	rj.enableMotor = true;
	rj.motorSpeed = speed;
	rj.maxMotorTorque = 2.0e6f;
	b3JointId j = b3CreateRevoluteJoint( scene.World(), &rj );

	Mechanism m;
	m.type = MechType::Windmill;
	m.joint = j;
	m.speed = speed;
	m.entity = blades;
	m.amplitude = bladeLength + 0.5f; // reach of the blade tips from the hub
	scene.mechanisms.push_back( m );
	return blades;
}

Entity* Builder::Slider( Vector3 center, Vector3 half, Vector3 axis, float amplitude, float speed, float phase, Mat mat )
{
	BodyOptions bo;
	bo.angularDamping = 0.5f;
	Entity* e = scene.CreateEntity( Kind::Mechanism, mat, center, b3Quat_identity, bo );
	scene.AddBox( e, { 0, 0, 0 }, b3Quat_identity, half, mat );
	e->homeY = homeY;
	scene.FinalizeEntity( e );
	b3Body_EnableSleep( e->body, false );

	b3PrismaticJointDef pj = b3DefaultPrismaticJointDef();
	pj.base.bodyIdA = scene.groundBody;
	pj.base.bodyIdB = e->body;
	pj.base.localFrameA.p = ToB3( center );
	pj.base.localFrameA.q = b3ComputeQuatBetweenUnitVectors( b3Vec3_axisX, b3Normalize( ToB3( axis ) ) );
	pj.base.localFrameB.p = { 0, 0, 0 };
	pj.base.localFrameB.q = pj.base.localFrameA.q;
	pj.enableSpring = true;
	pj.hertz = 1.2f;
	pj.dampingRatio = 0.8f;
	pj.enableLimit = true;
	pj.lowerTranslation = -amplitude * 1.1f;
	pj.upperTranslation = amplitude * 1.1f;
	b3JointId j = b3CreatePrismaticJoint( scene.World(), &pj );

	Mechanism m;
	m.type = MechType::Slider;
	m.joint = j;
	m.amplitude = amplitude;
	m.speed = speed;
	m.phase = phase;
	scene.mechanisms.push_back( m );

	// rails above and below
	BodyOptions so;
	so.type = b3_staticBody;
	Entity* rail = scene.CreateEntity( Kind::Static, Mat::Metal, center, b3Quat_identity, so );
	ShapeOptions sh;
	sh.category = CatStatic;
	sh.hitEvents = false;
	float span = amplitude + half.x + 0.6f;
	Vector3 ax = Vector3Normalize( axis );
	b3Quat rq = b3ComputeQuatBetweenUnitVectors( b3Vec3_axisX, ToB3( ax ) );
	scene.AddBox( rail, { 0, half.y + 0.12f, 0 }, rq, { span, 0.08f, 0.08f }, Mat::Metal, sh );
	scene.AddBox( rail, { 0, -half.y - 0.12f, 0 }, rq, { span, 0.08f, 0.08f }, Mat::Metal, sh );
	scene.FinalizeEntity( rail );
	return e;
}

Entity* Builder::BalloonBasket( Vector3 c, Color balloonColor, Color robe )
{
	b3WorldId world = scene.World();

	BodyOptions bo;
	bo.linearDamping = 0.4f;
	bo.angularDamping = 1.5f;
	Entity* basket = scene.CreateEntity( Kind::Block, Mat::Wood, c, b3Quat_identity, bo );
	const float hw = 0.62f;
	scene.AddBox( basket, { 0, 0, 0 }, b3Quat_identity, { hw, 0.06f, hw }, Mat::Wood );
	scene.AddBox( basket, { 0, 0.3f, -hw + 0.05f }, b3Quat_identity, { hw, 0.24f, 0.05f }, Mat::Wood );
	scene.AddBox( basket, { 0, 0.3f, hw - 0.05f }, b3Quat_identity, { hw, 0.24f, 0.05f }, Mat::Wood );
	scene.AddBox( basket, { -hw + 0.05f, 0.3f, 0 }, b3Quat_identity, { 0.05f, 0.24f, hw - 0.1f }, Mat::Wood );
	scene.AddBox( basket, { hw - 0.05f, 0.3f, 0 }, b3Quat_identity, { 0.05f, 0.24f, hw - 0.1f }, Mat::Wood );
	for ( Part& p : basket->parts )
	{
		p.tint = Color{ 170, 130, 80, 255 };
	}
	basket->homeY = c.y;
	scene.FinalizeEntity( basket );

	Entity* king = King( { c.x, c.y + 0.06f, c.z }, robe );
	king->homeY = c.y;

	float radius = 1.15f;
	Vector3 bp{ c.x, c.y + 3.4f, c.z };
	BodyOptions bb;
	bb.linearDamping = 0.8f;
	bb.angularDamping = 1.5f;
	Entity* balloon = scene.CreateEntity( Kind::Balloon, Mat::Balloon, bp, b3Quat_identity, bb );
	ShapeOptions bs;
	bs.densityScale = 10.0f;
	scene.AddSphere( balloon, { 0, 0, 0 }, radius, Mat::Balloon, bs );
	balloon->parts.back().tint = balloonColor;
	balloon->parts.back().size = { radius, radius * 1.12f, radius };
	scene.FinalizeEntity( balloon );
	b3Body_EnableSleep( balloon->body, false );
	b3Body_EnableSleep( basket->body, false );

	float payload = basket->mass + king->mass;
	float lift = payload * 10.0f * 1.7f + balloon->mass * 10.0f;
	b3Body_SetGravityScale( balloon->body, -lift / ( balloon->mass * 10.0f ) );

	// suspension ropes
	Vector3 bottom{ bp.x, bp.y - radius * 1.05f, bp.z };
	for ( int sx = -1; sx <= 1; sx += 2 )
	{
		for ( int sz = -1; sz <= 1; sz += 2 )
		{
			Vector3 corner{ c.x + sx * ( hw - 0.05f ), c.y + 0.54f, c.z + sz * ( hw - 0.05f ) };
			Vector3 top = Vector3Add( bottom, { sx * 0.35f, 0.1f, sz * 0.35f } );
			b3JointId j = MakeRopeJoint( world, basket->body, corner, balloon->body, top, 1.0f );
			Rope& r = scene.AddRope( basket->body, corner, balloon->body, top, 0.02f, Color{ 200, 190, 160, 255 } );
			r.joint = j;
			r.ropeLength = Vector3Distance( corner, top );
		}
	}

	// mooring line down into the clouds
	Vector3 anchor{ c.x, -30.0f, c.z };
	Vector3 under{ c.x, c.y - 0.06f, c.z };
	b3JointId tether = MakeRopeJoint( world, scene.groundBody, anchor, basket->body, under, 1.0f );
	Rope& r = scene.AddRope( scene.groundBody, anchor, basket->body, under, 0.025f, Color{ 120, 100, 80, 255 } );
	r.joint = tether;
	r.ropeLength = Vector3Distance( anchor, under );
	return balloon;
}

Entity* Builder::Sandbag( Vector3 center, float yaw )
{
	BodyOptions bo;
	bo.linearDamping = 1.5f;
	bo.angularDamping = 3.0f;
	Entity* e = scene.CreateEntity( Kind::Block, Mat::Sand, center, QuatYaw( yaw ), bo );
	ShapeOptions so;
	so.explosionScale = 0.25f;
	scene.AddBox( e, { 0, 0, 0 }, b3Quat_identity, { 0.5f, 0.2f, 0.3f }, Mat::Sand, so );
	// drawn as a plump pillow rather than a brick
	e->parts.back().visible = false;
	Part bag;
	bag.geo = Geo::Sphere;
	bag.size = { 0.54f, 0.24f, 0.33f };
	bag.mat = Mat::Sand;
	bag.tint = ColorBrightness( GetMatProps( Mat::Sand ).color, rng.Range( -0.12f, 0.06f ) );
	scene.AddVisual( e, bag );
	e->homeY = homeY;
	scene.FinalizeEntity( e );
	return e;
}

float Builder::SandbagWall( Vector3 start, bool alongX, int bags, int rows )
{
	Vector3 dir = alongX ? Vector3{ 1, 0, 0 } : Vector3{ 0, 0, 1 };
	float yaw = alongX ? 0.0f : PI * 0.5f;
	float y = start.y;
	for ( int r = 0; r < rows; ++r )
	{
		float shift = ( r % 2 ) * 0.5f;
		int n = bags - ( r % 2 );
		for ( int i = 0; i < n; ++i )
		{
			Vector3 c = Vector3Add( start, Vector3Scale( dir, 0.5f + shift + i * 1.02f ) );
			c.y = y + 0.2f;
			Sandbag( c, yaw );
		}
		y += 0.4f;
	}
	return y;
}

Entity* Builder::Bumper( Vector3 center, Vector3 half, float yaw )
{
	BodyOptions bo;
	bo.type = b3_staticBody;
	Entity* e = scene.CreateEntity( Kind::Static, Mat::Rubber, center, QuatYaw( yaw ), bo );
	ShapeOptions so;
	so.category = CatStatic;
	so.hitEvents = false;
	scene.AddBox( e, { 0, 0, 0 }, b3Quat_identity, half, Mat::Rubber, so );
	scene.FinalizeEntity( e );
	return e;
}

Entity* Builder::Shield( Vector3 center, Vector3 half, float yaw, float period, float onTime, float phase )
{
	BodyOptions bo;
	bo.type = b3_staticBody;
	Entity* e = scene.CreateEntity( Kind::Shield, Mat::Shield, center, QuatYaw( yaw ), bo );
	ShapeOptions so;
	so.category = CatShield;
	so.hitEvents = false;
	scene.AddBox( e, { 0, 0, 0 }, b3Quat_identity, half, Mat::Shield, so );
	scene.FinalizeEntity( e );

	Mechanism m;
	m.type = MechType::Blinker;
	m.entity = e;
	m.period = period;
	m.onTime = onTime;
	m.phase = phase;
	m.on = true;
	scene.mechanisms.push_back( m );

	// small crystal pylons at the lower corners anchor the wall visually
	for ( int s = -1; s <= 1; s += 2 )
	{
		Vector3 local{ s * ( half.x + 0.12f ), -half.y + 0.35f, 0.0f };
		Vector3 w = Vector3Add( center, Vector3RotateByQuaternion( local, ToRl( QuatYaw( yaw ) ) ) );
		Decoration d;
		d.type = Decoration::Pylon;
		d.pos = { w.x, center.y - half.y, w.z };
		d.scale = 1.0f;
		d.rot = yaw;
		d.color = GetMatProps( Mat::Shield ).color;
		game.AddDecoration( d );
	}
	return e;
}

Entity* Builder::Ledge( Vector3 center, Vector3 half, Mat mat, Quaternion rot, Color tint )
{
	BodyOptions bo;
	bo.type = b3_staticBody;
	Entity* e = scene.CreateEntity( Kind::Static, mat, center, ToB3( rot ), bo );
	ShapeOptions so;
	so.category = CatStatic;
	so.hitEvents = false;
	scene.AddBox( e, { 0, 0, 0 }, b3Quat_identity, half, mat, so );
	if ( tint.a > 0 )
	{
		e->parts.back().tint = tint;
	}
	scene.FinalizeEntity( e );
	return e;
}

Entity* Builder::Ramp( Vector3 lowEdge, float length, float halfWidth, float angle, Mat mat, Color tint )
{
	const float thick = 0.3f;
	Quaternion q = QuaternionFromAxisAngle( { 1, 0, 0 }, -angle ); // +z end goes up
	// the centre sits half a length up the slope and half a thickness below the surface
	Vector3 along = Vector3RotateByQuaternion( { 0, 0, 1 }, q );
	Vector3 normal = Vector3RotateByQuaternion( { 0, 1, 0 }, q );
	Vector3 c = Vector3Add( lowEdge, Vector3Subtract( Vector3Scale( along, length * 0.5f ), Vector3Scale( normal, thick ) ) );
	return Ledge( c, { halfWidth, thick, length * 0.5f }, mat, q, tint );
}

Entity* Builder::CurlingStone( Vector3 base )
{
	BodyOptions bo;
	bo.angularDamping = 0.3f;
	Entity* e = scene.CreateEntity( Kind::Block, Mat::Stone, base, b3Quat_identity, bo );
	ShapeOptions so;
	so.friction = 0.15f; // polished granite on ice: it glides
	scene.AddHull( e, { 0, 0, 0 }, b3Quat_identity, scene.Cylinder( 0.34f, 0.45f, 0.0f, 20 ), Mat::Stone, so );
	e->parts.back().tint = Color{ 125, 125, 135, 255 };

	Part band;
	band.geo = Geo::Hull;
	band.hull = scene.Cylinder( 0.08f, 0.458f, 0.0f, 20 );
	band.localPos = { 0, 0.17f, 0 };
	band.mat = Mat::Plain;
	band.tint = Color{ 200, 40, 50, 255 };
	scene.AddVisual( e, band );
	Part handle;
	handle.geo = Geo::Box;
	handle.localPos = { 0, 0.37f, -0.05f };
	handle.size = { 0.05f, 0.03f, 0.2f };
	handle.mat = Mat::Plain;
	handle.tint = Color{ 200, 40, 50, 255 };
	scene.AddVisual( e, handle );

	e->lethal = true;
	e->homeY = homeY;
	scene.FinalizeEntity( e );
	return e;
}

Entity* Builder::Snowball( Vector3 center, float radius )
{
	BodyOptions bo;
	bo.angularDamping = 0.02f;
	Entity* e = scene.CreateEntity( Kind::Block, Mat::Plain, center, b3Quat_identity, bo );
	ShapeOptions so;
	so.densityScale = 0.9f;
	so.mask = CatAll & ~CatDebris; // rolls straight over the shards of the dam it was leaning on
	scene.AddSphere( e, { 0, 0, 0 }, radius, Mat::Plain, so );
	e->parts.back().tint = Color{ 240, 245, 252, 255 };
	e->lethal = true;
	e->homeY = homeY;
	scene.FinalizeEntity( e );
	return e;
}

Entity* Builder::Gate( Vector3 center, Vector3 half )
{
	// the posts stand just outside the plank's ends, in the plank's own plane
	const float post = 0.14f;
	Entity* posts[2];
	for ( int s = 0; s < 2; ++s )
	{
		float x = center.x + ( s == 0 ? -1.0f : 1.0f ) * ( half.x + post );
		posts[s] = Ledge( { x, center.y + 0.2f, center.z }, { post, half.y + 0.5f, post + 0.02f }, Mat::Wood, { 0, 0, 0, 1 },
						  Color{ 120, 84, 52, 255 } );
	}
	// a slab of ice: a hit from the front only presses it against the snow behind, so it has to shatter
	// rather than tear loose
	Entity* plank = Box( center, half, Mat::Ice );
	for ( int s = 0; s < 2; ++s )
	{
		b3WeldJointDef jd = b3DefaultWeldJointDef();
		jd.base.bodyIdA = posts[s]->body;
		jd.base.bodyIdB = plank->body;
		Vector3 end{ ( s == 0 ? -1.0f : 1.0f ) * half.x, 0, 0 };
		jd.base.localFrameA.p = b3Body_GetLocalPoint( posts[s]->body, ToB3( Vector3Add( center, end ) ) );
		jd.base.localFrameA.q = b3Quat_identity;
		jd.base.localFrameB.p = ToB3( end );
		jd.base.localFrameB.q = b3Quat_identity;
		b3CreateWeldJoint( scene.World(), &jd );
	}
	return plank;
}

Entity* Builder::SnowShelter( Vector3 base, float halfX, float halfZ, float height )
{
	const float p = 0.16f;
	float ph = height * 0.5f;
	Entity* front = Box( { base.x, base.y + ph, base.z - halfZ + p }, { p, ph, p }, Mat::Ice );
	Box( { base.x - halfX + p, base.y + ph, base.z + halfZ - p }, { p, ph, p }, Mat::Ice );
	Box( { base.x + halfX - p, base.y + ph, base.z + halfZ - p }, { p, ph, p }, Mat::Ice );

	const float t = 0.22f;
	BodyOptions bo;
	Vector3 c{ base.x, base.y + height + t, base.z };
	Entity* roof = scene.CreateEntity( Kind::Block, Mat::Plain, c, b3Quat_identity, bo );
	ShapeOptions so;
	so.densityScale = 1.8f; // packed snow and ice: heavy
	scene.AddBox( roof, { 0, 0, 0 }, b3Quat_identity, { halfX + 0.25f, t, halfZ + 0.25f }, Mat::Plain, so );
	roof->parts.back().tint = Color{ 232, 240, 250, 255 };
	// icicles along the front and back eaves
	for ( int side = -1; side <= 1; side += 2 )
	{
		int n = (int)( ( halfX + 0.25f ) * 2.0f / 0.45f );
		for ( int i = 0; i < n; ++i )
		{
			float x = -( halfX + 0.25f ) + 0.25f + i * 0.45f;
			float len = 0.35f + 0.3f * rng.Float();
			Part ic;
			ic.geo = Geo::Hull;
			ic.hull = scene.Cone( len, 0.015f, 0.09f, 8 );
			ic.localPos = { x, -t - len, side * ( halfZ + 0.12f ) };
			ic.mat = Mat::Ice;
			ic.tint = GetMatProps( Mat::Ice ).color;
			scene.AddVisual( roof, ic );
		}
	}
	roof->lethal = true;
	roof->homeY = homeY;
	scene.FinalizeEntity( roof );
	return front;
}

void Builder::AimHint( Entity* king, Entity* via, Vector3 offset )
{
	game.AddAimHint( king, via, offset );
}

void Builder::Trees( Vector3 center, float radius, int count, float minR )
{
	for ( int i = 0; i < count; ++i )
	{
		float a = rng.Range( 0.0f, 2.0f * PI );
		float r = rng.Range( minR, radius - 0.8f );
		Decoration d;
		// draw the same random numbers in every realm so a level's layout never depends on its look
		bool pine = rng.Float() < 0.5f;
		d.type = pine || Look().pinesOnly ? Decoration::Pine : Decoration::Tree;
		d.pos = { center.x + cosf( a ) * r, center.y, center.z + sinf( a ) * r };
		d.scale = rng.Range( 0.7f, 1.2f );
		d.rot = rng.Range( 0.0f, 6.28f );
		d.color = ColorMix( Look().leafA, Look().leafB, rng.Float() );
		game.AddDecoration( d );
	}
}

void Builder::Flag( Vector3 base, Color color, float scale )
{
	game.AddFlag( base, color, scale );
}

void Builder::Fortress( Vector3 center, float radius )
{
	game.SetFortressCenter( center, radius );
}

// ---------------------------------------------------------------------------------------------
// Levels
// ---------------------------------------------------------------------------------------------

static void Level01( Builder& b )
{
	b.PlayerIsland();
	b.homeY = 0.0f;
	b.Island( { 0, 0, 30 }, 7.0f );
	float top = b.Tower( { 0, 0, 30 }, 2, 1.0f, 1.3f, Mat::Wood, Mat::Wood );
	b.King( { 0, top, 30 }, kPurple );
	b.Box( { 3.0f, 0.4f, 29.0f }, { 0.4f, 0.4f, 0.4f }, Mat::Wood, 0.2f );
	b.Box( { 3.0f, 1.2f, 29.0f }, { 0.4f, 0.4f, 0.4f }, Mat::Wood, -0.1f );
	b.Box( { -3.0f, 0.4f, 31.0f }, { 0.4f, 0.4f, 0.4f }, Mat::Wood, 0.5f );
	b.Trees( { 0, 0, 30 }, 7.0f, 4, 4.5f );
	b.Flag( { -2.5f, 0, 33.5f }, kCrimson );
	b.Fortress( { 0, 2, 30 }, 9.0f );
}

static void Level02( Builder& b )
{
	b.PlayerIsland();
	b.homeY = 1.0f;
	b.Island( { 0, 1, 34 }, 9.0f );
	// 2.5 m of wall: tall enough to hide a king standing right behind it
	b.Wall( { -4.0f, 1.0f, 30.0f }, true, 8, 5, Mat::Stone );
	// the third king hides at ground level behind the wall; the player finds him in the
	// opening fly-by or with TAB, and needs a lob (or a breach) to reach him
	b.King( { 0.0f, 1.0f, 31.6f }, kGreen );
	// the two visible kings stand well apart, so a single bomb cannot reach all three
	float top = b.Tower( { -3.6f, 1.0f, 35.0f }, 3, 1.0f, 1.2f, Mat::Stone, Mat::Wood );
	b.King( { -3.6f, top, 35.0f }, kCrimson );
	float c = b.Column( { 4.0f, 1.0f, 35.5f }, 2, 0.5f, Mat::Stone );
	b.Box( { 4.0f, c + 0.12f, 35.5f }, { 0.8f, 0.12f, 0.8f }, Mat::Stone );
	b.King( { 4.0f, c + 0.24f, 35.5f }, kBlue );
	b.Trees( { 0, 1, 34 }, 9.0f, 4, 7.2f );
	b.Flag( { 1.5f, 1.0f, 38.5f }, kCrimson );
	b.Fortress( { 0, 3, 34 }, 11.0f );
}

static void Level03( Builder& b )
{
	b.PlayerIsland();
	b.homeY = 0.0f;
	b.Island( { -7.0f, 0, 34 }, 4.5f );
	b.Island( { 7.0f, 0, 34 }, 4.5f );
	float t1 = b.Tower( { -7.0f, 0, 35.0f }, 2, 1.0f, 1.4f, Mat::Stone, Mat::Wood );
	b.King( { -7.0f, t1, 35.0f }, kGreen );
	float t2 = b.Tower( { 7.0f, 0, 35.0f }, 2, 1.0f, 1.4f, Mat::Stone, Mat::Wood );
	b.King( { 7.0f, t2, 35.0f }, kOrange );
	// anchored right at the island edges, so no plank starts inside the rock
	Vector3 mid = b.RopeBridge( { -2.45f, 0.0f, 34.0f }, { 2.45f, 0.0f, 34.0f }, 9, 0.8f, 0.55f );
	b.King( { mid.x, mid.y - 0.03f, mid.z }, kPurple );
	b.Trees( { -7.0f, 0, 34 }, 4.5f, 2, 3.0f );
	b.Trees( { 7.0f, 0, 34 }, 4.5f, 2, 3.0f );
	b.Flag( { -8.5f, 0, 32.0f }, kGreen );
	b.Flag( { 8.5f, 0, 32.0f }, kOrange );
	b.Fortress( { 0, 2, 34 }, 13.0f );
}

static void Level04( Builder& b )
{
	b.PlayerIsland();
	b.homeY = -1.0f;
	b.Island( { 0, -1, 33 }, 8.0f );
	float t = b.Tower( { 0, -1, 34.5f }, 3, 1.2f, 1.3f, Mat::Ice, Mat::Ice );
	b.King( { 0, t, 34.5f }, kTeal );
	float t2 = b.Tower( { -3.6f, -1, 33.5f }, 2, 0.9f, 1.2f, Mat::Ice, Mat::Wood );
	b.King( { -3.6f, t2, 33.5f }, kBlue );
	float t3 = b.Tower( { 3.6f, -1, 33.5f }, 2, 0.9f, 1.2f, Mat::Ice, Mat::Wood );
	b.King( { 3.6f, t3, 33.5f }, kPurple );
	b.Pyramid( { 0, -1, 30.5f }, 3, 0.35f, Mat::Ice );
	b.Trees( { 0, -1, 33 }, 8.0f, 3, 6.0f );
	b.Flag( { 5.5f, -1, 36.0f }, kTeal );
	b.Fortress( { 0, 1.5f, 33 }, 10.0f );
}

static void Level05( Builder& b )
{
	b.PlayerIsland();
	b.homeY = 0.0f;
	b.Island( { 0, 0, 32 }, 6.0f );
	b.King( { 0, 0, 33.0f }, kCrimson );
	b.Hut( { 0, 0, 33.0f }, 1.1f, 1.7f, Mat::Wood, Mat::Wood );
	b.BalloonBasket( { -8.0f, 3.0f, 34.0f }, Color{ 230, 70, 80, 255 }, kBlue );
	b.BalloonBasket( { 8.5f, 4.5f, 36.0f }, Color{ 250, 200, 50, 255 }, kGreen );
	b.Trees( { 0, 0, 32 }, 6.0f, 3, 3.5f );
	b.Flag( { 3.0f, 0, 35.0f }, kCrimson );
	b.Fortress( { 0, 3, 34 }, 14.0f );
}

static void Level06( Builder& b )
{
	b.PlayerIsland();
	b.homeY = 0.0f;
	b.Island( { 0, 0, 35 }, 9.5f );
	b.Windmill( { 0, 0, 29.5f }, 5.5f, 3.2f, 0.9f );
	b.Wall( { -3.5f, 0, 33.0f }, true, 7, 2, Mat::Stone );
	float t1 = b.Tower( { -4.6f, 0, 36.5f }, 3, 1.0f, 1.2f, Mat::Stone, Mat::Wood );
	b.King( { -4.6f, t1, 36.5f }, kOrange );
	float t2 = b.Tower( { 4.6f, 0, 36.5f }, 3, 1.0f, 1.2f, Mat::Wood, Mat::Wood );
	b.King( { 4.6f, t2, 36.5f }, kPurple );
	float c = b.Column( { 0, 0, 39.5f }, 3, 0.45f, Mat::Stone );
	b.Box( { 0, c + 0.12f, 39.5f }, { 0.75f, 0.12f, 0.75f }, Mat::Stone );
	b.King( { 0, c + 0.24f, 39.5f }, kCrimson );
	b.Trees( { 0, 0, 35 }, 9.5f, 4, 7.0f );
	b.Flag( { -6.0f, 0, 38.0f }, kOrange );
	b.Fortress( { 0, 3, 35 }, 12.0f );
}

static void Level07( Builder& b )
{
	b.PlayerIsland();
	b.homeY = 0.0f;
	b.Island( { 0, 0, 33.5f }, 9.0f );
	b.Pendulum( { 0, 8.0f, 30.5f }, 5.6f, 0.75f );
	float t1 = b.Tower( { -1.9f, 0, 33.2f }, 3, 0.9f, 1.2f, Mat::Wood, Mat::Wood );
	b.King( { -1.9f, t1, 33.2f }, kBlue );
	float t2 = b.Tower( { 1.9f, 0, 33.2f }, 3, 0.9f, 1.2f, Mat::Wood, Mat::Wood );
	b.King( { 1.9f, t2, 33.2f }, kGreen );
	b.Tnt( { 0, 0.35f, 33.6f } );
	float t3 = b.Tower( { 0, 0, 36.8f }, 2, 1.0f, 1.2f, Mat::Stone, Mat::Wood );
	b.King( { 0, t3, 36.8f }, kCrimson );
	b.Trees( { 0, 0, 33.5f }, 9.0f, 4, 6.5f );
	b.Flag( { 5.0f, 0, 36.0f }, kBlue );
	b.Fortress( { 0, 3, 33.5f }, 11.0f );
}

static void Level08( Builder& b )
{
	b.PlayerIsland();
	b.homeY = 0.0f;
	b.Island( { 0, 0, 34 }, 9.0f );
	b.Wall( { -3.0f, 0, 30.5f }, true, 6, 3, Mat::Stone );
	for ( int s = -1; s <= 1; s += 2 )
	{
		b.Tnt( { 3.2f * s, 0.35f, 34.5f } );
		float t = b.Tower( { 3.2f * s, 0, 34.5f }, 2, 1.0f, 1.3f, Mat::Stone, Mat::Wood );
		b.King( { 3.2f * s, t, 34.5f }, s < 0 ? kGreen : kOrange );
	}
	b.Tnt( { -0.75f, 0.35f, 35.0f } );
	b.Tnt( { 0.0f, 0.35f, 35.0f } );
	b.Tnt( { 0.75f, 0.35f, 35.0f } );
	b.Tnt( { -0.4f, 1.05f, 35.0f } );
	b.Tnt( { 0.4f, 1.05f, 35.0f } );
	b.Box( { 0, 1.52f, 35.0f }, { 1.3f, 0.12f, 0.6f }, Mat::Wood );
	b.King( { -0.6f, 1.64f, 35.0f }, kCrimson );
	b.King( { 0.6f, 1.64f, 35.0f }, kPurple );
	b.Trees( { 0, 0, 34 }, 9.0f, 4, 6.8f );
	b.Flag( { 0.0f, 0, 38.5f }, kCrimson );
	b.Fortress( { 0, 2, 34 }, 11.0f );
}

static void Level09( Builder& b )
{
	b.PlayerIsland();
	b.homeY = 2.0f;
	b.Island( { 0, 2, 37 }, 9.0f );
	b.Slider( { 0, 2.0f + 1.7f, 31.5f }, { 1.6f, 1.6f, 0.2f }, { 1, 0, 0 }, 3.8f, 0.8f, 0.0f, Mat::Stone );
	b.Slider( { 0, 2.0f + 5.2f, 32.3f }, { 1.6f, 1.4f, 0.2f }, { 1, 0, 0 }, 3.8f, 1.15f, PI, Mat::Stone );
	float t1 = b.Tower( { -3.0f, 2, 37.5f }, 4, 0.9f, 1.2f, Mat::Stone, Mat::Wood );
	b.King( { -3.0f, t1, 37.5f }, kBlue );
	float t2 = b.Tower( { 3.0f, 2, 37.5f }, 4, 0.9f, 1.2f, Mat::Stone, Mat::Wood );
	b.King( { 3.0f, t2, 37.5f }, kOrange );
	float t3 = b.Tower( { 0, 2, 40.0f }, 5, 1.0f, 1.2f, Mat::Wood, Mat::Wood );
	b.King( { 0, t3, 40.0f }, kPurple );
	b.Trees( { 0, 2, 37 }, 9.0f, 4, 6.5f );
	b.Flag( { -6.0f, 2, 40.0f }, kPurple );
	b.Fortress( { 0, 5, 37 }, 12.0f );
}

static void Level10( Builder& b )
{
	b.PlayerIsland();
	b.homeY = 0.0f;
	b.Island( { 0, 0, 39 }, 12.5f );

	// curtain wall with two gate towers
	b.Wall( { -7.0f, 0, 31.5f }, true, 5, 3, Mat::Stone );
	b.Wall( { 2.0f, 0, 31.5f }, true, 5, 3, Mat::Stone );
	float g1 = b.Column( { -1.4f, 0, 31.75f }, 3, 0.5f, Mat::Stone );
	b.Column( { 1.4f, 0, 31.75f }, 3, 0.5f, Mat::Stone );
	b.Box( { 0, g1 + 0.15f, 31.75f }, { 2.0f, 0.15f, 0.45f }, Mat::Wood );
	b.King( { 0, g1 + 0.3f, 31.75f }, kOrange );

	// the keep
	b.Tnt( { -0.5f, 0.35f, 39.0f } );
	b.Tnt( { 0.5f, 0.35f, 39.0f } );
	float k = b.Tower( { 0, 0, 39.0f }, 4, 1.4f, 1.3f, Mat::Stone, Mat::Wood );
	b.King( { 0, k, 39.0f }, kPurple );

	// ice tower and a wooden hall
	float it = b.Tower( { -5.0f, 0, 41.0f }, 3, 0.9f, 1.2f, Mat::Ice, Mat::Ice );
	b.King( { -5.0f, it, 41.0f }, kTeal );
	b.King( { 5.0f, 0, 41.0f }, kCrimson );
	float h = b.Hut( { 5.0f, 0, 41.0f }, 1.2f, 1.6f, Mat::Wood, Mat::Wood );
	b.Tnt( { 5.0f, h + 0.35f, 41.0f } );

	// outposts on satellite islands
	b.homeY = 3.0f;
	b.Island( { -14.0f, 3.0f, 33.0f }, 4.0f );
	float o1 = b.Tower( { -14.0f, 3.0f, 33.0f }, 2, 0.9f, 1.2f, Mat::Wood, Mat::Wood );
	b.King( { -14.0f, o1, 33.0f }, kGreen );
	b.Island( { 14.0f, 3.0f, 33.0f }, 4.0f );
	float o2 = b.Tower( { 14.0f, 3.0f, 33.0f }, 2, 0.9f, 1.2f, Mat::Wood, Mat::Wood );
	b.King( { 14.0f, o2, 33.0f }, kBlue );

	b.homeY = 0.0f;
	b.Trees( { 0, 0, 39 }, 12.5f, 5, 9.0f );
	b.Flag( { -8.0f, 0, 44.0f }, kPurple );
	b.Flag( { 8.0f, 0, 44.0f }, kPurple );
	b.Fortress( { 0, 4, 37 }, 18.0f );
}

static void Level11( Builder& b )
{
	b.PlayerIsland();
	b.homeY = 0.0f;
	b.Island( { 0, 0, 33.5f }, 9.0f );

	// two kings, each behind its own crystal wall; the walls take turns
	// kings on heavy stone pillars: the crystal walls are the defence, not flimsy wood
	for ( int s = -1; s <= 1; s += 2 )
	{
		float x = 3.4f * s;
		float top = b.Column( { x, 0, 35.0f }, 3, 0.5f, Mat::Stone );
		b.Box( { x, top + 0.12f, 35.0f }, { 0.8f, 0.12f, 0.8f }, Mat::Stone );
		b.King( { x, top + 0.24f, 35.0f }, s < 0 ? kTeal : kPurple );
		b.Shield( { x, 3.2f, 33.4f }, { 1.5f, 3.2f, 0.12f }, 0.0f, 4.0f, 2.4f, s < 0 ? 0.0f : 2.0f );
	}

	b.Box( { 0, 0.4f, 36.5f }, { 0.4f, 0.4f, 0.4f }, Mat::Wood, 0.3f );
	b.Box( { 0, 1.2f, 36.5f }, { 0.4f, 0.4f, 0.4f }, Mat::Wood, -0.2f );
	b.Trees( { 0, 0, 33.5f }, 9.0f, 4, 6.8f );
	b.Flag( { 5.5f, 0, 37.0f }, kTeal );
	b.Fortress( { 0, 2.5f, 34.0f }, 10.0f );
}

static void Level12( Builder& b )
{
	b.PlayerIsland();
	b.homeY = 1.0f;
	b.Island( { 0, 1, 36 }, 10.5f );

	// Supports are massive stone (a 1 m block weighs 2 t), and the three kings stand far apart,
	// so no single bomb can take them all: each one has to be earned.

	// left: two walls with different rhythms, the way through opens only when both are down
	float c = b.Column( { -4.4f, 1.0f, 36.0f }, 2, 0.5f, Mat::Stone );
	b.Box( { -4.4f, c + 0.12f, 36.0f }, { 0.8f, 0.12f, 0.8f }, Mat::Stone );
	b.King( { -4.4f, c + 0.24f, 36.0f }, kCrimson );
	b.Shield( { -4.4f, 3.4f, 34.2f }, { 1.5f, 2.4f, 0.12f }, 0.0f, 3.0f, 1.6f, 0.0f );
	b.Shield( { -4.4f, 3.4f, 33.2f }, { 1.5f, 2.4f, 0.12f }, 0.0f, 5.0f, 2.2f, 1.0f );

	// right: a stone pillar under a crystal canopy, with a stone wall in front forcing a lob
	float t = b.Column( { 4.4f, 1.0f, 37.5f }, 4, 0.5f, Mat::Stone );
	b.Box( { 4.4f, t + 0.12f, 37.5f }, { 0.8f, 0.12f, 0.8f }, Mat::Stone );
	b.King( { 4.4f, t + 0.24f, 37.5f }, kOrange );
	b.Shield( { 4.4f, t + 2.4f, 37.5f }, { 1.4f, 0.12f, 1.4f }, 0.0f, 3.5f, 2.0f, 0.5f );
	b.Wall( { 2.4f, 1.0f, 34.3f }, true, 4, 6, Mat::Stone );

	// back: a king on the roof of a stone hut, only reachable with a high shot
	float h = b.Hut( { 0, 1.0f, 42.0f }, 1.1f, 1.6f, Mat::Stone, Mat::Stone );
	b.King( { 0, h, 42.0f }, kGreen );

	b.Trees( { 0, 1, 36 }, 10.5f, 4, 8.3f );
	b.Flag( { -7.0f, 1.0f, 40.0f }, kCrimson );
	b.Fortress( { 0, 4.0f, 37.5f }, 13.0f );
}

static void Level13( Builder& b )
{
	b.PlayerIsland();
	b.homeY = 0.0f;
	b.Island( { 0, 0, 34.5f }, 9.5f );

	// centre: a courtyard walled in stone on three sides; behind the king a tall rubber wall
	// sends overshooting lobs back down onto him
	b.Wall( { -2.0f, 0, 31.5f }, true, 4, 6, Mat::Stone );
	b.Wall( { -2.25f, 0, 32.0f }, false, 3, 6, Mat::Stone );
	b.Wall( { 2.25f, 0, 32.0f }, false, 3, 6, Mat::Stone );
	float c = b.Column( { 0, 0, 34.0f }, 2, 0.5f, Mat::Stone );
	b.King( { 0, c, 34.0f }, kPurple );
	b.Bumper( { 0, 2.6f, 36.2f }, { 2.6f, 2.6f, 0.2f } );

	// right: a wooden tower behind a low wall of sandbags that swallows direct hits
	b.SandbagWall( { 3.6f, 0, 32.6f }, true, 3, 3 );
	float t = b.Tower( { 5.1f, 0, 35.0f }, 2, 1.0f, 1.2f, Mat::Wood, Mat::Wood );
	b.King( { 5.1f, t, 35.0f }, kCrimson );

	// left: a king on a pile of sandbags, which bombs barely move
	float pile = b.SandbagWall( { -6.1f, 0, 34.0f }, true, 2, 4 );
	b.Box( { -5.1f, pile + 0.12f, 34.0f }, { 0.8f, 0.12f, 0.6f }, Mat::Wood );
	b.King( { -5.1f, pile + 0.24f, 34.0f }, kGreen );

	b.Trees( { 0, 0, 34.5f }, 9.5f, 4, 7.6f );
	b.Flag( { 6.5f, 0, 38.5f }, kPurple );
	b.Fortress( { 0, 3.0f, 34.5f }, 12.0f );
}

static void Level14( Builder& b )
{
	b.PlayerIsland();
	b.homeY = 1.0f;
	b.Island( { 0, 1, 36.5f }, 10.0f );

	// centre: a tall wooden tower boxed in by sandbags, so bombs outside do nothing.
	// A sticky bomb on the tower, or a Vortice beside it, is the way in.
	b.SandbagWall( { -2.1f, 1.0f, 33.6f }, true, 4, 5 );
	b.SandbagWall( { -2.4f, 1.0f, 34.2f }, false, 3, 5 );
	b.SandbagWall( { 2.4f, 1.0f, 34.2f }, false, 3, 5 );
	float t = b.Tower( { 0, 1.0f, 35.6f }, 3, 1.0f, 1.3f, Mat::Wood, Mat::Wood );
	b.King( { 0, t, 35.6f }, kOrange );

	// right: a king on stone behind a rubber wall that bounces cannonballs away
	float c = b.Column( { 7.0f, 1.0f, 38.5f }, 2, 0.5f, Mat::Stone );
	b.Box( { 7.0f, c + 0.12f, 38.5f }, { 0.8f, 0.12f, 0.8f }, Mat::Wood );
	b.King( { 7.0f, c + 0.24f, 38.5f }, kBlue );
	b.Bumper( { 7.0f, 2.6f, 36.4f }, { 1.6f, 1.6f, 0.2f } );

	// left: a king on a sandbag mound
	float pile = b.SandbagWall( { -8.0f, 1.0f, 38.5f }, true, 2, 5 );
	b.Box( { -7.0f, pile + 0.12f, 38.5f }, { 0.8f, 0.12f, 0.6f }, Mat::Wood );
	b.King( { -7.0f, pile + 0.24f, 38.5f }, kTeal );

	b.Trees( { 0, 1, 36.5f }, 10.0f, 3, 8.6f );
	b.Flag( { 0.0f, 1.0f, 41.5f }, kOrange );
	b.Fortress( { 0, 4.0f, 37.0f }, 13.0f );
}

// ---------------------------------------------------------------------------------------------
// Picchi Gelati
// ---------------------------------------------------------------------------------------------

static const Color kSnow{ 226, 234, 246, 255 };

// A fixed stone house whose front wall stops short of the ice: a curling stone slides under it,
// a cannonball does not fit. Kings inside cannot be seen or shot directly.
static void StoneHouse( Builder& b, float cx, float floorY, float fz, float halfX )
{
	const float gap = 0.45f, top = floorY + 2.4f, w = 0.15f, depth = 3.0f;
	float frontBottom = floorY + gap;
	b.Ledge( { cx, ( frontBottom + top ) * 0.5f, fz }, { halfX, ( top - frontBottom ) * 0.5f, w }, Mat::Stone );
	for ( int s = -1; s <= 1; s += 2 )
	{
		b.Ledge( { cx + s * ( halfX - w ), ( floorY + top ) * 0.5f, fz + depth * 0.5f }, { w, ( top - floorY ) * 0.5f, depth * 0.5f }, Mat::Stone );
	}
	b.Ledge( { cx, ( floorY + top ) * 0.5f, fz + depth - w }, { halfX, ( top - floorY ) * 0.5f, w }, Mat::Stone );
	b.Ledge( { cx, top + w, fz + depth * 0.5f - w * 0.5f }, { halfX + 0.2f, w, depth * 0.5f + 0.2f }, Mat::Stone );
}

// Kings on ice bridges between rock spires: the bridges shatter.
static void LevelCrepacci( Builder& b )
{
	b.PlayerIsland();
	auto bridge = [&]( float x0, float x1, float y, float z, Color robe, Mat wall ) {
		b.homeY = y;
		b.Island( { x0, y, z }, 1.1f, 3.5f );
		b.Island( { x1, y, z }, 1.1f, 3.5f );
		float cx = ( x0 + x1 ) * 0.5f;
		float half = ( x1 - x0 ) * 0.5f - 1.1f + 0.75f;
		b.Box( { cx, y + 0.15f, z }, { half, 0.15f, 0.8f }, Mat::Ice );
		b.Wall( { cx - 1.0f, y + 0.3f, z - 0.5f }, true, 2, 3, wall );
		b.King( { cx, y + 0.3f, z + 0.3f }, robe );
	};
	bridge( -7.4f, -2.8f, 0.0f, 32.5f, kTeal, Mat::Stone );
	bridge( -2.3f, 2.3f, 2.5f, 38.5f, kBlue, Mat::Ice );
	bridge( 2.8f, 7.4f, 0.0f, 32.5f, kPurple, Mat::Stone );
	b.Flag( { 7.4f, 0.0f, 32.8f }, kTeal );
	b.Fortress( { 0, 1.5f, 35.0f }, 10.0f );
}

// Curling: push the stones along the ice, under the wall of the house where two kings hide.
static void LevelCurling( Builder& b )
{
	b.PlayerIsland();
	b.homeY = 0.0f;
	b.Island( { 0, 0, 34 }, 9.5f );
	b.Ledge( { 0, 0.05f, 33.5f }, { 2.6f, 0.05f, 6.5f }, Mat::Ice );
	const float fz = 36.6f;
	StoneHouse( b, 0.0f, 0.1f, fz, 2.5f );
	for ( int s = -1; s <= 1; s += 2 )
	{
		Entity* king = b.King( { s * 1.0f, 0.1f, fz + 1.2f }, s < 0 ? kTeal : kBlue );
		// each stone sits on the line from the cannon to its king
		Entity* stone = b.CurlingStone( { s * 0.8f, 0.1f, 30.5f } );
		b.AimHint( king, stone, { 0, 0.2f, -0.3f } );
	}

	// outside: a king on a stone column behind a low wall of ice bricks
	float c = b.Column( { 6.2f, 0, 35.5f }, 2, 0.5f, Mat::Stone );
	b.Box( { 6.2f, c + 0.12f, 35.5f }, { 0.8f, 0.12f, 0.8f }, Mat::Stone );
	b.King( { 6.2f, c + 0.24f, 35.5f }, kPurple );
	b.Wall( { 4.7f, 0, 33.6f }, true, 3, 3, Mat::Ice );

	b.Trees( { 0, 0, 34 }, 9.5f, 3, 7.5f );
	b.Flag( { -4.5f, 0, 38.0f }, kTeal );
	b.Fortress( { 1.5f, 1.5f, 35.0f }, 11.0f );
}

// Two roofs of packed snow on ice pillars, two kings under each.
static void LevelStalattiti( Builder& b )
{
	b.PlayerIsland();
	b.homeY = 0.0f;
	b.Island( { 0, 0, 34 }, 9.5f );
	const Color robes[4] = { kTeal, kBlue, kPurple, kCrimson };
	for ( int s = 0; s < 2; ++s )
	{
		float cx = s == 0 ? -4.3f : 4.3f;
		Entity* front = b.SnowShelter( { cx, 0, 35.0f }, 1.3f, 1.1f, 2.2f );
		for ( int k = 0; k < 2; ++k )
		{
			Entity* king = b.King( { cx + ( k == 0 ? -0.55f : 0.55f ), 0, 34.75f }, robes[s * 2 + k] );
			b.AimHint( king, front, { 0, 0.2f, 0 } );
		}
	}
	b.Pyramid( { 0, 0, 37.5f }, 3, 0.35f, Mat::Ice );
	b.Trees( { 0, 0, 34 }, 9.5f, 4, 7.0f );
	b.Flag( { 0, 0, 39.5f }, kTeal );
	b.Fortress( { 0, 1.5f, 35.0f }, 10.0f );
}

// Avalanche: a gate holds three snowballs on a ramp above the kings' towers.
static void LevelValanga( Builder& b )
{
	b.PlayerIsland();
	b.homeY = 0.0f;
	b.Island( { 0, 0, 35 }, 9.5f );

	const float angle = 26.0f * DEG2RAD;
	const Vector3 low{ 0, 0, 37.5f };
	b.Ramp( low, 8.0f, 2.4f, angle, Mat::Rock, kSnow );
	Vector3 along{ 0, sinf( angle ), cosf( angle ) };
	Vector3 foot = Vector3Add( low, Vector3Scale( along, 5.2f ) ); // where the gate meets the slope
	const Vector3 half{ 2.3f, 0.75f, 0.12f };
	Entity* gate = b.Gate( { 0, foot.y + half.y + 0.1f, foot.z }, half );

	// three snowballs side by side, each resting on the slope and against the gate
	const float r = 0.7f;
	for ( int i = -1; i <= 1; ++i )
	{
		float z = foot.z + half.z + r + 0.01f;
		float y = low.y + ( r + sinf( angle ) * ( z - low.z ) ) / cosf( angle ) + 0.01f;
		b.Snowball( { i * 1.6f, y, z }, r );
	}

	// stacks of solid crates in a row at the foot of the ramp, one in the path of each snowball: a snowball
	// cannot slip between their legs the way it would through a tower, and none can topple the others
	const Vector3 towers[3] = { { -2.0f, 0, 35.6f }, { 0.0f, 0, 35.6f }, { 2.0f, 0, 35.6f } };
	const Color robes[3] = { kTeal, kBlue, kPurple };
	for ( int i = 0; i < 3; ++i )
	{
		float t = b.Column( towers[i], i == 2 ? 3 : 4, 0.4f, Mat::Wood );
		Entity* king = b.King( { towers[i].x, t, towers[i].z }, robes[i] );
		b.AimHint( king, gate, { 0, 0.3f, 0 } );
	}
	b.Trees( { 0, 0, 35 }, 9.5f, 4, 7.0f );
	b.Flag( { 4.0f, 0, 38.5f }, kTeal );
	b.Fortress( { 0, 1.5f, 36.0f }, 11.0f );
}

// The finale: the ice palace behind a crystal shield, a snow shelter and a curling lane.
static void LevelReggia( Builder& b )
{
	b.PlayerIsland();
	b.homeY = 1.0f;
	b.Island( { 0, 1, 40 }, 12.5f );

	// centre: Re Ghiacciolo on his ice tower, behind a crystal wall
	float t = b.Tower( { 0, 1, 43.0f }, 3, 1.1f, 1.2f, Mat::Ice, Mat::Ice );
	b.King( { 0, t, 43.0f }, kTeal );
	b.Shield( { 0, 1.0f + 2.9f, 40.6f }, { 1.9f, 2.9f, 0.12f }, 0.0f, 4.5f, 2.6f, 0.0f );

	// left: two kings under a roof of snow
	Entity* front = b.SnowShelter( { -6.5f, 1, 39.0f }, 1.3f, 1.1f, 2.2f );
	for ( int k = 0; k < 2; ++k )
	{
		Entity* king = b.King( { -6.5f + ( k == 0 ? -0.55f : 0.55f ), 1, 38.75f }, k == 0 ? kBlue : kPurple );
		b.AimHint( king, front, { 0, 0.2f, 0 } );
	}

	// right: a curling lane into a stone house
	b.Ledge( { 6.2f, 1.05f, 38.0f }, { 2.0f, 0.05f, 6.0f }, Mat::Ice );
	StoneHouse( b, 6.2f, 1.1f, 41.6f, 1.8f );
	Entity* king = b.King( { 6.2f, 1.1f, 42.8f }, kCrimson );
	Entity* stone = b.CurlingStone( { 6.2f * ( 35.5f - 1.0f ) / ( 42.8f - 1.0f ), 1.1f, 35.5f } );
	b.AimHint( king, stone, { 0, 0.2f, -0.3f } );

	b.Trees( { 0, 1, 40 }, 12.5f, 4, 10.0f );
	b.Flag( { -3.0f, 1, 46.0f }, kTeal );
	b.Flag( { 3.0f, 1, 46.0f }, kTeal );
	b.Fortress( { 0, 4.0f, 40.5f }, 14.0f );
}

static const LevelDef s_levels[] = {
	{ "Primo Colpo", "Una torre di legno basta e avanza. Chi mai sparerebbe a un re?", "Muovi il mouse per mirare, rotellina per la potenza, click per sparare!",
	  { 4, 0, 0, 0, 0 }, 1, { 0, 0, 0 }, Level01, "prati_primo_colpo" },
	{ "Mura di Pietra", "Tre re, due in vista. Il terzo? Segreto di stato.", "Conta le corone: un re si nasconde. Premi TAB per guardare dietro le mura. La BOMBA (2) esplode all'impatto.",
	  { 5, 2, 0, 0, 0 }, 3, { 0, 0, 0 }, Level02, "prati_mura" },
	{ "Il Ponte", "Il mio ponte regge un re. Anche due, se stanno fermi.", "La PALLA INCATENATA (tasto 4) spazza tutto. Le corde si spezzano!",
	  { 3, 0, 0, 2, 0 }, 3, { 0, 0, 0 }, Level03, "prati_ponte" },
	{ "Palazzo di Ghiaccio", "Le mie torri di ghiaccio non si sciolgono, figuriamoci sotto le tue palle di ferro.", "Il ghiaccio si frantuma. Il GRAPPOLO (tasto 3) si divide con SPAZIO.",
	  { 3, 0, 2, 0, 0 }, 3, { 0, 0, 0 }, Level04, "gelo_palazzo" },
	{ "Mongolfiere", "Da quassù i tuoi cannoni sembrano giocattoli.", "Buca i palloni e i cesti precipiteranno tra le nuvole.",
	  { 3, 0, 2, 0, 0 }, 3, { 0.8f, 0, 0 }, Level05, "prati_mongolfiere" },
	{ "Il Mulino", "Le mie pale girano da cent'anni. Non si fermeranno per te.", "Aspetta il momento giusto, oppure passa sopra le pale. Prova il MACIGNO (5).",
	  { 4, 2, 0, 0, 1 }, 3, { 0, 0, 0 }, Level06, "mulini_mulino" },
	{ "Il Pendolo", "Tic, tac. Il pendolo decide chi resta in piedi.", "Colpisci il pendolo e lascia fare alla fisica. Occhio al vento!",
	  { 3, 1, 0, 0, 0 }, 2, { -2.2f, 0, 0 }, Level07, "mulini_pendolo" },
	{ "Polveriera", "La polvere da sparo è ben custodita: proprio sotto di noi.", "Il TNT esplode se colpito forte. Le esplosioni si propagano...",
	  { 2, 1, 0, 0, 0 }, 2, { 0, 0, 0 }, Level08, "prati_polveriera" },
	{ "Scudi Mobili", "Muri che vanno e vengono. Come le tue speranze.", "Gli scudi scorrono su binari. Spara nel varco!",
	  { 4, 0, 0, 1, 2 }, 4, { 1.4f, 0, 0.4f }, Level09, "mulini_scudi" },
	{ "La Cittadella", "Hai buttato giù i miei cugini. Ma me, non mi prendi.", "Sei re, tre isole. Usa tutto l'arsenale.", { 4, 3, 2, 2, 2 }, 6, { -1.0f, 0, 0 },
	  Level10, "prati_cittadella" },
	{ "Cristalli Guardiani", "Il cristallo protegge. Il cristallo aspetta. Il cristallo non sbaglia.",
	  "Gli scudi di cristallo si spengono a intervalli: guarda l'anello sopra ogni scudo e spara al momento giusto.",
	  { 4, 1, 0, 0, 0 }, 2, { 0, 0, 0 }, Level11, "gelo_cristalli" },
	{ "Doppia Guardia", "Due guardie di cristallo sono meglio di una.", "Due scudi in fila: si passa solo quando sono spenti entrambi. Tieni conto del volo.",
	  { 4, 2, 0, 0, 1 }, 3, { 0.8f, 0, 0 }, Level12, "gelo_doppia" },
	{ "Sponde di Gomma", "Qui tutto rimbalza, perfino le tue minacce.",
	  "La gomma rimanda indietro i colpi: usa il muro di gomma dietro il re. I sacchi assorbono urti ed esplosioni.",
	  { 5, 1, 0, 0, 0, 0, 0 }, 3, { 0, 0, 0 }, Level13, "prati_gomma" },
	{ "Il Bunker", "Sacchi di sabbia. Tanti, tanti sacchi di sabbia.", "Novità: il VORTICE (6) risucchia i blocchi, la bomba ADESIVA (7) si attacca ed esplode dopo 3 s.",
	  { 3, 1, 0, 0, 0, 2, 2 }, 3, { 0.6f, 0, 0 }, Level14, "prati_bunker" },
	{ "Crepacci", "Il ghiaccio regge. Quasi sempre.", "I ponti di ghiaccio si frantumano: colpiscili e i re cadranno tra le nuvole.",
	  { 4, 0, 1, 0, 0, 0, 0 }, 3, { 0, 0, 0 }, LevelCrepacci, "gelo_crepacci" },
	{ "Curling", "Le mie pietre scivolano. I miei re, no.",
	  "Due re si nascondono nella casa di pietra. Colpisci le pietre da curling da dietro: passano sotto il muro.",
	  { 4, 0, 0, 0, 0, 0, 0 }, 3, { 0, 0, 0 }, LevelCurling, "gelo_curling" },
	{ "Stalattiti", "Sotto il mio tetto nessuno ci tocca. Nemmeno il cielo.",
	  "I tetti di neve poggiano su colonne di ghiaccio: spezza una colonna davanti e il tetto crolla.",
	  { 4, 1, 0, 0, 0, 0, 0 }, 2, { 0, 0, 0 }, LevelStalattiti, "gelo_stalattiti" },
	{ "Valanga", "La neve lassù è ferma da secoli. Non svegliarla.", "Colpisci la diga di ghiaccio sulla rampa: le palle di neve faranno il resto.",
	  { 3, 0, 0, 0, 0, 0, 0 }, 1, { 0, 0, 0 }, LevelValanga, "gelo_valanga" },
	{ "La Reggia di Ghiacciolo", "Benvenuto nella mia reggia. Resterai congelato all'ingresso.",
	  "Cristallo, neve e pietre da curling: tutto quello che hai imparato sul ghiaccio ti servirà.",
	  { 5, 2, 1, 0, 0, 0, 0 }, 4, { 0, 0, 0 }, LevelReggia, "gelo_reggia" },
};

// ---------------------------------------------------------------------------------------------
// Campaigns
// ---------------------------------------------------------------------------------------------

static std::vector<Campaign> BuildCampaigns()
{
	auto idx = []( const char* id ) { return FindLevelById( id ); };
	std::vector<Campaign> c;
	c.push_back( { "Prati Alti", "Re Bernardo il Tondo", kPurple, 0,
				   "Le isole del Regno di Sopra restano in cielo grazie alla Corona dei Venti. Sei re avidi l'hanno spezzata "
				   "e se ne sono presi un frammento ciascuno: senza la corona intera, le isole scendono piano verso le nuvole. "
				   "Mastra Bombarda carica il cannone. Si parte dai Prati Alti, dove regna Re Bernardo il Tondo.",
				   "Re Bernardo rotola gi\u00f9 dalla sua cittadella e il primo frammento della Corona torna a brillare. "
				   "L'isola di Mastra Bombarda risale di qualche metro. Verso ovest, il vento porta il cigolio di cento mulini.",
				   { idx( "prati_primo_colpo" ), idx( "prati_mura" ), idx( "prati_ponte" ), idx( "prati_mongolfiere" ),
					 idx( "prati_polveriera" ), idx( "prati_gomma" ), idx( "prati_bunker" ), idx( "prati_cittadella" ) } } );
	c.push_back( { "Valle dei Mulini", "Regina Ottavia", kOrange, 1,
				   "Nella Valle dei Mulini il tramonto non finisce mai. La Regina Ottavia ha costruito difese che si muovono: "
				   "pale, pendoli, scudi su binari. Qui non basta mirare bene: bisogna scegliere il momento.",
				   "Le pale si fermano e il secondo frammento torna al suo posto. Pi\u00f9 in alto, dove l'aria si fa gelida, "
				   "qualcuno ha costruito un palazzo di ghiaccio.",
				   { idx( "mulini_mulino" ), idx( "mulini_pendolo" ), idx( "mulini_scudi" ) } } );
	c.push_back( { "Picchi Gelati", "Re Ghiacciolo III", kTeal, 2,
				   "Sui Picchi Gelati regna Re Ghiacciolo III, che non si fida di nessuno, e meno che mai dei muri normali: "
				   "i suoi sono di cristallo, e si accendono e si spengono quando vuole lui.",
				   "Il cristallo si spegne per sempre. Tre frammenti su sei: la Corona dei Venti ricomincia a soffiare.",
				   { idx( "gelo_palazzo" ), idx( "gelo_crepacci" ), idx( "gelo_curling" ), idx( "gelo_cristalli" ),
					 idx( "gelo_stalattiti" ), idx( "gelo_valanga" ), idx( "gelo_doppia" ), idx( "gelo_reggia" ) } } );
	c.push_back( { "Dune Sospese", "Sultana Zaira", { 225, 170, 40, 255 }, 3,
				   "Sulle Dune Sospese la sabbia vola pi\u00f9 in alto delle isole. La Sultana Zaira si nasconde dietro montagne "
				   "di sacchi, e il vento cambia a ogni colpo.",
				   "La tempesta di sabbia si posa. Quattro frammenti su sei.", {} } );
	c.push_back( { "Arcipelago delle Tempeste", "Re Fulmine", { 40, 60, 140, 255 }, 4,
				   "Nell'Arcipelago delle Tempeste le isole non stanno ferme un attimo, e Re Fulmine ama far piovere lampi sui "
				   "suoi nemici.",
				   "Le nuvole si aprono. Cinque frammenti su sei.", {} } );
	c.push_back( { "Fucina del Vulcano", "l'Imperatore di Ferro", { 190, 40, 30, 255 }, 5,
				   "Sopra un mare di lava, l'Imperatore di Ferro ha forgiato l'ultima fortezza del Regno di Sopra. Custodisce "
				   "l'ultimo frammento della Corona, e tutto quello che hai imparato ti servir\u00e0.",
				   "La Corona dei Venti \u00e8 di nuovo intera. Le isole tornano a salire, e Mastra Bombarda pu\u00f2 finalmente "
				   "riposare. Per un po'.",
				   {} } );
	return c;
}

static const std::vector<Campaign>& Campaigns()
{
	static std::vector<Campaign> c = BuildCampaigns();
	return c;
}

const Campaign& GetCampaign( int index )
{
	return Campaigns()[index];
}

int CampaignCount()
{
	return (int)Campaigns().size();
}

int CampaignOfLevel( int levelIndex )
{
	for ( int c = 0; c < CampaignCount(); ++c )
	{
		for ( int l : GetCampaign( c ).levels )
		{
			if ( l == levelIndex )
			{
				return c;
			}
		}
	}
	return -1;
}

int PositionInCampaign( int levelIndex )
{
	int c = CampaignOfLevel( levelIndex );
	if ( c < 0 )
	{
		return 0;
	}
	const std::vector<int>& ls = GetCampaign( c ).levels;
	for ( size_t i = 0; i < ls.size(); ++i )
	{
		if ( ls[i] == levelIndex )
		{
			return (int)i;
		}
	}
	return 0;
}

int FindLevelById( const char* id )
{
	for ( int i = 0; i < LevelCount(); ++i )
	{
		if ( strcmp( GetLevel( i ).id, id ) == 0 )
		{
			return i;
		}
	}
	return -1;
}

const LevelDef& GetLevel( int index )
{
	return s_levels[index];
}

int LevelCount()
{
	return (int)( sizeof( s_levels ) / sizeof( s_levels[0] ) );
}

// ---------------------------------------------------------------------------------------------
// Endless challenge generator
// ---------------------------------------------------------------------------------------------

ChallengePlan PlanChallenge( int round, uint32_t seed )
{
	ChallengePlan p;
	p.round = round;
	p.seed = seed;
	Rng r( seed * 31u + (uint32_t)round * 7919u );
	int d = round;

	p.islandRadius = r.Range( 7.5f, 10.0f );
	p.islandPos = { r.Range( -4.0f, 4.0f ), r.Range( -2.0f, 3.0f ), 27.0f + r.Range( 0.0f, 6.0f ) + (float)std::min( d, 6 ) };
	p.kings = std::max( 1, std::min( 5, 1 + d / 2 + r.Int( 0, 1 ) ) );
	p.satellite = d >= 3 && r.Float() < 0.5f;
	p.tnt = d >= 2 && r.Float() < 0.6f;
	p.mechanism = d >= 4 ? r.Int( 0, 3 ) : 0;
	if ( p.mechanism == 3 && p.kings < 2 )
	{
		p.mechanism = 0;
	}
	int onIsland = p.kings - ( p.mechanism == 3 ? 1 : 0 );
	p.structures = std::min( 6, onIsland + r.Int( 0, 2 ) );

	float windMag = d >= 2 ? r.Range( 0.0f, std::min( 0.6f * d, 3.0f ) ) : 0.0f;
	p.wind = { ( r.Float() < 0.5f ? -1.0f : 1.0f ) * windMag, 0.0f, r.Range( -0.3f, 0.3f ) * windMag };

	int total = p.kings + 2 + ( p.satellite ? 1 : 0 );
	p.ammo[(int)Ammo::Ball] = std::max( 1, total / 2 );
	int extra = total - p.ammo[(int)Ammo::Ball];
	int unlocked[6];
	int nUnlocked = 0;
	unlocked[nUnlocked++] = (int)Ammo::Bomb;
	if ( d >= 3 )
	{
		unlocked[nUnlocked++] = (int)Ammo::Cluster;
		unlocked[nUnlocked++] = (int)Ammo::Chain;
	}
	if ( d >= 4 )
	{
		unlocked[nUnlocked++] = (int)Ammo::Boulder;
	}
	if ( d >= 5 )
	{
		unlocked[nUnlocked++] = (int)Ammo::Sticky;
	}
	if ( d >= 6 )
	{
		unlocked[nUnlocked++] = (int)Ammo::Implosion;
	}
	for ( int i = 0; i < extra; ++i )
	{
		p.ammo[unlocked[r.Int( 0, nUnlocked - 1 )]] += 1;
	}
	p.par = p.kings + ( p.satellite ? 1 : 0 );
	return p;
}

void BuildChallenge( Builder& b )
{
	const ChallengePlan& p = *b.plan;
	static const Color robes[] = { kPurple, kCrimson, kBlue, kGreen, kOrange, kTeal };
	Rng& r = b.rng;

	b.PlayerIsland();
	Vector3 c = p.islandPos;
	b.homeY = c.y;
	b.Island( c, p.islandRadius );

	// pick non overlapping spots on the island
	std::vector<Vector3> slots;
	float front = c.z - p.islandRadius * 0.55f;
	if ( p.mechanism == 1 )
	{
		b.Windmill( { c.x, c.y, front }, 5.5f, 3.0f, r.Range( 0.7f, 1.1f ) * ( r.Float() < 0.5f ? -1.0f : 1.0f ) );
		slots.push_back( { c.x, c.y, front } );
	}
	for ( int attempt = 0; attempt < 400 && (int)slots.size() < p.structures + ( p.mechanism == 1 ? 1 : 0 ); ++attempt )
	{
		float a = r.Range( 0.0f, 2.0f * PI );
		float rad = ( p.islandRadius - 2.3f ) * sqrtf( r.Float() );
		Vector3 s{ c.x + cosf( a ) * rad, c.y, c.z + sinf( a ) * rad };
		bool ok = true;
		for ( const Vector3& o : slots )
		{
			if ( Vector3Distance( s, o ) < 3.4f )
			{
				ok = false;
				break;
			}
		}
		if ( ok )
		{
			slots.push_back( s );
		}
	}
	if ( p.mechanism == 1 )
	{
		slots.erase( slots.begin() );
	}
	// structures further back get the kings, the front ones are shields
	std::sort( slots.begin(), slots.end(), []( const Vector3& a, const Vector3& b ) { return a.z > b.z; } );

	int onIsland = p.kings - ( p.mechanism == 3 ? 1 : 0 );
	int kingsPlaced = 0;
	for ( size_t i = 0; i < slots.size(); ++i )
	{
		Vector3 s = slots[i];
		bool withKing = kingsPlaced < onIsland;
		Color robe = robes[( kingsPlaced + p.round ) % 6];
		int type = withKing ? r.Int( 0, 3 ) : r.Int( 0, 4 );
		Mat mats[3] = { Mat::Wood, Mat::Stone, Mat::Ice };
		Mat postMat = mats[r.Int( 0, p.round >= 2 ? 2 : 1 )];
		Mat slabMat = r.Float() < 0.7f ? Mat::Wood : postMat;
		float top = s.y;
		switch ( type )
		{
			case 0:
			{
				int floors = std::min( 4, 1 + r.Int( 0, 1 ) + p.round / 3 );
				top = b.Tower( s, floors, r.Range( 0.85f, 1.1f ), r.Range( 1.1f, 1.35f ), postMat, slabMat );
				break;
			}
			case 1:
			{
				float h = 0.45f;
				top = b.Column( s, r.Int( 2, 3 ), h, r.Float() < 0.6f ? Mat::Stone : Mat::Wood );
				b.Box( { s.x, top + 0.12f, s.z }, { 0.8f, 0.12f, 0.8f }, Mat::Wood );
				top += 0.24f;
				break;
			}
			case 2:
				top = b.Hut( s, r.Range( 1.0f, 1.2f ), r.Range( 1.4f, 1.8f ), Mat::Wood, Mat::Wood );
				break;
			case 3:
				top = b.Pyramid( s, r.Int( 2, 3 ), 0.4f, postMat == Mat::Ice ? Mat::Ice : Mat::Stone );
				break;
			default:
			{
				if ( p.round >= 4 && r.Float() < 0.5f )
				{
					int bags = r.Int( 3, 4 );
					b.SandbagWall( { s.x - bags * 0.5f, s.y, s.z }, true, bags, r.Int( 2, 4 ) );
				}
				else
				{
					int bricks = r.Int( 3, 5 );
					b.Wall( { s.x - bricks * 0.5f, s.y, s.z }, true, bricks, r.Int( 2, 4 ), Mat::Stone );
				}
				break;
			}
		}
		if ( withKing && type <= 3 )
		{
			b.King( { s.x, top, s.z }, robe );
			++kingsPlaced;
			if ( p.tnt && r.Float() < 0.6f )
			{
				b.Tnt( { s.x + r.Range( -1.6f, 1.6f ), s.y + 0.35f, s.z - 1.6f } );
			}
		}
	}

	// fall back to kings on plain slabs if the island ran out of room
	while ( kingsPlaced < onIsland )
	{
		Vector3 s{ c.x + r.Range( -2.0f, 2.0f ), c.y, c.z + r.Range( 0.0f, 2.0f ) };
		b.King( s, robes[kingsPlaced % 6] );
		++kingsPlaced;
	}

	if ( p.mechanism == 2 )
	{
		b.Slider( { c.x, c.y + 1.8f, c.z - p.islandRadius - 1.0f }, { 1.5f, 1.5f, 0.2f }, { 1, 0, 0 }, 3.2f, r.Range( 0.7f, 1.2f ), 0.0f,
				  Mat::Stone );
	}
	if ( p.mechanism == 3 )
	{
		float side = r.Float() < 0.5f ? -1.0f : 1.0f;
		b.BalloonBasket( { c.x + side * ( p.islandRadius + 3.0f ), c.y + 3.5f, c.z }, Color{ 230, 70, 80, 255 }, robes[5] );
	}

	float extent = p.islandRadius + 3.0f;
	if ( p.satellite )
	{
		float side = r.Float() < 0.5f ? -1.0f : 1.0f;
		Vector3 sc{ c.x + side * ( p.islandRadius + 6.5f ), c.y + 2.0f, c.z - 3.0f };
		b.homeY = sc.y;
		b.Island( sc, 3.6f );
		float t = b.Tower( sc, 2, 0.9f, 1.2f, Mat::Wood, Mat::Wood );
		b.King( { sc.x, t, sc.z }, robes[( p.round + 3 ) % 6] );
		b.homeY = c.y;
		extent += 6.0f;
	}
	if ( p.mechanism == 3 )
	{
		extent += 3.0f;
	}

	b.Trees( c, p.islandRadius, 3, p.islandRadius - 1.6f );
	b.Flag( { c.x + p.islandRadius * 0.6f, c.y, c.z + p.islandRadius * 0.5f }, robes[p.round % 6] );
	b.Fortress( { c.x, c.y + 3.0f, c.z }, extent );
}
