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

#  include <errno.h>

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

#include "clparse.h"
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

#if defined(WZ_OS_MAC)
// NOTE: Moving these defines is likely to (and has in the past) break the mac builds
# include <CoreServices/CoreServices.h>
# include <unistd.h>
# include "lib/framework/cocoa_wrapper.h"
#endif // WZ_OS_MAC

/* Always use fallbacks on Windows */
#if !defined(WZ_DATADIR)
#  define WZ_DATADIR "data"
#endif


enum FOCUS_STATE
{
	FOCUS_OUT,		// Window does not have the focus
	FOCUS_IN,		// Window has got the focus
};

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
static GS_GAMEMODE gameStatus = GS_TITLE_SCREEN;
// Status of the gameloop
static GAMECODE gameLoopStatus = GAMECODE_CONTINUE;
static FOCUS_STATE focusState = FOCUS_IN;

// Gets the full path to the folder that contains the application executable (UTF-8)
static std::string getCurrentApplicationFolder()
{
	// Not yet implemented for this platform
	return std::string();
}

// Gets the full path to the prefix of the folder that contains the application executable (UTF-8)
static std::string getCurrentApplicationFolderPrefix()
{
	// Remove the last path component
	std::string appPath = getCurrentApplicationFolder();

	if (appPath.empty())
	{
		return appPath;
	}

	// Remove trailing path separators (slashes)
	while (!appPath.empty() && (appPath.back() == '\\' || appPath.back() == '/'))
	{
		appPath.pop_back();
	}

	// Find the position of the last slash in the application folder
	size_t lastSlash = appPath.find_last_of("\\/", std::string::npos);

	if (lastSlash == std::string::npos)
	{
		// Did not find a path separator - does not appear to be a valid app folder?
		debug(LOG_ERROR, "Unable to find path separator in application executable path");
		return std::string();
	}

	// Trim off the last path component
	return appPath.substr(0, lastSlash);
}

static bool isPortableMode()
{
	static bool _checkedMode = false;
	static bool _isPortableMode = false;

	if (!_checkedMode)
	{
		_checkedMode = true;
	}

	return _isPortableMode;
}

/*!
 * Retrieves the current working directory and copies it into the provided output buffer
 * \param[out] dest the output buffer to put the current working directory in
 * \param size the size (in bytes) of \c dest
 * \return true on success, false if an error occurred (and dest doesn't contain a valid directory)
 */
#if !defined(WZ_PHYSFS_2_1_OR_GREATER)
static bool getCurrentDir(char *const dest, size_t const size)
{
	if (getcwd(dest, size) == nullptr)
	{
		if (errno == ERANGE)
		{
			debug(LOG_ERROR, "The buffer to contain our current directory is too small (%u bytes and more needed)", (unsigned int)size);
		}
		else
		{
			debug(LOG_ERROR, "getcwd failed: %s", strerror(errno));
		}

		return false;
	}

	// If we got here everything went well
	return true;
}
#endif

// Fallback method for earlier PhysFS verions that do not support PHYSFS_getPrefDir
// Importantly, this creates the folders if they do not exist
#if !defined(WZ_PHYSFS_2_1_OR_GREATER)
static std::string getPlatformPrefDir_Fallback(const char *org, const char *app)
{
	WzString basePath;
	WzString appendPath;
	char tmpstr[PATH_MAX] = { '\0' };
	const size_t size = sizeof(tmpstr);
#if defined(WZ_OS_UNIX)
	// Following PhysFS, use XDG's base directory spec, even if not on Linux.
	// Reference: https://standards.freedesktop.org/basedir-spec/basedir-spec-latest.html
	const char *envPath = getenv("XDG_DATA_HOME");

	if (envPath == nullptr)
	{
		// XDG_DATA_HOME isn't defined
		// Use HOME, and append ".local/share/" to match XDG's base directory spec
		envPath = getenv("HOME");

		if (envPath == nullptr)
		{
			// On PhysFS < 2.1, fall-back to using PHYSFS_getUserDir() if HOME isn't defined
			debug(LOG_INFO, "HOME environment variable isn't defined - falling back to PHYSFS_getUserDir()");
			envPath = PHYSFS_getUserDir();
		}

		appendPath = WzString(".local") + PHYSFS_getDirSeparator() + "share";
	}

	if (envPath != nullptr)
	{
		basePath = WzString::fromUtf8(envPath);

		if (!appendPath.isEmpty())
		{
			appendPath += PHYSFS_getDirSeparator();
		}

		appendPath += app;
	}
	else
#else

	// On PhysFS < 2.1, fall-back to using PHYSFS_getUserDir() for other OSes
	if (PHYSFS_getUserDir())
	{
		basePath = WzString::fromUtf8(PHYSFS_getUserDir());
		appendPath = WzString::fromUtf8(app);
	}
	else
#endif
		if (getCurrentDir(tmpstr, size))
		{
			basePath = WzString::fromUtf8(tmpstr);
			appendPath = WzString::fromUtf8(app);
		}
		else
		{
			debug(LOG_FATAL, "Can't get home / prefs directory?");
			abort();
		}

	// Create the folders within the basePath if they don't exist

	if (!PHYSFS_setWriteDir(basePath.toUtf8().c_str())) // Workaround for PhysFS not creating the writedir as expected.
	{
		debug(LOG_FATAL, "Error setting write directory to \"%s\": %s",
		      basePath.toUtf8().c_str(), WZ_PHYSFS_getLastError());
		exit(1);
	}

	WzString currentBasePath = basePath;
	const std::vector<WzString> appendPaths = appendPath.split(PHYSFS_getDirSeparator());

	for (const auto &folder : appendPaths)
	{
		if (!PHYSFS_mkdir(folder.toUtf8().c_str()))
		{
			debug(LOG_FATAL, "Error creating directory \"%s\" in \"%s\": %s",
			      folder.toUtf8().c_str(), PHYSFS_getWriteDir(), WZ_PHYSFS_getLastError());
			exit(1);
		}

		currentBasePath += PHYSFS_getDirSeparator();
		currentBasePath += folder;

		if (!PHYSFS_setWriteDir(currentBasePath.toUtf8().c_str())) // Workaround for PhysFS not creating the writedir as expected.
		{
			debug(LOG_FATAL, "Error setting write directory to \"%s\": %s",
			      currentBasePath.toUtf8().c_str(), WZ_PHYSFS_getLastError());
			exit(1);
		}
	}

	return (basePath + PHYSFS_getDirSeparator() + appendPath + PHYSFS_getDirSeparator()).toUtf8();
}
#endif

// Retrieves the appropriate storage directory for application-created files / prefs
// (Ensures the directory exists. Creates folders if necessary.)
static std::string getPlatformPrefDir(const char * org, const std::string &app)
{
	if (isPortableMode())
	{
		// When isPortableMode is true, the config dir should be stored in the same prefix as the app's bindir.
		// i.e. If the app executable path is:  <prefix>/bin/warzone2100.exe
		//      the config directory should be: <prefix>/<app>/
		std::string prefixPath = getCurrentApplicationFolderPrefix();

		if (prefixPath.empty())
		{
			// Failed to get the current application folder
			debug(LOG_FATAL, "Error getting the current application folder prefix - unable to proceed with portable mode");
			exit(1);
		}

		std::string appendPath = app;

		// Create the folders within the prefixPath if they don't exist
		if (!PHYSFS_setWriteDir(prefixPath.c_str())) // Workaround for PhysFS not creating the writedir as expected.
		{
			debug(LOG_FATAL, "Error setting write directory to \"%s\": %s",
			      prefixPath.c_str(), WZ_PHYSFS_getLastError());
			exit(1);
		}

		if (!PHYSFS_mkdir(appendPath.c_str()))
		{
			debug(LOG_FATAL, "Error creating directory \"%s\" in \"%s\": %s",
			      appendPath.c_str(), PHYSFS_getWriteDir(), WZ_PHYSFS_getLastError());
			exit(1);
		}

		return prefixPath + PHYSFS_getDirSeparator() + appendPath + PHYSFS_getDirSeparator();
	}

#if defined(WZ_PHYSFS_2_1_OR_GREATER)
	const char * prefsDir = PHYSFS_getPrefDir(org, app.c_str());

	if (prefsDir == nullptr)
	{
		debug(LOG_FATAL, "Failed to obtain prefs directory: %s", WZ_PHYSFS_getLastError());
		exit(1);
	}

	return std::string(prefsDir) + PHYSFS_getDirSeparator();
#else
	// PHYSFS_getPrefDir is not available - use fallback method (which requires OS-specific code)
	std::string prefDir = getPlatformPrefDir_Fallback(org, app.c_str());

	if (prefDir.empty())
	{
		debug(LOG_FATAL, "Failed to obtain prefs directory (fallback)");
		exit(1);
	}

	return prefDir;
#endif // defined(WZ_PHYSFS_2_1_OR_GREATER)
}

bool endsWith(std::string const &fullString, std::string const &endString)
{
	if (fullString.length() >= endString.length())
	{
		return (0 == fullString.compare(fullString.length() - endString.length(), endString.length(), endString));
	}
	else
	{
		return false;
	}
}

static void initialize_ConfigDir()
{
	std::string configDir;

	if (strlen(configdir) == 0)
	{
		configDir = getPlatformPrefDir("Warzone 2100 Project", version_getVersionedAppDirFolderName());
	}
	else
	{
		configDir = std::string(configdir);

		// Make sure that we have a directory separator at the end of the string
		if (!endsWith(configDir, PHYSFS_getDirSeparator()))
		{
			configDir += PHYSFS_getDirSeparator();
		}

		debug(LOG_WZ, "Using custom configuration directory: %s", configDir.c_str());
	}

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

static void check_Physfs()
{
	const PHYSFS_ArchiveInfo **i;
	bool zipfound = false;
	PHYSFS_Version compiled;
	PHYSFS_Version linked;

	PHYSFS_VERSION(&compiled);
	PHYSFS_getLinkedVersion(&linked);

	debug(LOG_WZ, "Compiled against PhysFS version: %d.%d.%d",
	      compiled.major, compiled.minor, compiled.patch);
	debug(LOG_WZ, "Linked against PhysFS version: %d.%d.%d",
	      linked.major, linked.minor, linked.patch);

	if (linked.major < 2)
	{
		debug(LOG_FATAL, "At least version 2 of PhysicsFS required!");
		exit(-1);
	}

	if (linked.major == 2 && linked.minor == 0 && linked.patch == 2)
	{
		debug(LOG_ERROR, "You have PhysicsFS 2.0.2, which is buggy. You may experience random errors/crashes due to spuriously missing files.");
		debug(LOG_ERROR, "Please upgrade/downgrade PhysicsFS to a different version, such as 2.0.3 or 2.0.1.");
	}

	for (i = PHYSFS_supportedArchiveTypes(); *i != nullptr; i++)
	{
		debug(LOG_WZ, "[**] Supported archive(s): [%s], which is [%s].", (*i)->extension, (*i)->description);

		if (!strncasecmp("zip", (*i)->extension, 3) && !zipfound)
		{
			zipfound = true;
		}
	}

	if (!zipfound)
	{
		debug(LOG_FATAL, "Your Physfs wasn't compiled with zip support.  Please recompile Physfs with zip support.  Exiting program.");
		exit(-1);
	}
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
#if defined(WZ_OS_MAC)
	// version-independent location for video files
	registerSearchPath("/Library/Application Support/Warzone 2100/", 1);
#endif

	// Commandline supplied datadir
	if (strlen(datadir) != 0)
	{
		registerSearchPath(datadir, 1);
	}

	// User's home dir
	registerSearchPath(PHYSFS_getWriteDir(), 2);
	rebuildSearchPath(mod_multiplay, true);

#if !defined(WZ_OS_MAC)
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

#endif

#ifdef WZ_OS_MAC

	if (!PHYSFS_exists("gamedesc.lev"))
	{
		CFURLRef resourceURL = CFBundleCopyResourcesDirectoryURL(CFBundleGetMainBundle());
		char resourcePath[PATH_MAX];

		if (CFURLGetFileSystemRepresentation(resourceURL, true,
		                                     (UInt8 *) resourcePath,
		                                     PATH_MAX))
		{
			WzString resourceDataPath(resourcePath);
			resourceDataPath += "/data";
			registerSearchPath(resourceDataPath.toUtf8().c_str(), 8);
			rebuildSearchPath(mod_multiplay, true);
		}
		else
		{
			debug(LOG_ERROR, "Could not change to resources directory.");
		}

		if (resourceURL != NULL)
		{
			CFRelease(resourceURL);
		}
	}

#endif

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
 * Preparations before entering the title (mainmenu) loop
 * Would start the timer in an event based mainloop
 */
static void startTitleLoop()
{
	SetGameMode(GS_TITLE_SCREEN);

	initLoadingScreen(true);

	if (!frontendInitialise("wrf/frontend.wrf"))
	{
		debug(LOG_FATAL, "Shutting down after failure");
		exit(EXIT_FAILURE);
	}

	closeLoadingScreen();
}


/*!
 * Shutdown/cleanup after the title (mainmenu) loop
 * Would stop the timer
 */
static void stopTitleLoop()
{
	if (!frontendShutdown())
	{
		debug(LOG_FATAL, "Shutting down after failure");
		exit(EXIT_FAILURE);
	}
}


/*!
 * Preparations before entering the game loop
 * Would start the timer in an event based mainloop
 */
static void startGameLoop()
{
	SetGameMode(GS_NORMAL);

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

	// set a flag for the trigger/event system to indicate initialisation is complete
	gameInitialised = true;

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
static void stopGameLoop()
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

	gameInitialised = false;
}


/*!
 * Load a savegame and start into the game loop
 * Game data should be initialised afterwards, so that startGameLoop is not necessary anymore.
 */
static bool initSaveGameLoad()
{
	// NOTE: always setGameMode correctly before *any* loading routines!
	SetGameMode(GS_NORMAL);
	screen_RestartBackDrop();

	// load up a save game
	if (!loadGameInit(saveGameName))
	{
		// FIXME: we really should throw up a error window, but we can't (easily) so I won't.
		debug(LOG_ERROR, "Trying to load Game %s failed!", saveGameName);
		debug(LOG_POPUP, "Failed to load a save game! It is either corrupted or a unsupported format.\n\nRestarting main menu.");
		// FIXME: If we bomb out on a in game load, then we would crash if we don't do the next two calls
		// Doesn't seem to be a way to tell where we are in game loop to determine if/when we should do the two calls.
		gameLoopStatus = GAMECODE_FASTEXIT;	// clear out all old data
		stopGameLoop();
		startTitleLoop(); // Restart into titleloop
		SetGameMode(GS_TITLE_SCREEN);
		return false;
	}

	screen_StopBackDrop();
	closeLoadingScreen();

	// Trap the cursor if cursor snapping is enabled
	if (war_GetTrapCursor())
	{
		wzGrabMouse();
	}

	if (challengeActive)
	{
		addMissionTimerInterface();
	}

	return true;
}


/*!
 * Run the code inside the gameloop
 */
static void runGameLoop()
{
	gameLoopStatus = gameLoop();

	switch (gameLoopStatus)
	{
		case GAMECODE_CONTINUE:
		case GAMECODE_PLAYVIDEO:
			break;

		case GAMECODE_QUITGAME:
			debug(LOG_MAIN, "GAMECODE_QUITGAME");
			stopGameLoop();
			startTitleLoop(); // Restart into titleloop
			break;

		case GAMECODE_LOADGAME:
			debug(LOG_MAIN, "GAMECODE_LOADGAME");
			stopGameLoop();
			initSaveGameLoad(); // Restart and load a savegame
			break;

		case GAMECODE_NEWLEVEL:
			debug(LOG_MAIN, "GAMECODE_NEWLEVEL");
			stopGameLoop();
			startGameLoop(); // Restart gameloop
			break;

		// Never thrown:
		case GAMECODE_FASTEXIT:
		case GAMECODE_RESTARTGAME:
			break;

		default:
			debug(LOG_ERROR, "Unknown code returned by gameLoop");
			break;
	}
}


/*!
 * Run the code inside the titleloop
 */
static void runTitleLoop()
{
	switch (titleLoop())
	{
		case TITLECODE_CONTINUE:
			break;

		case TITLECODE_QUITGAME:
			debug(LOG_MAIN, "TITLECODE_QUITGAME");
			stopTitleLoop();
			wzQuit();
			break;

		case TITLECODE_SAVEGAMELOAD:
		{
			debug(LOG_MAIN, "TITLECODE_SAVEGAMELOAD");
			initLoadingScreen(true);
			// Restart into gameloop and load a savegame, ONLY on a good savegame load!
			stopTitleLoop();

			if (!initSaveGameLoad())
			{
				// we had a error loading savegame (corrupt?), so go back to title screen?
				stopGameLoop();
				startTitleLoop();
				changeTitleMode(TITLE);
			}

			closeLoadingScreen();
			break;
		}

		case TITLECODE_STARTGAME:
			debug(LOG_MAIN, "TITLECODE_STARTGAME");
			initLoadingScreen(true);
			stopTitleLoop();
			startGameLoop(); // Restart into gameloop
			closeLoadingScreen();
			break;

		case TITLECODE_SHOWINTRO:
			debug(LOG_MAIN, "TITLECODE_SHOWINTRO");
			seq_ClearSeqList();
			seq_AddSeqToList("titles.ogg", nullptr, nullptr, false);
			seq_AddSeqToList("devastation.ogg", nullptr, "devastation.txa", false);
			seq_StartNextFullScreenVideo();
			break;

		default:
			debug(LOG_ERROR, "Unknown code returned by titleLoop");
			break;
	}
}

/*!
 * The mainloop.
 * Fetches events, executes appropriate code
 */
void mainLoop()
{
	frameUpdate(); // General housekeeping

	// Screenshot key is now available globally
	if (keyPressed(KEY_F10))
	{
		kf_ScreenDump();
		inputLoseFocus();		// remove it from input stream
	}

	if (NetPlay.bComms || focusState == FOCUS_IN || !war_GetPauseOnFocusLoss())
	{
		if (loop_GetVideoStatus())
		{
			videoLoop(); // Display the video if necessary
		}
		else switch (GetGameMode())
			{
				case GS_NORMAL: // Run the gameloop code
					runGameLoop();
					break;

				case GS_TITLE_SCREEN: // Run the titleloop code
					runTitleLoop();
					break;

				default:
					break;
			}

		realTimeUpdate(); // Update realTime.
	}
}

// for backend detection
extern const char *BACKEND;

void test(int argc, char *argv[])
{
  fprintf(stderr, "Succesfully working!\n");
  for(int i = 0; i < argc; i++)
  {
    fprintf(stderr, "%s\n", argv[i]);
  }
}

void initPhysFS()
{
  PHYSFS_init("TODO this should not be necessary?");
	PHYSFS_mkdir("challenges");	// custom challenges
	PHYSFS_mkdir("logs");		// netplay, mingw crash reports & WZ logs
}

int main(int argc, char *argv[])
{
  test(argc, argv);
	int utfargc = argc;
	char **utfargv = (char **)argv;
	wzMain(argc, argv);		// init Qt integration first

	debug_register_callback(debug_callback_stderr, nullptr, nullptr, nullptr);

	setupExceptionHandler(utfargc, utfargv, version_getFormattedVersionString(), version_getVersionedAppDirFolderName(), isPortableMode());

	/* Initialize the write/config directory for PhysicsFS.
	 * This needs to be done __after__ the early commandline parsing,
	 * because the user might tell us to use an alternative configuration
	 * directory.
	 */
	initialize_ConfigDir();

	/*** Initialize directory structure ***/

	make_dir(MultiCustomMapsPath, "maps", nullptr); // needed to prevent crashes when getting map

	PHYSFS_mkdir("mods/autoload");	// mods that are automatically loaded
	PHYSFS_mkdir("mods/campaign");	// campaign only mods activated with --mod_ca=example.wz
	PHYSFS_mkdir("mods/downloads");	// mod download directory
	PHYSFS_mkdir("mods/global");	// global mods activated with --mod=example.wz
	PHYSFS_mkdir("mods/multiplay");	// multiplay only mods activated with --mod_mp=example.wz
	PHYSFS_mkdir("mods/music");	// music mods that are automatically loaded

	make_dir(MultiPlayersPath, "multiplay", "players"); // player profiles

	PHYSFS_mkdir("music");	// custom music overriding default music and music mods

	make_dir(SaveGamePath, "savegames", nullptr); 	// save games
	PHYSFS_mkdir("savegames/campaign");		// campaign save games
	PHYSFS_mkdir("savegames/skirmish");		// skirmish save games

	make_dir(ScreenDumpPath, "screenshots", nullptr);	// for screenshots

	PHYSFS_mkdir("tests");			// test games launched with --skirmish=game

	PHYSFS_mkdir("userdata");		// per-mod data user generated data
	PHYSFS_mkdir("userdata/campaign");	// contains campaign templates
	PHYSFS_mkdir("userdata/mp");		// contains multiplayer templates
	memset(rulesettag, 0, sizeof(rulesettag)); // stores tag property of ruleset.json files

	if (!customDebugfile)
	{
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

	// NOTE: it is now safe to use debug() calls to make sure output gets captured.
	check_Physfs();
	debug(LOG_WZ, "Warzone 2100 - %s", version_getFormattedVersionString());
	debug(LOG_WZ, "Using language: %s", getLanguage());
	debug(LOG_WZ, "Backend: %s", BACKEND);
	debug(LOG_MEMORY, "sizeof: SIMPLE_OBJECT=%ld, BASE_OBJECT=%ld, DROID=%ld, STRUCTURE=%ld, FEATURE=%ld, PROJECTILE=%ld",
	      (long)sizeof(SIMPLE_OBJECT), (long)sizeof(BASE_OBJECT), (long)sizeof(DROID), (long)sizeof(STRUCTURE), (long)sizeof(FEATURE), (long)sizeof(PROJECTILE));


	/* Put in the writedir root */
	sstrcpy(KeyMapPath, "keymap.json");

	// initialise all the command line states
	war_SetDefaultStates();

	debug(LOG_MAIN, "initializing");

	loadConfig();

	// parse the command line

	// Save new (commandline) settings
	saveConfig();

	// Find out where to find the data
	scanDataDirs();

	// Now we check the mods to see if they exist or not (specified on the command line)
	// FIXME: I know this is a bit hackish, but better than nothing for now?
	{
		char modtocheck[256];
#if defined WZ_PHYSFS_2_1_OR_GREATER
		PHYSFS_Stat metaData;
#endif

		// check whether given global mods are regular files
		for (auto iterator = global_mods.begin(); iterator != global_mods.end();)
		{
			ssprintf(modtocheck, "mods/global/%s", iterator->c_str());
#if defined WZ_PHYSFS_2_0_OR_GREATER

			if (!PHYSFS_exists(modtocheck) || WZ_PHYSFS_isDirectory(modtocheck))
#elif defined WZ_PHYSFS_2_1_OR_GREATER
			PHYSFS_stat(modtocheck, &metaData);

			if (metaData.filetype != PHYSFS_FILETYPE_REGULAR)
#endif
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
#if defined WZ_PHYSFS_2_0_OR_GREATER

			if (!PHYSFS_exists(modtocheck) || WZ_PHYSFS_isDirectory(modtocheck))
#elif defined WZ_PHYSFS_2_1_OR_GREATER
			PHYSFS_stat(modtocheck, &metaData);

			if (metaData.filetype != PHYSFS_FILETYPE_REGULAR)
#endif
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
#if defined WZ_PHYSFS_2_0_OR_GREATER

			if (!PHYSFS_exists(modtocheck) || WZ_PHYSFS_isDirectory(modtocheck))
#elif defined WZ_PHYSFS_2_1_OR_GREATER
			PHYSFS_stat(modtocheck, &metaData);

			if (metaData.filetype != PHYSFS_FILETYPE_REGULAR)
#endif
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

	if (!wzMainScreenSetup(war_getAntialiasing(), war_getFullscreen(), war_GetVsync()))
	{
		return EXIT_FAILURE;
	}

	debug(LOG_WZ, "Warzone 2100 - %s", version_getFormattedVersionString());
	debug(LOG_WZ, "Using language: %s", getLanguage());
	debug(LOG_WZ, "Backend: %s", BACKEND);
	debug(LOG_MEMORY, "sizeof: SIMPLE_OBJECT=%ld, BASE_OBJECT=%ld, DROID=%ld, STRUCTURE=%ld, FEATURE=%ld, PROJECTILE=%ld",
	      (long)sizeof(SIMPLE_OBJECT), (long)sizeof(BASE_OBJECT), (long)sizeof(DROID), (long)sizeof(STRUCTURE), (long)sizeof(FEATURE), (long)sizeof(PROJECTILE));

	int w = pie_GetVideoBufferWidth();
	int h = pie_GetVideoBufferHeight();

	char buf[256];
	ssprintf(buf, "Video Mode %d x %d (%s)", w, h, war_getFullscreen() ? "fullscreen" : "window");
	addDumpInfo(buf);

	float horizScaleFactor, vertScaleFactor;
	wzGetGameToRendererScaleFactor(&horizScaleFactor, &vertScaleFactor);
	debug(LOG_WZ, "Game to Renderer Scale Factor (w x h): %f x %f", horizScaleFactor, vertScaleFactor);

	debug(LOG_MAIN, "Final initialization");

	if (!frameInitialise())
	{
		return EXIT_FAILURE;
	}

	if (!screenInitialise())
	{
		return EXIT_FAILURE;
	}

	if (!pie_LoadShaders())
	{
		return EXIT_FAILURE;
	}

	unsigned int windowWidth = 0, windowHeight = 0;
	wzGetWindowResolution(nullptr, &windowWidth, &windowHeight);
	war_SetWidth(windowWidth);
	war_SetHeight(windowHeight);

	pie_SetFogStatus(false);
	pie_ScreenFlip(CLEAR_BLACK);

	pal_Init();

	pie_LoadBackDrop(SCREEN_RANDOMBDROP);
	pie_SetFogStatus(false);
	pie_ScreenFlip(CLEAR_BLACK);

	if (!systemInitialise(horizScaleFactor, vertScaleFactor))
	{
		return EXIT_FAILURE;
	}

	//set all the pause states to false
	setAllPauseStates(false);

	// Copy this info to be used by the crash handler for the dump file
	ssprintf(buf, "Using Backend: %s", BACKEND);
	addDumpInfo(buf);
	ssprintf(buf, "Using language: %s", getLanguageName());
	addDumpInfo(buf);

	// Do the game mode specific initialisation.
	switch (GetGameMode())
	{
		case GS_TITLE_SCREEN:
			startTitleLoop();
			break;

		case GS_SAVEGAMELOAD:
			initSaveGameLoad();
			break;

		case GS_NORMAL:
			startGameLoop();
			break;

		default:
			debug(LOG_ERROR, "Weirdy game status, I'm afraid!!");
			break;
	}

#if defined(WZ_CC_MSVC) && defined(DEBUG)
	debug_MEMSTATS();
#endif
	debug(LOG_MAIN, "Entering main loop");
	wzMainEventLoop();
	saveConfig();
	systemShutdown();

	wzShutdown();
	debug(LOG_MAIN, "Completed shutting down Warzone 2100");
	return EXIT_SUCCESS;
}

/*!
 * Get the mode the game is currently in
 */
GS_GAMEMODE GetGameMode()
{
	return gameStatus;
}

/*!
 * Set the current mode
 */
void SetGameMode(GS_GAMEMODE status)
{
	gameStatus = status;
}
