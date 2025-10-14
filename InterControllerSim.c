#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <stdlib.h>

int main() {
    pid_t kernel_pid = getppid();

    while(1) {
        sleep(1); // (Timeslice)
        kill(kernel_pid, SIGUSR1); 
    }

    return 0;
}