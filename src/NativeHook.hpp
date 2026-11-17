#pragma once
#include <MinHook.h>

extern "C" MH_STATUS WINAPI ResonanceCreateAbsoluteHook(LPVOID target, LPVOID detour, LPVOID* original,
    const UINT8* offsets, UINT count);
