#include <arpa/inet.h>
#include <errno.h>
#include <math.h>
#include <openssl/md5.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "ConexaoRawSocket.h"
#include "connectionManager.h"
#include "files.h"
#include "message.h"
#include "pilha.h"
#include "tokenlizer.h"

#define BUFFER_SIZE (64 * 1024 * 1024)

#ifndef NETINTERFACE
#error NETINTERFACE não está definido. Use "make [interface de rede]"
//   definição do macro só pro editor de texto saber que é um macro
#define NETINTERFACE "ERROR"
#endif

enum comandos {
    BACKUP = 14,
    REC = 116,
    CDSV = 2,
    VERIFICAR = 119,
    CD = 7
};

enum estados {
    PROMPT_DE_COMANDO,
    ENVIANDO_BACKUP_FILE,
    EXECUTANDO_BACKUP,
    EXECUTANDO_REC,
    RECEBENDO_REC_FILE,
    ESPERANDO_MD5,  // Comando de verificação
    CD_CLIENT = 100,
    CD_SERVER,
    ERRO,
    EXIT
};

unsigned long hash_function(char *str) {
    unsigned long hash_value = 0;
    for (int j = 0; str[j] != '\0'; j++)
        hash_value ^= (unsigned long)str[j];
    return hash_value;
}

void printModoDeUso(void) {
    printf("Modo de Uso:\n\t$ client [backup|rec|cdsv|verificar|cd] [arquivos]\n");
}

int is_regular_file(const char *path) {
    struct stat path_stat;
    stat(path, &path_stat);
    return S_ISREG(path_stat.st_mode);
}

int identifica_comando(char *argv[], int argc) {
    int comando = -1;

    switch (hash_function(argv[0])) {
        case BACKUP:
            if (argc == 2)
                comando = C_BACKUP_FILE;
            else if (argc > 2)
                comando = C_BACKUP_GROUP;
            else
                comando = -1;
            break;

        case REC:
            if (argc == 2)
                comando = C_RECOVER_FILE;
            else if (argc > 2)
                comando = C_RECOVER_GROUP;
            else
                comando = -1;
            break;

        case CDSV:
            // Deve conter o comando e apenas um diretório
            if (argc == 2)
                comando = C_CD_SERVER;
            else
                comando = -1;
            break;

        case VERIFICAR:
            comando = C_VERIFY;
            break;

        case CD:
            comando = CD_CLIENT;
            break;

        default:
            comando = -1;
            break;
    }

    return comando;
}

void libera_e_sai(int exitCode, void *freeHeap) {
    void *ptr = NULL;

    while (desempilhar((type_pilha *)freeHeap, &ptr)) {
        free(ptr);
    }
    destruir_pilha((type_pilha *)freeHeap);

    if (exitCode == -1) {
        perror("Ocorreu um erro:\n");
    }
    fprintf(stderr, "\t%s\n", strerror(errno));
}

int interpreta_comando(int comando) {
    switch (comando) {
        case C_BACKUP_FILE:
        case C_BACKUP_GROUP:
            return EXECUTANDO_BACKUP;

        case C_RECOVER_FILE:
        case C_RECOVER_GROUP:
            return EXECUTANDO_REC;

        case C_CD_SERVER:
            return CD_SERVER;

        case C_VERIFY:
            return ERRO;

        case CD_CLIENT:
            return CD_CLIENT;

        default:
            return ERRO;
    }  // switch (comando)
}

int le_comando(int *argc, char *argv[]) {
    char buf[4096];
    size_t len;

    fprintf(stdout, "%s >>> ", getcwd(buf, 4096));
    fgets(buf, 4096, stdin);
    len = strlen(buf);
    if (len > 0 && buf[len - 1] == '\n')
        buf[--len] = '\0';
    if (tokenlize(buf, argc, argv)) {
        exit(-1);
    }

    return identifica_comando(argv, *argc);
}

int main(void) {
    type_pilha *freeHeap = criar_pilha(sizeof(void *));
    on_exit(libera_e_sai, freeHeap);

    // if (argc <= 1) {
    //     printModoDeUso();
    //     exit(0);
    // }

    int socket = ConexaoRawSocket(NETINTERFACE);

    void *packets_buffer = malloc(PACKET_SIZE_BYTES * sizeof(unsigned char));
    empilhar(freeHeap, packets_buffer);
    message_t *messageR = init_message(packets_buffer);  // Você recebe uma resposta. (Pacote recebido)

    int argc;
    char *argv[4096];
    FILE *curr_file = NULL;
    unsigned char *buffer = malloc(BUFFER_SIZE * sizeof(unsigned char));
    empilhar(freeHeap, buffer);
    if (buffer == NULL) {
        printf("Falha ao alocar buffer de arquivo.\n");
        exit(-1);
    }
    int qnt_arquivos = 0;
    int arquivos_processados = 0;
    int estado = PROMPT_DE_COMANDO;
    int comando;
    char error = 0;
    int count = 0;

    unsigned char type;
    int bytes;

    for (;;) {
        switch (estado) {
            // ==
            case PROMPT_DE_COMANDO:
                comando = le_comando(&argc, argv);
                if (comando == -1) {
                    printModoDeUso();
                    break;
                }

                estado = interpreta_comando(comando);
                count = 0;

                break;

            // ==
            case EXECUTANDO_BACKUP:
                estado = ERRO;
                if (count == 0) {
                    // Veio do estado PROMPT_DE_COMANDO para esse
                    count++;
                    arquivos_processados = 0;
                    qnt_arquivos = argc - 1;
                    for (int i = 1; i < argc; i++) {
                        if (access(argv[i], R_OK) != 0) {
                            printf("  \"%s\" \033[1;31m(Arquivo inexistante ou sem permissão de leitura)\033[0m\n", argv[i]);
                            estado = PROMPT_DE_COMANDO;
                        } else if (!is_regular_file(argv[i])) {
                            printf("  \"%s\" \033[1;31m(Arquivo não é um arquivo regular)\033[0m\n", argv[i]);
                            estado = PROMPT_DE_COMANDO;
                        } else {
                            printf("  \"%s\"\n", argv[i]);
                        }
                    }
                    // No caso de um erro ter ocorrido:
                    if (estado == PROMPT_DE_COMANDO) {
                        printf("\033[1;31mOperação abortada.\033[0m\n");
                        break;
                    }
                }

                if (arquivos_processados >= qnt_arquivos) {
                    estado = PROMPT_DE_COMANDO;
                    continue;
                }
                char *filename = argv[arquivos_processados + 1];
                if (open_file(&curr_file, filename) == 0)
                    if (cm_send_message(socket, filename, strlen(filename) + sizeof((char)'\0'), C_BACKUP_FILE, messageR) != -1)
                        if (cm_receive_message(socket, &error, 1, &type) != -1)
                            if (type == C_OK)
                                estado = ENVIANDO_BACKUP_FILE;

                break;

            // ==
            case ENVIANDO_BACKUP_FILE:
                estado = ERRO;
                if (feof(curr_file)) {
                    if (cm_send_message(socket, buffer, 0, C_END_OF_FILE, messageR) != -1)
                        if (cm_receive_message(socket, &error, 1, &type) != -1)
                            if (type == C_OK) {  // Enviou todo o arquivo.
                                estado = EXECUTANDO_BACKUP;
                                arquivos_processados += 1;
                                fprintf(stdout, "\033[%dA\033[0K\r", qnt_arquivos - arquivos_processados + 1);
                                fprintf(stdout, "  \"%s\" \033[0;32m(Enviado)", argv[arquivos_processados]);
                                fprintf(stdout, "\033[%dB\033[0K\r\033[0m", qnt_arquivos - arquivos_processados + 1);
                                fflush(stdout);
                            }
                    break;
                }

                bytes = fread(buffer, 1, BUFFER_SIZE, curr_file);
                int offset = 0;
                for (;;) {
                    if (cm_send_message(socket, buffer + offset, bytes, C_DATA, messageR) != -1) {
                        estado = ENVIANDO_BACKUP_FILE;
                        break;
                    } else {
                        messageError *messageErr = (messageError *)messageR;
                        if (messageErr->type == C_ERROR && messageErr->errorCode == BUFFER_FULL) {
                            offset += messageErr->extraInfo;
                            bytes -= messageErr->extraInfo;
                        } else {
                            printf("type: %d e extra: %d\n", messageErr->type, messageErr->errorCode);
                            estado = ERRO;
                            break;
                        }
                    }
                }

                if (estado == ERRO) {
                    arquivos_processados += 1;
                    fprintf(stdout, "\033[%dA\033[0K\r", qnt_arquivos - arquivos_processados + 1);
                    fprintf(stdout, "  \"%s\" \033[1;31m(ERRO)", argv[arquivos_processados]);
                    fprintf(stdout, "\033[%dB\033[0K\r\033[0m", qnt_arquivos - arquivos_processados + 1);
                    fflush(stdout);
                }
                break;

            // ==
            case EXECUTANDO_REC:
                if (count == 0) {
                    // Veio do estado PROMPT_DE_COMANDO para esse
                    count++;
                    estado = ERRO;
                    // Verifica se os arquivos existem no servidor
                    // Ou se algum erro de leitura ocorre
                    buffer[0] = (unsigned char)'\0';
                    for (int i = 1; i < argc; i++) {
                        strcat((char *)buffer, " ");
                        strcat((char *)buffer, argv[i]);
                    }
                    if (cm_send_message(socket, buffer, strlen((char *)buffer) + sizeof((char)'\0'), C_RECOVER_FILE, messageR) != -1)
                        if (cm_receive_message(socket, buffer, sizeof(int), &type) != -1) {
                            if (type == C_OK) {
                                arquivos_processados = 0;
                                qnt_arquivos = (int)(buffer[0]);
                                estado = RECEBENDO_REC_FILE;
                            } else if (type == C_ERROR) {
                                if (((messageError *)buffer)->errorCode == NO_READ_PERMISSION)
                                    fprintf(stderr, "O servidor não tem permissão de leitura de um dos arquivos requesitados.\n");
                                else if (((messageError *)buffer)->errorCode == FILE_NOT_FOUND)
                                    fprintf(stderr, "Um ou mais dos arquivos requisitados não existem.\n");
                                else if (((messageError *)buffer)->errorCode == CHECK_ERRNO) {
                                    errno = ((messageError *)buffer)->errnoCode;
                                }

                                break;
                            }
                        }
                }

                // Todos os arquivos já foram recebidos
                if (arquivos_processados >= qnt_arquivos) {
                    estado = PROMPT_DE_COMANDO;
                    continue;
                }

                // Cliente está pronto para receber um arquivo
                if (cm_send_message(socket, buffer, 0, C_OK, messageR) == -1)
                    exit(-1);

                // Recebe o nome de um arquivo
                if ((bytes = cm_receive_message(socket, buffer, BUFFER_SIZE, &type)) == -1)
                    exit(-1);
                if (type != C_FILE_NAME) {
                    fprintf(stderr, "Cliente recebeu pacote do tipo errado. Esperado: C_FILE_NAME\n");
                    exit(-1);
                }
                filename = (char *)buffer;

                // Abre esse arquivo
                // e manda uma resposta confirmando
                printf("Iniciando a recuperação do arquivo \"%s\".\n", filename);
                curr_file = fopen(filename, "w+");
                if (curr_file == NULL) {
                    printf("Falha ao abrir o arquivo. Recuperação abortada.\n");
                    estado = ERRO;
                    type = C_ERROR;
                    bytes = 2;
                    ((messageError *)buffer)->errorCode = CHECK_ERRNO;
                    ((messageError *)buffer)->errnoCode = errno;
                } else {
                    estado = RECEBENDO_REC_FILE;
                    type = C_OK;
                    bytes = 0;
                }
                if (cm_send_message(socket, buffer, bytes, type, messageR) == -1)
                    exit(-1);

                break;

            // ==
            case RECEBENDO_REC_FILE:
                if ((bytes = cm_receive_message(socket, buffer, BUFFER_SIZE, &type)) == -1)
                    exit(-1);
                switch (type) {
                    case C_DATA:
                        fwrite(buffer, 1, bytes, curr_file);
                        estado = RECEBENDO_REC_FILE;
                        break;
                    case C_END_OF_FILE:
                        fclose(curr_file);
                        if (cm_send_message(socket, buffer, 0, C_OK, messageR) == -1)
                            exit(-1);
                        printf("Recuperação Concluida.\n");
                        arquivos_processados += 1;
                        estado = EXECUTANDO_REC;
                        break;
                    default:
                        estado = ERRO;
                        break;
                }       // switch (typeR)
                break;  // case RECEBENDO_REC_FILE

            // ==
            case ESPERANDO_MD5:
                break;

            // ==
            case CD_CLIENT:
                printf("Mudando para o diretório: %s\n", argv[argc - 1]);
                if (chdir(argv[argc - 1]))
                    estado = ERRO;
                else
                    estado = PROMPT_DE_COMANDO;
                break;

            // ==
            case CD_SERVER:
                // Envia o caminho do diretório para o server.
                estado = ERRO;
                if (cm_send_message(socket, argv[argc - 1], strlen(argv[argc - 1]) + sizeof((char)'\0'), C_CD_SERVER, messageR) != -1)
                    if (cm_receive_message(socket, buffer, 2, &type) != -1) {
                        if (type == C_OK) {
                            printf("Diretório alterado para: \"%s\"\n", argv[argc - 1]);
                            estado = PROMPT_DE_COMANDO;
                        } else if (type == C_ERROR && ((messageError *)buffer)->errorCode == CHECK_ERRNO)
                            fprintf(stderr, "Ocorreu um erro no servidor.\n\t%s\n", strerror(((messageError *)buffer)->errnoCode));
                    }

                break;

            // ==
            case EXIT:
                exit(0);

            // ==
            case ERRO:
            default:
                fprintf(stderr, "Ocorreu um erro.\n\t%s\n", strerror(errno));
                estado = PROMPT_DE_COMANDO;
                continue;
        }  // switch(estado)

    }  // for(;;)

    exit(0);
}
