// VisualBoyAdvance - Nintendo Gameboy/GameboyAdvance (TM) emulator.
// Copyright (C) 1999-2003 Forgotten
// Copyright (C) 2005-2006 Forgotten and the VBA development team

// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2, or(at your option)
// any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software Foundation,
// Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <cmath>

#include <time.h>

#include "../AutoBuild.h"
#include "../version.h"

#include <SDL/SDL.h>

#include "../common/Patch.h"
#include "../common/ConfigManager.h"
#include "../gba/GBA.h"
#include "../gba/agbprint.h"
#include "../gba/Flash.h"
#include "../gba/RTC.h"
#include "../gba/Sound.h"

#include "../Util.h"
#include "text.h"
#include "inputSDL.h"
#include "../common/SoundSDL.h"

# include <unistd.h>
# define GETCWD getcwd

#ifndef __GNUC__
# define HAVE_DECL_GETOPT 0
# define __STDC__ 1
# include "getopt.h"
#else // ! __GNUC__
# define HAVE_DECL_GETOPT 1
# include <getopt.h>
#endif // ! __GNUC__


struct EmulatedSystem emulator = {
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  false,
  0
};

SDL_Surface *surface = NULL;

int systemSpeed = 0;
int systemRedShift = 5;
int systemBlueShift = 6;
int systemGreenShift = 5;
/* 0 is default */
int systemColorDepth = 16;
int systemVerbose = 0;
int systemFrameSkip = 0;
int systemSaveUpdateCounter = SYSTEM_SAVE_NOT_UPDATED;

int srcPitch = 0;
int destWidth = 0;
int destHeight = 0;
int desktopWidth = 0;
int desktopHeight = 0;

int filter_enlarge = 1;

int emulating = 0;
int RGB_LOW_BITS_MASK=0x821;
uint32_t systemColorMap32[0x10000];
uint16_t systemColorMap16[0x10000];
uint16_t systemGbPalette[24];

char filename[2048];

static int sdlSaveKeysSwitch = 0;
// if 0, then SHIFT+F# saves, F# loads (old VBA, ...)
// if 1, then SHIFT+F# loads, F# saves (linux snes9x, ...)
// if 2, then F5 decreases slot number, F6 increases, F7 saves, F8 loads

static int saveSlotPosition = 0; // default is the slot from normal F1
// internal slot number for undoing the last load
#define SLOT_POS_LOAD_BACKUP 8
// internal slot number for undoing the last save
#define SLOT_POS_SAVE_BACKUP 9

static int sdlOpenglScale = 1;
// will scale window on init by this much
static int sdlSoundToggledOff = 0;

extern int autoFireMaxCount;

#define REWIND_NUM 8
#define REWIND_SIZE 400000

enum VIDEO_SIZE{
	VIDEO_1X, VIDEO_2X, VIDEO_3X, VIDEO_4X, VIDEO_5X, VIDEO_6X,
	VIDEO_320x240, VIDEO_640x480, VIDEO_800x600, VIDEO_1024x768, VIDEO_1280x1024,
	VIDEO_OTHER
};

#define _stricmp strcasecmp

uint32_t throttleLastTime = 0;

bool pauseNextFrame = false;
int sdlMirroringEnable = 1;

static int ignore_first_resize_event = 0;

/* forward */
void systemConsoleMessage(const char*);

char* home;

char screenMessageBuffer[21];
uint32_t  screenMessageTime = 0;

#define SOUND_MAX_VOLUME 2.0
#define SOUND_ECHO       0.2
#define SOUND_STEREO     0.15

static void Create_Config_Folder()
{
	extern char *saveDir;
	extern char *batteryDir;
	
	homeDir = malloc(2048);
	batteryDir = malloc(2048);
	saveDir = malloc(2048);
	
	sprintf(homeDir, "%s/.vbam", getenv("HOME"));
	sprintf(batteryDir, "%s/saves", homeDir);
	sprintf(saveDir, "%s/states", homeDir);
	
	mkdir(homeDir, 0755);
	mkdir(batteryDir, 0755);
	mkdir(saveDir, 0755);
}

static void Free_config()
{
	if (!homeDir) free(homeDir);
	if (!batteryDir) free(batteryDir);
	if (!saveDir) free(saveDir);
}


static void sdlChangeVolume(float d)
{
	float oldVolume = soundGetVolume();
	float newVolume = oldVolume + d;

	if (newVolume < 0.0) newVolume = 0.0;
	if (newVolume > SOUND_MAX_VOLUME) newVolume = SOUND_MAX_VOLUME;

	if (fabs(newVolume - oldVolume) > 0.001) {
		char tmp[32];
		sprintf(tmp, "Volume: %i%%", (int)(newVolume*100.0+0.5));
		systemScreenMessage(tmp);
		soundSetVolume(newVolume);
	}
}

char *sdlGetFilename(char *name)
{
  static char filebuffer[2048];

  int len = strlen(name);

  char *p = name + len - 1;

  while(true) {
    if(*p == '/' ||
       *p == '\\') {
      p++;
      break;
    }
    len--;
    p--;
    if(len == 0)
      break;
  }

  if(len == 0)
    strcpy(filebuffer, name);
  else
    strcpy(filebuffer, p);
  return filebuffer;
}


FILE *sdlFindFile(const char *name)
{
	char buffer[4096];
	char path[2048];

	#define PATH_SEP ":"
	#define FILE_SEP '/'
	#define EXE_NAME "vbam"

  fprintf(stdout, "Searching for file %s\n", name);

  if(GETCWD(buffer, 2048)) {
    fprintf(stdout, "Searching current directory: %s\n", buffer);
  }

  FILE *f = fopen(name, "r");
  if(f != NULL) {
    return f;
  }

  if(homeDir) {
    fprintf(stdout, "Searching home directory: %s%c%s\n", homeDir, FILE_SEP, DOT_DIR);
    sprintf(path, "%s%c%s%c%s", homeDir, FILE_SEP, DOT_DIR, FILE_SEP, name);
    f = fopen(path, "r");
    if(f != NULL)
      return f;
  }

  fprintf(stdout, "Searching data directory: %s\n", PKGDATADIR);
  sprintf(path, "%s%c%s", PKGDATADIR, FILE_SEP, name);
  f = fopen(path, "r");
  if(f != NULL)
    return f;

  /*fprintf(stdout, "Searching system config directory: %s\n", SYSCONFDIR);
  sprintf(path, "%s%c%s", SYSCONFDIR, FILE_SEP, name);
  f = fopen(path, "r");
  if(f != NULL)
    return f;*/

  return NULL;
}

static void sdlApplyPerImagePreferences()
{
  FILE *f = sdlFindFile("vba-over.ini");
  if(!f) {
    fprintf(stdout, "vba-over.ini NOT FOUND (using emulator settings)\n");
    return;
  } else
    fprintf(stdout, "Reading vba-over.ini\n");

  char buffer[7];
  buffer[0] = '[';
  buffer[1] = rom[0xac];
  buffer[2] = rom[0xad];
  buffer[3] = rom[0xae];
  buffer[4] = rom[0xaf];
  buffer[5] = ']';
  buffer[6] = 0;

  char readBuffer[2048];

  bool found = false;

  while(1) {
    char *s = fgets(readBuffer, 2048, f);

    if(s == NULL)
      break;

    char *p  = strchr(s, ';');

    if(p)
      *p = 0;

    char *token = strtok(s, " \t\n\r=");

    if(!token)
      continue;
    if(strlen(token) == 0)
      continue;

    if(!strcmp(token, buffer)) {
      found = true;
      break;
    }
  }

  if(found) {
    while(1) {
      char *s = fgets(readBuffer, 2048, f);

      if(s == NULL)
        break;

      char *p = strchr(s, ';');
      if(p)
        *p = 0;

      char *token = strtok(s, " \t\n\r=");
      if(!token)
        continue;
      if(strlen(token) == 0)
        continue;

      if(token[0] == '[') // starting another image settings
        break;
      char *value = strtok(NULL, "\t\n\r=");
      if(value == NULL)
        continue;

      if(!strcmp(token, "rtcEnabled"))
        rtcEnable(atoi(value) == 0 ? false : true);
      else if(!strcmp(token, "flashSize")) {
        int size = atoi(value);
        if(size == 0x10000 || size == 0x20000)
          flashSetSize(size);
      } else if(!strcmp(token, "saveType")) {
        int save = atoi(value);
        if(save >= 0 && save <= 5)
          cpuSaveType = save;
      } else if(!strcmp(token, "mirroringEnabled")) {
        mirroringEnable = (atoi(value) == 0 ? false : true);
      }
    }
  }
  fclose(f);
}

static int sdlCalculateShift(uint32_t mask)
{
  int m = 0;

  while(mask) {
    m++;
    mask >>= 1;
  }

  return m-5;
}

/* returns filename of savestate num, in static buffer (not reentrant, no need to free,
 * but value won't survive much - so if you want to remember it, dup it)
 * You may use the buffer for something else though - until you call sdlStateName again
 */
static char * sdlStateName(int num)
{
  static char stateName[2048];

  if(saveDir)
    sprintf(stateName, "%s/%s%d.sgm", saveDir, sdlGetFilename(filename),
            num+1);
  else if (homeDir)
    sprintf(stateName, "%s/%s/%s%d.sgm", homeDir, DOT_DIR, sdlGetFilename(filename), num + 1);
  else
    sprintf(stateName,"%s%d.sgm", filename, num+1);

  return stateName;
}

void sdlWriteState(int num)
{
  char * stateName;

  stateName = sdlStateName(num);

  if(emulator.emuWriteState)
    emulator.emuWriteState(stateName);

  // now we reuse the stateName buffer - 2048 bytes fit in a lot
  if (num == SLOT_POS_LOAD_BACKUP)
  {
    sprintf(stateName, "Current state backed up to %d", num+1);
    systemScreenMessage(stateName);
  }
  else if (num>=0)
  {
    sprintf(stateName, "Wrote state %d", num+1);
    systemScreenMessage(stateName);
  }

  systemDrawScreen();
}

void sdlReadState(int num)
{
  char * stateName;

  stateName = sdlStateName(num);
  if(emulator.emuReadState)
    emulator.emuReadState(stateName);

  if (num == SLOT_POS_LOAD_BACKUP)
  {
	  sprintf(stateName, "Last load UNDONE");
  } else
  if (num == SLOT_POS_SAVE_BACKUP)
  {
	  sprintf(stateName, "Last save UNDONE");
  }
  else
  {
	  sprintf(stateName, "Loaded state %d", num+1);
  }
  systemScreenMessage(stateName);

  systemDrawScreen();
}

/*
 * perform savestate exchange
 * - put the savestate in slot "to" to slot "backup" (unless backup == to)
 * - put the savestate in slot "from" to slot "to" (unless from == to)
 */
void sdlWriteBackupStateExchange(int from, int to, int backup)
{
  char * dmp;
  char * stateNameOrig	= NULL;
  char * stateNameDest	= NULL;
  char * stateNameBack	= NULL;

  dmp		= sdlStateName(from);
  stateNameOrig = (char*)realloc(stateNameOrig, strlen(dmp) + 1);
  strcpy(stateNameOrig, dmp);
  dmp		= sdlStateName(to);
  stateNameDest = (char*)realloc(stateNameDest, strlen(dmp) + 1);
  strcpy(stateNameDest, dmp);
  dmp		= sdlStateName(backup);
  stateNameBack = (char*)realloc(stateNameBack, strlen(dmp) + 1);
  strcpy(stateNameBack, dmp);

  /* on POSIX, rename would not do anything anyway for identical names, but let's check it ourselves anyway */
  if (to != backup) {
	  if (-1 == rename(stateNameDest, stateNameBack)) {
		fprintf(stdout, "savestate backup: can't backup old state %s to %s", stateNameDest, stateNameBack );
		perror(": ");
	  }
  }
  if (to != from) {
	  if (-1 == rename(stateNameOrig, stateNameDest)) {
		fprintf(stdout, "savestate backup: can't move new state %s to %s", stateNameOrig, stateNameDest );
		perror(": ");
	  }
  }

  systemConsoleMessage("Savestate store and backup committed"); // with timestamp and newline
  fprintf(stdout, "to slot %d, backup in %d, using temporary slot %d\n", to+1, backup+1, from+1);

  free(stateNameOrig);
  free(stateNameDest);
  free(stateNameBack);
}

void sdlWriteBattery()
{
  char buffer[1048];

  if(batteryDir)
    sprintf(buffer, "%s/%s.sav", batteryDir, sdlGetFilename(filename));
  else if (homeDir)
    sprintf(buffer, "%s/%s/%s.sav", homeDir, DOT_DIR, sdlGetFilename(filename));
  else
    sprintf(buffer, "%s.sav", filename);

  emulator.emuWriteBattery(buffer);

  systemScreenMessage("Wrote battery");
}

void sdlReadBattery()
{
  char buffer[1048];

  if(batteryDir)
    sprintf(buffer, "%s/%s.sav", batteryDir, sdlGetFilename(filename));
  else if (homeDir)
    sprintf(buffer, "%s/%s/%s.sav", homeDir, DOT_DIR, sdlGetFilename(filename));
  else
    sprintf(buffer, "%s.sav", filename);

  bool res = false;

  res = emulator.emuReadBattery(buffer);

  if(res)
    systemScreenMessage("Loaded battery");
}

void sdlReadDesktopVideoMode() {
  const SDL_VideoInfo* vInfo = SDL_GetVideoInfo();
  desktopWidth = vInfo->current_w;
  desktopHeight = vInfo->current_h;
}

SDL_Surface* real_video;

void sdlInitVideo() {
	int screenWidth;
	int screenHeight;
	uint32_t rmask, gmask, bmask;
	
	destWidth = sizeX;
	destHeight = sizeY;

	screenWidth = destWidth;
    screenHeight = destHeight;

	real_video = SDL_SetVideoMode(0, 0, 16, SDL_HWSURFACE);
	
	SDL_FillRect(real_video, NULL, 0);
	SDL_Flip(real_video);
	
	if (real_video == NULL) 
	{
		systemMessage(0, "Failed to set video mode");
		SDL_Quit();
		exit(-1);
	}
	
	rmask = real_video->format->Rmask;
	gmask = real_video->format->Gmask;
	bmask = real_video->format->Bmask;
	systemRedShift = sdlCalculateShift(rmask);
	systemGreenShift = sdlCalculateShift(gmask);
	systemBlueShift = sdlCalculateShift(bmask);

	systemColorDepth = real_video->format->BitsPerPixel;

	if(systemColorDepth == 16) {
	srcPitch = sizeX*2 + 4;
	} else {
		if(systemColorDepth == 32)
			srcPitch = sizeX*4 + 4;
		else
		srcPitch = sizeX*3;
	}
}

#define MOD_KEYS    (KMOD_CTRL|KMOD_SHIFT|KMOD_ALT|KMOD_META)
#define MOD_NOCTRL  (KMOD_SHIFT|KMOD_ALT|KMOD_META)
#define MOD_NOALT   (KMOD_CTRL|KMOD_SHIFT|KMOD_META)
#define MOD_NOSHIFT (KMOD_CTRL|KMOD_ALT|KMOD_META)

/*
 * handle the F* keys (for savestates)
 * given the slot number and state of the SHIFT modifier, save or restore
 * (in savemode 3, saveslot is stored in saveSlotPosition and num means:
 *  4 .. F5: decrease slot number (down to 0)
 *  5 .. F6: increase slot number (up to 7, because 8 and 9 are reserved for backups)
 *  6 .. F7: save state
 *  7 .. F8: load state
 *  (these *should* be configurable)
 *  other keys are ignored
 * )
 */
static void sdlHandleSavestateKey(int num, int shifted)
{
	int action	= -1;
	// 0: load
	// 1: save
	int backuping	= 1; // controls whether we are doing savestate backups

	if ( sdlSaveKeysSwitch == 2 )
	{
		// ignore "shifted"
		switch (num)
		{
			// nb.: saveSlotPosition is base 0, but to the user, we show base 1 indexes (F## numbers)!
			case 4:
				if (saveSlotPosition > 0)
				{
					saveSlotPosition--;
					fprintf(stdout, "Changed savestate slot to %d.\n", saveSlotPosition + 1);
				} else
					fprintf(stderr, "Can't decrease slotnumber below 1.\n");
				return; // handled
			case 5:
				if (saveSlotPosition < 7)
				{
					saveSlotPosition++;
					fprintf(stdout, "Changed savestate slot to %d.\n", saveSlotPosition + 1);
				} else
					fprintf(stderr, "Can't increase slotnumber above 8.\n");
				return; // handled
			case 6:
				action	= 1; // save
				break;
			case 7:
				action	= 0; // load
				break;
			default:
				// explicitly ignore
				return; // handled
		}
	}

	if (sdlSaveKeysSwitch == 0 ) /* "classic" VBA: shifted is save */
	{
		if (shifted)
			action	= 1; // save
		else	action	= 0; // load
		saveSlotPosition	= num;
	}
	if (sdlSaveKeysSwitch == 1 ) /* "xKiv" VBA: shifted is load */
	{
		if (!shifted)
			action	= 1; // save
		else	action	= 0; // load
		saveSlotPosition	= num;
	}

	if (action < 0 || action > 1)
	{
		fprintf(
				stderr,
				"sdlHandleSavestateKey(%d,%d), mode %d: unexpected action %d.\n",
				num,
				shifted,
				sdlSaveKeysSwitch,
				action
		);
	}

	if (action)
	{        /* save */
		if (backuping)
		{
			sdlWriteState(-1); // save to a special slot
			sdlWriteBackupStateExchange(-1, saveSlotPosition, SLOT_POS_SAVE_BACKUP); // F10
		} else {
			sdlWriteState(saveSlotPosition);
		}
	} else { /* load */
		if (backuping)
		{
			/* first back up where we are now */
			sdlWriteState(SLOT_POS_LOAD_BACKUP); // F9
		}
		sdlReadState(saveSlotPosition);
        }

} // sdlHandleSavestateKey

void sdlPollEvents()
{
  SDL_Event event;
  while(SDL_PollEvent(&event)) {
    switch(event.type) {
    case SDL_QUIT:
      emulating = 0;
      break;
    case SDL_JOYHATMOTION:
    case SDL_JOYBUTTONDOWN:
    case SDL_JOYBUTTONUP:
    case SDL_JOYAXISMOTION:
    case SDL_KEYDOWN:
      inputProcessSDLEvent(event);
      break;
    case SDL_KEYUP:
      switch(event.key.keysym.sym) {
      case SDLK_r:
        if(!(event.key.keysym.mod & MOD_NOCTRL) &&
           (event.key.keysym.mod & KMOD_CTRL)) {
          if(emulating) {
            emulator.emuReset();

            systemScreenMessage("Reset");
          }
        }
        break;
      case SDLK_s:
        if(!(event.key.keysym.mod & MOD_NOCTRL) &&
           (event.key.keysym.mod & KMOD_CTRL)
	) {
		if (sdlSoundToggledOff) { // was off
			// restore saved state
			soundSetEnable( sdlSoundToggledOff );
			sdlSoundToggledOff = 0;
			systemConsoleMessage("Sound toggled on");
		} else { // was on
			sdlSoundToggledOff = soundGetEnable();
			soundSetEnable( 0 );
			systemConsoleMessage("Sound toggled off");
			if (!sdlSoundToggledOff) {
				sdlSoundToggledOff = 0x3ff;
			}
		}
	}
	break;
      case SDLK_KP_DIVIDE:
        sdlChangeVolume(-0.1);
        break;
      case SDLK_KP_MULTIPLY:
        sdlChangeVolume(0.1);
        break;
      case SDLK_p:
        if(!(event.key.keysym.mod & MOD_NOCTRL) &&
           (event.key.keysym.mod & KMOD_CTRL)) {
          paused = !paused;
          SDL_PauseAudio(paused);
          if(paused)
            wasPaused = true;
	  systemConsoleMessage(paused?"Pause on":"Pause off");
        }
        break;
      case SDLK_3:
	  case SDLK_END:
        emulating = 0;
        break;
      case SDLK_f:
        if(!(event.key.keysym.mod & MOD_NOCTRL) &&
           (event.key.keysym.mod & KMOD_CTRL)) {
          fullScreen = !fullScreen;
          sdlInitVideo();
        }
        break;
      case SDLK_F11:
          if(armState) {
            armNextPC -= 4;
            reg[15].I -= 4;
          } else {
            armNextPC -= 2;
            reg[15].I -= 2;
          }
        break;
      case SDLK_F1:
      case SDLK_F2:
      case SDLK_F3:
      case SDLK_F4:
      case SDLK_F5:
      case SDLK_F6:
      case SDLK_F7:
      case SDLK_F8:
        if(!(event.key.keysym.mod & MOD_NOSHIFT) &&
           (event.key.keysym.mod & KMOD_SHIFT)) {
		sdlHandleSavestateKey( event.key.keysym.sym - SDLK_F1, 1); // with SHIFT
        } else if(!(event.key.keysym.mod & MOD_KEYS)) {
		sdlHandleSavestateKey( event.key.keysym.sym - SDLK_F1, 0); // without SHIFT
	}
        break;
      /* backups - only load */
      case SDLK_F9:
        /* F9 is "load backup" - saved state from *just before* the last restore */
        if ( ! (event.key.keysym.mod & MOD_NOSHIFT) ) /* must work with or without shift, but only without other modifiers*/
	{
          sdlReadState(SLOT_POS_LOAD_BACKUP);
        }
        break;
      case SDLK_F10:
        /* F10 is "save backup" - what was in the last overwritten savestate before we overwrote it*/
        if ( ! (event.key.keysym.mod & MOD_NOSHIFT) ) /* must work with or without shift, but only without other modifiers*/
	{
          sdlReadState(SLOT_POS_SAVE_BACKUP);
        }
        break;
      case SDLK_1:
      case SDLK_2:
      //case SDLK_3:
      case SDLK_4:
        if(!(event.key.keysym.mod & MOD_NOALT) &&
           (event.key.keysym.mod & KMOD_ALT)) {
          const char *disableMessages[4] =
            { "autofire A disabled",
              "autofire B disabled",
              "autofire R disabled",
              "autofire L disabled"};
          const char *enableMessages[4] =
            { "autofire A",
              "autofire B",
              "autofire R",
              "autofire L"};

	  EKey k = KEY_BUTTON_A;
	  if (event.key.keysym.sym == SDLK_1)
	    k = KEY_BUTTON_A;
	  else if (event.key.keysym.sym == SDLK_2)
	    k = KEY_BUTTON_B;
	  else if (event.key.keysym.sym == SDLK_3)
	    k = KEY_BUTTON_R;
	  else if (event.key.keysym.sym == SDLK_4)
	    k = KEY_BUTTON_L;

          if(inputToggleAutoFire(k)) {
            systemScreenMessage(enableMessages[event.key.keysym.sym - SDLK_1]);
          } else {
            systemScreenMessage(disableMessages[event.key.keysym.sym - SDLK_1]);
          }
        } else if(!(event.key.keysym.mod & MOD_NOCTRL) &&
             (event.key.keysym.mod & KMOD_CTRL)) {
          int mask = 0x0100 << (event.key.keysym.sym - SDLK_1);
          layerSettings ^= mask;
          layerEnable = DISPCNT & layerSettings;
          CPUUpdateRenderBuffers(false);
        }
        break;
      case SDLK_5:
      case SDLK_6:
      case SDLK_7:
      case SDLK_8:
        if(!(event.key.keysym.mod & MOD_NOCTRL) &&
           (event.key.keysym.mod & KMOD_CTRL)) {
          int mask = 0x0100 << (event.key.keysym.sym - SDLK_1);
          layerSettings ^= mask;
          layerEnable = DISPCNT & layerSettings;
        }
        break;
      case SDLK_n:
        if(!(event.key.keysym.mod & MOD_NOCTRL) &&
           (event.key.keysym.mod & KMOD_CTRL)) {
          if(paused)
            paused = false;
          pauseNextFrame = true;
        }
        break;
      default:
        break;
      }
      inputProcessSDLEvent(event);
      break;
    }
  }
}

void usage(char *cmd)
{
  printf("%s [option ...] file\n", cmd);
  printf("\
\n\
Options:\n\
  -O, --opengl=MODE            Set OpenGL texture filter\n\
      --no-opengl               0 - Disable OpenGL\n\
      --opengl-nearest          1 - No filtering\n\
      --opengl-bilinear         2 - Bilinear filtering\n\
  -F, --fullscreen             Full screen\n\
  -G, --gdb=PROTOCOL           GNU Remote Stub mode:\n\
                                tcp      - use TCP at port 55555\n\
                                tcp:PORT - use TCP at port PORT\n\
                                pipe     - use pipe transport\n\
  -I, --ifb-filter=FILTER      Select interframe blending filter:\n\
");
  printf("\
  -N, --no-debug               Don't parse debug information\n\
  -S, --flash-size=SIZE        Set the Flash size\n\
      --flash-64k               0 -  64K Flash\n\
      --flash-128k              1 - 128K Flash\n\
  -T, --throttle=THROTTLE      Set the desired throttle (5...1000)\n\
  -b, --bios=BIOS              Use given bios file\n\
  -c, --config=FILE            Read the given configuration file\n\
  -f, --filter=FILTER          Select filter:\n\
");
  printf("\
  -h, --help                   Print this help\n\
  -i, --patch=PATCH            Apply given patch\n\
  -p, --profile=[HERTZ]        Enable profiling\n\
  -s, --frameskip=FRAMESKIP    Set frame skip (0...9)\n\
  -t, --save-type=TYPE         Set the available save type\n\
      --save-auto               0 - Automatic (EEPROM, SRAM, FLASH)\n\
      --save-eeprom             1 - EEPROM\n\
      --save-sram               2 - SRAM\n\
      --save-flash              3 - FLASH\n\
      --save-sensor             4 - EEPROM+Sensor\n\
      --save-none               5 - NONE\n\
  -v, --verbose=VERBOSE        Set verbose logging (trace.log)\n\
                                  1 - SWI\n\
                                  2 - Unaligned memory access\n\
                                  4 - Illegal memory write\n\
                                  8 - Illegal memory read\n\
                                 16 - DMA 0\n\
                                 32 - DMA 1\n\
                                 64 - DMA 2\n\
                                128 - DMA 3\n\
                                256 - Undefined instruction\n\
                                512 - AGBPrint messages\n\
\n\
Long options only:\n\
      --agb-print              Enable AGBPrint support\n\
      --auto-frameskip         Enable auto frameskipping\n\
      --no-agb-print           Disable AGBPrint support\n\
      --no-auto-frameskip      Disable auto frameskipping\n\
      --no-patch               Do not automatically apply patch\n\
      --no-pause-when-inactive Don't pause when inactive\n\
      --no-rtc                 Disable RTC support\n\
      --no-show-speed          Don't show emulation speed\n\
      --no-throttle            Disable throttle\n\
      --pause-when-inactive    Pause when inactive\n\
      --rtc                    Enable RTC support\n\
      --show-speed-normal      Show emulation speed\n\
      --show-speed-detailed    Show detailed speed data\n\
      --cheat 'CHEAT'          Add a cheat\n\
");
}

int main(int argc, char **argv)
{
#ifndef FINAL_BUILD
  fprintf(stdout, "VBA-M version %s [SDL]\n", "Git:");
#else
  fprintf(stdout, "VBA-M version %s [SDL]");
#endif

  home = argv[0];
  SetHome(home);

  frameSkip = 2;

  parseDebug = true;

  inputSetKeymap(PAD_1, KEY_LEFT, ReadPrefHex("Joy0_Left"));
  inputSetKeymap(PAD_1, KEY_RIGHT, ReadPrefHex("Joy0_Right"));
  inputSetKeymap(PAD_1, KEY_UP, ReadPrefHex("Joy0_Up"));
  inputSetKeymap(PAD_1, KEY_DOWN, ReadPrefHex("Joy0_Down"));
  inputSetKeymap(PAD_1, KEY_BUTTON_A, ReadPrefHex("Joy0_A"));
  inputSetKeymap(PAD_1, KEY_BUTTON_B, ReadPrefHex("Joy0_B"));
  inputSetKeymap(PAD_1, KEY_BUTTON_L, ReadPrefHex("Joy0_L"));
  inputSetKeymap(PAD_1, KEY_BUTTON_R, ReadPrefHex("Joy0_R"));
  inputSetKeymap(PAD_1, KEY_BUTTON_START, ReadPrefHex("Joy0_Start"));
  inputSetKeymap(PAD_1, KEY_BUTTON_SELECT, ReadPrefHex("Joy0_Select"));
  inputSetKeymap(PAD_1, KEY_BUTTON_SPEED, ReadPrefHex("Joy0_Speed"));
  inputSetKeymap(PAD_1, KEY_BUTTON_CAPTURE, ReadPrefHex("Joy0_Capture"));
  inputSetMotionKeymap(KEY_LEFT, ReadPrefHex("Motion_Left"));
  inputSetMotionKeymap(KEY_RIGHT, ReadPrefHex("Motion_Right"));
  inputSetMotionKeymap(KEY_UP, ReadPrefHex("Motion_Up"));
  inputSetMotionKeymap(KEY_DOWN, ReadPrefHex("Motion_Down"));

  LoadConfig(); // Parse command line arguments (overrides ini)
  ReadOpts(argc, argv);
  
  Create_Config_Folder();

  sdlSaveKeysSwitch = (ReadPrefHex("saveKeysSwitch"));
  sdlOpenglScale = (ReadPrefHex("openGLscale"));

  if(optPrintUsage) {
    usage(argv[0]);
    exit(-1);
  }

  if(optind < argc) {
    char *szFile = argv[optind];

    utilStripDoubleExtension(szFile, filename);
    char *p = strrchr(filename, '.');

    if(p)
      *p = 0;

    if (autoPatch && patchNum == 0)
    {
      char * tmp;
      // no patch given yet - look for ROMBASENAME.ips
      tmp = (char *)malloc(strlen(filename) + 4 + 1);
      sprintf(tmp, "%s.ips", filename);
      patchNames[patchNum] = tmp;
      patchNum++;

      // no patch given yet - look for ROMBASENAME.ups
      tmp = (char *)malloc(strlen(filename) + 4 + 1);
      sprintf(tmp, "%s.ups", filename);
      patchNames[patchNum] = tmp;
      patchNum++;

      // no patch given yet - look for ROMBASENAME.ppf
      tmp = (char *)malloc(strlen(filename) + 4 + 1);
      sprintf(tmp, "%s.ppf", filename);
      patchNames[patchNum] = tmp;
      patchNum++;
    }

    soundInit();

    bool failed = false;

    IMAGE_TYPE type = utilFindType(szFile);

    if(type == IMAGE_UNKNOWN) {
      systemMessage(0, "Unknown file type %s", szFile);
      exit(-1);
    }

	int size = CPULoadRom(szFile);
	failed = (size == 0);
	if(!failed) 
	{
		if (cpuSaveType == 0)
			utilGBAFindSave(size);
		else
			saveType = cpuSaveType;

        sdlApplyPerImagePreferences();

        doMirroring(mirroringEnable);

        emulator = GBASystem;

        CPUInit(biosFileNameGBA, useBios);
        int patchnum;
        for (patchnum = 0; patchnum < patchNum; patchnum++) {
          fprintf(stdout, "Trying patch %s%s\n", patchNames[patchnum],
            applyPatch(patchNames[patchnum], &rom, &size) ? " [success]" : "");
        }
        CPUReset();
	}

    if(failed) {
      systemMessage(0, "Failed to load file %s", szFile);
      exit(-1);
    }
  } else {
    soundInit();
    strcpy(filename, "gnu_stub");
    rom = (uint8_t *)malloc(0x2000000);
    workRAM = (uint8_t *)calloc(1, 0x40000);
    bios = (uint8_t *)calloc(1,0x4000);
    internalRAM = (uint8_t *)calloc(1,0x8000);
    paletteRAM = (uint8_t *)calloc(1,0x400);
    vram = (uint8_t *)calloc(1, 0x20000);
    oam = (uint8_t *)calloc(1, 0x400);
    pix = (uint8_t *)calloc(1, 2 * 240 * 160);
    ioMem = (uint8_t *)calloc(1, 0x400);

    emulator = GBASystem;

    CPUInit(biosFileNameGBA, useBios);
    CPUReset();
  }

  sdlReadBattery();


  int flags = SDL_INIT_VIDEO|SDL_INIT_AUDIO;

  if(SDL_Init(flags)) {
    systemMessage(0, "Failed to init SDL: %s", SDL_GetError());
    exit(-1);
  }

  if(SDL_InitSubSystem(SDL_INIT_JOYSTICK)) {
    systemMessage(0, "Failed to init joystick support: %s", SDL_GetError());
  }

  inputInitJoysticks();

	sizeX = 240;
	sizeY = 160;
	systemFrameSkip = frameSkip;

	sdlReadDesktopVideoMode();

	sdlInitVideo();

	if(systemColorDepth == 15)
		systemColorDepth = 16;

  if(systemColorDepth != 16 && systemColorDepth != 24 &&
     systemColorDepth != 32) {
    fprintf(stderr,"Unsupported color depth '%d'.\nOnly 16, 24 and 32 bit color depths are supported\n", systemColorDepth);
    exit(-1);
  }

  fprintf(stdout,"Color depth: %d\n", systemColorDepth);
  
  utilUpdateSystemColorMaps();

  emulating = 1;
  renderedFrames = 0;

  autoFrameSkipLastTime = throttleLastTime = systemGetClock();

  SDL_WM_SetCaption("VBA-M", NULL);

  while(emulating) 
  {
    emulator.emuMain(emulator.emuCount);
    sdlPollEvents();
  }

  emulating = 0;
  fprintf(stdout,"Shutting down\n");
  soundShutdown();

  if(rom != NULL) {
    sdlWriteBattery();
    emulator.emuCleanUp();
  }

  for (int i = 0; i < patchNum; i++) {
    free(patchNames[i]);
  }

  SaveConfigFile();
  CloseConfig();
  Free_config();
  SDL_FreeSurface(real_video);
  SDL_Quit();
  
  return 0;
}

void systemMessage(int num, const char *msg, ...)
{
  va_list valist;

  va_start(valist, msg);
  vfprintf(stderr, msg, valist);
  fprintf(stderr, "\n");
  va_end(valist);
}

void drawScreenMessage(uint8_t *screen, int pitch, int x, int y, unsigned int duration)
{
  if(screenMessage) {
    if(((systemGetClock() - screenMessageTime) < duration) &&
       !disableStatusMessages) {
      drawText(screen, pitch, x, y,
               screenMessageBuffer, false);
    } else {
      screenMessage = false;
    }
  }
}

void drawSpeed(uint8_t *screen, int pitch, int x, int y)
{
  char buffer[50];
  if(showSpeed == 1)
    sprintf(buffer, "%d%%", systemSpeed);
  else
    sprintf(buffer, "%3d%%(%d, %d fps)", systemSpeed,
            systemFrameSkip,
            showRenderedFrames);

  drawText(screen, pitch, x, y, buffer, showSpeedTransparent);
}

static void bitmap_scale(uint32_t startx, uint32_t starty, uint32_t viswidth, uint32_t visheight, uint32_t newwidth, uint32_t newheight,uint32_t pitchsrc,uint32_t pitchdest, uint16_t* src, uint16_t* dst)
{
    uint32_t W,H,ix,iy,x,y;
    x=startx<<16;
    y=starty<<16;
    W=newwidth;
    H=newheight;
    ix=(viswidth<<16)/W;
    iy=(visheight<<16)/H;

    do 
    {
        uint16_t* buffer_mem=&src[(y>>16)*pitchsrc];
        W=newwidth; x=startx<<16;
        do 
        {
            *dst++=buffer_mem[x>>16];
            x+=ix;
        } while (--W);
        dst+=pitchdest;
        y+=iy;
    } while (--H);
}

void systemDrawScreen()
{
	uint16_t __restrict__ *src, *dst;
	uint32_t pitch, y;
	renderedFrames++;
	
	bitmap_scale(0, 0, 240, 160, real_video->w, real_video->h, 240, 0, (uint16_t*)pix, (uint16_t*)real_video->pixels);
/*
	pitch = 320;
	src = (uint16_t* __restrict__)pix;
	dst = (uint16_t* __restrict__)real_video->pixels
		+ ((320 - 240) / 4) * sizeof(uint16_t)
		+ ((240 - 160) / 2) * pitch;
	for (y = 0; y < 160; y++)
	{
		memmove(dst, src, 240 * sizeof(uint16_t));
		src += 240;
		dst += pitch;
	}
*/
	SDL_Flip(real_video);
}

void systemSetTitle(const char *title)
{
	SDL_WM_SetCaption(title, NULL);
}

void systemShowSpeed(int speed)
{
  systemSpeed = speed;

  showRenderedFrames = renderedFrames;
  renderedFrames = 0;

  if(!fullScreen && showSpeed) {
    char buffer[80];
    if(showSpeed == 1)
      sprintf(buffer, "VBA-M - %d%%", systemSpeed);
    else
      sprintf(buffer, "VBA-M - %d%%(%d, %d fps)", systemSpeed,
              systemFrameSkip,
              showRenderedFrames);

    systemSetTitle(buffer);
  }
}

void systemFrame()
{
}

void system10Frames(int rate)
{
  uint32_t time = systemGetClock();
  if(!wasPaused && autoFrameSkip) {
    uint32_t diff = time - autoFrameSkipLastTime;
    int speed = 100;

    if(diff)
      speed = (1000000/rate)/diff;

    if(speed >= 98) {
      frameskipadjust++;

      if(frameskipadjust >= 3) {
        frameskipadjust=0;
        if(systemFrameSkip > 0)
          systemFrameSkip--;
      }
    } else {
      if(speed  < 80)
        frameskipadjust -= (90 - speed)/5;
      else if(systemFrameSkip < 9)
        frameskipadjust--;

      if(frameskipadjust <= -2) {
        frameskipadjust += 2;
        if(systemFrameSkip < 9)
          systemFrameSkip++;
      }
    }
  }
  if(systemSaveUpdateCounter) {
    if(--systemSaveUpdateCounter <= SYSTEM_SAVE_NOT_UPDATED) {
      sdlWriteBattery();
      systemSaveUpdateCounter = SYSTEM_SAVE_NOT_UPDATED;
    }
  }

  wasPaused = false;
  autoFrameSkipLastTime = time;
}

void systemScreenCapture(int a)
{
  char buffer[2048];

  if(captureFormat) {
    if(screenShotDir)
      sprintf(buffer, "%s/%s%02d.bmp", screenShotDir, sdlGetFilename(filename), a);
    else if (homeDir)
      sprintf(buffer, "%s/%s/%s%02d.bmp", homeDir, DOT_DIR, sdlGetFilename(filename), a);
    else
      sprintf(buffer, "%s%02d.bmp", filename, a);

    emulator.emuWriteBMP(buffer);
  } else {
    if(screenShotDir)
      sprintf(buffer, "%s/%s%02d.png", screenShotDir, sdlGetFilename(filename), a);
    else if (homeDir)
      sprintf(buffer, "%s/%s/%s%02d.png", homeDir, DOT_DIR, sdlGetFilename(filename), a);
    else
      sprintf(buffer, "%s%02d.png", filename, a);
    emulator.emuWritePNG(buffer);
  }

  systemScreenMessage("Screen capture");
}

void systemSaveOldest()
{
    // I need to be implemented
}

void systemLoadRecent()
{
    // I need to be implemented
}

uint32_t systemGetClock()
{
  return SDL_GetTicks();
}

void systemGbPrint(uint8_t *data,int len,int pages,int feed,int palette, int contrast)
{
}

/* xKiv: added timestamp */
void systemConsoleMessage(const char *msg)
{
  time_t now_time;
  struct tm now_time_broken;

  now_time		= time(NULL);
  now_time_broken	= *(localtime( &now_time ));
  fprintf(
		stdout,
		"%02d:%02d:%02d %02d.%02d.%4d: %s\n",
		now_time_broken.tm_hour,
		now_time_broken.tm_min,
		now_time_broken.tm_sec,
		now_time_broken.tm_mday,
		now_time_broken.tm_mon + 1,
		now_time_broken.tm_year + 1900,
		msg
  );
}

void systemScreenMessage(const char *msg)
{

  screenMessage = true;
  screenMessageTime = systemGetClock();
  if(strlen(msg) > 20) {
    strncpy(screenMessageBuffer, msg, 20);
    screenMessageBuffer[20] = 0;
  } else
    strcpy(screenMessageBuffer, msg);

  systemConsoleMessage(msg);
}

bool systemCanChangeSoundQuality()
{
  return false;
}

bool systemPauseOnFrame()
{
	if(pauseNextFrame) {
		paused = true;
		pauseNextFrame = false;
		return true;
	}
	return false;
}

bool systemReadJoypads()
{
	return true;
}

uint32_t systemReadJoypad(int which)
{
	return inputReadJoypad(which);
}
//static uint8_t sensorDarkness = 0xE8; // total darkness (including daylight on rainy days)

void systemUpdateSolarSensor()
{
}

void systemCartridgeRumble(bool)
{
}

void systemUpdateMotionSensor()
{
	inputUpdateMotionSensor();
	systemUpdateSolarSensor();
}

int systemGetSensorX()
{
	return inputGetSensorX();
}

int systemGetSensorY()
{
	return inputGetSensorY();
}

int systemGetSensorZ()
{
	return 0;
}

uint8_t systemGetSensorDarkness()
{
	return 0;
}

SoundDriver * systemSoundInit()
{
	soundShutdown();

	return new SoundSDL();
}

void systemOnSoundShutdown()
{
}

void systemOnWriteDataToSoundBuffer(const uint16_t * finalWave, int length)
{
}

void log(const char *defaultMsg, ...)
{
  static FILE *out = NULL;

  if(out == NULL) {
    out = fopen("trace.log","w");
  }

  va_list valist;

  va_start(valist, defaultMsg);
  vfprintf(out, defaultMsg, valist);
  va_end(valist);
}
