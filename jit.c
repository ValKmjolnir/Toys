#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>

const unsigned char code[]={
    0x55,               //	pushq	%rbp
    0x48,0x89,0xe5,     //	movq	%rsp, %rbp
    0x48,0x89,0x7d,0xf8,//	movq	%rdi, -8(%rbp)
    0x48,0x89,0x75,0xf0,//	movq	%rsi, -16(%rbp)
    0x48,0x8b,0x45,0xf8,//	movq	-8(%rbp), %rax
    0x48,0x03,0x45,0xf0,//	addq	-16(%rbp), %rax
    0x5d,               //	popq	%rbp
    0xc3                //  retq
};

typedef long (*func)(long,long);

// use gcc jit.c -S to get assembly
long add(long a,long b){
    return a+b;
}
/** assembly
_add:                                   ## @add
	.cfi_startproc
## %bb.0:
	pushq	%rbp
	.cfi_def_cfa_offset 16
	.cfi_offset %rbp, -16
	movq	%rsp, %rbp
	.cfi_def_cfa_register %rbp
	movq	%rdi, -8(%rbp)
	movq	%rsi, -16(%rbp)
	movq	-8(%rbp), %rax
	addq	-16(%rbp), %rax
	popq	%rbp
	retq
	.cfi_endproc
*/

/** objdump -S jit
0000000100003e60 <_add>:
100003e60: 55                          	pushq	%rbp
100003e61: 48 89 e5                    	movq	%rsp, %rbp
100003e64: 48 89 7d f8                 	movq	%rdi, -8(%rbp)
100003e68: 48 89 75 f0                 	movq	%rsi, -16(%rbp)
100003e6c: 48 8b 45 f8                 	movq	-8(%rbp), %rax
100003e70: 48 03 45 f0                 	addq	-16(%rbp), %rax
100003e74: 5d                          	popq	%rbp
100003e75: c3                          	retq
100003e76: 66 2e 0f 1f 84 00 00 00 00 00       	nopw	%cs:(%rax,%rax)
*/

func exec_malloc(){
    char* memory=(char*)mmap(
        NULL,
        4096,
        PROT_READ|PROT_WRITE|PROT_EXEC,
        MAP_PRIVATE|MAP_ANONYMOUS,
        -1,
        0);
    if(!memory){
        perror("failed to allocate memory");
        exit(-1);
    }
    for(unsigned int i=0;i<22;++i)
        memory[i]=code[i];
    return (func)memory;
}

void exec_free(func ptr){
    munmap(ptr,4096);
    return;
}

int main(){
    func ptr=exec_malloc();
    printf("%ld\n",ptr(1,2));
    exec_free(ptr);
    return 0;
}