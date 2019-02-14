#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "lib.h"

#define HOST "127.0.0.1"
#define PORT 10000

int main(int argc, char** argv) {

    msg t;
    init(HOST, PORT);

    int time, crc;
    int SEQ = 0;
    char type;
    unsigned char maxl;


    //Mesajul de tip S
    InitMsg_typeS(&t, &SEQ, &crc);
    send_message(&t);
    printf("[SENDER] SEND (S) : SEQ: %d\n", SEQ);

    int count = 1;

    while (1) {
        msg* y = receive_message_timeout(5000);
        //Mesaj NULL
        if ( y == NULL) {
            //Verificam daca trebuie inchisa conexiunea
            if (count <= 3) {
                send_message(&t);
                count++;
                printf("[SENDER] Timeout (S)\n");
            }
            else {
                //Oprim tot
                return 0;
            }
        }
        else {
            //Verificam type-ul mesajului primit
            type = y->payload[3];
            count = 1;
            if (type == Y) {
                //ACK
                //Extragem datele trimise de receiver
                maxl = (unsigned char) y->payload[4];
                time = (int) y->payload[5];
                //Recalculam numarul de secventa pentru urmatoarele transmisii
                SEQ = (y->payload[2] + 1) % 64;
                printf("[SENDER] ACK (S)\n");
                break;
            }
            else if (type == N) {
                //NACK
                //Calculam noul numar de secventa
                SEQ = (SEQ + 2) % 64;
                //Retrimitem mesajul
                printf("[SENDER] RESEND (S) : SEQ: %d\n", SEQ);
                InitMsg_typeS(&t, &SEQ, &crc);
                send_message(&t);
            }
        }
    }

    //Pentru fiecare fisier
    int i;
    for (i = 1; i < argc; i++) {
        //Pachetul F
        //In campul data avem numele fisierului
        int len = strlen(argv[i]) + 5;
        InitMsg_other_types(&t, &SEQ, &crc, F, argv[i], len);
        printf("[SENDER] File Header (%s) : SEQ: %d\n", argv[i], SEQ);
        send_message(&t);
        count = 1;

        if (CheckAck(&t, time, &SEQ, &crc, len, F, argv[i], 0) == 0) {
            return 0;
        }

        //Deschidem fisierul si incepem citirea datelor
        int fd = open(argv[i], O_RDONLY, 0777);
        char buf[maxl];
        int n;

        //Cat timp mai pot sa citesc din fisier
        while ((n = read(fd, buf, maxl)) > 0) { //n reprezinta numarul de bytes cititi
            len = 5 + n;
            InitMsg_typeD(&t, &SEQ, &crc, D, buf, len, n);
            send_message(&t);
            printf("[SENDER] SEND DATA : SEQ: %d\n", SEQ);
            //Confirmare ACK
            if (CheckAck(&t, time, &SEQ, &crc, len, D, buf, n) == 0) {
                return 0;
            }

            //Ne asiguram ca nu mai raman resturi in buf folosind memset
            memset(buf, 0, maxl);
            memset(t.payload, 0, t.len);
        }

        //Pachetul Z pentru a anunta ca am trimis un fisier integral si urmeaza altul

        len = 7;
        InitMsg_other_types(&t, &SEQ, &crc, Z, NULL, len);
        send_message(&t);
        count = 1;

        //Verificam ACK
        if (CheckAck(&t, time, &SEQ, &crc, len, Z, argv[i], 0) == 0) {
            return 0;
        }

        close(fd);
    }

    int len = 7;
    InitMsg_other_types(&t, &SEQ, &crc, B, NULL, len);
    send_message(&t);
    count = 1;
    //Confirmam ACK
    if (CheckAck(&t, time, &SEQ, &crc, len, B, NULL, 0) == 0) {
        return 0;
    }

    return 0;
}
