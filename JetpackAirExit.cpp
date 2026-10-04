// JetpackAirExit.asi - GTA San Andreas PC v1.0 (US)
// No ar, segurar o botao de sair (teclado ou controle, inclusive via GInput) por N segundos
// faz o CJ sair da jetpack. No chao o comportamento original continua igual (basta apertar).
//
// Tudo confirmado no gta_sa.exe 1.0 US:
//  - CTaskSimpleJetPack::ProcessControlInput = 0x67E7B0 (thiscall, ecx = task, pilha: ped).
//    Dentro dela o jogo so checa o botao de sair se o ped estiver no chao (bIsStanding,
//    bit 0 do byte em ped+0x46C). Por isso no ar o botao nao faz nada.
//  - CPad::<botao de sair, mantido> = 0x5400D0 (thiscall, ecx = CPad*). Le o mesmo mapeamento
//    do jogo (teclado e controle). O GInput escreve no CPad, entao tambem funciona com ele.
//  - Rotina de saida da jetpack = 0x67B660 (thiscall, ecx = task, pilha: ped), a mesma que o
//    jogo chama ao sair no chao.
//  - Gancho: o call em 0x67E851 (le o analogico) dentro de ProcessControlInput. Nesse ponto:
//    esi = task, edi = ped, ebp = CPad*. Um pequeno stub passa esses 3 valores para o plugin.
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

static const DWORD HOOK_SITE      = 0x67E851;   // call 0x53FD30
static const DWORD HOOK_ORIG_FN   = 0x53FD30;
static const DWORD FN_PADEXITHELD = 0x5400D0;
static const DWORD FN_EXITJETPACK = 0x67B660;
static const DWORD FN_PROCINPUT   = 0x67E7B0;
static const int   OFF_PEDFLAGS   = 0x46C;      // bit 0 = bIsStanding

typedef bool (__thiscall *PadHeld_t)(void* pad);
typedef void (__thiscall *ExitJetpack_t)(void* task, void* ped);

static HMODULE hSelf = NULL;
static char    gIni[MAX_PATH];
static bool    gLog = false;
static DWORD   gHoldMs = 3000;

static void Log(const char* fmt, ...)
{
    if (!gLog) return;
    char path[MAX_PATH];
    if (!GetModuleFileNameA(hSelf, path, MAX_PATH)) return;
    char* dot = strrchr(path, '.');
    if (dot) strcpy(dot, ".log"); else strcat(path, ".log");
    FILE* f = fopen(path, "a");
    if (!f) return;
    fprintf(f, "[%lu] ", GetTickCount());
    va_list ap; va_start(ap, fmt); vfprintf(f, fmt, ap); va_end(ap);
    fprintf(f, "\n");
    fclose(f);
}

// Chamado todo frame enquanto a jetpack esta ativa (dentro de ProcessControlInput)
static void __cdecl Process(void* task, void* ped, void* pad)
{
    static DWORD heldSince = 0;
    static DWORD lastCall  = 0;

    DWORD now = GetTickCount();
    if (now == 0) now = 1;
    if (lastCall == 0 || now - lastCall > 250) heldSince = 0;   // pausa/menu: recomeça a contagem
    lastCall = now;

    // No chao: comportamento original do jogo (sai ao apertar)
    if (*(unsigned char*)((DWORD)ped + OFF_PEDFLAGS) & 1) { heldSince = 0; return; }

    bool held = ((PadHeld_t)FN_PADEXITHELD)(pad);
    if (!held) { heldSince = 0; return; }

    if (heldSince == 0) { heldSince = now; Log("Botao de sair pressionado no ar"); return; }

    if (now - heldSince >= gHoldMs)
    {
        Log("Saindo da jetpack no ar apos %lu ms", now - heldSince);
        heldSince = 0;
        ((ExitJetpack_t)FN_EXITJETPACK)(task, ped);
    }
}

// Stub em codigo de maquina (sem assembly inline):
//   mov eax,0x53FD30 ; call eax          -> chama a funcao original (ecx = pad, retorno em eax)
//   push eax                              -> guarda o retorno
//   push ebp ; push edi ; push esi        -> pad, ped, task
//   mov eax,&Process ; call eax ; add esp,12
//   pop eax ; ret
static bool Install()
{
    // Confere o codigo do jogo antes de alterar
    if (IsBadReadPtr((void*)(HOOK_SITE - 2), 7) || IsBadReadPtr((void*)FN_PROCINPUT, 10)) return false;
    if (*(unsigned char*)HOOK_SITE != 0xE8) return false;
    if (HOOK_SITE + 5 + *(int*)(HOOK_SITE + 1) != HOOK_ORIG_FN) return false;
    static const unsigned char proc[]  = { 0x83,0xEC,0x24,0x53,0x55,0x56,0x57,0x8B,0x7C,0x24 };
    static const unsigned char padf[]  = { 0x66,0x83,0xB9,0x0E,0x01,0x00,0x00,0x00 };
    static const unsigned char exitf[] = { 0x83,0xEC,0x0C,0x55,0x56,0x8B,0xF1,0x8A,0x46,0x08 };
    if (memcmp((void*)FN_PROCINPUT,   proc,  sizeof proc)  != 0) return false;
    if (memcmp((void*)FN_PADEXITHELD, padf,  sizeof padf)  != 0) return false;
    if (memcmp((void*)FN_EXITJETPACK, exitf, sizeof exitf) != 0) return false;

    unsigned char* stub = (unsigned char*)VirtualAlloc(NULL, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!stub) return false;
    unsigned char* p = stub;
    *p++ = 0xB8; *(DWORD*)p = HOOK_ORIG_FN; p += 4;          // mov eax, orig
    *p++ = 0xFF; *p++ = 0xD0;                                // call eax
    *p++ = 0x50;                                             // push eax
    *p++ = 0x55;                                             // push ebp (pad)
    *p++ = 0x57;                                             // push edi (ped)
    *p++ = 0x56;                                             // push esi (task)
    *p++ = 0xB8; *(DWORD*)p = (DWORD)&Process; p += 4;       // mov eax, Process
    *p++ = 0xFF; *p++ = 0xD0;                                // call eax
    *p++ = 0x83; *p++ = 0xC4; *p++ = 0x0C;                   // add esp, 12
    *p++ = 0x58;                                             // pop eax
    *p++ = 0xC3;                                             // ret
    FlushInstructionCache(GetCurrentProcess(), stub, 64);

    DWORD old;
    if (!VirtualProtect((void*)(HOOK_SITE + 1), 4, PAGE_EXECUTE_READWRITE, &old)) return false;
    *(int*)(HOOK_SITE + 1) = (int)((DWORD)stub - (HOOK_SITE + 5));
    VirtualProtect((void*)(HOOK_SITE + 1), 4, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void*)HOOK_SITE, 5);
    return true;
}

static void Init()
{
    gLog = GetPrivateProfileIntA("Main", "Log", 0, gIni) != 0;
    if (!GetPrivateProfileIntA("Main", "Enable", 1, gIni)) { Log("Desativado no .ini"); return; }

    char buf[32];
    GetPrivateProfileStringA("Jetpack", "HoldSeconds", "3.0", buf, sizeof buf, gIni);
    double sec = atof(buf);
    if (sec < 0.0) sec = 0.0;
    if (sec > 60.0) sec = 60.0;
    gHoldMs = (DWORD)(sec * 1000.0 + 0.5);

    if (!Install()) { Log("Codigo do jogo diferente do esperado (nao e o SA 1.0 US?) - mod desativado"); return; }
    Log("Ativo: segurar %lu ms no ar", gHoldMs);
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        hSelf = hModule;
        GetModuleFileNameA(hModule, gIni, MAX_PATH);
        char* dot = strrchr(gIni, '.');
        if (dot) strcpy(dot, ".ini"); else strcat(gIni, ".ini");
        Init();
    }
    return TRUE;
}
