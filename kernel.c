/*******************************************************************************
 * KERNEL - Simulador de Sistema Operacional com Escalonamento Round-Robin
 *
 * Este módulo implementa o núcleo de um sistema operacional simplificado que
 * gerencia múltiplos processos de aplicação, tratando interrupções, syscalls
 * e operações de entrada/saída de forma coordenada.
 *
 * Funcionalidades principais:
 *   - Escalonamento de processos (Round-Robin)
 *   - Gerenciamento de estados de processos (READY, RUNNING, BLOCKED)
 *   - Tratamento de interrupções (IRQ0, IRQ1, IRQ2)
 *   - Controle de operações de I/O com fila de bloqueados
 *   - Comunicação inter-processos via pipes
 ******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <string.h>

#define MAX_PROCESSES 6
#define IO_DURATION_SECONDS 3

/*******************************************************************************
 * ESTRUTURAS DE DADOS - Fila de Processos Bloqueados
 *
 * Esta seção define a estrutura de uma fila circular para gerenciar processos
 * que estão aguardando operações de I/O. A fila opera em modo FIFO (First In,
 * First Out), garantindo que os processos sejam atendidos na ordem de chegada.
 ******************************************************************************/
int blocked_queue[MAX_PROCESSES];
int blocked_front = 0;
int blocked_rear = 0;
int io_in_progress = 0;

/*******************************************************************************
 * enqueue_blocked - Adiciona um processo à fila de bloqueados
 *
 * Insere o índice de um processo no final da fila circular de bloqueados.
 * Utilizado quando um processo solicita uma operação de I/O e precisa aguardar.
 *
 * Parâmetros:
 *   pid_index - Índice do processo na tabela PCB que será enfileirado
 *
 * Comportamento:
 *   - Adiciona o processo no final da fila (posição rear)
 *   - Incrementa o ponteiro rear de forma circular
 ******************************************************************************/
void enqueue_blocked(int pid_index) {
    blocked_queue[blocked_rear] = pid_index;
    blocked_rear = (blocked_rear + 1) % MAX_PROCESSES;
}

/*******************************************************************************
 * dequeue_blocked - Remove um processo da fila de bloqueados
 *
 * Remove e retorna o índice do primeiro processo na fila de bloqueados.
 * Implementa a política FIFO para atendimento das operações de I/O.
 *
 * Retorna:
 *   - Índice do processo removido da fila
 *   - -1 se a fila estiver vazia
 *
 * Comportamento:
 *   - Verifica se a fila está vazia antes de remover
 *   - Incrementa o ponteiro front de forma circular
 ******************************************************************************/
int dequeue_blocked() {
    if (blocked_front == blocked_rear)
        return -1;
    int pid_index = blocked_queue[blocked_front];
    blocked_front = (blocked_front + 1) % MAX_PROCESSES;
    return pid_index;
}

/*******************************************************************************
 * blocked_is_empty - Verifica se a fila de bloqueados está vazia
 *
 * Retorna:
 *   - 1 (true) se a fila estiver vazia
 *   - 0 (false) se houver processos na fila
 ******************************************************************************/
int blocked_is_empty() {
    return blocked_front == blocked_rear;
}

/*******************************************************************************
 * TIPOS E ESTRUTURAS DE DADOS
 ******************************************************************************/

/* Estados possíveis de um processo no sistema */
typedef enum { READY, RUNNING, BLOCKED } ProcessState;

/*
 * SyscallContext - Contexto de uma chamada de sistema
 *
 * Armazena as informações enviadas por um processo quando ele faz uma syscall.
 * Permite que o kernel saiba exatamente em que ponto do código a syscall foi
 * feita e qual operação foi solicitada.
 *
 * Campos:
 *   pc        - Program Counter (contador de programa) no momento da syscall
 *   operation - Tipo de operação ('R' para READ, 'W' para WRITE)
 */
typedef struct {
    int pc;
    char operation;
} SyscallContext;

/*
 * PCB - Process Control Block (Bloco de Controle de Processo)
 *
 * Estrutura fundamental que mantém todas as informações sobre um processo
 * que o kernel precisa para gerenciá-lo adequadamente.
 *
 * Campos:
 *   pid            - ID do processo no sistema operacional
 *   state          - Estado atual do processo (READY, RUNNING ou BLOCKED)
 *   io_pending     - Flag indicando se há uma operação de I/O em andamento
 *   io_timer       - Timer para controlar a duração de operações de I/O
 *   pipe_read_fd   - Descriptor do pipe para ler dados do app (App → Kernel)
 *   pipe_write_fd  - Descriptor do pipe para enviar dados ao app (Kernel → App)
 *   saved_pc       - Program Counter salvo durante uma syscall
 *   syscall_param  - Parâmetro da syscall (tipo de operação solicitada)
 *   saved_pc_valid - Flag que indica se há um PC válido para restaurar
 */
typedef struct {
    pid_t pid;
    ProcessState state;
    int io_pending;
    int io_timer;
    int pipe_read_fd;
    int pipe_write_fd;
    int saved_pc;
    char syscall_param;
    int saved_pc_valid;
} PCB;

/*******************************************************************************
 * VARIÁVEIS GLOBAIS DO KERNEL
 ******************************************************************************/
int num_apps = 0;
PCB *pcb_table = NULL;
pid_t controller_pid;
int current_running = -1;

/*******************************************************************************
 * PROTÓTIPOS DE FUNÇÕES
 ******************************************************************************/
void schedule();

/*******************************************************************************
 * HANDLERS DE INTERRUPÇÕES (IRQs)
 ******************************************************************************/

/*******************************************************************************
 * handle_irq0 - Handler da IRQ0 (Fim do Time Slice)
 *
 * Esta função é chamada quando o controlador de interrupções envia um sinal
 * indicando que o time slice (quantum de tempo) do processo atual acabou.
 * É o coração do escalonamento preemptivo Round-Robin.
 *
 * Parâmetros:
 *   sig - Número do sinal recebido (SIGUSR1)
 *
 * Comportamento:
 *   - Registra a ocorrência da interrupção
 *   - Aciona o escalonador para selecionar o próximo processo
 *
 * Contexto: Handler de sinal - executado de forma assíncrona
 ******************************************************************************/
void handle_irq0(int sig) {
    printf("\nKERNEL: IRQ0 (fim do time slice)\n");
    fflush(stdout);
    schedule();
}

/*******************************************************************************
 * handle_syscall_from_app - Handler da IRQ2 (Syscall de I/O)
 *
 * Tratador de interrupção chamado quando um processo de aplicação solicita
 * uma operação de entrada/saída (I/O). Esta função implementa todo o fluxo
 * de tratamento de syscalls, incluindo salvamento de contexto, bloqueio do
 * processo e gerenciamento da fila de I/O.
 *
 * Parâmetros:
 *   sig - Número do sinal recebido (SIGUSR2)
 *
 * Fluxo de execução:
 *   1. Lê o contexto da syscall (PC e operação) através do pipe
 *   2. Salva o contexto do processo para posterior restauração
 *   3. Bloqueia o processo (estado BLOCKED) e o envia para fila
 *   4. Se não há I/O em andamento, inicia a próxima operação
 *   5. Aciona o escalonador para selecionar outro processo
 *
 * Importante:
 *   - O processo é pausado (SIGSTOP) para evitar execução durante I/O
 *   - A flag saved_pc_valid garante que o contexto só será restaurado uma vez
 *   - A fila de bloqueados mantém a ordem FIFO para justiça no atendimento
 *
 * Contexto: Handler de sinal - executado de forma assíncrona
 ******************************************************************************/
void handle_syscall_from_app(int sig) {
    printf("KERNEL: Syscall de I/O do processo A%d (PID %d)\n",
           current_running, pcb_table[current_running].pid);
    fflush(stdout);

    SyscallContext ctx;
    int fd = pcb_table[current_running].pipe_read_fd;
    if (read(fd, &ctx, sizeof(SyscallContext)) > 0) {
        pcb_table[current_running].saved_pc = ctx.pc;
        pcb_table[current_running].syscall_param = ctx.operation;
        pcb_table[current_running].saved_pc_valid = 1;
        printf("KERNEL: Contexto salvo: PC=%d, OP=%c\n\n",
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
    printf("]\n\n");
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

/*******************************************************************************
 * handle_io_complete - Handler da IRQ1 (I/O Concluída)
 *
 * Tratador de interrupção chamado quando o controlador de I/O sinaliza que
 * uma operação de entrada/saída foi concluída. Esta função gerencia a
 * transição do processo de volta ao estado READY e coordena o início de
 * novas operações de I/O pendentes.
 *
 * Parâmetros:
 *   sig - Número do sinal recebido (SIGALRM)
 *
 * Fluxo de execução:
 *   1. Marca que não há mais I/O em progresso
 *   2. Localiza o processo que estava aguardando e o desbloqueia
 *   3. Move o processo do estado BLOCKED para READY
 *   4. Se há mais processos na fila, inicia a próxima operação de I/O
 *   5. Aciona o escalonador para redistribuir o processamento
 *
 * Importante:
 *   - Apenas o primeiro processo bloqueado com io_pending é desbloqueado
 *   - Se houver mais processos na fila, automaticamente inicia próxima I/O
 *   - Garante que sempre haja no máximo uma operação de I/O ativa
 *
 * Contexto: Handler de sinal - executado de forma assíncrona
 ******************************************************************************/
void handle_io_complete(int sig) {
    printf("\nKERNEL: IRQ1 (I/O concluída) recebido do InterControllerSim\n");
    printf("KERNEL: Fila de bloqueados após dequeue: [");
    for (int i = blocked_front; i != blocked_rear; i = (i + 1) % MAX_PROCESSES)
        printf(" A%d ", blocked_queue[i]);
    printf("]\n");
    fflush(stdout);

    io_in_progress = 0;

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

/*******************************************************************************
 * ESCALONADOR DE PROCESSOS
 ******************************************************************************/

/*******************************************************************************
 * schedule - Escalonador Round-Robin de Processos
 *
 * Implementa a política de escalonamento Round-Robin, selecionando o próximo
 * processo READY para executar. Esta é a função central do gerenciamento de
 * processos do kernel.
 *
 * Algoritmo Round-Robin:
 *   - Percorre a tabela de processos de forma circular
 *   - Seleciona o primeiro processo no estado READY encontrado
 *   - Garante distribuição justa do tempo de CPU entre todos os processos
 *
 * Fluxo de execução:
 *   1. Busca o próximo processo READY (política Round-Robin)
 *   2. Se não houver processos READY, retorna sem fazer nada
 *   3. Se há processo em execução, realiza preempção (pausa o processo)
 *   4. Atualiza o processo atual para o próximo selecionado
 *   5. Restaura o contexto salvo (PC) se houver syscall anterior
 *   6. Resume a execução do processo selecionado
 *
 * Tratamento de contexto:
 *   - Verifica a flag saved_pc_valid antes de restaurar contexto
 *   - Envia o PC restaurado (+1) através do pipe para o processo
 *   - Limpa a flag após restaurar para evitar restaurações duplicadas
 *
 * Importante:
 *   - Processos BLOCKED não são considerados para escalonamento
 *   - A preempção garante que nenhum processo monopolize a CPU
 *   - O contexto só é restaurado se houve uma syscall anterior
 ******************************************************************************/
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

    // --- Restaura o contexto salvo ---
    if (pcb_table[current_running].saved_pc_valid) {
        int restored_pc = pcb_table[current_running].saved_pc;
        write(pcb_table[current_running].pipe_write_fd, &restored_pc, sizeof(int));
        pcb_table[current_running].saved_pc_valid = 0;
    }

    kill(pcb_table[current_running].pid, SIGCONT);
}

/*******************************************************************************
 * FUNÇÃO PRINCIPAL DO KERNEL
 ******************************************************************************/

/*******************************************************************************
 * main - Ponto de entrada do kernel
 *
 * Inicializa todo o sistema operacional, criando os processos de aplicação,
 * configurando os canais de comunicação (pipes), registrando os handlers de
 * interrupções e iniciando o controlador de I/O.
 *
 * Parâmetros:
 *   argc - Número de argumentos da linha de comando
 *   argv - Array de argumentos (espera-se argv[1] = número de processos)
 *
 * Fluxo de inicialização:
 *   1. Valida os argumentos de entrada (número de apps entre 3 e 6)
 *   2. Aloca a tabela PCB para gerenciar os processos
 *   3. Cria os processos de aplicação via fork/exec
 *   4. Configura pipes bidirecionais para cada processo
 *   5. Inicializa os PCBs com estado READY
 *   6. Pausa todos os processos para controle inicial
 *   7. Registra os handlers de sinais (IRQ0, IRQ1, IRQ2)
 *   8. Cria o processo InterControllerSim
 *   9. Inicia o escalonamento
 *   10. Entra em loop infinito aguardando interrupções
 *
 * Comunicação inter-processos:
 *   - Cada app possui dois pipes: app→kernel e kernel→app
 *   - O kernel fecha as pontas não utilizadas dos pipes
 *   - Os file descriptors são passados via argumentos do execl
 *
 * Mapeamento de sinais:
 *   SIGUSR1  → IRQ0 (fim do time slice)
 *   SIGUSR2  → IRQ2 (syscall de I/O)
 *   SIGALRM  → IRQ1 (conclusão de I/O)
 *
 * Retorna:
 *   0 em caso de término normal (na prática, roda indefinidamente)
 ******************************************************************************/
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
            close(app_to_kernel[0]);
            close(kernel_to_app[1]);

            char fd_read_str[10], fd_write_str[10], use_io_str[2];
            sprintf(fd_read_str, "%d", kernel_to_app[0]);
            sprintf(fd_write_str, "%d", app_to_kernel[1]);

            // ALTERAR PARA TESTES
            int use_io = (i >= 3) ? 1 : 0;

            sprintf(fd_read_str, "%d", kernel_to_app[0]);
            sprintf(fd_write_str, "%d", app_to_kernel[1]);
            sprintf(use_io_str, "%d", use_io);

            execl("./app", "app", fd_read_str, fd_write_str, use_io_str, NULL);
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
        pcb_table[i].saved_pc_valid = 0;

        printf("KERNEL: Processo A%d criado (PID %d)\n", i, pid);
        kill(pcb_table[i].pid, SIGSTOP);
        printf("KERNEL: Processo A%d PARADO INICIALMENTE (PID %d)\n", i, pid);
        fflush(stdout);
    }      

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
