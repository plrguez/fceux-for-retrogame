#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <SDL/SDL.h>

#include "../dingoo.h"
#include "../dingoo-video.h"
#include "../keyscan.h"

#include "../dface.h"

#include "font.h"

#define OPTION_LABEL_COLUMN	60
#define OPTION_VALUE_COLUMN	220

// ...
extern SDL_Surface* screen;
extern int RunFileBrowser(char *source, char *romname, const char *types[],
		const char *info = NULL);
extern int isloaded;

typedef struct _menu_entry {
	const char *name;
	const char *info;
	int (*command)();
} MenuEntry;

SDL_Surface *gui_screen;
static SDL_Surface *g_bg;
static uint16 *g_psdl;
static uint8 g_preview[256 * 256 + 8];
static uint8 g_ispreview;
static char g_romname[32] = "";
static int g_dirty = 1;
int g_slot = 0; // make it accessible from input.cpp
static int g_romtype;
static unsigned long g_key = 0, last_key;
static int counter = 0;
static Config *oldConfig=NULL;
static uint8 *g_keyState = 0;

void FCEUGUI_Flip()
{
	SDL_Rect dstrect;

	dstrect.x = (screen->w - 320) / 2;
	dstrect.y = (screen->h - 240) / 2;

	SDL_BlitSurface(gui_screen, 0, screen, &dstrect);
	SDL_Flip(screen);
}

void readkey() 
{
	SDL_Event event;

	last_key = g_key;

	// loop, handling all pending events
	while(SDL_PollEvent(&event))
		switch(event.type) {
			case SDL_KEYUP:
				SDL_EnableKeyRepeat(0,0);
				g_key = 0;
				return;
			case SDL_KEYDOWN:
				SDL_EnableKeyRepeat(SDL_DEFAULT_REPEAT_DELAY + 100, SDL_DEFAULT_REPEAT_INTERVAL);
				g_key = event.key.keysym.sym;
				return;
		}

	if (g_key == 0)
		counter = 0;
}

int parsekey(unsigned long code, int repeat = 0) 
{
	if (g_key == code) {
		if (g_key != last_key) {
			counter = 0;
			g_dirty = 1;
			return 1;
		}

		if (repeat && (counter > 5)) {
			counter = 0;
			g_dirty = 1;
			return 1;
		}

		counter++;
		return 0;
	}

	return 0;
}

void draw_bg(SDL_Surface *bg) 
{
	if(bg)
		SDL_BlitSurface(bg, NULL, gui_screen, NULL);
	else
		SDL_FillRect(gui_screen, NULL, (1<<11) | (8<<5) | 10);
}

int update_time()
{

	return 1; // always set g_dirty
}

// Include additional files
#include "gui_submenu.cpp"
#include "file_browser.cpp"
#include "settings_menu.cpp"
#include "cheat_browser.cpp"

/* MENU COMMANDS */

// Load preview from save state
void load_preview() {
	char sname[2048];
	strcpy(sname, FCEU_MakeFName(FCEUMKF_STATE, g_slot, 0).c_str());
	strcat(sname, ".preview");

	FILE *fp = fopen(sname, "rb");
	if (fp) {
		fread(g_preview, 1, 256 * 256 + 8, fp);
		fclose(fp);
		g_ispreview = 1;
	} else {
		memset(g_preview, 0, 256 * 256 + 8);
		g_ispreview = 0;
	}
}

void save_preview()
{
	char sname[2048];
	strcpy(sname, FCEU_MakeFName(FCEUMKF_STATE, g_slot, 0).c_str());
	strcat(sname, ".preview");

	FILE *fp = fopen(sname, "wb");
	if (fp) {
		extern uint8 *XBuf;
		fwrite(XBuf, 1, 256 * 256 + 8, fp);
		fclose(fp);
	}
}

#define MASK  			    (0xF7DEF7DE | (0xF7DEF7DE << 16))
#define LMASK 			    (0x08210821 | (0x08210821 << 16))
#define INTERPOLATE(A, B) 	(((A & MASK) >> 1) + ((B & MASK) >> 1) + (A & B & LMASK))

void draw_preview(unsigned short *dest, int x, int y) {
	uint8 *PBuf = g_preview;
	uint16 *dst = (uint16 *) dest;

	PBuf += 256 * 8;
	dst += y * 320 + x;

	for (y = 0; y < 76; y++) {
		for (x = 0; x < 32; x++) {
			*dst++ = INTERPOLATE(g_psdl[*PBuf], g_psdl[*(PBuf+1)]);
			*dst++ = INTERPOLATE(g_psdl[*(PBuf+3)], g_psdl[*(PBuf+4)]);
			*dst++ = INTERPOLATE(g_psdl[*(PBuf+7)], g_psdl[*(PBuf+6)]);
			PBuf += 8;
		}

		PBuf += 256 * 2;
		dst += 224;
	}
}

void draw_shot_preview(unsigned short *dest, int x, int y) {
	extern uint8 *XBuf;
	uint8 *PBuf = XBuf;
	uint16 *dst = (uint16 *) dest;

	PBuf += 256 * 8;
	dst += y * 320 + x;

	for (y = 0; y < 76; y++) {
		for (x = 0; x < 32; x++) {
			*dst++ = INTERPOLATE(g_psdl[*PBuf], g_psdl[*(PBuf+1)]);
			*dst++ = INTERPOLATE(g_psdl[*(PBuf+3)], g_psdl[*(PBuf+4)]);
			*dst++ = INTERPOLATE(g_psdl[*(PBuf+7)], g_psdl[*(PBuf+6)]);
			PBuf += 8;
		}

		PBuf += 256 * 2;
		dst += 224;
	}
}

// Main menu commands
static int load_rom() {
	const char *types[] = { ".nes", ".fds", ".zip", ".fcm", ".fm2", ".nsf",
			NULL };
	char filename[128] = "\0", romname[128];
	int error;

	#ifdef WIN32
	if (!RunFileBrowser("d:\\", filename, types)) {
	#else
	if (!RunFileBrowser(NULL, filename, types)) {
	#endif
		CloseGame();
		SDL_Quit();
		exit(-1);
	}

	// Exit from selector without any file selected.
	if (filename[0] == '\0')
	    return 0;

	//  TODO - Must close game here?
	CloseGame();

	// Is this a movie?
	if (!(error = FCEUD_LoadMovie(filename, romname)))
		error = LoadGame(filename);

	if (error != 1) {
		CloseGame();
		SDL_Quit();
		exit(-1);
	}

	if (oldConfig) {
	        free(oldConfig);
	        oldConfig = NULL;
	}

	// Set last filename behind CloseGame to avoid save bad values in cfg
	// for previous loaded rom
	g_config->setOption("SDL.LastOpenFile", filename);

	return 1;
}
	
static int enable_cheats(void) {
	return RunCheatBrowser();
}

static int reset_nes() {
	FCEUI_ResetNES();
	return 1;
}

void HardReset(void)
{
	if (isloaded)
	{
		const char *lastFile;
		g_config->getOption("SDL.LastOpenFile", &lastFile);
		LoadGame(lastFile);
	}
}

static void backupCurrentConfig(void) {
	if (oldConfig) {
		free(oldConfig);
		oldConfig = NULL;
	}
	oldConfig = new Config(*g_config);
}

static void resetIfNeeded(void) {
	int gg_previous, gg_new;

	if (!oldConfig)
	    return;

	oldConfig->getOption("SDL.GameGenie", &gg_previous);
	g_config->getOption("SDL.GameGenie", &gg_new);

	if (gg_previous != gg_new)
	    HardReset();
	free(oldConfig);
	oldConfig = NULL;
}

// Dirty way of flipping disc
extern uint8 SelectDisk, InDisk;
static int flip_disc() {
	if (g_romtype != GIT_FDS)
		return 0;
	FCEUI_FDSFlip();
	return 1;
}

static int save_state() {
	FCEUI_SaveState(NULL);
	save_preview();
	load_preview();
	return 1;
}

static int load_state() {
	FCEUI_LoadState(NULL);
	return 1;
}

static int save_screenshot() {
	FCEUI_SaveSnapshot();
	return 1;
}

static int cmd_settings_menu() {
	return RunSettingsMenu();
}

static int cmd_exit() {
	FCEUI_CloseGame();
	return 1;
}

/* MAIN MENU */

static MenuEntry main_menu[] = { 
		{ "Load ROM", "Load new rom or movie", load_rom },
		{ "Reset", "Reset NES", reset_nes },
		{ "Cheats", "Enable and import cheats", enable_cheats },
		{ "Flip disc", "Switch side or disc (FDS)", flip_disc },
		{ "Save state", "Save current state", save_state },
		{ "Load state", "Load emulation state", load_state },
		{ "Screenshot", "Save current frame shot", save_screenshot },
		{ "Settings", "Change current settings", cmd_settings_menu },
		{ "Exit", "Exit emulator", cmd_exit } 
};
static int main_menu_items = sizeof(main_menu) / sizeof(main_menu[0]);
static MenuEntry main_menu_bck[sizeof(main_menu) / sizeof(main_menu[0])];

extern char FileBase[2048];

int FCEUGUI_Init(FCEUGI *gi) 
{

	// create 565 RGB surface
	gui_screen = SDL_CreateRGBSurface(SDL_SWSURFACE, 320, 240, 16, 0xf800, 0x7e0, 0x1f, 0);
	if(!gui_screen) printf("Error creating surface gui\n");

	// Load bg image
	g_bg = SDL_LoadBMP("./bg.bmp");

	if (InitFont() < 0)
		return -2;

	// Taken from RetroFW (pingflood)
	for (int i = 0; i < main_menu_items; i++)
		main_menu_bck[i] = main_menu[i];

	if (gi) {
		if (strlen(FileBase) > 28) {
			strncpy(g_romname, FileBase, 24);
			strcat(g_romname, "...");
		} else
			strcpy(g_romname, FileBase);
		g_romtype = gi->type;
	}

	return 0;
}

void FCEUGUI_Reset(FCEUGI *gi) {
	g_psdl = FCEUD_GetPaletteArray16();

	if (strlen(FileBase) > 28) {
		strncpy(g_romname, FileBase, 24);
		strcat(g_romname, "...");
	} else
		strcpy(g_romname, FileBase);
	g_romtype = gi->type;
}

void FCEUGUI_Kill() {
	// free stuff
	if (g_bg)
		SDL_FreeSurface(g_bg);
	if (gui_screen)
		SDL_FreeSurface(gui_screen);
	KillFont();
}

extern void InitGuiVideo();
// OpenDingux - Fix flip disk side
extern int FDSSwitchRequested;

void menu_items_process(int *current_menu_items) {
	// Remove FDS Flip if not disk is loaded
	if (g_romtype != GIT_FDS && *current_menu_items == main_menu_items) { // Remove "Flip disc" if not a FDS
		for (int i = 3; i < main_menu_items - 1; i++)
			main_menu[i] = main_menu_bck[i+1];
		(*current_menu_items)--;
	} else if (g_romtype == GIT_FDS && *current_menu_items < main_menu_items) {
		for (int i = 0; i < main_menu_items; i++)
			main_menu[i] = main_menu_bck[i];
		*current_menu_items = main_menu_items;
	}
}

void FCEUGUI_Run() {
	static int index = 0;
	static int spy = 72;
	int done = 0, y, i;
	static int current_menu_items = main_menu_items;

	backupCurrentConfig();

	InitGuiVideo();

	menu_items_process(&current_menu_items);

	load_preview();

	g_dirty = 1;
	while (!done) {

		// Parse input
		readkey();
		if (parsekey(DINGOO_B))
			done = 1;

		if (parsekey(DINGOO_UP, 0)) {
			if (index > 0) {
				index--;
				spy -= 16;
			} else {
				index = current_menu_items - 1;
				spy = 72 + 16*index;
			}
		}

		if (parsekey(DINGOO_DOWN, 0)) {
			if (index < current_menu_items - 1) {
				index++;
				spy += 16;
			} else {
				index = 0;
				spy = 72;
			}
		}

		if (parsekey(DINGOO_A)) {
			done = main_menu[index].command();
		}

		if (!strncmp(main_menu[index].name, "Save state", 10) || !strncmp(main_menu[index].name, "Load state", 10)) {
			if (parsekey(DINGOO_RIGHT, 0)) {
				if (g_slot < 9) {
					g_slot++;
					FCEUI_SelectState(g_slot, 0);
					load_preview();
				}
			}

			if (parsekey(DINGOO_LEFT, 0)) {
				if (g_slot > 0) {
					g_slot--;
					FCEUI_SelectState(g_slot, 0);
					load_preview();
				}
			}
		}

		// Must draw bg only when needed
		// Draw stuff
		if (g_dirty) {
			draw_bg(g_bg);
			
			//Draw Top and Bottom Bars
			DrawChar(gui_screen, SP_SELECTOR, 0, 37);
			DrawChar(gui_screen, SP_SELECTOR, 81, 37);
			DrawChar(gui_screen, SP_SELECTOR, 0, 225);
			DrawChar(gui_screen, SP_SELECTOR, 81, 225);
			DrawText(gui_screen, "B - Go Back", 235, 225);
			DrawChar(gui_screen, SP_LOGO, 12, 9);
			
			// Draw selector
			DrawChar(gui_screen, SP_SELECTOR, 56, spy);
			DrawChar(gui_screen, SP_SELECTOR, 77, spy);

			if (!strncmp(main_menu[index].name, "Save state", 10) || !strncmp(main_menu[index].name, "Load state", 10)) {
				// Draw state preview
				DrawChar(gui_screen, SP_PREVIEWBLOCK, 184, 73);
				draw_preview((unsigned short *)gui_screen->pixels, 185, 100);
				if (!g_ispreview)
					DrawChar(gui_screen, SP_NOPREVIEW, 207, 135);
			}

			if (!strncmp(main_menu[index].name, "Screenshot", 10)) {
				DrawChar(gui_screen, SP_PREVIEWBLOCK, 184, 73);
				draw_shot_preview((unsigned short *)gui_screen->pixels, 185, 100);
			}

			DrawText(gui_screen, "Now Playing:", 8, 37);
			DrawText(gui_screen, g_romname, 96, 37);

			// Draw menu
			for (i = 0, y = 72; i < current_menu_items; i++, y += 16) {
				DrawText(gui_screen, main_menu[i].name, 60, y);
			}

			// Draw info
			DrawText(gui_screen, main_menu[index].info, 8, 225);

			// If save/load state render slot preview and number
			if (!strncmp(main_menu[index].name, "Save state", 10) || !strncmp(main_menu[index].name, "Load state", 10)) {
				char tmp[32];
				sprintf(tmp, "Slot %d", g_slot);
				DrawText(gui_screen, tmp, 212, 80);

				if (g_slot > 0)
					DrawChar(gui_screen, SP_LEFTARROW, 197, 83);
				if (g_slot < 9)
					DrawChar(gui_screen, SP_RIGHTARROW, 259, 83);
			}

			// If screenshot render current frame preview
			if (!strncmp(main_menu[index].name, "Screenshot", 10)) {
				DrawText(gui_screen, "Preview", 207, 80);
			}

			g_dirty = 0;
		}

		SDL_Delay(16);

		// Update real screen
		FCEUGUI_Flip();
	}

	g_psdl = FCEUD_GetPaletteArray16();

	// Must update emulation core and drivers
	UpdateEMUCore(g_config);
	UpdateInputConfig(g_config);
	// OpenDingux - Fix flip disk side
	// Save switch request here, in FCEUD_DriverReset reset it
	int save_FDSSwitchRequested = FDSSwitchRequested;
	FCEUD_DriverReset();
	FDSSwitchRequested = save_FDSSwitchRequested;

	resetIfNeeded();

	// Clear screen
	dingoo_clear_video();

	g_key = 0;
	counter = 0;
}
