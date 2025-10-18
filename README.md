# Simulador de Sistema Operacional

Este projeto implementa um simulador de sistema operacional que demonstra conceitos fundamentais como escalonamento de processos, gerenciamento de I/O e controle de interrupções.

## Arquitetura do Sistema

O sistema é composto por três componentes principais:

- **Kernel**: Gerencia processos, escalonamento e controle de I/O
- **App**: Aplicação que simula processos de usuário com operações de I/O
- **InterControllerSim**: Simula o controlador de interrupções (timer)

## Pré-requisitos

- Compilador GCC
- Sistema operacional Unix/Linux (ou WSL no Windows)
- Make (opcional, mas recomendado)

## Como Compilar

### Usando Make (Recomendado)
```bash
make
```

### Compilação Manual
```bash
gcc -Wall -g -o kernel kernel.c
gcc -Wall -g -o app app.c
gcc -Wall -g -o InterControllerSim InterControllerSim.c
```

## Como Executar

### Execução Básica
```bash
./kernel <num_apps>
```

Onde `num_apps` é o número de processos de aplicação a serem criados (deve estar entre 3 e 6).

### Exemplo
```bash
./kernel 4
```

## Como Testar

### 1. Teste de Funcionamento Básico
Execute o sistema com 4 processos:
```bash
./kernel 4
```

**Comportamento esperado:**
- O kernel criará 4 processos de aplicação
- Cada processo executará instruções sequencialmente
- O escalonador alternará entre processos a cada time slice
- Processos farão syscalls de I/O (READ no PC=5, WRITE no PC=10)

### 2. Teste de Escalonamento
Observe a saída do sistema para verificar:
- Criação dos processos
- Alternância entre processos (preempção)
- Estados dos processos (READY, RUNNING, BLOCKED)

### 3. Teste de I/O
Verifique se:
- Processos fazem syscalls de I/O nos momentos corretos
- Processos são bloqueados durante operações de I/O
- Processos retornam ao estado READY após conclusão do I/O

### 4. Teste de Interrupções
O sistema deve mostrar:
- IRQ0 (fim do time slice) a cada segundo
- IRQ1 (syscall de I/O) quando processos fazem operações de I/O

## Saída Esperada

```
KERNEL: Criando 4 processos de aplicacao...
KERNEL: Processo A0 criado (PID 1234)
KERNEL: Processo A1 criado (PID 1235)
KERNEL: Processo A2 criado (PID 1236)
KERNEL: Processo A3 criado (PID 1237)
App iniciado (PID 1234)
App iniciado (PID 1235)
App iniciado (PID 1236)
App iniciado (PID 1237)
KERNEL: Parando todos os apps inicialmente...
KERNEL: Criando InterControllerSim...
InterControllerSim: Iniciado. Kernel PID = 1233
KERNEL: Iniciando escalonamento...
KERNEL: Executando processo A0 (PID 1234)
  App (PID 1234): executando instrucao (PC=0)
  App (PID 1234): executando instrucao (PC=1)
  ...

KERNEL: IRQ0 (fim do time slice)
KERNEL: Preemptando processo A0 (PID 1234)
KERNEL: Executando processo A1 (PID 1235)
  ...
```

## Parâmetros Configuráveis

### Time Slice
No arquivo `InterControllerSim.c`, linha 6:
```c
#define TIME_SLICE_SECONDS 1
```

### Duração do I/O
No arquivo `kernel.c`, linha 7:
```c
#define IO_DURATION_SECONDS 3
```

### Pontos de Syscall
No arquivo `app.c`, linhas 27-31:
```c
if (pc == 5) {
    syscall_io('R');  // READ no PC=5
} else if (pc == 10) {
    syscall_io('W');  // WRITE no PC=10
}
```

## Limpeza

Para remover os executáveis compilados:
```bash
make clean
```

## Estrutura do Projeto

```
trab1-so/
├── app.c              # Aplicação que simula processos de usuário
├── kernel.c           # Kernel do sistema operacional
├── InterControllerSim.c  # Controlador de interrupções
├── Makefile           # Script de compilação
└── README.md          # Este arquivo
```

## Conceitos Demonstrados

- **Escalonamento Round-Robin**: Processos são executados em time slices
- **Estados de Processo**: READY, RUNNING, BLOCKED
- **Gerenciamento de I/O**: Operações bloqueiam o processo
- **Sinais Unix**: Comunicação entre processos via SIGUSR1/SIGUSR2
- **PCB (Process Control Block)**: Estrutura de controle de processos

## Troubleshooting

### Erro de Compilação
- Verifique se o GCC está instalado
- No Windows, use WSL ou MinGW

### Processos Não Iniciam
- Verifique se os executáveis foram compilados corretamente
- Execute `make clean && make` para recompilar

### Sistema Trava
- Use Ctrl+C para interromper
- Verifique se não há processos órfãos rodando