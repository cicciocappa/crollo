// CROLLO - a physics siege game built on Box3D.
//
// Usage:
//   crollo                       play
//   crollo --debug               play with every level unlocked (progress is not changed)
//   crollo --autotest [shots] [level]
//                                headless: check every level (or just one, 1-based) is stable and winnable
//   crollo --scan-shots [level]  headless: the most kings a single shot can knock down, per ammunition
//   crollo --shot <mode> <level> <frames> <out.png>
//                                render a screenshot (modes: aim, fire, title, select, won)

#include "audio.h"
#include "game.h"
#include "levels.h"
#include "render.h"
#include "ui.h"

#if defined( __EMSCRIPTEN__ )
#include <emscripten/emscripten.h>
#endif

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// only: 1-based level number to test alone, 0 for all of them
[[maybe_unused]] static int RunAutoTest( int maxShots, int only )
{
	Game game( true );
	game.Init( nullptr, nullptr );
	int passed = 0, tested = 0;
	for ( int i = 0; i < LevelCount(); ++i )
	{
		if ( only != 0 && only != i + 1 )
		{
			continue;
		}
		++tested;
		if ( game.RunAutoTest( i, maxShots, true ) )
		{
			++passed;
		}
	}
	printf( "Superati %d/%d livelli\n", passed, tested );
	return passed == tested ? 0 : 1;
}

#if defined( __EMSCRIPTEN__ )
// The browser owns the loop: one call per animation frame.
static void WebFrame( void* arg )
{
	Game* game = (Game*)arg;
	game->Update( GetFrameTime() );
	BeginDrawing();
	ClearBackground( BLACK );
	game->Draw();
	EndDrawing();
}

int main()
{
	unsigned int flags = FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE;
	SetConfigFlags( flags );
	int w = EM_ASM_INT( return window.innerWidth; );
	int h = EM_ASM_INT( return window.innerHeight; );
	InitWindow( std::max( w, 320 ), std::max( h, 240 ), "Crollo" );
	SetExitKey( KEY_NULL );

	ui::Init();
	static Renderer renderer;
	renderer.Init();
	static Audio audio;
	audio.Init();
	static Game game( false );
	game.Init( &renderer, &audio );

	emscripten_set_main_loop_arg( WebFrame, &game, 0, 1 );
	return 0;
}
#else
int main( int argc, char** argv )
{
	if ( argc >= 2 && strcmp( argv[1], "--autotest-challenge" ) == 0 )
	{
		SetTraceLogLevel( LOG_WARNING );
		int rounds = argc >= 3 ? atoi( argv[2] ) : 10;
		int seeds = argc >= 4 ? atoi( argv[3] ) : 3;
		Game game( true );
		game.Init( nullptr, nullptr );
		int passed = 0, total = 0;
		for ( int r = 1; r <= rounds; ++r )
		{
			for ( int k = 0; k < seeds; ++k )
			{
				total += 1;
				passed += game.RunChallengeTest( r, 1000u + (uint32_t)( k * 7919 + r * 13 ), 20, true ) ? 1 : 0;
			}
		}
		printf( "Round superati %d/%d\n", passed, total );
		return 0;
	}

	if ( argc >= 2 && strcmp( argv[1], "--scan-shots" ) == 0 )
	{
		SetTraceLogLevel( LOG_WARNING );
		Game game( true );
		game.Init( nullptr, nullptr );
		int only = argc >= 3 ? atoi( argv[2] ) : 0;
		for ( int i = 0; i < LevelCount(); ++i )
		{
			if ( only == 0 || only == i + 1 )
			{
				game.ScanForEasyShots( i );
			}
		}
		return 0;
	}

	if ( argc >= 2 && strcmp( argv[1], "--test-ammo" ) == 0 )
	{
		SetTraceLogLevel( LOG_WARNING );
		Game game( true );
		game.Init( nullptr, nullptr );
		game.TestMaterialsAndAmmo();
		return 0;
	}

	if ( argc >= 2 && strcmp( argv[1], "--test-campaigns" ) == 0 )
	{
		Game game( true );
		game.Init( nullptr, nullptr );
		game.TestCampaigns();
		return 0;
	}
	if ( argc >= 2 && strcmp( argv[1], "--test-shields" ) == 0 )
	{
		SetTraceLogLevel( LOG_WARNING );
		Game game( true );
		game.Init( nullptr, nullptr );
		game.TestShields();
		return 0;
	}

	if ( argc >= 2 && strcmp( argv[1], "--autotest" ) == 0 )
	{
		SetTraceLogLevel( LOG_WARNING );
		int shots = argc >= 3 ? atoi( argv[2] ) : 12;
		int only = argc >= 4 ? atoi( argv[3] ) : 0;
		return RunAutoTest( shots, only );
	}

	if ( argc >= 3 && strcmp( argv[1], "--export-audio" ) == 0 )
	{
		Audio::ExportAll( argv[2], 30.0f );
		return 0;
	}

	const char* shotMode = nullptr;
	int shotLevel = 0;
	int shotFrames = 120;
	const char* shotFile = nullptr;
	if ( argc >= 6 && strcmp( argv[1], "--shot" ) == 0 )
	{
		shotMode = argv[2];
		shotLevel = atoi( argv[3] );
		shotFrames = atoi( argv[4] );
		shotFile = argv[5];
	}

	unsigned int flags = FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE;
	if ( getenv( "CROLLO_BENCH" ) == nullptr )
	{
		flags |= FLAG_VSYNC_HINT;
	}
	SetConfigFlags( flags );
	InitWindow( 1600, 900, "Crollo - un assedio fisico (Box3D)" );
	SetExitKey( KEY_NULL );
	SetTargetFPS( shotMode ? 0 : 144 );

	ui::Init();
	Renderer renderer;
	renderer.Init();
	Audio audio;
	if ( shotMode == nullptr )
	{
		audio.Init();
	}

	Game game( false );
	game.Init( &renderer, &audio );
	for ( int i = 1; i < argc; ++i )
	{
		if ( strcmp( argv[i], "--debug" ) == 0 )
		{
			game.SetDebug( true );
		}
	}

	if ( shotMode )
	{
		game.SetInputEnabled( false );
		if ( strcmp( shotMode, "title" ) == 0 )
		{
			game.SetScreen( Screen::Title );
		}
		else if ( strcmp( shotMode, "select" ) == 0 )
		{
			game.SelectCampaign( shotLevel ); // the "level" argument picks the campaign here
			game.SetScreen( Screen::LevelSelect );
		}
		else if ( strcmp( shotMode, "map" ) == 0 )
		{
			game.SetScreen( Screen::Map );
		}
		else if ( strcmp( shotMode, "story" ) == 0 || strcmp( shotMode, "outro" ) == 0 )
		{
			game.ShowStory( shotLevel, strcmp( shotMode, "outro" ) == 0 );
		}
		else if ( strcmp( shotMode, "howto" ) == 0 )
		{
			game.SetScreen( Screen::HowTo );
		}
		else if ( strcmp( shotMode, "challenge" ) == 0 )
		{
			SetRandomSeed( 42 + shotLevel );
			game.LoadChallenge( std::max( 1, shotLevel ), true );
			game.SetScreen( Screen::Playing );
			game.SkipIntro();
		}
		else if ( strcmp( shotMode, "pause" ) == 0 )
		{
			game.LoadLevel( shotLevel, false );
			game.SkipIntro();
			game.SetScreen( Screen::Paused );
		}
		else
		{
			game.LoadLevel( shotLevel, false );
			game.SetScreen( Screen::Playing );
			if ( strcmp( shotMode, "intro" ) != 0 )
			{
				game.SkipIntro();
			}
		}
	}

	int frame = 0;
	double benchStart = GetTime();
	double worst = 0.0;
	while ( WindowShouldClose() == false && game.WantsQuit() == false )
	{
		double frameStart = GetTime();
		float dt = shotMode ? 1.0f / 60.0f : GetFrameTime();
		if ( shotMode && strcmp( shotMode, "fire" ) == 0 && frame == 30 )
		{
			game.AutoFireAtKing();
		}
		// "fireall": keep shooting every few seconds until the level is won (exercises the replay)
		static int nextShot = 30;
		if ( shotMode && strcmp( shotMode, "fireall" ) == 0 && frame >= nextShot && game.LevelWon() == false &&
			 game.CurrentScreen() == Screen::Playing )
		{
			nextShot = game.AutoFireAtKing() ? frame + 300 : frame + 1;
		}
		game.Update( dt );

		BeginDrawing();
		ClearBackground( BLACK );
		game.Draw();
		if ( shotMode == nullptr && IsKeyDown( KEY_F3 ) )
		{
			DrawFPS( 10, 10 );
		}
		EndDrawing();

		++frame;
		if ( frame > 5 )
		{
			worst = std::max( worst, GetTime() - frameStart );
		}
		if ( shotMode && frame >= shotFrames )
		{
			if ( getenv( "CROLLO_BENCH" ) )
			{
				printf( "frames %d  avg %.2f ms  worst %.2f ms\n", frame, 1000.0 * ( GetTime() - benchStart ) / frame, 1000.0 * worst );
			}
			Image img = LoadImageFromScreen();
			ExportImage( img, shotFile );
			UnloadImage( img );
			break;
		}
	}

	audio.Shutdown();
	renderer.Shutdown();
	ui::Shutdown();
	CloseWindow();
	return 0;
}
#endif
