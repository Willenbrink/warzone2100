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
/*
 * Loop.c
 *
 * The main game loop
 *
 */
#include "lib/framework/frame.h"
#include "lib/framework/input.h"
#include "lib/framework/strres.h"
#include "lib/framework/wzapp.h"
#include "lib/framework/rational.h"

#include "lib/ivis_opengl/pieblitfunc.h"
#include "lib/ivis_opengl/piestate.h" //ivis render code
#include "lib/ivis_opengl/piemode.h"
// FIXME Direct iVis implementation include!
#include "lib/ivis_opengl/screen.h"
#include "baseobject.h"
#include "basedef.h"

#include "lib/gamelib/gtime.h"
#include "lib/script/script.h"
#include "lib/sound/audio.h"
#include "lib/sound/cdaudio.h"
#include "lib/sound/mixer.h"
#include "lib/netplay/netplay.h"

#include "loop.h"
#include "objects.h"
#include "display.h"
#include "map.h"
#include "hci.h"
#include "ingameop.h"
#include "miscimd.h"
#include "effects.h"
#include "radar.h"
#include "projectile.h"
#include "console.h"
#include "power.h"
#include "message.h"
#include "bucket3d.h"
#include "display3d.h"
#include "warzoneconfig.h"

#include "multiplay.h" //ajl
#include "scripttabs.h"
#include "levels.h"
#include "visibility.h"
#include "multimenu.h"
#include "intelmap.h"
#include "loadsave.h"
#include "game.h"
#include "multijoin.h"
#include "lighting.h"
#include "intimage.h"
#include "lib/framework/cursors.h"
#include "seqdisp.h"
#include "mission.h"
#include "warcam.h"
#include "lighting.h"
#include "mapgrid.h"
#include "edit3d.h"
#include "fpath.h"
#include "scriptextern.h"
#include "cmddroid.h"
#include "keybind.h"
#include "wrappers.h"
#include "random.h"
#include "qtscript.h"
#include "version.h"

#include "warzoneconfig.h"

#ifdef DEBUG
#include "objmem.h"
#endif

#include <numeric>


static void fireWaitingCallbacks();

/*
 * Global variables
 */
unsigned int loopPieCount;
unsigned int loopPolyCount;

/*
 * local variables
 */
static bool paused = false;
static bool video = false;

//holds which pause is valid at any one time
struct PAUSE_STATE
{
	bool gameUpdatePause;
	bool audioPause;
	bool scriptPause;
	bool scrollPause;
	bool consolePause;
};

static PAUSE_STATE	pauseState;
static	UDWORD	numDroids[MAX_PLAYERS];
static	UDWORD	numMissionDroids[MAX_PLAYERS];
static	UDWORD	numCommandDroids[MAX_PLAYERS];
static	UDWORD	numConstructorDroids[MAX_PLAYERS];

static SDWORD videoMode = 0;

LOOP_MISSION_STATE		loopMissionState = LMS_NORMAL;

// this is set by scrStartMission to say what type of new level is to be started
LEVEL_TYPE nextMissionType = LDS_NONE;

INT_RETVAL renderNotPaused(CURSOR *cursor)
{
  INT_RETVAL intRetVal = INT_NONE;
  /* Run the in game interface and see if it grabbed any mouse clicks */
  if (!rotActive && getWidgetsStatus() && dragBox3D.status != DRAG_DRAGGING && wallDrag.status != DRAG_DRAGGING)
	{
    intRetVal = intRunWidgets();
    // Send droid orders, if any. (Should do between intRunWidgets() calls, to avoid droid orders getting mixed up, in the case of multiple orders given while the game freezes due to net lag.)
    sendQueuedDroidInfo();
	}

  //don't process the object lists if paused or about to quit to the front end
  if (!gameUpdatePaused() && intRetVal != INT_QUIT)
	{
		if (dragBox3D.status != DRAG_DRAGGING && wallDrag.status != DRAG_DRAGGING
        && (intRetVal == INT_INTERCEPT
        || (radarOnScreen && CoordInRadar(mouseX(), mouseY()) && radarPermitted)))
    {
      // Using software cursors (when on) for these menus due to a bug in SDL's SDL_ShowCursor()
      wzSetCursor(CURSOR_DEFAULT);
    }

#ifdef DEBUG
			// check all flag positions for duplicate delivery points
    checkFactoryFlags();
#endif

    //handles callbacks for positioning of DP's
    process3DBuilding();
    processDeliveryRepos();
    
    //ajl. get the incoming netgame messages and process them.
    // FIXME Previous comment is deprecated. multiPlayerLoop does some other weird stuff, but not that anymore.
    if (bMultiPlayer)
      {
        multiPlayerLoop();
      }
    
    for (unsigned i = 0; i < MAX_PLAYERS; i++)
      {
        for (DROID *psCurr = apsDroidLists[i]; psCurr; psCurr = psCurr->psNext)
          {
            // Don't copy the next pointer - if droids somehow get destroyed in the graphics rendering loop, who cares if we crash.
            calcDroidIllumination(psCurr);
          }
      }
  }
  
  if (!consolePaused())
		{
			/* Process all the console messages */
			updateConsoleMessages();
		}

  if (!scrollPaused() && dragBox3D.status != DRAG_DRAGGING && intMode != INT_INGAMEOP)
		{
			*cursor = scroll();
			zoom();
		}
  return intRetVal;
}

INT_RETVAL renderPaused(CURSOR *cursor)
{
  INT_RETVAL intRetVal = INT_NONE;
  // Using software cursors (when on) for these menus due to a bug in SDL's SDL_ShowCursor()
  wzSetCursor(CURSOR_DEFAULT);

  if (dragBox3D.status != DRAG_DRAGGING)
		{
			*cursor = scroll();
			zoom();
		}

  if (InGameOpUp || isInGamePopupUp)		// ingame options menu up, run it!
		{
			WidgetTriggers const &triggers = widgRunScreen(psWScreen);
			unsigned widgval = triggers.empty() ? 0 : triggers.front().widget->id; // Just use first click here, since the next click could be on another menu.

			intProcessInGameOptions(widgval);

			if (widgval == INTINGAMEOP_QUIT_CONFIRM || widgval == INTINGAMEOP_POPUP_QUIT)
        {
          if (gamePaused())
            {
              kf_TogglePauseMode();
            }

          intRetVal = INT_QUIT;
        }
		}

  if (bLoadSaveUp && runLoadSave(true) && strlen(sRequestResult))
		{
			debug(LOG_NEVER, "Returned %s", sRequestResult);

			if (bRequestLoad)
        {
          loopMissionState = LMS_LOADGAME;
          NET_InitPlayers();			// otherwise alliances were not cleared
          sstrcpy(saveGameName, sRequestResult);
        }
			else
        {
          char msgbuffer[256] = {'\0'};

          if (saveInMissionRes())
            {
              if (saveGame(sRequestResult, GTYPE_SAVE_START))
                {
                  sstrcpy(msgbuffer, _("GAME SAVED: "));
                  sstrcat(msgbuffer, sRequestResult);
                  addConsoleMessage(msgbuffer, LEFT_JUSTIFY, NOTIFY_MESSAGE);
                }
              else
                {
                  ASSERT(false, "Mission Results: saveGame Failed");
                  sstrcpy(msgbuffer, _("Could not save game!"));
                  addConsoleMessage(msgbuffer, LEFT_JUSTIFY, NOTIFY_MESSAGE);
                  deleteSaveGame(sRequestResult);
                }
            }
          else if (bMultiPlayer || saveMidMission())
            {
              if (saveGame(sRequestResult, GTYPE_SAVE_MIDMISSION)) //mid mission from [esc] menu
                {
                  sstrcpy(msgbuffer, _("GAME SAVED: "));
                  sstrcat(msgbuffer, sRequestResult);
                  addConsoleMessage(msgbuffer, LEFT_JUSTIFY, NOTIFY_MESSAGE);
                }
              else
                {
                  ASSERT(!"saveGame(sRequestResult, GTYPE_SAVE_MIDMISSION) failed", "Mid Mission: saveGame Failed");
                  sstrcpy(msgbuffer, _("Could not save game!"));
                  addConsoleMessage(msgbuffer, LEFT_JUSTIFY, NOTIFY_MESSAGE);
                  deleteSaveGame(sRequestResult);
                }
            }
          else
            {
              ASSERT(false, "Attempt to save game with incorrect load/save mode");
            }
        }
		}
  return intRetVal;
}

GAMECODE renderLoop()
{
	if (bMultiPlayer && !NetPlay.isHostAlive && NetPlay.bComms && !NetPlay.isHost)
	{
		intAddInGamePopup();
	}

	audio_Update();

	wzShowMouse(true);

	CURSOR cursor = CURSOR_DEFAULT;

	INT_RETVAL intRetVal = INT_NONE;
	if (!paused)
    intRetVal = renderNotPaused(&cursor);
	else
    intRetVal = renderPaused(&cursor);

	/* Check for quit */
	bool quitting = intRetVal == INT_QUIT && !loop_GetVideoStatus();

	if (quitting)
	{
		//quitting from the game to the front end
		//so get a new backdrop
		pie_LoadBackDrop(SCREEN_RANDOMBDROP);
	}

	if (!quitting)
	{
		if (!loop_GetVideoStatus())
		{
			if (!gameUpdatePaused())
			{
				if (dragBox3D.status != DRAG_DRAGGING
				        && wallDrag.status != DRAG_DRAGGING
				        && intRetVal != INT_INTERCEPT)
				{
					ProcessRadarInput();
				}

				processInput();

				//no key clicks or in Intelligence Screen
				if (!isMouseOverRadar() && intRetVal == INT_NONE && !InGameOpUp && !isInGamePopupUp)
				{
					CURSOR cursor2 = processMouseClickInput();
					cursor = cursor2 == CURSOR_DEFAULT ? cursor : cursor2;
				}

				displayWorld();
			}

			wzPerfBegin(PERF_GUI, "User interface");
			/* Display the in game interface */
			pie_SetDepthBufferStatus(DEPTH_CMP_ALWAYS_WRT_ON);
			pie_SetFogStatus(false);

			if (bMultiPlayer && bDisplayMultiJoiningStatus)
			{
				intDisplayMultiJoiningStatus(bDisplayMultiJoiningStatus);
				setWidgetsStatus(false);
			}

			if (getWidgetsStatus())
			{
				intDisplayWidgets();
			}

			pie_SetDepthBufferStatus(DEPTH_CMP_LEQ_WRT_ON);
			pie_SetFogStatus(true);
			wzPerfEnd(PERF_GUI);
		}

		wzSetCursor(cursor);

		pie_GetResetCounts(&loopPieCount, &loopPolyCount);		/* Check for toggling display mode */		if ((keyDown(KEY_LALT) || keyDown(KEY_RALT)) && keyPressed(KEY_RETURN))
		{
			war_setFullscreen(!war_getFullscreen());
			wzToggleFullscreen();
		}
	}

	// deal with the mission state
	switch (loopMissionState)
	{
		case LMS_CLEAROBJECTS:
			missionDestroyObjects();
			setScriptPause(true);
			loopMissionState = LMS_SETUPMISSION;
			break;

		case LMS_NORMAL:
			// default
			break;

		case LMS_SETUPMISSION:
			setScriptPause(false);

			if (!setUpMission(nextMissionType))
			{
				return GAMECODE_QUITGAME;
			}

			break;

		case LMS_SAVECONTINUE:
			// just wait for this to be changed when the new mission starts
			break;

		case LMS_NEWLEVEL:
			//nextMissionType = MISSION_NONE;
			nextMissionType = LDS_NONE;
			return GAMECODE_NEWLEVEL;
			break;

		case LMS_LOADGAME:
			return GAMECODE_LOADGAME;
			break;

		default:
			ASSERT(false, "unknown loopMissionState");
			break;
	}

	int clearMode = 0;

	if (getDrawShadows())
	{
		clearMode |= CLEAR_SHADOW;
	}

	if (quitting || loopMissionState == LMS_SAVECONTINUE)
	{
		pie_SetFogStatus(false);
		clearMode = CLEAR_BLACK;
	}

	pie_ScreenFlip(clearMode);//gameloopflip

	if (quitting)
	{
		/* Check for toggling display mode */
		if ((keyDown(KEY_LALT) || keyDown(KEY_RALT)) && keyPressed(KEY_RETURN))
		{
			war_setFullscreen(!war_getFullscreen());
			wzToggleFullscreen();
		}

		return GAMECODE_QUITGAME;
	}
	else if (loop_GetVideoStatus())
	{
		audio_StopAll();
		return GAMECODE_PLAYVIDEO;
	}

	return GAMECODE_CONTINUE;
}

int getBuildingType(STRUCTURE *ptr)
{
  return ptr->pStructureType->type;
}

int getBuildingId(STRUCTURE *ptr)
{
  return ptr->id;
}

void gameStatePreUpdate()
{
  syncDebug("map = \"%s\", pseudorandom 32-bit integer = 0x%08X, allocated = %d %d %d %d %d %d %d %d %d %d, position = %d %d %d %d %d %d %d %d %d %d", game.map, gameRandU32(), NetPlay.players[0].allocated, NetPlay.players[1].allocated, NetPlay.players[2].allocated, NetPlay.players[3].allocated, NetPlay.players[4].allocated, NetPlay.players[5].allocated, NetPlay.players[6].allocated, NetPlay.players[7].allocated, NetPlay.players[8].allocated, NetPlay.players[9].allocated, NetPlay.players[0].position, NetPlay.players[1].position, NetPlay.players[2].position, NetPlay.players[3].position, NetPlay.players[4].position, NetPlay.players[5].position, NetPlay.players[6].position, NetPlay.players[7].position, NetPlay.players[8].position, NetPlay.players[9].position);

	for (unsigned n = 0; n < MAX_PLAYERS; ++n)
    {
      syncDebug("Player %d = \"%s\"", n, NetPlay.players[n].name);
    }

	// Add version string to desynch logs. Different version strings will not trigger a desynch dump per se, due to the syncDebug{Get, Set}Crc guard.
	auto crc = syncDebugGetCrc();
	syncDebug("My client version = %s", version_getVersionString());
	syncDebugSetCrc(crc);

	// Actually send pending droid orders.
	sendQueuedDroidInfo();

	sendPlayerGameTime();

	NETflush();  // Make sure the game time tick message is really sent over the network.

	if (!paused && !scriptPaused()) {
    /* Update the event system */
    if (!bInTutorial) {
      eventProcessTriggers(gameTime / SCR_TICKRATE);
    }
    else {
      eventProcessTriggers(realTime / SCR_TICKRATE);
    }
    updateScripts();
  }

	// Update the visibility change stuff
	visUpdateLevel();
  // Put all droids/structures/features into the grid.
	gridReset();

	// Check which objects are visible.
	processVisibility();

	// Update the map.
	mapUpdate();

	// update the command droids
	cmdDroidUpdate();

	fireWaitingCallbacks(); //Now is the good time to fire waiting callbacks (since interpreter is off now)
}

void gameStatePostUpdate()
{
	missionTimerUpdate();

	proj_UpdateAll();

	FEATURE *psNFeat;

	for (FEATURE *psCFeat = apsFeatureLists[0]; psCFeat; psCFeat = psNFeat)
    {
      psNFeat = psCFeat->psNext;
      featureUpdate(psCFeat);
    }

	// Clean up dead droid pointers in UI.
	hciUpdate();

	// Free dead droid memory.
	objmemUpdate();

	// Must end update, since we may or may not have ticked, and some message queue processing code may vary depending on whether it's in an update.
	gameTimeUpdateEnd();

	static int i = 0;

	if (i++ % 10 == 0) // trigger every second
    {
      jsDebugUpdate();
    }

}

/* The video playback loop */
void videoLoop()
{
	bool videoFinished;

	ASSERT(videoMode == 1, "videoMode out of sync");

	// display a frame of the FMV
	videoFinished = !seq_UpdateFullScreenVideo(nullptr);
	pie_ScreenFlip(CLEAR_BLACK);

	// should we stop playing?
	if (videoFinished || keyPressed(KEY_ESC) || mouseReleased(MOUSE_LMB))
	{
		seq_StopFullScreenVideo();

		//set the next video off - if any
		if (videoFinished && seq_AnySeqLeft())
		{
			seq_StartNextFullScreenVideo();
		}
		else
		{
			// remove the intelligence screen if necessary
			if (messageIsImmediate())
			{
				intResetScreen(true);
				setMessageImmediate(false);
			}

			//don't do the callback if we're playing the win/lose video
			if (!getScriptWinLoseVideo())
			{
				eventFireCallbackTrigger((TRIGGER_TYPE)CALL_VIDEO_QUIT);
			}
			else if (!bMultiPlayer)
			{
				displayGameOver(getScriptWinLoseVideo() == PLAY_WIN, false);
			}

			triggerEvent(TRIGGER_VIDEO_QUIT);
		}
	}
}


void loop_SetVideoPlaybackMode()
{
	videoMode += 1;
	paused = true;
	video = true;
	gameTimeStop();
	pie_SetFogStatus(false);
	audio_StopAll();
	wzShowMouse(false);
	screen_StopBackDrop();
	pie_ScreenFlip(CLEAR_BLACK);
}


void loop_ClearVideoPlaybackMode()
{
	videoMode -= 1;
	paused = false;
	video = false;
	gameTimeStart();
	pie_SetFogStatus(true);
	cdAudio_Resume();
	wzShowMouse(true);
	ASSERT(videoMode == 0, "loop_ClearVideoPlaybackMode: out of sync.");
}


SDWORD loop_GetVideoMode()
{
	return videoMode;
}

bool loop_GetVideoStatus()
{
	return video;
}

bool gamePaused()
{
	return paused;
}

void setGamePauseStatus(bool val)
{
	paused = val;
}

bool gameUpdatePaused()
{
	return pauseState.gameUpdatePause;
}
bool audioPaused()
{
	return pauseState.audioPause;
}
bool scriptPaused()
{
	return pauseState.scriptPause;
}
bool scrollPaused()
{
	return pauseState.scrollPause;
}
bool consolePaused()
{
	return pauseState.consolePause;
}

void setGameUpdatePause(bool state)
{
	pauseState.gameUpdatePause = state;
}
void setAudioPause(bool state)
{
	pauseState.audioPause = state;
}
void setScriptPause(bool state)
{
	pauseState.scriptPause = state;
}
void setScrollPause(bool state)
{
	pauseState.scrollPause = state;
}
void setConsolePause(bool state)
{
	pauseState.consolePause = state;
}

//set all the pause states to the state value
void setAllPauseStates(bool state)
{
	setGameUpdatePause(state);
	setAudioPause(state);
	setScriptPause(state);
	setScrollPause(state);
	setConsolePause(state);
}

UDWORD	getNumDroids(UDWORD player)
{
	return (numDroids[player]);
}

UDWORD	getNumMissionDroids(UDWORD player)
{
	return (numMissionDroids[player]);
}

UDWORD	getNumCommandDroids(UDWORD player)
{
	return numCommandDroids[player];
}

UDWORD	getNumConstructorDroids(UDWORD player)
{
	return numConstructorDroids[player];
}

void	setNumDroids(UDWORD player , int newVal)
{
  numDroids[player] = newVal;
}

void	setNumMissionDroids(UDWORD player , int newVal)
{
  numMissionDroids[player] = newVal;
}

void	setNumCommandDroids(UDWORD player , int newVal)
{
  numCommandDroids[player] = newVal;
}

void	setNumConstructorDroids(UDWORD player , int newVal)
{
  numConstructorDroids[player] = newVal;
}

// increase the droid counts - used by update factory to keep the counts in sync
void incNumDroids(UDWORD player)
{
	numDroids[player] += 1;
}
void incNumCommandDroids(UDWORD player)
{
	numCommandDroids[player] += 1;
}
void incNumConstructorDroids(UDWORD player)
{
	numConstructorDroids[player] += 1;
}

/* Fire waiting beacon messages which we couldn't run before */
static void fireWaitingCallbacks()
{
	bool bOK = true;

	while (!isMsgStackEmpty() && bOK)
	{
		bOK = msgStackFireTop();

		if (!bOK)
		{
			ASSERT(false, "fireWaitingCallbacks: msgStackFireTop() failed (stack count: %d)", msgStackGetCount());
		}
	}
}

bool isLasSat(STRUCTURE *psCurr)
{
  return asWeaponStats[psCurr->asWeaps[0].nStat].weaponSubClass == WSC_LAS_SAT;
}
