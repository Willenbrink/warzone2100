/*
	This file is part of Warzone 2100.
	Copyright (C) 1999-2004  Eidos Interactive
	Copyright (C) 2005-2017  Warzone 2100 Project

	Warzone 2100 is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	Warzone 2100 is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with Warzone 2100; if not, write to the Free Software
	Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
*/
/** @file
 *  The main file that launches the game and starts up everything.
 */

// Get platform defines before checking for them!
#include "lib/framework/wzapp.h"

#include <errno.h>

#include "lib/framework/input.h"
#include "lib/framework/physfs_ext.h"
#include "lib/framework/wzpaths.h"
#include "lib/exceptionhandler/exceptionhandler.h"
#include "lib/exceptionhandler/dumpinfo.h"

#include "lib/sound/playlist.h"
#include "lib/gamelib/gtime.h"
#include "lib/ivis_opengl/pieblitfunc.h"
#include "lib/ivis_opengl/piestate.h"
#include "lib/ivis_opengl/piepalette.h"
#include "lib/ivis_opengl/piemode.h"
#include "lib/ivis_opengl/screen.h"
#include "lib/netplay/netplay.h"
#include "lib/script/script.h"
#include "lib/sound/audio.h"
#include "lib/sound/cdaudio.h"

#include "challenge.h"
#include "configuration.h"
#include "display.h"
#include "display3d.h"
#include "frontend.h"
#include "game.h"
#include "init.h"
#include "levels.h"
#include "lighting.h"
#include "loadsave.h"
#include "loop.h"
#include "mission.h"
#include "modding.h"
#include "multiplay.h"
#include "qtscript.h"
#include "research.h"
#include "scripttabs.h"
#include "seqdisp.h"
#include "warzoneconfig.h"
#include "main.h"
#include "wrappers.h"
#include "version.h"
#include "map.h"
#include "keybind.h"
#include <time.h>

/* Always use fallbacks on Windows */
#if !defined(WZ_DATADIR)
#  define WZ_DATADIR "data"
#endif

bool customDebugfile = false;		// Default false: user has NOT specified where to store the stdout/err file.

char datadir[PATH_MAX] = ""; // Global that src/clparse.c:ParseCommandLine can write to, so it can override the default datadir on runtime. Needs to be empty on startup for ParseCommandLine to work!
char configdir[PATH_MAX] = ""; // specifies custom USER directory. Same rules apply as datadir above.
char rulesettag[40] = "";

//flag to indicate when initialisation is complete
bool	gameInitialised = false;
char	SaveGamePath[PATH_MAX];
char	ScreenDumpPath[PATH_MAX];
char	MultiCustomMapsPath[PATH_MAX];
char	MultiPlayersPath[PATH_MAX];
char	KeyMapPath[PATH_MAX];
// Start game in title mode:
// Status of the gameloop
static GAMECODE gameLoopStatus = GAMECODE_CONTINUE;

int getMaxPlayers()
{
  return MAX_PLAYERS;
}

bool isRunning()
{
  return gameInitialised;
}

void setRunning(bool value)
{
	gameInitialised = value;
}

static bool wz_autogame = false;
static std::string wz_saveandquit;
static std::string wz_test;

void setConfigdir(char *arg)
{
  sstrcpy(configdir, arg);
}

void setDatadir(char *arg)
{
  sstrcpy(datadir, arg);
}

void setDebug(char *arg)
{
  debug_enable_switch(arg);
}

void setDebugfile(char *arg)
{
  WzString debug_filename = arg;
  debug_register_callback(debug_callback_file, debug_callback_file_init, debug_callback_file_exit, &debug_filename); // note: by the time this function returns, all use of debug_filename has completed
  customDebugfile = true;
}

void autogame()
{
  wz_autogame = true;
}

bool autogame_enabled()
{
	return wz_autogame;
}

const std::string &saveandquit_enabled()
{
	return wz_saveandquit;
}

const std::string &wz_skirmish_test()
{
	return wz_test;
}

// Retrieves the appropriate storage directory for application-created files / prefs
// (Ensures the directory exists. Creates folders if necessary.)
static std::string getPlatformPrefDir(const char * org, const std::string &app)
{
	const char * prefsDir = PHYSFS_getPrefDir(org, app.c_str());

	if (prefsDir == nullptr)
	{
		debug(LOG_FATAL, "Failed to obtain prefs directory: %s", WZ_PHYSFS_getLastError());
		exit(1);
	}

	return std::string(prefsDir) + PHYSFS_getDirSeparator();
}

bool endsWith(std::string const &fullString, std::string const &endString)
{
  return !fullString.compare(fullString.length() - endString.length(), endString.length(), endString);
}

static void initialize_ConfigDir()
{
	std::string configDir =
    getPlatformPrefDir("Warzone 2100 Project", version_getVersionedAppDirFolderName());

	if (!PHYSFS_setWriteDir(configDir.c_str())) // Workaround for PhysFS not creating the writedir as expected.
	{
		debug(LOG_FATAL, "Error setting write directory to \"%s\": %s",
		      configDir.c_str(), WZ_PHYSFS_getLastError());
		exit(1);
	}

	if (!OverrideRPTDirectory(configDir.c_str()))
	{
		// since it failed, we just use our default path, and not the user supplied one.
		debug(LOG_ERROR, "Error setting exception handler to use directory %s", configDir.c_str());
	}

	// Config dir first so we always see what we write
	PHYSFS_mount(PHYSFS_getWriteDir(), NULL, PHYSFS_PREPEND);

	PHYSFS_permitSymbolicLinks(1);

	debug(LOG_WZ, "Write dir: %s", PHYSFS_getWriteDir());
	debug(LOG_WZ, "Base dir: %s", PHYSFS_getBaseDir());
}

/*!
 * \brief Adds default data dirs
 *
 * Priority:
 * Lower loads first. Current:
 * --datadir > User's home dir > source tree data > AutoPackage > BaseDir > DEFAULT_DATADIR
 *
 * Only --datadir and home dir are always examined. Others only if data still not found.
 *
 * We need ParseCommandLine, before we can add any mods...
 *
 * \sa rebuildSearchPath
 */
static void scanDataDirs()
{
	// Commandline supplied datadir
	if (strlen(datadir) != 0)
	{
		registerSearchPath(datadir, 1);
	}

	// User's home dir
	registerSearchPath(PHYSFS_getWriteDir(), 2);
	rebuildSearchPath(mod_multiplay, true);

	// Check PREFIX-based paths
	std::string tmpstr;

	// Find out which PREFIX we are in...
	std::string prefix = getWZInstallPrefix();
	std::string dirSeparator(PHYSFS_getDirSeparator());

	if (!PHYSFS_exists("gamedesc.lev"))
	{
		// Data in source tree (<prefix>/data/)
		tmpstr = prefix + dirSeparator + "data" + dirSeparator;
		registerSearchPath(tmpstr.c_str(), 3);
		rebuildSearchPath(mod_multiplay, true);

		if (!PHYSFS_exists("gamedesc.lev"))
		{
			// Relocation for AutoPackage (<prefix>/share/warzone2100/)
			tmpstr = prefix + dirSeparator + "share" + dirSeparator + "warzone2100" + dirSeparator;
			registerSearchPath(tmpstr.c_str(), 4);
			rebuildSearchPath(mod_multiplay, true);

			if (!PHYSFS_exists("gamedesc.lev"))
			{
				// Program dir
				registerSearchPath(PHYSFS_getBaseDir(), 5);
				rebuildSearchPath(mod_multiplay, true);

				if (!PHYSFS_exists("gamedesc.lev"))
				{
					// Guessed fallback default datadir on Unix
					std::string wzDataDir = WZ_DATADIR;

					if (!wzDataDir.empty())
					{
#ifndef WZ_DATADIR_ISABSOLUTE
						// Treat WZ_DATADIR as a relative path - append to the install PREFIX
						tmpstr = prefix + dirSeparator + wzDataDir;
						registerSearchPath(tmpstr.c_str(), 6);
						rebuildSearchPath(mod_multiplay, true);
#else
						// Treat WZ_DATADIR as an absolute path, and use directly
						registerSearchPath(wzDataDir.c_str(), 6);
						rebuildSearchPath(mod_multiplay, true);
#endif
					}

					if (!PHYSFS_exists("gamedesc.lev"))
					{
						// Guessed fallback default datadir on Unix
						// TEMPORARY: Fallback to ensure old WZ_DATADIR behavior as a last-resort
						//			  This is only present for the benefit of the automake build toolchain.
						registerSearchPath(WZ_DATADIR, 7);
						rebuildSearchPath(mod_multiplay, true);
					}
				}
			}
		}
	}

	/** Debugging and sanity checks **/

	printSearchPath();

	if (PHYSFS_exists("gamedesc.lev"))
	{
		debug(LOG_WZ, "gamedesc.lev found at %s", PHYSFS_getRealDir("gamedesc.lev"));
	}
	else
	{
		debug(LOG_FATAL, "Could not find game data. Aborting.");
		exit(1);
	}
}


/***************************************************************************
	Make a directory in write path and set a variable to point to it.
***************************************************************************/
static void make_dir(char *dest, const char *dirname, const char *subdir)
{
	strcpy(dest, dirname);

	if (subdir != nullptr)
	{
		strcat(dest, "/");
		strcat(dest, subdir);
	}

	{
		size_t l = strlen(dest);

		if (dest[l - 1] != '/')
		{
			dest[l] = '/';
			dest[l + 1] = '\0';
		}
	}

	if (!PHYSFS_mkdir(dest))
	{
		debug(LOG_FATAL, "Unable to create directory \"%s\" in write dir \"%s\"!",
		      dest, PHYSFS_getWriteDir());
		exit(EXIT_FAILURE);
	}
}

/*!
 * Preparations before entering the game loop
 * Would start the timer in an event based mainloop
 */
void startGameLoop()
{
  setRunning(true);

	// Not sure what aLevelName is, in relation to game.map. But need to use aLevelName here, to be able to start the right map for campaign, and need game.hash, to start the right non-campaign map, if there are multiple identically named maps.
	if (!levLoadData(aLevelName, &game.hash, nullptr, GTYPE_SCENARIO_START))
	{
		debug(LOG_FATAL, "Shutting down after failure");
		exit(EXIT_FAILURE);
	}

	screen_StopBackDrop();

	// Trap the cursor if cursor snapping is enabled
	if (war_GetTrapCursor())
	{
		wzGrabMouse();
	}

	// Disable resizable windows if it's a multiplayer game
	if (runningMultiplayer())
	{
		// This is because the main loop gets frozen while the window resizing / edge dragging occurs
		// which effectively pauses the game, and pausing is otherwise disabled in multiplayer games.
		// FIXME: Figure out a workaround?
		wzSetWindowIsResizable(false);
	}

	if (challengeActive)
	{
		addMissionTimerInterface();
	}

	if (game.type == SKIRMISH)
	{
		eventFireCallbackTrigger((TRIGGER_TYPE)CALL_START_NEXT_LEVEL);
	}

	triggerEvent(TRIGGER_START_LEVEL);
	screen_disableMapPreview();
}


/*!
 * Shutdown/cleanup after the game loop
 * Would stop the timer
 */
void stopGameLoop()
{
	clearInfoMessages(); // clear CONPRINTF messages before each new game/mission

	if (gameLoopStatus != GAMECODE_NEWLEVEL)
	{
		clearBlueprints();
		initLoadingScreen(true); // returning to f.e. do a loader.render not active
		pie_EnableFog(false); // don't let the normal loop code set status on

		if (gameLoopStatus != GAMECODE_LOADGAME)
		{
			if (!levReleaseAll())
			{
				debug(LOG_ERROR, "levReleaseAll failed!");
			}
		}

		closeLoadingScreen();
		reloadMPConfig();
	}

	// Disable cursor trapping
	if (war_GetTrapCursor())
	{
		wzReleaseMouse();
	}

	// Re-enable resizable windows
	if (!wzIsFullscreen())
	{
		// FIXME: This is required because of the disabling in startGameLoop()
		wzSetWindowIsResizable(true);
	}
}

// for backend detection
extern const char *BACKEND;

void initPhysFS()
{
  //NOTE: Apparently passing NULL on Linux is bad but it seems to work...
  PHYSFS_init(NULL);
	PHYSFS_mkdir("challenges");	// custom challenges
	PHYSFS_mkdir("logs");		// netplay, mingw crash reports & WZ logs
	PHYSFS_mkdir("mods/autoload");	// mods that are automatically loaded
	PHYSFS_mkdir("mods/campaign");	// campaign only mods activated with --mod_ca=example.wz
	PHYSFS_mkdir("mods/downloads");	// mod download directory
	PHYSFS_mkdir("mods/global");	// global mods activated with --mod=example.wz
	PHYSFS_mkdir("mods/multiplay");	// multiplay only mods activated with --mod_mp=example.wz
	PHYSFS_mkdir("mods/music");	// music mods that are automatically loaded
	PHYSFS_mkdir("music");	// custom music overriding default music and music mods
	PHYSFS_mkdir("savegames/campaign");		// campaign save games
	PHYSFS_mkdir("savegames/skirmish");		// skirmish save games
	PHYSFS_mkdir("tests");			// test games launched with --skirmish=game
	PHYSFS_mkdir("userdata");		// per-mod data user generated data
	PHYSFS_mkdir("userdata/campaign");	// contains campaign templates
	PHYSFS_mkdir("userdata/mp");		// contains multiplayer templates

	initialize_ConfigDir();
	make_dir(MultiCustomMapsPath, "maps", nullptr); // needed to prevent crashes when getting map
	make_dir(MultiPlayersPath, "multiplay", "players"); // player profiles
	make_dir(SaveGamePath, "savegames", nullptr); 	// save games
	make_dir(ScreenDumpPath, "screenshots", nullptr);	// for screenshots
}

void initMods()
{
  char modtocheck[256];
  PHYSFS_Stat metaData;

  // check whether given global mods are regular files
  for (auto iterator = global_mods.begin(); iterator != global_mods.end();)
  {
    ssprintf(modtocheck, "mods/global/%s", iterator->c_str());
    PHYSFS_stat(modtocheck, &metaData);

    if (metaData.filetype != PHYSFS_FILETYPE_REGULAR)
    {
      debug(LOG_ERROR, "The global mod \"%s\" you have specified doesn't exist!", iterator->c_str());
      global_mods.erase(iterator);
      rebuildSearchPath(mod_multiplay, true);
    }
    else
    {
      info("global mod \"%s\" is enabled", iterator->c_str());
      ++iterator;
    }
  }

  // check whether given campaign mods are regular files
  for (auto iterator = campaign_mods.begin(); iterator != campaign_mods.end();)
  {
    ssprintf(modtocheck, "mods/campaign/%s", iterator->c_str());
    PHYSFS_stat(modtocheck, &metaData);

    if (metaData.filetype != PHYSFS_FILETYPE_REGULAR)
    {
      debug(LOG_ERROR, "The campaign mod \"%s\" you have specified doesn't exist!", iterator->c_str());
      campaign_mods.erase(iterator);
      rebuildSearchPath(mod_campaign, true);
    }
    else
    {
      info("campaign mod \"%s\" is enabled", iterator->c_str());
      ++iterator;
    }
  }
  // check whether given multiplay mods are regular files
  for (auto iterator = multiplay_mods.begin(); iterator != multiplay_mods.end();)
  {
    ssprintf(modtocheck, "mods/multiplay/%s", iterator->c_str());
    PHYSFS_stat(modtocheck, &metaData);

    if (metaData.filetype != PHYSFS_FILETYPE_REGULAR)
      {
        debug(LOG_ERROR, "The multiplay mod \"%s\" you have specified doesn't exist!", iterator->c_str());
        multiplay_mods.erase(iterator);
        rebuildSearchPath(mod_multiplay, true);
      }
    else
      {
        info("multiplay mod \"%s\" is enabled", iterator->c_str());
        ++iterator;
      }
  }
}

void init()
{
  //TODO move somewhere appropriate
  setlocale(LC_NUMERIC, "C");
	memset(rulesettag, 0, sizeof(rulesettag)); // stores tag property of ruleset.json files
  if (!customDebugfile) {
    // there was no custom debug file specified  (--debug-file=blah)
    // so we use our write directory to store our logs.
    time_t aclock;
    struct tm *newtime;
    char buf[PATH_MAX];

    time(&aclock);					// Get time in seconds
    newtime = localtime(&aclock);		// Convert time to struct
    // Note: We are using fopen(), and not physfs routines to open the file
    // log name is logs/(or \)WZlog-MMDD_HHMMSS.txt
    snprintf(buf, sizeof(buf), "%slogs%sWZlog-%02d%02d_%02d%02d%02d.txt", PHYSFS_getWriteDir(), PHYSFS_getDirSeparator(),
             newtime->tm_mon + 1, newtime->tm_mday, newtime->tm_hour, newtime->tm_min, newtime->tm_sec);
    WzString debug_filename = buf;
    debug_register_callback(debug_callback_file, debug_callback_file_init, debug_callback_file_exit, &debug_filename); // note: by the time this function returns, all use of debug_filename has completed

    debug(LOG_WZ, "Using %s debug file", buf);
  }

	/* Put in the writedir root */
	sstrcpy(KeyMapPath, "keymap.json");

	// initialise all the command line states
	war_SetDefaultStates();

	loadConfig();

	// Save new (commandline) settings
	saveConfig();

	// Find out where to find the data
	scanDataDirs();

  initMods();
}

void init2 ()
{
	float horizScaleFactor, vertScaleFactor;
	wzGetGameToRendererScaleFactor(&horizScaleFactor, &vertScaleFactor);
	debug(LOG_WZ, "Game to Renderer Scale Factor (w x h): %f x %f", horizScaleFactor, vertScaleFactor);

	debug(LOG_MAIN, "Final initialization");

	frameInitialise();
	screenInitialise();
	pie_LoadShaders();

	unsigned int windowWidth = 0, windowHeight = 0;
	wzGetWindowResolution(&windowWidth, &windowHeight);
	war_SetWidth(windowWidth);
	war_SetHeight(windowHeight);

	pie_SetFogStatus(false);
	pie_ScreenFlip(CLEAR_BLACK);

	pal_Init();

	pie_LoadBackDrop(SCREEN_RANDOMBDROP);
	pie_SetFogStatus(false);
	pie_ScreenFlip(CLEAR_BLACK);

	systemInitialise(horizScaleFactor, vertScaleFactor);

	//set all the pause states to false
	setAllPauseStates(false);
}

void halt()
{
  saveConfig();
	systemShutdown();
	wzShutdown();
}

int main()
{
  return 1;
}
