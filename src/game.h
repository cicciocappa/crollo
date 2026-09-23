// Game rules, camera, cannon, projectiles and screens.
#pragma once

#include "audio.h"
#include "particles.h"
#include "physics.h"
#include "render.h"

#include <string>
#include <vector>

enum class Ammo : int
{
	Ball,
	Bomb,
	Cluster,
	Chain,
	Boulder,
	Implosion, // "Vortice": pulls everything towards the blast
	Sticky,	   // welds itself to what it hits, explodes after 3 s
	Count
};

struct AmmoInfo
{
	const char* name;
	const char* description;
	Color color;
};

const AmmoInfo& GetAmmoInfo( Ammo a );

enum class Screen
{
	Title,
	Map,		 // the six realms of the Kingdom Above
	LevelSelect, // the levels of one campaign
	Story,		 // a campaign's prologue or epilogue
	HowTo,
	Playing,
	Paused,
	Won,
	Lost,
	Replay,
};

enum class CamMode
{
	Intro,
	Aim,
	Follow,
	Overview,
	Attract,
};

struct FloatText
{
	Vector3 pos;
	std::string text;
	Color color;
	float life;
	float maxLife;
	float scale;
};

struct FlagInfo
{
	Vector3 base;
	Color color;
	float scale;
	float phase;
};

// Things the replay cannot see in the physics world (effects), stamped with the simulation step.
struct ReplayEvent
{
	enum Type : uint8_t
	{
		CannonFire,
		Explosion,
		KingDown,
		Shatter,
		BalloonPop,
		Snap,
		Implosion,
		Stick,
	};
	Type type;
	int step;
	Vector3 pos;
	Vector3 dir;
	float radius;
	bool big;
};

// Indexed by level index in memory; saved by the level's stable id, so the level table can grow.
struct Progress
{
	static const int kMaxLevels = 64;
	static const int kMaxCampaigns = 8;
	int stars[kMaxLevels] = {};
	int best[kMaxLevels] = {};
	bool introSeen[kMaxCampaigns] = {};
	bool outroSeen[kMaxCampaigns] = {};
	bool shadows = true;
	bool music = true;
	bool sfx = true;
	bool aimAssist = false;
	int bestChallenge = 0;
	int bestRound = 0;
};

class Builder;
struct LevelDef;
struct ChallengePlan;

class Game
{
public:
	explicit Game( bool headless );
	~Game();

	void Init( Renderer* renderer, Audio* audio );
	void Update( float dt );
	void Draw();

	bool WantsQuit() const
	{
		return m_quit;
	}

	// Test harness hooks
	void LoadLevel( int index, bool attract );
	void LoadChallenge( int round, bool fresh );
	void SkipIntro();
	bool RunAutoTest( int levelIndex, int maxShots, bool verbose );
	bool RunChallengeTest( int round, uint32_t seed, int maxShots, bool verbose );
	bool PlayOutAutomatically( int maxShots, int& downAtStart, int& shots );
	void TestShields();
	void TestMaterialsAndAmmo();
	void StepSimulation( float dt );
	bool AutoFireAtKing(); // false when no shot was fired (nothing to hit, or waiting for a shield)
	// Aims the cannon at a point (planning around obstacles and shields) and fires; false if it had to wait.
	bool FireAt( Vector3 aimPoint, Ammo type, const Entity* target );
	void ScanForEasyShots( int levelIndex );

	int KingsRemaining() const;
	int KingsTotal() const
	{
		return (int)m_kings.size();
	}
	bool LevelWon() const
	{
		return m_won;
	}
	Screen CurrentScreen() const
	{
		return m_screen;
	}
	void SetScreen( Screen s );
	void SetInputEnabled( bool on )
	{
		m_inputEnabled = on;
	}
	// Debug mode (--debug): every level can be picked from the start. Saved progress is untouched.
	void SetDebug( bool on )
	{
		m_debug = on;
	}

	// Used by level builders
	Scene& GetScene()
	{
		return m_scene;
	}
	void RegisterKing( Entity* king );
	void AddDecoration( const Decoration& d )
	{
		m_decorations.push_back( d );
	}
	void AddFlag( Vector3 base, Color color, float scale );
	void SetCannon( Vector3 pos, float yaw );
	void SetFortressCenter( Vector3 c, float radius );
	int GetLevelCount() const;

	// Campaign progress (also used by the tests)
	int CampaignStars( int campaign ) const;
	int CampaignMaxStars( int campaign ) const;
	int StarsToUnlock( int campaign ) const; // stars needed in the previous campaign
	bool CampaignUnlocked( int campaign ) const;
	bool LevelUnlocked( int levelIndex ) const;
	int ContinueLevel() const; // first unlocked level not beaten yet, or -1
	// Shows a campaign's prologue or epilogue on the attract scene of that realm.
	void ShowStory( int campaign, bool outro, int playAfter = -1 );
	void OpenCampaign( int campaign );
	void SelectCampaign( int campaign )
	{
		m_campaign = campaign;
	}
	void TestCampaigns();

private:
	// flow
	bool LoadDef( const LevelDef* def, uint32_t seed, bool attract, const ChallengePlan* plan );
	void UpdatePlaying( float dt );
	void UpdateAttract( float dt );
	void UpdateCamera( float dt );
	void UpdateWorld( float dt );
	void FixedStep();
	void HandleEvents();
	void CheckOutcome( float dt );
	void RestartLevel();
	void NextLevel();
	int NextInCampaign() const; // next level of the current campaign, or -1 after its last level
	void ApplyBiome( int biome );
	void LoadAttractFor( int campaign ); // a random level of that campaign (any when -1) as the backdrop

	// actions
	void Fire();
	void FireProjectile( Ammo type, Vector3 muzzle, Vector3 dir, float speed );
	void Special( Entity* projectile );
	void Detonate( Entity* e );
	void Explode( Vector3 pos, float radius, float impulse, bool big );
	void Implode( Vector3 pos, float radius, float impulse );
	void StickTo( Entity* bomb, Entity* other );
	void Shatter( Entity* e );
	void DefeatKing( Entity* king, const char* reason );
	void PopBalloon( Entity* balloon );
	void Kill( Entity* e );
	void AddScore( int points, Vector3 where, bool showText );
	void AddText( Vector3 pos, const std::string& text, Color color, float scale = 1.0f );
	void Shake( float amount );
	Vector3 Muzzle() const;
	Vector3 AimDir() const;
	float LaunchSpeed() const;
	int AmmoLeft() const;
	bool AnyProjectileFlying() const;
	void SelectAmmo( int index );
	int ComputeStars() const;
	// Time along an arc at which it would meet a crystal shield that is up at that moment, or -1.
	float PathShieldBlock( Vector3 p0, Vector3 v, Vector3 accel, float maxTime, float startTime ) const;
	bool ShieldAreaClear( const Mechanism& m ) const;
	void UpdateShields();
	void AssignShieldTimerSlots();
	void DrawShieldGhosts();
	void DrawShieldTimers();

	// drawing
	void DrawWorld();
	void DrawTrajectory();
	void DrawHUD();
	void DrawTitle();
	void DrawLevelSelect();
	void DrawMap();
	void DrawStory();
	void DrawHowTo();
	void DrawPause();
	void DrawResult( bool won );
	void DrawFloatTexts();

	// persistence
	void LoadProgress();
	void SaveProgress();

	// replay of the winning shot, re-simulated from a Box3D recording
	void RestartRecording();
	void StopRecording();
	bool StartReplay();
	void UpdateReplay( float dt );
	void EndReplay();
	void DrawReplay();
	void DrawReplayOverlay();
	void AddReplayEvent( ReplayEvent::Type type, Vector3 pos, Vector3 dir = { 0, 0, 0 }, float radius = 0.0f, bool big = false );
	void HitEffects( Vector3 point, float speed, Mat struck, float heavyMass, bool dust );

	bool m_headless = false;
	bool m_inputEnabled = true;
	bool m_debug = false;
	Vector2 m_mouseDelta{ 0, 0 };
	bool m_hadLock = false;
	int m_lockAttempts = 0;
	int m_workers = 1;
	bool m_quit = false;
	Renderer* m_renderer = nullptr;
	Audio* m_audio = nullptr;
	Scene m_scene;
	Particles m_particles;
	Progress m_progress;

	Screen m_screen = Screen::Title;
	Screen m_prevScreen = Screen::Title;
	float m_screenTime = 0.0f;
	float m_time = 0.0f;

	// endless challenge
	bool m_challenge = false;
	int m_round = 1;
	uint32_t m_challengeSeed = 1;
	int m_challengeTotal = 0;
	LevelDef* m_challengeDef = nullptr;
	ChallengePlan* m_plan = nullptr;
	char m_challengeName[64] = {};

	// campaigns
	int m_biome = 0;
	int m_campaign = 0; // the one open in the level grid
	int m_storyCampaign = 0;
	bool m_storyOutro = false;
	int m_storyPlay = -1; // level to start when the story card closes (-1: back to the map / grid)

	// level
	int m_levelIndex = 0;
	const LevelDef* m_level = nullptr;
	bool m_attract = false;
	std::vector<Entity*> m_kings;
	std::vector<Decoration> m_decorations;
	std::vector<FlagInfo> m_flags;
	std::vector<FloatText> m_texts;
	Vector3 m_fortressCenter{ 0, 0, 30 };
	float m_fortressRadius = 10.0f;
	Vector3 m_wind{};
	int m_ammo[(int)Ammo::Count] = {};
	int m_selected = 0;
	int m_score = 0;
	int m_displayScore = 0;
	int m_shots = 0;
	int m_kingsDown = 0;
	float m_levelTime = 0.0f;
	float m_sinceShot = 100.0f;
	float m_calmTime = 0.0f;
	bool m_won = false;
	bool m_lost = false;
	float m_outcomeTimer = 0.0f;
	int m_starsEarned = 0;
	int m_bonus = 0;
	bool m_newBest = false;

	// cannon
	Vector3 m_cannonPos{};
	float m_cannonBaseYaw = 0.0f;
	float m_yaw = 0.0f;
	float m_pitch = 0.35f;
	float m_power = 0.55f;
	float m_recoil = 0.0f;
	float m_reload = 0.0f;

	// simulation clock
	float m_accumulator = 0.0f;
	float m_timeScale = 1.0f;
	float m_targetTimeScale = 1.0f;
	float m_alpha = 1.0f;
	int m_hitSoundsThisFrame = 0;

	// camera
	CamMode m_camMode = CamMode::Aim;
	Camera3D m_camera{};
	Vector3 m_camPos{};
	Vector3 m_camTarget{};
	float m_camFov = 50.0f;
	float m_shake = 0.0f;
	float m_screenFlash = 0.0f;
	float m_introTime = 0.0f;
	float m_orbitYaw = 0.0f;
	float m_orbitPitch = 0.4f;
	float m_orbitDist = 30.0f;
	bool m_zoom = false;
	Entity* m_focus = nullptr;
	Vector3 m_focusLast{};
	float m_followTime = 0.0f;
	float m_watchTime = 0.0f;

	// attract mode (title screen demo)
	float m_attractTimer = 0.0f;
	int m_attractShots = 0;

	// recording & replay
	b3Recording* m_recording = nullptr;
	bool m_recordingActive = false;
	int m_recordStartStep = 0;
	int m_winStep = -1;
	int m_impactStep = -1;
	Vector3 m_impactPoint{};
	Vector3 m_shotDir{ 0, 0, 1 };
	std::vector<int> m_shotSerials;
	std::vector<ReplayEvent> m_replayEvents;
	b3RecPlayer* m_player = nullptr;
	bool m_replayAvailable = false;
	float m_replayClock = 0.0f;
	float m_replayTime = 0.0f;
	int m_replayEndFrame = 0;
	std::vector<Vector3> m_rpPrevPos, m_rpPos;
	std::vector<Quaternion> m_rpPrevRot, m_rpRot;
	std::vector<bool> m_rpHave;
	std::vector<int> m_rpSerialToOrd;
	float m_rpAlpha = 0.0f;
	int m_rpGlobalStep = 0;
	Vector3 m_rpLook{};
	Vector3 m_rpCamPos{};
	float m_rpOrbit = 0.0f;

};
