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

#include "lib/framework/wzstring.h"
#include "clparse.h"
#include "main.h"

/// Enable automatic test games
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
