#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>

#define TIME_SLICE_SECONDS 1

int main(int argc, char *argv[]) {
    pid_t kernel_pid = getppid();

    printf("InterControllerSim: Iniciado. Kernel PID = %d\n", kernel_pid);
    fflush(stdout);

    while (1) {
        sleep(TIME_SLICE_SECONDS);
        kill(kernel_pid, SIGUSR1);
    }

    return 0;
}
