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
		if ( t == Decoration::Tree || t == Decoration::Pine )
		{
			bool pine = t == Decoration::Pine;
			TreeKind kind = Look().desert ? ( pine ? TreeKind::Cactus : TreeKind::Palm ) : ( pine ? TreeKind::Pine : TreeKind::Oak );
			Tree( d.pos, d.scale, kind, d.color, d.rot );
		}
		else
		{
			game.AddDecoration( d );
		}
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
	// riding a lift: he only counts as fallen below the lowest point of its run
	for ( const Mechanism& m : scene.mechanisms )
	{
		if ( m.type != MechType::Mover || m.entity == nullptr )
		{
			continue;
		}
		Vector3 c = m.entity->pos;
		Vector3 h = m.entity->parts[0].size;
		if ( fabsf( feet.x - c.x ) < h.x + 0.2f && fabsf( feet.z - c.z ) < h.z + 0.2f && feet.y > c.y && feet.y < c.y + h.y + 0.4f )
		{
			e->homeY = feet.y + std::min( m.home.y, m.home.y + m.amplitude * m.axis.y ) - c.y;
			// and he sets off already moving with it
			Vector3 v = Vector3Scale( Vector3Subtract( MoverPosAt( m, 0.01f ), MoverPosAt( m, 0.0f ) ), 100.0f );
			b3Body_SetLinearVelocity( e->body, ToB3( v ) );
		}
	}
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
	// the axle: the hub sits clear of the roof, which would otherwise rub against it and stall the blades
	scene.AddBox( tower, { 0, towerHeight - 0.2f, -0.9f }, b3Quat_identity, { 0.12f, 0.12f, 0.18f }, Mat::Wood, sh );
	scene.FinalizeEntity( tower );

	Vector3 hub{ base.x, base.y + towerHeight - 0.2f, base.z - 1.35f };
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
	m.entity = e;
	m.axis = Vector3Normalize( axis );
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

Entity* Builder::Reinforced( Vector3 center, Vector3 half, float yaw )
{
	BodyOptions bo;
	bo.type = b3_staticBody;
	Entity* e = scene.CreateEntity( Kind::Static, Mat::Stone, center, QuatYaw( yaw ), bo );
	ShapeOptions so;
	so.category = CatStatic;
	so.hitEvents = true;
	scene.AddBox( e, { 0, 0, 0 }, b3Quat_identity, half, Mat::Stone, so );
	e->parts.back().tint = Color{ 150, 140, 130, 255 };
	// iron bands across the face and down the edges, standing a little proud of the stone
	const Color iron{ 48, 50, 58, 255 };
	for ( int k = -1; k <= 1; k += 2 )
	{
		Part band;
		band.localPos = { 0, k * half.y * 0.55f, 0 };
		band.size = { half.x + 0.04f, 0.11f, half.z + 0.04f };
		band.mat = Mat::Metal;
		band.tint = iron;
		scene.AddVisual( e, band );
		Part post;
		post.localPos = { k * ( half.x - 0.08f ), 0, 0 };
		post.size = { 0.11f, half.y + 0.02f, half.z + 0.04f };
		post.mat = Mat::Metal;
		post.tint = iron;
		scene.AddVisual( e, post );
	}
	e->reinforced = true;
	scene.FinalizeEntity( e );
	return e;
}

Entity* Builder::GlassPane( Vector3 center, Vector3 half, float yaw, bool moving )
{
	BodyOptions bo;
	bo.type = moving ? b3_kinematicBody : b3_staticBody;
	Entity* e = scene.CreateEntity( moving ? Kind::Mechanism : Kind::Static, Mat::Glass, center, QuatYaw( yaw ), bo );
	ShapeOptions so;
	so.category = moving ? CatBlock : CatStatic;
	scene.AddBox( e, { 0, 0, 0 }, b3Quat_identity, half, Mat::Glass, so );
	// a brass frame, so it never passes for a crystal shield that will switch off
	const Color brass{ 176, 132, 58, 255 };
	const float bar = 0.07f;
	for ( int k = -1; k <= 1; k += 2 )
	{
		Part edge;
		edge.localPos = { 0, k * ( half.y - bar ), 0 };
		edge.size = { half.x, bar, half.z + 0.03f };
		edge.mat = Mat::Metal;
		edge.tint = brass;
		scene.AddVisual( e, edge );
		Part post;
		post.localPos = { k * ( half.x - bar ), 0, 0 };
		post.size = { bar, half.y, half.z + 0.03f };
		post.mat = Mat::Metal;
		post.tint = brass;
		scene.AddVisual( e, post );
	}
	e->homeY = -1000.0f;
	scene.FinalizeEntity( e );
	return e;
}

Entity* Builder::Platform( Vector3 center, Vector3 half, Mat mat, Color tint, float yaw )
{
	BodyOptions bo;
	bo.type = b3_kinematicBody;
	Entity* e = scene.CreateEntity( Kind::Mechanism, mat, center, QuatYaw( yaw ), bo );
	ShapeOptions so;
	so.category = CatBlock;
	scene.AddBox( e, { 0, 0, 0 }, b3Quat_identity, half, mat, so );
	if ( tint.a > 0 )
	{
		e->parts.back().tint = tint;
	}
	e->homeY = -1000.0f;
	scene.FinalizeEntity( e );
	return e;
}

Entity* Builder::Carpet( Vector3 center, float halfX, float halfZ, Color color, float yaw )
{
	BodyOptions bo;
	bo.type = b3_kinematicBody;
	Entity* e = scene.CreateEntity( Kind::Mechanism, Mat::Sand, center, QuatYaw( yaw ), bo );
	ShapeOptions so;
	so.category = CatBlock;
	so.friction = 1.0f;
	scene.AddBox( e, { 0, 0, 0 }, b3Quat_identity, { halfX, 0.06f, halfZ }, Mat::Sand, so );
	e->parts.back().tint = color;
	// a golden border, a medallion in the middle and a tassel at each corner
	const Color gold{ 230, 180, 60, 255 };
	Part border;
	border.localPos = { 0, -0.015f, 0 };
	border.size = { halfX + 0.05f, 0.05f, halfZ + 0.05f };
	border.mat = Mat::Sand;
	border.tint = gold;
	scene.AddVisual( e, border );
	Part medallion;
	medallion.localPos = { 0, 0.004f, 0 };
	medallion.size = { halfX * 0.55f, 0.06f, halfZ * 0.55f };
	medallion.mat = Mat::Sand;
	medallion.tint = ColorBrightness( color, -0.35f );
	scene.AddVisual( e, medallion );
	for ( int i = -1; i <= 1; i += 2 )
	{
		for ( int k = -1; k <= 1; k += 2 )
		{
			Part tassel;
			tassel.geo = Geo::Sphere;
			tassel.localPos = { i * ( halfX + 0.1f ), -0.03f, k * ( halfZ + 0.1f ) };
			tassel.size = { 0.07f, 0.07f, 0.07f };
			tassel.mat = Mat::Gold;
			tassel.tint = gold;
			scene.AddVisual( e, tassel );
		}
	}
	e->homeY = -1000.0f;
	scene.FinalizeEntity( e );
	return e;
}

Entity* Builder::Mover( Entity* e, Vector3 axis, float distance, float travel, float pause, float phase )
{
	Mechanism m;
	m.type = MechType::Mover;
	m.entity = e;
	m.home = e->pos;
	m.axis = Vector3Normalize( axis );
	m.amplitude = distance;
	m.travel = travel;
	m.pause = pause;
	m.phase = phase;
	scene.mechanisms.push_back( m );
	// start where the cycle puts it at time 0
	Vector3 start = MoverPosAt( m, 0.0f );
	b3Body_SetTransform( e->body, ToB3( start ), b3Body_GetRotation( e->body ) );
	e->pos = e->prevPos = start;
	return e;
}

Entity* Builder::MagicBarrier( Vector3 center, Vector3 half, float yaw )
{
	BodyOptions bo;
	bo.type = b3_staticBody;
	Entity* e = scene.CreateEntity( Kind::Static, Mat::Magic, center, QuatYaw( yaw ), bo );
	ShapeOptions so;
	so.category = CatBarrier;
	so.hitEvents = false;
	scene.AddBox( e, { 0, 0, 0 }, b3Quat_identity, half, Mat::Magic, so );
	// a frame of dark stone round the edges of the lattice, so the barrier is plain to see. The panel may be a
	// wall facing either way or a roof: the frame runs round its two long sides, whichever they are
	const Color frame{ 70, 60, 90, 255 };
	const float h[3] = { half.x, half.y, half.z };
	int thin = h[0] <= h[1] && h[0] <= h[2] ? 0 : ( h[1] <= h[2] ? 1 : 2 );
	for ( int k = 1; k <= 2; ++k )
	{
		int along = ( thin + k ) % 3;	// the bar runs along this axis...
		int across = ( thin + 3 - k ) % 3; // ...and sits on both edges of this one
		for ( int s = -1; s <= 1; s += 2 )
		{
			float pos[3] = { 0, 0, 0 };
			float size[3] = { 0, 0, 0 };
			pos[across] = s * ( h[across] + 0.08f );
			size[across] = 0.08f;
			size[along] = h[along] + 0.16f;
			size[thin] = h[thin] + 0.06f;
			Part bar;
			bar.localPos = { pos[0], pos[1], pos[2] };
			bar.size = { size[0], size[1], size[2] };
			bar.mat = Mat::Stone;
			bar.tint = frame;
			scene.AddVisual( e, bar );
		}
	}
	scene.FinalizeEntity( e );
	return e;
}

Entity* Builder::MagicOrb( Vector3 center, float radius )
{
	BodyOptions bo;
	bo.angularDamping = 0.3f;
	Entity* e = scene.CreateEntity( Kind::Block, Mat::Orb, center, b3Quat_identity, bo );
	ShapeOptions so;
	so.mask = CatAll & ~CatBarrier & ~CatDebris;
	so.rollingResistance = 0.01f; // rolls a long way, like a stone on ice
	scene.AddSphere( e, { 0, 0, 0 }, radius, Mat::Orb, so );
	e->lethal = true;
	e->homeY = homeY;
	scene.FinalizeEntity( e );
	return e;
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

Entity* Builder::Puffball( Vector3 center, float radius )
{
	BodyOptions bo;
	bo.angularDamping = 0.3f;
	bo.linearDamping = 0.4f;
	Entity* e = scene.CreateEntity( Kind::Block, Mat::Balloon, center, b3Quat_identity, bo );
	ShapeOptions so;
	so.mask = CatAll & ~CatDebris;
	scene.AddSphere( e, { 0, 0, 0 }, radius, Mat::Balloon, so );
	// a shade bluer and shinier than packed snow, for those who look closely
	e->parts.back().tint = Color{ 232, 240, 255, 255 };
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

void Builder::AimHint( Entity* king, Entity* via, Vector3 offset, float lob )
{
	game.AddAimHint( king, via, offset, lob );
}

void Builder::ShiftingWind( float strength )
{
	game.SetShiftingWind( strength );
}

void Builder::Trees( Vector3 center, float radius, int count, float minR )
{
	for ( int i = 0; i < count; ++i )
	{
		float a = rng.Range( 0.0f, 2.0f * PI );
		float r = rng.Range( minR, radius - 0.8f );
		// draw the same random numbers in every realm so a level's layout never depends on its look
		bool pine = rng.Float() < 0.5f;
		Vector3 base = { center.x + cosf( a ) * r, center.y, center.z + sinf( a ) * r };
		float scale = rng.Range( 0.7f, 1.2f );
		float yaw = rng.Range( 0.0f, 6.28f );
		TreeKind kind = Look().desert ? ( pine ? TreeKind::Cactus : TreeKind::Palm )
									  : ( pine || Look().pinesOnly ? TreeKind::Pine : TreeKind::Oak );
		Tree( base, scale, kind, ColorMix( Look().leafA, Look().leafB, rng.Float() ), yaw );
	}
}

static bool TreeOverlapFound( b3ShapeId shapeId, void* context )
{
	(void)shapeId;
	*(bool*)context = true;
	return false;
}

Entity* Builder::Tree( Vector3 base, float scale, TreeKind kind, Color leaf, float yaw )
{
	BodyOptions bo;
	bo.type = b3_staticBody;
	Entity* e = scene.CreateEntity( Kind::Static, Mat::Wood, base, QuatYaw( yaw ), bo );
	ShapeOptions so;
	so.category = CatStatic;
	so.hitEvents = true;
	std::vector<std::pair<std::vector<Vector3>, float>> probes;
	scene.AddTree( e, scale, kind, leaf, 0.0f, so, &probes );

	// a fixed tree grown into a tower would shove it over: leave that one out
	b3QueryFilter filter = b3DefaultQueryFilter();
	filter.categoryBits = CatStatic;
	filter.maskBits = CatBlock | CatKing;
	bool found = false;
	Quaternion q = QuaternionFromAxisAngle( { 0, 1, 0 }, yaw );
	for ( const auto& pr : probes )
	{
		b3Vec3 points[B3_MAX_SHAPE_CAST_POINTS];
		int n = 0;
		for ( const Vector3& v : pr.first )
		{
			if ( n < B3_MAX_SHAPE_CAST_POINTS )
			{
				points[n++] = ToB3( Vector3RotateByQuaternion( v, q ) );
			}
		}
		b3ShapeProxy proxy{ points, n, pr.second };
		b3World_OverlapShape( scene.World(), ToB3( base ), &proxy, filter, TreeOverlapFound, &found );
	}
	if ( found )
	{
		if ( getenv( "CROLLO_DEBUG" ) )
			fprintf( stderr, "albero tolto a %.1f %.1f %.1f: toccherebbe una costruzione\n", base.x, base.y, base.z );
		scene.DestroyEntity( e );
		return nullptr;
	}
	scene.FinalizeEntity( e );
	return e;
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

// A king hidden in the woods: a pine in front of him, the crown of an oak over his head against lobs, and a
// woodpile under the pine's low branches. Only the chain shot cuts the pine down, and it falls on him.
static void GroveKing( Builder& b, Vector3 feet, Color robe )
{
	Vector3 toCannon = Vector3Normalize( { -feet.x, 0.0f, -feet.z } );
	float yaw = atan2f( toCannon.x, toCannon.z );
	auto ahead = [&]( float d ) { return Vector3Add( feet, Vector3Scale( toCannon, d ) ); };
	const Biome& look = b.Look();
	Entity* king = b.King( feet, robe );
	b.Tree( ahead( -0.75f ), 1.7f, TreeKind::Oak, ColorMix( look.leafA, look.leafB, 0.3f ), yaw );
	// the pine's lowest branches spread 1.4 m and hang 0.9 m up: the king stands just clear of them, and the
	// woodpile closes the gap underneath
	Entity* pine = b.Tree( ahead( 1.85f ), 1.5f, TreeKind::Pine, ColorMix( look.leafA, look.leafB, 0.8f ), yaw );
	Vector3 pile = ahead( 3.6f );
	Entity* logs = b.Ledge( { pile.x, pile.y + 0.5f, pile.z }, { 1.3f, 0.5f, 0.3f }, Mat::Wood, QuaternionFromAxisAngle( { 0, 1, 0 }, yaw ) );
	// drawn as logs stacked three high and two deep
	logs->parts.back().visible = false;
	for ( int row = 0; row < 3; ++row )
	{
		for ( int k = 0; k < 2; ++k )
		{
			Part log;
			log.geo = Geo::Cylinder;
			float r = 0.165f;
			log.localPos = { -1.3f + b.rng.Range( -0.08f, 0.08f ), -0.5f + r + row * 2.0f * r, ( k == 0 ? -0.15f : 0.15f ) };
			log.localRot = QuaternionFromAxisAngle( { 0, 0, 1 }, -PI * 0.5f );
			log.size = { r, 2.6f, r };
			log.mat = Mat::Wood;
			log.tint = ColorBrightness( Color{ 140, 98, 60, 255 }, b.rng.Range( -0.12f, 0.08f ) );
			b.scene.AddVisual( logs, log );
		}
	}
	b.scene.FinalizeEntity( logs );
	if ( pine )
	{
		b.AimHint( king, pine, { 0, 2.0f, 0 } );
	}
}

static void LevelBoschetto( Builder& b )
{
	b.PlayerIsland();
	b.homeY = 0.0f;
	b.Island( { 0, 0, 36 }, 10.0f );
	GroveKing( b, { -4.2f, 0, 37.5f }, kGreen );
	GroveKing( b, { 0.6f, 0, 40.0f }, kPurple );
	GroveKing( b, { 5.0f, 0, 37.0f }, kOrange );
	b.Trees( { 0, 0, 36 }, 10.0f, 4, 8.2f );
	b.Flag( { 1.5f, 0, 43.0f }, kPurple );
	b.Fortress( { 0.5f, 2, 38 }, 11.0f );
}

// ---------------------------------------------------------------------------------------------
// Dune Sospese
// ---------------------------------------------------------------------------------------------

static const Color kGold{ 225, 170, 40, 255 };
static const Color kCarpetRed{ 170, 40, 45, 255 };
static const Color kCarpetBlue{ 40, 70, 150, 255 };
static const Color kCarpetGreen{ 40, 120, 80, 255 };
static const Color kSandstone{ 232, 196, 138, 255 };

// Gives the stone of everything built since entity `from` the warm colour of sandstone.
static void Sandstone( Builder& b, size_t from )
{
	for ( size_t i = from; i < b.scene.entities.size(); ++i )
	{
		Entity* e = b.scene.entities[i];
		bool changed = false;
		for ( Part& p : e->parts )
		{
			if ( p.mat == Mat::Stone )
			{
				p.tint = ColorBrightness( kSandstone, b.rng.Range( -0.06f, 0.06f ) );
				changed = true;
			}
		}
		if ( changed )
		{
			b.scene.FinalizeEntity( e );
		}
	}
}

// A king riding a flying carpet: the carpet crosses from `from` to `to` in `travel` seconds, rests `pause`
// seconds and comes back. Keep the peak acceleration, 6 * distance / travel^2, under about 3.5 m/s^2, or
// the king ends up tipping over at the turns.
static Entity* CarpetKing( Builder& b, Vector3 from, Vector3 to, float travel, float pause, float phase, Color carpet, Color robe )
{
	Entity* c = b.Carpet( from, 0.8f, 0.7f, carpet );
	b.Mover( c, Vector3Subtract( to, from ), Vector3Distance( from, to ), travel, pause, phase );
	return b.King( Vector3Add( c->pos, { 0, 0.06f, 0 } ), robe );
}

static void LevelCarovana( Builder& b )
{
	b.PlayerIsland();
	b.homeY = 0.0f;
	b.Island( { 0, 0, 35 }, 9.0f );
	size_t from = b.scene.entities.size();
	float t1 = b.Tower( { -3.5f, 0, 37.0f }, 2, 0.9f, 1.2f, Mat::Stone, Mat::Wood );
	b.King( { -3.5f, t1, 37.0f }, kTeal );
	float t2 = b.Tower( { 3.5f, 0, 37.0f }, 2, 0.9f, 1.2f, Mat::Stone, Mat::Wood );
	b.King( { 3.5f, t2, 37.0f }, kCrimson );
	Sandstone( b, from );
	// a carpet drifting across in front of the towers
	CarpetKing( b, { -5.5f, 3.2f, 31.5f }, { 5.5f, 3.2f, 31.5f }, 4.0f, 1.5f, 0.0f, kCarpetRed, kGold );
	b.Trees( { 0, 0, 35 }, 9.0f, 4, 7.0f );
	b.Flag( { 0, 0, 40.5f }, kGold );
	b.Fortress( { 0, 2, 35 }, 11.0f );
}

static void LevelVetrate( Builder& b )
{
	b.PlayerIsland();
	b.homeY = 0.0f;
	b.Island( { 0, 0, 35 }, 9.0f );
	// a glass front: the kings behind it are in plain view, and out of reach of anything but a lob
	b.GlassPane( { 0, 1.4f, 32.6f }, { 4.4f, 1.4f, 0.06f } );
	b.King( { -2.7f, 0, 35.4f }, kTeal );
	b.King( { 2.7f, 0, 35.4f }, kCrimson );
	size_t from = b.scene.entities.size();
	float t = b.Tower( { 0, 0, 36.0f }, 3, 0.9f, 1.2f, Mat::Stone, Mat::Wood );
	Sandstone( b, from );
	b.King( { 0, t, 36.0f }, kGold );
	b.Trees( { 0, 0, 35 }, 9.0f, 4, 7.0f );
	b.Flag( { 4.5f, 0, 37.5f }, kGold );
	b.Fortress( { 0, 2, 35 }, 11.0f );
}

static void LevelMontacarichi( Builder& b )
{
	b.PlayerIsland();
	b.homeY = 0.0f;
	b.Island( { 0, 0, 35 }, 9.0f );
	b.Ledge( { 0, 1.6f, 33.0f }, { 5.2f, 1.6f, 0.3f }, Mat::Stone, { 0, 0, 0, 1 }, kSandstone );
	// two lifts right behind the wall: a king only shows above it at the top of the run
	for ( int s = -1; s <= 1; s += 2 )
	{
		Entity* lift = b.Platform( { s * 3.0f, 0.15f, 34.2f }, { 0.8f, 0.15f, 0.8f }, Mat::Stone, kSandstone );
		b.Mover( lift, { 0, 1, 0 }, 3.4f, 2.2f, 1.8f, s < 0 ? 0.0f : 3.0f );
		b.King( Vector3Add( lift->pos, { 0, 0.15f, 0 } ), s < 0 ? kTeal : kCrimson );
	}
	size_t from = b.scene.entities.size();
	float t = b.Tower( { 0, 0, 36.8f }, 3, 0.9f, 1.2f, Mat::Stone, Mat::Wood );
	Sandstone( b, from );
	b.King( { 0, t, 36.8f }, kGold );
	b.Trees( { 0, 0, 35 }, 9.0f, 4, 7.0f );
	b.Flag( { -4.5f, 0, 38.0f }, kGold );
	b.Fortress( { 0, 2, 35 }, 11.0f );
}

static void LevelTappeti( Builder& b )
{
	b.PlayerIsland();
	b.homeY = 0.0f;
	b.Island( { 0, 0, 36 }, 9.0f );
	// one carpet sweeps across in front, one climbs and dips on the left
	CarpetKing( b, { -6.0f, 3.4f, 31.0f }, { 6.0f, 3.4f, 31.0f }, 5.0f, 1.0f, 0.0f, kCarpetRed, kTeal );
	CarpetKing( b, { -4.5f, 1.2f, 36.0f }, { -4.5f, 5.2f, 38.0f }, 3.0f, 1.2f, 1.5f, kCarpetGreen, kPurple );
	// the third hides behind a pane of glass held up between two columns, and shows only at the ends of its run
	for ( int s = -1; s <= 1; s += 2 )
	{
		b.Ledge( { 3.5f + s * 1.75f, 2.7f, 34.0f }, { 0.2f, 2.7f, 0.2f }, Mat::Stone, { 0, 0, 0, 1 }, kSandstone );
	}
	b.GlassPane( { 3.5f, 3.6f, 34.0f }, { 1.55f, 1.3f, 0.06f } );
	CarpetKing( b, { 0.8f, 2.8f, 35.6f }, { 6.8f, 2.8f, 35.6f }, 3.5f, 1.6f, 2.0f, kCarpetBlue, kCrimson );
	b.ShiftingWind( 1.2f );
	b.Trees( { 0, 0, 36 }, 9.0f, 4, 7.2f );
	b.Flag( { 0, 0, 41.0f }, kGold );
	b.Fortress( { 0, 3, 35 }, 11.0f );
}

static void LevelMiraggio( Builder& b )
{
	b.PlayerIsland();
	b.homeY = 0.0f;
	b.Island( { 0, 0, 34.5f }, 9.5f );

	const float front = 34.8f, back = 38.0f, h = 3.2f;
	b.MagicBarrier( { 0, h * 0.5f, front }, { 3.0f, h * 0.5f, 0.08f } );
	b.MagicBarrier( { 0, h + 0.08f, ( front + back ) * 0.5f }, { 3.0f, 0.08f, ( back - front ) * 0.5f } );
	for ( int s = -1; s <= 1; s += 2 )
	{
		Vector3 feet{ s * 1.5f, 0, 36.4f };
		Entity* king = b.King( feet, s < 0 ? kTeal : kCrimson );
		float z = 33.3f;
		Entity* orb = b.MagicOrb( { feet.x * z / feet.z, 0.5f, z } );
		b.AimHint( king, orb, { 0, 0.1f, -0.25f } );
	}
	// a pane of glass slides to and fro in front of the orbs: strike one while it is clear
	Entity* pane = b.GlassPane( { -3.2f, 1.1f, 31.2f }, { 1.3f, 1.1f, 0.06f }, 0.0f, true );
	b.Mover( pane, { 1, 0, 0 }, 6.4f, 2.6f, 1.2f );

	// outside: a carpet goes back and forth along the right side
	CarpetKing( b, { 6.2f, 2.6f, 31.5f }, { 6.2f, 2.6f, 38.5f }, 4.0f, 1.0f, 0.0f, kCarpetRed, kGold );
	b.Trees( { 0, 0, 34.5f }, 9.5f, 4, 7.6f );
	b.Flag( { -5.5f, 0, 37.5f }, kGold );
	b.Fortress( { 1.0f, 2.0f, 35.0f }, 11.0f );
}

// A king in the oasis: a big cactus in front of him, and behind him a palm leaning forward over his head
// against lobs. Only the chain shot cuts the cactus down, and it falls on him.
static void OasisKing( Builder& b, Vector3 feet, Color robe )
{
	Vector3 toCannon = Vector3Normalize( { -feet.x, 0.0f, -feet.z } );
	float yaw = atan2f( toCannon.x, toCannon.z );
	auto ahead = [&]( float d ) { return Vector3Add( feet, Vector3Scale( toCannon, d ) ); };
	const Biome& look = b.Look();
	Entity* king = b.King( feet, robe );
	// the palm leans along its own x axis: turn it so that points at the cannon
	b.Tree( ahead( -1.1f ), 1.3f, TreeKind::Palm, look.leafB, atan2f( -toCannon.z, toCannon.x ) );
	Entity* cactus = b.Tree( ahead( 2.1f ), 1.8f, TreeKind::Cactus, ColorMix( look.leafA, look.leafB, 0.4f ), yaw );
	if ( cactus )
	{
		b.AimHint( king, cactus, { 0, 2.0f, 0 } );
	}
}

static void LevelOasi( Builder& b )
{
	b.PlayerIsland();
	b.homeY = 0.0f;
	b.Island( { 0, 0, 36 }, 10.0f );
	OasisKing( b, { -4.2f, 0, 37.5f }, kTeal );
	OasisKing( b, { 0.6f, 0, 40.0f }, kGold );
	OasisKing( b, { 5.0f, 0, 37.0f }, kCrimson );
	// the pool the oasis is named after
	b.Ledge( { 0.4f, 0.02f, 35.6f }, { 1.6f, 0.02f, 1.1f }, Mat::Ice, { 0, 0, 0, 1 }, Color{ 70, 150, 200, 255 } );
	b.Trees( { 0, 0, 36 }, 10.0f, 4, 8.3f );
	b.Flag( { 1.5f, 0, 43.0f }, kGold );
	b.Fortress( { 0.5f, 2, 38 }, 11.0f );
}

static void LevelTempesta( Builder& b )
{
	b.PlayerIsland();
	b.homeY = 0.0f;
	b.Island( { 0, 0, 36 }, 10.0f );
	// three dunes of sandstone, each king behind a heap of sandbags
	b.Ledge( { -4.5f, 0.75f, 37.0f }, { 1.7f, 0.75f, 1.7f }, Mat::Stone, { 0, 0, 0, 1 }, kSandstone );
	b.Ledge( { 0.0f, 1.25f, 39.5f }, { 1.9f, 1.25f, 1.9f }, Mat::Stone, { 0, 0, 0, 1 }, kSandstone );
	b.Ledge( { 4.5f, 0.5f, 36.5f }, { 1.6f, 0.5f, 1.6f }, Mat::Stone, { 0, 0, 0, 1 }, kSandstone );
	size_t from = b.scene.entities.size();
	b.homeY = 1.5f;
	float t1 = b.Tower( { -4.5f, 1.5f, 37.4f }, 1, 0.9f, 1.2f, Mat::Stone, Mat::Wood );
	b.King( { -4.5f, t1, 37.4f }, kTeal );
	b.SandbagWall( { -6.0f, 1.5f, 35.8f }, true, 3, 2 );
	b.homeY = 2.5f;
	b.King( { 0.0f, 2.5f, 40.0f }, kPurple );
	b.SandbagWall( { -1.5f, 2.5f, 38.4f }, true, 3, 3 );
	b.homeY = 1.0f;
	float t3 = b.Tower( { 4.5f, 1.0f, 36.9f }, 2, 0.9f, 1.2f, Mat::Stone, Mat::Wood );
	b.King( { 4.5f, t3, 36.9f }, kCrimson );
	b.SandbagWall( { 3.0f, 1.0f, 35.2f }, true, 3, 2 );
	Sandstone( b, from );
	b.homeY = 0.0f;
	// and one on a carpet, high over the dunes
	CarpetKing( b, { -6.0f, 6.0f, 41.5f }, { 6.0f, 6.0f, 41.5f }, 5.0f, 1.0f, 0.0f, kCarpetBlue, kGold );
	b.ShiftingWind( 2.0f );
	b.Trees( { 0, 0, 36 }, 10.0f, 4, 8.3f );
	b.Flag( { -1.0f, 0, 44.0f }, kGold );
	b.Fortress( { 0, 3, 38 }, 12.0f );
}

static void LevelPalazzoZaira( Builder& b )
{
	b.PlayerIsland();
	b.homeY = 0.0f;
	b.Island( { 0, 0, 37 }, 11.0f );

	// the front of the palace: three panes of glass with gaps between them, and the Sultana gliding behind
	for ( int i = -1; i <= 1; ++i )
	{
		b.GlassPane( { i * 4.0f, 1.6f, 33.0f }, { 1.25f, 1.6f, 0.06f } );
	}
	CarpetKing( b, { -4.0f, 0.4f, 35.0f }, { 4.0f, 0.4f, 35.0f }, 4.0f, 1.2f, 0.0f, kCarpetRed, kGold );

	// two guards on lifts behind a wall at the back
	b.Ledge( { 0, 1.6f, 38.6f }, { 8.2f, 1.6f, 0.3f }, Mat::Stone, { 0, 0, 0, 1 }, kSandstone );
	for ( int s = -1; s <= 1; s += 2 )
	{
		Entity* lift = b.Platform( { s * 6.4f, 0.15f, 39.8f }, { 0.8f, 0.15f, 0.8f }, Mat::Stone, kSandstone );
		b.Mover( lift, { 0, 1, 0 }, 3.4f, 2.0f, 1.6f, s < 0 ? 1.0f : 4.6f );
		b.King( Vector3Add( lift->pos, { 0, 0.15f, 0 } ), s < 0 ? kTeal : kPurple );
	}

	// a magic kiosk off to the left, clear of the glass: its orb waits on the line from the cannon
	{
		Vector3 feet{ -8.6f, 0, 36.0f };
		b.MagicBarrier( { -8.4f, 1.3f, 35.0f }, { 1.3f, 1.3f, 0.08f } );
		b.MagicBarrier( { -8.4f, 2.68f, 35.9f }, { 1.3f, 0.08f, 0.9f } );
		Entity* guard = b.King( feet, kCrimson );
		float z = 33.6f;
		Entity* orb = b.MagicOrb( { feet.x * z / feet.z, 0.5f, z } );
		b.AimHint( guard, orb, { 0, 0.1f, -0.25f } );
	}

	// mountains of sandbags on either side of the front
	b.SandbagWall( { 6.0f, 0, 32.2f }, true, 3, 3 );
	b.SandbagWall( { -2.8f, 0, 30.6f }, true, 2, 2 );
	// palms and cacti round the back, clear of every line of fire
	const Biome& look = b.Look();
	b.Tree( { -4.0f, 0, 45.0f }, 1.2f, TreeKind::Palm, look.leafB, 0.4f );
	b.Tree( { 4.5f, 0, 45.0f }, 1.1f, TreeKind::Palm, look.leafA, 2.2f );
	b.Tree( { 9.5f, 0, 36.0f }, 1.0f, TreeKind::Cactus, look.leafA, 1.0f );
	b.Tree( { -9.8f, 0, 41.0f }, 0.9f, TreeKind::Cactus, look.leafB, 2.0f );
	b.Flag( { 0, 0, 44.0f }, kGold );
	b.Flag( { 7.5f, 0, 41.0f }, kGold );
	b.Fortress( { 0, 2, 37 }, 13.0f );
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
	// already swinging from side to side in front of the towers: strike it at the right moment
	Entity* ball = b.Pendulum( { 0, 8.0f, 30.5f }, 5.6f, 0.75f );
	b3Body_SetLinearVelocity( ball->body, { 3.5f, 0, 0 } );
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

	// the powder store: a casemate of solid masonry with a roof and a single narrow gunport,
	// lined up with the top row of the powder kegs. Nothing but a clean shot through the slit gets in.
	const Color masonry{ 128, 124, 132, 255 };
	const float front = 30.5f, back = 33.8f, roof = 3.2f;
	const float mid = ( front + back ) * 0.5f, halfDepth = ( back - front ) * 0.5f;
	b.Ledge( { 0, 0.3f, front }, { 4.2f, 0.3f, 0.3f }, Mat::Stone, { 0, 0, 0, 1 }, masonry );
	b.Ledge( { 0, ( 1.5f + roof ) * 0.5f, front }, { 4.2f, ( roof - 1.5f ) * 0.5f, 0.3f }, Mat::Stone, { 0, 0, 0, 1 }, masonry );
	for ( int s = -1; s <= 1; s += 2 )
	{
		b.Ledge( { 2.6f * s, 1.05f, front }, { 1.6f, 0.45f, 0.3f }, Mat::Stone, { 0, 0, 0, 1 }, masonry );
		b.Ledge( { 4.5f * s, roof * 0.5f, mid }, { 0.3f, roof * 0.5f, halfDepth + 0.3f }, Mat::Stone, { 0, 0, 0, 1 }, masonry );
	}
	b.Ledge( { 0, roof * 0.5f, back }, { 4.2f, roof * 0.5f, 0.3f }, Mat::Stone, { 0, 0, 0, 1 }, masonry );
	b.Ledge( { 0, roof + 0.15f, mid }, { 4.8f, 0.15f, halfDepth + 0.3f }, Mat::Stone, { 0, 0, 0, 1 }, masonry );

	// the kegs sit right behind the gunport, the kings on a plank above them; one more keg under each side stand
	const float kz = front + 1.1f;
	for ( int s = -1; s <= 1; s += 2 )
	{
		b.Tnt( { 3.1f * s, 0.35f, mid } );
		float t = b.Tower( { 3.1f * s, 0, mid }, 1, 0.95f, 1.3f, Mat::Stone, Mat::Wood );
		b.King( { 3.1f * s, t, mid }, s < 0 ? kGreen : kOrange );
	}
	b.Tnt( { -0.75f, 0.35f, kz } );
	b.Tnt( { 0.0f, 0.35f, kz } );
	b.Tnt( { 0.75f, 0.35f, kz } );
	Entity* keg = b.Tnt( { -0.4f, 1.05f, kz } );
	b.Tnt( { 0.4f, 1.05f, kz } );
	b.Box( { 0, 1.52f, kz }, { 1.3f, 0.12f, 0.6f }, Mat::Wood );
	b.King( { -0.6f, 1.64f, kz }, kCrimson );
	b.King( { 0.6f, 1.64f, kz }, kPurple );
	for ( Entity* e : b.scene.entities )
	{
		if ( e->kind == Kind::King )
		{
			b.AimHint( e, keg );
		}
	}

	b.Trees( { 0, 0, 34 }, 9.0f, 4, 6.8f );
	b.Flag( { 0.0f, 0, 38.5f }, kCrimson );
	b.Fortress( { 0, 2, 34 }, 11.0f );
}

// Regina Ottavia's keep: solid masonry with an iron-banded gate that only the boulder breaks, and the
// queen on a wooden stand inside. Returns the top of the roof.
static float QueenKeep( Builder& b, float x, float y0, float gate )
{
	const Color masonry{ 128, 124, 132, 255 };
	const float top = y0 + 3.8f, back = gate + 3.3f;
	for ( int s = -1; s <= 1; s += 2 )
	{
		b.Ledge( { x + 1.65f * s, ( y0 + top ) * 0.5f, ( gate + back ) * 0.5f }, { 0.3f, ( top - y0 ) * 0.5f, ( back - gate ) * 0.5f + 0.25f },
				 Mat::Stone, { 0, 0, 0, 1 }, masonry );
	}
	b.Ledge( { x, ( y0 + top ) * 0.5f, back }, { 1.35f, ( top - y0 ) * 0.5f, 0.25f }, Mat::Stone, { 0, 0, 0, 1 }, masonry );
	b.Ledge( { x, top + 0.15f, ( gate + back ) * 0.5f }, { 1.95f, 0.15f, ( back - gate ) * 0.5f + 0.25f }, Mat::Stone, { 0, 0, 0, 1 },
			 masonry );
	Entity* door = b.Reinforced( { x, ( y0 + top ) * 0.5f, gate }, { 1.35f, ( top - y0 ) * 0.5f, 0.25f } );
	// a single storey: the queen, crown and all, has to stand clear of the roof
	float t = b.Tower( { x, y0, gate + 1.7f }, 1, 0.9f, 1.2f, Mat::Wood, Mat::Wood );
	Entity* queen = b.King( { x, t, gate + 1.7f }, kOrange ); // Regina Ottavia's own colour
	// through the gate at the queen's own height: a hit on her stand only pins her against the back wall
	b.AimHint( queen, door, { 0, t + 0.7f - door->pos.y, 0 } );
	return top + 0.3f;
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
	b.King( { 3.0f, t2, 37.5f }, kGreen );
	// centre: the queen's keep; the boulder still has to find the gap between the sliding walls
	QueenKeep( b, 0.0f, 2.0f, 38.9f );
	b.Trees( { 0, 2, 37 }, 9.0f, 4, 6.5f );
	b.Flag( { -6.0f, 2, 40.0f }, kPurple );
	b.Fortress( { 0, 5, 37 }, 12.0f );
	b.ShiftingWind( 1.6f );
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
	float k = b.Tower( { 0, 0, 39.0f }, 4, 1.4f, 1.3f, Mat::Stone, Mat::Wood );
	b.King( { 0, k, 39.0f }, kPurple );

	// ice tower and a wooden hall
	float it = b.Tower( { -5.0f, 0, 41.0f }, 3, 0.9f, 1.2f, Mat::Ice, Mat::Ice );
	b.King( { -5.0f, it, 41.0f }, kTeal );
	b.King( { 5.0f, 0, 41.0f }, kCrimson );
	b.Hut( { 5.0f, 0, 41.0f }, 1.2f, 1.6f, Mat::Wood, Mat::Wood );

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
	b.ShiftingWind( 1.6f );
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

	// centre: a courtyard of solid masonry on three sides with a roof over the king. The only way in is the
	// gap between the roof and the rubber wall behind him, which leans back: a steep lob dropped in there
	// comes off it almost flat and flies back under the roof
	const Color masonry{ 128, 124, 132, 255 };
	const float h = 3.2f, eave = 34.9f, foot = 35.9f, lean = 0.35f;
	b.Ledge( { 0, h * 0.5f, 31.5f }, { 2.5f, h * 0.5f, 0.25f }, Mat::Stone, { 0, 0, 0, 1 }, masonry );
	for ( int s = -1; s <= 1; s += 2 )
	{
		b.Ledge( { 2.25f * s, h * 0.5f, ( 31.75f + foot ) * 0.5f }, { 0.25f, h * 0.5f, ( foot - 31.75f ) * 0.5f }, Mat::Stone, { 0, 0, 0, 1 },
				 masonry );
	}
	b.Ledge( { 0, h + 0.12f, ( 31.25f + eave ) * 0.5f }, { 2.5f, 0.12f, ( eave - 31.25f ) * 0.5f }, Mat::Stone, { 0, 0, 0, 1 }, masonry );
	// on a plinth, right in the path of the shots coming back off the rubber
	b.Ledge( { 0, 0.45f, 34.4f }, { 0.45f, 0.45f, 0.45f }, Mat::Stone, { 0, 0, 0, 1 }, masonry );
	Entity* sheltered = b.King( { 0, 0.9f, 34.4f }, kPurple );
	// the slab's lower front edge sits on the ground at `foot`
	Vector3 half{ 2.6f, 2.4f, 0.2f };
	Vector3 edge = Vector3RotateByQuaternion( { 0, -half.y, -half.z }, QuaternionFromAxisAngle( { 1, 0, 0 }, lean ) );
	Entity* rubber = b.Ledge( { 0, -edge.y - 0.08f, foot - edge.z }, half, Mat::Rubber, QuaternionFromAxisAngle( { 1, 0, 0 }, lean ) );
	b.AimHint( sheltered, rubber, { 0, 2.3f - rubber->pos.y, foot + tanf( lean ) * 2.3f - rubber->pos.z }, 10.0f );

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
	StoneHouse( b, 0.0f, 0.1f, fz, 1.5f );
	Entity* king = b.King( { 0, 0.1f, fz + 1.2f }, kTeal );
	// two stones in a row on the line to the gap: strike the back one and the front one slides on,
	// the back one stays as a spare
	b.CurlingStone( { 0, 0.1f, 30.8f } );
	Entity* back = b.CurlingStone( { 0, 0.1f, 29.0f } );
	b.AimHint( king, back, { 0, 0.2f, -0.3f } );

	// outside, one on each side: kings on stone columns behind low walls of ice bricks
	for ( int s = -1; s <= 1; s += 2 )
	{
		float c = b.Column( { 6.2f * s, 0, 35.5f }, 2, 0.5f, Mat::Stone );
		b.Box( { 6.2f * s, c + 0.12f, 35.5f }, { 0.8f, 0.12f, 0.8f }, Mat::Stone );
		b.King( { 6.2f * s, c + 0.24f, 35.5f }, s < 0 ? kBlue : kPurple );
		b.Wall( { 6.2f * s - 1.5f, 0, 33.6f }, true, 3, 3, Mat::Ice );
	}

	b.Trees( { 0, 0, 34 }, 9.5f, 3, 7.5f );
	b.Flag( { -4.5f, 0, 38.0f }, kTeal );
	b.Fortress( { 1.5f, 1.5f, 35.0f }, 11.0f );
}

// Valle dei Mulini: two windmills turning opposite ways, a king behind each and one on a tower in the gap
// between the sails. The boulder can snap one set of sails off.
static void LevelDueMulini( Builder& b )
{
	b.PlayerIsland();
	b.homeY = 0.0f;
	b.Island( { 0, 0, 35.5f }, 10.0f );
	for ( int s = -1; s <= 1; s += 2 )
	{
		b.Windmill( { 3.8f * s, 0, 31.0f }, 5.5f, 3.0f, 0.8f * s );
		// off to the side of the mill's tower, but still behind its sails
		float c = b.Column( { 5.4f * s, 0, 37.0f }, 2, 0.5f, Mat::Stone );
		b.Box( { 5.4f * s, c + 0.12f, 37.0f }, { 0.8f, 0.12f, 0.8f }, Mat::Stone );
		b.King( { 5.4f * s, c + 0.24f, 37.0f }, s < 0 ? kOrange : kTeal );
	}
	float t = b.Tower( { 0, 0, 38.5f }, 3, 0.9f, 1.2f, Mat::Wood, Mat::Wood );
	b.King( { 0, t, 38.5f }, kPurple );

	b.Trees( { 0, 0, 35.5f }, 10.0f, 4, 8.0f );
	b.Flag( { -7.0f, 0, 39.0f }, kOrange );
	b.Fortress( { 0, 3.5f, 35.0f }, 12.0f );
}

// Valle dei Mulini: billiards with a magic orb. The king sits in a lattice pavilion off to the left; a block of
// masonry hides the straight line to him, so the orb has to go on, off the rubber panel ahead, and round.
static void LevelSpondaMagica( Builder& b )
{
	b.PlayerIsland();
	b.homeY = 0.0f;
	b.Island( { 0, 0, 34 }, 9.5f );

	const Vector3 feet{ -3.4f, 0, 35.3f };
	const float h = 2.6f;
	// the pavilion: front, the side facing the panel, and a roof
	b.MagicBarrier( { feet.x, h * 0.5f, feet.z - 1.3f }, { 1.3f, h * 0.5f, 0.08f } );
	b.MagicBarrier( { feet.x + 1.3f, h * 0.5f, feet.z }, { 0.08f, h * 0.5f, 1.3f } );
	b.MagicBarrier( { feet.x, h + 0.08f, feet.z }, { 1.4f, 0.08f, 1.4f } );
	Entity* king = b.King( feet, kCrimson );

	Entity* orb = b.MagicOrb( { 0, 0.5f, 31.0f } );
	b.AimHint( king, orb, { 0, 0.1f, -0.25f } );
	// the panel, turned 45 degrees to send the orb off to the left. The orb meets it before its centre line,
	// so it stands a little further back than the king: the orb then runs straight at him
	b.Bumper( { 0, 1.0f, feet.z + 1.2f }, { 1.2f, 1.0f, 0.15f }, PI * 0.25f );
	// masonry on the diagonal between the orb and the king
	b.Ledge( { -2.0f, 0.6f, 32.9f }, { 0.8f, 0.6f, 0.8f }, Mat::Stone, { 0, 0, 0, 1 }, Color{ 128, 124, 132, 255 } );

	// right: a king on a stone column behind sandbags that swallow flat shots
	b.SandbagWall( { 3.2f, 0, 33.2f }, true, 3, 4 );
	float c = b.Column( { 4.2f, 0, 35.2f }, 2, 0.5f, Mat::Stone );
	b.Box( { 4.2f, c + 0.12f, 35.2f }, { 0.8f, 0.12f, 0.8f }, Mat::Stone );
	b.King( { 4.2f, c + 0.24f, 35.2f }, kTeal );

	b.Trees( { 0, 0, 34 }, 9.5f, 3, 7.8f );
	b.Flag( { 6.5f, 0, 38.0f }, kOrange );
	b.Fortress( { 0, 1.5f, 34.5f }, 11.0f );
}

// Valle dei Mulini, the finale: Regina Ottavia's palace. Everything the valley has taught at once: the gate
// behind a sliding wall for the boulder, a king in a lattice pavilion with his orb, a king behind turning
// sails, a guard on the roof of the keep, and a wind that never blows the same way twice.
static void LevelPalazzoOttavia( Builder& b )
{
	b.PlayerIsland();
	b.homeY = 0.0f;
	b.Island( { 0, 0, 36 }, 12.0f );

	// centre: the keep, and in front of it a stone wall sliding back and forth across the gate
	float roof = QueenKeep( b, 0.0f, 0.0f, 38.0f );
	b.Slider( { 0, 1.7f, 33.0f }, { 1.6f, 1.6f, 0.2f }, { 1, 0, 0 }, 3.2f, 0.7f, 0.0f, Mat::Stone );
	b.King( { 0.9f, roof, 40.4f }, kPurple );

	// left: the lattice pavilion with its orb
	const Vector3 feet{ -6.2f, 0, 38.0f };
	const float h = 2.8f;
	b.MagicBarrier( { feet.x, h * 0.5f, feet.z - 1.4f }, { 1.5f, h * 0.5f, 0.08f } );
	b.MagicBarrier( { feet.x, h + 0.08f, feet.z }, { 1.6f, 0.08f, 1.5f } );
	Entity* seer = b.King( feet, kCrimson );
	float oz = 34.4f;
	Entity* orb = b.MagicOrb( { feet.x * oz / feet.z, 0.5f, oz } );
	b.AimHint( seer, orb, { 0, 0.1f, -0.25f } );

	// right: a king behind the sails of a mill
	b.Windmill( { 6.0f, 0, 31.5f }, 5.5f, 3.0f, -0.9f );
	float c = b.Column( { 7.6f, 0, 37.5f }, 2, 0.5f, Mat::Stone );
	b.Box( { 7.6f, c + 0.12f, 37.5f }, { 0.8f, 0.12f, 0.8f }, Mat::Stone );
	b.King( { 7.6f, c + 0.24f, 37.5f }, kTeal );

	b.Trees( { 0, 0, 36 }, 12.0f, 4, 10.0f );
	b.Flag( { -3.0f, roof, 41.0f }, kPurple );
	b.Flag( { 3.0f, roof, 41.0f }, kPurple );
	b.Fortress( { 0, 3.5f, 37.0f }, 14.0f );
	b.ShiftingWind( 1.2f );
}

// Valle dei Mulini: the granary. Brick walls on the flanks give way to cannonballs; the iron-banded wall in the
// middle only to the boulder, and the king stands too close behind it for a lob.
static void LevelGranaio( Builder& b )
{
	b.PlayerIsland();
	b.homeY = 0.0f;
	b.Island( { 0, 0, 35 }, 9.5f );

	Entity* wall = b.Reinforced( { 0, 2.25f, 34.0f }, { 1.6f, 2.25f, 0.3f } );
	float t = b.Tower( { 0, 0, 35.6f }, 1, 0.9f, 1.3f, Mat::Wood, Mat::Wood );
	Entity* miller = b.King( { 0, t, 35.6f }, kOrange );
	b.AimHint( miller, wall, { 0, -0.8f, 0 } );
	// a roof over the king, from the top of the wall back
	b.Ledge( { 0, 4.62f, 35.4f }, { 1.9f, 0.12f, 1.6f }, Mat::Wood, { 0, 0, 0, 1 }, Color{ 150, 100, 55, 255 } );

	for ( int s = -1; s <= 1; s += 2 )
	{
		b.Wall( { 4.4f * s - 1.5f, 0, 33.4f }, true, 3, 3, Mat::Stone );
		float tt = b.Tower( { 4.4f * s, 0, 35.8f }, 2, 0.9f, 1.2f, Mat::Wood, Mat::Wood );
		b.King( { 4.4f * s, tt, 35.8f }, s < 0 ? kPurple : kGreen );
	}

	b.Trees( { 0, 0, 35 }, 9.5f, 4, 7.6f );
	b.Flag( { 0.0f, 0, 38.5f }, kOrange );
	b.Fortress( { 0, 2.0f, 35.0f }, 11.0f );
	b.ShiftingWind( 1.2f );
}

// Valle dei Mulini: two kings in a pavilion of magic lattice. Cannonballs bounce off it, but the magic orbs
// lying in front roll straight through: strike an orb from behind and send it into its king.
static void LevelSfere( Builder& b )
{
	b.PlayerIsland();
	b.homeY = 0.0f;
	b.Island( { 0, 0, 34.5f }, 9.5f );

	const float front = 34.8f, back = 38.0f, h = 3.2f;
	b.MagicBarrier( { 0, h * 0.5f, front }, { 3.0f, h * 0.5f, 0.08f } );
	b.MagicBarrier( { 0, h + 0.08f, ( front + back ) * 0.5f }, { 3.0f, 0.08f, ( back - front ) * 0.5f } );
	for ( int s = -1; s <= 1; s += 2 )
	{
		Vector3 feet{ s * 1.5f, 0, 36.4f };
		Entity* king = b.King( feet, s < 0 ? kOrange : kCrimson );
		// each orb lies on the line from the cannon to its king, close to the barrier: a first taste, so a shot
		// a little off line still gets there
		float z = 33.3f;
		Entity* orb = b.MagicOrb( { feet.x * z / feet.z, 0.5f, z } );
		b.AimHint( king, orb, { 0, 0.1f, -0.25f } );
	}

	// outside: a wooden tower behind a wall of sandbags
	b.SandbagWall( { 4.8f, 0, 33.0f }, true, 3, 2 );
	float t = b.Tower( { 6.0f, 0, 35.5f }, 2, 0.9f, 1.2f, Mat::Wood, Mat::Wood );
	b.King( { 6.0f, t, 35.5f }, kPurple );

	b.Trees( { 0, 0, 34.5f }, 9.5f, 4, 7.6f );
	b.Flag( { -5.5f, 0, 37.5f }, kOrange );
	b.Fortress( { 1.0f, 2.0f, 35.0f }, 11.0f );
}

// The two-king versions of Curling. With rails, each stone has its own lane and a stone knocked off
// line is turned back towards its king; without, both stones have to be struck just right.
static void CurlingForTwo( Builder& b, bool rails )
{
	b.PlayerIsland();
	b.homeY = 0.0f;
	b.Island( { 0, 0, 34 }, 9.5f );
	b.Ledge( { 0, 0.05f, 33.5f }, { 2.6f, 0.05f, 6.5f }, Mat::Ice );
	const float fz = 36.6f, laneStart = 31.4f, laneEnd = fz - 0.2f;
	StoneHouse( b, 0.0f, 0.1f, fz, 2.5f );
	const Color boards{ 150, 105, 60, 255 };
	float mid = ( laneStart + laneEnd ) * 0.5f, len = ( laneEnd - laneStart ) * 0.5f;
	if ( rails )
	{
		b.Ledge( { 0, 0.3f, mid }, { 0.06f, 0.2f, len }, Mat::Wood, { 0, 0, 0, 1 }, boards );
	}
	for ( int s = -1; s <= 1; s += 2 )
	{
		Entity* king = b.King( { s * 1.0f, 0.1f, fz + 1.2f }, s < 0 ? kTeal : kBlue );
		// each stone sits on the line from the cannon to its king
		Entity* stone = b.CurlingStone( { s * 0.8f, 0.1f, 30.5f } );
		b.AimHint( king, stone, { 0, 0.2f, -0.3f } );
		// the outer rail narrows the lane from 2.3 m to 1.8 m as it nears the house
		if ( rails )
		{
			float yaw = atan2f( s * 0.5f, 2.0f * len );
			b.Ledge( { s * 2.05f, 0.3f, mid }, { 0.06f, 0.2f, len }, Mat::Wood, QuaternionFromAxisAngle( { 0, 1, 0 }, yaw ), boards );
		}
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

static void LevelCurlingDoppio( Builder& b )
{
	CurlingForTwo( b, true );
}

static void LevelCurlingCampioni( Builder& b )
{
	CurlingForTwo( b, false );
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
// A ramp of snow held back by an ice dam above three kings. With `real` snowballs the dam is the way
// to win; otherwise the "snowballs" are powder puffs and shooting the dam wastes a shot.
static void Avalanche( Builder& b, bool real )
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
		if ( real )
		{
			b.Snowball( { i * 1.6f, y, z }, r );
		}
		else
		{
			b.Puffball( { i * 1.6f, y, z }, r );
		}
	}

	// stacks of solid crates in a row at the foot of the ramp, one in the path of each snowball: a snowball
	// cannot slip between their legs the way it would through a tower, and none can topple the others
	const Vector3 towers[3] = { { -2.0f, 0, 35.6f }, { 0.0f, 0, 35.6f }, { 2.0f, 0, 35.6f } };
	const Color robes[3] = { kTeal, kBlue, kPurple };
	for ( int i = 0; i < 3; ++i )
	{
		float t = b.Column( towers[i], i == 2 ? 3 : 4, 0.4f, Mat::Wood );
		Entity* king = b.King( { towers[i].x, t, towers[i].z }, robes[i] );
		if ( real )
		{
			b.AimHint( king, gate, { 0, 0.3f, 0 } );
		}
	}
	if ( real == false )
	{
		// a bank of snow at the foot of the ramp stops a cannonball rolling back down from the dam:
		// a shot wasted on the dam must stay wasted
		b.Ledge( { 0, 0.2f, low.z - 0.3f }, { 2.4f, 0.2f, 0.3f }, Mat::Rock, { 0, 0, 0, 1 }, kSnow );
	}
	b.Trees( { 0, 0, 35 }, 9.5f, 4, 7.0f );
	b.Flag( { 4.0f, 0, 38.5f }, kTeal );
	b.Fortress( { 0, 1.5f, 36.0f }, 11.0f );
}

static void LevelValanga( Builder& b )
{
	Avalanche( b, true );
}

static void LevelNeveFresca( Builder& b )
{
	Avalanche( b, false );
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
	{ "Il Ponte", "Il mio ponte regge un re. Anche due, se stanno fermi.", "La PALLA INCATENATA (tasto 4) spazza tutto: spezza le corde e taglia gli alberi!",
	  { 3, 0, 0, 2, 0 }, 3, { 0, 0, 0 }, Level03, "prati_ponte" },
	{ "Palazzo di Ghiaccio", "Le mie torri di ghiaccio non si sciolgono, figuriamoci sotto le tue palle di ferro.", "Il ghiaccio si frantuma. Il GRAPPOLO (tasto 3) si divide con SPAZIO.",
	  { 3, 0, 2, 0, 0 }, 3, { 0, 0, 0 }, Level04, "gelo_palazzo" },
	{ "Mongolfiere", "Da quassù i tuoi cannoni sembrano giocattoli.", "Buca i palloni e i cesti precipiteranno tra le nuvole.",
	  { 3, 0, 2, 0, 0 }, 3, { 0.8f, 0, 0 }, Level05, "prati_mongolfiere" },
	{ "Il Mulino", "Le mie pale girano da cent'anni. Non si fermeranno per te.",
	  "Aspetta il momento giusto per passare fra le pale, oppure spezzale con il MACIGNO (5).",
	  { 4, 2, 0, 0, 1 }, 3, { 0, 0, 0 }, Level06, "mulini_mulino" },
	{ "Il Pendolo", "Tic, tac. Il pendolo decide chi resta in piedi.", "Il pendolo oscilla: colpiscilo quando passa davanti alle torri e lascia fare alla fisica. Occhio al vento!",
	  { 3, 1, 0, 0, 0 }, 2, { -2.2f, 0, 0 }, Level07, "mulini_pendolo" },
	{ "Polveriera", "La polvere da sparo è ben custodita: proprio sotto di noi.",
	  "La casamatta non si scalfisce, ma la feritoia guarda dritta sul TNT. Centrala: le esplosioni si propagano...",
	  { 4, 1, 0, 0, 0 }, 2, { 0, 0, 0 }, Level08, "prati_polveriera" },
	{ "Scudi Mobili", "Muri che vanno e vengono. Come le tue speranze.",
	  "Gli scudi scorrono e il vento cambia a ogni colpo. Il portone cerchiato di ferro lo sfonda solo il MACIGNO (5).",
	  { 4, 0, 0, 1, 2 }, 6, { 1.4f, 0, 0.4f }, Level09, "mulini_scudi" },
	{ "La Cittadella", "Hai buttato giù i miei cugini. Ma me, non mi prendi.", "Sei re, tre isole, e il vento cambia a ogni colpo: guarda la freccia prima di sparare.", { 4, 3, 2, 2, 2 }, 7, { -1.0f, 0, 0 },
	  Level10, "prati_cittadella" },
	{ "Cristalli Guardiani", "Il cristallo protegge. Il cristallo aspetta. Il cristallo non sbaglia.",
	  "Gli scudi di cristallo si spengono a intervalli: guarda l'anello sopra ogni scudo e spara al momento giusto.",
	  { 3, 0, 0, 0, 0 }, 2, { 0, 0, 0 }, Level11, "gelo_cristalli" },
	{ "Doppia Guardia", "Due guardie di cristallo sono meglio di una.", "Due scudi in fila: si passa solo quando sono spenti entrambi. Tieni conto del volo.",
	  { 4, 2, 0, 0, 1 }, 3, { 0.8f, 0, 0 }, Level12, "gelo_doppia" },
	{ "Sponde di Gomma", "Qui tutto rimbalza, perfino le tue minacce.",
	  "Il re al centro sta sotto un tetto: tira oltre il tetto, sul muro di gomma, e il rimbalzo lo colpirà. I sacchi assorbono urti ed esplosioni.",
	  { 5, 1, 0, 0, 0, 0, 0 }, 3, { 0, 0, 0 }, Level13, "prati_gomma" },
	{ "Il Bunker", "Sacchi di sabbia. Tanti, tanti sacchi di sabbia.", "Novità: il VORTICE (6) risucchia i blocchi, la bomba ADESIVA (7) si attacca ed esplode dopo 3 s.",
	  { 3, 1, 0, 0, 0, 2, 2 }, 3, { 0.6f, 0, 0 }, Level14, "prati_bunker" },
	{ "Crepacci", "Il ghiaccio regge. Quasi sempre.", "I ponti di ghiaccio si frantumano: colpiscili e i re cadranno tra le nuvole.",
	  { 4, 0, 1, 0, 0, 0, 0 }, 3, { 0, 0, 0 }, LevelCrepacci, "gelo_crepacci" },
	{ "Curling", "Le mie pietre scivolano. I miei re, no.",
	  "Un re si nasconde nella casa di pietra: colpisci da dietro la pietra da curling in fondo, la spinta passa all'altra che scivola sotto il muro.",
	  { 4, 0, 0, 0, 0, 0, 0 }, 3, { 0, 0, 0 }, LevelCurling, "gelo_curling" },
	{ "Stalattiti", "Sotto il mio tetto nessuno ci tocca. Nemmeno il cielo.",
	  "I tetti di neve poggiano su colonne di ghiaccio: spezza una colonna davanti e il tetto crolla.",
	  { 3, 0, 0, 0, 0, 0, 0 }, 2, { 0, 0, 0 }, LevelStalattiti, "gelo_stalattiti" },
	{ "Valanga", "La neve lassù è ferma da secoli. Non svegliarla.", "Colpisci la diga di ghiaccio sulla rampa: le palle di neve faranno il resto.",
	  { 2, 0, 0, 0, 0, 0, 0 }, 1, { 0, 0, 0 }, LevelValanga, "gelo_valanga" },
	{ "La Reggia di Ghiacciolo", "Benvenuto nella mia reggia. Resterai congelato all'ingresso.",
	  "Cristallo, neve e pietre da curling: tutto quello che hai imparato sul ghiaccio ti servirà.",
	  { 5, 2, 1, 0, 0, 0, 0 }, 4, { 0, 0, 0 }, LevelReggia, "gelo_reggia" },
	{ "Doppio Curling", "Due pietre, due re. Ti tremerà la mano.",
	  "Due re nella casa di pietra: colpisci da dietro ogni pietra da curling. Le sponde di legno la riportano verso il suo re.",
	  { 5, 0, 0, 0, 0, 0, 0 }, 3, { 0, 0, 0 }, LevelCurlingDoppio, "gelo_curling_doppio" },
	{ "Curling dei Campioni", "Niente sponde, niente aiuti. Solo tu, due pietre e il ghiaccio.",
	  "Due re nella casa di pietra e nessuna sponda: ogni pietra va colpita da dietro, dritta sul suo re.",
	  { 4, 0, 0, 0, 0, 0, 0 }, 3, { 0, 0, 0 }, LevelCurlingCampioni, "gelo_curling_campioni" },
	{ "Neve Fresca", "Anche questa neve è ferma da secoli. Più o meno.", "Prima di svegliare la neve, guardala bene.",
	  { 3, 0, 0, 0, 0, 0, 0 }, 3, { 0, 0, 0 }, LevelNeveFresca, "gelo_neve_fresca" },
	{ "Sfere Magiche", "Il mio padiglione è stregato: le tue palle di ferro non passano.",
	  "Le palle di cannone rimbalzano sulla barriera magica, le sfere magiche la attraversano: colpisci ogni sfera da dietro e mandala sul suo re.",
	  { 4, 0, 0, 0, 0, 0, 0 }, 3, { 0, 0, 0 }, LevelSfere, "mulini_sfere" },
	{ "Il Granaio", "Il mio grano è al sicuro dietro il ferro. E io con lui.",
	  "I muri di mattoni cedono alle palle; quello cerchiato di ferro solo al MACIGNO (5). Il vento cambia a ogni colpo.",
	  { 3, 0, 0, 0, 2, 0, 0 }, 3, { 0, 0, 0 }, LevelGranaio, "mulini_granaio" },
	{ "Due Mulini", "Due mulini, due venti, due volte il fastidio per te.",
	  "Le pale girano in versi opposti: passa nei varchi o scavalcale. Il MACIGNO (5) spezza le pale di un mulino.",
	  { 4, 0, 0, 0, 1, 0, 0 }, 3, { 0, 0, 0 }, LevelDueMulini, "mulini_due" },
	{ "Sponda Magica", "Il mio padiglione non si vede nemmeno da qui. Figurati colpirlo.",
	  "La sfera magica attraversa la barriera: mandala sul pannello di gomma, rimbalzerà verso il re.",
	  { 4, 0, 0, 0, 0, 0, 0 }, 2, { 0, 0, 0 }, LevelSpondaMagica, "mulini_sponda" },
	{ "Il Palazzo di Ottavia", "Mulini, magie, ferro e vento: il mio palazzo ha tutto. Tranne una porta per te.",
	  "La regina è dietro il portone di ferro: serve il MACIGNO (5), nel varco del muro che scorre. Il vento cambia a ogni colpo.",
	  { 4, 0, 0, 0, 2, 0, 0 }, 5, { 0.8f, 0, 0 }, LevelPalazzoOttavia, "mulini_palazzo" },
	{ "Il Boschetto", "Nel mio boschetto nessuno mi trova. Nemmeno le tue palle di ferro.",
	  "Le palle rimbalzano sui tronchi: solo la PALLA INCATENATA (4) taglia gli alberi. Premi TAB per trovare i re nascosti.",
	  { 2, 0, 0, 4, 0, 0, 0 }, 3, { 0, 0, 0 }, LevelBoschetto, "prati_boschetto" },
	{ "La Carovana", "Benvenuto sulle Dune. Qui niente sta fermo, men che meno io.",
	  "Il re sul tappeto volante si muove: mira dove sar\u00e0 quando arriva la palla, non dove \u00e8 adesso.",
	  { 5, 0, 0, 0, 0, 0, 0 }, 3, { 0, 0, 0 }, LevelCarovana, "dune_carovana" },
	{ "Vetrate", "Guardami pure. Toccarmi \u00e8 un'altra faccenda.",
	  "Il vetro cerchiato d'ottone non si rompe e non si spegne: passaci sopra, di pallonetto.",
	  { 5, 0, 0, 0, 0, 0, 0 }, 3, { 0, 0, 0 }, LevelVetrate, "dune_vetrate" },
	{ "Il Montacarichi", "Su e gi\u00f9, su e gi\u00f9. Prendimi, se ci riesci.",
	  "I re salgono e scendono dietro il muro: spara mentre salgono, la palla arriva quando sono in cima.",
	  { 5, 0, 0, 0, 0, 0, 0 }, 3, { 0, 0, 0 }, LevelMontacarichi, "dune_montacarichi" },
	{ "Tappeti in Volo", "I miei tappeti volano pi\u00f9 alti delle tue palle. E il vento soffia per me.",
	  "Tre re su tre tappeti. Uno passa dietro il vetro: aspetta che esca allo scoperto. Il vento cambia a ogni colpo.",
	  { 6, 0, 0, 0, 0, 0, 0 }, 3, { 0.6f, 0, 0 }, LevelTappeti, "dune_tappeti" },
	{ "Miraggio", "Vedi le mie sfere? Sono un miraggio. O forse no.",
	  "Le sfere magiche passano la barriera, ma davanti scorre una lastra di vetro: colpisci la sfera quando la lastra \u00e8 lontana.",
	  { 5, 0, 0, 0, 0, 0, 0 }, 3, { 0, 0, 0 }, LevelMiraggio, "dune_miraggio" },
	{ "L'Oasi", "All'ombra delle mie palme non mi trova nessuno.",
	  "I cactus fermano le palle e le palme riparano dall'alto: taglia i cactus con la PALLA INCATENATA (4), poi finisci il lavoro.",
	  { 3, 0, 0, 4, 0, 0, 0 }, 5, { 0, 0, 0 }, LevelOasi, "dune_oasi" },
	{ "Tempesta di Sabbia", "Senti il vento? Soffia sempre dalla mia parte.",
	  "Il vento cambia forte a ogni colpo: guarda la freccia. I sacchi inghiottono le palle, il MACIGNO (5) passa.",
	  { 6, 1, 0, 0, 1, 0, 0 }, 4, { -1.2f, 0, 0 }, LevelTempesta, "dune_tempesta" },
	{ "Il Palazzo di Zaira", "Vetro, magia e sabbia. Il mio palazzo \u00e8 un gioiello, e io la sua perla.",
	  "La Sultana passa dietro la facciata di vetro: spara nei varchi. Le guardie salgono e scendono, la sfera magica passa la barriera.",
	  { 7, 2, 0, 2, 1, 0, 0 }, 5, { 0, 0, 0 }, LevelPalazzoZaira, "dune_palazzo" },
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
				   { idx( "prati_primo_colpo" ), idx( "prati_mura" ), idx( "prati_ponte" ), idx( "prati_boschetto" ), idx( "prati_mongolfiere" ),
					 idx( "prati_polveriera" ), idx( "prati_gomma" ), idx( "prati_bunker" ), idx( "prati_cittadella" ) } } );
	c.push_back( { "Valle dei Mulini", "Regina Ottavia", kOrange, 1,
				   "Nella Valle dei Mulini il tramonto non finisce mai. La Regina Ottavia ha costruito difese che si muovono: "
				   "pale, pendoli, scudi su binari, e perfino barriere incantate. Qui non basta mirare bene: bisogna scegliere "
				   "il momento.",
				   "Le pale si fermano e il secondo frammento torna al suo posto. Pi\u00f9 in alto, dove l'aria si fa gelida, "
				   "qualcuno ha costruito un palazzo di ghiaccio.",
				   { idx( "mulini_mulino" ), idx( "mulini_pendolo" ), idx( "mulini_granaio" ), idx( "mulini_sfere" ),
					 idx( "mulini_due" ), idx( "mulini_scudi" ), idx( "mulini_sponda" ), idx( "mulini_palazzo" ) } } );
	c.push_back( { "Picchi Gelati", "Re Ghiacciolo III", kTeal, 2,
				   "Sui Picchi Gelati regna Re Ghiacciolo III, che non si fida di nessuno, e meno che mai dei muri normali: "
				   "i suoi sono di cristallo, e si accendono e si spengono quando vuole lui.",
				   "Il cristallo si spegne per sempre. Tre frammenti su sei: la Corona dei Venti ricomincia a soffiare.",
				   { idx( "gelo_palazzo" ), idx( "gelo_crepacci" ), idx( "gelo_curling" ), idx( "gelo_cristalli" ),
					 idx( "gelo_stalattiti" ), idx( "gelo_valanga" ), idx( "gelo_neve_fresca" ), idx( "gelo_curling_doppio" ), idx( "gelo_doppia" ),
					 idx( "gelo_curling_campioni" ), idx( "gelo_reggia" ) } } );
	c.push_back( { "Dune Sospese", "Sultana Zaira", { 225, 170, 40, 255 }, 3,
				   "Sulle Dune Sospese la sabbia vola pi\u00f9 in alto delle isole. La Sultana Zaira si nasconde dietro montagne "
				   "di sacchi, e il vento cambia a ogni colpo.",
				   "La tempesta di sabbia si posa. Quattro frammenti su sei.",
				   { idx( "dune_carovana" ), idx( "dune_vetrate" ), idx( "dune_montacarichi" ), idx( "dune_tappeti" ),
					 idx( "dune_miraggio" ), idx( "dune_oasi" ), idx( "dune_tempesta" ), idx( "dune_palazzo" ) } } );
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
