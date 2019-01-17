  SECTION .text
  bits 64
  global _elfvm_run

BREAKPOINT_NOPS EQU 6

; int32_t* reg, int32_t* pc, uint8_t** code, int32 end
; rcx           rdx->r11     r8              r9
_elfvm_run:
  push rbp
  mov rbp, rsp
  sub rsp, 8*(4+2)

  push r12
  push r13
  push r14
  push r15
  push rdx ; pc output ptr

  movsx rax, dword [rdx]               ; read PC
  mov rax, qword [r8 + rax*8]          ; resolve to x86 code addr
  add rax, BREAKPOINT_NOPS             ; skip breakpoint bytes

  mov r10d, dword [rcx+0*4]
  mov r11d, dword [rcx+1*4]
  mov r12d, dword [rcx+2*4]
  mov r13d, dword [rcx+3*4]
  mov r14d, dword [rcx+4*4]
  mov r15d, dword [rcx+5*4]

  call rax
  ; eax is -1 on halt, else PC breakpoint

  mov dword [rcx+0*4], r10d
  mov dword [rcx+1*4], r11d
  mov dword [rcx+2*4], r12d
  mov dword [rcx+3*4], r13d
  mov dword [rcx+4*4], r14d
  mov dword [rcx+5*4], r15d

  pop rdx
  pop r15
  pop r14
  pop r13
  pop r12

  mov dword [rdx], eax				   ; store PC/halt indicator in *pc

  mov rsp, rbp
  pop rbp
  ret


; translation scratch-pad
unused:
  xor r8d,r8d
  xor r9d,r9d
  xor r10d,r10d
  xor r11d,r11d
  xor r12d,r12d
  xor r13d,r13d
  xor r14d,r14d
  xor r15d,r15d

  mov dword [rcx+4], 0x12341234
  mov eax, dword [rcx+4]
  mov rax, 0x1122334455667788
  jmp rax
  or eax, dword [rcx+8]
  cmp eax, 0x44556655
  mov eax, 0x77887788
  sete al
  movzx eax, al
  cmp eax, 0x44554455
  jl .willJmp
  xor eax, eax
  dec eax
  ret
.willJmp:
  inc eax
  mov eax,eax
  mov rax, qword [r8+rax*8+8]
  jmp rax
