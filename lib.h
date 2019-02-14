#ifndef LIB
#define LIB

#define S 'S'
#define F 'F'
#define D 'D'
#define Z 'Z'
#define B 'B'
#define Y 'Y'
#define N 'N'

typedef struct {
    int len;
    char payload[1400];
} msg;


typedef struct {
	char SOH;
	char LEN;
	char SEQ;
	char TYPE;
	char* DATA;
	unsigned short CHECK;
	char MARK;
} Packet;

typedef struct {
	char MAXL;
	char TIME;
	char NPAD;
	char PADC;
	char EOL;
	char QCTL;
	char QBIN;
	char CHKT;
	char REPT;
	char CAPA;
	char R;
} SendInitPacket;

typedef struct {
	char SOH;
	char LEN;
	char SEQ;
	char TYPE;
	SendInitPacket DATA;
	unsigned short CHECK;
	char MARK;
} PacketS;

void init(char* remote, int remote_port);
void set_local_port(int port);
void set_remote(char* ip, int port);
int send_message(const msg* m);
int recv_message(msg* r);
msg* receive_message_timeout(int timeout); //timeout in milliseconds
unsigned short crc16_ccitt(const void *buf, int len);

	/* == general == */

void InitPacket(Packet* packet, char soh, char len, char seq, char type, char* data, char mark) {

	(*packet).SOH = soh;
	(*packet).LEN = len;
	(*packet).SEQ = seq;
	(*packet).TYPE = type;
	(*packet).DATA = data;
	(*packet).MARK = mark;
}


void InitSendPacket(SendInitPacket* S_Packet) {

	(*S_Packet).MAXL = 250;
	(*S_Packet).TIME = 5;
	(*S_Packet).NPAD = 0;
	(*S_Packet).PADC = 0;
	(*S_Packet).EOL = 0x0D;
	(*S_Packet).QCTL = 0;
	(*S_Packet).QBIN = 0;
	(*S_Packet).CHKT = 0;
	(*S_Packet).REPT = 0;
	(*S_Packet).CAPA = 0;
	(*S_Packet).R = 0;
}

void InitPacketS(PacketS* packet, char soh, char len, char seq, char type, SendInitPacket data, char mark) {

	(*packet).SOH = soh;
	(*packet).LEN = len;
	(*packet).SEQ = seq;
	(*packet).TYPE = type;
	(*packet).DATA = data;
	(*packet).MARK = mark;
}

	/* == sender == */

void InitMsg(msg* message, Packet packet, int len_data) {

	memcpy(&(*message).payload[0], &packet.SOH, 1);
	memcpy(&(*message).payload[1], &packet.LEN, 1);
	memcpy(&(*message).payload[2], &packet.SEQ, 1);
	memcpy(&(*message).payload[3], &packet.TYPE, 1);
	memcpy(&(*message).payload[4], packet.DATA, len_data);
}

void InitMsg_CHECK(msg* message, Packet packet, int len_data) {

	memcpy(&(*message).payload[4 + len_data], &packet.CHECK, 2);
	memcpy(&(*message).payload[4 + len_data + 2], &packet.MARK, 1);
}

void InitMsg_typeS(msg* message, int* SEQ, int* crc) {

	SendInitPacket packet;
	InitSendPacket(&packet);

	PacketS s_packet;
	InitPacketS(&s_packet, 0x01, sizeof(packet) + 5, *SEQ, S, packet, 0x0D);

	*crc = crc16_ccitt(&s_packet, sizeof(s_packet) - 4);
	s_packet.CHECK = *crc;
	//Copiem continutul in payload
	memcpy(message->payload, &s_packet, sizeof(s_packet));
	message->len = sizeof(s_packet);

}

void InitMsg_other_types(msg* message, int* SEQ, int* crc, char type, char* data, int len) {

	Packet packet;
	InitPacket(&packet, 0x01, len, *SEQ, type, data, 0x0D);

	//Mesaj nou
	if (data != NULL) {
		InitMsg(message, packet, strlen(data));
		*crc = crc16_ccitt(message->payload, strlen(data) + 4);
		packet.CHECK = *crc;
		InitMsg_CHECK(message, packet, strlen(data));
		message->len = strlen(data) + 7;
	}
	else {
		InitMsg(message, packet, 0);
		*crc = crc16_ccitt(message->payload, 4);
		packet.CHECK = *crc;
		InitMsg_CHECK(message, packet, 0);
		message->len = 7;
	}
}

void InitMsg_typeD(msg* message, int* SEQ, int* crc, char type, char* data, int len, int read_bytes) {

	Packet packet;
	InitPacket(&packet, 0x01, len, *SEQ, type, data, 0x0D);

	//Mesaj nou
	if (data != NULL) {
		InitMsg(message, packet, read_bytes);
		*crc = crc16_ccitt(message->payload, read_bytes + 4);
		packet.CHECK = *crc;
		InitMsg_CHECK(message, packet, read_bytes);
		message->len = read_bytes + 7;
	}
	else {
		InitMsg(message, packet, 0);
		*crc = crc16_ccitt(message->payload, 4);
		packet.CHECK = *crc;
		InitMsg_CHECK(message, packet, 0);
		message->len  = 7;
	}
}

int CheckAck(msg* message, int time, int* SEQ, int* crc, int len, char type, char* data, int n) {

	msg* aux;
	int count = 1;
	char type_received;

	while(1) {
		aux = receive_message_timeout(time * 1000);
		if (aux == NULL) {
			if (count <= 3) {
				printf("[SENDER] Timeout(%c)\n", type);
				send_message(message);
				count++;
			}
			else {
				return 0;
			}
		}
		else {
			type_received = aux->payload[3];
			count = 1;

			if (type_received == Y) {
				//ACK
				//Update SEQ
				*SEQ = (aux->payload[2] + 1) % 64;
				printf("[SENDER] ACK (%c) : SEQ becomes: %d:\n", type, *SEQ);
				break;
			}
			else if (type_received == N) {
				//NACK
				//Recalculam SEQ
				*SEQ = (*SEQ + 2) % 64;
				//Recalculam CRC
				if (type != D) {
					InitMsg_other_types(message, SEQ, crc, type, data, len);
				}
				else {
					InitMsg_typeD(message, SEQ, crc, type, data, len, n);
				}

				printf("[SENDER] RESEND (%c) : SEQ: %d\n", type, *SEQ);
				send_message(message);
			}
		}
	}

	return 1;
}

	/* == receiver == */

unsigned short CRC_typeS(msg* message) {

	unsigned short data_size = (*message).len - 9; //Din cauza structurii(padding) avem 2 biti suplimentari
	unsigned short message_crc;
	//Extragem cei doi bytes din campul CHECK
	//Offset cu 1 mai mare din cauza structurii(padding)
	unsigned char check1 = message->payload[5 + data_size];
	unsigned char check2 = message->payload[6 + data_size];

	message_crc = (check2 << 8) + check1;
	return message_crc;
}

unsigned short CRC_from_message(msg* message) {

	unsigned short data_size = (*message).len - 7;
	unsigned short message_crc;
	unsigned char check2 = (*message).payload[4 + data_size];
	unsigned char check1 = (*message).payload[5 + data_size];

	message_crc = (check1 << 8) + check2;
	return message_crc;
}

void InitPacket_recv(Packet* packet, char soh, char len, char seq, char type, char* data, unsigned short check, char mark) {

	(*packet).SOH = soh;
	(*packet).LEN = len;
	(*packet).SEQ = seq;
	(*packet).TYPE = type;
	(*packet).DATA = data;
	(*packet).CHECK = check;
	(*packet).MARK = mark;
}

void InitPacketS_recv(PacketS* packet, char soh, char len, char seq, char type, SendInitPacket data,unsigned short check, char mark) {

	(*packet).SOH = soh;
	(*packet).LEN = len;
	(*packet).SEQ = seq;
	(*packet).TYPE = type;
	(*packet).DATA = data;
	(*packet).CHECK = check;
	(*packet).MARK = mark;
}

void InitMsg_toSend(msg* message, int* SEQ, char type, unsigned short* crc) {

	Packet packet;
	InitPacket_recv(&packet, 0x01, 5, *SEQ, type, NULL, *crc, 0x0D0);
	memcpy(message->payload, &packet,sizeof(packet));
	message->len = 7;
}


#endif

