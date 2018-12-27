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

#ifndef __INCLUDED_SRC_MAIN_H__
#define __INCLUDED_SRC_MAIN_H__

#include "lib/framework/frame.h"

enum GS_GAMEMODE
{
	GS_TITLE_SCREEN = 1,
	GS_NORMAL = 2,
	GS_SAVEGAMELOAD = 3
};

//flag to indicate when initialisation is complete
extern bool gameInitialised;
extern bool customDebugfile;

GS_GAMEMODE GetGameMode();
void setGameMode(GS_GAMEMODE status) asm ("setGameMode");
void mainLoop() asm ("WZLoop");
void runTitleLoop() asm ("runTitleLoop");
void startTitleLoop() asm ("startTitleLoop");
void stopTitleLoop() asm ("stopTitleLoop");
void startGameLoop() asm ("startGameLoop");
void stopGameLoop() asm ("stopGameLoop");
void initSaveGameLoad() asm ("initSaveGameLoad");
void halt() asm ("halt");
void init() asm ("init");
void test(int argc, char* argv[]) asm ("test");
void initPhysFS() asm ("initPhysFS");

extern char SaveGamePath[PATH_MAX];
extern char datadir[PATH_MAX];
extern char configdir[PATH_MAX];
extern char KeyMapPath[PATH_MAX];
extern char MultiPlayersPath[PATH_MAX];
extern char rulesettag[40];

void autogame() asm ("autogame");
void setConfigdir(char *arg) asm ("setConfigdir");
void setDatadir(char *arg) asm ("setDatadir");
void setDebug(char *arg) asm ("setDebug");
void setDebugfile(char *arg) asm ("setDebugfile");

bool autogame_enabled();
const std::string &saveandquit_enabled();
const std::string &wz_skirmish_test();

#endif // __INCLUDED_SRC_MAIN_H__
