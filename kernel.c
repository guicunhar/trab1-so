#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>

int main() {
    pid_t pid_filho;

    printf("Kernel: Vou criar o processo de aplicação A1...\n");

    pid_filho = fork();

    if (pid_filho == 0) {
        char *args[] = {"./app", NULL};
        execvp(args[0], args);

    } else if (pid_filho > 0) {
        printf("Kernel: App A1 criado com PID %d. Vou deixá-lo rodar por 3 segundos.\n", pid_filho);
        sleep(3);

        printf("\nKernel: Tempo esgotado! Enviando SIGSTOP para A1.\n");
        kill(pid_filho, SIGSTOP);
        sleep(3);

        printf("\nKernel: Reativando A1. Enviando SIGCONT.\n");
        kill(pid_filho, SIGCONT);
        sleep(3);

        printf("\nKernel: Encerrando a simulação. Enviando SIGKILL para A1.\n");
        kill(pid_filho, SIGKILL); 
        wait(NULL); 
        printf("Kernel: Simulação terminada.\n");
    }

    return 0;
}