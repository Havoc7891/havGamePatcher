// SPDX-License-Identifier: MIT
#ifndef HAVGAMEPATCHER_FRAME_FAILURE_HOOKS_HPP
#define HAVGAMEPATCHER_FRAME_FAILURE_HOOKS_HPP

extern "C" __declspec(naked) void EndSceneFailureHook()
{
  __asm {
    push 0
    push 0
    pushfd
    pushad
    add dword ptr [esp+12],8
    cld
    mov ebp,esp
    sub esp,528
    and esp,-16
    fxsave [esp]
    mov eax,dword ptr [config+8]
    mov dword ptr [esp+512],eax
    push ebp
    call TryAbortQuadFrame
    add esp,4
    test eax,eax
    jnz Aborted
    mov eax,dword ptr [esp+512]
    lea ecx,[eax+recoveryEndSceneFatalResume]
    mov dword ptr [ebp+36],ecx
    lea ecx,[eax+recoveryEndSceneFatalMessage]
    mov dword ptr [ebp+40],ecx
    fxrstor [esp]
    mov esp,ebp
    popad
    popfd
    ret
  Aborted:
    mov eax,dword ptr [esp+512]
    lea ecx,[eax+recoveryFailureNativeReturn]
    mov dword ptr [ebp+40],ecx
    fxrstor [esp]
    mov esp,ebp
    popad
    popfd
    lea esp,[esp+4]
    ret
  }
}

extern "C" __declspec(naked) void RawSurfaceLockResultHook()
{
  __asm {
    push 0
    pushfd
    pushad
    mov ebp,esp
    mov eax,dword ptr [config+8]
    add eax,recoveryLockResultResume
    mov dword ptr [ebp+36],eax
    cld
    sub esp,528
    and esp,-16
    fxsave [esp]
    push ebp
    call RecordWorldRelockResult
    add esp,4
    fxrstor [esp]
    mov esp,ebp
    popad
    popfd
    cmp eax,1
    sbb eax,eax
    ret
  }
}

#endif
