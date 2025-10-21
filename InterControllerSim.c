#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>

#define TIME_SLICE_SECONDS 1
#define IO_DURATION_SECONDS 3

pid_t kernel_pid;

// 🔔 Handler: chamado quando o kernel sinaliza que começou uma operação de I/O
void handle_io_request(int sig) {
    printf("InterControllerSim: pedido de I/O recebido, gerando IRQ1 em %d segundos...\n", IO_DURATION_SECONDS);
    fflush(stdout);

    // simula o tempo que o dispositivo leva pra terminar o I/O
    sleep(IO_DURATION_SECONDS);

    // envia IRQ1 (SIGALRM) de volta ao kernel, avisando que terminou
    kill(kernel_pid, SIGALRM);
    printf("InterControllerSim: IRQ1 enviado ao kernel.\n");
    fflush(stdout);
}

int main(int argc, char *argv[]) {
    kernel_pid = getppid();  // o PID do kernel é o processo pai
    printf("InterControllerSim: Iniciado. Kernel PID = %d\n", kernel_pid);
    fflush(stdout);

    // reage quando o kernel pedir I/O
    signal(SIGUSR2, handle_io_request);

    while (1) {
        // IRQ0 — clock (a cada 1 segundo)
        sleep(TIME_SLICE_SECONDS);
        kill(kernel_pid, SIGUSR1);
    }

    return 0;
}
