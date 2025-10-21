#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>

#define MAX_ITERATIONS 10

int pc = 0;
int pipe_from_kernel_fd;
int pipe_to_kernel_fd;
int use_io = 0; // 0 = sem I/O, 1 = com I/O

typedef struct {
    int pc;
    char operation;
} SyscallContext;

void syscall_io(char operation) {
    pid_t kernel_pid = getppid();

    if (operation == 'R') {
        printf("  App (PID %d, PC=%d): syscall READ do disco D1\n", getpid(), pc);
    } else if (operation == 'W') {
        printf("  App (PID %d, PC=%d): syscall WRITE no disco D1\n", getpid(), pc);
    }

    SyscallContext ctx = { .pc = pc, .operation = operation };
    write(pipe_to_kernel_fd, &ctx, sizeof(SyscallContext));
    kill(kernel_pid, SIGUSR2);
}

int main(int argc, char *argv[]) {
    if (argc < 4) {
        fprintf(stderr, "Uso: app <fd_from_kernel> <fd_to_kernel> <use_io>\n");
        exit(1);
    }

    pipe_from_kernel_fd = atoi(argv[1]);
    pipe_to_kernel_fd = atoi(argv[2]);
    use_io = atoi(argv[3]); // 0 = sem IO, 1 = com IO

    fcntl(pipe_from_kernel_fd, F_SETFL, O_NONBLOCK);

    printf("App iniciado (PID %d) - IO=%d - Pipes K→A:%d / A→K:%d\n",
           getpid(), use_io, pipe_from_kernel_fd, pipe_to_kernel_fd);
    fflush(stdout);

    while (pc < MAX_ITERATIONS) {
        int restored_pc = -1;
        if (read(pipe_from_kernel_fd, &restored_pc, sizeof(int)) > 0) {
            // Restaurando contexto — não incrementa PC agora
            pc = restored_pc;
            printf("  App (PID %d): restaurando contexto (PC=%d)\n", getpid(), pc);
            fflush(stdout);
        } else {
            // Executando instrução normal
            printf("  App (PID %d): executando instrucao (PC=%d)\n", getpid(), pc);
            fflush(stdout);

            if (use_io) {
                if (pc == 5 || pc == 15) syscall_io('R');
                else if (pc == 10 || pc == 20) syscall_io('W');
            }

            pc++; // só incrementa se for instrução normal
        }

        sleep(1);
    }


    close(pipe_from_kernel_fd);
    close(pipe_to_kernel_fd);
    return 0;
}
