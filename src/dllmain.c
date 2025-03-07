#include "boilerplate.h"
#include "helpers.h"
#include "poll.h"
#include <stdio.h>
#include <sys/stat.h>
#include <time.h>

bool testEnabled     = false;
u16 drumMax          = 0xFFFF;
u16 drumMin          = 0xFFFF;
char accessCode1[21] = "00000000000000000001";
char accessCode2[21] = "00000000000000000002";
char chipId1[33]     = "00000000000000000000000000000001";
char chipId2[33]     = "00000000000000000000000000000002";
char *server         = "127.0.0.1";

typedef i32 (*callbackAttach) (i32, i32, i32 *);
typedef void (*callbackTouch) (i32, i32, u8[168], u64);
typedef void event ();
typedef void waitTouchEvent (callbackTouch, u64);
bool waitingForTouch = false;
callbackTouch touchCallback;
u64 touchData;
callbackAttach attachCallback;
i32 *attachData;
HMODULE plugins[255] = { 0 };

#define ON_HIT(bind) IsButtonTapped (bind) ? drumMax == drumMin ? drumMax : (u16)(rand () % drumMax + drumMin) : 0

Keybindings EXIT          = { .keycodes = { VK_ESCAPE } };
Keybindings TEST          = { .keycodes = { VK_F1 } };
Keybindings SERVICE       = { .keycodes = { VK_F2 } };
Keybindings DEBUG_UP      = { .keycodes = { VK_UP } };
Keybindings DEBUG_DOWN    = { .keycodes = { VK_DOWN } };
Keybindings DEBUG_ENTER   = { .keycodes = { VK_RETURN } };
Keybindings COIN_ADD      = { .keycodes = { VK_RETURN }, .buttons = { SDL_CONTROLLER_BUTTON_START } };
Keybindings CARD_INSERT_1 = { .keycodes = { 'P' } };
Keybindings CARD_INSERT_2 = {};
Keybindings P1_LEFT_BLUE  = { .keycodes = { 'D' }, .axis = { SDL_AXIS_LTRIGGER_DOWN } };
Keybindings P1_LEFT_RED   = { .keycodes = { 'F' }, .buttons = { SDL_CONTROLLER_BUTTON_LEFTSTICK } };
Keybindings P1_RIGHT_RED  = { .keycodes = { 'J' }, .buttons = { SDL_CONTROLLER_BUTTON_RIGHTSTICK } };
Keybindings P1_RIGHT_BLUE = { .keycodes = { 'K' }, .axis = { SDL_AXIS_RTRIGGER_DOWN } };
Keybindings P2_LEFT_BLUE  = {};
Keybindings P2_LEFT_RED   = {};
Keybindings P2_RIGHT_RED  = {};
Keybindings P2_RIGHT_BLUE = {};

u32 inline generate_rand () { return rand () + rand () * rand () + rand (); }

const char *
lookupCardId (const char *searchAccessCode) {
	FILE *cards = fopen (configPath ("cards.dat"), "r");
	char currentAccessCode[21];
	char currentCardId[33];

	while (fscanf (cards, "%20s%32s", currentAccessCode, currentCardId) == 2) {
		printInfo ("Current card: %s\n", currentCardId);
		if (strcmp (searchAccessCode, currentAccessCode) == 0) {
			fclose (cards);
			return strdup (currentCardId);
		}
	}

	fclose (cards);
	printWarning ("Unable to find card with access code %s\n", searchAccessCode);
	return NULL; // Not found
}

bool
addNewCard (const char *accessCode, const char *chipId) {
	printInfo ("Adding new card %s | ID: %s\n", accessCode, chipId);
	FILE *file = fopen (configPath ("cards.dat"), "a");
	if (file == NULL) {
		perror ("Error opening file cards.dat");
		return false; // Return null to indicate failure
	}

	// Write the new line at the end
	fprintf (file, "%s%s\n", accessCode, chipId);
	fclose (file);

	// Return true to indicate success
	return true;
}

u16 __fastcall bnusio_GetAnalogIn (u8 which) {
	switch (which) {
	case 0: return ON_HIT (P1_LEFT_BLUE);  // Player 1 Left Blue
	case 1: return ON_HIT (P1_LEFT_RED);   // Player 1 Left Red
	case 2: return ON_HIT (P1_RIGHT_RED);  // Player 1 Right Red
	case 3: return ON_HIT (P1_RIGHT_BLUE); // Player 1 Right Blue
	case 4: return ON_HIT (P2_LEFT_BLUE);  // Player 2 Left Blue
	case 5: return ON_HIT (P2_LEFT_RED);   // Player 2 Left Red
	case 6: return ON_HIT (P2_RIGHT_RED);  // Player 2 Right Red
	case 7: return ON_HIT (P2_RIGHT_BLUE); // Player 2 Right Blue
	default: return 0;
	}
}

u16 __fastcall bnusio_GetCoin (i32 a1) {
	static int coin_count = 0;

	if (a1 != 1) return coin_count;
	static bool inited       = false;
	static HWND windowHandle = 0;

	if (!inited) {
		windowHandle = FindWindowA ("nuFoundation.Window", 0);

		InitializePoll (windowHandle);

		toml_table_t *config = openConfig (configPath ("keyconfig.toml"));
		if (config) {
			SetConfigValue (config, "EXIT", &EXIT);

			SetConfigValue (config, "TEST", &TEST);
			SetConfigValue (config, "SERVICE", &SERVICE);
			SetConfigValue (config, "DEBUG_UP", &DEBUG_UP);
			SetConfigValue (config, "DEBUG_DOWN", &DEBUG_DOWN);
			SetConfigValue (config, "DEBUG_ENTER", &DEBUG_ENTER);

			SetConfigValue (config, "COIN_ADD", &COIN_ADD);
			SetConfigValue (config, "CARD_INSERT_1", &CARD_INSERT_1);
			SetConfigValue (config, "CARD_INSERT_2", &CARD_INSERT_2);

			SetConfigValue (config, "P1_LEFT_BLUE", &P1_LEFT_BLUE);
			SetConfigValue (config, "P1_LEFT_RED", &P1_LEFT_RED);
			SetConfigValue (config, "P1_RIGHT_RED", &P1_RIGHT_RED);
			SetConfigValue (config, "P1_RIGHT_BLUE", &P1_RIGHT_BLUE);
			SetConfigValue (config, "P2_LEFT_BLUE", &P2_LEFT_BLUE);
			SetConfigValue (config, "P2_LEFT_RED", &P2_LEFT_RED);
			SetConfigValue (config, "P2_RIGHT_RED", &P2_RIGHT_RED);
			SetConfigValue (config, "P2_RIGHT_BLUE", &P2_RIGHT_BLUE);

			toml_free (config);
		}

		inited = true;
	}

	UpdatePoll (windowHandle);
	if (IsButtonTapped (COIN_ADD) && !testEnabled) coin_count++;
	if (IsButtonTapped (TEST)) testEnabled = !testEnabled;
	if (IsButtonTapped (EXIT)) ExitProcess (0);
	if (waitingForTouch) {
		static u8 cardData[168]
		    = { 0x01, 0x01, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x92, 0x2E, 0x58, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00,
			    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7F, 0x5C, 0x97, 0x44, 0xF0, 0x88, 0x04, 0x00, 0x43, 0x26, 0x2C, 0x33, 0x00, 0x04,
			    0x06, 0x10, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30,
			    0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x00, 0x00, 0x00, 0x00, 0x30, 0x30, 0x30, 0x30,
			    0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00,
			    0x01, 0x00, 0x00, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x4E, 0x42, 0x47, 0x49, 0x43, 0x36,
			    0x00, 0x00, 0xFA, 0xE9, 0x69, 0x00, 0xF6, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
		bool hasInserted = false;
		if (IsButtonTapped (CARD_INSERT_1)) {
			for (int i = 0; plugins[i] != 0; i++) {
				FARPROC insertEvent = GetProcAddress (plugins[i], "BeforeCard1Insert");
				if (insertEvent) ((event *)insertEvent) ();
			}
			for (int i = 0; plugins[i] != 0; i++) {
				FARPROC insertEvent = GetProcAddress (plugins[i], "Card1Insert");
				if (insertEvent) {
					((event *)insertEvent) ();
					hasInserted = true;
				}
			}
			if (!hasInserted) {
				// Re-read the config file in case it has changed since the app started up
				toml_table_t *config = openConfig (configPath ("config.toml"));
				if (config) {
					// Look up access code and chip id from cards.dat using the value in config
					strncpy (accessCode1, readConfigString (config, "card1_access_code", accessCode1), sizeof (accessCode1) - 1);
					accessCode1[sizeof (accessCode1) - 1] = '\0'; // Ensure null-termination

					const char *foundCardId1 = lookupCardId (accessCode1);
					if (foundCardId1) {
						strncpy (chipId1, foundCardId1, sizeof (chipId1) - 1);
						chipId1[sizeof (chipId1) - 1] = '\0'; // Ensure null-termination
						free ((void *)foundCardId1);
					} else {
						srand (time (0));
						sprintf (chipId1, "%032X", generate_rand ());
						addNewCard (accessCode1, chipId1);
					}
				}

				printWarning ("Scanning card: %s | ID: %s\n", accessCode1, chipId1);
				memcpy (cardData + 0x2C, chipId1, 33);
				memcpy (cardData + 0x50, accessCode1, 21);
				touchCallback (0, 0, cardData, touchData);
			}
		} else if (IsButtonTapped (CARD_INSERT_2)) {
			for (int i = 0; plugins[i] != 0; i++) {
				FARPROC insertEvent = GetProcAddress (plugins[i], "BeforeCard2Insert");
				if (insertEvent) ((event *)insertEvent) ();
			}
			for (int i = 0; plugins[i] != 0; i++) {
				FARPROC insertEvent = GetProcAddress (plugins[i], "Card2Insert");
				if (insertEvent) {
					((event *)insertEvent) ();
					hasInserted = true;
				}
			}
			if (!hasInserted) {
				// Re-read the config file in case it has changed since the app started up
				toml_table_t *config = openConfig (configPath ("config.toml"));
				if (config) {
					// Look up access code and chip id from cards.dat using the value in config
					strncpy (accessCode2, readConfigString (config, "card2_access_code", accessCode2), sizeof (accessCode2) - 1);
					accessCode2[sizeof (accessCode2) - 1] = '\0'; // Ensure null-termination

					const char *foundCardId2 = lookupCardId (accessCode2);
					if (foundCardId2) {
						strncpy (chipId2, foundCardId2, sizeof (chipId2) - 1);
						chipId2[sizeof (chipId2) - 1] = '\0'; // Ensure null-termination
						free ((void *)foundCardId2);
					} else {
						srand (time (0));
						sprintf (chipId2, "%032X", generate_rand ());
						addNewCard (accessCode2, chipId2);
					}
				}

				printWarning ("Scanning card: %s | ID: %s\n", accessCode2, chipId2);
				memcpy (cardData + 0x2C, chipId2, 33);
				memcpy (cardData + 0x50, accessCode2, 21);
				touchCallback (0, 0, cardData, touchData);
			}
		}
	}
	for (int i = 0; plugins[i] != 0; i++) {
		FARPROC updateEvent = GetProcAddress (plugins[i], "Update");
		if (updateEvent) ((event *)updateEvent) ();
	}
	if (attachCallback) attachCallback (0, 0, attachData);
	return coin_count;
}

u32 __stdcall bnusio_GetSwIn () {
	u32 sw = 0;
	sw |= (u32)testEnabled << 7;
	sw |= (u32)IsButtonDown (DEBUG_ENTER) << 9;
	sw |= (u32)IsButtonDown (DEBUG_DOWN) << 12;
	sw |= (u32)IsButtonDown (DEBUG_UP) << 13;
	sw |= (u32)IsButtonDown (SERVICE) << 14;
	return sw;
}

i64 __stdcall bnusio_Close () {
	for (int i = 0; plugins[i] != 0; i++) {
		FARPROC exitEvent = GetProcAddress (plugins[i], "Exit");
		if (exitEvent) ((event *)exitEvent) ();
	}
	return 0;
}

HOOK_DYNAMIC (u64, __stdcall, bngrw_Init) {
	for (int i = 0; plugins[i] != 0; i++) {
		FARPROC initEvent = GetProcAddress (plugins[i], "Init");
		if (initEvent) ((event *)initEvent) ();
	}
	return 0;
}

HOOK_DYNAMIC (u64, __stdcall, bngrw_attach, i32 a1, char *a2, i32 a3, i32 a4, callbackAttach callback, i32 *a6) {
	// This is way too fucking jank
	attachCallback = callback;
	attachData     = a6;
	return 1;
}

HOOK_DYNAMIC (i32, __stdcall, bngrw_reqWaitTouch, u32 a1, i32 a2, u32 a3, callbackTouch callback, u64 a5) {
	waitingForTouch = true;
	touchCallback   = callback;
	touchData       = a5;
	for (int i = 0; plugins[i] != 0; i++) {
		FARPROC touchEvent = GetProcAddress (plugins[i], "WaitTouch");
		if (touchEvent) ((waitTouchEvent *)touchEvent) (callback, a5);
	}
	return 1;
}

HOOK_DYNAMIC (i32, __stdcall, ws2_getaddrinfo, char *node, char *service, void *hints, void *out) {
	return originalws2_getaddrinfo (server, service, hints, out);
}

i32 __stdcall DllMain (HMODULE mod, DWORD cause, void *ctx) {
	if (cause == DLL_PROCESS_DETACH) DisposePoll ();
	if (cause != DLL_PROCESS_ATTACH) return true;

	init_boilerplate ();

	INSTALL_HOOK_DYNAMIC (bngrw_attach, PROC_ADDRESS ("bngrw.dll", "BngRwAttach"));
	INSTALL_HOOK_DYNAMIC (bngrw_reqWaitTouch, PROC_ADDRESS ("bngrw.dll", "BngRwReqWaitTouch"));
	INSTALL_HOOK_DYNAMIC (bngrw_Init, PROC_ADDRESS ("bngrw.dll", "BngRwInit"));

	INSTALL_HOOK_DYNAMIC (ws2_getaddrinfo, PROC_ADDRESS ("ws2_32.dll", "getaddrinfo"));

	// Set current directory to the directory of the executable
	// Find all files in the plugins directory that end with .dll
	// Call loadlibraryW on those files
	// Create a message box if they fail to load
	wchar_t path[MAX_PATH];
	GetModuleFileNameW (NULL, path, MAX_PATH);
	*wcsrchr (path, '\\') = '\0';
	SetCurrentDirectoryW (path);

	WIN32_FIND_DATAW fd;
	int i        = 0;
	HANDLE hFind = FindFirstFileW (L"plugins/*.dll", &fd);
	if (hFind != INVALID_HANDLE_VALUE) {
		do {
			if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
			wchar_t filePath[MAX_PATH];
			wcscpy (filePath, path);
			wcscat (filePath, L"/plugins/");
			wcscat (filePath, fd.cFileName);
			HMODULE hModule = LoadLibraryW (filePath);
			if (!hModule) {
				wchar_t buf[128];
				swprintf (buf, 128, L"Failed to load plugin %d", GetLastError ());
				MessageBoxW (NULL, buf, fd.cFileName, MB_ICONERROR);
			} else {
				plugins[i] = hModule;
				i++;
				FARPROC preInitEvent = GetProcAddress (hModule, "PreInit");
				if (preInitEvent) ((event *)preInitEvent) ();
			}
		} while (FindNextFileW (hFind, &fd));
		FindClose (hFind);
	}

	toml_table_t *config = openConfig (configPath ("config.toml"));
	if (config) {
		drumMax = readConfigInt (config, "drumMax", drumMax);
		drumMin = readConfigInt (config, "drumMin", drumMin);
		server  = readConfigString (config, "server", server);
		strncpy (accessCode1, readConfigString (config, "card1_access_code", accessCode1), sizeof (accessCode1) - 1);
		accessCode1[sizeof (accessCode1) - 1] = '\0'; // Ensure null-termination
		strncpy (accessCode2, readConfigString (config, "card2_access_code", accessCode2), sizeof (accessCode2) - 1);
		accessCode2[sizeof (accessCode2) - 1] = '\0'; // Ensure null-termination
		toml_free (config);
	}

	struct stat buffer;

	if (stat ("cards.dat", &buffer) == 0) {
		const char *foundCardId1 = lookupCardId (accessCode1);
		if (foundCardId1) {
			strncpy (chipId1, foundCardId1, sizeof (chipId1) - 1);
			chipId1[sizeof (chipId1) - 1] = '\0'; // Ensure null-termination
			free ((void *)foundCardId1);
		} else {
			srand (time (0));
			sprintf (chipId1, "%032X", generate_rand ());
			addNewCard (accessCode1, chipId1);
		}
		const char *foundCardId2 = lookupCardId (accessCode2);
		if (foundCardId2) {
			strncpy (chipId2, foundCardId2, sizeof (chipId2) - 1);
			chipId2[sizeof (chipId2) - 1] = '\0'; // Ensure null-termination
			free ((void *)foundCardId2);
		} else {
			srand (time (0));
			sprintf (chipId2, "%032X", generate_rand ());
			addNewCard (accessCode2, chipId2);
		}
	} else {
		// New file is auto-generated with 2 cards
		FILE *cards_new = fopen (configPath ("cards.dat"), "w");
		if (cards_new) {
			srand (time (0));
			sprintf (chipId1, "%032X", generate_rand ());
			fprintf (cards_new, "%s%s\n", accessCode1, chipId1);
			sprintf (chipId2, "%032X", generate_rand ());
			fprintf (cards_new, "%s%s\n", accessCode2, chipId2);
			fclose (cards_new);
		}
	}

	return true;
}
