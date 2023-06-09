#if !defined(__MESSAGE_H__)
#define __MESSAGE_H__

#define MESSAGE_SIZE_BYTES 67  // 67 bytes.
#define PACKET_SIZE_BYTES (MESSAGE_SIZE_BYTES + 14)

#define START_FRAME_DELIMITER_BITFIELD_SIZE 8  // 8 bits.
#define LENGTH_BITFIELD_SIZE 6                 // 6 bits.
#define SEQUENCE_BITFIELD_SIZE 6               // 6 bits.
#define SEQ_MAX 63                             // (2^SEQUENCE_BITFIELD_SIZE -1)
#define TYPE_BITFIELD_SIZE 4                   // 4 bits.
#define DATA_MAX_SIZE 504                      // 504 bits. 63 bytes.
#define DATA_MAX_SIZE_BYTES 63                 // 504 bits. 63 bytes.
#define PARITY_BITFIELD_SIZE 8                 // 8 bits.

#define NEXT_SEQUENCE(seq) ((seq + 1) % (SEQ_MAX + 1))
#define PREV_SEQUENCE(seq) ((seq + 63) % (SEQ_MAX + 1))
#define PREV_MULT_SEQUENCE(seq, qnt) ((seq + 63 - qnt) % (SEQ_MAX + 1))

typedef struct message_t {
    unsigned char start_frame_delimiter;
    struct __attribute__((__packed__)) {
        unsigned char type : TYPE_BITFIELD_SIZE;
        unsigned char sequence : SEQUENCE_BITFIELD_SIZE;
        unsigned char length : LENGTH_BITFIELD_SIZE;
    };
    unsigned char data[DATA_MAX_SIZE_BYTES];
    unsigned char parity;
} message_t;

#define START_FRAME_DELIMITER 0b01111110

// MESSAGE CODES

#define C_BACKUP_FILE 0b0000
#define C_BACKUP_GROUP 0b0001
#define C_RECOVER_FILE 0b0010
#define C_RECOVER_GROUP 0b0011
#define C_CD_SERVER 0b0100
#define C_VERIFY 0b0101
#define C_FILE_NAME 0b0110
#define C_MD5 0b0111
#define C_DATA 0b1000
#define C_END_OF_FILE 0b1001
#define C_END_OF_GROUP 0b1010
#define C_METAMESSAGE 0b1011
#define C_ERROR 0b1100
#define C_OK 0b1101
#define C_ACK 0b1110
#define C_NACK 0b1111

typedef struct messageACK_NACK {
    unsigned char start_frame_delimiter;
    struct __attribute__((__packed__)) {
        unsigned char type : TYPE_BITFIELD_SIZE;
        unsigned char sequence : SEQUENCE_BITFIELD_SIZE;
        unsigned char length : LENGTH_BITFIELD_SIZE;
    };
    unsigned char code;
} ACK_NACK_CODE;

// ERROR CODES

typedef struct messageError {
    unsigned char start_frame_delimiter;
    struct __attribute__((__packed__)) {
        unsigned char type : TYPE_BITFIELD_SIZE;
        unsigned char sequence : SEQUENCE_BITFIELD_SIZE;
        unsigned char length : LENGTH_BITFIELD_SIZE;
    };
    unsigned char errorCode;
    int extraInfo;
    int errnoCode;
} messageError;

#define DISK_FULL 0
#define NO_WRITE_PERMISSION 1
#define FILE_NOT_FOUND 2
#define NO_READ_PERMISSION 3
#define BUFFER_FULL 4
#define CHECK_ERRNO 5

enum RECEIVE_MESSAGE_ERROR_STATUS {
    RECVM_STATUS_PARITY_ERROR = -1,  // Algo deu errado, pode crashar.
    RECVM_STATUS_PACKET_LOSS = -3,   // O outro lado não recebeu a sua resposta. Reenvie-a.
    RECVM_STATUS_ERROR = -4,         // A paridade está errada, envie NACK.
    RECVM_TIMEOUT = -5
};

// Inicializa a estrutura do pacote e da mensagem.
// O buffer do pacote deve ser ter, no mínimo, tamanho PACKET_SIZE_BYTES.
message_t *init_message(void *packet_buffer);

char *message_type_str(unsigned char type_code);
char isCommandMessageType(unsigned char type_code);

int send_message(int socket, message_t *message);
int send_nack(int socket, message_t *messageA, message_t *messageR);
int receive_message(int socket, message_t *message, unsigned int timeoutSec);

unsigned char message_parity(message_t *message);

void printMessage(message_t *message);
void prinfhexMessage(message_t *message);

void *packetPtr_from_message(message_t *message);

void flush_recv_queue(int socket);

#endif  // __MESSAGE_H__
