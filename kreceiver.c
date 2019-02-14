#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include "lib.h"

#define HOST "127.0.0.1"
#define PORT 10001

int main(int argc, char** argv) {

    msg *r, t;
    init(HOST, PORT);

    int count = 1;

    //Astept mesajul de tip S
    r = receive_message_timeout(5000);
    while (1) {
        if (r == NULL) {
            //Mesaj NULL
            //Incrementam contor
            if (count <= 3) {
                printf("[RECEIVER] Timeout (S)\n");
                count++;
                r = receive_message_timeout(5000);
            }
            else {
                return 0;
            }
        }
        else {
            break;
        }
    }

    int time = 0;
    int SEQ = 1;
    unsigned short crc = crc16_ccitt(r->payload, r->len - 4);
    unsigned short r_crc = CRC_typeS(r);

    SendInitPacket s;
    InitSendPacket(&s);
    PacketS m;
    //Daca mesajul este eronat, trimitem NACK
    if (crc != r_crc) {
        InitMsg_toSend(&t, &SEQ, N, &crc);
        send_message(&t);

        //Astept un nou packet de tip S
        count = 1;

        while (1) {
            msg* y = receive_message_timeout(5000);
            if (y == NULL) {
                //Timeout: asteptam de 3 ori
                if (count <= 3) {
                    send_message(&t);
                    printf("[RECEIVER] Timeout\n");
                    count++;
                }
                else {
                    return 0;
                }
            }
            else {
                count = 1; //resetam count
                //Calculam numarul de secventa pentru urmatorul mesaj de trimis
                SEQ = (SEQ + 2) % 64;
                //Calculam noul crc pentru mesajul primit
                crc = crc16_ccitt(y->payload, y->len -4);
                unsigned short y_crc = CRC_typeS(y);
                printf("[RECEIVER] (S) SEND NACK : SEQ: %d\n", SEQ);

                if (crc == y_crc) {
                    //Mesaj primit corect. Retinem numarul de secventa si timpul trasmis de sender
                    SEQ = (y->payload[2] + 1) % 64;
                    time = (int) y->payload[5];
                    break;
                }

                //Construim mesaj de tip NACK
                InitMsg_toSend(&t, &SEQ, N, &crc);
                send_message(&t);
            }
        }
    }

    //Timpul trimis de sender
    time = (int) r->payload[5];

    //Construim mesaj de tip ACK
    InitPacketS_recv(&m, 0x01, sizeof(s) + 5, SEQ, Y, s, crc, 0x0D);
    printf("[RECEIVER] SEND ACK (S) : SEQ: %d\n", m.SEQ);
    memcpy(t.payload, &m, sizeof(m));
    t.len = sizeof(s) + 7;
    send_message(&t);


    //Asteptam urmatorul mesaj
    while (1) {
        r = receive_message_timeout(time * 1000);
        if (r == NULL) {
            if (count <= 3) {
                printf("[RECEIVER] Timeout\n");
                count++;
            }
            else {
                return 0;
            }
        }
        else {
            //Am primit mesaj nou. Verificam corectitudinea si tipul
            count = 1;
            unsigned short data_size = r->len - 7;
            crc = crc16_ccitt(r->payload, r->len -3);
            r_crc = CRC_from_message(r);

            int ok = 0;
            char* file_name;
            int okData = 0;
            int fd;
            char* buffer;
            char type = r->payload[3];
            int seq_primit = r->payload[2];

            if (crc != r_crc) {
                //Mesaj eronat
                //Construim mesajul NACK
                SEQ = (SEQ + 2) % 64;
                printf("[RECEIVER] SEND NACK : SEQ: %d\n", SEQ);
                InitMsg_toSend(&t, &SEQ, N, &crc);
                send_message(&t);

                //Asteptam un mesaj corect
                int count_2 = 1;
                while (1) {
                    msg* y = receive_message_timeout(5000);
                    if (y == NULL) {
                        if (count_2 <= 3) {
                            printf("[RECEIVER] Timeout\n");
                            count_2++;
                        }
                        else {
                            return 0;
                        }
                    }
                    else {
                        count_2 = 1;
                        //Verificam crc mesaj nou
                        crc = crc16_ccitt(y->payload, y->len -3);
                        unsigned short y_crc = CRC_from_message(y);
                        SEQ = (SEQ + 2) % 64;

                        if (crc == y_crc) {
                            //Mesaj corect
                            SEQ = (y->payload[2] + 1) % 64;
                            data_size = r->len - 7;
                            type = y->payload[3];
                            seq_primit = r->payload[2];

                            if (type == F) {
                                //Pachet F
                                file_name = malloc(5 + data_size);
                                strcpy(file_name, "recv_");
                                strncat(file_name, &y->payload[4], data_size);
                                ok = 1;
                            }
                            else if (type == D) {
                                //Pachet D
                                buffer = calloc(data_size, sizeof(char));
                                //Copiem datele in buffer
                                memcpy(buffer, &y->payload[4], data_size);
                                okData = 1;
                            }

                            break;
                        }

                        //Construim mesajul de transmis de tip NACK
                        printf("[RECEIVER] RESEND NACK : SEQ: %d\n", SEQ);
                        InitMsg_toSend(&t, &SEQ, N, &crc);
                        send_message(&t);
                    }
                }
            }

            if (ok == 0 && okData == 0) {
                //Mesaj corect
                SEQ = (r->payload[2] + 1) % 64;
            }

            if (type == Z) {
                close(fd);
            }
            if (type == B) {
                break;
            }
            if (seq_primit < (SEQ - 1) % 64) {
                //Pentru a nu avea mesaje duplicate
                continue;
            }
             if (type == F && ok == 0){
                //Pachet primit corect
                file_name = malloc(5 + data_size);
                strcpy(file_name, "recv_");
                strncat(file_name, &r->payload[4], data_size);
                SEQ = (r->payload[2] + 1) % 64;
            }
            if(type == F){
                //Deschidem fisierul
                fd = open(file_name, O_WRONLY | O_CREAT, 0777);
            }
            else if (type == D) {
                if (okData == 0) {
                    buffer = calloc(data_size, sizeof(char));
                    memcpy(buffer, &r->payload[4], data_size);
                    SEQ = (r->payload[2] + 1) % 64;
                }
                //Scirem datele din buffer in fisier
                write(fd, buffer, data_size);
                memset(buffer, 0, data_size);
            }

            printf("[RECEIVER] SEND ACK (%c) : SEQ: %d\n", type, SEQ);
            //ACK
            InitMsg_toSend(&t, &SEQ, Y, &crc);
            send_message(&t);
        }
    }

    //Trimitem ACK pentru pachetul primit
    printf("[RECEIVER] ACK (B) : SEQ: %d\n", SEQ);
    InitMsg_toSend(&t, &SEQ, Y, &crc);
    send_message(&t);
    return 0;

}
