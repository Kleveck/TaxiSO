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
#include <sys/wait.h>
#include <pthread.h>
#include <limits.h>
#include "comum.h"


Utilizador users[MAX_USERS];
Servico servicos[MAX_SERVICOS];


const int AGENDADO  = 0;
const int EM_VIAGEM = 1;
const int TERMINADO = 2;
const int EM_ESPERA = 3;

int n_users       = 0;
int num_servicos  = 0;
int tempoAtual    = 1;
int max_veiculos  = 0;
long total_km = 0;
int veiculos_ativos = 0;
int next_service_id = 1;

int running = 1;

pthread_mutex_t mux_dados = PTHREAD_MUTEX_INITIALIZER;

void iniciar_veiculo(int id) {
    int pipe_anonimo[2];

    if (pipe(pipe_anonimo) == -1) {
        perror("pipe");
        return;
    }

    pid_t pid = fork();
    if (pid == -1) {
        perror("fork");
        close(pipe_anonimo[0]);
        close(pipe_anonimo[1]);
        return;
    }

    if (pid == 0) {
        // filho: veículo
        char pid_cli_str[16];
        char id_str[16];
        char dist_str[16];

        close(pipe_anonimo[0]);
        close(STDOUT_FILENO);
        dup(pipe_anonimo[1]);
        close(pipe_anonimo[1]);

        sprintf(pid_cli_str, "%d", servicos[id].pid_cliente);
        sprintf(id_str,      "%d", servicos[id].id);
        sprintf(dist_str,    "%d", servicos[id].distancia);

        execl("./veiculo", "veiculo", id_str, dist_str, pid_cli_str, NULL);
        perror("execl");
        _exit(1);
    }

    close(pipe_anonimo[1]);

    servicos[id].pid_veiculo = pid;
    servicos[id].fd_veiculo = pipe_anonimo[0];
    servicos[id].status = EM_VIAGEM;
    servicos[id].perc_concluida = 0;
    veiculos_ativos++;

    printf("\n[CONTROLADOR] Iniciado veiculo PID %d para serviço %d (t=%d)\n", pid, servicos[id].id, tempoAtual);
    printf("administrador@controlador:~$ ");
}

// procura utilizador por pid
int procura_user(pid_t pid) {
    for (int i = 0; i < n_users; i++)
        if (users[i].pid == pid)
            return i;
    return -1;
}

// remove utilizador do array
void remover_cliente(pid_t pid) {
    int id = procura_user(pid);
    if (id == -1) return;

    for (int j = id; j < n_users - 1; j++)
        users[j] = users[j+1];

    n_users--;
    printf("\n[CONTROLADOR] Cliente PID %d removido. Ativos: %d\n", pid, n_users);
    printf("administrador@controlador:~$ ");
}

// verifica se username ja existe
int nome_existe(const char *nome) {
    for (int i = 0; i < n_users; i++)
        if (strcmp(users[i].username, nome) == 0)
            return 1;
    return 0;
}

// encontrar indice de serviço por id (apenas agendados/em viagem) 
int encontra_servico_index(int id) {
    for (int i = 0; i < num_servicos; i++)
        if (servicos[i].id == id &&
            (servicos[i].status == AGENDADO ||
             servicos[i].status == EM_VIAGEM ||
             servicos[i].status == EM_ESPERA))
            return i;
    return -1;
}

// cancela todo os serviços (agendados/em viagem) de um cliente
void cancelar_servicos_cliente(pid_t pid_cliente) {
    union sigval v;
    memset(&v, 0, sizeof(v));

    for (int i = 0; i < num_servicos; i++) {
        if (servicos[i].pid_cliente != pid_cliente)
            continue;

        if (servicos[i].status == EM_VIAGEM) {
            // EM_VIAGEM: só sinal (não mexe em status nem veiculos_ativos)
            sigqueue(servicos[i].pid_veiculo, SIGUSR1, v);
        }
        else if (servicos[i].status == AGENDADO || servicos[i].status == EM_ESPERA) {
            // AGENDADO/EM_ESPERA: termina já
            servicos[i].status = TERMINADO;
        }
    }
}

// cancela todos os serviços de todos os clientes
void cancelar_todos_servicos() {
    union sigval v;
    memset(&v, 0, sizeof(v));

    for (int i = 0; i < num_servicos; i++) {

        if (servicos[i].status == EM_VIAGEM) {
            // se EM_VIAGEM: só sinal
            sigqueue(servicos[i].pid_veiculo, SIGUSR1, v);
        }
        else if (servicos[i].status == AGENDADO || servicos[i].status == EM_ESPERA) {
            // se AGENDADO/EM_ESPERA: termina já
            servicos[i].status = TERMINADO;
        }
    }
}

int tem_conflito_horario(pid_t pid_cliente, int hora_inicio, int distancia) {
    int hora_fim = hora_inicio + distancia;
    
    for (int i = 0; i < num_servicos; i++) {
        // so verifica servicos ativos do mesmo cliente
        if (servicos[i].pid_cliente == pid_cliente &&
            (servicos[i].status == AGENDADO || 
             servicos[i].status == EM_VIAGEM ||
             servicos[i].status == EM_ESPERA)) {
            
            int outro_inicio = servicos[i].hora_agendada;
            int outro_fim = servicos[i].hora_agendada + servicos[i].distancia;
            
            // verifica sobreposicao
            if (!(hora_fim <= outro_inicio || hora_inicio >= outro_fim)) {
                return 1; // conflito
            }
        }
    }
    return 0; // sem conflito
}

void processar_lista_espera() {
    while (veiculos_ativos < max_veiculos) {
        int melhor_idx  = -1;
        int melhor_hora = INT_MAX;
        int melhor_id   = INT_MAX;

        for (int i = 0; i < num_servicos; i++) {
            if (servicos[i].status == EM_ESPERA &&
                servicos[i].hora_agendada <= tempoAtual) {

                if (servicos[i].hora_agendada < melhor_hora ||
                    (servicos[i].hora_agendada == melhor_hora && servicos[i].id < melhor_id)) {
                    melhor_idx  = i;
                    melhor_hora = servicos[i].hora_agendada;
                    melhor_id   = servicos[i].id;
                }
            }
        }

        if (melhor_idx == -1) break;

        iniciar_veiculo(melhor_idx);

        if (servicos[melhor_idx].status != EM_VIAGEM) break;
    }
}


// parse de comando do admin
CommandType parse_comando(const char *cmd) {
    if (strcmp(cmd, "listar")   == 0) return listar;
    if (strcmp(cmd, "utiliz")   == 0) return utiliz;
    if (strcmp(cmd, "frota")    == 0) return frota;
    if (strcmp(cmd, "cancelar") == 0) return cancelar;
    if (strcmp(cmd, "km")       == 0) return km;
    if (strcmp(cmd, "hora")     == 0) return hora;
    if (strcmp(cmd, "terminar") == 0) return terminar;
    return invalido;
}

void *thread_tempo(void *arg) {
    (void)arg;

    while (running) {
        sleep(INTERVALO_TEMPO);
        pthread_mutex_lock(&mux_dados);
        tempoAtual++;

        // Verifica serviços agendados que já podem arrancar
        for (int i = 0; i < num_servicos; i++) {

            if (servicos[i].status == AGENDADO &&
                servicos[i].hora_agendada <= tempoAtual) {

                if (veiculos_ativos < max_veiculos) {
                    iniciar_veiculo(i);
                } else {
                    // Frota cheia nesta hora -> vai para lista de espera
                    servicos[i].status = EM_ESPERA;
                }
            }
        }

        // Tenta iniciar serviços em espera, se houver frota livre
        processar_lista_espera();

        pthread_mutex_unlock(&mux_dados);
    }

    return NULL;
}

void notificar_cancelamento_cliente(Servico *s) {
    char pipe_cli[64];
    int fd_cli;
    resposta rsp;

    sprintf(pipe_cli, PIPE_CLIENTE, s->pid_cliente);
    fd_cli = open(pipe_cli, O_WRONLY);
    if (fd_cli == -1)
        return; 

    rsp.status = 1;
    rsp.id_servico = s->id;
    sprintf(rsp.msg, "[Controlador] Servico %d cancelado.", s->id);

    write(fd_cli, &rsp, sizeof(rsp));
    close(fd_cli);
}

void *thread_veiculo() {
    char msg[256];
    int n;

    while (running) {
        for (int i = 0; i < num_servicos; i++) {
            int fd;
            pid_t pid_veic;
            int dist;

            pthread_mutex_lock(&mux_dados);
            if (servicos[i].status != EM_VIAGEM) {
                pthread_mutex_unlock(&mux_dados);
                continue;
            }
            fd = servicos[i].fd_veiculo;
            pid_veic = servicos[i].pid_veiculo;
            dist = servicos[i].distancia;
            pthread_mutex_unlock(&mux_dados);

            if (fd < 0)
                continue;

            n = read(fd, msg, sizeof(msg) - 1);
            if (n <= 0)
                continue;

            msg[n] = '\0';

            int terminou = 0;
            int cancelado = 0;
            int perc_encontrada = -1;

            for (int k = 0; k < n; k++) {
                if (strncmp(&msg[k], "PERC", 4) == 0) {

                    int x = 0;
                    if (sscanf(&msg[k], "PERC: %d", &x) == 1) {
                        perc_encontrada = x;
                    }
                }
                if (strncmp(&msg[k], "Concluido", 9) == 0) {
                    terminou = 1;
                }
                if (strncmp(&msg[k], "Cancelado", 9) == 0){
                    cancelado = 1;
                }
            }
            pthread_mutex_lock(&mux_dados);

            if (perc_encontrada >= 0 &&
                servicos[i].status == EM_VIAGEM) {
                servicos[i].perc_concluida = perc_encontrada;

                /*printf("Servico %d: %d%% concluido\n",
                       servicos[i].id,
                       servicos[i].perc_concluida);*/
            }

            if ((terminou && servicos[i].status == EM_VIAGEM) || cancelado) {
                close(servicos[i].fd_veiculo);
                servicos[i].fd_veiculo = -1;
                servicos[i].status = TERMINADO;

                int perc = servicos[i].perc_concluida;
                if (perc <= 0) perc = 0;
                if (perc > 100) perc = 100;

                long km_efetivos = (dist * perc) / 100;
                total_km = total_km + km_efetivos;

                veiculos_ativos--;
                waitpid(servicos[i].pid_veiculo, NULL, 0);

                printf("\n[CONTROLADOR] Serviço %d finalizado (%d%%, %ld km acumulados)\n",
                       servicos[i].id, perc, total_km);
                printf("administrador@controlador:~$ ");
            }
            //printf("administrador@controlador:~$ ");
            pthread_mutex_unlock(&mux_dados);

        }
    }

    return NULL;
}

void terminar_sistema() {
    pthread_mutex_lock(&mux_dados);
    cancelar_todos_servicos();
    
    //notifica todos os clientes conectados
    for (int i = 0; i < n_users; i++) {
        char pipe_cli[64];
        sprintf(pipe_cli, PIPE_CLIENTE, users[i].pid);
        
        int fd_cli = open(pipe_cli, O_WRONLY | O_NONBLOCK);
        if (fd_cli != -1) {
            resposta rsp;
            rsp.status = -1;
            rsp.id_servico = -1;
            strcpy(rsp.msg, "[Controlador] Sistema a encerrar. Volte mais tarde.");
            write(fd_cli, &rsp, sizeof(rsp));
            close(fd_cli);
        }
    }
    
    running = 0;
    pthread_mutex_unlock(&mux_dados);
    
    //desbloquea thread cliente
    int fd = open(PIPE_NAME, O_WRONLY | O_NONBLOCK);
    if (fd != -1) {
        pedido p_exit;
        memset(&p_exit, 0, sizeof(p_exit));
        p_exit.type = REQ_SAIR;
        write(fd, &p_exit, sizeof(p_exit));
        close(fd);
    }
}

void *thread_admin() {
    char linha[128];
    char cmd[32];

    while (running) {
        printf("administrador@controlador:~$ ");

        if (fgets(linha, sizeof(linha), stdin) == NULL) {
            if (!running) break;
            continue;
        }
        linha[strcspn(linha, "\n")] = '\0';
        if (linha[0] == '\0') continue;

        sscanf(linha, "%31s", cmd);
        CommandType tipo = parse_comando(cmd);

        switch (tipo) {

        case listar: {
            pthread_mutex_lock(&mux_dados);
            printf("----- SERVIÇOS ------\n");
            int encontrou = 0;
            for (int i = 0; i < num_servicos; i++) {
                if (servicos[i].status == AGENDADO ||
                    servicos[i].status == EM_VIAGEM ||
                    servicos[i].status == EM_ESPERA) {
                    
                    const char *estado;
                    if (servicos[i].status == AGENDADO) estado = "Agendado";
                    else if (servicos[i].status == EM_ESPERA) estado = "Em espera";
                    else estado = "Em viagem";

                    printf("- ID: %d || Utilizador: %s || Distancia: %dkm || Hora: %d || Estado: %s\n",
                           servicos[i].id,
                           servicos[i].username,
                           servicos[i].distancia,
                           servicos[i].hora_agendada,
                           estado);
                    if (servicos[i].status == EM_VIAGEM) {
                        printf(" (%d%%)", servicos[i].perc_concluida);
                    }
                    printf("\n");
                    encontrou = 1;
                }
            }
            if (!encontrou) {
                printf("  Nenhum serviço ativo.\n");
            }
            pthread_mutex_unlock(&mux_dados);
            break;
        }

        case utiliz:
        {
            pthread_mutex_lock(&mux_dados);
            printf("--------- UTILIZADORES --------\n");
            if (n_users == 0)
            {
                printf("  Nenhum utilizador conectado.\n");
            }
            else
            {
                for (int i = 0; i < n_users; i++)
                {
                    // Procurar estado do utilizador nos serviços
                    char estado[50] = "Livre";
                    int servico_ativo = -1;
                    for (int j = 0; j < num_servicos; j++)
                    {
                        if (servicos[j].pid_cliente == users[i].pid)
                        {
                            if (servicos[j].status == EM_VIAGEM)
                            {
                                sprintf(estado, "Em Viagem (Servico %d - %d%%)",
                                        servicos[j].id, servicos[j].perc_concluida);
                                servico_ativo = servicos[j].id;
                                break;
                            }
                            else if (servicos[j].status == AGENDADO)
                            {
                                int tempo_espera = servicos[j].hora_agendada - tempoAtual;
                                sprintf(estado, "Servico Agendado (ID %d - faltam %ds)",
                                        servicos[j].id, tempo_espera);
                                servico_ativo = servicos[j].id;
                                break;
                            }
                            else if (servicos[j].status == EM_ESPERA)
                            {
                                sprintf(estado, "Em Lista de Espera (ID %d)", servicos[j].id);
                                servico_ativo = servicos[j].id;
                                break;
                            }
                        }
                    }
                    printf("- %s (PID %d) - Estado: %s\n",
                           users[i].username, users[i].pid, estado);
                }
            }
            pthread_mutex_unlock(&mux_dados);
            break;
        }

        case frota: {
            pthread_mutex_lock(&mux_dados);
            printf("------ FROTA ------\n");
            int encontrou = 0;
            for (int i = 0; i < num_servicos; i++) {
                if (servicos[i].status == EM_VIAGEM) {
                    printf("  Veiculo PID %d || Servico %d || %d%%\n", servicos[i].pid_veiculo, servicos[i].id, servicos[i].perc_concluida);
                    encontrou = 1;
                }
            }
            if (!encontrou) {
                printf("  Nenhum veículo em operação.\n");
            }
            pthread_mutex_unlock(&mux_dados);
            break;
        }

        case cancelar: {
            int id;
            if (sscanf(linha, "cancelar %d", &id) != 1) {
                printf("Uso: cancelar <id>\n");
                break;
            }

            pthread_mutex_lock(&mux_dados);

            union sigval v;
            memset(&v, 0, sizeof(v));

            int cancelados = 0;

            if (id == 0) {
                for (int i = 0; i < num_servicos; i++) {

                    if (servicos[i].status == EM_VIAGEM) {
                        // EM_VIAGEM: só sinal
                        sigqueue(servicos[i].pid_veiculo, SIGUSR1, v);
                        notificar_cancelamento_cliente(&servicos[i]);
                        cancelados++;
                    }
                    else if (servicos[i].status == AGENDADO || servicos[i].status == EM_ESPERA) {
                        // AGENDADO/EM_ESPERA: termina já
                        servicos[i].status = TERMINADO;
                        notificar_cancelamento_cliente(&servicos[i]);
                        cancelados++;
                    }
                }

                printf("Total de %d servico(s) cancelado(s).\n", cancelados);
            }
            else {
                int idx = encontra_servico_index(id);
                if (idx == -1) {
                    printf("Servico com o id %d nao foi encontrado.\n", id);
                } else {

                    if (servicos[idx].status == EM_VIAGEM) {
                        sigqueue(servicos[idx].pid_veiculo, SIGUSR1, v);
                        notificar_cancelamento_cliente(&servicos[idx]);
                        printf("Pedido de cancelamento enviado ao veiculo (servico %d).\n", id);
                        cancelados = 1;
                    }
                    else if (servicos[idx].status == AGENDADO || servicos[idx].status == EM_ESPERA) {
                        servicos[idx].status = TERMINADO;
                        notificar_cancelamento_cliente(&servicos[idx]);
                        printf("O servico com o id %d foi cancelado.\n", id);
                        cancelados = 1;
                    }
                }
            }

            pthread_mutex_unlock(&mux_dados);
            break;
        }

        case km:
            pthread_mutex_lock(&mux_dados);
            printf("Total de quilómetros percorridos: %ld\n", total_km);
            pthread_mutex_unlock(&mux_dados);
            break;

        case hora:
            pthread_mutex_lock(&mux_dados);
            printf("Tempo atual: %d segundos\n", tempoAtual);
            pthread_mutex_unlock(&mux_dados);
            break;

        case terminar:
            printf("A terminar...\n");
            terminar_sistema();
            break;


        default:
            printf("Comando invalido. Use: listar, utiliz, frota, cancelar, km, hora, terminar\n");
            break;
        }
    }

    return NULL;
}

void *thread_cliente(void *arg) {
    int fd = open(PIPE_NAME, O_RDWR);
    if (fd == -1) {
        perror("open PIPE_NAME");
        return NULL;
    }

    pedido p;
    resposta rsp;
    char pipe_cli[64];

    while (running) {
        int n = read(fd, &p, sizeof(pedido));
        if (!running) break;
        if (n <= 0) continue;

        sprintf(pipe_cli, PIPE_CLIENTE, p.pid);

        if (p.type == REQ_SAIR) {
            pthread_mutex_lock(&mux_dados);
            cancelar_servicos_cliente(p.pid);
            remover_cliente(p.pid);
            pthread_mutex_unlock(&mux_dados);
            continue;
        }

        int fd_cli = open(pipe_cli, O_WRONLY);
        if (fd_cli == -1) {
            continue;
        }

        rsp.status = 0;
        rsp.id_servico = -1;
        rsp.msg[0] = '\0';

        switch (p.type) {

        case REQ_LOGIN:
            pthread_mutex_lock(&mux_dados);
            if (procura_user(p.pid) == -1) {
                if (nome_existe(p.dados.login.username)) {
                    rsp.status = 0;
                    strcpy(rsp.msg,"[Controlador] Erro: Username ja esta em uso!");
                } else if (n_users < MAX_USERS) {
                    users[n_users].pid = p.pid;
                    strncpy(users[n_users].username,p.dados.login.username,sizeof(users[n_users].username) - 1);
                    users[n_users].username[sizeof(users[n_users].username)-1] = '\0';
                    users[n_users].em_viagem = 0;
                    n_users++;

                    rsp.status = 1;
                    strcpy(rsp.msg,"[Controlador] Login com Sucesso");
                    printf("\n[LOGIN] %s (PID %d)\n",p.dados.login.username, p.pid);
                    printf("administrador@controlador:~$ ");
                    fflush(stdout);
                } else {
                    rsp.status = 0;
                    strcpy(rsp.msg,"[Controlador] Erro: Servidor cheio");
                }
            } else {
                rsp.status = 1;
                strcpy(rsp.msg,"[Controlador] Bem-vindo de volta");
            }
            pthread_mutex_unlock(&mux_dados);
            break;

        case REQ_HORA_ATUAL:
            pthread_mutex_lock(&mux_dados);
            rsp.status = 1;
            rsp.id_servico = -1;
            sprintf(rsp.msg, "[Controlador] Hora atual: %d", tempoAtual);
            pthread_mutex_unlock(&mux_dados);
            break;

        case REQ_AGENDAR:
            pthread_mutex_lock(&mux_dados);

            if (tem_conflito_horario(p.pid, p.dados.agendar.hora, p.dados.agendar.distancia)) {
                rsp.status = 0;
                strcpy(rsp.msg, "[Controlador] Conflito de horario com outro servico seu.");
                pthread_mutex_unlock(&mux_dados);
                break;
            }

            if (num_servicos < MAX_SERVICOS) {
                Servico *s = &servicos[num_servicos];

                s->id          = next_service_id;
                s->pid_cliente = p.pid;
                s->distancia   = p.dados.agendar.distancia;
                s->hora_agendada = p.dados.agendar.hora;

                s->pid_veiculo = -1;
                s->fd_veiculo  = -1;
                s->perc_concluida = 0;

                // previsão: quantos serviços já estão marcados para esta hora?
                int ocupacao = 0;
                for (int j = 0; j < num_servicos; j++) {
                    if (servicos[j].status != TERMINADO &&
                        servicos[j].hora_agendada == s->hora_agendada) {
                        ocupacao++;
                    }
                }

                s->status = (ocupacao >= max_veiculos) ? EM_ESPERA : AGENDADO;

                for (int i = 0; i < n_users; i++) {
                    if (users[i].pid == p.pid) {
                        strncpy(s->username, users[i].username, sizeof(s->username) - 1);
                        s->username[sizeof(s->username) - 1] = '\0';
                        break;
                    }
                }

                rsp.status = 1;
                rsp.id_servico = next_service_id;

                int tempo_espera = s->hora_agendada - tempoAtual;

                // mensagens CURTAS para não estourar MSG_SIZE
                if (s->status == EM_ESPERA) {
                    sprintf(rsp.msg, "[Controlador] Espera: Servico %d, hora %d (+%d)",
                            s->id, s->hora_agendada, tempo_espera);
                } else {
                    sprintf(rsp.msg, "[Controlador] Agendado: Servico %d, hora %d (+%d)",
                            s->id, s->hora_agendada, tempo_espera);
                }

                num_servicos++;
                next_service_id++;
            } else {
                rsp.status = 0;
                strcpy(rsp.msg, "[Controlador] Erro: Lista de servicos cheia.");
            }

            pthread_mutex_unlock(&mux_dados);
            break;


        case REQ_CANCELAR: {
            int id = p.dados.id_cancelar;

            pthread_mutex_lock(&mux_dados);

            union sigval value;
            memset(&value, 0, sizeof(value));

            if (id == 0) {
                int cancelados = 0;

                for (int i = 0; i < num_servicos; i++) {
                    if (servicos[i].pid_cliente != p.pid)
                        continue;

                    if (servicos[i].status == EM_VIAGEM) {
                        // EM_VIAGEM: só sinal
                        sigqueue(servicos[i].pid_veiculo, SIGUSR1, value);
                        cancelados++;
                    }
                    else if (servicos[i].status == AGENDADO || servicos[i].status == EM_ESPERA) {
                        // AGENDADO/EM_ESPERA: termina já
                        servicos[i].status = TERMINADO;
                        cancelados++;
                    }
                }

                rsp.status = 1;
                sprintf(rsp.msg, "[Controlador] %d servico(s) cancelado(s).", cancelados);

                printf("\n[CANCELAMENTO] Cliente PID %d cancelou %d servico(s)\n", p.pid, cancelados);
                printf("administrador@controlador:~$ ");
                fflush(stdout);
            }
            else {
                int idx = encontra_servico_index(id);

                if (idx == -1 || servicos[idx].pid_cliente != p.pid) {
                    rsp.status = 0;
                    strcpy(rsp.msg, "[Controlador] Servico nao encontrado.");
                } else {

                    if (servicos[idx].status == EM_VIAGEM) {
                        // EM_VIAGEM: só sinal
                        sigqueue(servicos[idx].pid_veiculo, SIGUSR1, value);
                        rsp.status = 1;
                        strcpy(rsp.msg, "[Controlador] Cancelamento enviado ao veiculo.");
                    }
                    else if (servicos[idx].status == AGENDADO || servicos[idx].status == EM_ESPERA) {
                        // AGENDADO/EM_ESPERA: termina já
                        servicos[idx].status = TERMINADO;
                        rsp.status = 1;
                        strcpy(rsp.msg, "[Controlador] Servico cancelado.");
                    }
                    else {
                        rsp.status = 0;
                        strcpy(rsp.msg, "[Controlador] Servico ja terminado.");
                    }

                    printf("\n[CANCELAMENTO] Servico %d cancelado (PID %d)\n", id, p.pid);
                    printf("administrador@controlador:~$ ");
                    fflush(stdout);
                }
            }

            pthread_mutex_unlock(&mux_dados);
            break;
        }

        case REQ_CONSULTAR: {
            int id_consulta = p.dados.id_consultar;
            pthread_mutex_lock(&mux_dados);

            int enviados = 0;
            for (int i = 0; i < num_servicos; i++)
            {
                if (servicos[i].pid_cliente == p.pid)
                {
                    rsp.status = 1;
                    rsp.id_servico = servicos[i].id;
                    if (servicos[i].status == AGENDADO)
                    {
                        int espera = servicos[i].hora_agendada - tempoAtual;
                        sprintf(rsp.msg,
                                "[Controlador] Servico %d: Agendado para t=%d (faltam %ds)",
                                servicos[i].id, servicos[i].hora_agendada, espera);
                    }
                    else if (servicos[i].status == EM_VIAGEM)
                    {
                        sprintf(rsp.msg,
                                "[Controlador] Servico %d: Em viagem (%d%% concluido)",
                                servicos[i].id, servicos[i].perc_concluida);
                    }
                    else if (servicos[i].status == EM_ESPERA)
                    {
                        sprintf(rsp.msg,
                        "[Controlador] Servico %d: Em espera (hora %d)",
                        servicos[i].id, servicos[i].hora_agendada);
                    }
                    else
                    {
                        sprintf(rsp.msg,
                                "[Controlador] Servico %d: Terminado",
                                servicos[i].id);
                    }
                    write(fd_cli, &rsp, sizeof(rsp));
                    enviados++;
                }
            }
            if (enviados == 0)
            {
                rsp.status = 1;
                rsp.id_servico = -1;
                strcpy(rsp.msg, "[Controlador] Nao tem servicos registados.");
                write(fd_cli, &rsp, sizeof(rsp));
            }

            pthread_mutex_unlock(&mux_dados);
            close(fd_cli);
            continue;
        }

        default:
            strcpy(rsp.msg, "[Controlador] Pedido invalido");
            break;
        }

        write(fd_cli, &rsp, sizeof(rsp));
        close(fd_cli);
    }

    close(fd);
    return NULL;
}

void handle_signal(int sig, siginfo_t *info, void *ctx) {

    if (sig == SIGINT) {
        terminar_sistema();
    }
}

int main(void) {
    setbuf(stdout, NULL);
    signal(SIGPIPE, SIG_IGN);

    char *env = getenv("NVEICULOS");
    if (!env) {
        fprintf(stderr, "ERRO: Variavel NVEICULOS nao definida.\n");
        fprintf(stderr, "Use: NVEICULOS=10 ./controlador\n");
        fprintf(stderr, "Ou:  make run\n");
        exit(1);
    }

    max_veiculos = atoi(env);

    if (access(PIPE_NAME, F_OK) == 0) {
        printf("Ja existe um controlador em execucao.\n");
        exit(1);
    }

    if (mkfifo(PIPE_NAME, 0666) == -1) {
        perror("mkfifo");
        exit(1);
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_flags = SA_SIGINFO;
    sa.sa_sigaction = handle_signal;
    sigaction(SIGINT,  &sa, NULL);

    pthread_t tid_tempo, tid_cli, tid_admin, tid_tel;

    pthread_create(&tid_tempo,  NULL, thread_tempo,    NULL);
    pthread_create(&tid_cli,    NULL, thread_cliente,  NULL);
    pthread_create(&tid_admin,  NULL, thread_admin,    NULL);
    pthread_create(&tid_tel,    NULL, thread_veiculo, NULL);

    pthread_join(tid_tempo, NULL);
    pthread_join(tid_tel,   NULL);
    pthread_join(tid_cli,   NULL);
    pthread_join(tid_admin, NULL);

    unlink(PIPE_NAME);
    return 0;
}