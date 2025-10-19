#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>

#define MAX_ITERATIONS 30

int pc = 0;
int kernel_pipe_fd; // <<< NOVO: FD do pipe para escrever para o kernel

// <<< NOVO: Esta struct deve ser IDÊNTICA à do kernel.c
typedef struct {
    int pc;
    char operation;
} SyscallContext;


// <<< MODIFICADO: Esta é a função que faz a syscall
void syscall_io(char operation) {
    pid_t kernel_pid = getppid();

    if (operation == 'R') {
        printf("  App (PID %d, PC=%d): syscall READ do disco D1\n", getpid(), pc);
    } else if (operation == 'W') {
        printf("  App (PID %d, PC=%d): syscall WRITE no disco D1\n", getpid(), pc);
    }
    
    // --- INÍCIO DO ENVIO DE CONTEXTO ---
    SyscallContext ctx; // <<< NOVO
    ctx.pc = pc; // <<< NOVO: Prepara o contexto
    ctx.operation = operation; // <<< NOVO

    // <<< NOVO: Escreve o contexto no pipe ANTES de sinalizar o kernel
    write(kernel_pipe_fd, &ctx, sizeof(SyscallContext));
    // --- FIM DO ENVIO DE CONTEXTO ---

    // Sinaliza o kernel (agora o kernel sabe que pode ler o pipe)
    kill(kernel_pid, SIGUSR2);
}

// <<< MODIFICADO: Main agora recebe o FD do pipe como argumento
int main(int argc, char *argv[]) {

    // <<< NOVO: Lê o FD do pipe passado pelo kernel via exec
    if (argc < 2) {
        fprintf(stderr, "App (PID %d): ERRO - FD do pipe não recebido.\n", getpid());
        exit(1);
    }
    kernel_pipe_fd = atoi(argv[1]); // Converte o argumento string para int

    printf("App iniciado (PID %d) - Pipe para kernel: %d\n", getpid(), kernel_pipe_fd);
    fflush(stdout);

     while(pc < MAX_ITERATIONS) {
        printf("  App (PID %d): executando instrucao (PC=%d)\n", getpid(), pc);
        fflush(stdout);

        if (pc == 5 || pc == 15) {
            syscall_io('R');
        } else if (pc == 10 || pc == 20) {
            syscall_io('W');
        }

        pc++;
        sleep(1);
    }

    close(kernel_pipe_fd); // <<< NOVO: Fecha o pipe ao terminar (boa prática)
    return 0;
}