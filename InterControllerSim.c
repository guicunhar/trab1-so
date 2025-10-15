#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>   // getppid(), sleep()
#include <signal.h>   // kill(), SIGUSR1, SIGUSR2
#include <sys/types.h>

int main() {
    pid_t kernel_pid = getppid();
    int counter = 0;

    while (1) {
        sleep(1); 
        kill(kernel_pid, SIGUSR1);

        counter++;
        if (counter % 3 == 0) {
            kill(kernel_pid, SIGUSR2); 
        }
    }

    return 0;
}
