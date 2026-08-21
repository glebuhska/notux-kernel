bits 32

section .multiboot
align 4
    dd 0x1BADB002               ; Magic number
    dd 0x00000003               ; Flags
    dd -(0x1BADB002 + 0x00000003) ; Checksum

section .bss
align 4096
pml4_table:
    resb 4096
pdpt_table:
    resb 4096
pd_table:
    resb 16384 

align 16
stack_bottom:
    resb 16384
stack_top:

align 16
global tss64
tss64:
    resb 104

multiboot_info:
    resd 1
multiboot_magic:
    resd 1

section .text
global _start
extern kernel

_start:
    cli
    mov esp, stack_top

    mov dword [multiboot_magic], eax
    mov dword [multiboot_info], ebx
    mov eax, pdpt_table
    or eax, 0x07
    mov [pml4_table], eax

    mov eax, pd_table
    or eax, 0x07
    mov [pdpt_table], eax       ; 0 - 1 GB

    add eax, 4096
    mov [pdpt_table + 8], eax   ; 1 - 2 GB

    add eax, 4096
    mov [pdpt_table + 16], eax  ; 2 - 3 GB

    add eax, 4096
    mov [pdpt_table + 24], eax  ; 3 - 4 GB
    mov edi, pd_table
    mov ebx, 0x00000087
    mov ecx, 2048
.map_pd:
    mov dword [edi], ebx
    add ebx, 0x200000 
    add edi, 8
    loop .map_pd
    mov eax, pml4_table
    mov cr3, eax

    mov eax, cr4
    or eax, (1 << 5) | (1 << 9) | (1 << 10)
    mov cr4, eax

    mov ecx, 0xC0000080
    rdmsr
    or eax, 1 << 8
    wrmsr

    mov eax, cr0
    or eax, (1 << 31) | (1 << 0)
    mov cr0, eax

    lgdt [gdt64_pointer]

    jmp 0x08:long_mode_start

bits 64
long_mode_start:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    mov rsp, stack_top

    mov rax, stack_top
    mov [tss64 + 4], rax

    mov rax, tss64
    mov word [gdt64_tss + 2], ax        ; Base [15:0]
    shr rax, 16
    mov byte [gdt64_tss + 4], al        ; Base [23:16]
    mov byte [gdt64_tss + 7], ah        ; Base [31:24]
    shr rax, 16
    mov dword [gdt64_tss + 8], eax      ; Base [63:32]


    mov ax, 0x28
    ltr ax


    mov edi, dword [multiboot_magic]
    mov esi, dword [multiboot_info]

    call kernel

hang:
    cli
    hlt
    jmp hang

section .rodata
align 8
gdt64:
    dq 0                                ; 0x00: Null Descriptor

gdt64_code:                             ; 0x08: Kernel Code (Ring 0)
    dq (1<<43) | (1<<44) | (1<<47) | (1<<53)

gdt64_data:                             ; 0x10: Kernel Data (Ring 0)
    dq (1<<41) | (1<<44) | (1<<47)

gdt64_user_data:                        ; 0x18: User Data (Ring 3)
    dq (1<<41) | (1<<44) | (1<<47) | (3<<45)

gdt64_user_code:                        ; 0x20: User Code (Ring 3)
    dq (1<<43) | (1<<44) | (1<<47) | (1<<53) | (3<<45)

gdt64_tss:
    dw 103
    dw 0
    db 0
    db 0x89
    db 0x00
    db 0
    dd 0
    dd 0

gdt64_pointer:
    dw gdt64_pointer - gdt64 - 1
    dq gdt64