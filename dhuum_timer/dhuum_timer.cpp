/*
* dhuum timer
* based on arcdps api sample: https://www.deltaconnected.com/arcdps/api/arcdps_combatdemo.cpp
*/

#include "dhuum_timer.h"
#include <stdio.h>

/* dll main -- winapi */
BOOL APIENTRY DllMain(HANDLE hModule, DWORD ulReasonForCall, LPVOID lpReserved) {
	switch (ulReasonForCall) {
	case DLL_PROCESS_ATTACH: dll_init(hModule); break;
	case DLL_PROCESS_DETACH: dll_exit(); break;

	case DLL_THREAD_ATTACH:  break;
	case DLL_THREAD_DETACH:  break;
	}
	return 1;
}

/* dll attach -- from winapi */
void dll_init(HANDLE hModule) {
	return;
}

/* dll detach -- from winapi */
void dll_exit() {
	return;
}

/* log to arcdps.log, thread/async safe */
void log_file(char* str) {
	size_t(*log)(char*) = (size_t(*)(char*))filelog;
	if (log) (*log)(str);
	return;
}

/* log to extensions tab in arcdps log window, thread/async safe */
void log_arc(char* str) {
	size_t(*log)(char*) = (size_t(*)(char*))arclog;
	if (log) (*log)(str);
	return;
}

/* export -- arcdps looks for this exported function and calls the address it returns on client load */
extern "C" __declspec(dllexport) void* get_init_addr(char* arcversion,
	ImGuiContext* imguictx, void* id3dptr, HANDLE arcdll, void* mallocfn,
	void* freefn, uint32_t d3dversion) {
	// id3dptr is IDirect3D9* if d3dversion==9, or IDXGISwapChain* if d3dversion==11
	arcvers = arcversion;
	filelog = (void*)GetProcAddress((HMODULE)arcdll, "e3");
	arclog = (void*)GetProcAddress((HMODULE)arcdll, "e8");
	ImGui::SetCurrentContext((ImGuiContext*)imguictx);
	ImGui::SetAllocatorFunctions((void *(*)(size_t, void*))mallocfn, (void (*)(void*, void*))freefn); // on imgui 1.80+
	return mod_init;
}

/* export -- arcdps looks for this exported function and calls the address it returns */
extern "C" __declspec(dllexport) void* get_release_addr() {
	arcvers = 0;
	return mod_release;
}

/* initialize mod -- return table that arcdps will use for callbacks */
arcdps_exports* mod_init() {
#if LOGGING_ENABLED
	logging = new arc_logging(arcvers);
#endif

	/* globals */
	in_hoc = false;
	dhuum_present = false;
	entered_combat = false;
	self_dead = false;
	encounter_start_time = 0;

	current_green_circle = 0;

	/* Time Array for convenience to match with symbols */
	for (int i = 0; i < 19; ++i) {
		green_circle_times.push_back(570000 - 30000 * i);
	}

	//Symbols
	std::vector<std::string> symbol_names = { "Arrow", "Circle", "Heart", "Square", "Star", "Spiral", "Triangle", "Arrow", "Circle", "Heart", "Square", "Star", "Spiral", "Triangle", "Arrow", "Circle", "Heart", "Square", "Star" };
	for (int i = 0; i < symbol_names.size(); ++i) {
		green_circle_symbols.push_back(symbol_names[i]);
	}

	//Greater Death Marks
	current_greater_death_mark = 0;
	for (int i = 0; i < GREATER_DEATH_MARK_EVENTS; ++i) {
		greater_death_mark_times.push_back(385000 - 80000 * i);
	}

	/* for arcdps */
	memset(&arc_exports, 0, sizeof(arcdps_exports));
	arc_exports.size = sizeof(arcdps_exports);
	arc_exports.sig = 0x92345678;
	arc_exports.imguivers = IMGUI_VERSION_NUM;
	arc_exports.out_name = "dhuum timer";
	arc_exports.out_build = "0.2";
	arc_exports.wnd_nofilter = mod_wnd;
	arc_exports.combat = mod_combat;
	arc_exports.imgui = mod_imgui;
	return &arc_exports;
}

/* release mod -- return ignored */
uintptr_t mod_release() {
#if LOGGING_ENABLED
	if (logging) {
		delete logging;
	}
#endif

	return 0;
}

/* window callback -- return is assigned to umsg (return zero to not be processed by arcdps or game) */
uintptr_t mod_wnd(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	return uMsg;
}

//Dhuum (ce1a580a125f3510:313, 4bfa:ffffffff)
//Deathly Aura:48238

/* combat callback -- may be called asynchronously. return ignored */
/* one participant will be party/squad, or minion of. no spawn statechange events. despawn statechange only on marked boss npcs */
uintptr_t mod_combat(cbtevent* ev, ag* src, ag* dst, char* skillname, uint64_t id, uint64_t revision)
{
#if LOGGING_ENABLED
	if (logging) {
		logging->on_combat(ev, src, dst, skillname);
	}
#endif

	//Skip processing combat events if we are not in the Hall of Chains
	if (!in_hoc) return uintptr_t();

	/* ev is null. dst will only be valid on tracking add. skillname will also be null */
	if (ev && src) {
		if (!src->name || !strlen(src->name)) src->name = "(area)";
		if (!dst->name || !strlen(dst->name)) dst->name = "(area)";

		if (!dhuum_present) {
			/* default names */
			if (!src->name || !strlen(src->name)) src->name = "(area)";
			if (!dst->name || !strlen(dst->name)) dst->name = "(area)";

			if (src->prof == 0x4bfa && strcmp(src->name, "Dhuum") == 0) {
				dhuum_present = true;
			}
		}

		/* Enter Combat */
		else if (src->self) {
			if (ev->is_statechange == CBTS_ENTERCOMBAT) {
				entered_combat = true;
				encounter_start_time = ev->time;
			}

			else if (ev->is_statechange == CBTS_CHANGEDEAD) {
				self_dead = true;
			}

			else if (self_dead && ev->buff == 1 && ev->skillid == 848) {
				mod_reset();
			}
		}
	}

	return 0;
}

void mod_green_circles(const uint32_t timer) {
	if (current_green_circle < green_circle_times.size()) {

		uint32_t time_to = (timer - green_circle_times[current_green_circle]) / 1000;

		if (time_to <= 0) {
			current_green_circle++;
			return;
		}

		uint32_t player = current_green_circle % 3;

		ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.f), "%lus to ", time_to);
		ImGui::SameLine();
		ImGui::TextColored(ImVec4(0.f, 1.f, 1.f, 1.f), "%s", green_circle_symbols[current_green_circle].c_str());
		ImGui::SameLine();
		ImGui::TextColored(ImVec4(0.f, 1.f, 0.f, 1.f), " (Player %d)", player + 1);
	}
}

void mod_greater_death_marks(const uint32_t timer) {
	if (current_greater_death_mark < greater_death_mark_times.size()) {
		uint32_t time_to = (timer - greater_death_mark_times[current_greater_death_mark]) / 1000;

		if (time_to <= 0) {
			current_greater_death_mark++;
			return;
		}

		else if (time_to <= 30) {
			ImGui::Separator();
			ImGui::TextColored(ImVec4(1.0f, 0.f, 0.f, 1.0f), "%lus to Greater Death Mark", time_to);
		}
	}
}

void mod_preevent(const uint32_t timer) {
	uint32_t time_to = (timer - 480000) / 1000;

	if (time_to <= 0) {
		return;
	}

	if (time_to < 90) {
		ImGui::Text("");
		ImGui::Text("Pre-event phase ends in %lus", time_to);
	}
}

uintptr_t mod_imgui() {
	/* Poll for the MumbleLink shared memory file since it isn't always (never?) available during mod_init */
	if (lm == NULL) {
		HANDLE hMapObject = OpenFileMappingW(FILE_MAP_READ, FALSE, L"MumbleLink");
		if (hMapObject != NULL) {
			lm = (LinkedMem *)MapViewOfFile(hMapObject, FILE_MAP_READ, 0, 0, sizeof(LinkedMem));
			if (lm == NULL) {
				CloseHandle(hMapObject);
				hMapObject = NULL;
			}
			else {
				mc = (MumbleContext*)lm->context;
				log_arc("MumbleLink Shared Memory Found\n");
			}
		}
	}

	if (mc != NULL) {
		if (mc->mapId == 1264) {
			in_hoc = true;
			/* Present window only after Dhuum enters tracking range */
			if (dhuum_present) {

				if (!ImGui::Begin("Dhuum Timer", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
					ImGui::End();
					return uintptr_t();
				}

				if (entered_combat) {

					uint64_t current = timeGetTime();
					uint32_t elapsed = (uint32_t)(current - encounter_start_time);
					uint32_t timer = 600000 - elapsed;

					mod_green_circles(timer);

					mod_greater_death_marks(timer);

					mod_preevent(timer);
				}

				else {
					ImGui::Text("Ready, waiting for enter combat...");
				}

				ImGui::End();
			}
		}
		else {
			if (in_hoc) {
				mod_reset();
			}
			in_hoc = false;
		}
	}
	
	return 0;
}

void mod_reset() {
	entered_combat = false;
	dhuum_present = false;
	self_dead = false;

	encounter_start_time = 0;
	current_green_circle = 0;
	current_greater_death_mark = 0;
}