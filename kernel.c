#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>

#define IO_DURATION_SECONDS 3

typedef enum { READY, RUNNING, BLOCKED } ProcessState;

typedef struct {
    pid_t pid;
    ProcessState state;
    int io_pending;
    int io_timer;
} PCB;

int num_apps = 0;
PCB *pcb_table = NULL;
pid_t controller_pid;
int current_running = -1;

void schedule() {
    int next = -1;

    for (int i = (current_running + 1) % num_apps; ; i = (i + 1) % num_apps) {
        if (pcb_table[i].state == READY) {
            next = i;
            break;
        }
        if (i == current_running) break;
    }

    if (next == -1) {
        printf("KERNEL: Nenhum processo READY, aguardando...\n");
        fflush(stdout);
        return;
    }

    if (current_running != -1 && pcb_table[current_running].state == RUNNING) {
        printf("KERNEL: Preemptando processo A%d (PID %d)\n", current_running, pcb_table[current_running].pid);
        fflush(stdout);
        kill(pcb_table[current_running].pid, SIGSTOP);
        pcb_table[current_running].state = READY;
    }

    current_running = next;
    pcb_table[current_running].state = RUNNING;
    printf("KERNEL: Executando processo A%d (PID %d)\n", current_running, pcb_table[current_running].pid);
    fflush(stdout);
    kill(pcb_table[current_running].pid, SIGCONT);
}

void handle_irq0(int sig) {
    printf("\nKERNEL: IRQ0 (fim do time slice)\n");
    fflush(stdout);

    for (int i = 0; i < num_apps; i++) {
        if (pcb_table[i].io_pending) {
            pcb_table[i].io_timer--;
            if (pcb_table[i].io_timer <= 0) {
                printf("KERNEL: I/O do processo A%d (PID %d) concluído\n", i, pcb_table[i].pid);
                fflush(stdout);
                pcb_table[i].state = READY;
                pcb_table[i].io_pending = 0;
            }
        }
    }

    schedule();
}

void handle_irq1(int sig) {
    printf("KERNEL: IRQ1 (syscall de I/O) do processo A%d (PID %d)\n", current_running, pcb_table[current_running].pid);
    fflush(stdout);

    kill(pcb_table[current_running].pid, SIGSTOP);
    pcb_table[current_running].state = BLOCKED;
    pcb_table[current_running].io_pending = 1;
    pcb_table[current_running].io_timer = IO_DURATION_SECONDS;

    schedule();
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Uso: %s <num_apps>\n", argv[0]);
        printf("  onde num_apps está entre 3 e 6\n");
        exit(1);
    }

    num_apps = atoi(argv[1]);
    if (num_apps < 3 || num_apps > 6) {
        printf("ERRO: num_apps deve estar entre 3 e 6\n");
        exit(1);
    }

    pcb_table = malloc(num_apps * sizeof(PCB));

    printf("KERNEL: Criando %d processos de aplicacao...\n", num_apps);
    fflush(stdout);

    for (int i = 0; i < num_apps; i++) {
        pid_t pid = fork();
        if (pid == 0) {
            execl("./app", "app", NULL);
            perror("execl");
            exit(1);
        }

        pcb_table[i].pid = pid;
        pcb_table[i].state = READY;
        pcb_table[i].io_pending = 0;
        pcb_table[i].io_timer = 0;

        printf("KERNEL: Processo A%d criado (PID %d)\n", i, pid);
        fflush(stdout);
    }

    sleep(1);

    printf("KERNEL: Parando todos os apps inicialmente...\n");
    fflush(stdout);
    for (int i = 0; i < num_apps; i++) {
        kill(pcb_table[i].pid, SIGSTOP);
    }

    signal(SIGUSR1, handle_irq0);
    signal(SIGUSR2, handle_irq1);

    printf("KERNEL: Criando InterControllerSim...\n");
    fflush(stdout);
    controller_pid = fork();
    if (controller_pid == 0) {
        execl("./InterControllerSim", "InterControllerSim", NULL);
        perror("execl");
        exit(1);
    }

    sleep(1);

    printf("KERNEL: Iniciando escalonamento...\n");
    fflush(stdout);
    schedule();

    while (1) {
        pause();
    }

    free(pcb_table);
    return 0;
}
