// dllmain.cpp : Defines the entry point for the DLL application.
#include "pch.h"

BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
                     )
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}


void blackbox_function(double a, double b, double c, double d, double* out) {
    // Example: very simple linear weights (these should be replaced with your trained model)
    out[0] = 1000 + 10 * a + 5 * b - 3 * c + 2 * d;
    out[1] = 1000 + 10 * a - 5 * b + 3 * c - 2 * d;
    out[2] = 1000 + 10 * a + 5 * b + 3 * c - 2 * d;
    out[3] = 1000 + 10 * a - 5 * b - 3 * c + 2 * d;
}
