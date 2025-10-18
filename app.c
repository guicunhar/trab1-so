#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>

int pc = 0;

void syscall_io(char operation) {
    pid_t kernel_pid = getppid();

    if (operation == 'R') {
        printf("  App (PID %d, PC=%d): syscall READ do disco D1\n", getpid(), pc);
        kill(kernel_pid, SIGUSR2);
    } else if (operation == 'W') {
        printf("  App (PID %d, PC=%d): syscall WRITE no disco D1\n", getpid(), pc);
        kill(kernel_pid, SIGUSR2);
    }
}

int main() {
    printf("App iniciado (PID %d)\n", getpid());

    while (1) {
        printf("  App (PID %d): executando instrucao (PC=%d)\n", getpid(), pc);
        fflush(stdout);

        if (pc == 5) {
            syscall_io('R');
        } else if (pc == 10) {
            syscall_io('W');
        }

        pc++;
        sleep(1);
    }

    return 0;
}
