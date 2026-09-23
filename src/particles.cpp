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
		else
		{
			p.vel = Vector3Scale( p.vel, k );
		}
		p.pos = Vector3Add( p.pos, Vector3Scale( p.vel, dt ) );
		p.size += p.grow * dt;
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

	// Alpha blended smoke, sorted back to front
	std::vector<const Particle*> smoke;
	for ( const Particle& p : m_items )
	{
		if ( p.type == PType::Smoke )
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
		float alpha = ( t < 0.1f ? t / 0.1f : 1.0f ) * ( 1.0f - t );
		DrawBillboard( camera, tex, p->pos, p->size, WithAlpha( p->color, alpha * ( p->color.a / 255.0f ) ) );
	}
	rlDrawRenderBatchActive();

	BeginBlendMode( BLEND_ADDITIVE );
	for ( const Particle& p : m_items )
	{
		if ( p.type == PType::Smoke || p.type == PType::Chip )
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
