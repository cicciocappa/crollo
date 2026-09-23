#include "particles.h"

#include "render.h"
#include "rlgl.h"

#include <algorithm>

static const size_t kMaxParticles = 4000;

void Particles::Emit( const Particle& p )
{
	if ( m_items.size() >= kMaxParticles )
	{
		// overwrite the oldest-ish particle
		m_items[FxRng().Next() % m_items.size()] = p;
		return;
	}
	m_items.push_back( p );
}

void Particles::Update( float dt, Vector3 wind )
{
	for ( Particle& p : m_items )
	{
		p.life -= dt;
		p.vel.y -= p.gravity * dt;
		float k = expf( -p.drag * dt );
		if ( p.type == PType::Smoke )
		{
			// smoke drifts with the wind
			p.vel = Vector3Add( Vector3Scale( Vector3Subtract( p.vel, wind ), k ), wind );
		}
		else if ( p.type == PType::Flake )
		{
			// falls at its own terminal speed, carried by the wind, fluttering side to side
			Vector3 target = Vector3Add( Vector3Add( Vector3Scale( wind, 1.4f ), p.drift ), { 0, -p.gravity, 0 } );
			p.vel = Vector3Add( Vector3Scale( Vector3Subtract( p.vel, target ), k ), target );
			float flutter = sinf( p.life * p.spin.y + p.spin.x ) * p.spin.z;
			p.pos.x += flutter * dt;
			p.pos.z += cosf( p.life * p.spin.y * 0.7f + p.spin.x ) * p.spin.z * 0.6f * dt;
			p.vel.y += p.gravity * dt; // undo the generic gravity below: flakes drift at terminal speed
		}
		else
		{
			p.vel = Vector3Scale( p.vel, k );
		}
		p.pos = Vector3Add( p.pos, Vector3Scale( p.vel, dt ) );
		if ( p.type != PType::Flake ) // for weather, grow is the aspect ratio of the flake
		{
			p.size += p.grow * dt;
		}
		if ( p.type == PType::Chip )
		{
			float w = Vector3Length( p.spin );
			if ( w > 1e-4f )
			{
				Quaternion dq = QuaternionFromAxisAngle( Vector3Scale( p.spin, 1.0f / w ), w * dt );
				p.rot = QuaternionMultiply( dq, p.rot );
			}
		}
	}
	m_items.erase( std::remove_if( m_items.begin(), m_items.end(), []( const Particle& p ) { return p.life <= 0.0f || p.pos.y < -40.0f; } ),
				   m_items.end() );
}

void Particles::Draw( Renderer& renderer, const Camera3D& camera )
{
	// Chips are opaque cubes
	for ( const Particle& p : m_items )
	{
		if ( p.type == PType::Chip )
		{
			renderer.DrawShadedCube( p.pos, p.rot, p.size, p.color );
		}
	}
	rlDrawRenderBatchActive();

	Texture2D tex = renderer.SoftTexture();

	// rain: short lines along the velocity
	for ( const Particle& p : m_items )
	{
		if ( p.type == PType::Streak )
		{
			float t = 1.0f - p.life / p.maxLife;
			float alpha = std::min( 1.0f, t * 8.0f ) * std::min( 1.0f, p.life * 4.0f );
			DrawLine3D( p.pos, Vector3Subtract( p.pos, Vector3Scale( p.vel, p.size ) ), WithAlpha( p.color, alpha * ( p.color.a / 255.0f ) ) );
		}
	}
	rlDrawRenderBatchActive();

	// Alpha blended smoke and weather, sorted back to front
	std::vector<const Particle*> smoke;
	for ( const Particle& p : m_items )
	{
		if ( p.type == PType::Smoke || p.type == PType::Flake )
		{
			smoke.push_back( &p );
		}
	}
	std::sort( smoke.begin(), smoke.end(), [&]( const Particle* a, const Particle* b ) {
		return Vector3DistanceSqr( a->pos, camera.position ) > Vector3DistanceSqr( b->pos, camera.position );
	} );

	rlDisableDepthMask();
	for ( const Particle* p : smoke )
	{
		float t = 1.0f - p->life / p->maxLife;
		if ( p->type == PType::Flake )
		{
			// fade in and out at the ends of its life; leaves and grains are stretched and tumble
			// fade in and out at the ends of its life, and close to the lens where it would be a big blur
			float a = std::min( 1.0f, t * 6.0f ) * std::min( 1.0f, p->life * 1.5f ) * ( p->color.a / 255.0f );
			float d = Vector3Distance( p->pos, camera.position );
			a *= Clamp01( ( d - 2.5f ) / 4.0f );
			if ( a <= 0.01f )
			{
				continue;
			}
			float spin = p->life * p->spin.y * 40.0f + p->spin.x * 57.0f;
			Texture2D flake = renderer.FlakeTexture();
			Rectangle src{ 0, 0, (float)flake.width, (float)flake.height };
			Vector2 size{ p->size, p->size * p->grow };
			DrawBillboardPro( camera, flake, src, p->pos, { 0, 1, 0 }, size, { size.x * 0.5f, size.y * 0.5f }, spin, WithAlpha( p->color, a ) );
			continue;
		}
		float alpha = ( t < 0.1f ? t / 0.1f : 1.0f ) * ( 1.0f - t );
		DrawBillboard( camera, tex, p->pos, p->size, WithAlpha( p->color, alpha * ( p->color.a / 255.0f ) ) );
	}
	rlDrawRenderBatchActive();

	BeginBlendMode( BLEND_ADDITIVE );
	for ( const Particle& p : m_items )
	{
		if ( p.type == PType::Smoke || p.type == PType::Chip || p.type == PType::Flake || p.type == PType::Streak )
		{
			continue;
		}
		float t = 1.0f - p.life / p.maxLife;
		float alpha = ( 1.0f - t ) * ( p.color.a / 255.0f );
		if ( p.type == PType::Star )
		{
			alpha *= 0.6f + 0.4f * sinf( p.life * 30.0f );
		}
		DrawBillboard( camera, tex, p.pos, p.size, WithAlpha( p.color, alpha ) );
	}
	EndBlendMode();
	rlEnableDepthMask();
}

void Particles::Dust( Vector3 pos, Color color, int count, float speed )
{
	Rng& r = FxRng();
	for ( int i = 0; i < count; ++i )
	{
		Particle p{};
		p.type = PType::Smoke;
		p.pos = Vector3Add( pos, Vector3Scale( r.InSphere(), 0.2f ) );
		Vector3 v = r.OnSphere();
		v.y = fabsf( v.y ) * 0.6f + 0.2f;
		p.vel = Vector3Scale( v, speed * r.Range( 0.3f, 1.0f ) );
		p.color = ColorMix( color, Color{ 230, 225, 215, 255 }, 0.6f );
		p.color.a = 170;
		p.size = r.Range( 0.3f, 0.7f );
		p.grow = r.Range( 0.6f, 1.4f );
		p.maxLife = p.life = r.Range( 0.8f, 1.8f );
		p.drag = 2.5f;
		p.gravity = -0.3f;
		Emit( p );
	}
}

void Particles::Debris( Vector3 pos, Color color, int count, float speed, float size )
{
	Rng& r = FxRng();
	for ( int i = 0; i < count; ++i )
	{
		Particle p{};
		p.type = PType::Chip;
		p.pos = Vector3Add( pos, Vector3Scale( r.InSphere(), 0.15f ) );
		Vector3 v = r.OnSphere();
		v.y = fabsf( v.y ) + 0.3f;
		p.vel = Vector3Scale( v, speed * r.Range( 0.4f, 1.0f ) );
		p.rot = QuaternionFromAxisAngle( r.OnSphere(), r.Range( 0, 6.28f ) );
		p.spin = Vector3Scale( r.OnSphere(), r.Range( 4.0f, 16.0f ) );
		p.color = ColorBrightness( color, r.Range( -0.25f, 0.1f ) );
		p.size = size * r.Range( 0.5f, 1.2f );
		p.maxLife = p.life = r.Range( 1.5f, 3.0f );
		p.drag = 0.3f;
		p.gravity = 12.0f;
		Emit( p );
	}
}

void Particles::Explosion( Vector3 pos, float radius )
{
	Rng& r = FxRng();
	// fireball
	for ( int i = 0; i < 22; ++i )
	{
		Particle p{};
		p.type = PType::Fire;
		p.pos = Vector3Add( pos, Vector3Scale( r.InSphere(), radius * 0.2f ) );
		p.vel = Vector3Scale( r.OnSphere(), r.Range( 2.0f, 7.0f ) );
		p.color = ColorMix( Color{ 200, 150, 70, 150 }, Color{ 200, 60, 10, 150 }, r.Float() );
		p.size = r.Range( 0.8f, 1.6f ) * radius * 0.3f;
		p.grow = 1.5f;
		p.maxLife = p.life = r.Range( 0.2f, 0.5f );
		p.drag = 5.0f;
		p.gravity = -2.0f;
		Emit( p );
	}
	// smoke
	for ( int i = 0; i < 22; ++i )
	{
		Particle p{};
		p.type = PType::Smoke;
		p.pos = Vector3Add( pos, Vector3Scale( r.InSphere(), radius * 0.3f ) );
		p.vel = Vector3Scale( r.OnSphere(), r.Range( 1.0f, 5.0f ) );
		p.vel.y += 1.5f;
		unsigned char g = (unsigned char)r.Range( 50, 110 );
		p.color = Color{ g, g, g, 200 };
		p.size = r.Range( 1.0f, 2.0f );
		p.grow = r.Range( 1.5f, 3.0f );
		p.maxLife = p.life = r.Range( 1.5f, 3.2f );
		p.drag = 1.8f;
		p.gravity = -0.8f;
		Emit( p );
	}
	// sparks
	for ( int i = 0; i < 40; ++i )
	{
		Particle p{};
		p.type = PType::Spark;
		p.pos = pos;
		p.vel = Vector3Scale( r.OnSphere(), r.Range( 8.0f, 22.0f ) );
		p.vel.y = fabsf( p.vel.y );
		p.color = Color{ 255, 200, 90, 255 };
		p.size = r.Range( 0.1f, 0.25f );
		p.maxLife = p.life = r.Range( 0.4f, 1.1f );
		p.drag = 1.0f;
		p.gravity = 14.0f;
		Emit( p );
	}
	// short bright core
	Particle core{};
	core.type = PType::Ring;
	core.pos = pos;
	core.color = Color{ 255, 230, 180, 140 };
	core.size = radius * 0.6f;
	core.grow = radius * 4.0f;
	core.maxLife = core.life = 0.12f;
	Emit( core );
}

void Particles::Implosion( Vector3 pos, float radius )
{
	Rng& r = FxRng();
	// violet sparks start on a shell and rush to the centre
	for ( int i = 0; i < 70; ++i )
	{
		Particle p{};
		p.type = PType::Spark;
		Vector3 d = r.OnSphere();
		float dist = radius * r.Range( 0.8f, 1.2f );
		float life = r.Range( 0.35f, 0.5f );
		p.pos = Vector3Add( pos, Vector3Scale( d, dist ) );
		p.vel = Vector3Scale( d, -dist / life );
		p.color = ColorMix( Color{ 200, 140, 255, 255 }, Color{ 110, 60, 220, 255 }, r.Float() );
		p.size = r.Range( 0.15f, 0.3f );
		p.maxLife = p.life = life;
		Emit( p );
	}
	// dust pulled in from around the blast
	for ( int i = 0; i < 14; ++i )
	{
		Particle p{};
		p.type = PType::Smoke;
		Vector3 d = r.OnSphere();
		d.y = fabsf( d.y ) * 0.4f;
		p.pos = Vector3Add( pos, Vector3Scale( d, radius * 0.9f ) );
		p.vel = Vector3Scale( d, -radius * 1.4f );
		unsigned char g = (unsigned char)r.Range( 120, 170 );
		p.color = Color{ g, (unsigned char)( g * 0.9f ), g, 170 };
		p.size = r.Range( 0.6f, 1.2f );
		p.grow = -0.4f;
		p.maxLife = p.life = r.Range( 0.6f, 0.9f );
		p.drag = 2.0f;
		Emit( p );
	}
	Particle core{};
	core.type = PType::Ring;
	core.pos = pos;
	core.color = Color{ 190, 140, 255, 170 };
	core.size = radius * 1.6f;
	core.grow = -radius * 3.0f;
	core.maxLife = core.life = 0.45f;
	Emit( core );
}

void Particles::MuzzleBlast( Vector3 pos, Vector3 dir )
{
	Rng& r = FxRng();
	for ( int i = 0; i < 10; ++i )
	{
		Particle p{};
		p.type = PType::Fire;
		p.pos = Vector3Add( pos, Vector3Scale( dir, r.Range( 0.0f, 0.6f ) ) );
		p.vel = Vector3Add( Vector3Scale( dir, r.Range( 4.0f, 12.0f ) ), Vector3Scale( r.OnSphere(), 1.5f ) );
		p.color = Color{ 255, (unsigned char)r.Range( 150, 230 ), 80, 255 };
		p.size = r.Range( 0.4f, 0.9f );
		p.grow = 1.5f;
		p.maxLife = p.life = r.Range( 0.08f, 0.2f );
		p.drag = 8.0f;
		Emit( p );
	}
	for ( int i = 0; i < 18; ++i )
	{
		Particle p{};
		p.type = PType::Smoke;
		p.pos = Vector3Add( pos, Vector3Scale( r.InSphere(), 0.2f ) );
		p.vel = Vector3Add( Vector3Scale( dir, r.Range( 1.0f, 7.0f ) ), Vector3Scale( r.OnSphere(), 1.0f ) );
		unsigned char g = (unsigned char)r.Range( 170, 230 );
		p.color = Color{ g, g, g, 190 };
		p.size = r.Range( 0.4f, 0.8f );
		p.grow = r.Range( 0.8f, 1.8f );
		p.maxLife = p.life = r.Range( 1.2f, 2.4f );
		p.drag = 2.2f;
		p.gravity = -0.5f;
		Emit( p );
	}
}

void Particles::Stars( Vector3 pos, int count )
{
	Rng& r = FxRng();
	for ( int i = 0; i < count; ++i )
	{
		Particle p{};
		p.type = PType::Star;
		p.pos = pos;
		Vector3 v = r.OnSphere();
		v.y = fabsf( v.y ) + 0.5f;
		p.vel = Vector3Scale( v, r.Range( 1.5f, 4.0f ) );
		p.color = Color{ 255, 230, 90, 255 };
		p.size = r.Range( 0.25f, 0.45f );
		p.maxLife = p.life = r.Range( 0.8f, 1.5f );
		p.drag = 1.5f;
		p.gravity = 1.0f;
		Emit( p );
	}
}

void Particles::Sparkle( Vector3 pos, Color color, int count )
{
	Rng& r = FxRng();
	for ( int i = 0; i < count; ++i )
	{
		Particle p{};
		p.type = PType::Spark;
		p.pos = Vector3Add( pos, Vector3Scale( r.InSphere(), 0.4f ) );
		p.vel = Vector3Scale( r.OnSphere(), r.Range( 1.0f, 5.0f ) );
		p.color = color;
		p.size = r.Range( 0.08f, 0.2f );
		p.maxLife = p.life = r.Range( 0.3f, 0.8f );
		p.drag = 2.0f;
		p.gravity = 4.0f;
		Emit( p );
	}
}

void Particles::Trail( Vector3 pos, Color color, float size )
{
	Rng& r = FxRng();
	Particle p{};
	p.type = PType::Smoke;
	p.pos = Vector3Add( pos, Vector3Scale( r.InSphere(), 0.05f ) );
	p.vel = Vector3Scale( r.OnSphere(), 0.2f );
	p.color = color;
	p.size = size;
	p.grow = 0.6f;
	p.maxLife = p.life = 0.9f;
	p.drag = 1.0f;
	Emit( p );
}

void Particles::Weather( Ambient kind, Vector3 focus, float dt, Vector3 wind, Color a, Color b )
{
	float rate = 0.0f;
	switch ( kind )
	{
		case Ambient::Leaves:
			rate = 7.0f;
			break;
		case Ambient::Snow:
			rate = 120.0f;
			break;
		case Ambient::Sand:
			rate = 90.0f;
			break;
		case Ambient::Rain:
			rate = 260.0f;
			break;
		case Ambient::Embers:
			rate = 16.0f;
			break;
		default:
			return;
	}
	// weather never crowds out the particles of the siege itself
	if ( m_items.size() > kMaxParticles * 3 / 4 )
	{
		return;
	}
	Rng& r = FxRng();
	m_weatherDebt += rate * dt;
	const float R = 20.0f;
	while ( m_weatherDebt >= 1.0f )
	{
		m_weatherDebt -= 1.0f;
		Particle p{};
		p.color = ColorMix( a, b, r.Float() );
		p.pos = { focus.x + r.Range( -R, R ), focus.y + r.Range( 6.0f, 18.0f ), focus.z + r.Range( -R, R ) };
		p.rot = QuaternionIdentity();
		switch ( kind )
		{
			case Ambient::Leaves:
				p.type = PType::Flake;
				p.size = r.Range( 0.12f, 0.18f );
				p.grow = 0.5f; // aspect ratio of a leaf
				p.gravity = r.Range( 0.9f, 1.5f );
				p.drag = 1.5f;
				p.spin = { r.Range( 0.0f, 6.28f ), r.Range( 1.5f, 3.0f ), r.Range( 0.8f, 1.6f ) };
				p.drift = { 0.7f, 0.0f, 0.3f };
				p.maxLife = p.life = 14.0f;
				break;
			case Ambient::Snow:
				p.type = PType::Flake;
				p.size = r.Range( 0.09f, 0.15f );
				p.grow = 1.0f;
				p.gravity = r.Range( 1.2f, 2.0f );
				p.drag = 2.0f;
				p.spin = { r.Range( 0.0f, 6.28f ), r.Range( 1.0f, 2.0f ), r.Range( 0.3f, 0.7f ) };
				p.maxLife = p.life = 12.0f;
				p.color.a = 230;
				break;
			case Ambient::Sand:
				// grains stream sideways close to the islands
				p.type = PType::Flake;
				p.pos.y = focus.y + r.Range( -2.0f, 8.0f );
				p.size = r.Range( 0.06f, 0.1f );
				p.grow = 0.6f;
				p.gravity = r.Range( 0.1f, 0.4f );
				p.drag = 1.0f;
				p.drift = { 4.5f, 0.0f, 1.2f };
				p.vel = p.drift;
				p.spin = { r.Range( 0.0f, 6.28f ), r.Range( 4.0f, 8.0f ), r.Range( 0.3f, 0.8f ) };
				p.maxLife = p.life = 6.0f;
				p.color.a = 235;
				break;
			case Ambient::Rain:
				p.type = PType::Streak;
				p.vel = Vector3Add( { 1.5f, -r.Range( 17.0f, 22.0f ), 0.6f }, Vector3Scale( wind, 1.5f ) );
				p.size = 0.035f; // length of the streak in seconds of travel
				p.gravity = 0.0f;
				p.drag = 0.0f;
				p.maxLife = p.life = 1.6f;
				p.color.a = 120;
				break;
			case Ambient::Embers:
				// sparks rising from the lava below
				p.type = PType::Fire;
				p.pos.y = focus.y + r.Range( -14.0f, 2.0f );
				p.vel = { r.Range( -0.4f, 0.4f ), r.Range( 1.5f, 3.5f ), r.Range( -0.4f, 0.4f ) };
				p.size = r.Range( 0.1f, 0.2f );
				p.gravity = -0.3f;
				p.drag = 0.2f;
				p.maxLife = p.life = r.Range( 4.0f, 7.0f );
				break;
			default:
				break;
		}
		Emit( p );
	}
}
