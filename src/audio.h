// Procedural audio: every sound effect is synthesized at startup, and the music is a small
// generative lute (Karplus-Strong plucked strings) running in the audio thread.
#pragma once

#include "raylib.h"

enum class Sfx : int
{
	Cannon,
	WoodHit,
	StoneHit,
	IceHit,
	IceBreak,
	MetalHit,
	Explosion,
	KingDown,
	Win,
	Lose,
	Click,
	Whoosh,
	RopeSnap,
	Pop,
	Star,
	Split,
	Boing,
	SandHit,
	Implosion,
	Beep,
	Stick,
	Count
};

class Audio
{
public:
	void Init();
	void Shutdown();

	void Play( Sfx sfx, float volume = 1.0f, float pitch = 1.0f, float pan = 0.5f );
	void PlayAt( Sfx sfx, Vector3 position, float volume = 1.0f, float pitch = 1.0f );
	void SetListener( Vector3 position, Vector3 right );

	void SetMusicEnabled( bool on );
	bool MusicEnabled() const
	{
		return m_musicOn;
	}
	void SetSfxEnabled( bool on )
	{
		m_sfxOn = on;
	}
	bool SfxEnabled() const
	{
		return m_sfxOn;
	}
	void SetWindStrength( float s );

	// Writes every synthesized effect and a stretch of music as WAV files (for inspection).
	static void ExportAll( const char* directory, float musicSeconds );

	bool Ready() const
	{
		return m_ready;
	}

private:
	static const int kVoices = 8;
	Sound m_sounds[(int)Sfx::Count][kVoices] = {};
	int m_next[(int)Sfx::Count] = {};
	AudioStream m_stream{};
	bool m_ready = false;
	bool m_musicOn = true;
	bool m_sfxOn = true;
	Vector3 m_listenerPos{};
	Vector3 m_listenerRight{ 1, 0, 0 };
};
