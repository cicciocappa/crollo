#include "game.h"

#include "levels.h"
#include "rlgl.h"
#include "ui.h"

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

#if defined( __EMSCRIPTEN__ )
#include <emscripten/emscripten.h>

// Progress lives in localStorage; the C side keeps using a small file in the in-memory filesystem.
EM_JS( void, WebLoadSave, ( const char* path ), {
	try
	{
		var v = localStorage.getItem( 'crollo_save' );
		if ( v )
			FS.writeFile( UTF8ToString( path ), v );
	}
	catch ( e )
	{
	}
} );

EM_JS( void, WebStoreSave, ( const char* path ), {
	try
	{
		localStorage.setItem( 'crollo_save', FS.readFile( UTF8ToString( path ), { encoding : 'utf8' } ) );
	}
	catch ( e )
	{
	}
} );

// raylib keeps only the last pointer-lock movement of a frame; sum them all for smooth aiming.
EM_JS( void, WebInstallMouse, (), {
	Module.crolloDX = 0;
	Module.crolloDY = 0;
	document.addEventListener( 'mousemove', function( e ) {
		if ( document.pointerLockElement && Math.abs( e.movementX ) < 400 && Math.abs( e.movementY ) < 400 )
		{
			Module.crolloDX += e.movementX;
			Module.crolloDY += e.movementY;
		}
	} );
} );

EM_JS( float, WebTakeMouseX, (), {
	var v = Module.crolloDX || 0;
	Module.crolloDX = 0;
	return v;
} );

EM_JS( float, WebTakeMouseY, (), {
	var v = Module.crolloDY || 0;
	Module.crolloDY = 0;
	return v;
} );
#endif

static const float kFixedDt = 1.0f / 60.0f;
static const int kSubSteps = 4;
static const float kGravity = 10.0f;
static const float kMinSpeed = 12.0f;
static const float kMaxSpeed = 34.0f;
static const float kIntroSeconds = 5.0f;

static const AmmoInfo s_ammo[(int)Ammo::Count] = {
	{ "Palla", "Palla di ferro: semplice e affidabile.", { 70, 72, 82, 255 } },
	{ "Bomba", "Esplode all'impatto, o quando premi SPAZIO.", { 40, 40, 44, 255 } },
	{ "Grappolo", "Premi SPAZIO in volo: si divide in sette.", { 90, 115, 75, 255 } },
	{ "Catena", "Due palle incatenate che ruotano e spazzano.", { 110, 110, 120, 255 } },
	{ "Macigno", "Enorme e pesante. Lento, ma sfonda tutto.", { 125, 110, 98, 255 } },
	{ "Vortice", "Implode: risucchia i blocchi verso il centro (SPAZIO in volo).", { 110, 60, 160, 255 } },
	{ "Adesiva", "Si attacca a ciò che colpisce ed esplode dopo 3 s (o con SPAZIO).", { 70, 140, 70, 255 } },
};

const AmmoInfo& GetAmmoInfo( Ammo a )
{
	return s_ammo[(int)a];
}

// Ammunition that does something when the player presses SPACE mid-flight.
static bool HasSpecial( int ammo )
{
	return ammo == (int)Ammo::Bomb || ammo == (int)Ammo::Cluster || ammo == (int)Ammo::Implosion || ammo == (int)Ammo::Sticky;
}

static Color kGold{ 255, 200, 50, 255 };
static Color kCream{ 255, 244, 220, 255 };

// ---------------------------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------------------------

Game::Game( bool headless )
	: m_headless( headless )
{
	m_challengeDef = new LevelDef();
	m_plan = new ChallengePlan();
}

Game::~Game()
{
	StopRecording();
	if ( m_player )
	{
		b3DestroyPlayer( m_player );
	}
	if ( m_recording )
	{
		b3DestroyRecording( m_recording );
	}
	m_scene.Destroy();
	delete m_challengeDef;
	delete m_plan;
}

void Game::Init( Renderer* renderer, Audio* audio )
{
	m_renderer = renderer;
	m_audio = audio;
#if defined( __EMSCRIPTEN__ )
	if ( m_headless == false )
	{
		WebInstallMouse();
	}
#endif
	LoadProgress();
	if ( m_renderer )
	{
		m_renderer->shadowsEnabled = m_progress.shadows;
	}
	if ( m_audio )
	{
		m_audio->SetMusicEnabled( m_progress.music );
		m_audio->SetSfxEnabled( m_progress.sfx );
	}
	if ( m_headless == false )
	{
		SetScreen( Screen::Title );
	}
}

int Game::GetLevelCount() const
{
	return LevelCount();
}

void Game::RegisterKing( Entity* king )
{
	m_kings.push_back( king );
}

void Game::AddFlag( Vector3 base, Color color, float scale )
{
	Decoration d;
	d.type = Decoration::Flag;
	d.pos = base;
	d.scale = scale;
	d.rot = 0.0f;
	d.color = color;
	m_decorations.push_back( d );
	m_flags.push_back( { base, color, scale, (float)m_flags.size() * 1.7f } );
}

void Game::SetCannon( Vector3 pos, float yaw )
{
	m_cannonPos = pos;
	m_cannonBaseYaw = yaw;
}

void Game::AddAimHint( Entity* king, Entity* via, Vector3 offset, float lob )
{
	m_aimHints.push_back( { king->serial, via->serial, via->pos, offset, lob } );
}

void Game::SetFortressCenter( Vector3 c, float radius )
{
	m_fortressCenter = c;
	m_fortressRadius = radius;
}

int Game::KingsRemaining() const
{
	int n = 0;
	for ( const Entity* k : m_kings )
	{
		if ( k->defeated == false )
		{
			++n;
		}
	}
	return n;
}

void Game::ApplyBiome( int biome )
{
	// CROLLO_BIOME forces a look on every level, for screenshots and tuning
	if ( const char* force = getenv( "CROLLO_BIOME" ) )
	{
		biome = atoi( force );
	}
	int n = BiomeCount();
	m_biome = ( biome % n + n ) % n;
	const Biome& b = GetBiome( m_biome );
	if ( m_renderer )
	{
		m_renderer->SetBiome( b );
	}
	if ( m_audio )
	{
		m_audio->SetMusicStyle( b.musicStyle );
	}
}

void Game::LoadLevel( int index, bool attract )
{
	m_challenge = false;
	m_levelIndex = index;
	int c = CampaignOfLevel( index );
	if ( c >= 0 && attract == false )
	{
		m_campaign = c;
	}
	ApplyBiome( c >= 0 ? GetCampaign( c ).biome : 0 );
	LoadDef( &GetLevel( index ), 1000u + (uint32_t)index * 77u, attract, nullptr );
}

void Game::LoadChallenge( int round, bool fresh )
{
	if ( fresh )
	{
		m_challengeTotal = 0;
		m_challengeSeed = (uint32_t)( GetRandomValue( 1, 1 << 30 ) );
		if ( m_headless )
		{
			m_challengeSeed = 12345u;
		}
	}
	m_challenge = true;
	m_round = round;
	// every round visits a realm, the ones without a campaign yet included
	ApplyBiome( (int)( ( m_challengeSeed >> 3 ) + (uint32_t)round * 5u ) );

	// A generated fortress might be unstable; try a few seeds until every king stands.
	for ( int attempt = 0; attempt < 6; ++attempt )
	{
		uint32_t seed = m_challengeSeed + (uint32_t)attempt * 101u;
		*m_plan = PlanChallenge( round, seed );
		snprintf( m_challengeName, sizeof( m_challengeName ), "Sfida - Round %d", round );
		LevelDef& def = *m_challengeDef;
		def.name = m_challengeName;
		def.subtitle = round == 1 ? "Quanti round riuscirai a superare?" : "Le fortezze si fanno più robuste...";
		def.hint = "Ogni round è generato a caso. I punti si sommano: le munizioni avanzate valgono bonus.";
		for ( int i = 0; i < (int)Ammo::Count; ++i )
		{
			def.ammo[i] = m_plan->ammo[i];
		}
		def.par = m_plan->par;
		def.wind = m_plan->wind;
		def.build = BuildChallenge;
		if ( LoadDef( &def, seed, false, m_plan ) )
		{
			break;
		}
	}
}

bool Game::LoadDef( const LevelDef* def, uint32_t seed, bool attract, const ChallengePlan* plan )
{
	int workers = 1;
	if ( m_headless == false )
	{
		workers = (int)std::min( 4u, std::max( 1u, std::thread::hardware_concurrency() / 2 ) );
	}
	m_workers = workers;

	StopRecording();
	if ( m_player )
	{
		b3DestroyPlayer( m_player );
		m_player = nullptr;
	}
	m_replayAvailable = false;
	m_winStep = -1;
	m_impactStep = -1;
	m_shotSerials.clear();
	m_shotPartner = 0;
	m_replayEvents.clear();

	// the previous level's hulls are freed with its world, so their cached meshes must go too
	if ( m_renderer )
	{
		m_renderer->ClearHullCache();
	}
	m_scene.Create( workers );
	m_particles.Clear();
	m_texts.clear();
	m_decorations.clear();
	m_flags.clear();
	m_kings.clear();
	m_aimHints.clear();
	m_focus = nullptr;

	m_level = def;
	m_attract = attract;
	m_wind = m_level->wind;
	if ( m_headless )
	{
		// tests: a level plays out the same whether it runs alone or after the others
		FxRng() = Rng( 0xC0FFEEu ^ seed );
	}
	m_windShift = 0.0f;
	m_windPending = false;
	m_windChanged = 0.0f;
	m_windRng = Rng( seed ^ 0x77196e5du );

	for ( int i = 0; i < (int)Ammo::Count; ++i )
	{
		m_ammo[i] = attract ? 99 : m_level->ammo[i];
	}

	Builder b( *this, seed );
	b.plan = plan;
	b.biome = &GetBiome( m_biome );
	m_level->build( b );

	// Let the structures settle before the player sees them.
	for ( int i = 0; i < 30; ++i )
	{
		m_scene.Step( kFixedDt, kSubSteps );
	}
	for ( Entity* e : m_scene.entities )
	{
		e->prevPos = e->pos;
		e->prevRot = e->rot;
		e->age = 0.0f;
	}
	m_scene.hits.clear();
	m_scene.touches.clear();

	m_selected = 0;
	for ( int i = 0; i < (int)Ammo::Count; ++i )
	{
		if ( m_ammo[i] > 0 )
		{
			m_selected = i;
			break;
		}
	}

	m_score = 0;
	m_displayScore = 0;
	m_shots = 0;
	m_kingsDown = 0;
	m_levelTime = 0.0f;
	m_sinceShot = 100.0f;
	m_calmTime = 0.0f;
	m_won = false;
	m_lost = false;
	m_outcomeTimer = 0.0f;
	m_starsEarned = 0;
	m_bonus = 0;
	m_newBest = false;
	m_accumulator = 0.0f;
	m_timeScale = 1.0f;
	m_targetTimeScale = 1.0f;
	m_attractTimer = 1.5f;
	m_attractShots = 0;

	Vector3 toFort = Vector3Subtract( m_fortressCenter, m_cannonPos );
	m_yaw = atan2f( toFort.x, toFort.z );
	m_cannonBaseYaw = m_yaw;
	m_pitch = 0.32f;
	m_power = 0.55f;
	m_recoil = 0.0f;
	m_reload = 0.0f;

	m_orbitYaw = PI;
	m_orbitPitch = 0.45f;
	m_orbitDist = m_fortressRadius * 2.4f;
	m_zoom = false;

	if ( attract )
	{
		m_camMode = CamMode::Attract;
		m_orbitYaw = FxRng().Range( 0.0f, 2.0f * PI );
	}
	else
	{
		m_camMode = CamMode::Intro;
		m_introTime = 0.0f;
	}

	// start the camera somewhere sensible
	m_camTarget = m_fortressCenter;
	m_camPos = Vector3Add( m_fortressCenter, { m_fortressRadius * 2.0f, m_fortressRadius, -m_fortressRadius * 1.2f } );
	m_camFov = 50.0f;
	UpdateCamera( 0.0f );

	if ( m_audio )
	{
		m_audio->SetWindStrength( 0.25f + Vector3Length( m_wind ) * 0.25f );
	}

	AssignShieldTimerSlots();

	// report whether every king survived the settling
	for ( const Entity* k : m_kings )
	{
		Vector3 up = Vector3RotateByQuaternion( { 0, 1, 0 }, k->rot );
		if ( up.y < 0.9f || k->pos.y < k->homeY - 0.3f )
		{
			return false;
		}
	}
	return true;
}

void Game::SkipIntro()
{
	if ( m_camMode == CamMode::Intro )
	{
		m_camMode = CamMode::Aim;
	}
}

void Game::RestartLevel()
{
	if ( m_challenge )
	{
		LoadChallenge( m_round, false );
		SkipIntro();
		SetScreen( Screen::Playing );
		return;
	}
	LoadLevel( m_levelIndex, false );
	SkipIntro();
	SetScreen( Screen::Playing );
}

void Game::NextLevel()
{
	if ( m_challenge )
	{
		LoadChallenge( m_round + 1, false );
		SetScreen( Screen::Playing );
		return;
	}
	int next = NextInCampaign();
	if ( next < 0 )
	{
		SetScreen( Screen::Map );
		return;
	}
	LoadLevel( next, false );
	SetScreen( Screen::Playing );
}

int Game::NextInCampaign() const
{
	int c = CampaignOfLevel( m_levelIndex );
	if ( c < 0 )
	{
		return -1;
	}
	const std::vector<int>& ls = GetCampaign( c ).levels;
	int pos = PositionInCampaign( m_levelIndex );
	return pos + 1 < (int)ls.size() ? ls[pos + 1] : -1;
}

void Game::LoadAttractFor( int campaign )
{
	std::vector<int> pool;
	if ( campaign >= 0 && campaign < CampaignCount() )
	{
		pool = GetCampaign( campaign ).levels;
	}
	if ( pool.empty() )
	{
		for ( int i = 0; i < LevelCount(); ++i )
		{
			pool.push_back( i );
		}
	}
	// avoid showing the same siege twice in a row
	if ( pool.size() > 1 && m_attract )
	{
		pool.erase( std::remove( pool.begin(), pool.end(), m_levelIndex ), pool.end() );
	}
	LoadLevel( pool[FxRng().Int( 0, (int)pool.size() - 1 )], true );
}

// ---------------------------------------------------------------------------------------------
// Campaign progress
// ---------------------------------------------------------------------------------------------

int Game::CampaignStars( int campaign ) const
{
	int n = 0;
	for ( int l : GetCampaign( campaign ).levels )
	{
		n += m_progress.stars[l];
	}
	return n;
}

int Game::CampaignMaxStars( int campaign ) const
{
	return 3 * (int)GetCampaign( campaign ).levels.size();
}

int Game::StarsToUnlock( int campaign ) const
{
	if ( campaign <= 0 )
	{
		return 0;
	}
	// half the stars of the previous realm open the next one
	return ( CampaignMaxStars( campaign - 1 ) + 1 ) / 2;
}

bool Game::CampaignUnlocked( int campaign ) const
{
	if ( campaign < 0 || campaign >= CampaignCount() || GetCampaign( campaign ).levels.empty() )
	{
		return false;
	}
	if ( m_debug || campaign == 0 )
	{
		return true;
	}
	// a realm where stars were already won stays open, even if the one before has grown new levels since
	if ( CampaignStars( campaign ) > 0 )
	{
		return true;
	}
	return CampaignUnlocked( campaign - 1 ) && CampaignStars( campaign - 1 ) >= StarsToUnlock( campaign );
}

bool Game::LevelUnlocked( int levelIndex ) const
{
	if ( m_debug )
	{
		return true;
	}
	int c = CampaignOfLevel( levelIndex );
	if ( c < 0 || CampaignUnlocked( c ) == false )
	{
		return false;
	}
	int pos = PositionInCampaign( levelIndex );
	return pos == 0 || m_progress.stars[GetCampaign( c ).levels[pos - 1]] > 0 || m_progress.stars[levelIndex] > 0;
}

int Game::ContinueLevel() const
{
	for ( int c = 0; c < CampaignCount(); ++c )
	{
		if ( CampaignUnlocked( c ) == false )
		{
			continue;
		}
		for ( int l : GetCampaign( c ).levels )
		{
			if ( LevelUnlocked( l ) && m_progress.stars[l] == 0 )
			{
				return l;
			}
		}
	}
	return -1;
}

void Game::ShowStory( int campaign, bool outro, int playAfter )
{
	m_storyCampaign = campaign;
	m_storyOutro = outro;
	m_storyPlay = playAfter;
	if ( campaign >= 0 && campaign < Progress::kMaxCampaigns )
	{
		( outro ? m_progress.outroSeen : m_progress.introSeen )[campaign] = true;
		SaveProgress();
	}
	SetScreen( Screen::Story );
}

void Game::OpenCampaign( int campaign )
{
	if ( CampaignUnlocked( campaign ) == false )
	{
		return;
	}
	m_campaign = campaign;
	if ( m_progress.introSeen[campaign] == false )
	{
		ShowStory( campaign, false );
		return;
	}
	SetScreen( Screen::LevelSelect );
}

void Game::SetScreen( Screen s )
{
	m_prevScreen = m_screen;
	m_screen = s;
	m_screenTime = 0.0f;
	if ( m_headless )
	{
		return;
	}

	m_hadLock = false;
	m_lockAttempts = 0;
	if ( s == Screen::Playing )
	{
		DisableCursor();
	}
	else
	{
		EnableCursor();
	}

	if ( s == Screen::Won || s == Screen::Lost )
	{
		// a slow victory lap around the ruins
		m_camMode = CamMode::Attract;
		Vector3 d = Vector3Subtract( m_camera.position, m_fortressCenter );
		m_orbitYaw = atan2f( d.x, d.z );
	}

	// menus play a demo siege behind them, in the realm they are about
	bool fresh = m_attract == false || m_scene.IsValid() == false;
	if ( s == Screen::Title || s == Screen::Map || s == Screen::HowTo )
	{
		if ( fresh )
		{
			LoadAttractFor( -1 );
		}
	}
	else if ( s == Screen::LevelSelect )
	{
		if ( fresh || CampaignOfLevel( m_levelIndex ) != m_campaign )
		{
			LoadAttractFor( m_campaign );
		}
	}
	else if ( s == Screen::Story )
	{
		if ( fresh || CampaignOfLevel( m_levelIndex ) != m_storyCampaign )
		{
			LoadAttractFor( m_storyCampaign );
		}
	}
}

// ---------------------------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------------------------

static const char* SavePath()
{
	return TextFormat( "%scrollo_save.txt", GetApplicationDirectory() );
}

// Save file, one "key value..." per line. Levels are stored by id; the old format ("level <index> <stars>",
// "best <index> <score>") used the order of the level table, which the ids keep, so it still loads.
static void ReadSave( FILE* f, Progress& p )
{
	char line[256];
	while ( fgets( line, sizeof( line ), f ) )
	{
		char key[64] = {}, arg[64] = {};
		int v = 0;
		if ( sscanf( line, "%63s %63s %d", key, arg, &v ) != 3 )
		{
			continue;
		}
		char* end = nullptr;
		long num = strtol( arg, &end, 10 );
		bool numeric = end != arg && *end == 0;
		int level = numeric ? (int)num : FindLevelById( arg );
		bool levelOk = level >= 0 && level < LevelCount() && level < Progress::kMaxLevels;
		bool campOk = numeric && num >= 0 && num < Progress::kMaxCampaigns;

		if ( ( strcmp( key, "star" ) == 0 || strcmp( key, "level" ) == 0 ) && levelOk )
		{
			p.stars[level] = std::max( p.stars[level], std::max( 0, std::min( 3, v ) ) );
		}
		else if ( strcmp( key, "best" ) == 0 && levelOk )
		{
			p.best[level] = std::max( p.best[level], v );
		}
		else if ( strcmp( key, "intro" ) == 0 && campOk )
		{
			p.introSeen[num] = v != 0;
		}
		else if ( strcmp( key, "outro" ) == 0 && campOk )
		{
			p.outroSeen[num] = v != 0;
		}
		else if ( strcmp( key, "challenge" ) == 0 && numeric )
		{
			p.bestChallenge = (int)num;
			p.bestRound = v;
		}
		else if ( strcmp( key, "opt" ) == 0 && numeric )
		{
			if ( num == 0 )
				p.shadows = v != 0;
			if ( num == 1 )
				p.music = v != 0;
			if ( num == 2 )
				p.sfx = v != 0;
			if ( num == 3 )
				p.aimAssist = v != 0;
		}
	}
}

static void WriteSave( FILE* f, const Progress& p )
{
	fprintf( f, "version 2 0\n" );
	for ( int i = 0; i < LevelCount() && i < Progress::kMaxLevels; ++i )
	{
		if ( p.stars[i] > 0 || p.best[i] > 0 )
		{
			fprintf( f, "star %s %d\n", GetLevel( i ).id, p.stars[i] );
			fprintf( f, "best %s %d\n", GetLevel( i ).id, p.best[i] );
		}
	}
	for ( int c = 0; c < Progress::kMaxCampaigns; ++c )
	{
		if ( p.introSeen[c] )
			fprintf( f, "intro %d 1\n", c );
		if ( p.outroSeen[c] )
			fprintf( f, "outro %d 1\n", c );
	}
	fprintf( f, "challenge %d %d\n", p.bestChallenge, p.bestRound );
	fprintf( f, "opt 0 %d\nopt 1 %d\nopt 2 %d\nopt 3 %d\n", p.shadows, p.music, p.sfx, p.aimAssist );
}

void Game::LoadProgress()
{
	if ( m_headless )
	{
		return;
	}
#if defined( __EMSCRIPTEN__ )
	WebLoadSave( SavePath() );
#endif
	FILE* f = fopen( SavePath(), "r" );
	if ( f == nullptr )
	{
		return;
	}
	ReadSave( f, m_progress );
	fclose( f );
}

void Game::SaveProgress()
{
	// attract-mode sieges never reach CheckOutcome's bookkeeping, so saving from the menus is safe
	if ( m_headless || m_inputEnabled == false )
	{
		return;
	}
	FILE* f = fopen( SavePath(), "w" );
	if ( f == nullptr )
	{
		return;
	}
	WriteSave( f, m_progress );
	fclose( f );
#if defined( __EMSCRIPTEN__ )
	WebStoreSave( SavePath() );
#endif
}

void Game::TestCampaigns()
{
	int failures = 0;
	auto check = [&]( bool ok, const char* what ) {
		printf( "  %-62s %s\n", what, ok ? "ok" : "FALLITO" );
		failures += ok ? 0 : 1;
	};

	// every level has a unique id and belongs to exactly one campaign
	bool idsOk = true, ownedOk = true;
	for ( int i = 0; i < LevelCount(); ++i )
	{
		idsOk = idsOk && GetLevel( i ).id && FindLevelById( GetLevel( i ).id ) == i;
		int owners = 0;
		for ( int c = 0; c < CampaignCount(); ++c )
		{
			for ( int l : GetCampaign( c ).levels )
			{
				owners += l == i ? 1 : 0;
			}
		}
		ownedOk = ownedOk && owners == 1;
	}
	check( idsOk, "id dei livelli unici" );
	check( ownedOk, "ogni livello sta in una sola campagna" );
	check( CampaignCount() == 6 && CampaignCount() <= Progress::kMaxCampaigns, "sei campagne" );
	check( LevelCount() <= Progress::kMaxLevels, "spazio per tutti i livelli nel salvataggio" );

	// an old save (by index) migrates, and the new format reads back the same
	const char* legacy = "level 0 3\nbest 0 9000\nlevel 1 2\nbest 1 7000\nlevel 5 1\nbest 5 100\nchallenge 4200 3\nopt 1 0\n";
	FILE* f = tmpfile();
	fputs( legacy, f );
	rewind( f );
	Progress old;
	ReadSave( f, old );
	fclose( f );
	check( old.stars[0] == 3 && old.stars[1] == 2 && old.stars[5] == 1 && old.best[1] == 7000, "salvataggio vecchio migrato" );
	check( old.bestChallenge == 4200 && old.bestRound == 3 && old.music == false, "record e opzioni migrati" );
	old.introSeen[1] = true;
	old.outroSeen[0] = true;
	f = tmpfile();
	WriteSave( f, old );
	rewind( f );
	Progress back;
	ReadSave( f, back );
	fclose( f );
	bool same = memcmp( back.stars, old.stars, sizeof( old.stars ) ) == 0 && memcmp( back.best, old.best, sizeof( old.best ) ) == 0 &&
				back.introSeen[1] && back.outroSeen[0] && back.introSeen[0] == false && back.music == false;
	check( same, "formato nuovo: scrivi e rileggi" );

	// unlock rules
	Progress saved = m_progress;
	bool debug = m_debug;
	m_debug = false;
	m_progress = Progress();
	const Campaign& c0 = GetCampaign( 0 );
	check( LevelUnlocked( c0.levels[0] ) && LevelUnlocked( c0.levels[1] ) == false, "primo livello aperto, secondo chiuso" );
	check( CampaignUnlocked( 1 ) == false, "seconda campagna chiusa all'inizio" );
	check( ContinueLevel() == c0.levels[0], "CONTINUA parte dal primo livello" );
	m_progress.stars[c0.levels[0]] = 1;
	check( LevelUnlocked( c0.levels[1] ) && ContinueLevel() == c0.levels[1], "vincere apre il livello dopo" );
	// half the stars of the Prati Alti, rounded up, open the next realm
	int need = StarsToUnlock( 1 );
	auto spread = [&]( int total ) {
		for ( int l : c0.levels )
		{
			m_progress.stars[l] = std::min( 2, total );
			total -= m_progress.stars[l];
		}
	};
	spread( need - 1 );
	check( CampaignUnlocked( 1 ) == false, TextFormat( "%d stelle su %d non bastano", need - 1, CampaignMaxStars( 0 ) ) );
	spread( need );
	check( CampaignUnlocked( 1 ) && need == ( CampaignMaxStars( 0 ) + 1 ) / 2,
		   TextFormat( "%d stelle su %d aprono la Valle dei Mulini", need, CampaignMaxStars( 0 ) ) );
	check( CampaignUnlocked( 3 ) == false, "le campagne senza livelli restano chiuse" );
	check( CampaignUnlocked( 2 ) == false, "i Picchi Gelati chiusi senza stelle nella Valle dei Mulini" );
	m_progress.stars[GetCampaign( 2 ).levels[0]] = 1;
	check( CampaignUnlocked( 2 ), "un regno dove hai già stelle resta aperto anche se il precedente è cresciuto" );
	m_debug = true;
	check( CampaignUnlocked( 2 ) && LevelUnlocked( GetCampaign( 2 ).levels.back() ), "--debug apre tutto" );
	m_debug = debug;
	m_progress = saved;

	printf( "Campagne: %s\n", failures == 0 ? "tutto ok" : TextFormat( "%d controlli falliti", failures ) );
}

// ---------------------------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------------------------

Vector3 Game::AimDir() const
{
	return CannonAimDir( m_yaw, m_pitch );
}

Vector3 Game::Muzzle() const
{
	Vector3 pivot = Vector3Add( m_cannonPos, { 0, 0.95f, 0 } );
	return Vector3Add( pivot, Vector3Scale( AimDir(), 1.8f ) );
}

float Game::LaunchSpeed() const
{
	return kMinSpeed + ( kMaxSpeed - kMinSpeed ) * m_power;
}

int Game::AmmoLeft() const
{
	int n = 0;
	for ( int i = 0; i < (int)Ammo::Count; ++i )
	{
		n += m_ammo[i];
	}
	return n;
}

bool Game::AnyProjectileFlying() const
{
	for ( const Entity* e : m_scene.entities )
	{
		if ( e->alive && e->kind == Kind::Projectile && e->restTimer < 0.8f && e->age < 12.0f && e->pos.y > -30.0f )
		{
			return true;
		}
		if ( e->alive && e->kind == Kind::Projectile && e->ammo == (int)Ammo::Sticky && e->fuse >= 0.0f )
		{
			return true;
		}
	}
	return false;
}

void Game::SelectAmmo( int index )
{
	if ( index < 0 || index >= (int)Ammo::Count )
	{
		return;
	}
	if ( m_ammo[index] > 0 && index != m_selected )
	{
		m_selected = index;
		if ( m_audio )
		{
			m_audio->Play( Sfx::Click, 0.5f, 1.2f );
		}
	}
}

int Game::ComputeStars() const
{
	if ( m_level == nullptr )
	{
		return 1;
	}
	if ( m_shots <= m_level->par )
	{
		return 3;
	}
	if ( m_shots <= m_level->par + 1 )
	{
		return 2;
	}
	return 1;
}

void Game::AddScore( int points, Vector3 where, bool showText )
{
	m_score += points;
	if ( showText )
	{
		AddText( where, TextFormat( "+%d", points ), kGold, 1.0f );
	}
}

void Game::AddText( Vector3 pos, const std::string& text, Color color, float scale )
{
	if ( m_headless )
	{
		return;
	}
	m_texts.push_back( { pos, text, color, 1.6f, 1.6f, scale } );
}

void Game::Shake( float amount )
{
	m_shake = std::max( m_shake, amount );
}

void Game::Kill( Entity* e )
{
	if ( e == nullptr || e->alive == false )
	{
		return;
	}
	if ( e == m_focus )
	{
		if ( e->pos.y > m_fortressCenter.y - 6.0f )
		{
			m_focusLast = e->pos;
		}
		m_focus = nullptr;
	}
	for ( Entity* other : m_scene.entities )
	{
		if ( other->partner == e )
		{
			other->partner = nullptr;
		}
	}
	if ( e->kind == Kind::King )
	{
		m_kings.erase( std::remove( m_kings.begin(), m_kings.end(), e ), m_kings.end() );
	}
	m_scene.DestroyEntity( e );
}

// ---------------------------------------------------------------------------------------------
// Firing
// ---------------------------------------------------------------------------------------------

void Game::Fire()
{
	if ( m_reload > 0.0f || m_ammo[m_selected] <= 0 )
	{
		return;
	}
	if ( Cheating() == false )
	{
		m_ammo[m_selected] -= 1;
	}
	m_shots += 1;
	if ( getenv( "CROLLO_DEBUG" ) )
		fprintf( stderr, "Fire: yaw %.2f pitch %.2f power %.2f t=%.2f\n", m_yaw, m_pitch, m_power, m_screenTime );
	Ammo type = (Ammo)m_selected;
	// every shot starts a fresh Box3D recording, seeded with a snapshot of the world
	RestartRecording();
	FireProjectile( type, Muzzle(), AimDir(), LaunchSpeed() );
	m_shotSerials.clear();
	m_shotPartner = m_focus && m_focus->partner ? m_focus->partner->serial : 0;
	if ( m_focus )
	{
		m_shotSerials.push_back( m_focus->serial );
	}
	m_shotDir = AimDir();
	AddReplayEvent( ReplayEvent::CannonFire, Muzzle(), AimDir() );

	if ( m_ammo[m_selected] == 0 )
	{
		for ( int i = 0; i < (int)Ammo::Count; ++i )
		{
			if ( m_ammo[i] > 0 )
			{
				m_selected = i;
				break;
			}
		}
	}
}

void Game::FireProjectile( Ammo type, Vector3 muzzle, Vector3 dir, float speed )
{
	m_windPending = m_windShift > 0.0f;
	m_recoil = 0.45f;
	m_reload = 0.9f;
	m_sinceShot = 0.0f;
	m_calmTime = 0.0f;

	BodyOptions bo;
	bo.bullet = true;
	bo.angularDamping = 0.1f;
	ShapeOptions so;
	so.category = CatProjectile;

	Entity* first = nullptr;
	Rng& r = FxRng();

	switch ( type )
	{
		case Ammo::Ball:
		{
			bo.velocity = Vector3Scale( dir, speed );
			Entity* e = m_scene.CreateEntity( Kind::Projectile, Mat::Metal, muzzle, b3Quat_identity, bo );
			so.rollingResistance = 0.08f;
			m_scene.AddSphere( e, { 0, 0, 0 }, 0.3f, Mat::Metal, so );
			first = e;
			break;
		}
		case Ammo::Bomb:
		{
			bo.velocity = Vector3Scale( dir, speed );
			bo.angularVelocity = Vector3Scale( r.OnSphere(), 3.0f );
			Entity* e = m_scene.CreateEntity( Kind::Projectile, Mat::Dark, muzzle, b3Quat_identity, bo );
			so.densityScale = 3.0f;
			so.contactEvents = true;
			m_scene.AddSphere( e, { 0, 0, 0 }, 0.34f, Mat::Dark, so );
			Part cap;
			cap.geo = Geo::Box;
			cap.localPos = { 0, 0.34f, 0 };
			cap.size = { 0.09f, 0.07f, 0.09f };
			cap.mat = Mat::Metal;
			cap.tint = Color{ 110, 110, 118, 255 };
			m_scene.AddVisual( e, cap );
			Part fuse;
			fuse.geo = Geo::Sphere;
			fuse.localPos = { 0, 0.45f, 0 };
			fuse.size = { 0.06f, 0.06f, 0.06f };
			fuse.mat = Mat::Plain;
			fuse.tint = Color{ 255, 180, 60, 255 };
			m_scene.AddVisual( e, fuse );
			first = e;
			break;
		}
		case Ammo::Cluster:
		{
			bo.velocity = Vector3Scale( dir, speed );
			Entity* e = m_scene.CreateEntity( Kind::Projectile, Mat::Metal, muzzle, b3Quat_identity, bo );
			so.densityScale = 0.7f;
			m_scene.AddSphere( e, { 0, 0, 0 }, 0.33f, Mat::Metal, so );
			e->parts.back().tint = Color{ 95, 120, 80, 255 };
			first = e;
			break;
		}
		case Ammo::Chain:
		{
			Vector3 side = Vector3Normalize( Vector3CrossProduct( dir, { 0, 1, 0 } ) );
			Vector3 axis = Vector3Normalize( Vector3CrossProduct( side, dir ) );
			Vector3 omega = Vector3Scale( axis, 11.0f );
			Vector3 v = Vector3Scale( dir, speed * 0.95f );
			Entity* balls[2];
			for ( int i = 0; i < 2; ++i )
			{
				Vector3 offset = Vector3Scale( side, i == 0 ? -0.6f : 0.6f );
				BodyOptions cb = bo;
				cb.velocity = Vector3Add( v, Vector3CrossProduct( omega, offset ) );
				Entity* e = m_scene.CreateEntity( Kind::Projectile, Mat::Metal, Vector3Add( muzzle, offset ), b3Quat_identity, cb );
				m_scene.AddSphere( e, { 0, 0, 0 }, 0.24f, Mat::Metal, so );
				balls[i] = e;
			}
			balls[0]->partner = balls[1];
			balls[1]->partner = balls[0];

			b3DistanceJointDef jd = b3DefaultDistanceJointDef();
			jd.base.bodyIdA = balls[0]->body;
			jd.base.bodyIdB = balls[1]->body;
			jd.length = 1.2f;
			b3JointId j = b3CreateDistanceJoint( m_scene.World(), &jd );
			Rope& rope = m_scene.AddRope( balls[0]->body, balls[0]->pos, balls[1]->body, balls[1]->pos, 0.05f, Color{ 90, 90, 98, 255 } );
			rope.joint = j;
			for ( int i = 0; i < 2; ++i )
			{
				balls[i]->ammo = (int)type;
				m_scene.FinalizeEntity( balls[i] );
			}
			first = balls[0];
			break;
		}
		case Ammo::Boulder:
		{
			bo.velocity = Vector3Scale( dir, speed * 0.82f );
			bo.angularVelocity = Vector3Scale( r.OnSphere(), 2.0f );
			Entity* e = m_scene.CreateEntity( Kind::Projectile, Mat::Rock, muzzle, b3Quat_identity, bo );
			so.densityScale = 1.1f;
			m_scene.AddHull( e, { 0, 0, 0 }, b3Quat_identity, m_scene.RockHull( 0.62f, r.Next() ), Mat::Rock, so );
			e->parts.back().tint = Color{ 135, 120, 105, 255 };
			first = e;
			break;
		}
		case Ammo::Implosion:
		{
			bo.velocity = Vector3Scale( dir, speed );
			bo.angularVelocity = Vector3Scale( r.OnSphere(), 4.0f );
			Entity* e = m_scene.CreateEntity( Kind::Projectile, Mat::Dark, muzzle, b3Quat_identity, bo );
			so.densityScale = 3.0f;
			so.contactEvents = true;
			m_scene.AddSphere( e, { 0, 0, 0 }, 0.34f, Mat::Dark, so );
			e->parts.back().tint = Color{ 70, 35, 110, 255 };
			Part band;
			band.geo = Geo::Sphere;
			band.size = { 0.37f, 0.08f, 0.37f };
			band.mat = Mat::Shield;
			band.tint = Color{ 190, 140, 255, 255 };
			m_scene.AddVisual( e, band );
			first = e;
			break;
		}
		case Ammo::Sticky:
		{
			bo.velocity = Vector3Scale( dir, speed );
			Entity* e = m_scene.CreateEntity( Kind::Projectile, Mat::Dark, muzzle, b3Quat_identity, bo );
			so.densityScale = 1.2f;
			so.contactEvents = true;
			m_scene.AddSphere( e, { 0, 0, 0 }, 0.3f, Mat::Dark, so );
			e->parts.back().tint = Color{ 55, 120, 55, 255 };
			for ( int k = 0; k < 6; ++k )
			{
				// blobs of glue around the shell
				Part blob;
				blob.geo = Geo::Sphere;
				float a = k * PI / 3.0f;
				blob.localPos = { cosf( a ) * 0.27f, ( k % 2 ? 0.1f : -0.12f ), sinf( a ) * 0.27f };
				blob.size = { 0.09f, 0.09f, 0.09f };
				blob.mat = Mat::Rubber;
				blob.tint = Color{ 120, 210, 90, 255 };
				m_scene.AddVisual( e, blob );
			}
			first = e;
			break;
		}
		default:
			break;
	}

	for ( Entity* e : m_scene.entities )
	{
		if ( e->kind == Kind::Projectile && e->ammo < 0 )
		{
			e->ammo = (int)type;
			e->homeY = -1000.0f;
			m_scene.FinalizeEntity( e );
		}
	}

	m_focus = first;
	m_followTime = 0.0f;
	m_watchTime = 0.0f;

	m_particles.MuzzleBlast( Vector3Add( muzzle, Vector3Scale( dir, 0.2f ) ), dir );
	if ( m_audio )
	{
		m_audio->PlayAt( Sfx::Cannon, muzzle, 1.0f, type == Ammo::Boulder ? 0.8f : FxRng().Range( 0.95f, 1.05f ) );
	}
	Shake( m_attract ? 0.1f : 0.35f );

	if ( m_attract == false && m_camMode == CamMode::Aim )
	{
		m_camMode = CamMode::Follow;
	}
}

void Game::Special( Entity* p )
{
	if ( p == nullptr || p->alive == false || p->specialUsed || p->kind != Kind::Projectile )
	{
		return;
	}
	if ( p->ammo == (int)Ammo::Bomb || p->ammo == (int)Ammo::Implosion || p->ammo == (int)Ammo::Sticky )
	{
		p->specialUsed = true;
		Detonate( p );
	}
	else if ( p->ammo == (int)Ammo::Cluster )
	{
		p->specialUsed = true;
		Vector3 pos = p->pos;
		Vector3 vel = ToRl( b3Body_GetLinearVelocity( p->body ) );
		Kill( p );
		Rng& r = FxRng();
		float speed = Vector3Length( vel );
		Vector3 dir = speed > 0.1f ? Vector3Scale( vel, 1.0f / speed ) : Vector3{ 0, 0, 1 };
		Entity* first = nullptr;
		for ( int i = 0; i < 7; ++i )
		{
			Vector3 spread = Vector3Scale( r.InSphere(), 0.16f );
			Vector3 d = Vector3Normalize( Vector3Add( dir, spread ) );
			BodyOptions bo;
			bo.bullet = true;
			bo.velocity = Vector3Scale( d, speed * r.Range( 0.9f, 1.12f ) );
			Entity* e = m_scene.CreateEntity( Kind::Projectile, Mat::Metal, Vector3Add( pos, Vector3Scale( r.InSphere(), 0.3f ) ),
											  b3Quat_identity, bo );
			ShapeOptions so;
			so.category = CatProjectile;
			so.densityScale = 1.4f;
			m_scene.AddSphere( e, { 0, 0, 0 }, 0.17f, Mat::Metal, so );
			e->parts.back().tint = Color{ 95, 120, 80, 255 };
			e->ammo = (int)Ammo::Ball;
			e->specialUsed = true;
			e->homeY = -1000.0f;
			m_scene.FinalizeEntity( e );
			if ( first == nullptr )
			{
				first = e;
			}
		}
		if ( m_focus == nullptr )
		{
			m_focus = first;
		}
		if ( first && std::find( m_shotSerials.begin(), m_shotSerials.end(), p->serial ) != m_shotSerials.end() )
		{
			m_shotSerials.push_back( first->serial );
		}
		m_particles.Sparkle( pos, Color{ 255, 220, 120, 255 }, 20 );
		m_particles.Dust( pos, Color{ 200, 200, 200, 255 }, 5, 2.0f );
		if ( m_audio )
		{
			m_audio->PlayAt( Sfx::Split, pos, 1.0f );
		}
	}
}

void Game::Detonate( Entity* e )
{
	if ( e == nullptr || e->alive == false )
	{
		return;
	}
	Vector3 pos = e->pos;
	bool tnt = e->mat == Mat::Tnt;
	bool implosion = e->kind == Kind::Projectile && e->ammo == (int)Ammo::Implosion;
	Kill( e );
	if ( tnt )
	{
		AddScore( 300, pos, false );
		Explode( pos, 4.2f, 2600.0f, true );
	}
	else if ( implosion )
	{
		Implode( pos, 4.5f, 1500.0f );
	}
	else
	{
		// a bomb is a local tool; big blasts belong to TNT (see --scan-shots)
		Explode( pos, 2.6f, 1300.0f, false );
	}
}

// A negative explosion: Box3D's b3World_Explode accepts a negative impulse, which pulls every shape
// in range towards the centre. Towers around the blast fold inwards onto it.
void Game::Implode( Vector3 pos, float radius, float impulse )
{
	AddReplayEvent( ReplayEvent::Implosion, pos, { 0, 0, 0 }, radius, false );
	b3ExplosionDef def = b3DefaultExplosionDef();
	def.position = ToB3( pos );
	def.radius = radius;
	def.falloff = radius * 0.3f;
	def.impulsePerArea = -impulse;
	def.maskBits = CatBlock | CatKing | CatProjectile | CatDebris;
	b3World_Explode( m_scene.World(), &def );

	m_particles.Implosion( pos, radius );
	if ( m_audio )
	{
		m_audio->PlayAt( Sfx::Implosion, pos, 1.0f, FxRng().Range( 0.95f, 1.05f ) );
	}
	float camDist = Vector3Distance( m_camPos, pos );
	Shake( 0.6f * Clamp01( 1.4f - camDist / 60.0f ) );
	if ( m_attract == false && m_won == false )
	{
		m_timeScale = std::min( m_timeScale, 0.5f );
	}
	for ( Entity* e : m_scene.entities )
	{
		if ( e->alive && e->isStatic == false && Vector3Distance( e->pos, pos ) < radius )
		{
			e->flash = std::max( e->flash, 0.25f );
		}
	}
}

// The sticky bomb welds itself to whatever it touched, keeping its current pose, and starts a fuse.
void Game::StickTo( Entity* bomb, Entity* other )
{
	if ( bomb->stuck || other->kind == Kind::Projectile || other->kind == Kind::Shield || other->alive == false )
	{
		return;
	}
	bomb->stuck = true;
	bomb->hasHit = true;
	bomb->fuse = 3.0f;

	b3WorldTransform xa = b3Body_GetTransform( other->body );
	b3WorldTransform xb = b3Body_GetTransform( bomb->body );
	b3WeldJointDef jd = b3DefaultWeldJointDef();
	jd.base.bodyIdA = other->body;
	jd.base.bodyIdB = bomb->body;
	jd.base.localFrameA.p = b3Body_GetLocalPoint( other->body, xb.p );
	jd.base.localFrameA.q = b3InvMulQuat( xa.q, xb.q );
	jd.base.localFrameB = b3Transform_identity;
	b3CreateWeldJoint( m_scene.World(), &jd );

	AddReplayEvent( ReplayEvent::Stick, bomb->pos );
	m_particles.Sparkle( bomb->pos, Color{ 140, 230, 100, 255 }, 10 );
	if ( m_audio )
	{
		m_audio->PlayAt( Sfx::Stick, bomb->pos, 0.9f, FxRng().Range( 0.9f, 1.1f ) );
	}
}

void Game::Explode( Vector3 pos, float radius, float impulse, bool big )
{
	if ( getenv( "CROLLO_DEBUG" ) )
		fprintf( stderr, "  esplosione a %.1f %.1f %.1f raggio %.1f\n", pos.x, pos.y, pos.z, radius );
	b3ExplosionDef def = b3DefaultExplosionDef();
	def.position = ToB3( pos );
	def.radius = radius;
	def.falloff = big ? radius * 0.8f : radius * 0.5f;
	def.impulsePerArea = impulse;
	def.maskBits = CatBlock | CatKing | CatProjectile | CatDebris;

	// Box3D pushes straight through walls: note which bodies a barrier shields, and give them back their
	// velocities once the blast has been applied (it only changes velocities).
	struct Shielded
	{
		Entity* e;
		b3Vec3 v, w;
	};
	std::vector<Shielded> shielded;
	for ( Entity* e : m_scene.entities )
	{
		if ( e->alive && e->isStatic == false && Vector3Distance( e->pos, pos ) < radius + def.falloff + 1.5f && BlastReaches( pos, e ) == false )
		{
			shielded.push_back( { e, b3Body_GetLinearVelocity( e->body ), b3Body_GetAngularVelocity( e->body ) } );
		}
	}
	b3World_Explode( m_scene.World(), &def );
	for ( const Shielded& sh : shielded )
	{
		b3Body_SetLinearVelocity( sh.e->body, sh.v );
		b3Body_SetAngularVelocity( sh.e->body, sh.w );
	}

	AddReplayEvent( ReplayEvent::Explosion, pos, { 0, 0, 0 }, radius, big );
	m_particles.Explosion( pos, radius );
	m_particles.Debris( pos, Color{ 60, 50, 45, 255 }, 14, 9.0f, 0.12f );
	if ( m_audio )
	{
		m_audio->PlayAt( Sfx::Explosion, pos, 1.0f, big ? FxRng().Range( 0.8f, 0.9f ) : FxRng().Range( 0.95f, 1.1f ) );
	}
	float camDist = Vector3Distance( m_camPos, pos );
	Shake( ( big ? 1.1f : 0.8f ) * Clamp01( 1.4f - camDist / 60.0f ) );
	m_screenFlash = std::max( m_screenFlash, ( big ? 0.35f : 0.25f ) * Clamp01( 1.3f - camDist / 50.0f ) );
	if ( m_attract == false && m_won == false )
	{
		m_timeScale = std::min( m_timeScale, 0.45f );
	}

	for ( size_t i = 0; i < m_scene.entities.size(); ++i )
	{
		Entity* e = m_scene.entities[i];
		if ( e->alive == false || e->isStatic )
		{
			continue;
		}
		float d = Vector3Distance( e->pos, pos );
		if ( d > radius * 1.3f || BlastReaches( pos, e ) == false )
		{
			continue;
		}
		if ( e->mat == Mat::Tnt && e->kind == Kind::Block && d < radius * 1.3f && e->fuse < 0.0f )
		{
			e->fuse = 0.12f + FxRng().Range( 0.0f, 0.18f );
		}
		if ( e->mat == Mat::Ice && e->kind == Kind::Block && d < radius )
		{
			e->breakQueued = true;
		}
		if ( e->kind == Kind::Balloon && d < radius * 1.2f )
		{
			PopBalloon( e );
		}
		if ( e->kind == Kind::King && e->defeated == false && d < radius * 0.5f )
		{
			DefeatKing( e, "boom" );
		}
		if ( d < radius )
		{
			e->flash = std::max( e->flash, 0.35f * ( 1.0f - d / radius ) );
		}
	}

	for ( Rope& rope : m_scene.ropes )
	{
		if ( rope.breakable == false || B3_IS_NULL( rope.joint ) || b3Joint_IsValid( rope.joint ) == false )
		{
			continue;
		}
		if ( b3Body_IsValid( rope.bodyA ) == false )
		{
			continue;
		}
		Vector3 p = ToRl( b3Body_GetWorldPoint( rope.bodyA, rope.localA ) );
		if ( Vector3Distance( p, pos ) < radius * 0.8f )
		{
			b3DestroyJoint( rope.joint, true );
		}
	}
}

bool Game::BlastReaches( Vector3 pos, const Entity* e ) const
{
	b3QueryFilter filter = b3DefaultQueryFilter();
	filter.categoryBits = CatAll;
	filter.maskBits = CatStatic | CatShield | CatBarrier;
	// a king is reached if its feet, middle or head is in the open; anything else, its centre
	const float kingHeights[3] = { 0.25f, 0.6f, 1.08f };
	const float centre[1] = { 0.0f };
	bool king = e->kind == Kind::King;
	const float* heights = king ? kingHeights : centre;
	int count = king ? 3 : 1;
	for ( int i = 0; i < count; ++i )
	{
		Vector3 p = Vector3Add( e->pos, Vector3RotateByQuaternion( { 0, heights[i], 0 }, e->rot ) );
		b3RayResult hit = b3World_CastRayClosest( m_scene.World(), ToB3( pos ), ToB3( Vector3Subtract( p, pos ) ), filter );
		if ( hit.hit == false )
		{
			return true;
		}
	}
	return false;
}

void Game::Shatter( Entity* e )
{
	if ( e == nullptr || e->alive == false || e->parts.empty() )
	{
		return;
	}
	Vector3 pos = e->pos;
	Quaternion rot = e->rot;
	Vector3 half = e->parts[0].size;
	Vector3 vel = ToRl( b3Body_GetLinearVelocity( e->body ) );
	Color tint = e->parts[0].tint;
	Kill( e );

	Rng& r = FxRng();
	for ( int i = -1; i <= 1; i += 2 )
	{
		for ( int j = -1; j <= 1; j += 2 )
		{
			for ( int k = -1; k <= 1; k += 2 )
			{
				Vector3 local{ i * half.x * 0.5f, j * half.y * 0.5f, k * half.z * 0.5f };
				Vector3 wp = Vector3Add( pos, Vector3RotateByQuaternion( local, rot ) );
				BodyOptions bo;
				Vector3 outward = Vector3RotateByQuaternion( Vector3Normalize( local ), rot );
				bo.velocity = Vector3Add( vel, Vector3Add( Vector3Scale( outward, r.Range( 1.0f, 3.0f ) ), { 0, 1.0f, 0 } ) );
				bo.angularVelocity = Vector3Scale( r.OnSphere(), r.Range( 2.0f, 6.0f ) );
				Entity* s = m_scene.CreateEntity( Kind::Shard, Mat::Ice, wp, ToB3( rot ), bo );
				ShapeOptions so;
				so.category = CatDebris;
				so.hitEvents = false;
				Vector3 h{ half.x * 0.46f, half.y * 0.46f, half.z * 0.46f };
				m_scene.AddBox( s, { 0, 0, 0 }, b3Quat_identity, h, Mat::Ice, so );
				s->parts.back().tint = tint;
				s->homeY = -1000.0f;
				m_scene.FinalizeEntity( s );
				s->age = r.Range( 0.0f, 1.5f );
			}
		}
	}

	AddReplayEvent( ReplayEvent::Shatter, pos );
	m_particles.Sparkle( pos, Color{ 200, 240, 255, 255 }, 18 );
	m_particles.Debris( pos, Color{ 200, 235, 255, 255 }, 10, 5.0f, 0.1f );
	if ( m_audio )
	{
		m_audio->PlayAt( Sfx::IceBreak, pos, 0.9f, r.Range( 0.9f, 1.15f ) );
	}
	AddScore( 250, pos, false );
}

void Game::Crumble( Entity* e )
{
	if ( e == nullptr || e->alive == false || e->parts.empty() )
	{
		return;
	}
	Vector3 pos = e->pos;
	Quaternion rot = e->rot;
	Vector3 half = e->parts[0].size;
	Color tint = e->parts[0].tint;
	Vector3 blow = ToRl( e->lastVel );
	Kill( e );

	// split into blocks of about half a metre, driven on by the blow
	Rng& r = FxRng();
	int nx = std::max( 1, (int)roundf( half.x * 2.0f / 0.6f ) );
	int ny = std::max( 1, (int)roundf( half.y * 2.0f / 0.6f ) );
	int nz = std::max( 1, (int)roundf( half.z * 2.0f / 0.6f ) );
	Vector3 cell{ half.x / nx, half.y / ny, half.z / nz };
	for ( int i = 0; i < nx; ++i )
	{
		for ( int j = 0; j < ny; ++j )
		{
			for ( int k = 0; k < nz; ++k )
			{
				Vector3 local{ -half.x + cell.x * ( 2 * i + 1 ), -half.y + cell.y * ( 2 * j + 1 ), -half.z + cell.z * ( 2 * k + 1 ) };
				BodyOptions bo;
				bo.velocity = Vector3Add( Vector3Scale( r.OnSphere(), r.Range( 0.5f, 2.0f ) ), Vector3Scale( blow, r.Range( 0.3f, 0.6f ) ) );
				bo.angularVelocity = Vector3Scale( r.OnSphere(), r.Range( 1.0f, 4.0f ) );
				Entity* c = m_scene.CreateEntity( Kind::Block, Mat::Stone, Vector3Add( pos, Vector3RotateByQuaternion( local, rot ) ), ToB3( rot ), bo );
				ShapeOptions so;
				so.category = CatBlock;
				m_scene.AddBox( c, { 0, 0, 0 }, b3Quat_identity, Vector3Scale( cell, 0.9f ), Mat::Stone, so );
				c->parts.back().tint = ColorBrightness( tint, r.Range( -0.15f, 0.05f ) );
				c->homeY = -1000.0f;
				m_scene.FinalizeEntity( c );
			}
		}
	}

	AddReplayEvent( ReplayEvent::Shatter, pos );
	m_particles.Debris( pos, Color{ 150, 140, 130, 255 }, 24, 7.0f, 0.14f );
	if ( m_audio )
	{
		m_audio->PlayAt( Sfx::StoneHit, pos, 1.0f, r.Range( 0.6f, 0.75f ) );
		m_audio->PlayAt( Sfx::MetalHit, pos, 0.8f, r.Range( 0.7f, 0.85f ) );
	}
	AddScore( 500, pos, false );
}

void Game::FellTree( Entity* e )
{
	if ( e == nullptr || e->alive == false )
	{
		return;
	}
	Vector3 base = e->pos;
	Quaternion rot = e->rot;
	float scale = e->tree;
	bool pine = e->pine;
	Color leaf = e->leaf;
	Vector3 blow = ToRl( e->lastVel );
	Kill( e );

	// the stump stays where it was
	float cut = 0.3f * scale;
	BodyOptions sb;
	sb.type = b3_staticBody;
	Entity* stump = m_scene.CreateEntity( Kind::Static, Mat::Wood, base, ToB3( rot ), sb );
	float radius = ( pine ? 0.12f : 0.14f ) * scale;
	ShapeOptions so;
	so.category = CatStatic;
	so.visible = false;
	m_scene.AddBox( stump, { 0, cut * 0.5f - 0.02f, 0 }, b3Quat_identity, { radius * 0.9f, cut * 0.5f - 0.02f, radius * 0.9f }, Mat::Wood, so );
	Part p;
	p.geo = Geo::Cylinder;
	p.size = { radius, cut, radius };
	p.mat = Mat::Wood;
	p.tint = pine ? Color{ 100, 70, 45, 255 } : Color{ 110, 76, 48, 255 };
	m_scene.AddVisual( stump, p );
	Part ring = p;
	ring.localPos = { 0, cut, 0 };
	ring.size = { radius * 0.8f, 0.01f, radius * 0.8f };
	ring.mat = Mat::Plain;
	ring.tint = Color{ 222, 190, 140, 255 };
	m_scene.AddVisual( stump, ring );
	m_scene.FinalizeEntity( stump );

	// the rest topples over the way the chain was going, pivoting on the stump. It lets the shot that
	// cut it through for a moment, then it is solid again, and heavy enough to knock a king down.
	Vector3 dir = { blow.x, 0.0f, blow.z };
	dir = Vector3Length( dir ) > 0.1f ? Vector3Normalize( dir ) : Vector3{ 0, 0, 1 };
	Vector3 cutPoint = Vector3Add( base, Vector3RotateByQuaternion( { 0, cut, 0 }, rot ) );
	BodyOptions bo;
	bo.angularDamping = 0.3f;
	Entity* t = m_scene.CreateEntity( Kind::Block, Mat::Wood, cutPoint, ToB3( rot ), bo );
	ShapeOptions to;
	to.category = CatBlock;
	to.mask = CatAll & ~(uint64_t)CatProjectile;
	m_scene.AddTree( t, scale, pine, leaf, cut, to );
	t->tree = 0.0f;
	t->lethal = true;
	t->ghost = 0.5f;
	t->homeY = -1000.0f;
	m_scene.FinalizeEntity( t );
	Vector3 omega = Vector3Scale( Vector3CrossProduct( { 0, 1, 0 }, dir ), 1.3f );
	Vector3 arm = Vector3Subtract( ToRl( b3Body_GetWorldCenter( t->body ) ), cutPoint );
	b3Body_SetAngularVelocity( t->body, ToB3( omega ) );
	b3Body_SetLinearVelocity( t->body, ToB3( Vector3Add( Vector3CrossProduct( omega, arm ), Vector3Scale( dir, 0.6f ) ) ) );

	AddReplayEvent( ReplayEvent::Fell, cutPoint, { leaf.r / 255.0f, leaf.g / 255.0f, leaf.b / 255.0f }, scale );
	TreeFallEffects( cutPoint, leaf, scale );
	AddScore( 300, cutPoint, false );
}

void Game::TreeFallEffects( Vector3 cutPoint, Color leaf, float scale )
{
	m_particles.Debris( cutPoint, Color{ 186, 140, 90, 255 }, 14, 4.0f, 0.07f );
	m_particles.Debris( Vector3Add( cutPoint, { 0, 1.6f * scale, 0 } ), leaf, 18, 3.0f, 0.1f );
	if ( m_audio )
	{
		m_audio->PlayAt( Sfx::WoodHit, cutPoint, 1.0f, 0.6f );
		m_audio->PlayAt( Sfx::RopeSnap, cutPoint, 0.8f, 0.65f );
	}
}

void Game::BreakBlades( Entity* blades )
{
	for ( Mechanism& m : m_scene.mechanisms )
	{
		if ( m.type != MechType::Windmill || m.entity != blades )
		{
			continue;
		}
		if ( b3Joint_IsValid( m.joint ) )
		{
			b3DestroyJoint( m.joint, true );
		}
		m.joint = b3_nullJointId;
		m.entity = nullptr;
		m_particles.Debris( blades->pos, Color{ 186, 128, 74, 255 }, 20, 6.0f, 0.12f );
		if ( m_audio )
		{
			m_audio->PlayAt( Sfx::WoodHit, blades->pos, 1.0f, 0.6f );
			m_audio->PlayAt( Sfx::RopeSnap, blades->pos, 0.9f, 0.7f );
		}
		AddScore( 500, blades->pos, false );
	}
}

void Game::DefeatKing( Entity* king, const char* reason )
{
	if ( king == nullptr || king->defeated )
	{
		return;
	}
	if ( getenv( "CROLLO_DEBUG" ) )
		fprintf( stderr, "King down (%s) at %.2f %.2f %.2f t=%.2f shots=%d\n", reason, king->pos.x, king->pos.y, king->pos.z, m_levelTime, m_shots );
	king->defeated = true;
	king->defeatTime = m_levelTime;
	m_kingsDown += 1;

	Vector3 head = Vector3Add( king->pos, Vector3RotateByQuaternion( { 0, 1.3f, 0 }, king->rot ) );
	m_scene.visuals[king->serial].defeatStep = m_scene.stepCount;
	AddReplayEvent( ReplayEvent::KingDown, head );
	AddScore( 5000, head, true );
	m_particles.Stars( head, 14 );
	if ( m_audio )
	{
		m_audio->PlayAt( Sfx::KingDown, head, 1.0f, FxRng().Range( 0.9f, 1.1f ) );
	}

	// knocked out: eyes shut
	for ( Part& p : king->parts )
	{
		if ( p.mat == Mat::Dark )
		{
			p.size.y = 0.012f;
			p.size.x = 0.05f;
		}
	}

	// the crown pops off as its own little rigid body
	if ( king->crownIndex >= 0 && king->crownIndex < (int)king->parts.size() && king->alive )
	{
		Part& crownPart = king->parts[king->crownIndex];
		if ( crownPart.visible )
		{
			crownPart.visible = false;
			Vector3 cp = Vector3Add( king->pos, Vector3RotateByQuaternion( crownPart.localPos, king->rot ) );
			BodyOptions bo;
			Vector3 kv = ToRl( b3Body_GetLinearVelocity( king->body ) );
			bo.velocity = Vector3Add( kv, { FxRng().Range( -1.5f, 1.5f ), 4.5f, FxRng().Range( -1.5f, 1.5f ) } );
			bo.angularVelocity = Vector3Scale( FxRng().OnSphere(), 6.0f );
			Entity* crown = m_scene.CreateEntity( Kind::Crown, Mat::Gold, cp, ToB3( king->rot ), bo );
			ShapeOptions so;
			so.category = CatDebris;
			so.hitEvents = false;
			m_scene.AddHull( crown, { 0, 0, 0 }, b3Quat_identity, crownPart.hull, Mat::Gold, so );
			crown->homeY = -1000.0f;
			m_scene.FinalizeEntity( crown );
		}
	}
}

void Game::PopBalloon( Entity* balloon )
{
	if ( balloon == nullptr || balloon->alive == false )
	{
		return;
	}
	Vector3 pos = balloon->pos;
	Color c = balloon->parts.empty() ? RED : balloon->parts[0].tint;
	Kill( balloon );
	AddReplayEvent( ReplayEvent::BalloonPop, pos, { c.r / 255.0f, c.g / 255.0f, c.b / 255.0f } );
	m_particles.Debris( pos, c, 22, 7.0f, 0.18f );
	m_particles.Dust( pos, Color{ 255, 255, 255, 255 }, 6, 2.5f );
	if ( m_audio )
	{
		m_audio->PlayAt( Sfx::Pop, pos, 1.0f, FxRng().Range( 0.9f, 1.1f ) );
	}
	AddScore( 500, pos, true );
}

// ---------------------------------------------------------------------------------------------
// Simulation
// ---------------------------------------------------------------------------------------------

void Game::FixedStep()
{
	const float dt = kFixedDt;

	// wind pushes projectiles and balloons
	for ( Entity* e : m_scene.entities )
	{
		if ( e->alive == false )
		{
			continue;
		}
		if ( e->kind == Kind::Projectile && e->hasHit == false )
		{
			b3Body_ApplyForceToCenter( e->body, ToB3( Vector3Scale( m_wind, e->mass ) ), false );
		}
		else if ( e->kind == Kind::Balloon )
		{
			b3Body_ApplyForceToCenter( e->body, ToB3( Vector3Scale( m_wind, e->mass * 0.8f ) ), true );
		}
	}

	if ( m_windPending && AnyProjectileFlying() == false )
	{
		// the shot has landed: the wind turns for the next one
		m_windPending = false;
		float a = m_windRng.Range( 0.0f, 2.0f * PI );
		float strength = m_windShift * m_windRng.Range( 0.4f, 1.0f );
		m_wind = { cosf( a ) * strength, 0.0f, sinf( a ) * strength };
		m_windChanged = 2.5f;
		if ( m_audio )
		{
			m_audio->SetWindStrength( 0.25f + strength * 0.25f );
		}
	}

	for ( Mechanism& m : m_scene.mechanisms )
	{
		if ( m.type == MechType::Slider && b3Joint_IsValid( m.joint ) )
		{
			b3PrismaticJoint_SetTargetTranslation( m.joint, m.amplitude * sinf( m.speed * m_scene.time + m.phase ) );
		}
	}
	UpdateShields();

	for ( Entity* e : m_scene.entities )
	{
		if ( e->alive && e->kind == Kind::Projectile )
		{
			e->lastVel = b3Body_GetLinearVelocity( e->body );
		}
	}
	m_scene.Step( dt, kSubSteps );
	HandleEvents();

	for ( size_t i = 0; i < m_scene.entities.size(); ++i )
	{
		Entity* e = m_scene.entities[i];
		if ( e->alive == false || e->isStatic )
		{
			continue;
		}
		e->age += dt;
		if ( e->ghost > 0.0f )
		{
			// a felled tree has let the chain through: from now on it stops shots again
			e->ghost -= dt;
			if ( e->ghost <= 0.0f )
			{
				b3ShapeId shapes[8];
				int n = b3Body_GetShapes( e->body, shapes, 8 );
				for ( int k = 0; k < n; ++k )
				{
					b3Filter f = b3Shape_GetFilter( shapes[k] );
					f.maskBits = CatAll;
					b3Shape_SetFilter( shapes[k], f, true );
				}
			}
		}

		if ( e->pos.y < -45.0f )
		{
			if ( e->kind == Kind::King && e->defeated == false )
			{
				DefeatKing( e, "void" );
			}
			if ( e->kind != Kind::King )
			{
				Kill( e );
				continue;
			}
			if ( b3Body_IsEnabled( e->body ) )
			{
				b3Body_Disable( e->body );
			}
			continue;
		}

		switch ( e->kind )
		{
			case Kind::Block:
				if ( e->scoredFall == false && e->pos.y < e->homeY - 3.0f )
				{
					e->scoredFall = true;
					AddScore( e->mat == Mat::Stone ? 150 : 100, e->pos, false );
				}
				if ( e->fuse >= 0.0f )
				{
					e->fuse -= dt;
					e->flash = ( fmodf( e->fuse, 0.1f ) < 0.05f ) ? 0.8f : 0.1f;
					if ( e->fuse <= 0.0f )
					{
						Detonate( e );
					}
				}
				break;

			case Kind::King:
				if ( e->defeated == false )
				{
					Vector3 up = Vector3RotateByQuaternion( { 0, 1, 0 }, e->rot );
					if ( up.y < 0.55f )
					{
						e->tiltTimer += dt;
					}
					else
					{
						e->tiltTimer = std::max( 0.0f, e->tiltTimer - dt );
					}
					if ( e->tiltTimer > 0.35f )
					{
						DefeatKing( e, "tilt" );
					}
					else if ( e->pos.y < e->homeY - 2.5f )
					{
						DefeatKing( e, "fall" );
					}
				}
				break;

			case Kind::Projectile:
			{
				Vector3 v = ToRl( b3Body_GetLinearVelocity( e->body ) );
				float speed = Vector3Length( v );
				if ( speed < 0.8f )
				{
					e->restTimer += dt;
				}
				else
				{
					e->restTimer = 0.0f;
				}
				if ( m_headless == false && speed > 6.0f && ( m_scene.stepCount % 2 ) == 0 && e->hasHit == false )
				{
					Color c = e->ammo == (int)Ammo::Bomb ? Color{ 255, 200, 120, 160 } : Color{ 235, 235, 235, 110 };
					m_particles.Trail( e->pos, c, e->ammo == (int)Ammo::Boulder ? 0.7f : 0.35f );
				}
				if ( e->ammo == (int)Ammo::Bomb || e->ammo == (int)Ammo::Implosion )
				{
					e->flash = ( fmodf( e->age, 0.25f ) < 0.12f ) ? 0.6f : 0.0f;
					if ( e->age > 7.0f )
					{
						Detonate( e );
					}
				}
				else if ( e->ammo == (int)Ammo::Sticky && e->fuse >= 0.0f )
				{
					// the fuse beeps faster and faster
					float before = e->fuse;
					e->fuse -= dt;
					float period = e->fuse > 1.0f ? 0.5f : 0.2f;
					if ( floorf( before / period ) != floorf( e->fuse / period ) && m_audio )
					{
						m_audio->PlayAt( Sfx::Beep, e->pos, 0.6f, e->fuse > 1.0f ? 1.0f : 1.3f );
					}
					e->flash = fmodf( e->fuse, period ) < period * 0.4f ? 0.8f : 0.0f;
					if ( e->fuse <= 0.0f )
					{
						Detonate( e );
					}
				}
				break;
			}

			case Kind::Shard:
				if ( e->age > 5.0f && e->parts.empty() == false )
				{
					float k = 1.0f - Clamp01( ( e->age - 5.0f ) / 0.8f );
					Part& p = e->parts[0];
					p.size = Vector3Scale( p.size, k > 0.0f ? powf( k, 0.08f ) : 0.0f );
				}
				if ( e->age > 5.8f )
				{
					Kill( e );
				}
				break;

			case Kind::Crown:
				if ( e->age > 12.0f && m_headless )
				{
					Kill( e );
				}
				break;

			default:
				break;
		}
	}

	m_sinceShot += dt;
	m_levelTime += dt;
}

void Game::HandleEvents()
{
	std::vector<HitRecord> hits = m_scene.hits;
	std::sort( hits.begin(), hits.end(), []( const HitRecord& a, const HitRecord& b ) { return a.speed > b.speed; } );

	for ( const HitRecord& h : hits )
	{
		Entity* a = h.a;
		Entity* b = h.b;
		if ( a->alive == false || b->alive == false )
		{
			continue;
		}

		{
			// sound and dust use the material of whatever got struck
			Entity* struck = a->kind == Kind::Projectile ? b : a;
			Entity* other = struck == a ? b : a;
			Mat m = struck->isStatic ? other->mat : struck->mat;
			if ( struck->isStatic && other->kind == Kind::Projectile )
			{
				bool keeps = struck->mat == Mat::Shield || struck->mat == Mat::Rubber || struck->mat == Mat::Sand || struck->mat == Mat::Magic;
				m = keeps || struck->tree > 0.0f ? struck->mat : Mat::Rock;
			}
			HitEffects( h.point, h.speed, m, std::max( a->mass, b->mass ), true );
		}

		Entity* pair[2] = { a, b };
		for ( int s = 0; s < 2; ++s )
		{
			Entity* x = pair[s];
			Entity* other = pair[1 - s];
			if ( x->alive == false )
			{
				continue;
			}

			if ( x->kind == Kind::Projectile )
			{
				if ( x->hasHit == false && other->kind != Kind::Projectile )
				{
					x->hasHit = true;
					if ( x == m_focus )
					{
						m_focusLast = h.point;
					}
					bool shot = std::find( m_shotSerials.begin(), m_shotSerials.end(), x->serial ) != m_shotSerials.end() ||
								( m_shotPartner > 0 && x->serial == m_shotPartner );
					if ( m_impactStep < 0 && shot )
					{
						m_impactStep = m_scene.stepCount;
						m_impactPoint = h.point;
					}
				}
				bool impactFuse = x->ammo == (int)Ammo::Bomb || x->ammo == (int)Ammo::Implosion;
				if ( impactFuse && x->age > 0.12f && other->kind != Kind::Projectile )
				{
					Detonate( x );
					continue;
				}
				if ( x->ammo == (int)Ammo::Sticky && x->age > 0.12f && x->stuck == false )
				{
					StickTo( x, other );
				}
				// sand swallows the blow: the shot sinks in and the bag keeps only a little of the push.
				// The boulder is heavy enough to plough on.
				if ( other->kind == Kind::Block && other->mat == Mat::Sand && x->ammo != (int)Ammo::Sticky )
				{
					float keep = x->ammo == (int)Ammo::Boulder ? 0.5f : 0.15f;
					b3Body_SetLinearVelocity( x->body, b3MulSV( keep, b3Body_GetLinearVelocity( x->body ) ) );
					b3Body_SetAngularVelocity( x->body, b3MulSV( keep, b3Body_GetAngularVelocity( x->body ) ) );
					b3Body_SetLinearVelocity( other->body, b3MulSV( 0.25f, b3Body_GetLinearVelocity( other->body ) ) );
					b3Body_SetAngularVelocity( other->body, b3MulSV( 0.25f, b3Body_GetAngularVelocity( other->body ) ) );
				}
			}

			// only the boulder smashes reinforced masonry and snaps windmill blades; it ploughs on through
			bool boulder = other->kind == Kind::Projectile && other->ammo == (int)Ammo::Boulder && h.speed > 6.0f;
			bool blades = false;
			for ( const Mechanism& m : m_scene.mechanisms )
			{
				blades = blades || ( m.type == MechType::Windmill && m.entity == x );
			}
			if ( boulder && ( ( x->reinforced && x->breakQueued == false ) || blades ) )
			{
				b3Body_SetLinearVelocity( other->body, b3MulSV( 0.7f, other->lastVel ) );
				if ( x->reinforced )
				{
					x->breakQueued = true;
					x->lastVel = other->lastVel; // the rubble is driven on the way the boulder was going
				}
				else
				{
					BreakBlades( x );
				}
			}

			// only the chain shot cuts down a tree, and it swings on through
			bool chain = other->kind == Kind::Projectile && other->ammo == (int)Ammo::Chain && h.speed > 5.0f;
			if ( chain && x->tree > 0.0f && x->isStatic && x->breakQueued == false )
			{
				x->breakQueued = true;
				x->lastVel = other->lastVel;
				b3Body_SetLinearVelocity( other->body, b3MulSV( 0.75f, other->lastVel ) );
			}

			if ( x->kind == Kind::Block && x->mat == Mat::Tnt && h.speed > 4.5f && x->fuse < 0.0f )
			{
				x->fuse = 0.06f;
			}

			// ice breaks under real blows, not under its own chips: a shard flying across the island must not
			// set off a chain of shattering pillars
			if ( x->kind == Kind::Block && x->mat == Mat::Ice && h.speed > 3.4f && other->kind != Kind::Shard )
			{
				x->breakQueued = true;
			}

			if ( x->kind == Kind::King && x->defeated == false )
			{
				bool struckByShot = other->kind == Kind::Projectile && h.speed > 3.0f;
				bool crushed = other->lethal && h.speed > 3.0f;
				if ( struckByShot || crushed || h.speed > 8.0f )
				{
					if ( getenv( "CROLLO_DEBUG" ) )
						fprintf( stderr, "  re colpito da kind=%d mat=%d a %.1f m/s\n", (int)other->kind, (int)other->mat, h.speed );
					DefeatKing( x, "hit" );
				}
			}

			if ( x->kind == Kind::Balloon && h.speed > 1.0f )
			{
				PopBalloon( x );
			}

			if ( h.speed > 3.0f && x->isStatic == false && other->kind == Kind::Projectile )
			{
				x->flash = std::max( x->flash, 0.3f );
			}
		}
	}

	for ( const BeginTouchRecord& t : m_scene.touches )
	{
		Entity* pair[2] = { t.a, t.b };
		for ( int s = 0; s < 2; ++s )
		{
			Entity* x = pair[s];
			Entity* other = pair[1 - s];
			if ( x->alive == false || x->kind != Kind::Projectile || x->age <= 0.12f || other->kind == Kind::Projectile )
			{
				continue;
			}
			if ( x->ammo == (int)Ammo::Bomb || x->ammo == (int)Ammo::Implosion )
			{
				Detonate( x );
			}
			else if ( x->ammo == (int)Ammo::Sticky && x->stuck == false )
			{
				StickTo( x, other );
			}
		}
	}

	// joints that got overloaded snap
	for ( b3JointId j : m_scene.overloadedJoints )
	{
		if ( b3Joint_IsValid( j ) == false )
		{
			continue;
		}
		b3BodyId bodyA = b3Joint_GetBodyA( j );
		b3Transform frame = b3Joint_GetLocalFrameA( j );
		Vector3 p = ToRl( b3Body_GetWorldPoint( bodyA, frame.p ) );
		b3DestroyJoint( j, true );
		AddReplayEvent( ReplayEvent::Snap, p );
		m_particles.Debris( p, Color{ 170, 140, 90, 255 }, 6, 3.0f, 0.06f );
		m_particles.Dust( p, Color{ 200, 180, 150, 255 }, 3, 1.0f );
		if ( m_audio )
		{
			m_audio->PlayAt( Sfx::RopeSnap, p, 0.8f, FxRng().Range( 0.9f, 1.2f ) );
		}
	}

	for ( size_t i = 0; i < m_scene.entities.size(); ++i )
	{
		Entity* e = m_scene.entities[i];
		if ( e->alive && e->breakQueued )
		{
			if ( e->reinforced )
			{
				Crumble( e );
			}
			else if ( e->tree > 0.0f )
			{
				FellTree( e );
			}
			else
			{
				Shatter( e );
			}
		}
	}
}

// ---------------------------------------------------------------------------------------------
// Crystal shields
// ---------------------------------------------------------------------------------------------

static bool OverlapFound( b3ShapeId shapeId, void* context )
{
	(void)shapeId;
	*(bool*)context = true;
	return false;
}

bool Game::ShieldAreaClear( const Mechanism& m ) const
{
	const Entity* e = m.entity;
	if ( e == nullptr || e->parts.empty() )
	{
		return true;
	}
	Vector3 h = e->parts[0].size;
	b3Vec3 corners[8];
	int n = 0;
	for ( int x = -1; x <= 1; x += 2 )
	{
		for ( int y = -1; y <= 1; y += 2 )
		{
			for ( int z = -1; z <= 1; z += 2 )
			{
				corners[n++] = ToB3( Vector3RotateByQuaternion( { x * h.x, y * h.y, z * h.z }, e->rot ) );
			}
		}
	}
	b3ShapeProxy proxy{ corners, 8, 0.0f };
	b3QueryFilter filter = b3DefaultQueryFilter();
	filter.categoryBits = CatShield;
	filter.maskBits = CatBlock | CatKing | CatProjectile | CatDebris;
	bool found = false;
	b3World_OverlapShape( m_scene.World(), ToB3( e->pos ), &proxy, filter, OverlapFound, &found );
	return found == false;
}

// Shields that line up behind each other as seen from the cannon would stack their timers on top of
// each other. Give each wall in such a group its own spot on its top edge: left, right, then centre.
void Game::AssignShieldTimerSlots()
{
	std::vector<Mechanism*> shields;
	for ( Mechanism& m : m_scene.mechanisms )
	{
		if ( m.type == MechType::Blinker && m.entity != nullptr && m.entity->parts.empty() == false )
		{
			m.timerSlot = 0;
			m.timerGroup = -1;
			m.timerOrder = 0;
			shields.push_back( &m );
		}
	}
	auto distance = [&]( const Mechanism* m ) { return Vector3Distance( m->entity->pos, m_cannonPos ); };
	std::sort( shields.begin(), shields.end(), [&]( const Mechanism* a, const Mechanism* b ) { return distance( a ) < distance( b ); } );

	std::vector<bool> grouped( shields.size(), false );
	for ( size_t i = 0; i < shields.size(); ++i )
	{
		if ( grouped[i] )
		{
			continue;
		}
		const Entity* a = shields[i]->entity;
		Vector3 da = Vector3Subtract( a->pos, m_cannonPos );
		float angA = atan2f( da.x, da.z );
		float halfAngle = atanf( a->parts[0].size.x / std::max( 1.0f, Vector3Length( da ) ) );
		std::vector<size_t> group{ i };
		for ( size_t j = i + 1; j < shields.size(); ++j )
		{
			const Entity* b = shields[j]->entity;
			Vector3 db = Vector3Subtract( b->pos, m_cannonPos );
			float angB = atan2f( db.x, db.z );
			bool sameLine = fabsf( angA - angB ) < halfAngle * 1.5f;
			bool sameHeight = fabsf( a->pos.y - b->pos.y ) < a->parts[0].size.y + b->parts[0].size.y;
			if ( grouped[j] == false && sameLine && sameHeight )
			{
				group.push_back( j );
			}
		}
		if ( group.size() > 1 )
		{
			const int order[3] = { -1, 1, 0 };
			for ( size_t k = 0; k < group.size(); ++k )
			{
				shields[group[k]]->timerSlot = order[k % 3];
				shields[group[k]]->timerGroup = (int)i;
				shields[group[k]]->timerOrder = (int)k + 1;
				grouped[group[k]] = true;
			}
		}
	}
}

void Game::UpdateShields()
{
	for ( Mechanism& m : m_scene.mechanisms )
	{
		if ( m.type != MechType::Blinker || m.entity == nullptr || m.entity->alive == false )
		{
			continue;
		}
		float c = fmodf( m_scene.time + m.phase, m.period );
		bool want = c < m.onTime;
		m.untilToggle = want ? m.onTime - c : m.period - c;

		if ( want != m.on )
		{
			// never switch a wall on inside a block, a king or a flying ball: wait until it is clear
			if ( want == false || ShieldAreaClear( m ) )
			{
				m.on = want;
				if ( want )
				{
					b3Body_Enable( m.entity->body );
				}
				else
				{
					b3Body_Disable( m.entity->body );
				}
				if ( m_audio && m_attract == false )
				{
					m_audio->PlayAt( Sfx::Whoosh, m.entity->pos, 0.35f, want ? 1.4f : 0.9f );
				}
			}
		}

		// flicker for the last 0.6 s before the wall goes away
		m.entity->flash = ( m.on && m.untilToggle < 0.6f && fmodf( m.untilToggle, 0.16f ) < 0.08f ) ? 0.5f : 0.0f;
	}
}

float Game::PathShieldBlock( Vector3 p0, Vector3 v, Vector3 accel, float maxTime, float startTime ) const
{
	const float radius = 0.35f; // a little more than a cannonball
	const float dt = 0.02f;
	Vector3 prev = p0;
	for ( float t = dt; t <= maxTime; t += dt )
	{
		Vector3 p = Vector3Add( p0, Vector3Add( Vector3Scale( v, t ), Vector3Scale( accel, 0.5f * t * t ) ) );
		for ( const Mechanism& m : m_scene.mechanisms )
		{
			if ( m.type != MechType::Blinker || m.entity == nullptr || m.entity->parts.empty() )
			{
				continue;
			}
			const Entity* e = m.entity;
			Vector3 h = Vector3AddValue( e->parts[0].size, radius );
			Quaternion inv = QuaternionInvert( e->rot );
			Vector3 a = Vector3RotateByQuaternion( Vector3Subtract( prev, e->pos ), inv );
			Vector3 b = Vector3RotateByQuaternion( Vector3Subtract( p, e->pos ), inv );

			// slab test of the segment a-b against the inflated box
			float t0 = 0.0f, t1 = 1.0f;
			bool hit = true;
			float av[3] = { a.x, a.y, a.z }, bv[3] = { b.x, b.y, b.z }, hv[3] = { h.x, h.y, h.z };
			for ( int k = 0; k < 3 && hit; ++k )
			{
				float d = bv[k] - av[k];
				if ( fabsf( d ) < 1e-6f )
				{
					hit = fabsf( av[k] ) <= hv[k];
					continue;
				}
				float u0 = ( -hv[k] - av[k] ) / d;
				float u1 = ( hv[k] - av[k] ) / d;
				if ( u0 > u1 )
				{
					std::swap( u0, u1 );
				}
				t0 = std::max( t0, u0 );
				t1 = std::min( t1, u1 );
				hit = t0 <= t1;
			}
			if ( hit && BlinkerOnAt( m, startTime + t - dt * ( 1.0f - t0 ) ) )
			{
				return t;
			}
		}
		prev = p;
	}
	return -1.0f;
}

void Game::DrawShieldGhosts()
{
	// switched-off walls: a faint outline so the player knows where they will come back
	rlDisableDepthMask();
	for ( const Mechanism& m : m_scene.mechanisms )
	{
		if ( m.type != MechType::Blinker || m.on || m.entity == nullptr || m.entity->parts.empty() )
		{
			continue;
		}
		const Entity* e = m.entity;
		Vector3 h = e->parts[0].size;
		bool soon = m.untilToggle < 0.6f;
		float pulse = soon ? ( fmodf( m.untilToggle, 0.16f ) < 0.08f ? 1.0f : 0.4f ) : 0.25f;
		Color fill = WithAlpha( Color{ 110, 215, 255, 255 }, 0.12f * pulse );
		Color wire = WithAlpha( Color{ 170, 235, 255, 255 }, 0.8f * pulse );
		Matrix mtx = ComposeTRS( e->pos, e->rot, { 1, 1, 1 } );
		rlPushMatrix();
		rlMultMatrixf( MatrixToFloat( mtx ) );
		DrawCube( { 0, 0, 0 }, 2.0f * h.x, 2.0f * h.y, 2.0f * h.z, fill );
		DrawCubeWires( { 0, 0, 0 }, 2.0f * h.x, 2.0f * h.y, 2.0f * h.z, wire );
		rlPopMatrix();
	}
	rlDrawRenderBatchActive();
	rlEnableDepthMask();
}

void Game::DrawShieldTimers()
{
	float S = ui::S();
	for ( const Mechanism& m : m_scene.mechanisms )
	{
		if ( m.type != MechType::Blinker || m.entity == nullptr || m.entity->parts.empty() )
		{
			continue;
		}
		const Entity* e = m.entity;
		Vector3 top = Vector3Add( e->pos, { 0, e->parts[0].size.y + 0.6f, 0 } );
		Vector3 toTop = Vector3Subtract( top, m_camera.position );
		if ( Vector3DotProduct( toTop, Vector3Subtract( m_camera.target, m_camera.position ) ) <= 0.0f )
		{
			continue;
		}
		Vector2 c = GetWorldToScreen( top, m_camera );
		if ( m.timerSlot != 0 )
		{
			// move the timer to the left or right end of the wall's top edge, as seen on screen
			Vector3 axis = Vector3RotateByQuaternion( { 1, 0, 0 }, e->rot );
			float off = std::max( 0.0f, e->parts[0].size.x - 0.35f );
			Vector2 pa = GetWorldToScreen( Vector3Add( top, Vector3Scale( axis, off ) ), m_camera );
			Vector2 pb = GetWorldToScreen( Vector3Subtract( top, Vector3Scale( axis, off ) ), m_camera );
			Vector2 left = pa.x < pb.x ? pa : pb;
			Vector2 right = pa.x < pb.x ? pb : pa;
			c = m.timerSlot < 0 ? left : right;
		}
		float span = m.on ? m.onTime : m.period - m.onTime;
		float frac = Clamp01( m.untilToggle / std::max( span, 0.01f ) );
		// cyan ring = wall up, green ring = open; the arc shows the time left in that state
		Color col = m.on ? Color{ 110, 215, 255, 255 } : Color{ 120, 230, 110, 255 };
		float r = 17.0f * S;
		DrawCircleV( c, r + 3 * S, Color{ 20, 25, 35, 170 } );
		DrawRing( c, r - 5 * S, r, -90.0f, -90.0f + 360.0f * frac, 32, col );
		if ( m.timerOrder > 0 )
		{
			// numbered: 1 is the wall nearest to the cannon
			ui::TextCentered( TextFormat( "%d", m.timerOrder ), c.x, c.y - 14 * S, 26, col, false );
		}
		else if ( m.on )
		{
			DrawRectangleV( { c.x - 4 * S, c.y - 6 * S }, { 8 * S, 12 * S }, col );
		}
		else
		{
			DrawRing( c, 3 * S, 6 * S, 0, 360, 16, col );
		}
	}

	// For walls lined up behind each other, what matters is when they are ALL down at once.
	std::vector<int> done;
	for ( const Mechanism& m : m_scene.mechanisms )
	{
		if ( m.type != MechType::Blinker || m.timerGroup < 0 || m.timerOrder != 1 ||
			 std::find( done.begin(), done.end(), m.timerGroup ) != done.end() )
		{
			continue;
		}
		done.push_back( m.timerGroup );
		std::vector<const Mechanism*> members;
		for ( const Mechanism& o : m_scene.mechanisms )
		{
			if ( o.type == MechType::Blinker && o.timerGroup == m.timerGroup )
			{
				members.push_back( &o );
			}
		}
		auto allDown = [&]( float t ) {
			for ( const Mechanism* o : members )
			{
				if ( BlinkerOnAt( *o, t ) )
				{
					return false;
				}
			}
			return true;
		};
		float now = m_scene.time;
		bool openNow = allDown( now );
		float change = -1.0f;
		for ( float t = 0.05f; t < 30.0f; t += 0.05f )
		{
			if ( allDown( now + t ) != openNow )
			{
				change = t;
				break;
			}
		}

		const Entity* e = m.entity;
		Vector3 top = Vector3Add( e->pos, { 0, e->parts[0].size.y + 0.6f, 0 } );
		if ( Vector3DotProduct( Vector3Subtract( top, m_camera.position ), Vector3Subtract( m_camera.target, m_camera.position ) ) <= 0.0f )
		{
			continue;
		}
		Vector2 c = GetWorldToScreen( top, m_camera );
		const char* label = change < 0.0f ? ( openNow ? "varco sempre aperto" : "nessun varco" )
							: openNow	  ? TextFormat( "VARCO APERTO: %.1f s", change )
										  : TextFormat( "varco tra %.1f s", change );
		Color col = openNow ? Color{ 120, 230, 110, 255 } : Color{ 255, 240, 220, 255 };
		Vector2 size = ui::Measure( label, 24 );
		DrawRectangleRounded( { c.x - size.x * 0.5f - 8 * S, c.y + 26 * S, size.x + 16 * S, size.y + 6 * S }, 0.4f, 6, Color{ 20, 25, 35, 190 } );
		ui::Text( label, { c.x - size.x * 0.5f, c.y + 29 * S }, 24, col );
	}
}

void Game::HitEffects( Vector3 point, float speed, Mat m, float heavyMass, bool dust )
{
	if ( speed > 1.8f && m_hitSoundsThisFrame < 7 && m_audio )
	{
		Sfx sfx = Sfx::WoodHit;
		switch ( m )
		{
			case Mat::Stone:
			case Mat::Rock:
			case Mat::Grass:
				sfx = Sfx::StoneHit;
				break;
			case Mat::Ice:
				sfx = Sfx::IceHit;
				break;
			case Mat::Shield:
			case Mat::Magic:
			case Mat::Orb:
				sfx = Sfx::IceHit;
				heavyMass = -1.0f; // bright crystal ping, see below
				break;
			case Mat::Rubber:
				sfx = Sfx::Boing;
				break;
			case Mat::Sand:
				sfx = Sfx::SandHit;
				break;
			case Mat::Metal:
			case Mat::Dark:
			case Mat::Gold:
				sfx = Sfx::MetalHit;
				break;
			default:
				sfx = Sfx::WoodHit;
				break;
		}
		float vol = Clamp01( ( speed - 1.5f ) / 9.0f ) * 0.9f + 0.1f;
		float pitch = FxRng().Range( 0.88f, 1.12f ) * ( heavyMass > 1500.0f ? 0.8f : 1.0f ) * ( heavyMass < 0.0f ? 1.6f : 1.0f );
		if ( m_screen == Screen::Replay )
		{
			pitch *= 0.8f;
		}
		m_audio->PlayAt( sfx, point, vol, pitch );
		m_hitSoundsThisFrame += 1;
	}

	if ( m_headless == false && dust && speed > 4.0f )
	{
		Color c = GetMatProps( m ).color;
		m_particles.Dust( point, c, speed > 9.0f ? 6 : 3, 1.5f );
		if ( speed > 7.0f )
		{
			m_particles.Debris( point, c, speed > 12.0f ? 8 : 4, speed * 0.4f, 0.08f );
		}
	}
}

void Game::UpdateWorld( float dt )
{
	m_hitSoundsThisFrame = 0;
	m_timeScale = ExpDecay( m_timeScale, m_targetTimeScale, 2.5f, dt );
	m_accumulator += dt * m_timeScale;
	int steps = 0;
	while ( m_accumulator >= kFixedDt && steps < 4 )
	{
		FixedStep();
		m_accumulator -= kFixedDt;
		++steps;
	}
	if ( steps == 4 )
	{
		m_accumulator = std::min( m_accumulator, kFixedDt );
	}
	m_alpha = Clamp01( m_accumulator / kFixedDt );

	for ( Entity* e : m_scene.entities )
	{
		e->flash = std::max( 0.0f, e->flash - dt * 3.0f );
	}

	m_particles.Update( dt * m_timeScale, m_wind );
	if ( m_headless == false )
	{
		const Biome& bi = GetBiome( m_biome );
		Color a = bi.leafA, b = bi.leafB;
		switch ( bi.ambient )
		{
			case Ambient::Snow:
				a = { 255, 255, 255, 255 };
				b = { 215, 230, 250, 255 };
				break;
			case Ambient::Sand:
				a = { 185, 135, 75, 255 };
				b = { 215, 165, 100, 255 };
				break;
			case Ambient::Rain:
				a = { 190, 205, 225, 255 };
				b = { 160, 175, 200, 255 };
				break;
			case Ambient::Embers:
				a = { 255, 170, 60, 255 };
				b = { 255, 90, 20, 255 };
				break;
			default:
				break;
		}
		// the weather falls in front of the camera, wherever it looks
		Vector3 fwd = Vector3Normalize( Vector3Subtract( m_camera.target, m_camera.position ) );
		Vector3 focus = Vector3Add( m_camera.position, Vector3Scale( fwd, 14.0f ) );
		m_particles.Weather( bi.ambient, focus, dt * m_timeScale, m_wind, a, b );
	}
	for ( FloatText& t : m_texts )
	{
		t.life -= dt;
		t.pos.y += dt * 1.2f;
	}
	m_texts.erase( std::remove_if( m_texts.begin(), m_texts.end(), []( const FloatText& t ) { return t.life <= 0.0f; } ), m_texts.end() );

	m_scene.CollectGarbage();

	m_recoil = ExpDecay( m_recoil, 0.0f, 6.0f, dt );
	m_reload = std::max( 0.0f, m_reload - dt );
	if ( m_displayScore < m_score )
	{
		int step = std::max( 1, ( m_score - m_displayScore ) / 8 );
		m_displayScore = std::min( m_score, m_displayScore + step );
	}
}

void Game::StepSimulation( float dt )
{
	UpdateWorld( dt );
	CheckOutcome( dt );
}

void Game::CheckOutcome( float dt )
{
	if ( m_attract )
	{
		return;
	}

	if ( m_won || m_lost )
	{
		m_outcomeTimer += dt;
		if ( m_won && m_outcomeTimer > 1.6f )
		{
			m_targetTimeScale = 1.0f;
		}
		if ( m_outcomeTimer > 2.6f && m_screen == Screen::Playing && m_headless == false )
		{
			if ( m_won && StartReplay() )
			{
				SetScreen( Screen::Replay );
				return;
			}
			SetScreen( m_won ? Screen::Won : Screen::Lost );
			if ( m_audio )
			{
				m_audio->Play( m_won ? Sfx::Win : Sfx::Lose, 0.9f );
			}
		}
		return;
	}

	if ( m_kings.empty() == false && KingsRemaining() == 0 )
	{
		m_won = true;
		m_winStep = m_scene.stepCount;
		m_outcomeTimer = 0.0f;
		m_targetTimeScale = 0.3f;
		m_bonus = Cheating() ? 0 : AmmoLeft() * 1500;
		m_score += m_bonus;
		m_starsEarned = ComputeStars();
		if ( Cheating() )
		{
			return; // a test run: stars and records stay as they were
		}
		if ( m_challenge )
		{
			m_challengeTotal += m_score;
			m_newBest = m_challengeTotal > m_progress.bestChallenge && m_progress.bestChallenge > 0;
			m_progress.bestChallenge = std::max( m_progress.bestChallenge, m_challengeTotal );
			m_progress.bestRound = std::max( m_progress.bestRound, m_round );
		}
		else if ( m_levelIndex < Progress::kMaxLevels )
		{
			m_progress.stars[m_levelIndex] = std::max( m_progress.stars[m_levelIndex], m_starsEarned );
			if ( m_score > m_progress.best[m_levelIndex] )
			{
				m_newBest = m_progress.best[m_levelIndex] > 0;
				m_progress.best[m_levelIndex] = m_score;
			}
		}
		SaveProgress();
		return;
	}

	// lost when the ammo is gone and the dust has settled
	if ( AmmoLeft() == 0 && AnyProjectileFlying() == false && m_sinceShot > 2.5f )
	{
		int awake = b3World_GetAwakeBodyCount( m_scene.World() );
		int always = 0;
		for ( const Entity* e : m_scene.entities )
		{
			if ( e->alive && ( e->kind == Kind::Mechanism || e->kind == Kind::Balloon ) )
			{
				++always;
			}
		}
		always += (int)m_scene.mechanisms.size();
		if ( awake <= always * 2 + 2 )
		{
			m_calmTime += dt;
		}
		else
		{
			m_calmTime = 0.0f;
		}
		if ( m_calmTime > 1.5f || m_sinceShot > 14.0f )
		{
			m_lost = true;
			m_outcomeTimer = 0.0f;
			if ( m_challenge )
			{
				int total = m_challengeTotal + m_score;
				m_newBest = total > m_progress.bestChallenge && m_progress.bestChallenge > 0;
				m_progress.bestChallenge = std::max( m_progress.bestChallenge, total );
				SaveProgress();
			}
		}
	}
}

// ---------------------------------------------------------------------------------------------
// Update
// ---------------------------------------------------------------------------------------------

void Game::Update( float dt )
{
	dt = std::min( dt, 0.05f );
	m_time += dt;
	m_screenTime += dt;

#if defined( __EMSCRIPTEN__ )
	{
		Vector2 locked{ WebTakeMouseX(), WebTakeMouseY() };
		m_mouseDelta = IsCursorHidden() ? locked : GetMouseDelta();
	}
#else
	m_mouseDelta = GetMouseDelta();
	if ( IsKeyPressed( KEY_F11 ) )
	{
		ToggleBorderlessWindowed();
	}
#endif
	if ( IsKeyPressed( KEY_M ) && m_audio )
	{
		m_progress.music = !m_progress.music;
		m_audio->SetMusicEnabled( m_progress.music );
		SaveProgress();
	}

	switch ( m_screen )
	{
		case Screen::Title:
		case Screen::Map:
		case Screen::LevelSelect:
		case Screen::HowTo:
		case Screen::Story:
			UpdateAttract( dt );
			if ( IsKeyPressed( KEY_ESCAPE ) && m_screen != Screen::Title )
			{
				SetScreen( m_screen == Screen::LevelSelect ? Screen::Map : Screen::Title );
			}
			break;
		case Screen::Playing:
			UpdatePlaying( dt );
			break;
		case Screen::Paused:
			if ( IsKeyPressed( KEY_ESCAPE ) || IsKeyPressed( KEY_P ) )
			{
				SetScreen( Screen::Playing );
			}
			UpdateCamera( dt );
			break;
		case Screen::Replay:
			UpdateReplay( dt );
			break;
		case Screen::Won:
		case Screen::Lost:
			UpdateWorld( dt );
			UpdateCamera( dt );
			if ( IsKeyPressed( KEY_R ) )
			{
				RestartLevel();
			}
			if ( m_screen == Screen::Won && IsKeyPressed( KEY_ENTER ) )
			{
				if ( m_challenge || NextInCampaign() >= 0 )
				{
					NextLevel();
				}
				else if ( CampaignOfLevel( m_levelIndex ) >= 0 )
				{
					ShowStory( CampaignOfLevel( m_levelIndex ), true );
				}
			}
			break;
	}

	if ( m_audio )
	{
		Vector3 fwd = Vector3Normalize( Vector3Subtract( m_camera.target, m_camera.position ) );
		Vector3 right = Vector3Normalize( Vector3CrossProduct( fwd, { 0, 1, 0 } ) );
		m_audio->SetListener( m_camera.position, right );
	}
}

void Game::UpdateAttract( float dt )
{
	UpdateWorld( dt );
	UpdateCamera( dt );

	m_attractTimer -= dt;
	int remaining = KingsRemaining();
	if ( remaining > 0 && m_attractTimer <= 0.0f && m_attractShots < 14 )
	{
		if ( AutoFireAtKing() )
		{
			m_attractShots += 1;
			m_attractTimer = FxRng().Range( 2.8f, 4.2f );
		}
		else
		{
			m_attractTimer = 0.1f; // waiting for a shield to drop
		}
	}
	if ( ( remaining == 0 || m_attractShots >= 14 ) && m_attractTimer < -3.5f )
	{
		int realm = m_screen == Screen::LevelSelect ? m_campaign : m_screen == Screen::Story ? m_storyCampaign : -1;
		LoadAttractFor( realm );
	}
}

bool Game::AutoFireAtKing()
{
	std::vector<Entity*> targets;
	for ( Entity* k : m_kings )
	{
		if ( k->defeated == false && k->alive )
		{
			targets.push_back( k );
		}
	}
	if ( targets.empty() )
	{
		return false;
	}
	Rng& r = FxRng();
	Entity* target = targets[r.Int( 0, (int)targets.size() - 1 )];
	Vector3 aimPoint = Vector3Add( target->pos, { 0, 0.7f, 0 } );
	if ( m_attract )
	{
		aimPoint = Vector3Add( aimPoint, Vector3Scale( r.InSphere(), 0.6f ) );
	}
	// some kings are brought down indirectly: go for the stone, gate or pillar while it is still in place
	const Entity* goal = target;
	float lob = 0.0f;
	for ( const AimHintRecord& h : m_aimHints )
	{
		if ( h.king != target->serial )
		{
			continue;
		}
		for ( Entity* e : m_scene.entities )
		{
			if ( e->serial == h.via && e->alive && Vector3Distance( e->pos, h.home ) < 0.6f )
			{
				aimPoint = Vector3Add( e->pos, h.offset );
				goal = e;
				lob = h.lob;
				break;
			}
		}
		if ( goal != target )
		{
			break;
		}
	}

	// pick ammunition
	Ammo type = Ammo::Ball;
	const char* forced = getenv( "CROLLO_AMMO" );
	if ( forced )
	{
		type = (Ammo)Clamp( (float)atoi( forced ), 0.0f, (float)Ammo::Count - 1 );
	}
	else if ( m_attract )
	{
		type = (Ammo)r.Int( 0, (int)Ammo::Count - 1 );
	}
	else
	{
		for ( int i = 0; i < (int)Ammo::Count; ++i )
		{
			int idx = ( m_shots + i ) % (int)Ammo::Count;
			if ( m_ammo[idx] > 0 )
			{
				type = (Ammo)idx;
				break;
			}
		}
		if ( m_ammo[(int)type] <= 0 )
		{
			return false;
		}
		// nothing else gets through reinforced masonry: keep the boulders for it
		int ironWalls = 0;
		for ( const Entity* e : m_scene.entities )
		{
			ironWalls += e->alive && e->reinforced ? 1 : 0;
		}
		// and only the chain shot fells the trees the kings hide behind
		int shelters = 0;
		for ( const AimHintRecord& h : m_aimHints )
		{
			for ( const Entity* e : m_scene.entities )
			{
				shelters += e->serial == h.via && e->alive && e->tree > 0.0f && e->isStatic ? 1 : 0;
			}
		}
		auto spare = [&]( Ammo keep ) {
			for ( int i = 0; i < (int)Ammo::Count; ++i )
			{
				if ( i != (int)keep && m_ammo[i] > 0 )
				{
					type = (Ammo)i;
					break;
				}
			}
		};
		if ( goal->reinforced && m_ammo[(int)Ammo::Boulder] > 0 )
		{
			type = Ammo::Boulder;
		}
		else if ( goal->tree > 0.0f && m_ammo[(int)Ammo::Chain] > 0 )
		{
			type = Ammo::Chain;
		}
		else if ( type == Ammo::Boulder && m_ammo[(int)Ammo::Boulder] <= ironWalls )
		{
			spare( Ammo::Boulder );
		}
		else if ( type == Ammo::Chain && m_ammo[(int)Ammo::Chain] <= shelters )
		{
			spare( Ammo::Chain );
		}
	}

	return FireAt( aimPoint, type, goal, lob );
}

bool Game::FireAt( Vector3 aimPoint, Ammo type, const Entity* target, float lob )
{
	Rng& r = FxRng();

	// Exact solution under constant acceleration (gravity + wind): choose a flight time,
	// then v = (d - a t^2 / 2) / t. Iterate because the muzzle moves with the aim.
	Vector3 accel = Vector3Add( { 0, -kGravity, 0 }, m_wind );
	float scale = type == Ammo::Boulder ? 0.82f : 1.0f;

	// Solve an arc for a chosen horizontal speed. Iterate because the muzzle moves with the aim.
	auto solve = [&]( float hs, Vector3& v, float& flight ) {
		for ( int iter = 0; iter < 4; ++iter )
		{
			Vector3 d = Vector3Subtract( aimPoint, Muzzle() );
			float hd = sqrtf( d.x * d.x + d.z * d.z );
			flight = std::max( 0.5f, hd / hs );
			v = Vector3Scale( Vector3Subtract( d, Vector3Scale( accel, 0.5f * flight * flight ) ), 1.0f / flight );
			float speed = Vector3Length( v ) / scale;
			Vector3 dir = Vector3Normalize( v );
			m_yaw = atan2f( dir.x, dir.z );
			m_pitch = asinf( Clamp( dir.y, -1.0f, 1.0f ) );
			m_power = Clamp01( ( speed - kMinSpeed ) / ( kMaxSpeed - kMinSpeed ) );
		}
		float speed = Vector3Length( v ) / scale;
		return speed >= kMinSpeed && speed <= kMaxSpeed;
	};

	// March a ray along the arc; the path is clear if the first thing it meets is the target.
	b3QueryFilter filter = b3DefaultQueryFilter();
	filter.maskBits = CatStatic | CatBlock | CatKing | CatBarrier;
	bool sliderBlocked = false;
	float edgeReach = type == Ammo::Boulder ? 0.5f : 0.0f;
	std::vector<Vector3> edges{ { 0, 0, 0 } };
	if ( edgeReach > 0.0f )
	{
		edges.push_back( { 0, edgeReach, 0 } );
		edges.push_back( { 0, -edgeReach, 0 } );
		edges.push_back( { edgeReach, 0, 0 } );
		edges.push_back( { -edgeReach, 0, 0 } );
	}
	auto pathClear = [&]( Vector3 v, float flight ) {
		Vector3 p0 = Muzzle();
		Vector3 prev = p0;
		for ( float t = 0.05f; t < flight + 0.2f; t += 0.05f )
		{
			Vector3 p = Vector3Add( p0, Vector3Add( Vector3Scale( v, t ), Vector3Scale( accel, 0.5f * t * t ) ) );
			// sliding walls: where will the wall be when the shot crosses its rail?
			for ( const Mechanism& m : m_scene.mechanisms )
			{
				if ( m.type != MechType::Slider || m.entity == nullptr || m.entity->alive == false )
				{
					continue;
				}
				float wz = m.entity->pos.z;
				if ( ( prev.z - wz ) * ( p.z - wz ) <= 0.0f && fabsf( p.z - prev.z ) > 1e-5f )
				{
					float f = ( wz - prev.z ) / ( p.z - prev.z );
					Vector3 c = Vector3Lerp( prev, p, f );
					float when = m_scene.time + t - 0.05f * ( 1.0f - f );
					float shift = m.amplitude * ( sinf( m.speed * when + m.phase ) - sinf( m.speed * m_scene.time + m.phase ) );
					Vector3 wall = Vector3Add( m.entity->pos, Vector3Scale( m.axis, shift ) );
					Vector3 half = m.entity->parts[0].size;
					if ( fabsf( c.x - wall.x ) < half.x + 0.4f + edgeReach && fabsf( c.y - wall.y ) < half.y + 0.4f + edgeReach )
					{
						sliderBlocked = true;
						return false;
					}
				}
			}
			// the blades keep turning while the shot flies: treat their whole disk as closed
			for ( const Mechanism& m : m_scene.mechanisms )
			{
				if ( m.type != MechType::Windmill || m.entity == nullptr )
				{
					continue;
				}
				float hz = m.entity->pos.z;
				if ( ( prev.z - hz ) * ( p.z - hz ) <= 0.0f && fabsf( p.z - prev.z ) > 1e-5f )
				{
					Vector3 c = Vector3Lerp( prev, p, ( hz - prev.z ) / ( p.z - prev.z ) );
					if ( Vector2Distance( { c.x, c.y }, { m.entity->pos.x, m.entity->pos.y } ) < m.amplitude + 0.4f )
					{
						return false;
					}
				}
			}
			// the boulder is big: check its top, bottom and sides too, not just its centre
			for ( const Vector3& o : edges )
			{
				Vector3 from = Vector3Add( prev, o );
				b3RayResult hit = b3World_CastRayClosest( m_scene.World(), ToB3( from ), ToB3( Vector3Subtract( p, prev ) ), filter );
				Entity* struck = hit.hit ? EntityFromShape( hit.shapeId ) : nullptr;
				bool sliding = false;
				for ( const Mechanism& m : m_scene.mechanisms )
				{
					sliding = sliding || ( m.type == MechType::Slider && m.entity == struck && struck != nullptr );
				}
				if ( hit.hit && sliding == false )
				{
					return struck == target || Vector3Distance( ToRl( hit.point ), aimPoint ) < 1.2f + edgeReach;
				}
			}
			prev = p;
		}
		return true;
	};

	const float speeds[] = { 22.0f, 19.0f, 16.5f, 14.0f, 12.0f, 10.0f, 8.5f, 7.0f };
	const int count = (int)( sizeof( speeds ) / sizeof( speeds[0] ) );
	int start = m_attract ? r.Int( 0, 2 ) : 0;
	Vector3 best{};
	bool found = false;
	bool fallback = false;
	bool waitForShield = false;
	bool waitForSlider = false;
	for ( int k = start; k < count; ++k )
	{
		Vector3 v;
		float flight;
		if ( lob > 0.0f && speeds[k] > lob )
		{
			continue;
		}
		if ( solve( speeds[k] * ( m_attract ? r.Range( 0.9f, 1.1f ) : 1.0f ), v, flight ) == false )
		{
			continue;
		}
		if ( fallback == false )
		{
			best = v;
			fallback = true;
		}
		sliderBlocked = false;
		bool clear = pathClear( v, flight );
		waitForSlider = waitForSlider || sliderBlocked;
		if ( clear )
		{
			// clear of blocks, but would a crystal shield be up when the shot passes through it?
			if ( PathShieldBlock( Muzzle(), v, accel, flight + 0.2f, m_scene.time ) >= 0.0f )
			{
				waitForShield = true;
				continue;
			}
			best = v;
			found = true;
			break;
		}
	}
	if ( found == false && ( waitForShield || waitForSlider ) )
	{
		return false; // a window will open: try again on a later frame
	}
	if ( found == false && fallback == false )
	{
		return false;
	}

	Vector3 dir = Vector3Normalize( best );
	float speed = Vector3Length( best ) / scale;
	m_yaw = atan2f( dir.x, dir.z );
	m_pitch = asinf( Clamp( dir.y, -1.0f, 1.0f ) );
	m_power = Clamp01( ( speed - kMinSpeed ) / ( kMaxSpeed - kMinSpeed ) );
	if ( m_attract == false )
	{
		m_ammo[(int)type] -= 1;
		m_shots += 1;
	}
	RestartRecording();
	FireProjectile( type, Muzzle(), dir, speed );
	if ( m_attract == false )
	{
		m_shotSerials.clear();
		m_shotPartner = m_focus && m_focus->partner ? m_focus->partner->serial : 0;
		if ( m_focus )
		{
			m_shotSerials.push_back( m_focus->serial );
		}
		m_shotDir = dir;
		AddReplayEvent( ReplayEvent::CannonFire, Muzzle(), dir );
	}
	return true;
}

void Game::UpdatePlaying( float dt )
{
	bool intro = m_camMode == CamMode::Intro;
	m_introTime += dt;

	if ( m_inputEnabled == false )
	{
		UpdateWorld( dt );
		CheckOutcome( dt );
		UpdateCamera( dt );
		return;
	}

	if ( IsKeyPressed( KEY_ESCAPE ) || IsKeyPressed( KEY_P ) )
	{
		SetScreen( Screen::Paused );
		return;
	}

	bool clickConsumed = false;
#if defined( __EMSCRIPTEN__ )
	// In the browser ESC releases the pointer lock before the game sees the key: treat that as a pause.
	if ( IsCursorHidden() )
	{
		m_hadLock = true;
	}
	else if ( m_hadLock )
	{
		SetScreen( Screen::Paused );
		return;
	}
	else if ( IsMouseButtonPressed( MOUSE_BUTTON_LEFT ) && m_lockAttempts < 2 && m_screenTime > 0.3f )
	{
		// a click on the unlocked canvas grabs the mouse again instead of firing
		DisableCursor();
		m_lockAttempts += 1;
		clickConsumed = true;
	}
#endif

	if ( intro )
	{
		if ( m_introTime > 0.3f && ( IsMouseButtonPressed( MOUSE_BUTTON_LEFT ) || IsKeyPressed( KEY_SPACE ) || IsKeyPressed( KEY_ENTER ) ) )
		{
			m_camMode = CamMode::Aim;
		}
	}
	else if ( m_won == false && m_lost == false )
	{
		if ( IsKeyPressed( KEY_R ) )
		{
			RestartLevel();
			return;
		}
		if ( IsKeyPressed( KEY_TAB ) )
		{
			m_camMode = m_camMode == CamMode::Overview ? CamMode::Aim : CamMode::Overview;
			if ( m_camMode == CamMode::Overview )
			{
				Vector3 d = Vector3Subtract( m_camPos, m_fortressCenter );
				m_orbitYaw = atan2f( d.x, d.z );
				m_orbitPitch = 0.45f;
			}
		}
		for ( int i = 0; i < (int)Ammo::Count; ++i )
		{
			if ( IsKeyPressed( KEY_ONE + i ) )
			{
				SelectAmmo( i );
			}
		}
		if ( IsKeyPressed( KEY_Q ) || IsKeyPressed( KEY_E ) )
		{
			int dir = IsKeyPressed( KEY_E ) ? 1 : -1;
			for ( int k = 1; k <= (int)Ammo::Count; ++k )
			{
				int idx = ( m_selected + dir * k + (int)Ammo::Count * 2 ) % (int)Ammo::Count;
				if ( m_ammo[idx] > 0 )
				{
					SelectAmmo( idx );
					break;
				}
			}
		}
		if ( IsKeyPressed( KEY_F9 ) && m_challenge == false )
		{
			m_cheat = !m_cheat;
			if ( m_cheat )
			{
				// bring back any ammunition already used up
				for ( int i = 0; i < (int)Ammo::Count; ++i )
				{
					m_ammo[i] = std::max( m_ammo[i], m_level->ammo[i] );
				}
			}
		}
		if ( IsKeyPressed( KEY_T ) )
		{
			m_progress.aimAssist = !m_progress.aimAssist;
			SaveProgress();
		}
		if ( IsKeyPressed( KEY_O ) && m_renderer )
		{
			m_progress.shadows = !m_progress.shadows;
			m_renderer->shadowsEnabled = m_progress.shadows;
			SaveProgress();
		}

		// the first frames after capturing the cursor report a bogus jump
		Vector2 md = m_screenTime > 0.15f ? m_mouseDelta : Vector2{ 0, 0 };
		float wheel = GetMouseWheelMove();

		if ( m_camMode == CamMode::Aim || m_camMode == CamMode::Follow )
		{
			float sens = m_zoom ? 0.0007f : 0.0020f;
			if ( m_camMode == CamMode::Aim )
			{
				m_yaw -= md.x * sens;
				m_pitch -= md.y * sens;
			}
			float keyRate = ( IsKeyDown( KEY_LEFT_SHIFT ) ? 0.12f : 0.45f ) * dt;
			if ( IsKeyDown( KEY_A ) || IsKeyDown( KEY_LEFT ) )
				m_yaw += keyRate;
			if ( IsKeyDown( KEY_D ) || IsKeyDown( KEY_RIGHT ) )
				m_yaw -= keyRate;
			if ( IsKeyDown( KEY_UP ) )
				m_pitch += keyRate;
			if ( IsKeyDown( KEY_DOWN ) )
				m_pitch -= keyRate;
			m_power += wheel * 0.025f;
			float powerRate = ( IsKeyDown( KEY_LEFT_SHIFT ) ? 0.08f : 0.35f ) * dt;
			if ( IsKeyDown( KEY_W ) )
				m_power += powerRate;
			if ( IsKeyDown( KEY_S ) )
				m_power -= powerRate;
			m_power = Clamp01( m_power );
			m_yaw = Clamp( m_yaw, m_cannonBaseYaw - 1.2f, m_cannonBaseYaw + 1.2f );
			m_pitch = Clamp( m_pitch, -0.12f, 1.2f );
			m_zoom = m_camMode == CamMode::Aim && IsMouseButtonDown( MOUSE_BUTTON_RIGHT );
		}
		else if ( m_camMode == CamMode::Overview )
		{
			m_orbitYaw -= md.x * 0.004f;
			m_orbitPitch = Clamp( m_orbitPitch + md.y * 0.003f, 0.05f, 1.35f );
			m_orbitDist = Clamp( m_orbitDist - wheel * 2.0f, 8.0f, 80.0f );
		}

		if ( IsMouseButtonPressed( MOUSE_BUTTON_LEFT ) && clickConsumed == false )
		{
			if ( m_camMode == CamMode::Aim )
			{
				Fire();
			}
			else
			{
				m_camMode = CamMode::Aim;
			}
		}

		if ( IsKeyPressed( KEY_SPACE ) )
		{
			Entity* best = nullptr;
			if ( m_focus && m_focus->alive && m_focus->specialUsed == false && HasSpecial( m_focus->ammo ) )
			{
				best = m_focus;
			}
			if ( best == nullptr )
			{
				for ( Entity* e : m_scene.entities )
				{
					if ( e->alive && e->kind == Kind::Projectile && e->specialUsed == false && HasSpecial( e->ammo ) &&
						 ( best == nullptr || e->age < best->age ) )
					{
						best = e;
					}
				}
			}
			Special( best );
		}
	}

	UpdateWorld( dt );
	CheckOutcome( dt );
	UpdateCamera( dt );
}

// How much a camera riding a chain shot still swings with the ball it follows: all of it at the start,
// fading to nothing within a couple of seconds.
static float ChainSwing( float t )
{
	return expf( -1.1f * std::max( 0.0f, t ) );
}

void Game::UpdateCamera( float dt )
{
	Vector3 up{ 0, 1, 0 };
	Vector3 desiredPos = m_camPos;
	Vector3 desiredTarget = m_camTarget;
	float desiredFov = 50.0f;
	float rate = 5.0f;

	Vector3 dir = AimDir();
	Vector3 flat = Vector3Normalize( { dir.x, 0.0f, dir.z } );
	// over the shoulder, so the cannon stays visible at the lower left
	Vector3 side = Vector3CrossProduct( flat, up );
	Vector3 aimPos = Vector3Add( Vector3Add( m_cannonPos, { 0, 3.1f, 0 } ), Vector3Add( Vector3Scale( flat, -6.6f ), Vector3Scale( side, 1.5f ) ) );
	Vector3 aimTarget = Vector3Add( Vector3Add( Muzzle(), Vector3Scale( dir, 14.0f ) ), Vector3Scale( side, 0.9f ) );
	aimTarget.y = Clamp( aimTarget.y, m_cannonPos.y + 0.8f, m_cannonPos.y + 4.5f );

	switch ( m_camMode )
	{
		case CamMode::Intro:
		{
			// A full lap of the fortress that passes behind it, so nothing it hides goes unseen,
			// then a glide back to the cannon.
			float t = Clamp01( m_introTime / kIntroSeconds );
			float front = m_cannonBaseYaw + PI; // the side facing the cannon
			float lap = SmoothStep( 0.0f, 0.72f, t );
			float ang = front + 1.0f + lap * ( 2.0f * PI - 2.0f );
			float R = m_fortressRadius * 1.5f + 6.0f;
			Vector3 orbit = Vector3Add( m_fortressCenter, { sinf( ang ) * R, 4.0f + R * 0.25f, cosf( ang ) * R } );
			float w = SmoothStep( 0.62f, 1.0f, t );
			desiredPos = Vector3Lerp( orbit, aimPos, w );
			desiredTarget = Vector3Lerp( m_fortressCenter, aimTarget, w );
			rate = 3.5f;
			if ( m_introTime >= kIntroSeconds )
			{
				m_camMode = CamMode::Aim;
			}
			break;
		}
		case CamMode::Aim:
			desiredPos = aimPos;
			desiredTarget = aimTarget;
			desiredFov = 44.0f;
			if ( m_zoom )
			{
				desiredPos = Vector3Add( Vector3Add( m_cannonPos, { 0, 1.9f, 0 } ), Vector3Scale( flat, -1.2f ) );
				desiredTarget = Vector3Add( Muzzle(), Vector3Scale( dir, 40.0f ) );
				desiredFov = 20.0f;
			}
			rate = 9.0f;
			break;
		case CamMode::Follow:
		{
			m_followTime += dt;
			if ( m_focus != nullptr && m_focus->alive && m_focus->hasHit == false && m_focus->pos.y > m_fortressCenter.y - 8.0f )
			{
				Vector3 p = m_focus->pos;
				Vector3 v = ToRl( b3Body_GetLinearVelocity( m_focus->body ) );
				if ( m_focus->partner != nullptr && m_focus->partner->alive )
				{
					// a chain shot: riding one ball makes the view swing with the spin. Keep the swing at the
					// start, then settle on the middle of the chain.
					float w = ChainSwing( m_followTime );
					Vector3 mid = Vector3Lerp( p, m_focus->partner->pos, 0.5f );
					Vector3 vMid = Vector3Lerp( v, ToRl( b3Body_GetLinearVelocity( m_focus->partner->body ) ), 0.5f );
					p = Vector3Lerp( mid, p, w );
					v = Vector3Lerp( vMid, v, w );
				}
				Vector3 vf = Vector3Normalize( { v.x, 0.0f, v.z } );
				desiredPos = Vector3Add( Vector3Add( p, Vector3Scale( vf, -7.5f ) ), { 0, 2.6f, 0 } );
				desiredTarget = Vector3Add( p, Vector3Scale( v, 0.12f ) );
				m_focusLast = p;
				m_watchTime = 0.0f;
				rate = 7.0f;
			}
			else
			{
				if ( m_focus && m_focus->alive && m_focus->pos.y > m_fortressCenter.y - 6.0f )
				{
					m_focusLast = Vector3Lerp( m_focusLast, m_focus->pos, 1.0f - expf( -2.0f * dt ) );
				}
				m_watchTime += dt;
				Vector3 away = Vector3Subtract( m_camPos, m_focusLast );
				away.y = 0.0f;
				float len = Vector3Length( away );
				away = len > 0.01f ? Vector3Scale( away, 1.0f / len ) : Vector3Scale( flat, -1.0f );
				desiredPos = Vector3Add( m_focusLast, Vector3Add( Vector3Scale( away, 13.0f ), { 0, 4.5f, 0 } ) );
				desiredTarget = m_focusLast;
				rate = 1.8f;
				int awake = b3World_GetAwakeBodyCount( m_scene.World() );
				if ( ( m_watchTime > 3.0f && awake < 4 + (int)m_scene.mechanisms.size() * 2 ) || m_watchTime > 6.5f )
				{
					if ( m_won == false && m_lost == false )
					{
						m_camMode = CamMode::Aim;
					}
				}
			}
			if ( m_followTime > 14.0f && m_won == false )
			{
				m_camMode = CamMode::Aim;
			}
			break;
		}
		case CamMode::Overview:
		{
			Vector3 off{ sinf( m_orbitYaw ) * cosf( m_orbitPitch ), sinf( m_orbitPitch ), cosf( m_orbitYaw ) * cosf( m_orbitPitch ) };
			desiredPos = Vector3Add( m_fortressCenter, Vector3Scale( off, m_orbitDist ) );
			desiredTarget = m_fortressCenter;
			rate = 6.0f;
			break;
		}
		case CamMode::Attract:
		{
			m_orbitYaw += dt * 0.06f;
			float R = m_fortressRadius * 1.5f + 8.0f;
			Vector3 center = Vector3Lerp( m_fortressCenter, m_cannonPos, 0.2f );
			desiredPos = Vector3Add( center, { sinf( m_orbitYaw ) * R, 4.0f + R * 0.18f, cosf( m_orbitYaw ) * R } );
			// on the title screen look a bit to the left of the action so the menu leaves it visible
			Vector3 toCenter = Vector3Normalize( Vector3Subtract( center, desiredPos ) );
			Vector3 right = Vector3Normalize( Vector3CrossProduct( toCenter, up ) );
			float shift = m_attract ? -R * 0.32f : 0.0f;
			desiredTarget = Vector3Add( Vector3Add( center, { 0, 1.0f, 0 } ), Vector3Scale( right, shift ) );
			rate = 1.5f;
			break;
		}
	}

	if ( dt <= 0.0f )
	{
		m_camPos = desiredPos;
		m_camTarget = desiredTarget;
		m_camFov = desiredFov;
	}
	else
	{
		m_camPos = ExpDecay( m_camPos, desiredPos, rate, dt );
		m_camTarget = ExpDecay( m_camTarget, desiredTarget, rate * 1.2f, dt );
		m_camFov = ExpDecay( m_camFov, desiredFov, 8.0f, dt );
	}

	m_shake = ExpDecay( m_shake, 0.0f, 4.0f, dt );
	m_screenFlash = std::max( 0.0f, m_screenFlash - dt * 2.5f );
	m_windChanged = std::max( 0.0f, m_windChanged - dt );
	Vector3 shake{};
	if ( m_shake > 0.001f )
	{
		float t = m_time * 40.0f;
		shake = { sinf( t * 1.3f ) * m_shake * 0.25f, sinf( t * 1.7f + 1.0f ) * m_shake * 0.25f, sinf( t * 1.1f + 2.0f ) * m_shake * 0.25f };
	}

	m_camera.position = Vector3Add( m_camPos, shake );
	m_camera.target = Vector3Add( m_camTarget, Vector3Scale( shake, 0.5f ) );
	m_camera.up = up;
	m_camera.fovy = m_camFov;
	m_camera.projection = CAMERA_PERSPECTIVE;
}

// ---------------------------------------------------------------------------------------------
// Automated testing
// ---------------------------------------------------------------------------------------------

bool Game::PlayOutAutomatically( int maxShots, int& downAtStart, int& shots )
{
	SkipIntro();
	m_camMode = CamMode::Aim;

	// Stability: nothing should fall over on its own.
	for ( int i = 0; i < 300; ++i )
	{
		StepSimulation( kFixedDt );
	}
	downAtStart = (int)KingsTotal() - KingsRemaining();

	shots = 0;
	while ( m_won == false && shots < maxShots && AmmoLeft() > 0 )
	{
		int waited = 0;
		while ( AutoFireAtKing() == false && waited < 60 * 12 )
		{
			StepSimulation( kFixedDt );
			++waited;
		}
		if ( getenv( "CROLLO_DEBUG" ) && waited > 0 )
		{
			fprintf( stderr, "  colpo %d: attesa di %.2f s per uno scudo\n", shots + 1, waited * kFixedDt );
		}
		++shots;
		for ( int i = 0; i < 60 * 8; ++i )
		{
			// split the cluster shortly after launch
			if ( i == 40 && m_focus && m_focus->ammo == (int)Ammo::Cluster )
			{
				Special( m_focus );
			}
			StepSimulation( kFixedDt );
			if ( m_won )
			{
				break;
			}
		}
	}
	return m_won && downAtStart == 0;
}

// Fires flat shots at the first king of level 11 at many moments and checks that the outcome
// matches the shield prediction: blocked when the wall is predicted up, a hit otherwise.
void Game::TestShields()
{
	int agree = 0, total = 0;
	for ( int trial = 0; trial < 16; ++trial )
	{
		LoadLevel( 10, false );
		SkipIntro();
		for ( int i = 0; i < 20 + trial * 15; ++i )
		{
			StepSimulation( kFixedDt );
		}
		Entity* king = m_kings[0];
		Vector3 aim = Vector3Add( king->pos, { 0, 0.7f, 0 } );
		Vector3 accel{ 0, -kGravity, 0 };
		Vector3 v{};
		float flight = 0.0f;
		for ( int iter = 0; iter < 4; ++iter )
		{
			Vector3 d = Vector3Subtract( aim, Muzzle() );
			flight = sqrtf( d.x * d.x + d.z * d.z ) / 24.0f;
			v = Vector3Scale( Vector3Subtract( d, Vector3Scale( accel, 0.5f * flight * flight ) ), 1.0f / flight );
			Vector3 dir = Vector3Normalize( v );
			m_yaw = atan2f( dir.x, dir.z );
			m_pitch = asinf( dir.y );
		}
		bool predictedBlocked = PathShieldBlock( Muzzle(), v, accel, flight + 0.2f, m_scene.time ) >= 0.0f;
		FireProjectile( Ammo::Ball, Muzzle(), Vector3Normalize( v ), Vector3Length( v ) );
		for ( int i = 0; i < 180; ++i )
		{
			StepSimulation( kFixedDt );
		}
		bool kingDown = king->defeated;
		bool ok = predictedBlocked != kingDown;
		agree += ok ? 1 : 0;
		total += 1;
		printf( "t=%5.2f  previsto %-9s  re %s  %s\n", 20 * kFixedDt + trial * 15 * kFixedDt, predictedBlocked ? "bloccato" : "libero",
				kingDown ? "abbattuto" : "in piedi", ok ? "ok" : "DIVERSO" );
	}
	printf( "Previsioni corrette: %d/%d\n", agree, total );
}

// Looks for "easy shots": for every ammunition the level offers, fires one shot at a grid of points
// over the fortress and reports the most kings a single shot knocks down. A level with several kings
// that falls to one shot is usually a design problem.
void Game::ScanForEasyShots( int levelIndex )
{
	const LevelDef& def = GetLevel( levelIndex );
	LoadLevel( levelIndex, false );
	int kings = KingsTotal();
	Vector3 center = m_fortressCenter;
	float reach = std::min( 6.0f, m_fortressRadius * 0.5f );

	std::vector<Vector3> points;
	b3QueryFilter filter = b3DefaultQueryFilter();
	filter.maskBits = CatStatic | CatBlock | CatKing;
	for ( int ix = -2; ix <= 2; ++ix )
	{
		for ( int iz = -1; iz <= 1; ++iz )
		{
			Vector3 top{ center.x + ix * reach * 0.5f, center.y + 30.0f, center.z + iz * reach * 0.5f };
			b3RayResult hit = b3World_CastRayClosest( m_scene.World(), ToB3( top ), { 0, -60.0f, 0 }, filter );
			if ( hit.hit )
			{
				points.push_back( Vector3Add( ToRl( hit.point ), { 0, 0.2f, 0 } ) );
			}
		}
	}

	printf( "Livello %2d %-22s re:%d |", levelIndex + 1, def.name, kings );
	for ( int a = 0; a < (int)Ammo::Count; ++a )
	{
		if ( def.ammo[a] <= 0 )
		{
			continue;
		}
		int best = 0;
		Vector3 bestPoint{};
		for ( const Vector3& p : points )
		{
			LoadLevel( levelIndex, false );
			SkipIntro();
			for ( int i = 0; i < 120; ++i )
			{
				StepSimulation( kFixedDt );
			}
			int waited = 0;
			while ( FireAt( p, (Ammo)a, nullptr ) == false && waited < 600 )
			{
				StepSimulation( kFixedDt );
				++waited;
			}
			for ( int i = 0; i < 60 * 8; ++i )
			{
				if ( i == 40 && m_focus && m_focus->ammo == (int)Ammo::Cluster )
				{
					Special( m_focus );
				}
				StepSimulation( kFixedDt );
			}
			if ( getenv( "CROLLO_DEBUG" ) )
			{
				fprintf( stderr, "  scan %s mira %.1f %.1f %.1f -> re abbattuti %d\n", GetAmmoInfo( (Ammo)a ).name, p.x, p.y, p.z, m_kingsDown );
			}
			if ( m_kingsDown > best )
			{
				best = m_kingsDown;
				bestPoint = p;
			}
		}
		bool alarm = kings >= 2 && best >= kings;
		printf( " %s %d/%d%s", GetAmmoInfo( (Ammo)a ).name, best, kings,
				alarm ? TextFormat( " (!! a x=%.1f z=%.1f)", bestPoint.x, bestPoint.z ) : "" );
		fflush( stdout );
	}
	printf( "\n" );
}

// Checks the session-2 additions one by one, each in a level that contains it.
void Game::TestMaterialsAndAmmo()
{
	auto settle = [&]( int steps ) {
		for ( int i = 0; i < steps; ++i )
		{
			StepSimulation( kFixedDt );
		}
	};

	// 1. rubber: a flat shot at the rubber wall of level 14 must come back towards the cannon
	{
		LoadLevel( 13, false );
		SkipIntro();
		settle( 60 );
		FireAt( { 7.0f, 2.6f, 36.2f }, Ammo::Ball, nullptr );
		Entity* ball = m_focus;
		float vzBefore = ball ? ToRl( b3Body_GetLinearVelocity( ball->body ) ).z : 0.0f;
		float vzMin = 1e9f;
		for ( int i = 0; i < 150 && ball && ball->alive; ++i )
		{
			StepSimulation( kFixedDt );
			vzMin = std::min( vzMin, ToRl( b3Body_GetLinearVelocity( ball->body ) ).z );
		}
		printf( "Gomma: velocita' verso la fortezza %.1f m/s, dopo il rimbalzo %.1f m/s -> %s\n", vzBefore, vzMin,
				vzMin < -0.5f * vzBefore ? "rimbalza" : "NON rimbalza" );
	}

	// 2. sandbags vs wood: the same blast next to each, how far do the pieces get pushed?
	{
		auto blastAndMeasure = [&]( Vector3 at, Mat mat ) {
			LoadLevel( 12, false );
			SkipIntro();
			settle( 60 );
			std::vector<std::pair<Entity*, Vector3>> start;
			for ( Entity* e : m_scene.entities )
			{
				if ( e->alive && e->kind == Kind::Block && e->mat == mat && Vector3Distance( e->pos, at ) < 2.6f )
				{
					start.push_back( { e, e->pos } );
				}
			}
			Explode( at, 2.6f, 1300.0f, false );
			settle( 120 );
			float sum = 0.0f;
			for ( auto& p : start )
			{
				sum += Vector3Distance( p.first->pos, p.second );
			}
			return start.empty() ? 0.0f : sum / start.size();
		};
		float sand = blastAndMeasure( { -5.1f, 0.6f, 33.2f }, Mat::Sand );
		float wood = blastAndMeasure( { 5.1f, 0.6f, 33.8f }, Mat::Wood );
		printf( "Sabbia: spostamento medio dopo una bomba %.2f m, legno %.2f m -> %s\n", sand, wood,
				sand < wood * 0.5f ? "i sacchi resistono" : "i sacchi NON resistono" );
	}

	// 3. implosion: right after the blast, the pieces around it must be moving towards its centre
	{
		LoadLevel( 13, false );
		SkipIntro();
		settle( 60 );
		Vector3 at{ 0.0f, 3.0f, 35.6f };
		std::vector<Entity*> near;
		for ( Entity* e : m_scene.entities )
		{
			float d = Vector3Distance( e->pos, at );
			if ( e->alive && e->kind == Kind::Block && e->mat != Mat::Sand && d < 4.5f && d > 0.5f )
			{
				near.push_back( e );
			}
		}
		Implode( at, 4.5f, 1500.0f );
		StepSimulation( kFixedDt );
		float inward = 0.0f;
		int towards = 0;
		for ( Entity* e : near )
		{
			Vector3 v = ToRl( b3Body_GetLinearVelocity( e->body ) );
			float radial = Vector3DotProduct( v, Vector3Normalize( Vector3Subtract( at, e->pos ) ) );
			inward += radial;
			towards += radial > 0.0f ? 1 : 0;
		}
		printf( "Vortice: %d pezzi su %d si muovono verso il centro, velocita' media verso il centro %.1f m/s -> %s\n", towards,
				(int)near.size(), inward / near.size(), towards * 10 >= (int)near.size() * 8 ? "risucchia" : "NON risucchia" );
	}

	// 4. sticky bomb: it must weld to the tower, hold on, and go off about 3 s later
	{
		LoadLevel( 13, false );
		SkipIntro();
		settle( 60 );
		FireAt( { 0.0f, 3.5f, 35.6f }, Ammo::Sticky, nullptr );
		Entity* bomb = m_focus;
		float stuckAt = -1.0f, goneAt = -1.0f;
		for ( int i = 0; i < 60 * 8 && bomb; ++i )
		{
			StepSimulation( kFixedDt );
			if ( stuckAt < 0.0f && bomb->alive && bomb->stuck )
			{
				stuckAt = i * kFixedDt;
			}
			if ( bomb->alive == false )
			{
				goneAt = i * kFixedDt;
				break;
			}
		}
		printf( "Adesiva: attaccata a %.2f s, esplosa a %.2f s (miccia %.2f s) -> %s\n", stuckAt, goneAt, goneAt - stuckAt,
				stuckAt >= 0.0f && fabsf( goneAt - stuckAt - 3.0f ) < 0.1f ? "ok" : "PROBLEMA" );
	}

	// 5. blasts and barriers: a bomb going off against the rubber wall of level 14 must not reach the king behind it
	{
		auto kingNear = [&]( Vector3 p ) {
			Entity* best = nullptr;
			for ( Entity* e : m_scene.entities )
			{
				if ( e->alive && e->kind == Kind::King && ( best == nullptr || Vector3Distance( e->pos, p ) < Vector3Distance( best->pos, p ) ) )
				{
					best = e;
				}
			}
			return best;
		};
		LoadLevel( 13, false );
		SkipIntro();
		settle( 60 );
		Entity* king = kingNear( { 7.0f, 3.0f, 38.5f } );
		Explode( { 7.0f, 2.6f, 35.8f }, 2.6f, 1300.0f, false );
		settle( 180 );
		printf( "Barriera: bomba contro la gomma, il re dietro %s -> %s\n", king->defeated ? "cade" : "resta in piedi",
				king->defeated ? "FALLITO" : "ok" );

		// and without the wall the same bomb knocks him down
		LoadLevel( 13, false );
		SkipIntro();
		settle( 60 );
		king = kingNear( { 7.0f, 3.0f, 38.5f } );
		Vector3 behind = Vector3Add( king->pos, { 0.0f, 0.6f, 1.5f } );
		Explode( behind, 2.6f, 1300.0f, false );
		settle( 180 );
		printf( "Barriera: la stessa bomba alle spalle del re, senza muro, %s -> %s\n", king->defeated ? "lo abbatte" : "non lo abbatte",
				king->defeated ? "ok" : "FALLITO" );
	}

	// 6. sandbags soak up cannonballs: a ball fired into the sandbag wall of level 14 barely moves it
	{
		LoadLevel( 13, false );
		SkipIntro();
		settle( 60 );
		Vector3 aim{ 0.0f, 2.0f, 33.6f };
		std::vector<std::pair<Entity*, Vector3>> bags;
		for ( Entity* e : m_scene.entities )
		{
			if ( e->alive && e->kind == Kind::Block && e->mat == Mat::Sand && Vector3Distance( e->pos, aim ) < 2.5f )
			{
				bags.push_back( { e, e->pos } );
			}
		}
		FireAt( aim, Ammo::Ball, nullptr );
		settle( 240 );
		float sum = 0.0f;
		int moved = 0;
		for ( auto& p : bags )
		{
			float d = Vector3Distance( p.first->pos, p.second );
			sum += d;
			moved += d > 0.3f ? 1 : 0;
		}
		printf( "Sabbia: una palla contro i sacchi, spostamento medio %.2f m, %d sacchi su %d spostati di oltre 30 cm -> %s\n",
				bags.empty() ? 0.0f : sum / bags.size(), moved, (int)bags.size(), moved <= 1 ? "assorbono" : "NON assorbono" );
	}

	// 7. reinforced masonry: a ball bounces off the gate of level 9, the boulder smashes it and the queen falls
	{
		auto shootGate = [&]( Ammo type ) {
			LoadLevel( 8, false );
			SkipIntro();
			settle( 60 );
			Entity* gate = nullptr;
			Entity* queen = nullptr;
			for ( Entity* e : m_scene.entities )
			{
				gate = e->reinforced ? e : gate;
				queen = e->kind == Kind::King && fabsf( e->pos.x ) < 0.5f ? e : queen;
			}
			Vector3 from = Vector3Add( gate->pos, { 0, 0, -4.0f } );
			FireProjectile( type, from, { 0, 0, 1 }, type == Ammo::Boulder ? 18.0f / 0.82f : 18.0f );
			settle( 240 );
			// dead entities are freed a frame later: look the gate up again rather than trusting the pointer
			bool standing = false;
			for ( Entity* e : m_scene.entities )
			{
				standing = standing || ( e->alive && e->reinforced );
			}
			return std::make_pair( standing, queen->defeated );
		};
		auto ball = shootGate( Ammo::Ball );
		auto boulder = shootGate( Ammo::Boulder );
		printf( "Portone rinforzato: palla -> %s, macigno -> %s e la regina %s -> %s\n", ball.first ? "regge" : "crolla",
				boulder.first ? "regge" : "crolla", boulder.second ? "cade" : "resta in piedi",
				ball.first && boulder.first == false && boulder.second ? "ok" : "FALLITO" );
	}

	// 7b. trees: a ball bounces off the pine in front of the middle king of Il Boschetto, the chain cuts it down
	// and the king behind it falls
	{
		auto shootPine = [&]( Ammo type ) {
			LoadLevel( FindLevelById( "prati_boschetto" ), false );
			SkipIntro();
			settle( 60 );
			Entity* king = nullptr;
			for ( Entity* e : m_kings )
			{
				king = king == nullptr || fabsf( e->pos.x ) < fabsf( king->pos.x ) ? e : king;
			}
			Entity* pine = nullptr;
			for ( Entity* e : m_scene.entities )
			{
				if ( e->tree > 0.0f && e->pine && ( pine == nullptr || Vector3Distance( e->pos, king->pos ) < Vector3Distance( pine->pos, king->pos ) ) )
				{
					pine = e;
				}
			}
			int serial = pine->serial;
			Vector3 dir = Vector3Normalize( { king->pos.x - pine->pos.x, 0.0f, king->pos.z - pine->pos.z } );
			FireProjectile( type, Vector3Add( pine->pos, Vector3Add( { 0, 2.0f, 0 }, Vector3Scale( dir, -4.0f ) ) ), dir, 18.0f );
			settle( 240 );
			bool standing = false;
			for ( Entity* e : m_scene.entities )
			{
				standing = standing || ( e->alive && e->serial == serial );
			}
			return std::make_pair( standing, king->defeated );
		};
		auto ball = shootPine( Ammo::Ball );
		auto chain = shootPine( Ammo::Chain );
		printf( "Alberi: palla -> %s e il re %s, catena -> %s e il re %s -> %s\n", ball.first ? "regge" : "cade",
				ball.second ? "cade" : "resta in piedi", chain.first ? "regge" : "cade", chain.second ? "cade" : "resta in piedi",
				ball.first && ball.second == false && chain.first == false && chain.second ? "ok" : "FALLITO" );
	}

	// 8. windmill blades: a ball bounces off, the boulder snaps them off the axle
	{
		auto shootBlades = [&]( Ammo type ) {
			LoadLevel( 5, false );
			SkipIntro();
			settle( 60 );
			const Mechanism* mill = nullptr;
			for ( const Mechanism& m : m_scene.mechanisms )
			{
				mill = m.type == MechType::Windmill ? &m : mill;
			}
			Vector3 hub = mill->entity->pos;
			// straight at a blade halfway along, wherever it is right now
			Vector3 target = Vector3Add( hub, Vector3RotateByQuaternion( { 0, 1.8f, 0 }, mill->entity->rot ) );
			FireProjectile( type, Vector3Add( target, { 0, 0, -4.0f } ), { 0, 0, 1 }, type == Ammo::Boulder ? 18.0f / 0.82f : 18.0f );
			settle( 120 );
			return b3Joint_IsValid( mill->joint );
		};
		bool afterBall = shootBlades( Ammo::Ball );
		bool afterBoulder = shootBlades( Ammo::Boulder );
		printf( "Pale del mulino: palla -> %s, macigno -> %s -> %s\n", afterBall ? "girano" : "spezzate", afterBoulder ? "girano" : "spezzate",
				afterBall && afterBoulder == false ? "ok" : "FALLITO" );
	}

	// 9. the same shot at the ice dam: real snowballs bring down all three kings, the powder puffs of the trap none
	{
		auto breakDam = [&]( const char* id ) {
			LoadLevel( FindLevelById( id ), false );
			SkipIntro();
			settle( 60 );
			Entity* dam = nullptr;
			for ( Entity* e : m_scene.entities )
			{
				dam = e->kind == Kind::Block && e->mat == Mat::Ice ? e : dam;
			}
			FireAt( Vector3Add( dam->pos, { 0, 0.3f, 0 } ), Ammo::Ball, dam );
			settle( 600 );
			return KingsTotal() - KingsRemaining();
		};
		int avalanche = breakDam( "gelo_valanga" );
		int powder = breakDam( "gelo_neve_fresca" );
		printf( "Valanga: re abbattuti dalla neve vera %d, dalla neve fresca %d -> %s\n", avalanche, powder,
				avalanche == 3 && powder == 0 ? "ok" : "FALLITO" );
	}

	// 10. the magic barrier: a cannonball fired point-blank at a king behind it bounces off, his orb goes through
	{
		auto shootAt = [&]( bool orb ) {
			LoadLevel( FindLevelById( "mulini_sfere" ), false );
			SkipIntro();
			settle( 60 );
			Entity* king = nullptr;
			Entity* ball = nullptr;
			for ( Entity* e : m_scene.entities )
			{
				king = e->kind == Kind::King && e->pos.x < -1.0f && e->pos.x > -2.0f ? e : king;
			}
			for ( Entity* e : m_scene.entities )
			{
				ball = e->mat == Mat::Orb && e->pos.x < 0.0f ? e : ball;
			}
			Vector3 dir = Vector3Normalize( Vector3Subtract( Vector3Add( king->pos, { 0, 0.5f, 0 } ), ball->pos ) );
			if ( orb )
			{
				b3Body_SetLinearVelocity( ball->body, ToB3( Vector3Scale( dir, 8.0f ) ) );
			}
			else
			{
				FireProjectile( Ammo::Ball, Vector3Add( ball->pos, { 0, 1.0f, 0 } ), dir, 16.0f );
			}
			settle( 240 );
			return king->defeated;
		};
		bool byBall = shootAt( false );
		bool byOrb = shootAt( true );
		printf( "Barriera magica: palla di cannone -> %s, sfera magica -> %s -> %s\n", byBall ? "passa" : "respinta",
				byOrb ? "passa" : "respinta", byBall == false && byOrb ? "ok" : "FALLITO" );
	}
}

bool Game::RunAutoTest( int levelIndex, int maxShots, bool verbose )
{
	LoadLevel( levelIndex, false );
	int downAtStart = 0, shots = 0;
	bool ok = PlayOutAutomatically( maxShots, downAtStart, shots );
	if ( verbose )
	{
		printf( "Livello %2d %-22s re:%d abbattuti-all'avvio:%d colpi:%d vinto:%s punti:%d corpi:%d\n", levelIndex + 1,
				GetLevel( levelIndex ).name, KingsTotal(), downAtStart, shots, m_won ? "SI" : "no", m_score, (int)m_scene.entities.size() );
	}
	return ok;
}

bool Game::RunChallengeTest( int round, uint32_t seed, int maxShots, bool verbose )
{
	m_challengeSeed = seed;
	m_challengeTotal = 0;
	LoadChallenge( round, false );
	int kings = KingsTotal();
	int ammo = AmmoLeft();
	int downAtStart = 0, shots = 0;
	bool ok = PlayOutAutomatically( maxShots, downAtStart, shots );
	if ( verbose )
	{
		printf( "Sfida round %2d seed %6u  re:%d munizioni:%d abbattuti-all'avvio:%d colpi:%d vinto:%s corpi:%d\n", round, seed, kings, ammo,
				downAtStart, shots, m_won ? "SI" : "no", (int)m_scene.entities.size() );
	}
	return ok;
}

// ---------------------------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------------------------

void Game::Draw()
{
	ui::BeginFrame();
	if ( m_screen == Screen::Replay && m_player && m_renderer )
	{
		DrawReplay();
		DrawReplayOverlay();
		return;
	}
	if ( m_scene.IsValid() && m_renderer )
	{
		DrawWorld();
	}

	switch ( m_screen )
	{
		case Screen::Title:
			DrawTitle();
			break;
		case Screen::LevelSelect:
			DrawLevelSelect();
			break;
		case Screen::Map:
			DrawMap();
			break;
		case Screen::Story:
			DrawStory();
			break;
		case Screen::HowTo:
			DrawHowTo();
			break;
		case Screen::Playing:
			DrawHUD();
			break;
		case Screen::Paused:
			DrawHUD();
			DrawPause();
			break;
		case Screen::Won:
			DrawHUD();
			DrawResult( true );
			break;
		case Screen::Lost:
			DrawHUD();
			DrawResult( false );
			break;
		case Screen::Replay:
			break;
	}
}

void Game::DrawWorld()
{
	Renderer& r = *m_renderer;
	Vector3 mid = Vector3Lerp( m_cannonPos, m_fortressCenter, 0.5f );
	float radius = Vector3Distance( m_cannonPos, m_fortressCenter ) * 0.5f + m_fortressRadius + 6.0f;
	r.BeginScene( m_camera, mid, radius, m_time, m_wind );

	for ( const Entity* e : m_scene.entities )
	{
		if ( e->alive && ( e->kind != Kind::Shield || b3Body_IsEnabled( e->body ) ) )
		{
			r.AddEntity( e, m_alpha );
		}
	}

	CannonPose pose;
	pose.position = m_cannonPos;
	pose.yaw = m_yaw;
	pose.pitch = m_pitch;
	pose.recoil = m_recoil;
	pose.wheelSpin = m_recoil * 0.6f;
	pose.visible = true;
	r.AddCannon( pose );

	for ( const Decoration& d : m_decorations )
	{
		r.AddDecoration( d );
	}

	for ( const Rope& rope : m_scene.ropes )
	{
		if ( B3_IS_NULL( rope.joint ) == false && b3Joint_IsValid( rope.joint ) == false )
		{
			continue;
		}
		if ( b3Body_IsValid( rope.bodyA ) == false || b3Body_IsValid( rope.bodyB ) == false )
		{
			continue;
		}
		if ( b3Body_IsEnabled( rope.bodyA ) == false || b3Body_IsEnabled( rope.bodyB ) == false )
		{
			continue;
		}
		Vector3 a = ToRl( b3Body_GetWorldPoint( rope.bodyA, rope.localA ) );
		Vector3 b = ToRl( b3Body_GetWorldPoint( rope.bodyB, rope.localB ) );
		float sag = 0.0f;
		if ( B3_IS_NULL( rope.joint ) == false && b3Joint_GetType( rope.joint ) == b3_distanceJoint &&
			 b3DistanceJoint_IsSpringEnabled( rope.joint ) )
		{
			float L = b3DistanceJoint_GetMaxLength( rope.joint );
			float d = Vector3Distance( a, b );
			if ( d < L )
			{
				sag = sqrtf( L * L - d * d ) * 0.5f;
			}
		}
		r.AddRope( a, b, rope.radius, sag, rope.color );
	}

	r.RenderShadows();
	r.RenderSky();

	BeginMode3D( m_camera );
	r.RenderOpaque();

	for ( const FlagInfo& f : m_flags )
	{
		Vector3 top = Vector3Add( f.base, { 0, 2.5f * f.scale, 0 } );
		r.DrawFlagCloth( top, 1.3f * f.scale, 0.8f * f.scale, f.color, f.phase );
	}

	if ( m_screen == Screen::Playing && ( m_camMode == CamMode::Aim ) && m_won == false && m_lost == false )
	{
		DrawTrajectory();
	}

	DrawShieldGhosts();
	m_particles.Draw( r, m_camera );
	EndMode3D();
	r.EndScene();

	DrawFloatTexts();
	if ( m_screenFlash > 0.0f )
	{
		DrawRectangle( 0, 0, GetScreenWidth(), GetScreenHeight(), WithAlpha( Color{ 255, 240, 210, 255 }, m_screenFlash ) );
	}
}

void Game::DrawTrajectory()
{
	if ( m_ammo[m_selected] <= 0 )
	{
		return;
	}
	Vector3 p0 = Muzzle();
	float speed = LaunchSpeed() * ( m_selected == (int)Ammo::Boulder ? 0.82f : 1.0f );
	Vector3 v = Vector3Scale( AimDir(), speed );
	Vector3 g{ 0, -kGravity, 0 };
	if ( m_progress.aimAssist )
	{
		g = Vector3Add( g, m_wind );
	}

	float maxT = m_progress.aimAssist ? 6.0f : 0.8f;
	const float dt = 0.04f;
	Vector3 prev = p0;
	// with aim assist, show where a crystal shield would be up when the ball gets there
	float shieldT = m_progress.aimAssist ? PathShieldBlock( p0, v, g, maxT, m_scene.time ) : -1.0f;
	b3QueryFilter filter = b3DefaultQueryFilter();
	filter.maskBits = CatStatic | CatBlock | CatKing | CatBarrier;
	int i = 0;
	for ( float t = dt; t <= maxT; t += dt, ++i )
	{
		Vector3 p = Vector3Add( p0, Vector3Add( Vector3Scale( v, t ), Vector3Scale( g, 0.5f * t * t ) ) );
		if ( shieldT >= 0.0f && t >= shieldT )
		{
			Vector3 sp = Vector3Add( p0, Vector3Add( Vector3Scale( v, shieldT ), Vector3Scale( g, 0.5f * shieldT * shieldT ) ) );
			Color c{ 110, 215, 255, 255 };
			DrawSphereEx( sp, 0.2f, 8, 8, c );
			DrawLine3D( Vector3Add( sp, { -0.5f, -0.5f, 0 } ), Vector3Add( sp, { 0.5f, 0.5f, 0 } ), c );
			DrawLine3D( Vector3Add( sp, { -0.5f, 0.5f, 0 } ), Vector3Add( sp, { 0.5f, -0.5f, 0 } ), c );
			break;
		}
		if ( m_progress.aimAssist )
		{
			b3RayResult hit = b3World_CastRayClosest( m_scene.World(), ToB3( prev ), ToB3( Vector3Subtract( p, prev ) ), filter );
			if ( hit.hit )
			{
				Vector3 hp = ToRl( hit.point );
				DrawSphereEx( hp, 0.22f, 8, 8, Color{ 255, 80, 60, 230 } );
				DrawCircle3D( Vector3Add( hp, Vector3Scale( ToRl( hit.normal ), 0.03f ) ), 0.5f,
							  Vector3CrossProduct( { 0, 1, 0 }, ToRl( hit.normal ) ),
							  acosf( Clamp( ToRl( hit.normal ).y, -1.0f, 1.0f ) ) * RAD2DEG, Color{ 255, 80, 60, 255 } );
				break;
			}
		}
		if ( i % 2 == 0 )
		{
			float fade = m_progress.aimAssist ? 1.0f : 1.0f - t / maxT;
			DrawSphereEx( p, 0.07f + 0.03f * fade, 6, 6, WithAlpha( Color{ 255, 250, 235, 255 }, 0.3f + 0.7f * fade ) );
		}
		prev = p;
	}
}

void Game::DrawFloatTexts()
{
	for ( const FloatText& t : m_texts )
	{
		Vector3 toText = Vector3Subtract( t.pos, m_camera.position );
		Vector3 fwd = Vector3Subtract( m_camera.target, m_camera.position );
		if ( Vector3DotProduct( toText, fwd ) <= 0.0f )
		{
			continue;
		}
		Vector2 s = GetWorldToScreen( t.pos, m_camera );
		float a = Clamp01( t.life / t.maxLife * 2.0f );
		float pop = 1.0f + 0.4f * SmoothStep( 1.3f, 1.6f, t.life );
		ui::TextOutlined( t.text.c_str(), s.x, s.y, 44.0f * t.scale * pop, WithAlpha( t.color, a ), WithAlpha( Color{ 90, 40, 10, 255 }, a ),
						  3.0f );
	}
}

void Game::DrawHUD()
{
	float S = ui::S();
	int W = GetScreenWidth();
	int H = GetScreenHeight();
	if ( m_level == nullptr )
	{
		return;
	}

	if ( m_camMode == CamMode::Intro && m_screen == Screen::Playing )
	{
		float a = Clamp01( m_introTime * 2.0f ) * Clamp01( ( kIntroSeconds - 1.0f - m_introTime ) * 2.0f );
		int camp = m_challenge ? -1 : CampaignOfLevel( m_levelIndex );
		const char* over = m_challenge ? "Sfida infinita" : TextFormat( "Livello %d", m_levelIndex + 1 );
		if ( camp >= 0 )
		{
			const Campaign& cc = GetCampaign( camp );
			over = TextFormat( "%s  •  %d / %d", cc.name, PositionInCampaign( m_levelIndex ) + 1, (int)cc.levels.size() );
		}
		DrawRectangle( 0, (int)( H * 0.04f ), W, (int)( ( camp >= 0 ? 250 : 210 ) * S ), WithAlpha( BLACK, 0.35f * a ) );
		ui::TextOutlined( over, W * 0.5f, H * 0.04f + 14 * S, 40, WithAlpha( kCream, a ), WithAlpha( Color{ 60, 30, 10, 255 }, a ), 3 );
		ui::TextOutlined( m_level->name, W * 0.5f, H * 0.04f + 58 * S, 92, WithAlpha( kGold, a ), WithAlpha( Color{ 90, 40, 10, 255 }, a ),
						  5 );
		if ( camp >= 0 )
		{
			// the subtitle is the king's own taunt
			ui::TextCentered( TextFormat( "«%s»", m_level->subtitle ), W * 0.5f, H * 0.04f + 158 * S, 34, WithAlpha( WHITE, a ) );
			ui::TextCentered( TextFormat( "- %s", GetCampaign( camp ).king ), W * 0.5f, H * 0.04f + 202 * S, 28,
							  WithAlpha( Color{ 255, 220, 150, 255 }, a ) );
		}
		else
		{
			ui::TextCentered( m_level->subtitle, W * 0.5f, H * 0.04f + 158 * S, 34, WithAlpha( WHITE, a ) );
		}
		ui::TextCentered( "click per iniziare", W * 0.5f, H - 90 * S, 28, WithAlpha( WHITE, 0.5f + 0.5f * sinf( m_time * 4.0f ) ) );
		return;
	}

	DrawShieldTimers();

	// top left: level + kings
	int total = KingsTotal() + ( m_kingsDown - ( KingsTotal() - KingsRemaining() ) );
	const char* title = m_challenge ? m_level->name : TextFormat( "%d. %s", PositionInCampaign( m_levelIndex ) + 1, m_level->name );
	float panelW = std::max( ui::Measure( title, 38 ).x + 40 * S, ( 36 + total * 52 ) * S );
	ui::Panel( { 20 * S, 20 * S, panelW, 110 * S }, Color{ 40, 30, 25, 170 }, Color{ 255, 220, 150, 120 } );
	ui::TextShadow( title, { 38 * S, 28 * S }, 38, kCream );
	for ( int i = 0; i < total; ++i )
	{
		bool down = i < m_kingsDown;
		Vector2 c{ ( 58 + i * 52 ) * S, 96 * S };
		ui::Crown( c, 40 * S, down ? Color{ 110, 100, 90, 255 } : kGold );
		if ( down )
		{
			DrawLineEx( { c.x - 14 * S, c.y - 14 * S }, { c.x + 14 * S, c.y + 14 * S }, 5 * S, Color{ 200, 40, 40, 255 } );
			DrawLineEx( { c.x + 14 * S, c.y - 14 * S }, { c.x - 14 * S, c.y + 14 * S }, 5 * S, Color{ 200, 40, 40, 255 } );
		}
	}

	// top right: score
	const char* score = TextFormat( "%d", m_displayScore + ( m_challenge ? m_challengeTotal - ( m_won ? m_score : 0 ) : 0 ) );
	Vector2 sm = ui::Measure( score, 56 );
	ui::Panel( { W - 40 * S - std::max( sm.x, 180 * S ), 20 * S, std::max( sm.x, 180 * S ) + 20 * S, 110 * S }, Color{ 40, 30, 25, 170 },
			   Color{ 255, 220, 150, 120 } );
	ui::TextShadow( "PUNTI", { W - 30 * S - std::max( sm.x, 180 * S ), 26 * S }, 26, Color{ 255, 220, 160, 255 } );
	ui::TextShadow( score, { W - 30 * S - sm.x, 56 * S }, 56, kGold );

	// wind indicator
	float windStrength = Vector3Length( m_wind );
	if ( windStrength > 0.05f )
	{
		Vector3 fwd = Vector3Subtract( m_camera.target, m_camera.position );
		fwd.y = 0;
		fwd = Vector3Normalize( fwd );
		Vector3 right = Vector3CrossProduct( fwd, { 0, 1, 0 } );
		Vector2 d{ Vector3DotProduct( m_wind, right ), -Vector3DotProduct( m_wind, fwd ) };
		d = Vector2Normalize( d );
		Vector2 c{ W * 0.5f, 70 * S };
		// a shifting wind flashes its panel for a moment after it turns
		bool turned = m_windChanged > 0.0f && fmodf( m_windChanged, 0.5f ) > 0.2f;
		ui::Panel( { c.x - 120 * S, 20 * S, 240 * S, 100 * S }, Color{ 40, 30, 25, 150 }, turned ? kGold : Color{ 255, 220, 150, 100 } );
		float len = ( 20 + windStrength * 12 ) * S;
		ui::Arrow( { c.x - d.x * len * 0.5f - 50 * S, c.y + 8 * S - d.y * len * 0.5f },
				   { c.x + d.x * len * 0.5f - 50 * S, c.y + 8 * S + d.y * len * 0.5f }, 5 * S, Color{ 180, 220, 255, 255 } );
		ui::TextShadow( m_windChanged > 0.0f ? "CAMBIA!" : "VENTO", { c.x - 5 * S, 30 * S }, 26, Color{ 200, 230, 255, 255 } );
		ui::TextShadow( TextFormat( "%.1f", windStrength ), { c.x - 5 * S, 60 * S }, 40, WHITE );
	}

	// ammo bar
	int types[(int)Ammo::Count];
	int count = 0;
	for ( int i = 0; i < (int)Ammo::Count; ++i )
	{
		if ( m_level->ammo[i] > 0 || m_ammo[i] > 0 )
		{
			types[count++] = i;
		}
	}
	// bottom right, clear of the cannon which sits at the lower left of the aiming view
	float slot = 112 * S;
	float gap = 12 * S;
	float barW = count * slot + ( count - 1 ) * gap;
	float x0 = W - barW - 40 * S;
	float y0 = H - slot - 44 * S;
	for ( int k = 0; k < count; ++k )
	{
		int i = types[k];
		bool sel = i == m_selected;
		bool empty = m_ammo[i] <= 0;
		Rectangle rc{ x0 + k * ( slot + gap ), y0 - ( sel ? 12 * S : 0 ), slot, slot };
		ui::Panel( rc, sel ? Color{ 255, 214, 120, 235 } : Color{ 40, 30, 25, 180 }, sel ? Color{ 120, 60, 10, 255 } : Color{ 255, 220, 150, 110 } );
		Vector2 c{ rc.x + slot * 0.5f, rc.y + slot * 0.44f };
		ui::AmmoIcon( i, c, 22 * S );
		if ( empty )
		{
			DrawRectangleRounded( rc, 0.18f, 8, Color{ 30, 30, 30, 140 } );
		}
		ui::TextShadow( TextFormat( "%d", i + 1 ), { rc.x + 10 * S, rc.y + 4 * S }, 24, sel ? Color{ 90, 40, 10, 255 } : Color{ 255, 230, 180, 200 },
						sel ? 0.0f : 2.0f );
		if ( Cheating() )
		{
			// the font has no infinity sign: draw one
			Color ic = sel ? Color{ 90, 40, 10, 255 } : WHITE;
			Vector2 ci{ rc.x + slot - 30 * S, rc.y + slot - 20 * S };
			DrawRing( { ci.x - 7 * S, ci.y }, 4 * S, 8 * S, 0, 360, 20, ic );
			DrawRing( { ci.x + 7 * S, ci.y }, 4 * S, 8 * S, 0, 360, 20, ic );
		}
		else
		{
			const char* cnt = TextFormat( "x%d", m_ammo[i] );
			Vector2 cm = ui::Measure( cnt, 30 );
			ui::TextShadow( cnt, { rc.x + slot - cm.x - 10 * S, rc.y + slot - cm.y - 4 * S }, 30,
							sel ? Color{ 90, 40, 10, 255 } : ( empty ? GRAY : WHITE ), sel ? 0.0f : 2.0f );
		}
	}
	if ( count > 0 )
	{
		const AmmoInfo& info = s_ammo[m_selected];
		const char* desc = TextFormat( "%s  -  %s", info.name, info.description );
		Vector2 dm = ui::Measure( desc, 28 );
		ui::TextShadow( desc, { W - 40 * S - dm.x, y0 - 54 * S }, 28, kCream );
	}

	// power meter
	{
		float h = 300 * S;
		float w = 34 * S;
		Rectangle rc{ W - 70 * S, H * 0.5f - h * 0.5f, w, h };
		ui::Panel( { rc.x - 8 * S, rc.y - 8 * S, w + 16 * S, h + 16 * S }, Color{ 40, 30, 25, 170 }, Color{ 255, 220, 150, 110 }, 0.4f );
		float fill = h * m_power;
		for ( int i = 0; i < (int)fill; i += 2 )
		{
			float t = i / h;
			Color c = ColorMix( Color{ 90, 200, 90, 255 }, Color{ 250, 200, 40, 255 }, t * 1.6f );
			if ( t > 0.6f )
			{
				c = ColorMix( Color{ 250, 200, 40, 255 }, Color{ 230, 60, 40, 255 }, ( t - 0.6f ) * 2.5f );
			}
			DrawRectangle( (int)rc.x, (int)( rc.y + h - i - 2 ), (int)w, 2, c );
		}
		ui::TextCentered( TextFormat( "%d", (int)( m_power * 100.0f + 0.5f ) ), rc.x + w * 0.5f, rc.y + h + 14 * S, 30, WHITE );
		ui::TextCentered( "POTENZA", rc.x + w * 0.5f - 10 * S, rc.y - 50 * S, 24, kCream );
	}

	if ( Cheating() )
	{
		ui::TextShadow( TextFormat( "TRUCCHI (F9): colpi infiniti  -  sparati %d", m_shots ), { 24 * S, 142 * S }, 24, Color{ 255, 150, 110, 255 } );
	}

	// contextual hints
	const char* hint = nullptr;
	if ( m_camMode == CamMode::Follow )
	{
		bool special = m_focus && m_focus->alive && m_focus->specialUsed == false && HasSpecial( m_focus->ammo );
		hint = special ? "SPAZIO: abilità speciale   •   CLICK: torna al cannone" : "CLICK: torna al cannone";
	}
	else if ( m_camMode == CamMode::Overview )
	{
		hint = "Mouse: ruota  •  Rotellina: zoom  •  TAB: torna al cannone";
	}
	else if ( m_levelTime < 9.0f && m_shots == 0 )
	{
		hint = m_level->hint;
	}
	if ( hint && m_screen == Screen::Playing )
	{
		ui::TextCentered( hint, W * 0.5f, 150 * S, 30, WHITE );
	}

	ui::Text( "Mouse: mira  •  Rotellina/W-S: potenza  •  Tasto destro: zoom  •  1-7: munizioni  •  TAB: panoramica  •  R: ricomincia  •  ESC: pausa",
			  { 20 * S, H - 30 * S }, 20, Color{ 255, 255, 255, 150 } );

	if ( m_camMode == CamMode::Aim && m_zoom )
	{
		float cx = W * 0.5f, cy = H * 0.5f;
		DrawCircleLinesV( { cx, cy }, 26 * S, Color{ 255, 255, 255, 160 } );
		DrawLineEx( { cx - 40 * S, cy }, { cx - 10 * S, cy }, 2 * S, Color{ 255, 255, 255, 160 } );
		DrawLineEx( { cx + 10 * S, cy }, { cx + 40 * S, cy }, 2 * S, Color{ 255, 255, 255, 160 } );
		DrawLineEx( { cx, cy - 40 * S }, { cx, cy - 10 * S }, 2 * S, Color{ 255, 255, 255, 160 } );
		DrawLineEx( { cx, cy + 10 * S }, { cx, cy + 40 * S }, 2 * S, Color{ 255, 255, 255, 160 } );
	}

	if ( m_won && m_screen == Screen::Playing )
	{
		float a = Clamp01( m_outcomeTimer * 2.0f );
		ui::TextOutlined( "TUTTI I RE SONO CADUTI!", W * 0.5f, H * 0.38f, 84 * ( 0.8f + 0.2f * a ), WithAlpha( kGold, a ),
						  WithAlpha( Color{ 90, 40, 10, 255 }, a ), 5 );
	}
	if ( m_lost && m_screen == Screen::Playing )
	{
		float a = Clamp01( m_outcomeTimer * 2.0f );
		ui::TextOutlined( "Munizioni esaurite...", W * 0.5f, H * 0.38f, 70, WithAlpha( kCream, a ), WithAlpha( Color{ 60, 20, 20, 255 }, a ), 4 );
	}
	if ( m_progress.aimAssist )
	{
		ui::TextShadow( "Mira assistita", { W - 220 * S, 140 * S }, 22, Color{ 255, 150, 130, 220 } );
	}
}

void Game::DrawTitle()
{
	float S = ui::S();
	int W = GetScreenWidth();
	int H = GetScreenHeight();

	// darken the left side where the menu lives; the demo siege plays on the right
	DrawRectangleGradientH( 0, 0, (int)( W * 0.55f ), H, Color{ 10, 20, 40, 170 }, Color{ 10, 20, 40, 0 } );

	float colX = W * 0.25f;
	float bob = sinf( m_time * 1.5f ) * 6.0f * S;
	ui::TextOutlined( "CROLLO", colX, H * 0.08f + bob, 220, kGold, Color{ 90, 40, 10, 255 }, 9 );
	ui::TextCentered( "un assedio fisico tra le nuvole", colX, H * 0.08f + 240 * S, 40, kCream );

	float bw = 420 * S;
	float bh = 78 * S;
	float x = colX - bw * 0.5f;
	float y = H * 0.4f;

	bool started = false;
	for ( int i = 0; i < LevelCount(); ++i )
	{
		started = started || m_progress.stars[i] > 0;
	}
	int cont = ContinueLevel();
	int contCampaign = cont >= 0 ? CampaignOfLevel( cont ) : -1;
	if ( started && contCampaign >= 0 )
	{
		const Campaign& cc = GetCampaign( contCampaign );
		ui::TextShadow( TextFormat( "%s  %d/%d", cc.name, PositionInCampaign( cont ) + 1, (int)cc.levels.size() ), { x + bw + 24 * S, y + bh * 0.3f }, 28,
						Color{ 255, 235, 190, 230 } );
	}
	if ( ui::Button( { x, y, bw, bh }, started ? "CONTINUA" : "GIOCA" ) )
	{
		if ( m_audio )
			m_audio->Play( Sfx::Click );
		if ( contCampaign < 0 )
		{
			SetScreen( Screen::Map ); // everything open is beaten: pick from the map
			return;
		}
		m_campaign = contCampaign;
		if ( m_progress.introSeen[contCampaign] == false )
		{
			ShowStory( contCampaign, false, cont );
			return;
		}
		LoadLevel( cont, false );
		SetScreen( Screen::Playing );
		return;
	}
	if ( ui::Button( { x, y + bh * 1.25f, bw, bh }, "MAPPA DEI REGNI" ) )
	{
		if ( m_audio )
			m_audio->Play( Sfx::Click );
		SetScreen( Screen::Map );
	}
	if ( ui::Button( { x, y + bh * 2.5f, bw, bh }, "SFIDA INFINITA" ) )
	{
		if ( m_audio )
			m_audio->Play( Sfx::Click );
		LoadChallenge( 1, true );
		SetScreen( Screen::Playing );
		return;
	}
	if ( ui::Button( { x, y + bh * 3.75f, bw, bh }, "COME SI GIOCA" ) )
	{
		if ( m_audio )
			m_audio->Play( Sfx::Click );
		SetScreen( Screen::HowTo );
	}
#if !defined( __EMSCRIPTEN__ )
	if ( ui::Button( { x, y + bh * 5.0f, bw, bh }, "ESCI" ) )
	{
		m_quit = true;
	}
#endif
	if ( m_progress.bestChallenge > 0 )
	{
		ui::TextCentered( TextFormat( "Record sfida: %d punti (round %d)", m_progress.bestChallenge, m_progress.bestRound ), colX, y + bh * 6.2f, 26,
						  Color{ 255, 230, 180, 220 } );
	}

	ui::TextCentered( "Fisica: Box3D di Erin Catto   •   Grafica: raylib   •   Suoni e musica sintetizzati al volo", W * 0.5f, H - 46 * S, 22,
					  Color{ 255, 255, 255, 170 } );
	ui::TextCentered( TextFormat( "Demo: %s", m_level ? m_level->name : "" ), W * 0.75f, H - 90 * S, 26, Color{ 255, 255, 255, 140 } );
	ui::Text( TextFormat( "M: musica %s", m_progress.music ? "on" : "off" ), { 20 * S, 20 * S }, 22, Color{ 255, 255, 255, 150 } );
	if ( m_debug )
	{
		ui::TextShadow( "DEBUG: tutti i livelli sbloccati", { W - 420 * S, 20 * S }, 24, Color{ 255, 150, 110, 255 } );
	}
}

static Color Rgb( Vector3 v, unsigned char a = 255 )
{
	auto c = []( float x ) { return (unsigned char)( Clamp01( x ) * 255.0f ); };
	return Color{ c( v.x ), c( v.y ), c( v.z ), a };
}

// A realm as a little floating island in its own sky, for the map and the story cards.
static void DrawRealmIsland( Vector2 c, float R, const Biome& b, bool locked, float time )
{
	auto tone = [&]( Color col ) { return locked ? ColorMix( col, Color{ 95, 98, 110, col.a }, 0.75f ) : col; };
	DrawCircleGradient( (int)c.x, (int)( c.y + R * 0.1f ), R * 1.3f, tone( Rgb( b.horizon, 220 ) ), tone( Rgb( b.zenith, 0 ) ) );
	if ( b.lavaGlow > 0.0f && locked == false )
	{
		DrawCircleGradient( (int)c.x, (int)( c.y + R * 1.05f ), R * 0.8f, Color{ 255, 120, 30, 170 }, Color{ 255, 60, 10, 0 } );
	}
	Color rock = tone( b.rock );
	Color rockDark = ColorBrightness( rock, -0.25f );
	DrawTriangle( { c.x - R, c.y + R * 0.1f }, { c.x + R * 0.05f, c.y + R * 1.05f }, { c.x + R, c.y + R * 0.1f }, rock );
	DrawTriangle( { c.x - R * 0.2f, c.y + R * 0.2f }, { c.x + R * 0.35f, c.y + R * 0.8f }, { c.x + R * 0.95f, c.y + R * 0.1f }, rockDark );
	DrawTriangle( { c.x - R * 0.95f, c.y + R * 0.15f }, { c.x - R * 0.55f, c.y + R * 0.7f }, { c.x - R * 0.2f, c.y + R * 0.2f }, rockDark );
	DrawEllipse( (int)c.x, (int)( c.y + R * 0.12f ), R, R * 0.3f, tone( Rgb( b.dirtA ) ) );
	DrawEllipse( (int)c.x, (int)c.y, R, R * 0.3f, tone( Rgb( b.grassB ) ) );
	DrawEllipse( (int)c.x, (int)( c.y + R * 0.04f ), R * 0.8f, R * 0.2f, tone( Rgb( b.grassA ) ) );
	// three trees and a tiny tower
	const float tx[3] = { -0.55f, -0.25f, 0.6f };
	for ( int i = 0; i < 3; ++i )
	{
		Vector2 base{ c.x + tx[i] * R, c.y - R * 0.02f + ( i == 1 ? R * 0.1f : 0.0f ) };
		Color leaf = tone( i % 2 ? b.leafB : b.leafA );
		float h = R * ( i == 1 ? 0.42f : 0.34f );
		if ( b.pinesOnly || i == 1 )
		{
			DrawTriangle( { base.x, base.y - h }, { base.x - h * 0.33f, base.y }, { base.x + h * 0.33f, base.y }, leaf );
		}
		else
		{
			DrawRectangleV( { base.x - R * 0.02f, base.y - h * 0.5f }, { R * 0.04f, h * 0.5f }, tone( Color{ 110, 76, 48, 255 } ) );
			DrawCircleV( { base.x, base.y - h * 0.62f }, h * 0.36f, leaf );
		}
	}
	Color stone = tone( Color{ 170, 165, 160, 255 } );
	DrawRectangleV( { c.x + R * 0.05f, c.y - R * 0.5f }, { R * 0.22f, R * 0.5f }, stone );
	for ( int i = 0; i < 3; ++i )
	{
		DrawRectangleV( { c.x + R * 0.05f + i * R * 0.08f, c.y - R * 0.57f }, { R * 0.06f, R * 0.08f }, stone );
	}
	float wave = sinf( time * 3.0f ) * R * 0.03f;
	Color flag = tone( Color{ 220, 50, 50, 255 } );
	DrawLineEx( { c.x + R * 0.16f, c.y - R * 0.57f }, { c.x + R * 0.16f, c.y - R * 0.85f }, R * 0.02f, stone );
	DrawTriangle( { c.x + R * 0.17f, c.y - R * 0.85f }, { c.x + R * 0.17f, c.y - R * 0.72f }, { c.x + R * 0.36f, c.y - R * 0.78f + wave }, flag );
}

static void DrawLock( Vector2 c, float s, Color color )
{
	DrawRectangleRounded( { c.x - 16 * s, c.y, 32 * s, 26 * s }, 0.2f, 4, color );
	DrawRing( c, 9 * s, 14 * s, 180, 360, 16, color );
}

void Game::DrawMap()
{
	float S = ui::S();
	int W = GetScreenWidth();
	int H = GetScreenHeight();
	// the demo siege stays a faint backdrop: the map is the picture here
	DrawRectangleGradientV( 0, 0, W, H, Color{ 10, 16, 38, 228 }, Color{ 30, 44, 78, 215 } );

	ui::TextOutlined( "IL REGNO DI SOPRA", W * 0.5f, 40 * S, 90, kGold, Color{ 90, 40, 10, 255 }, 5 );
	int fragments = 0;
	for ( int c = 0; c < CampaignCount(); ++c )
	{
		const Campaign& cc = GetCampaign( c );
		fragments += cc.levels.empty() == false && m_progress.stars[cc.levels.back()] > 0 ? 1 : 0;
	}
	ui::TextCentered( TextFormat( "Frammenti della Corona dei Venti recuperati: %d / %d", fragments, CampaignCount() ), W * 0.5f, 158 * S, 32,
					  Color{ 255, 240, 210, 240 } );
	if ( m_debug )
	{
		ui::TextShadow( "DEBUG: tutti i livelli sbloccati", { 30 * S, 30 * S }, 26, Color{ 255, 150, 110, 255 } );
	}

	const int n = CampaignCount();
	auto nodePos = [&]( int i ) {
		float x = W * 0.5f + ( i - ( n - 1 ) * 0.5f ) * std::min( 300 * S, W * 0.155f );
		float y = H * ( i % 2 == 0 ? 0.58f : 0.37f ) + sinf( m_time * 0.9f + i * 1.3f ) * 5 * S;
		return Vector2{ x, y };
	};
	const float R = 88 * S;

	// the dotted road between realms
	for ( int i = 0; i + 1 < n; ++i )
	{
		Vector2 a = nodePos( i ), b = nodePos( i + 1 );
		Vector2 mid{ ( a.x + b.x ) * 0.5f, std::min( a.y, b.y ) - 30 * S };
		bool open = CampaignUnlocked( i + 1 );
		Color col = open ? Color{ 255, 210, 90, 230 } : Color{ 220, 225, 235, 110 };
		const int segs = 16;
		for ( int k = 0; k < segs; k += 2 )
		{
			auto at = [&]( float t ) {
				float u = 1.0f - t;
				return Vector2{ u * u * a.x + 2 * u * t * mid.x + t * t * b.x, u * u * a.y + 2 * u * t * mid.y + t * t * b.y };
			};
			float t0 = 0.18f + 0.64f * k / segs, t1 = 0.18f + 0.64f * ( k + 1 ) / segs;
			DrawLineEx( at( t0 ), at( t1 ), 6 * S, col );
		}
	}

	for ( int i = 0; i < n; ++i )
	{
		const Campaign& cc = GetCampaign( i );
		const Biome& bi = GetBiome( cc.biome );
		bool soon = cc.levels.empty();
		bool open = CampaignUnlocked( i );
		Vector2 c = nodePos( i );
		bool hover = open && CheckCollisionPointCircle( GetMousePosition(), c, R * 1.15f ) && IsCursorHidden() == false;
		float r = hover ? R * 1.07f : R;
		if ( hover )
		{
			DrawRing( { c.x, c.y + r * 0.1f }, r * 1.22f, r * 1.3f, 0, 360, 48, Color{ 255, 220, 120, 200 } );
		}
		DrawRealmIsland( c, r, bi, open == false, m_time + i );

		bool freed = soon == false && m_progress.stars[cc.levels.back()] > 0;
		if ( freed )
		{
			ui::Crown( { c.x - r * 0.45f, c.y - r * 0.75f }, 46 * S, kGold );
		}
		if ( open == false && soon == false )
		{
			DrawLock( { c.x, c.y - r * 0.4f }, S * 1.2f, Color{ 45, 45, 55, 255 } );
		}

		float ty = c.y + r * 1.15f;
		ui::TextOutlined( cc.name, c.x, ty, 34, open ? kCream : Color{ 200, 200, 210, 255 }, Color{ 50, 30, 20, 255 }, 3 );
		ty += 42 * S;
		if ( soon )
		{
			ui::TextCentered( "in arrivo", c.x, ty, 24, Color{ 220, 225, 235, 200 } );
		}
		else if ( open )
		{
			ui::TextCentered( cc.king, c.x, ty, 24, Color{ 255, 235, 200, 230 } );
			ty += 34 * S;
			const char* st = TextFormat( "%d / %d", CampaignStars( i ), CampaignMaxStars( i ) );
			float w = ui::Measure( st, 26 ).x;
			ui::Star( { c.x - w * 0.5f - 6 * S, ty + 14 * S }, 13 * S, kGold, Color{ 150, 80, 10, 255 } );
			ui::TextShadow( st, { c.x - w * 0.5f + 14 * S, ty }, 26, WHITE );
		}
		else
		{
			int need = StarsToUnlock( i ) - CampaignStars( i - 1 );
			ui::TextCentered( TextFormat( "servono altre %d stelle", need ), c.x, ty, 24, Color{ 220, 225, 235, 220 } );
			ty += 30 * S;
			ui::TextCentered( TextFormat( "in %s", GetCampaign( i - 1 ).name ), c.x, ty, 22, Color{ 220, 225, 235, 180 } );
		}

		if ( hover && IsMouseButtonPressed( MOUSE_BUTTON_LEFT ) )
		{
			if ( m_audio )
				m_audio->Play( Sfx::Click );
			OpenCampaign( i );
			return;
		}
	}

	if ( ui::Button( { W * 0.5f - 160 * S, H - 130 * S, 320 * S, 72 * S }, "INDIETRO" ) )
	{
		if ( m_audio )
			m_audio->Play( Sfx::Click );
		SetScreen( Screen::Title );
	}
}

void Game::DrawStory()
{
	float S = ui::S();
	int W = GetScreenWidth();
	int H = GetScreenHeight();
	float t = m_screenTime;
	DrawRectangle( 0, 0, W, H, Color{ 10, 15, 30, (unsigned char)( 150 * Clamp01( t * 3.0f ) ) } );

	const Campaign& cc = GetCampaign( m_storyCampaign );
	const char* text = m_storyOutro ? cc.outro : cc.intro;
	// the card grows with the text: a rough line count from the paragraph's width
	float textW = 1240 * S - 180 * S;
	int lines = (int)ceilf( ui::Measure( text, 32 ).x / ( textW * 0.9f ) );
	float panelH = ( 262 + lines * 32 * 1.35f + 150 ) * S;
	float slide = ( 1.0f - SmoothStep( 0.0f, 0.4f, t ) ) * 120 * S;
	Rectangle panel{ W * 0.5f - 620 * S, H * 0.5f - panelH * 0.5f + 40 * S + slide, 1240 * S, panelH };
	ui::Panel( panel, Color{ 250, 238, 210, 246 }, Color{ 120, 70, 20, 255 }, 0.06f );
	Color ink{ 80, 40, 15, 255 };

	DrawRealmIsland( { panel.x + panel.width * 0.5f, panel.y - 40 * S }, 70 * S, GetBiome( cc.biome ), false, m_time );
	ui::TextCentered( m_storyOutro ? "EPILOGO" : TextFormat( "CAPITOLO %d", m_storyCampaign + 1 ), panel.x + panel.width * 0.5f,
					  panel.y + 60 * S, 28, Color{ 170, 100, 30, 255 }, false );
	ui::TextOutlined( cc.name, panel.x + panel.width * 0.5f, panel.y + 96 * S, 76, kGold, Color{ 110, 55, 10, 255 }, 4 );
	ui::TextCentered( TextFormat( "Il regno di %s", cc.king ), panel.x + panel.width * 0.5f, panel.y + 190 * S, 30, ink, false );
	DrawRectangle( (int)( panel.x + panel.width * 0.5f - 160 * S ), (int)( panel.y + 236 * S ), (int)( 320 * S ), (int)( 3 * S ),
				   Color{ 200, 150, 80, 255 } );

	ui::TextWrapped( text, { panel.x + 90 * S, panel.y + 262 * S }, textW, 32, ink, 1.35f );

	const char* label = m_storyPlay >= 0 ? "ALL'ASSALTO!" : "AVANTI";
	bool go = ui::Button( { W * 0.5f - 200 * S, panel.y + panel.height - 100 * S, 400 * S, 72 * S }, label );
	if ( t > 0.4f && ( IsKeyPressed( KEY_ENTER ) || IsKeyPressed( KEY_SPACE ) || IsKeyPressed( KEY_ESCAPE ) ) )
	{
		go = true;
	}
	if ( go )
	{
		if ( m_audio )
			m_audio->Play( Sfx::Click );
		if ( m_storyPlay >= 0 )
		{
			LoadLevel( m_storyPlay, false );
			SetScreen( Screen::Playing );
		}
		else if ( m_storyOutro )
		{
			SetScreen( Screen::Map );
		}
		else
		{
			m_campaign = m_storyCampaign;
			SetScreen( Screen::LevelSelect );
		}
	}
}

void Game::DrawLevelSelect()
{
	float S = ui::S();
	int W = GetScreenWidth();
	int H = GetScreenHeight();
	DrawRectangle( 0, 0, W, H, Color{ 10, 15, 30, 120 } );

	const Campaign& cc = GetCampaign( m_campaign );
	ui::TextOutlined( cc.name, W * 0.5f, 40 * S, 90, kGold, Color{ 90, 40, 10, 255 }, 5 );
	ui::TextCentered( TextFormat( "Il regno di %s", cc.king ), W * 0.5f, 150 * S, 32, Color{ 255, 240, 210, 240 } );
	const char* st = TextFormat( "%d / %d", CampaignStars( m_campaign ), CampaignMaxStars( m_campaign ) );
	float sw = ui::Measure( st, 40 ).x;
	ui::Star( { W * 0.5f - sw * 0.5f - 12 * S, 216 * S }, 20 * S, kGold, Color{ 120, 60, 10, 255 } );
	ui::TextShadow( st, { W * 0.5f - sw * 0.5f + 18 * S, 196 * S }, 40, WHITE );
	if ( m_debug )
	{
		ui::TextShadow( "DEBUG: tutti i livelli sbloccati", { 30 * S, 30 * S }, 26, Color{ 255, 150, 110, 255 } );
	}

	const int count = (int)cc.levels.size();
	int cols = std::min( 4, std::max( 1, count ) );
	int rows = ( count + cols - 1 ) / cols;
	float cw = 270 * S;
	float gap = 30 * S;
	float avail = H - 170 * S - 280 * S;
	float ch = std::min( 220 * S, ( avail - ( rows - 1 ) * gap ) / std::max( 1, rows ) );
	float k = ch / ( 220 * S ); // vertical scale for the card contents
	float gridW = cols * cw + ( cols - 1 ) * gap;
	float x0 = W * 0.5f - gridW * 0.5f;
	float y0 = 280 * S + ( avail - rows * ch - ( rows - 1 ) * gap ) * 0.5f;

	for ( int j = 0; j < count; ++j )
	{
		int i = cc.levels[j];
		int row = j / cols;
		int col = j % cols;
		// a short last row sits in the middle
		int inRow = std::min( cols, count - row * cols );
		float shift = ( cols - inRow ) * ( cw + gap ) * 0.5f;
		Rectangle rc{ x0 + shift + col * ( cw + gap ), y0 + row * ( ch + gap ), cw, ch };
		bool unlocked = LevelUnlocked( i );
		bool finale = j == count - 1;
		bool hover = unlocked && ui::Hovered( rc );
		Color fill = unlocked ? ( hover ? Color{ 255, 226, 150, 245 } : Color{ 250, 236, 205, 235 } ) : Color{ 90, 90, 100, 200 };
		if ( hover )
		{
			rc.y -= 6 * S;
		}
		ui::Panel( rc, fill, finale ? Color{ 200, 140, 20, 255 } : Color{ 120, 70, 20, 255 } );
		Color ink = unlocked ? Color{ 90, 45, 15, 255 } : Color{ 50, 50, 55, 255 };
		ui::TextCentered( TextFormat( "%d", j + 1 ), rc.x + cw * 0.5f, rc.y + 10 * S * k, 80 * k, ink, false );
		if ( finale )
		{
			ui::Crown( { rc.x + 40 * S, rc.y + 44 * S * k }, 34 * S * k, unlocked ? kGold : Color{ 60, 60, 66, 255 } );
		}
		const LevelDef& L = GetLevel( i );
		ui::TextCentered( L.name, rc.x + cw * 0.5f, rc.y + 100 * S * k, 30, ink, false );
		for ( int s = 0; s < 3; ++s )
		{
			bool got = s < m_progress.stars[i];
			ui::Star( { rc.x + cw * 0.5f + ( s - 1 ) * 46 * S, rc.y + 172 * S * k }, 19 * S * k, got ? kGold : Color{ 200, 190, 170, 255 },
					  got ? Color{ 150, 80, 10, 255 } : Color{ 160, 150, 130, 255 } );
		}
		if ( unlocked == false )
		{
			DrawLock( { rc.x + cw - 42 * S, rc.y + 36 * S }, S, Color{ 50, 50, 55, 255 } );
		}
		if ( hover && IsMouseButtonPressed( MOUSE_BUTTON_LEFT ) )
		{
			if ( m_audio )
				m_audio->Play( Sfx::Click );
			LoadLevel( i, false );
			SetScreen( Screen::Playing );
			return;
		}
	}

	float bw = 320 * S, bh = 72 * S, g = 30 * S;
	bool outro = count > 0 && m_progress.stars[cc.levels.back()] > 0;
	int buttons = outro ? 3 : 2;
	float bx = W * 0.5f - ( buttons * bw + ( buttons - 1 ) * g ) * 0.5f;
	if ( ui::Button( { bx, H - 130 * S, bw, bh }, "MAPPA" ) )
	{
		if ( m_audio )
			m_audio->Play( Sfx::Click );
		SetScreen( Screen::Map );
		return;
	}
	if ( ui::Button( { bx + bw + g, H - 130 * S, bw, bh }, "PROLOGO" ) )
	{
		if ( m_audio )
			m_audio->Play( Sfx::Click );
		ShowStory( m_campaign, false );
		return;
	}
	if ( outro && ui::Button( { bx + 2 * ( bw + g ), H - 130 * S, bw, bh }, "EPILOGO" ) )
	{
		if ( m_audio )
			m_audio->Play( Sfx::Click );
		ShowStory( m_campaign, true );
		return;
	}
}

void Game::DrawHowTo()
{
	float S = ui::S();
	int W = GetScreenWidth();
	int H = GetScreenHeight();
	DrawRectangle( 0, 0, W, H, Color{ 10, 15, 30, 130 } );

	Rectangle panel{ W * 0.5f - 700 * S, 60 * S, 1400 * S, H - 220 * S };
	ui::Panel( panel, Color{ 250, 238, 210, 240 }, Color{ 120, 70, 20, 255 }, 0.06f );
	Color ink{ 80, 40, 15, 255 };
	ui::TextCentered( "COME SI GIOCA", W * 0.5f, panel.y + 24 * S, 70, Color{ 200, 120, 20, 255 }, false );
	float x = panel.x + 60 * S;
	float y = panel.y + 120 * S;
	ui::Text( "Abbatti tutti i re nemici: falli cadere, ribaltare o colpiscili in pieno.", { x, y }, 32, ink );
	y += 60 * S;
	const char* lines[] = {
		"Mouse  -  ruota il cannone",
		"Rotellina / W-S  -  potenza del colpo (SHIFT per regolazioni fini)",
		"Click sinistro  -  spara (e in volo: torna al cannone)",
		"Click destro (tieni premuto)  -  cannocchiale",
		"1 - 7  oppure  Q / E  -  scegli la munizione",
		"SPAZIO  -  abilità speciale del proiettile in volo",
		"TAB  -  panoramica della fortezza    •    T  -  mira assistita",
		"R  -  ricomincia    •    O  -  ombre    •    M  -  musica    •    F11  -  schermo intero",
	};
	for ( const char* l : lines )
	{
		ui::Text( l, { x + 20 * S, y }, 26, ink );
		y += 36 * S;
	}
	y += 14 * S;
	const float rowH = 54 * S;
	for ( int i = 0; i < (int)Ammo::Count; ++i )
	{
		ui::AmmoIcon( i, { x + 40 * S + ( i % 2 ) * 640 * S, y + 20 * S + ( i / 2 ) * rowH }, 18 * S );
		ui::Text( TextFormat( "%s: %s", s_ammo[i].name, s_ammo[i].description ), { x + 76 * S + ( i % 2 ) * 640 * S, y + 6 * S + ( i / 2 ) * rowH },
				  22, ink );
	}
	y += ( ( (int)Ammo::Count + 1 ) / 2 ) * rowH + 6 * S;
	ui::Text( "Meno colpi usi, più stelle ottieni. Le munizioni avanzate valgono punti bonus.", { x, y }, 28, ink );
	y += 44 * S;
	ui::Text( "SFIDA INFINITA: fortezze sempre nuove, generate a caso. I punti si sommano round dopo round.", { x, y }, 28, ink );
	y += 44 * S;
	ui::Text( "Dopo ogni vittoria il REPLAY ri-simula il colpo decisivo dalla registrazione deterministica di Box3D.", { x, y }, 28, ink );

	if ( ui::Button( { W * 0.5f - 160 * S, H - 130 * S, 320 * S, 72 * S }, "INDIETRO" ) )
	{
		if ( m_audio )
			m_audio->Play( Sfx::Click );
		SetScreen( Screen::Title );
	}
}

void Game::DrawPause()
{
	float S = ui::S();
	int W = GetScreenWidth();
	int H = GetScreenHeight();
	DrawRectangle( 0, 0, W, H, Color{ 10, 15, 30, 150 } );
	Rectangle panel{ W * 0.5f - 300 * S, H * 0.5f - 350 * S, 600 * S, 640 * S };
	ui::Panel( panel, Color{ 250, 238, 210, 245 }, Color{ 120, 70, 20, 255 }, 0.08f );
	ui::TextCentered( "PAUSA", W * 0.5f, panel.y + 24 * S, 80, Color{ 200, 120, 20, 255 }, false );

	float bw = 440 * S, bh = 66 * S;
	float x = W * 0.5f - bw * 0.5f;
	float y = panel.y + 140 * S;
	if ( ui::Button( { x, y, bw, bh }, "RIPRENDI" ) )
	{
		SetScreen( Screen::Playing );
		return;
	}
	y += bh * 1.25f;
	if ( ui::Button( { x, y, bw, bh }, "RICOMINCIA" ) )
	{
		RestartLevel();
		return;
	}
	y += bh * 1.25f;
	if ( ui::Button( { x, y, bw, bh }, "LIVELLI" ) )
	{
		SetScreen( Screen::LevelSelect );
		return;
	}
	y += bh * 1.25f;
	if ( ui::Button( { x, y, bw, bh }, "MENU PRINCIPALE" ) )
	{
		SetScreen( Screen::Title );
		return;
	}
	y += bh * 1.5f;

	float hw = bw * 0.48f;
	if ( ui::Button( { x, y, hw, bh * 0.85f }, m_progress.music ? "Musica: sì" : "Musica: no" ) )
	{
		m_progress.music = !m_progress.music;
		if ( m_audio )
			m_audio->SetMusicEnabled( m_progress.music );
		SaveProgress();
	}
	if ( ui::Button( { x + bw - hw, y, hw, bh * 0.85f }, m_progress.sfx ? "Effetti: sì" : "Effetti: no" ) )
	{
		m_progress.sfx = !m_progress.sfx;
		if ( m_audio )
			m_audio->SetSfxEnabled( m_progress.sfx );
		SaveProgress();
	}
	y += bh * 1.1f;
	if ( ui::Button( { x, y, hw, bh * 0.85f }, m_progress.shadows ? "Ombre: sì" : "Ombre: no" ) )
	{
		m_progress.shadows = !m_progress.shadows;
		if ( m_renderer )
			m_renderer->shadowsEnabled = m_progress.shadows;
		SaveProgress();
	}
	if ( ui::Button( { x + bw - hw, y, hw, bh * 0.85f }, m_progress.aimAssist ? "Mira: assistita" : "Mira: normale" ) )
	{
		m_progress.aimAssist = !m_progress.aimAssist;
		SaveProgress();
	}
}

void Game::DrawResult( bool won )
{
	float S = ui::S();
	int W = GetScreenWidth();
	int H = GetScreenHeight();
	float t = m_screenTime;
	DrawRectangle( 0, 0, W, H, Color{ 10, 15, 30, (unsigned char)( 120 * Clamp01( t * 3.0f ) ) } );

	float slide = ( 1.0f - SmoothStep( 0.0f, 0.35f, t ) ) * 200 * S;
	Rectangle panel{ W * 0.5f - 380 * S, H * 0.5f - 330 * S + slide, 760 * S, 660 * S };
	ui::Panel( panel, Color{ 250, 238, 210, 245 }, Color{ 120, 70, 20, 255 }, 0.08f );
	Color ink{ 80, 40, 15, 255 };

	if ( won && m_challenge )
	{
		ui::TextOutlined( TextFormat( "ROUND %d SUPERATO!", m_round ), W * 0.5f, panel.y + 30 * S, 80, kGold, Color{ 110, 55, 10, 255 }, 5 );
		ui::TextCentered( TextFormat( "Punti del round: %d  (bonus munizioni %d)", m_score, m_bonus ), W * 0.5f, panel.y + 170 * S, 34, ink, false );
		ui::TextCentered( TextFormat( "Totale: %d", m_challengeTotal ), W * 0.5f, panel.y + 230 * S, 60, ink, false );
		ui::TextCentered( TextFormat( "Record: %d", m_progress.bestChallenge ), W * 0.5f, panel.y + 320 * S, 30, ink, false );
		if ( m_newBest )
		{
			ui::TextCentered( "Nuovo record!", W * 0.5f, panel.y + 360 * S, 32, Color{ 210, 60, 40, 255 }, false );
		}
	}
	else if ( m_challenge )
	{
		int total = m_challengeTotal + m_score;
		ui::TextOutlined( "SFIDA TERMINATA", W * 0.5f, panel.y + 30 * S, 84, Color{ 230, 110, 90, 255 }, Color{ 90, 20, 20, 255 }, 5 );
		ui::TextCentered( TextFormat( "Sei arrivato al round %d", m_round ), W * 0.5f, panel.y + 170 * S, 38, ink, false );
		ui::TextCentered( TextFormat( "Punti totali: %d", total ), W * 0.5f, panel.y + 230 * S, 56, ink, false );
		ui::TextCentered( TextFormat( "Record: %d  (round %d)", m_progress.bestChallenge, m_progress.bestRound ), W * 0.5f, panel.y + 320 * S, 30,
						  ink, false );
		if ( m_newBest )
		{
			ui::TextCentered( "Nuovo record!", W * 0.5f, panel.y + 360 * S, 32, Color{ 210, 60, 40, 255 }, false );
		}
	}
	else if ( won )
	{
		ui::TextOutlined( "VITTORIA!", W * 0.5f, panel.y + 20 * S, 100, kGold, Color{ 110, 55, 10, 255 }, 5 );
		for ( int s = 0; s < 3; ++s )
		{
			float appear = SmoothStep( 0.4f + s * 0.35f, 0.6f + s * 0.35f, t );
			bool got = s < m_starsEarned;
			float r = ( 52 + ( s == 1 ? 12 : 0 ) ) * S * ( got ? ( 0.6f + 0.4f * appear + 0.25f * sinf( appear * PI ) ) : 1.0f );
			Vector2 c{ W * 0.5f + ( s - 1 ) * 140 * S, panel.y + 200 * S - ( s == 1 ? 16 * S : 0 ) };
			ui::Star( c, r, got && appear > 0.0f ? kGold : Color{ 200, 190, 170, 255 }, got && appear > 0.0f ? Color{ 150, 80, 10, 255 } : Color{ 160, 150, 130, 255 },
					  got ? ( 1.0f - appear ) * 2.0f : 0.0f );
			// play a chime as each star lands
			float prevT = t - GetFrameTime();
			float land = 0.6f + s * 0.35f;
			if ( got && prevT < land && t >= land && m_audio )
			{
				m_audio->Play( Sfx::Star, 0.8f, 1.0f + s * 0.12f );
			}
		}
		ui::TextCentered( TextFormat( "Punti: %d", m_score ), W * 0.5f, panel.y + 290 * S, 48, ink, false );
		ui::TextCentered( TextFormat( "Colpi usati: %d  (per 3 stelle: %d)   •   Bonus munizioni: %d", m_shots, m_level ? m_level->par : 0, m_bonus ),
						  W * 0.5f, panel.y + 352 * S, 26, ink, false );
		if ( Cheating() )
		{
			ui::TextCentered( "Modalità trucchi: vittoria non salvata", W * 0.5f, panel.y + 392 * S, 32, Color{ 210, 60, 40, 255 }, false );
		}
		else if ( m_newBest )
		{
			ui::TextCentered( "Nuovo record!", W * 0.5f, panel.y + 392 * S, 32, Color{ 210, 60, 40, 255 }, false );
		}
	}
	else
	{
		ui::TextOutlined( "SCONFITTA", W * 0.5f, panel.y + 20 * S, 100, Color{ 230, 110, 90, 255 }, Color{ 90, 20, 20, 255 }, 5 );
		ui::TextCentered( TextFormat( "I re resistono ancora: %d rimasti", KingsRemaining() ), W * 0.5f, panel.y + 190 * S, 38, ink, false );
		ui::TextCentered( "Suggerimento: usa TAB per studiare la fortezza.", W * 0.5f, panel.y + 250 * S, 28, ink, false );
		ui::TextCentered( TextFormat( "Punti: %d", m_score ), W * 0.5f, panel.y + 310 * S, 44, ink, false );
	}

	float bw = 440 * S, bh = 70 * S;
	float x = W * 0.5f - bw * 0.5f;
	float y = panel.y + 440 * S;
	if ( m_challenge && won == false )
	{
		float hw2 = bw * 0.48f;
		if ( ui::Button( { x, y, hw2, bh }, "NUOVA SFIDA" ) )
		{
			if ( m_audio )
				m_audio->Play( Sfx::Click );
			LoadChallenge( 1, true );
			SetScreen( Screen::Playing );
			return;
		}
		if ( ui::Button( { x + bw - hw2, y, hw2, bh }, "MENU" ) )
		{
			if ( m_audio )
				m_audio->Play( Sfx::Click );
			SetScreen( Screen::Title );
			return;
		}
		return;
	}
	if ( won && ( m_challenge || NextInCampaign() >= 0 ) )
	{
		if ( ui::Button( { x, y, bw, bh }, m_challenge ? "PROSSIMO ROUND" : "PROSSIMO LIVELLO" ) )
		{
			if ( m_audio )
				m_audio->Play( Sfx::Click );
			NextLevel();
			return;
		}
		y += bh * 1.2f;
	}
	else if ( won && CampaignOfLevel( m_levelIndex ) >= 0 )
	{
		// the realm's last fortress: its fragment of the crown comes home
		if ( ui::Button( { x, y, bw, bh }, "EPILOGO" ) )
		{
			if ( m_audio )
				m_audio->Play( Sfx::Click );
			ShowStory( CampaignOfLevel( m_levelIndex ), true );
			return;
		}
		y += bh * 1.2f;
	}
	// bottom row: retry, replay, level list (a won challenge round cannot be retried for points)
	bool showRetry = !( m_challenge && won );
	bool showReplay = won && m_replayAvailable;
	int buttons = 1 + ( showRetry ? 1 : 0 ) + ( showReplay ? 1 : 0 );
	float gapB = 16 * S;
	float rowW = buttons == 3 ? 620 * S : bw;
	float hw = ( rowW - ( buttons - 1 ) * gapB ) / buttons;
	float rx = W * 0.5f - rowW * 0.5f;
	if ( showRetry )
	{
		if ( ui::Button( { rx, y, hw, bh }, won ? "RIPETI" : "RIPROVA" ) )
		{
			if ( m_audio )
				m_audio->Play( Sfx::Click );
			RestartLevel();
			return;
		}
		rx += hw + gapB;
	}
	if ( showReplay )
	{
		if ( ui::Button( { rx, y, hw, bh }, "REPLAY" ) )
		{
			if ( m_audio )
				m_audio->Play( Sfx::Click );
			if ( StartReplay() )
			{
				SetScreen( Screen::Replay );
				return;
			}
		}
		rx += hw + gapB;
	}
	if ( ui::Button( { rx, y, hw, bh }, m_challenge ? "MENU" : "LIVELLI" ) )
	{
		if ( m_audio )
			m_audio->Play( Sfx::Click );
		SetScreen( m_challenge ? Screen::Title : Screen::LevelSelect );
		return;
	}
}

// ---------------------------------------------------------------------------------------------
// Replay: Box3D records every mutation and step since the last shot. After a win, a b3RecPlayer
// rebuilds that world from the seed snapshot and re-simulates it deterministically, and we draw
// it from a cinematic camera in slow motion. Bodies are matched to visuals by their name (serial).
// ---------------------------------------------------------------------------------------------

void Game::AddReplayEvent( ReplayEvent::Type type, Vector3 pos, Vector3 dir, float radius, bool big )
{
	if ( m_recordingActive == false )
	{
		return;
	}
	m_replayEvents.push_back( { type, m_scene.stepCount, pos, dir, radius, big } );
}

void Game::RestartRecording()
{
	if ( m_headless || m_attract || m_scene.IsValid() == false )
	{
		return;
	}
	if ( m_recording == nullptr )
	{
		m_recording = b3CreateRecording( 1 << 20 );
	}
	StopRecording();
	b3World_StartRecording( m_scene.World(), m_recording );
	m_recordingActive = true;
	m_recordStartStep = m_scene.stepCount;
	m_replayEvents.clear();
	m_impactStep = -1;
	m_replayAvailable = false;
}

void Game::StopRecording()
{
	if ( m_recordingActive && m_scene.IsValid() )
	{
		b3World_StopRecording( m_scene.World() );
	}
	m_recordingActive = false;
}

static void ReadReplayBodies( b3RecPlayer* player, std::vector<Vector3>& pos, std::vector<Quaternion>& rot, std::vector<bool>& have,
							  std::vector<Vector3>& prevPos, std::vector<Quaternion>& prevRot, std::vector<int>& serialToOrd )
{
	int n = b3RecPlayer_GetBodyCount( player );
	if ( (int)pos.size() < n )
	{
		pos.resize( n );
		rot.resize( n );
		prevPos.resize( n );
		prevRot.resize( n );
		have.resize( n, false );
	}
	std::fill( serialToOrd.begin(), serialToOrd.end(), -1 );
	for ( int i = 0; i < n; ++i )
	{
		b3BodyId id = b3RecPlayer_GetBodyId( player, i );
		if ( B3_IS_NULL( id ) || b3Body_IsValid( id ) == false || b3Body_IsEnabled( id ) == false )
		{
			have[i] = false;
			continue;
		}
		b3WorldTransform xf = b3Body_GetTransform( id );
		pos[i] = ToRl( xf.p );
		rot[i] = ToRl( xf.q );
		if ( have[i] == false )
		{
			prevPos[i] = pos[i];
			prevRot[i] = rot[i];
			have[i] = true;
		}
		int serial = atoi( b3Body_GetName( id ) );
		if ( serial > 0 && serial < (int)serialToOrd.size() )
		{
			serialToOrd[serial] = i;
		}
	}
}

bool Game::StartReplay()
{
	if ( m_headless || m_recording == nullptr )
	{
		return false;
	}
	StopRecording();
	const uint8_t* data = b3Recording_GetData( m_recording );
	int size = b3Recording_GetSize( m_recording );
	if ( data == nullptr || size <= 0 )
	{
		return false;
	}
	if ( m_player )
	{
		b3DestroyPlayer( m_player );
		m_player = nullptr;
	}
	m_player = b3CreatePlayer( data, size, m_workers );
	if ( m_player == nullptr )
	{
		return false;
	}

	int frames = b3RecPlayer_GetFrameCount( m_player );
	int winFrame = m_winStep >= 0 ? m_winStep - m_recordStartStep : frames;
	int start = std::max( 0, winFrame - 600 );
	m_replayEndFrame = std::min( frames, winFrame + 45 );
	if ( start > 0 )
	{
		b3RecPlayer_SeekFrame( m_player, start );
	}

	m_replayAvailable = true;
	m_replayClock = 0.0f;
	m_replayTime = 0.0f;
	m_rpPos.clear();
	m_rpRot.clear();
	m_rpPrevPos.clear();
	m_rpPrevRot.clear();
	m_rpHave.clear();
	m_rpSerialToOrd.assign( m_scene.visuals.size(), -1 );
	ReadReplayBodies( m_player, m_rpPos, m_rpRot, m_rpHave, m_rpPrevPos, m_rpPrevRot, m_rpSerialToOrd );
	m_rpAlpha = 0.0f;
	m_rpGlobalStep = m_recordStartStep + b3RecPlayer_GetFrame( m_player );

	Vector3 flat = Vector3Normalize( { m_shotDir.x, 0.0f, m_shotDir.z } );
	Vector3 side = Vector3CrossProduct( flat, { 0, 1, 0 } );
	m_rpLook = Vector3Add( m_cannonPos, { 0, 1.0f, 0 } );
	m_rpCamPos = Vector3Add( m_cannonPos, Vector3Add( Vector3Scale( side, 9.0f ), Vector3Add( Vector3Scale( flat, -3.0f ), { 0, 3.0f, 0 } ) ) );
	m_rpOrbit = 0.0f;
	m_particles.Clear();
	m_texts.clear();
	m_screenFlash = 0.0f;
	return true;
}

void Game::EndReplay()
{
	if ( m_player && getenv( "CROLLO_DEBUG" ) )
	{
		fprintf( stderr, "Replay: frame %d/%d diverged=%d at %d\n", b3RecPlayer_GetFrame( m_player ), b3RecPlayer_GetFrameCount( m_player ),
				 b3RecPlayer_HasDiverged( m_player ), b3RecPlayer_GetDivergeFrame( m_player ) );
	}
	if ( m_player )
	{
		b3DestroyPlayer( m_player );
		m_player = nullptr;
	}
	m_particles.Clear();
	SetScreen( Screen::Won );
	if ( m_audio )
	{
		m_audio->Play( Sfx::Win, 0.9f );
	}
}

void Game::UpdateReplay( float dt )
{
	m_hitSoundsThisFrame = 0;
	m_replayTime += dt;
	if ( m_player == nullptr )
	{
		EndReplay();
		return;
	}
	if ( m_inputEnabled && m_replayTime > 0.3f &&
		 ( IsMouseButtonPressed( MOUSE_BUTTON_LEFT ) || IsKeyPressed( KEY_SPACE ) || IsKeyPressed( KEY_ESCAPE ) || IsKeyPressed( KEY_ENTER ) ) )
	{
		EndReplay();
		return;
	}

	// fast while the shot flies, very slow around the impact
	float speed = 0.6f;
	if ( m_impactStep >= 0 )
	{
		int d = m_rpGlobalStep - m_impactStep;
		speed = d < -30 ? 1.0f : ( d < 90 ? 0.3f : 0.8f );
	}

	m_replayClock += dt * speed;
	int guard = 0;
	b3WorldId world = b3RecPlayer_GetWorldId( m_player );
	while ( m_replayClock >= kFixedDt && guard < 4 )
	{
		int frame = b3RecPlayer_GetFrame( m_player );
		if ( frame >= m_replayEndFrame || b3RecPlayer_IsAtEnd( m_player ) )
		{
			EndReplay();
			return;
		}

		for ( size_t i = 0; i < m_rpPos.size(); ++i )
		{
			m_rpPrevPos[i] = m_rpPos[i];
			m_rpPrevRot[i] = m_rpRot[i];
		}
		b3RecPlayer_StepFrame( m_player );
		ReadReplayBodies( m_player, m_rpPos, m_rpRot, m_rpHave, m_rpPrevPos, m_rpPrevRot, m_rpSerialToOrd );
		m_rpGlobalStep = m_recordStartStep + b3RecPlayer_GetFrame( m_player );

		// effects the physics world cannot show by itself
		for ( const ReplayEvent& ev : m_replayEvents )
		{
			if ( ev.step != m_rpGlobalStep )
			{
				continue;
			}
			switch ( ev.type )
			{
				case ReplayEvent::CannonFire:
					m_particles.MuzzleBlast( ev.pos, ev.dir );
					if ( m_audio )
						m_audio->PlayAt( Sfx::Cannon, ev.pos, 0.8f, 0.8f );
					break;
				case ReplayEvent::Explosion:
					m_particles.Explosion( ev.pos, ev.radius );
					m_particles.Debris( ev.pos, Color{ 60, 50, 45, 255 }, 14, 9.0f, 0.12f );
					if ( m_audio )
						m_audio->PlayAt( Sfx::Explosion, ev.pos, 1.0f, 0.7f );
					Shake( 0.6f );
					break;
				case ReplayEvent::KingDown:
					m_particles.Stars( ev.pos, 14 );
					if ( m_audio )
						m_audio->PlayAt( Sfx::KingDown, ev.pos, 1.0f, 0.85f );
					break;
				case ReplayEvent::Shatter:
					m_particles.Sparkle( ev.pos, Color{ 200, 240, 255, 255 }, 18 );
					m_particles.Debris( ev.pos, Color{ 200, 235, 255, 255 }, 10, 5.0f, 0.1f );
					if ( m_audio )
						m_audio->PlayAt( Sfx::IceBreak, ev.pos, 0.9f, 0.8f );
					break;
				case ReplayEvent::Fell:
				{
					Color c{ (unsigned char)( ev.dir.x * 255 ), (unsigned char)( ev.dir.y * 255 ), (unsigned char)( ev.dir.z * 255 ), 255 };
					TreeFallEffects( ev.pos, c, ev.radius );
					break;
				}
				case ReplayEvent::BalloonPop:
				{
					Color c{ (unsigned char)( ev.dir.x * 255 ), (unsigned char)( ev.dir.y * 255 ), (unsigned char)( ev.dir.z * 255 ), 255 };
					m_particles.Debris( ev.pos, c, 22, 7.0f, 0.18f );
					if ( m_audio )
						m_audio->PlayAt( Sfx::Pop, ev.pos, 1.0f, 0.8f );
					break;
				}
				case ReplayEvent::Implosion:
					m_particles.Implosion( ev.pos, ev.radius );
					if ( m_audio )
						m_audio->PlayAt( Sfx::Implosion, ev.pos, 1.0f, 0.8f );
					Shake( 0.5f );
					break;
				case ReplayEvent::Stick:
					m_particles.Sparkle( ev.pos, Color{ 140, 230, 100, 255 }, 10 );
					if ( m_audio )
						m_audio->PlayAt( Sfx::Stick, ev.pos, 0.9f, 0.8f );
					break;
				case ReplayEvent::Snap:
					m_particles.Debris( ev.pos, Color{ 170, 140, 90, 255 }, 6, 3.0f, 0.06f );
					if ( m_audio )
						m_audio->PlayAt( Sfx::RopeSnap, ev.pos, 0.8f, 0.8f );
					break;
			}
		}

		// the replayed world reports its own collisions, so impacts sound and puff like the real thing
		b3ContactEvents ce = b3World_GetContactEvents( world );
		for ( int i = 0; i < ce.hitCount; ++i )
		{
			const b3ContactHitEvent& h = ce.hitEvents[i];
			Mat ma = (Mat)h.userMaterialIdA;
			Mat mb = (Mat)h.userMaterialIdB;
			Mat struck = ( ma == Mat::Metal || ma == Mat::Dark ) ? mb : ma;
			if ( struck == Mat::Grass )
			{
				struck = Mat::Rock;
			}
			HitEffects( ToRl( h.point ), h.approachSpeed, struck, 0.0f, true );
		}

		m_replayClock -= kFixedDt;
		++guard;
	}
	if ( guard == 4 )
	{
		m_replayClock = std::min( m_replayClock, kFixedDt );
	}
	m_rpAlpha = Clamp01( m_replayClock / kFixedDt );
	m_particles.Update( dt * speed, m_wind );

	// camera: follow the shot from the side, then circle the point of impact
	Vector3 up{ 0, 1, 0 };
	Vector3 flat = Vector3Normalize( { m_shotDir.x, 0.0f, m_shotDir.z } );
	Vector3 side = Vector3CrossProduct( flat, up );
	Vector3 proj{};
	bool haveProj = false;
	auto replayed = [&]( int serial, Vector3& out ) {
		if ( serial <= 0 || serial >= (int)m_rpSerialToOrd.size() || m_rpSerialToOrd[serial] < 0 )
		{
			return false;
		}
		int ord = m_rpSerialToOrd[serial];
		out = Vector3Lerp( m_rpPrevPos[ord], m_rpPos[ord], m_rpAlpha );
		return true;
	};
	for ( int k = (int)m_shotSerials.size() - 1; k >= 0 && haveProj == false; --k )
	{
		haveProj = replayed( m_shotSerials[k], proj );
	}
	Vector3 other;
	if ( haveProj && m_shotPartner > 0 && replayed( m_shotPartner, other ) )
	{
		float w = ChainSwing( ( m_rpGlobalStep - m_recordStartStep ) * kFixedDt );
		proj = Vector3Lerp( Vector3Lerp( proj, other, 0.5f ), proj, w );
	}

	bool beforeImpact = m_impactStep < 0 || m_rpGlobalStep < m_impactStep;
	Vector3 look, camTarget;
	float rate;
	if ( haveProj && beforeImpact )
	{
		look = proj;
		camTarget = Vector3Add( proj, Vector3Add( Vector3Scale( side, 9.0f ), Vector3Add( Vector3Scale( flat, -3.5f ), { 0, 2.0f, 0 } ) ) );
		rate = 6.0f;
	}
	else
	{
		Vector3 focus = m_impactStep >= 0 ? m_impactPoint : m_fortressCenter;
		m_rpOrbit += dt * 0.22f;
		Vector3 offset = Vector3Add( Vector3Scale( side, 11.0f ), Vector3Add( Vector3Scale( flat, -5.0f ), { 0, 4.0f, 0 } ) );
		offset = Vector3RotateByQuaternion( offset, QuaternionFromAxisAngle( up, m_rpOrbit ) );
		look = Vector3Add( focus, { 0, 0.8f, 0 } );
		camTarget = Vector3Add( focus, offset );
		rate = 1.6f;
	}
	m_rpLook = ExpDecay( m_rpLook, look, rate * 1.4f, dt );
	m_rpCamPos = ExpDecay( m_rpCamPos, camTarget, rate, dt );

	m_shake = ExpDecay( m_shake, 0.0f, 4.0f, dt );
	Vector3 shake{};
	if ( m_shake > 0.001f )
	{
		float t = m_time * 40.0f;
		shake = { sinf( t * 1.3f ) * m_shake * 0.2f, sinf( t * 1.7f + 1.0f ) * m_shake * 0.2f, 0.0f };
	}
	m_camera.position = Vector3Add( m_rpCamPos, shake );
	m_camera.target = m_rpLook;
	m_camera.up = up;
	m_camera.fovy = 45.0f;
	m_camera.projection = CAMERA_PERSPECTIVE;
}

void Game::DrawReplay()
{
	Renderer& r = *m_renderer;
	Vector3 mid = Vector3Lerp( m_cannonPos, m_fortressCenter, 0.5f );
	float radius = Vector3Distance( m_cannonPos, m_fortressCenter ) * 0.5f + m_fortressRadius + 6.0f;
	r.BeginScene( m_camera, mid, radius, m_time, m_wind );

	std::vector<Part> kingParts;
	for ( size_t i = 0; i < m_rpPos.size(); ++i )
	{
		if ( m_rpHave[i] == false )
		{
			continue;
		}
		b3BodyId id = b3RecPlayer_GetBodyId( m_player, (int)i );
		if ( B3_IS_NULL( id ) || b3Body_IsValid( id ) == false )
		{
			continue;
		}
		int serial = atoi( b3Body_GetName( id ) );
		if ( serial <= 0 || serial >= (int)m_scene.visuals.size() )
		{
			continue;
		}
		const VisualRecord& vr = m_scene.visuals[serial];
		Vector3 pos = Vector3Lerp( m_rpPrevPos[i], m_rpPos[i], m_rpAlpha );
		Quaternion rot = QuaternionNlerp( m_rpPrevRot[i], m_rpRot[i], m_rpAlpha );
		if ( vr.kind == Kind::King && vr.defeatStep >= 0 && vr.defeatStep <= m_rpGlobalStep )
		{
			// knocked out at this point of the replay: the crown has flown and the eyes are shut
			kingParts = vr.parts;
			for ( Part& p : kingParts )
			{
				if ( p.geo == Geo::Hull && p.mat == Mat::Gold )
				{
					p.visible = false;
				}
				if ( p.mat == Mat::Dark )
				{
					p.size.y = 0.012f;
					p.size.x = 0.05f;
				}
			}
			r.AddParts( kingParts, pos, rot, 0.0f );
		}
		else
		{
			r.AddParts( vr.parts, pos, rot, 0.0f );
		}
	}

	CannonPose pose;
	pose.position = m_cannonPos;
	pose.yaw = atan2f( m_shotDir.x, m_shotDir.z );
	pose.pitch = asinf( Clamp( m_shotDir.y, -1.0f, 1.0f ) );
	pose.recoil = 0.0f;
	pose.wheelSpin = 0.0f;
	pose.visible = true;
	r.AddCannon( pose );

	for ( const Decoration& d : m_decorations )
	{
		r.AddDecoration( d );
	}

	// ropes that are not meant to snap can be drawn from the replayed bodies
	auto endpoint = [&]( int serial, b3Vec3 local, Vector3& out ) {
		if ( serial == 0 )
		{
			out = ToRl( local );
			return true;
		}
		if ( serial < 0 || serial >= (int)m_rpSerialToOrd.size() || m_rpSerialToOrd[serial] < 0 )
		{
			return false;
		}
		int ord = m_rpSerialToOrd[serial];
		Vector3 pos = Vector3Lerp( m_rpPrevPos[ord], m_rpPos[ord], m_rpAlpha );
		Quaternion rot = QuaternionNlerp( m_rpPrevRot[ord], m_rpRot[ord], m_rpAlpha );
		out = Vector3Add( pos, Vector3RotateByQuaternion( ToRl( local ), rot ) );
		return true;
	};
	for ( const Rope& rope : m_scene.ropes )
	{
		if ( rope.breakable )
		{
			continue;
		}
		Vector3 a, b;
		if ( endpoint( rope.serialA, rope.localA, a ) == false || endpoint( rope.serialB, rope.localB, b ) == false )
		{
			continue;
		}
		float sag = 0.0f;
		if ( rope.ropeLength > 0.0f )
		{
			float d = Vector3Distance( a, b );
			if ( d < rope.ropeLength )
			{
				sag = sqrtf( rope.ropeLength * rope.ropeLength - d * d ) * 0.5f;
			}
		}
		r.AddRope( a, b, rope.radius, sag, rope.color );
	}

	r.RenderShadows();
	r.RenderSky();
	BeginMode3D( m_camera );
	r.RenderOpaque();
	for ( const FlagInfo& f : m_flags )
	{
		Vector3 top = Vector3Add( f.base, { 0, 2.5f * f.scale, 0 } );
		r.DrawFlagCloth( top, 1.3f * f.scale, 0.8f * f.scale, f.color, f.phase );
	}
	m_particles.Draw( r, m_camera );
	EndMode3D();
	r.EndScene();
}

void Game::DrawReplayOverlay()
{
	float S = ui::S();
	int W = GetScreenWidth();
	int H = GetScreenHeight();
	float bar = H * 0.1f * Clamp01( m_replayTime * 3.0f );
	DrawRectangle( 0, 0, W, (int)bar, BLACK );
	DrawRectangle( 0, H - (int)bar, W, (int)bar + 1, BLACK );
	if ( fmodf( m_replayTime, 1.0f ) < 0.6f )
	{
		DrawCircleV( { 44 * S, bar * 0.5f }, 11 * S, Color{ 230, 40, 40, 255 } );
	}
	ui::Text( "REPLAY", { 66 * S, bar * 0.5f - 22 * S }, 44, WHITE );
	ui::Text( "ricostruito dalla registrazione di Box3D", { 250 * S, bar * 0.5f - 12 * S }, 24, Color{ 200, 200, 200, 255 } );
	const char* skip = "click per saltare";
	Vector2 m = ui::Measure( skip, 26 );
	ui::Text( skip, { W - m.x - 30 * S, H - bar * 0.5f - m.y * 0.5f }, 26, Color{ 220, 220, 220, 200 } );
}
