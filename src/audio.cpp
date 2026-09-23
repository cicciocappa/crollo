#include "audio.h"

#include "raymath.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

static const int kRate = 44100;

// ---------------------------------------------------------------------------------------------
// Synthesis helpers
// ---------------------------------------------------------------------------------------------

namespace
{
struct Noise
{
	uint32_t s;
	explicit Noise( uint32_t seed )
		: s( seed ? seed : 1u )
	{
	}
	float operator()()
	{
		s ^= s << 13;
		s ^= s >> 17;
		s ^= s << 5;
		return ( s >> 8 ) * ( 2.0f / 16777216.0f ) - 1.0f;
	}
	float Uniform()
	{
		return ( ( *this )() + 1.0f ) * 0.5f;
	}
};

struct OnePole
{
	float y = 0.0f;
	float Low( float x, float cutoff )
	{
		float a = 1.0f - expf( -2.0f * PI * cutoff / kRate );
		y += a * ( x - y );
		return y;
	}
};

// RBJ biquad band pass (constant peak gain)
struct BandPass
{
	float b0 = 0, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
	float x1 = 0, x2 = 0, y1 = 0, y2 = 0;
	void Set( float freq, float q )
	{
		float w = 2.0f * PI * freq / kRate;
		float alpha = sinf( w ) / ( 2.0f * q );
		float a0 = 1.0f + alpha;
		b0 = alpha / a0;
		b1 = 0.0f;
		b2 = -alpha / a0;
		a1 = -2.0f * cosf( w ) / a0;
		a2 = ( 1.0f - alpha ) / a0;
	}
	float Process( float x )
	{
		float y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
		x2 = x1;
		x1 = x;
		y2 = y1;
		y1 = y;
		return y;
	}
};

struct Buffer
{
	std::vector<float> s;
	explicit Buffer( float seconds )
		: s( (size_t)( seconds * kRate ), 0.0f )
	{
	}
	int N() const
	{
		return (int)s.size();
	}
	static float T( int i )
	{
		return (float)i / kRate;
	}
};

float Env( float t, float attack, float decayRate )
{
	float a = attack > 0.0f ? fminf( 1.0f, t / attack ) : 1.0f;
	return a * expf( -t * decayRate );
}

void AddSine( Buffer& b, float start, float freq, float amp, float decay, float attack = 0.001f, float sweepTo = -1.0f,
			  float sweepRate = 0.0f )
{
	float phase = 0.0f;
	int i0 = (int)( start * kRate );
	for ( int i = i0; i < b.N(); ++i )
	{
		float t = Buffer::T( i - i0 );
		float f = freq;
		if ( sweepTo > 0.0f )
		{
			f = sweepTo + ( freq - sweepTo ) * expf( -t * sweepRate );
		}
		phase += 2.0f * PI * f / kRate;
		float e = Env( t, attack, decay );
		if ( e < 1e-4f && t > attack )
		{
			break;
		}
		b.s[i] += amp * e * sinf( phase );
	}
}

void Normalize( Buffer& b, float peak )
{
	float m = 1e-6f;
	for ( float v : b.s )
	{
		m = fmaxf( m, fabsf( v ) );
	}
	float k = peak / m;
	for ( float& v : b.s )
	{
		v *= k;
	}
	// short fade out to avoid clicks
	int fade = std::min( 400, b.N() );
	for ( int i = 0; i < fade; ++i )
	{
		b.s[b.N() - 1 - i] *= (float)i / fade;
	}
}

Wave ToWave( const Buffer& b )
{
	Wave w = {};
	w.frameCount = (unsigned int)b.N();
	w.sampleRate = kRate;
	w.sampleSize = 16;
	w.channels = 1;
	short* data = (short*)MemAlloc( b.N() * sizeof( short ) );
	for ( int i = 0; i < b.N(); ++i )
	{
		float v = fmaxf( -1.0f, fminf( 1.0f, b.s[i] ) );
		data[i] = (short)( v * 32000.0f );
	}
	w.data = data;
	return w;
}

// ---------------------------------------------------------------------------------------------
// Sound recipes
// ---------------------------------------------------------------------------------------------

Buffer MakeCannon()
{
	Buffer b( 1.6f );
	Noise n( 11 );
	OnePole lp, lp2;
	float brown = 0.0f;
	for ( int i = 0; i < b.N(); ++i )
	{
		float t = Buffer::T( i );
		float cutoff = 180.0f + 4200.0f * expf( -t * 7.0f );
		float blast = lp.Low( n(), cutoff ) * Env( t, 0.002f, 5.5f ) * 1.4f;
		float crack = ( t < 0.02f ) ? n() * ( 1.0f - t / 0.02f ) * 0.8f : 0.0f;
		brown = ( brown + 0.02f * n() ) / 1.02f;
		float rumble = lp2.Low( brown, 120.0f ) * Env( t, 0.02f, 1.8f ) * 6.0f;
		b.s[i] += blast + crack + rumble;
	}
	AddSine( b, 0.0f, 120.0f, 1.1f, 4.0f, 0.003f, 36.0f, 6.0f );
	Normalize( b, 0.95f );
	return b;
}

Buffer MakeWoodHit( uint32_t seed )
{
	Buffer b( 0.3f );
	Noise n( seed );
	BandPass bp;
	bp.Set( 900.0f + 300.0f * n(), 3.0f );
	for ( int i = 0; i < b.N(); ++i )
	{
		float t = Buffer::T( i );
		b.s[i] += bp.Process( n() ) * Env( t, 0.0005f, 45.0f ) * 2.5f;
	}
	float base = 190.0f + 40.0f * n();
	AddSine( b, 0.0f, base, 0.6f, 28.0f );
	AddSine( b, 0.0f, base * 2.37f, 0.35f, 38.0f );
	AddSine( b, 0.0f, base * 4.1f, 0.2f, 50.0f );
	Normalize( b, 0.8f );
	return b;
}

Buffer MakeStoneHit( uint32_t seed )
{
	Buffer b( 0.4f );
	Noise n( seed );
	OnePole lp;
	for ( int i = 0; i < b.N(); ++i )
	{
		float t = Buffer::T( i );
		float grit = ( n.Uniform() > 0.985f ) ? n() * 0.6f * expf( -t * 12.0f ) : 0.0f;
		b.s[i] += lp.Low( n(), 1100.0f ) * Env( t, 0.001f, 20.0f ) * 2.2f + grit;
	}
	AddSine( b, 0.0f, 95.0f, 0.9f, 22.0f, 0.001f, 60.0f, 20.0f );
	Normalize( b, 0.85f );
	return b;
}

Buffer MakeIceHit( uint32_t seed )
{
	Buffer b( 0.35f );
	Noise n( seed );
	float f = 2000.0f + 600.0f * n();
	AddSine( b, 0.0f, f, 0.5f, 16.0f );
	AddSine( b, 0.0f, f * 1.58f, 0.35f, 20.0f );
	AddSine( b, 0.0f, f * 2.31f, 0.25f, 26.0f );
	OnePole lp;
	for ( int i = 0; i < b.N(); ++i )
	{
		float t = Buffer::T( i );
		float x = n();
		b.s[i] += ( x - lp.Low( x, 3000.0f ) ) * Env( t, 0.0005f, 60.0f ) * 0.8f;
	}
	Normalize( b, 0.6f );
	return b;
}

Buffer MakeIceBreak()
{
	Buffer b( 1.0f );
	Noise n( 77 );
	for ( int k = 0; k < 70; ++k )
	{
		float start = n.Uniform() * n.Uniform() * 0.55f;
		float f = 1600.0f + n.Uniform() * 6000.0f;
		AddSine( b, start, f, 0.15f + 0.2f * n.Uniform(), 30.0f + 40.0f * n.Uniform() );
	}
	OnePole lp;
	for ( int i = 0; i < b.N(); ++i )
	{
		float t = Buffer::T( i );
		float x = n();
		b.s[i] += ( x - lp.Low( x, 1500.0f ) ) * Env( t, 0.001f, 9.0f ) * 1.1f;
	}
	Normalize( b, 0.8f );
	return b;
}

Buffer MakeMetalHit()
{
	Buffer b( 0.9f );
	AddSine( b, 0.0f, 523.0f, 0.5f, 6.0f );
	AddSine( b, 0.0f, 1341.0f, 0.35f, 7.5f );
	AddSine( b, 0.0f, 2209.0f, 0.25f, 9.0f );
	AddSine( b, 0.0f, 3173.0f, 0.15f, 12.0f );
	Noise n( 5 );
	for ( int i = 0; i < 600; ++i )
	{
		b.s[i] += n() * ( 1.0f - i / 600.0f ) * 0.4f;
	}
	Normalize( b, 0.6f );
	return b;
}

Buffer MakeExplosion()
{
	Buffer b( 2.4f );
	Noise n( 99 );
	OnePole lp, lp2;
	float brown = 0.0f;
	for ( int i = 0; i < b.N(); ++i )
	{
		float t = Buffer::T( i );
		float cutoff = 120.0f + 3500.0f * expf( -t * 3.2f );
		float body = lp.Low( n(), cutoff ) * Env( t, 0.004f, 2.0f ) * 2.2f;
		float crackle = ( t < 0.9f && n.Uniform() > 0.992f ) ? n() * expf( -t * 3.0f ) : 0.0f;
		brown = ( brown + 0.02f * n() ) / 1.02f;
		float rumble = lp2.Low( brown, 90.0f ) * Env( t, 0.05f, 1.2f ) * 8.0f;
		b.s[i] += body + crackle + rumble;
	}
	AddSine( b, 0.0f, 70.0f, 1.0f, 2.5f, 0.004f, 28.0f, 3.0f );
	Normalize( b, 0.98f );
	return b;
}

Buffer MakeKingDown()
{
	Buffer b( 0.9f );
	// bonk
	AddSine( b, 0.0f, 950.0f, 0.4f, 25.0f, 0.001f, 320.0f, 30.0f );
	// descending cartoon "wooo"
	float phase = 0.0f;
	for ( int i = (int)( 0.08f * kRate ); i < b.N(); ++i )
	{
		float t = Buffer::T( i ) - 0.08f;
		float f = 180.0f + 420.0f * expf( -t * 3.0f );
		f *= 1.0f + 0.05f * sinf( 2.0f * PI * 7.0f * t );
		phase += 2.0f * PI * f / kRate;
		float e = Env( t, 0.03f, 2.8f );
		float v = sinf( phase ) + 0.35f * sinf( 2.0f * phase ) + 0.2f * sinf( 3.0f * phase );
		b.s[i] += v * e * 0.5f;
	}
	Normalize( b, 0.75f );
	return b;
}

void AddPluck( Buffer& b, float start, float freq, float amp, float decay )
{
	AddSine( b, start, freq, amp, decay, 0.002f );
	AddSine( b, start, freq * 2.0f, amp * 0.45f, decay * 1.6f, 0.002f );
	AddSine( b, start, freq * 3.0f, amp * 0.25f, decay * 2.2f, 0.002f );
	AddSine( b, start, freq * 4.0f, amp * 0.1f, decay * 3.0f, 0.002f );
}

Buffer MakeWin()
{
	Buffer b( 2.0f );
	const float notes[] = { 523.25f, 659.25f, 783.99f, 1046.5f };
	for ( int i = 0; i < 4; ++i )
	{
		AddPluck( b, i * 0.11f, notes[i], 0.5f, 4.5f );
	}
	AddPluck( b, 0.5f, 523.25f, 0.35f, 2.0f );
	AddPluck( b, 0.5f, 659.25f, 0.35f, 2.0f );
	AddPluck( b, 0.5f, 783.99f, 0.35f, 2.0f );
	AddPluck( b, 0.5f, 1046.5f, 0.4f, 1.8f );
	Normalize( b, 0.8f );
	return b;
}

Buffer MakeLose()
{
	Buffer b( 1.8f );
	const float notes[] = { 392.0f, 370.0f, 349.2f, 329.6f };
	float phase = 0.0f;
	OnePole lp;
	for ( int i = 0; i < b.N(); ++i )
	{
		float t = Buffer::T( i );
		int k = std::min( 3, (int)( t / 0.32f ) );
		float local = t - k * 0.32f;
		float f = notes[k];
		if ( k == 3 )
		{
			f *= 1.0f + 0.03f * sinf( 2.0f * PI * 6.0f * local );
		}
		phase += 2.0f * PI * f / kRate;
		float saw = 2.0f * ( phase / ( 2.0f * PI ) - floorf( phase / ( 2.0f * PI ) + 0.5f ) );
		float e = ( k < 3 ) ? Env( local, 0.02f, 3.0f ) : Env( local, 0.02f, 1.2f );
		b.s[i] = lp.Low( saw, 1400.0f ) * e;
	}
	Normalize( b, 0.6f );
	return b;
}

Buffer MakeClick()
{
	Buffer b( 0.06f );
	AddSine( b, 0.0f, 1400.0f, 0.6f, 80.0f );
	AddSine( b, 0.0f, 2800.0f, 0.2f, 120.0f );
	Normalize( b, 0.5f );
	return b;
}

Buffer MakeWhoosh()
{
	Buffer b( 0.7f );
	Noise n( 3 );
	BandPass bp;
	for ( int i = 0; i < b.N(); ++i )
	{
		float t = Buffer::T( i );
		float c = 500.0f + 1800.0f * sinf( PI * t / 0.7f );
		if ( ( i & 31 ) == 0 )
		{
			bp.Set( c, 1.5f );
		}
		float e = sinf( PI * t / 0.7f );
		b.s[i] = bp.Process( n() ) * e * e;
	}
	Normalize( b, 0.6f );
	return b;
}

Buffer MakeRopeSnap()
{
	Buffer b( 0.45f );
	Noise n( 21 );
	for ( int i = 0; i < 900; ++i )
	{
		b.s[i] += n() * ( 1.0f - i / 900.0f );
	}
	AddSine( b, 0.0f, 260.0f, 0.7f, 9.0f, 0.001f, 120.0f, 8.0f );
	AddSine( b, 0.0f, 520.0f, 0.3f, 12.0f, 0.001f, 240.0f, 8.0f );
	Normalize( b, 0.7f );
	return b;
}

Buffer MakePop()
{
	Buffer b( 0.3f );
	Noise n( 8 );
	OnePole lp;
	for ( int i = 0; i < b.N(); ++i )
	{
		float t = Buffer::T( i );
		b.s[i] += lp.Low( n(), 5000.0f ) * Env( t, 0.0003f, 55.0f ) * 2.0f;
	}
	AddSine( b, 0.0f, 140.0f, 0.6f, 25.0f );
	Normalize( b, 0.85f );
	return b;
}

Buffer MakeStar()
{
	Buffer b( 0.8f );
	AddSine( b, 0.0f, 1567.98f, 0.4f, 6.0f );
	AddSine( b, 0.0f, 2349.3f, 0.3f, 7.0f );
	AddSine( b, 0.06f, 3135.9f, 0.25f, 8.0f );
	Normalize( b, 0.55f );
	return b;
}

Buffer MakeSplit()
{
	Buffer b( 0.4f );
	Noise n( 44 );
	BandPass bp;
	bp.Set( 2500.0f, 2.0f );
	for ( int i = 0; i < b.N(); ++i )
	{
		float t = Buffer::T( i );
		b.s[i] += bp.Process( n() ) * Env( t, 0.001f, 18.0f ) * 1.5f;
	}
	AddSine( b, 0.0f, 880.0f, 0.3f, 14.0f, 0.001f, 1760.0f, 10.0f );
	Normalize( b, 0.6f );
	return b;
}

Buffer MakeBoing()
{
	// a rubbery "boing": a sine that bends upwards with a fast, decaying wobble
	Buffer b( 0.55f );
	float phase = 0.0f;
	for ( int i = 0; i < b.N(); ++i )
	{
		float t = Buffer::T( i );
		float f = 170.0f + 260.0f * ( 1.0f - expf( -t * 9.0f ) );
		f *= 1.0f + 0.12f * expf( -t * 5.0f ) * sinf( 2.0f * PI * 17.0f * t );
		phase += 2.0f * PI * f / kRate;
		b.s[i] = ( sinf( phase ) + 0.3f * sinf( 2.0f * phase ) ) * Env( t, 0.004f, 6.0f );
	}
	Normalize( b, 0.7f );
	return b;
}

Buffer MakeSandHit()
{
	// a dull, muffled thud: low-passed noise and a soft low sine
	Buffer b( 0.35f );
	Noise n( 31 );
	OnePole lp, lp2;
	for ( int i = 0; i < b.N(); ++i )
	{
		float t = Buffer::T( i );
		b.s[i] = lp2.Low( lp.Low( n(), 500.0f ), 500.0f ) * Env( t, 0.002f, 22.0f ) * 4.0f;
	}
	AddSine( b, 0.0f, 75.0f, 0.5f, 18.0f, 0.003f );
	Normalize( b, 0.8f );
	return b;
}

Buffer MakeImplosion()
{
	// a deep thump followed by an inward rush that closes down, like air being sucked away
	Buffer b( 1.2f );
	Noise n( 57 );
	OnePole lp;
	for ( int i = 0; i < b.N(); ++i )
	{
		float t = Buffer::T( i );
		float cutoff = 3200.0f * expf( -t * 3.5f ) + 150.0f;
		float rush = lp.Low( n(), cutoff ) * sinf( PI * Clamp( t / 1.1f, 0.0f, 1.0f ) ) * 1.6f;
		b.s[i] = rush;
	}
	AddSine( b, 0.0f, 55.0f, 1.1f, 5.0f, 0.004f, 30.0f, 4.0f );
	AddSine( b, 0.05f, 420.0f, 0.25f, 4.0f, 0.01f, 90.0f, 5.0f );
	Normalize( b, 0.95f );
	return b;
}

Buffer MakeBeep()
{
	Buffer b( 0.09f );
	AddSine( b, 0.0f, 1850.0f, 0.6f, 25.0f, 0.002f );
	Normalize( b, 0.45f );
	return b;
}

Buffer MakeStick()
{
	// a wet "splat" as the sticky bomb grabs hold
	Buffer b( 0.25f );
	Noise n( 71 );
	BandPass bp;
	bp.Set( 700.0f, 1.2f );
	for ( int i = 0; i < b.N(); ++i )
	{
		float t = Buffer::T( i );
		b.s[i] = bp.Process( n() ) * Env( t, 0.001f, 30.0f ) * 3.0f;
	}
	AddSine( b, 0.0f, 140.0f, 0.5f, 25.0f, 0.001f, 80.0f, 20.0f );
	Normalize( b, 0.7f );
	return b;
}

// ---------------------------------------------------------------------------------------------
// Generative music + ambient wind, synthesized in the audio thread
// ---------------------------------------------------------------------------------------------

struct Pluck
{
	float buf[2400];
	int len = 0;
	int idx = 0;
	float decay = 0.996f;
	float gain = 0.0f;
	bool active = false;
	float bright = 0.5f;
};

struct MusicState
{
	Pluck voices[10];
	int nextVoice = 0;
	int sampleInStep = 0;
	int step = 0;
	int melody = 62;
	Noise rng{ 1234 };
	float brown = 0.0f;
	float windPhase1 = 0.0f;
	float windPhase2 = 0.0f;
	OnePole windLp;
	OnePole outLp;
	std::atomic<float> musicGain{ 0.0f };
	std::atomic<float> windGain{ 0.0f };
	float musicGainSmoothed = 0.0f;
	float windGainSmoothed = 0.0f;
};

MusicState g_music;

float MidiToFreq( int m )
{
	return 440.0f * powf( 2.0f, ( m - 69 ) / 12.0f );
}

void TriggerPluck( int midi, float velocity, float brightness )
{
	MusicState& ms = g_music;
	Pluck& p = ms.voices[ms.nextVoice];
	ms.nextVoice = ( ms.nextVoice + 1 ) % 10;
	float f = MidiToFreq( midi );
	p.len = std::max( 2, std::min( 2399, (int)( kRate / f ) ) );
	p.idx = 0;
	p.gain = velocity;
	p.bright = brightness;
	p.decay = 0.9965f + 0.002f * ( 1.0f - brightness );
	// initial excitation: filtered noise burst
	float y = 0.0f;
	for ( int i = 0; i < p.len; ++i )
	{
		float x = ms.rng();
		y += brightness * ( x - y );
		p.buf[i] = y;
	}
	p.active = true;
}

void SequencerStep()
{
	MusicState& ms = g_music;
	// Andalusian cadence in D minor: Dm - C - Bb - A
	static const int chords[4][3] = { { 50, 53, 57 }, { 48, 52, 55 }, { 46, 50, 53 }, { 45, 49, 52 } };
	static const int arp[8] = { 0, 1, 2, 1, 0, 2, 1, 2 };
	static const int scale[] = { 62, 64, 65, 67, 69, 70, 72, 73, 74, 76 };
	const int scaleCount = 10;

	int bar = ( ms.step / 8 ) % 4;
	int s = ms.step % 8;
	const int* chord = chords[bar];

	if ( s == 0 )
	{
		TriggerPluck( chord[0] - 12, 0.55f, 0.35f );
	}
	if ( s == 4 )
	{
		TriggerPluck( chord[0] - 5, 0.4f, 0.35f );
	}

	if ( ms.rng.Uniform() < 0.8f )
	{
		TriggerPluck( chord[arp[s]] + 12, 0.22f + 0.08f * ms.rng.Uniform(), 0.55f );
	}

	bool melodyBeat = ( s == 0 || s == 3 || s == 6 );
	if ( melodyBeat && ( ms.step / 32 ) % 2 == 1 && ms.rng.Uniform() < 0.7f )
	{
		// random walk on the scale, pulled towards chord tones
		int idx = 0;
		for ( int i = 0; i < scaleCount; ++i )
		{
			if ( scale[i] == ms.melody )
			{
				idx = i;
			}
		}
		int delta = (int)( ms.rng.Uniform() * 5.0f ) - 2;
		idx = std::max( 0, std::min( scaleCount - 1, idx + delta ) );
		ms.melody = scale[idx];
		if ( s == 0 )
		{
			ms.melody = chord[( ms.step / 8 ) % 3] + 12;
		}
		TriggerPluck( ms.melody + 12, 0.3f, 0.7f );
	}

	ms.step += 1;
}

void MusicCallback( void* bufferData, unsigned int frames )
{
	MusicState& ms = g_music;
	float* out = (float*)bufferData;
	const int stepLen = (int)( kRate * 60.0f / 88.0f / 2.0f );
	float targetMusic = ms.musicGain.load();
	float targetWind = ms.windGain.load();

	for ( unsigned int i = 0; i < frames; ++i )
	{
		ms.musicGainSmoothed += 0.00005f * ( targetMusic - ms.musicGainSmoothed );
		ms.windGainSmoothed += 0.00005f * ( targetWind - ms.windGainSmoothed );

		if ( ms.sampleInStep == 0 && ms.musicGainSmoothed > 0.001f )
		{
			SequencerStep();
		}
		ms.sampleInStep = ( ms.sampleInStep + 1 ) % stepLen;

		float m = 0.0f;
		for ( Pluck& p : ms.voices )
		{
			if ( p.active == false )
			{
				continue;
			}
			int i0 = p.idx;
			int i1 = ( p.idx + 1 ) % p.len;
			float v = p.buf[i0];
			p.buf[i0] = 0.5f * ( p.buf[i0] + p.buf[i1] ) * p.decay;
			p.idx = i1;
			m += v * p.gain;
			p.gain *= 0.99997f;
			if ( p.gain < 0.001f )
			{
				p.active = false;
			}
		}
		m = ms.outLp.Low( m, 5000.0f );

		// wind: brown noise with two slow gusting LFOs
		ms.brown = ( ms.brown + 0.02f * ms.rng() ) / 1.02f;
		ms.windPhase1 += 2.0f * PI * 0.11f / kRate;
		ms.windPhase2 += 2.0f * PI * 0.037f / kRate;
		float gust = 0.55f + 0.3f * sinf( ms.windPhase1 ) + 0.15f * sinf( ms.windPhase2 );
		float wind = ms.windLp.Low( ms.brown, 300.0f + 500.0f * gust ) * gust * 5.0f;

		float v = m * 0.85f * ms.musicGainSmoothed + wind * ms.windGainSmoothed;
		out[i] = fmaxf( -1.0f, fminf( 1.0f, v ) );
	}
}

} // namespace

// ---------------------------------------------------------------------------------------------
// Audio
// ---------------------------------------------------------------------------------------------

void Audio::Init()
{
	InitAudioDevice();
	if ( IsAudioDeviceReady() == false )
	{
		TraceLog( LOG_WARNING, "Audio device not available, running silent" );
		return;
	}

	auto load = [&]( Sfx id, Buffer buf ) {
		Wave w = ToWave( buf );
		Sound base = LoadSoundFromWave( w );
		UnloadWave( w );
		m_sounds[(int)id][0] = base;
		for ( int v = 1; v < kVoices; ++v )
		{
			m_sounds[(int)id][v] = LoadSoundAlias( base );
		}
	};

	load( Sfx::Cannon, MakeCannon() );
	load( Sfx::WoodHit, MakeWoodHit( 3 ) );
	load( Sfx::StoneHit, MakeStoneHit( 9 ) );
	load( Sfx::IceHit, MakeIceHit( 4 ) );
	load( Sfx::IceBreak, MakeIceBreak() );
	load( Sfx::MetalHit, MakeMetalHit() );
	load( Sfx::Explosion, MakeExplosion() );
	load( Sfx::KingDown, MakeKingDown() );
	load( Sfx::Win, MakeWin() );
	load( Sfx::Lose, MakeLose() );
	load( Sfx::Click, MakeClick() );
	load( Sfx::Whoosh, MakeWhoosh() );
	load( Sfx::RopeSnap, MakeRopeSnap() );
	load( Sfx::Pop, MakePop() );
	load( Sfx::Star, MakeStar() );
	load( Sfx::Split, MakeSplit() );
	load( Sfx::Boing, MakeBoing() );
	load( Sfx::SandHit, MakeSandHit() );
	load( Sfx::Implosion, MakeImplosion() );
	load( Sfx::Beep, MakeBeep() );
	load( Sfx::Stick, MakeStick() );

	SetAudioStreamBufferSizeDefault( 2048 );
	m_stream = LoadAudioStream( kRate, 32, 1 );
	SetAudioStreamCallback( m_stream, MusicCallback );
	PlayAudioStream( m_stream );
	SetMusicEnabled( true );
	SetWindStrength( 0.3f );

	m_ready = true;
}

void Audio::Shutdown()
{
	if ( m_ready == false )
	{
		if ( IsAudioDeviceReady() )
		{
			CloseAudioDevice();
		}
		return;
	}
	StopAudioStream( m_stream );
	UnloadAudioStream( m_stream );
	for ( int s = 0; s < (int)Sfx::Count; ++s )
	{
		for ( int v = kVoices - 1; v >= 1; --v )
		{
			UnloadSoundAlias( m_sounds[s][v] );
		}
		UnloadSound( m_sounds[s][0] );
	}
	CloseAudioDevice();
	m_ready = false;
}

void Audio::Play( Sfx sfx, float volume, float pitch, float pan )
{
	if ( m_ready == false || m_sfxOn == false || volume <= 0.01f )
	{
		return;
	}
	int id = (int)sfx;
	Sound& s = m_sounds[id][m_next[id]];
	m_next[id] = ( m_next[id] + 1 ) % kVoices;
	SetSoundVolume( s, fminf( volume, 1.0f ) );
	SetSoundPitch( s, pitch );
	SetSoundPan( s, pan );
	PlaySound( s );
}

void Audio::SetListener( Vector3 position, Vector3 right )
{
	m_listenerPos = position;
	m_listenerRight = right;
}

void Audio::PlayAt( Sfx sfx, Vector3 position, float volume, float pitch )
{
	Vector3 d = Vector3Subtract( position, m_listenerPos );
	float dist = Vector3Length( d );
	float atten = 1.0f / ( 1.0f + dist / 30.0f );
	float side = dist > 0.01f ? Vector3DotProduct( Vector3Scale( d, 1.0f / dist ), m_listenerRight ) : 0.0f;
	// raylib pan: 0.5 is centered, lower values pan right
	float pan = 0.5f - 0.35f * side;
	Play( sfx, volume * atten, pitch, pan );
}

void Audio::SetMusicEnabled( bool on )
{
	m_musicOn = on;
	g_music.musicGain.store( on ? 0.55f : 0.0f );
}

void Audio::SetWindStrength( float s )
{
	g_music.windGain.store( fmaxf( 0.0f, fminf( 1.0f, s ) ) * 0.35f );
}

void Audio::ExportAll( const char* dir, float musicSeconds )
{
	struct Item
	{
		const char* name;
		Buffer buf;
	};
	Item items[] = {
		{ "cannon", MakeCannon() },		   { "wood", MakeWoodHit( 3 ) },	{ "stone", MakeStoneHit( 9 ) },
		{ "ice", MakeIceHit( 4 ) },		   { "icebreak", MakeIceBreak() },	{ "metal", MakeMetalHit() },
		{ "explosion", MakeExplosion() }, { "kingdown", MakeKingDown() }, { "win", MakeWin() },
		{ "lose", MakeLose() },			   { "click", MakeClick() },		{ "whoosh", MakeWhoosh() },
		{ "snap", MakeRopeSnap() },		   { "pop", MakePop() },			{ "star", MakeStar() },
		{ "split", MakeSplit() },		   { "boing", MakeBoing() },		{ "sand", MakeSandHit() },
		{ "implosion", MakeImplosion() }, { "beep", MakeBeep() },		{ "stick", MakeStick() },
	};
	for ( Item& it : items )
	{
		Wave w = ToWave( it.buf );
		ExportWave( w, TextFormat( "%s/%s.wav", dir, it.name ) );
		UnloadWave( w );
	}

	g_music.musicGain.store( 0.55f );
	g_music.windGain.store( 0.1f );
	g_music.musicGainSmoothed = 0.55f;
	g_music.windGainSmoothed = 0.1f;
	int frames = (int)( musicSeconds * kRate );
	std::vector<float> out( frames );
	MusicCallback( out.data(), (unsigned int)frames );
	Buffer b( musicSeconds );
	for ( int i = 0; i < frames && i < b.N(); ++i )
	{
		b.s[i] = out[i];
	}
	Wave w = ToWave( b );
	ExportWave( w, TextFormat( "%s/music.wav", dir ) );
	UnloadWave( w );
}
