#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <string.h>

#define MAX_PROCESSES 6
#define IO_DURATION_SECONDS 3

// --- Estruturas de fila de bloqueados ---
int blocked_queue[MAX_PROCESSES];
int blocked_front = 0;
int blocked_rear = 0;
int io_in_progress = 0;

void enqueue_blocked(int pid_index) {
    blocked_queue[blocked_rear] = pid_index;
    blocked_rear = (blocked_rear + 1) % MAX_PROCESSES;
}

int dequeue_blocked() {
    if (blocked_front == blocked_rear)
        return -1;
    int pid_index = blocked_queue[blocked_front];
    blocked_front = (blocked_front + 1) % MAX_PROCESSES;
    return pid_index;
}

int blocked_is_empty() {
    return blocked_front == blocked_rear;
}

// --- Tipos e estruturas ---
typedef enum { READY, RUNNING, BLOCKED } ProcessState;

typedef struct {
    int pc;
    char operation;
} SyscallContext;

typedef struct {
    pid_t pid;
    ProcessState state;
    int io_pending;
    int io_timer;
    int pipe_read_fd;   // lê do app (App → Kernel)
    int pipe_write_fd;  // escreve para app (Kernel → App)
    int saved_pc;
    char syscall_param;
    int saved_pc_valid; // <<< NOVO: indica se há PC a restaurar
} PCB;

int num_apps = 0;
PCB *pcb_table = NULL;
pid_t controller_pid;
int current_running = -1;

// --- Protótipos ---
void schedule();

// --- IRQ0: Fim do timeslice ---
void handle_irq0(int sig) {
    printf("\nKERNEL: IRQ0 (fim do time slice)\n");
    fflush(stdout);
    schedule();
}

// --- IRQ2: Syscall recebida do App ---
void handle_syscall_from_app(int sig) {
    printf("KERNEL: Syscall de I/O do processo A%d (PID %d)\n",
           current_running, pcb_table[current_running].pid);
    fflush(stdout);

    SyscallContext ctx;
    int fd = pcb_table[current_running].pipe_read_fd;
    if (read(fd, &ctx, sizeof(SyscallContext)) > 0) {
        pcb_table[current_running].saved_pc = ctx.pc;
        pcb_table[current_running].syscall_param = ctx.operation;
        pcb_table[current_running].saved_pc_valid = 1; // <<< NOVO
        printf("KERNEL: Contexto salvo: PC=%d, OP=%c\n",
               pcb_table[current_running].saved_pc,
               pcb_table[current_running].syscall_param);
        fflush(stdout);
    } else {
        printf("KERNEL: ERRO ao ler pipe do app A%d\n", current_running);
        fflush(stdout);
    }

    kill(pcb_table[current_running].pid, SIGSTOP);
    pcb_table[current_running].state = BLOCKED;
    pcb_table[current_running].io_pending = 1;

    enqueue_blocked(current_running);

    printf("Fila de bloqueados: [");
    for (int i = blocked_front; i != blocked_rear; i = (i + 1) % MAX_PROCESSES)
        printf(" A%d ", blocked_queue[i]);
    printf("]\n");
    fflush(stdout);

    if (!io_in_progress) {
        int first = dequeue_blocked();
        if (first != -1) {
            io_in_progress = 1;
            pcb_table[first].io_pending = 1;
            printf("KERNEL: Iniciando I/O de A%d (PID %d)\n",
                   first, pcb_table[first].pid);
            kill(controller_pid, SIGUSR2);
        }
    }

    schedule();
}

// --- IRQ1: I/O concluída ---
void handle_io_complete(int sig) {
    printf("\nKERNEL: IRQ1 (I/O concluída) recebido do InterControllerSim\n");
    printf("KERNEL: Fila de bloqueados após dequeue: [");
    for (int i = blocked_front; i != blocked_rear; i = (i + 1) % MAX_PROCESSES)
        printf(" A%d ", blocked_queue[i]);
    printf("]\n");
    fflush(stdout);

    io_in_progress = 0;

    // desbloqueia o primeiro processo bloqueado
    for (int i = 0; i < num_apps; i++) {
        if (pcb_table[i].state == BLOCKED && pcb_table[i].io_pending) {
            pcb_table[i].state = READY;
            pcb_table[i].io_pending = 0;
            printf("KERNEL: Processo A%d (PID %d) desbloqueado\n",
                   i, pcb_table[i].pid);
            fflush(stdout);
            break;
        }
    }

    // inicia próxima I/O, se houver
    if (!blocked_is_empty()) {
        int next = dequeue_blocked();
        io_in_progress = 1;
        pcb_table[next].io_pending = 1;
        printf("KERNEL: Iniciando próxima I/O de A%d (PID %d)\n",
               next, pcb_table[next].pid);
        fflush(stdout);
        kill(controller_pid, SIGUSR2);
    }

    schedule();
}

// --- Escalonador ---
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
        printf("KERNEL: Preemptando processo A%d (PID %d)\n",
               current_running, pcb_table[current_running].pid);
        fflush(stdout);
        kill(pcb_table[current_running].pid, SIGSTOP);
        pcb_table[current_running].state = READY;
    }

    current_running = next;
    pcb_table[current_running].state = RUNNING;

    printf("KERNEL: Executando processo A%d (PID %d)\n",
           current_running, pcb_table[current_running].pid);
    fflush(stdout);

    // --- Restaura o contexto salvo apenas se existir ---
    if (pcb_table[current_running].saved_pc_valid) {
        int restored_pc = pcb_table[current_running].saved_pc + 1;
        write(pcb_table[current_running].pipe_write_fd, &restored_pc, sizeof(int));
        pcb_table[current_running].saved_pc_valid = 0; // evita reenvio
    }

    kill(pcb_table[current_running].pid, SIGCONT);
}

// --- MAIN ---
int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Uso: %s <num_apps>\n", argv[0]);
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
        int app_to_kernel[2], kernel_to_app[2];
        pipe(app_to_kernel);
        pipe(kernel_to_app);

        pid_t pid = fork();
        if (pid == 0) {
            close(app_to_kernel[0]); // app não lê
            close(kernel_to_app[1]); // app não escreve

            char fd_read_str[10], fd_write_str[10];
            sprintf(fd_read_str, "%d", kernel_to_app[0]);  // kernel → app
            sprintf(fd_write_str, "%d", app_to_kernel[1]); // app → kernel

            execl("./app", "app", fd_read_str, fd_write_str, NULL);
            perror("execl");
            exit(1);
        }

        close(app_to_kernel[1]);
        close(kernel_to_app[0]);

        pcb_table[i].pid = pid;
        pcb_table[i].state = READY;
        pcb_table[i].io_pending = 0;
        pcb_table[i].io_timer = 0;
        pcb_table[i].pipe_read_fd  = app_to_kernel[0];
        pcb_table[i].pipe_write_fd = kernel_to_app[1];
        pcb_table[i].saved_pc = 0;
        pcb_table[i].syscall_param = '\0';
        pcb_table[i].saved_pc_valid = 0; // <<< NOVO

        printf("KERNEL: Processo A%d criado (PID %d)\n", i, pid);
        fflush(stdout);
    }

    sleep(1);
    printf("KERNEL: Parando todos os apps inicialmente...\n");
    fflush(stdout);
    for (int i = 0; i < num_apps; i++)
        kill(pcb_table[i].pid, SIGSTOP);

    signal(SIGUSR1, handle_irq0);
    signal(SIGUSR2, handle_syscall_from_app);
    signal(SIGALRM, handle_io_complete);

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

    while (1) pause();
    return 0;
}
