#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>

#define NUM_APPS 10
pid_t app_pids[NUM_APPS];
pid_t controller_pid;
int current_app_index = 0;

void scheduler_handler(int signum) {
    printf("\nKernel: Trocando de processo \n");

    kill(app_pids[current_app_index], SIGSTOP);

    current_app_index = (current_app_index + 1) % NUM_APPS;

    printf("Kernel: Próximo App a ser executado:PID %d \n\n", app_pids[current_app_index]);
    kill(app_pids[current_app_index], SIGCONT);
}


int main() {
    printf("Kernel: Criando processos de aplicação...\n");

    for (int i = 0; i < NUM_APPS; i++) {
        app_pids[i] = fork();
        if (app_pids[i] == 0) {
            execvp("./app", NULL);
        }
    }

    sleep(1); 
    printf("Kernel: Parando todos os apps para iniciar o escalonamento...\n");
    for (int i = 0; i < NUM_APPS; i++) {
        kill(app_pids[i], SIGSTOP);
    }

    signal(SIGUSR1, scheduler_handler);

    printf("Kernel: Criando o processo InterControllerSim\n");
    controller_pid = fork();
    if (controller_pid == 0) {
        execvp("./InterControllerSim", NULL);
    }

    printf("\nKernel: Iniciando o primeiro processo (PID %d).\n", app_pids[0]);
    kill(app_pids[0], SIGCONT);

    printf("Kernel: Entrando em modo de espera por interrupções...\n\n");
    while(1) {
        pause(); 
    }

    return 0;
}