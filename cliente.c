#define _POSIX_C_SOURCE 200112L
#define _XOPEN_SOURCE 700

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <pthread.h>
#include "comum.h"

int running = 1;
int em_viagem = 0;        // 1 dentro do taxi
int login_sucesso = 0;
int hora_atual_global = 0;

char pipe_cliente[40];
int servico_atual = -1;

void terminar_cliente(void) {

        //terminar thread leitura    
        char pipe_cli[40];
        sprintf(pipe_cliente, PIPE_CLIENTE, getpid());
        int fd_tt = open(pipe_cliente, O_WRONLY);
        if (fd_tt != -1) {
            resposta rsp_tt;
            memset(&rsp_tt, 0, sizeof(rsp_tt));
            rsp_tt.status = 0;
            write(fd_tt, &rsp_tt, sizeof(resposta));
            close(fd_tt);
        }
}

// THREAD DE LEITURA DO PIPE DO CLIENTE
void *thread_leitura() {
    int fd_leitura = open(pipe_cliente, O_RDWR);
    if (fd_leitura == -1) {
        perror("Erro a abrir pipe cliente para leitura");
        running = 0;
        return NULL;
    }

    resposta rsp;
    int nbytes;

    while (running) {
        nbytes = read(fd_leitura, &rsp, sizeof(resposta));

        if (!running)
            break;

        if (nbytes == 0) {
            // outro lado fechou o pipe
            break;
        }

        if (nbytes < 0) {
            if (errno == EINTR)
                continue;
            perror("Erro a ler do pipe cliente");
            break;
        }

        printf("\nMENSAGEM: %s", rsp.msg);

        if (rsp.id_servico > 0) {
            servico_atual = rsp.id_servico;
            printf(" [Ref. Servico: %d]", rsp.id_servico);
        }
        printf("\n");

        // taxi chegou 
        if (strncmp(rsp.msg, "[Veiculo] TAXI CHEGOU.", 21) == 0) {
            printf("Cliente entrou no veiculo. Viagem em Progresso.\n");
            em_viagem = 1;
            printf("cliente@taxi:~$ ");
            fflush(stdout);
        }

        // viagem terminou ou cancelada 
        if (strncmp(rsp.msg, "[Veiculo] Viagem concluida", 26) == 0 ||
            strncmp(rsp.msg, "[Veiculo] Viagem interrompida", 29) == 0) {
            printf("Viagem terminada.\n");
            em_viagem = 0;
            servico_atual = -1;
        }

        if (strncmp(rsp.msg, "[Controlador] Servico ", 23) == 0) {
            int id;
            if (sscanf(rsp.msg,
                       "[Controlador] Servico %d cancelado.", &id) == 1) {
                if (servico_atual == id) {
                    em_viagem = 0;
                    servico_atual = -1;
                }
            }
        }

        if (strncmp(rsp.msg, "[Controlador] Login com Sucesso", 31) == 0 ||
            strncmp(rsp.msg, "[Controlador] Bem-vindo de volta", 32) == 0)
        {
            login_sucesso = 1;
        }

        if (strncmp(rsp.msg, "[Controlador] Erro", 18) == 0) {
            running = 0;
            login_sucesso = 1;
        }

        if (strncmp(rsp.msg, "[Controlador] Hora atual:", 25) == 0)
        {
            sscanf(rsp.msg, "[Controlador] Hora atual: %d", &hora_atual_global);
            printf("cliente@taxi:~$ ");
            fflush(stdout);
        }

        if (strncmp(rsp.msg, "[Controlador] Agendado para as", 30) == 0)
        {
            printf("cliente@taxi:~$ ");
            fflush(stdout);
        }

        if (strcmp(rsp.msg, "[Controlador] Sistema a encerrar. Volte mais tarde.") == 0)
        {
            running = 0;
            terminar_cliente();
            break;
        }

        /*if (running &&
            strncmp(rsp.msg, "[Controlador] Login", 19) != 0) {
            printf("cliente@taxi:~$ ");
            fflush(stdout);
        }*/
    }

    close(fd_leitura);
    return NULL;
}

void handle_signal(int signal, siginfo_t *info, void *context) {
    if (signal == SIGINT || signal == SIGPIPE) {
        running = 0;
        terminar_cliente();
    }
}

int main(int argc, char *argv[]) {
    setbuf(stdout, NULL);

    if (argc != 2) {
        printf("Uso: %s <username>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_flags = SA_SIGINFO;
    sa.sa_sigaction = handle_signal;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGPIPE, &sa, NULL);

    sprintf(pipe_cliente, PIPE_CLIENTE, getpid());

    if (mkfifo(pipe_cliente, 0666) == -1 && errno != EEXIST) {
        perror("Erro a criar pipe cliente");
        exit(EXIT_FAILURE);
    }

    if (access(PIPE_NAME, F_OK) == -1) {
        printf("O controlador nao esta a correr.\n");
        unlink(pipe_cliente);
        exit(EXIT_FAILURE);
    }

    int fd_server = open(PIPE_NAME, O_WRONLY);
    if (fd_server == -1) {
        perror("Erro a ligar ao servidor");
        unlink(pipe_cliente);
        exit(EXIT_FAILURE);
    }

    pthread_t t_leitura;
    if (pthread_create(&t_leitura, NULL, thread_leitura, NULL) != 0) {
        perror("Erro a criar thread de leitura");
        close(fd_server);
        unlink(pipe_cliente);
        exit(EXIT_FAILURE);
    }

    pedido p;
    p.pid = getpid();
    strncpy(p.dados.login.username,
            argv[1],
            sizeof(p.dados.login.username) - 1);
    p.dados.login.username[sizeof(p.dados.login.username) - 1] = '\0';

    printf("-------- Servico de Taxi -----------\n");
    printf("A tentar login como: %s...\n", p.dados.login.username);

    p.type = REQ_LOGIN;
    write(fd_server, &p, sizeof(pedido));

    while (!login_sucesso && running) {
        sleep(1);
    }

    if (!running) {
        pthread_join(t_leitura, NULL);
        close(fd_server);
        unlink(pipe_cliente);
        return 0;
    } 

    while (running) {
        printf("cliente@taxi:~$ ");
        fflush(stdout);

        char linha[100];
        if (fgets(linha, sizeof(linha), stdin) == NULL) {
            break;
        }

        linha[strcspn(linha, "\n")] = '\0';

        // ignora comando vazio
        if (linha[0] == '\0') {
            continue;
        }

        char comando[20];
        sscanf(linha, "%19s", comando);

        if (strcmp(comando, "terminar") == 0) {
            if (em_viagem) {
                printf("Erro: Nao pode sair durante um servico. "
                       "Aguarde ou cancele primeiro.\n");
                continue;
            }
            p.type = REQ_SAIR;
            p.pid = getpid();
            write(fd_server, &p, sizeof(pedido));
            running = 0;
            terminar_cliente();
        }
        else if (strcmp(comando, "agendar") == 0) {
            int h, d;
            char local[MSG_SIZE];
            // agendar <hora> <local> <distancia>
            if (sscanf(linha, "agendar %d %99s %d", &h, local, &d) != 3) {
                printf("Uso: agendar <hora> <local> <distancia>\n");
                continue;
            }

            if (d <= 0) {
                printf("Erro: distancia deve ser positiva.\n");
                continue;
            }

            pedido p_hora;
            p_hora.type = REQ_HORA_ATUAL;
            p_hora.pid = getpid();
            write(fd_server, &p_hora, sizeof(pedido));
            sleep(1); 

            if (h < hora_atual_global) {
                printf("\nErro: hora de agendamento deve ser >= hora atual (%d).\n",
                    hora_atual_global);
                continue;
            }

            memset(&p, 0, sizeof(pedido));
            p.type = REQ_AGENDAR;
            p.pid = getpid();
            p.dados.agendar.hora = h;
            p.dados.agendar.distancia = d;

            write(fd_server, &p, sizeof(pedido));
            printf("Pedido de agendamento enviado.\n");
        }

        else if (strcmp(comando, "cancelar") == 0) {
            int id;
            if (sscanf(linha, "cancelar %d", &id) != 1) {
                printf("Uso: cancelar <id>\n");
                continue;
            }

            if (id < 0) {
                printf("Erro: id invalido.\n");
                continue;
            }

            p.type = REQ_CANCELAR;
            p.pid = getpid();
            p.dados.id_cancelar = id;

            write(fd_server, &p, sizeof(pedido));
            printf("Pedido de cancelamento enviado.\n");
        }

        else if (strcmp(comando, "consultar") == 0) {
            if (servico_atual <= 0) {
                printf("Nao tem nenhum servico ativo no momento.\n");
                continue;
            }            
            
            p.type = REQ_CONSULTAR;
            p.pid = getpid();
            p.dados.id_consultar = 0;  // 0  todos os serviços do cliente

            write(fd_server, &p, sizeof(pedido));
            printf("A consultar seus servicos...\n");
        }
        else if (strcmp(comando, "hora") == 0)
        {
            p.type = REQ_HORA_ATUAL;
            p.pid = getpid();
            write(fd_server, &p, sizeof(pedido));
            printf("A consultar a Hora...\n");
        }
        else
        {
            printf("Comando invalido. Use: agendar, cancelar, consultar, hora, terminar\n");
        }
    }

    printf("\nA desligar cliente...\n");

    pthread_join(t_leitura, NULL);
    close(fd_server);
    unlink(pipe_cliente);

    return 0;
}