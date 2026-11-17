OPTION CASEMAP:NONE

EXTERN DwmForwardSlots:QWORD
EXTERN ResolveDwmExport:PROC

.code

DwmDispatch PROC
    lea r11, DwmForwardSlots
    mov rax, [r11+r10*8]
    test rax, rax
    jz DwmResolveSlow
    jmp rax
DwmDispatch ENDP

DwmResolveSlow PROC FRAME
    sub rsp, 0a8h
    .allocstack 0a8h
    .endprolog
    mov [rsp+20h], rcx
    mov [rsp+28h], rdx
    mov [rsp+30h], r8
    mov [rsp+38h], r9
    movaps [rsp+40h], xmm0
    movaps [rsp+50h], xmm1
    movaps [rsp+60h], xmm2
    movaps [rsp+70h], xmm3
    movaps [rsp+80h], xmm4
    movaps [rsp+90h], xmm5
    mov ecx, r10d
    call ResolveDwmExport
    movaps xmm5, [rsp+90h]
    movaps xmm4, [rsp+80h]
    movaps xmm3, [rsp+70h]
    movaps xmm2, [rsp+60h]
    movaps xmm1, [rsp+50h]
    movaps xmm0, [rsp+40h]
    mov r9, [rsp+38h]
    mov r8, [rsp+30h]
    mov rdx, [rsp+28h]
    mov rcx, [rsp+20h]
    test rax, rax
    jz missing_export
    add rsp, 0a8h
    jmp rax
missing_export:
    mov eax, 8007007fh
    add rsp, 0a8h
    ret
DwmResolveSlow ENDP

INCLUDE DwmApiStubs.inc

END
