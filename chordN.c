#define _CRT_SECURE_NO_WARNINGS
#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <winsock2.h>
#include <string.h>
#include <windows.h>
#include <process.h> 

#define FNameMax 32             /* Max length of File Name */
#define FileMax  32            /* Max number of Files */
#define baseM    6            /* base number */
#define ringSize 64            /* ringSize = 2^baseM */
#define fBufSize 1024         /* file buffer size */

typedef struct {                /* Node Info Type Structure */
	int ID;                     /* ID */
	struct sockaddr_in addrInfo;/* Socket address */
} nodeInfoType;

typedef struct {            /* File Type Structure */
	char Name[FNameMax];       /* File Name */
	int  Key;               /* File Key */
	nodeInfoType owner;         /* Owner's Node */
	nodeInfoType refOwner;      /* Ref Owner's Node */
} fileRefType;

typedef struct {               /* Global Information of Current Files */
	unsigned int fileNum;         /* Number of files */
	fileRefType  fileRef[FileMax];   /* The Number of Current Files */
} fileInfoType;

typedef struct {             /* Finger Table Structure */
	nodeInfoType Pre;          /* Predecessor pointer */
	nodeInfoType finger[baseM];   /* Fingers (array of pointers) */
} fingerInfoType;

typedef struct {                /* Chord Information Structure */
	fileInfoType   FRefInfo;   /* File Ref Own Information */
	fingerInfoType fingerInfo;   /* Finger Table Information */
} chordInfoType;

typedef struct {            /* Node Structure */
	nodeInfoType  nodeInfo;     /* Node's IPv4 Address */
	fileInfoType  fileInfo;     /* File Own Information */
	chordInfoType chordInfo;    /* Chord Data Information */
} nodeType;

typedef struct {
	unsigned short msgID;      // message ID
	unsigned short msgType;      // message type (0: request, 1: response)
	nodeInfoType   nodeInfo;   // node address info 
	short          moreInfo;   // more info 
	fileRefType    fileInfo;   // file (reference) info
	unsigned int   bodySize;   // body size in Bytes
} chordHeaderType;             // CHORD message header type

void ErrorHandling(char * msg);

void procRecvMsg(void *);
// thread function for handling receiving messages 

void procPPandFF(void *);
// thread function for sending ping messages and fixfinger 

int recvn(SOCKET s, char *buf, int len, int flags);
// For receiving a file

unsigned strHash(const char *);
// A Simple Hash Function from a string to the ID/key space

int twoPow(int power);
// For getting a power of 2 

int modMinus(int modN, int minuend, int subtrand);
// For modN modular operation of "minend - subtrand"

int modPlus(int modN, int addend1, int addend2);
// For modN modular operation of "addend1 + addend2"

int modIn(int modN, int targNum, int range1, int range2, int leftmode, int rightmode);
// For checking if targNum is "in" the range using left and right modes 
// under modN modular environment 

char *fgetsCleanup(char *);
// For handling fgets function

void flushStdin(void);
// For flushing stdin

void showCommand(void);
// For showing commands

void stabilizeL(SOCKET socket, int leaveID);
void stabilizeJ(SOCKET socket);
int move_keys();
int fix_finger();

void notify(nodeInfoType targetNode);


nodeInfoType find_successor(int IDKey);
// For finding successor of IDKey for a node curNode

nodeInfoType find_predecessor(int IDKey);
// For finding predecessor of IDKey for a node curNode

nodeInfoType find_closest_predecessor(nodeType curNode, int IDKey);
// For finding closest predecessor of IDKey for a node curNode

int lookup();

nodeType myNode = { 0 };               // node information -> global variable
SOCKET rqSock, rpSock, flSock, frSock, fsSock, pfSock;
HANDLE hMutex;
nodeInfoType init = { 0 };

int sMode = 1; // silent mode

void err_quit(char *msg)
{
	LPVOID lpMsgBuf;
	FormatMessage(
		FORMAT_MESSAGE_ALLOCATE_BUFFER |
		FORMAT_MESSAGE_FROM_SYSTEM,
		NULL, WSAGetLastError(),
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPTSTR)&lpMsgBuf, 0, NULL);
	MessageBox(NULL, (LPCTSTR)lpMsgBuf, (LPCTSTR)msg, MB_ICONERROR);
	LocalFree(lpMsgBuf);
	exit(-1);
}

void err_display(char *msg)
{
	LPVOID lpMsgBuf;
	FormatMessage(
		FORMAT_MESSAGE_ALLOCATE_BUFFER |
		FORMAT_MESSAGE_FROM_SYSTEM,
		NULL, WSAGetLastError(),
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPTSTR)&lpMsgBuf, 0, NULL);
	printf("[%s] %s", msg, (LPCTSTR)lpMsgBuf);
	LocalFree(lpMsgBuf);
}

int main(int argc, char *argv[])
{
	WSADATA wsaData;
	HANDLE hThread[2];
	int exitFlag = 0; // indicates termination condition
	char command[7];
	char cmdChar = '\0';
	int joinFlag = 0; // indicates the join/create status
	char tempIP[16];
	char tempPort[6];
	char fileName[FNameMax + 1];
	char fileBuf[fBufSize];
	char strSockAddr[21];
	int keyCount;
	struct sockaddr_in peerAddr, targetAddr;
	chordHeaderType tempMsg, bufMsg;
	int optVal = 5000;  // 5 seconds
	int retVal; // return value
	nodeInfoType succNode = { 0 }, predNode = { 0 }, targetNode = { 0 };
	fileInfoType keysInfo;
	fileRefType refInfo;
	FILE *fp = NULL;
	char* body = NULL;
	struct sockaddr_in clientAddr;
	int i, j, k, targetKey, addrSize, fileSize, numTotal, searchResult, resultFlag;
	int servAddrLen;//서버 주소의 길이
	int targAddrLen; //타겟 주소의 길이
	int peerAddrLen; //피어 주소의 길이

	/* step 0: Program Initialization  */
	/* step 1: Commnad line argument handling  */
	/* step 2: Winsock handling */
	/* step 3: Prompt handling (loop) */
	/* step 4: User input processing (switch) */
	/* step 5: Program termination */

	/* step 0 */

	printf("*****************************************************************\n");
	printf("*         DHT-Based P2P Protocol (CHORD) Node Controller        *\n");
	printf("*                  Ver. 0.5     Oct. 17, 2016                   *\n");
	printf("*                       (c) Kim, Tae-Hyong                      *\n");
	printf("*****************************************************************\n\n");

	/* step 1: Commnad line argument handling  */

	myNode.nodeInfo.addrInfo.sin_family = AF_INET;

	if (argc != 3){
		printf("\a[ERROR] Usage : %s <IP Addr> <Port No(49152~65535)>\n", argv[0]);
		exit(1);
	}

	if ((myNode.nodeInfo.addrInfo.sin_addr.s_addr = inet_addr(argv[1])) == INADDR_NONE) {
		printf("\a[ERROR] <IP Addr> is wrong!\n");
		exit(1);
	}

	if (atoi(argv[2]) > 65535 || atoi(argv[2]) < 49152) {
		printf("\a[ERROR] <Port No> should be in [49152, 65535]!\n");
		exit(1);
	}
	myNode.nodeInfo.addrInfo.sin_port = htons(atoi(argv[2]));

	strcpy(strSockAddr, argv[2]);
	strcat(strSockAddr, argv[1]);
	printf("strSoclAddr: %s\n", strSockAddr);
	myNode.nodeInfo.ID = strHash(strSockAddr);


	printf(">>> Welcome to ChordNode Program! \n");
	printf(">>> Your IP address: %s, Port No: %d, ID: %d \n", argv[1], atoi(argv[2]), myNode.nodeInfo.ID);
	printf(">>> Silent Mode is ON!\n\n");

	/* step 2: Winsock handling */

	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) { /* Load Winsock 2.2 DLL */
		printf("\a[ERROR] WSAStartup() error!");
		exit(1);
	}
	hMutex = CreateMutex(NULL, FALSE, NULL);
	if (hMutex == NULL) {
		printf("\a[ERROR] CreateMutex() error!");
		exit(1);
	}

	rpSock = socket(PF_INET, SOCK_DGRAM, 0);
	if (rpSock == INVALID_SOCKET) err_quit("rp socket()");
	flSock = socket(PF_INET, SOCK_STREAM, 0);
	if (flSock == INVALID_SOCKET) err_quit("fl socket()");
	rqSock = socket(PF_INET, SOCK_DGRAM, 0);
	if (rqSock == INVALID_SOCKET) err_quit("rq socket()");
	frSock = socket(PF_INET, SOCK_STREAM, 0);
	if (frSock == INVALID_SOCKET) err_quit("fr socket()");

	retVal = setsockopt(rqSock, SOL_SOCKET, SO_RCVTIMEO, (char *)&optVal, sizeof(optVal));
	if (retVal == SOCKET_ERROR) {
		printf("\a[ERROR] setsockopt() Error!\n");
		exit(1);
	}

	ZeroMemory(&myNode.nodeInfo.addrInfo, sizeof(myNode.nodeInfo.addrInfo));
	myNode.nodeInfo.addrInfo.sin_family = AF_INET;
	myNode.nodeInfo.addrInfo.sin_port = htons(atoi(argv[2]));
	myNode.nodeInfo.addrInfo.sin_addr.s_addr = inet_addr(argv[1]);//htonl(INADDR_ANY);
	//myNode.nodeInfo.ID = -1;
	printf("%s, %d\n", inet_ntoa(myNode.nodeInfo.addrInfo.sin_addr), ntohs(myNode.nodeInfo.addrInfo.sin_port));
	retVal = bind(rpSock, (SOCKADDR *)&myNode.nodeInfo.addrInfo,
		sizeof(myNode.nodeInfo.addrInfo));
	if (retVal == SOCKET_ERROR) err_quit("rpSock bind()");

	retVal = bind(flSock, (SOCKADDR *)&myNode.nodeInfo.addrInfo,
		sizeof(myNode.nodeInfo.addrInfo));
	if (retVal == SOCKET_ERROR) err_quit("flSock bind()");

	retVal = listen(flSock, SOMAXCONN);
	if (retVal == SOCKET_ERROR)
		ErrorHandling("listen() error");

	showCommand();

	//3번전에 스레드를 이용해서 소켓통신 데이터를 받을 수 있도록 해야한다.
	/* step 3: Prompt handling (loop) */
	//세번째 인자인 함수 이름 앞에 (void *)를 넣으세요.
	//네번째 인자는 메인 쓰레드가 종료될 때(Q입력시) Flag를 1로 만들어
	//다른 쓰레드도 같이 종료되도록 만드는데 사용합니다.

	do {

		while (1) {
			printf("CHORD> \n");
			printf("CHORD> Enter your command ('help' for help message).\n");
			printf("CHORD> ");
			fgets(command, sizeof(command), stdin);
			fgetsCleanup(command);
			if (!strcmp(command, "c") || !strcmp(command, "create"))
				cmdChar = 'c';
			else if (!strcmp(command, "j") || !strcmp(command, "join"))
				cmdChar = 'j';
			else if (!strcmp(command, "l") || !strcmp(command, "leave"))
				cmdChar = 'l';
			else if (!strcmp(command, "a") || !strcmp(command, "add"))
				cmdChar = 'a';
			else if (!strcmp(command, "d") || !strcmp(command, "delete"))
				cmdChar = 'd';
			else if (!strcmp(command, "s") || !strcmp(command, "search"))
				cmdChar = 's';
			else if (!strcmp(command, "f") || !strcmp(command, "finger"))
				cmdChar = 'f';
			else if (!strcmp(command, "i") || !strcmp(command, "info"))
				cmdChar = 'i';
			else if (!strcmp(command, "h") || !strcmp(command, "help"))
				cmdChar = 'h';
			else if (!strcmp(command, "m") || !strcmp(command, "mute"))
				cmdChar = 'm';
			else if (!strcmp(command, "q") || !strcmp(command, "quit"))
				cmdChar = 'q';
			else if (!strlen(command))
				continue;
			else {
				printf("\a[ERROR] Wrong command! Input a correct command.\n\n");
				continue;
			}
			break;
		}

		/* step 4: User input processing (switch) */

		switch (cmdChar) {
		case 'l':
			memset(&bufMsg, 0, sizeof(bufMsg));
			//WaitForSingleObject(hMutex, INFINITE);
			//자기가 소유한 파일의 레퍼런스 값들도 다른노드에서 지워주고 가야한다.
			printf("CHORD> Leave start\n");
			succNode = myNode.chordInfo.fingerInfo.finger[0];
			memset(&keysInfo, 0, sizeof(keysInfo));
			keyCount = 0;
			bufMsg.bodySize = 0;
			for (i = 0; i < myNode.chordInfo.FRefInfo.fileNum; i++){
				for (k = 0; k < myNode.fileInfo.fileNum; k++){
					if (myNode.fileInfo.fileRef[k].Key == myNode.chordInfo.FRefInfo.fileRef[i].Key){
						for (j = i; j < myNode.chordInfo.FRefInfo.fileNum - 1; j++){
							WaitForSingleObject(hMutex, INFINITE);
							myNode.chordInfo.FRefInfo.fileRef[j] = myNode.chordInfo.FRefInfo.fileRef[j + 1];
							ReleaseMutex(hMutex);
						}
						WaitForSingleObject(hMutex, INFINITE);
						myNode.chordInfo.FRefInfo.fileNum--;
						ReleaseMutex(hMutex);
						i--;// 배열 땡겼으니까

						for (j = k; j < myNode.fileInfo.fileNum - 1; k++){
							WaitForSingleObject(hMutex, INFINITE);
							myNode.fileInfo.fileRef[j] = myNode.fileInfo.fileRef[j + 1];
							ReleaseMutex(hMutex);
						}
						WaitForSingleObject(hMutex, INFINITE);
						myNode.fileInfo.fileNum--;
						ReleaseMutex(hMutex);
						k--;
					}
				}
			}

			//자기가 자기의 소유 파일 레퍼런스 소유한 값을 지워준다.

			for (i = 0; i < myNode.fileInfo.fileNum; i++){
				bufMsg.msgID = 10;
				bufMsg.msgType = 0;
				bufMsg.moreInfo = myNode.fileInfo.fileRef[i].Key;
				bufMsg.bodySize = 0;
				retVal = sendto(rqSock, (char *)&bufMsg, sizeof(bufMsg), 0, (struct sockaddr *) &myNode.fileInfo.fileRef[i].refOwner.addrInfo, sizeof(myNode.fileInfo.fileRef[i].refOwner.addrInfo));
				if (retVal == SOCKET_ERROR){
					printf("File Reference Delete msg request for LeaveSendto error!\n");
					exit(1);
				}
				retVal = recvfrom(rqSock, (char *)&tempMsg, sizeof(tempMsg), 0, NULL, NULL);
				if (retVal == SOCKET_ERROR){
					printf("CHORD> File Reference Delete msg request for Leave Recvfrom error!\n");
					exit(1);
				}
				//배열 당겨주면 좋지만 별 상관없다.
			}

			for (i = 0; i < myNode.chordInfo.FRefInfo.fileNum; i++){
				keysInfo.fileRef[keyCount] = myNode.chordInfo.FRefInfo.fileRef[i];
				bufMsg.bodySize += sizeof(keysInfo.fileRef[keyCount]);
				//printf("uuuuu : %s, %d\n", keysInfo.fileRef[keyCount].Name, keysInfo.fileRef[keyCount].Key);
				keyCount++;

				for (j = i; j < myNode.chordInfo.FRefInfo.fileNum - 1; j++){
					WaitForSingleObject(hMutex, INFINITE);
					myNode.chordInfo.FRefInfo.fileRef[j] = myNode.chordInfo.FRefInfo.fileRef[j + 1];
					ReleaseMutex(hMutex);
				}
				WaitForSingleObject(hMutex, INFINITE);
				myNode.chordInfo.FRefInfo.fileNum--;
				ReleaseMutex(hMutex);
				i--;// 배열 땡겼으니까
			}

			printf("keyCount: %d, bodySize: %d\n", keyCount, bufMsg.bodySize);

			body = &keysInfo.fileRef[0];
			bufMsg.msgID = 8;
			bufMsg.msgType = 0;
			bufMsg.moreInfo = keyCount;
			retVal = sendto(rqSock, (char *)&bufMsg, sizeof(bufMsg), 0, (struct sockaddr *) &succNode.addrInfo, sizeof(succNode.addrInfo));
			if (retVal == SOCKET_ERROR){
				printf("LeaveKeys request Sendto error!\n");
				exit(1);
			}
			retVal = recvfrom(rqSock, (char *)&tempMsg, sizeof(tempMsg), 0, NULL, NULL);
			if (retVal == SOCKET_ERROR){
				printf("LeaveKeys request Recvfrom error!\n");
				exit(1);
			}
			printf("kkkkeyCount: %d, bodySize: %d\n", keyCount, bufMsg.bodySize);
			retVal = sendto(rqSock, (char *)body, bufMsg.bodySize, 0, (struct sockaddr *)&succNode.addrInfo, sizeof(succNode.addrInfo));
			if (retVal == SOCKET_ERROR){
				printf("LeaveKeys request body Sendto error!\n");
				exit(1);
			}

			retVal = recvfrom(rqSock, (char *)&tempMsg, sizeof(tempMsg), 0, NULL, NULL);
			if (retVal == SOCKET_ERROR){
				printf("LeaveKeys request body Recvfrom error!\n");
				exit(1);
			}

			bufMsg.msgID = 4;
			bufMsg.msgType = 0;
			bufMsg.nodeInfo = myNode.chordInfo.fingerInfo.Pre;
			bufMsg.moreInfo = 0;

			retVal = sendto(rqSock, (char *)&bufMsg, sizeof(bufMsg), 0, (struct sockaddr *) &myNode.chordInfo.fingerInfo.finger[0].addrInfo, sizeof(myNode.chordInfo.fingerInfo.finger[0].addrInfo));
			if (retVal == SOCKET_ERROR){
				printf("predecessor Update request Sendto error!\n");
				exit(1);
			}
			retVal = recvfrom(rqSock, (char *)&tempMsg, sizeof(tempMsg), 0, NULL, NULL);
			if (retVal == SOCKET_ERROR){
				printf("predecessor Update request Recvfrom error!\n");
				exit(1);
			}
			
			bufMsg.msgID = 6;
			bufMsg.msgType = 0;
			bufMsg.nodeInfo = myNode.chordInfo.fingerInfo.finger[0];
			bufMsg.moreInfo = 0;

			retVal = sendto(rqSock, (char *)&bufMsg, sizeof(bufMsg), 0, (struct sockaddr *) &myNode.chordInfo.fingerInfo.Pre.addrInfo, sizeof(myNode.chordInfo.fingerInfo.Pre.addrInfo));
			if (retVal == SOCKET_ERROR){
				printf("predecessor Update request Sendto error!\n");
				exit(1);
			}
			retVal = recvfrom(rqSock, (char *)&tempMsg, sizeof(tempMsg), 0, NULL, NULL);
			if (retVal == SOCKET_ERROR){
				printf("predecessor Update request Recvfrom error!\n");
				exit(1);
			}
			//ReleaseMutex(hMutex);
			exit(1);

			break;
		case 'i':
			printf("CHORD> My Node Information: \n");
			printf("CHORD> My Node IP Addr: %s, Port No: %d, ID: %d\n", inet_ntoa(myNode.nodeInfo.addrInfo.sin_addr), ntohs(myNode.nodeInfo.addrInfo.sin_port), myNode.nodeInfo.ID);
			for (i = 0; i < myNode.fileInfo.fileNum; i++)
			{
				printf("CHORD> %dth own file name: %s, key: %d\n",i+1, myNode.fileInfo.fileRef[i].Name, myNode.fileInfo.fileRef[i].Key);
			}
			for (i = 0; i < myNode.chordInfo.FRefInfo.fileNum; i++)
			{
				printf("CHORD> %dth file ref name: %s, key: %d, owner ID: %d\n", i+1, myNode.chordInfo.FRefInfo.fileRef[i].Name, myNode.chordInfo.FRefInfo.fileRef[i].Key, myNode.chordInfo.FRefInfo.fileRef[i].owner.ID);
			}
			break;
		case 'f':
			printf("CHORD> Finger table Information: \n");
			printf("CHORD> My Node IP Addr: %s, Port No: %d, ID: %d\n", inet_ntoa(myNode.nodeInfo.addrInfo.sin_addr), ntohs(myNode.nodeInfo.addrInfo.sin_port), myNode.nodeInfo.ID);
			printf("CHORD> Predecessor IP Addr: %s, Port No: %d, ID: %d\n", inet_ntoa(myNode.chordInfo.fingerInfo.Pre.addrInfo.sin_addr), ntohs(myNode.chordInfo.fingerInfo.Pre.addrInfo.sin_port), myNode.chordInfo.fingerInfo.Pre.ID);
			for (i = 0; i < baseM; i++){
				printf("CHORD> Finger[%d] IP Addr: %s, Port No: %d, ID: %d\n", i, inet_ntoa(myNode.chordInfo.fingerInfo.finger[i].addrInfo.sin_addr), ntohs(myNode.chordInfo.fingerInfo.finger[i].addrInfo.sin_port), myNode.chordInfo.fingerInfo.finger[i].ID);
			}
			break;
		case 'd':
			printf("CHORD> Enter the file name to delete: ");
			scanf("%s", fileName);
			targetKey = strHash(fileName);
			succNode = find_successor(rqSock, targetKey);
			if (myNode.nodeInfo.ID == succNode.ID)
			{
				for (i = 0; i < myNode.chordInfo.FRefInfo.fileNum; i++){
					if (myNode.chordInfo.FRefInfo.fileRef[i].Key == targetKey){
						for (j = i; j < myNode.chordInfo.FRefInfo.fileNum - 1; j++){
							WaitForSingleObject(hMutex, INFINITE);
							myNode.chordInfo.FRefInfo.fileRef[i] = myNode.chordInfo.FRefInfo.fileRef[i + 1];
							ReleaseMutex(hMutex);
						}
						WaitForSingleObject(hMutex, INFINITE);
						myNode.chordInfo.FRefInfo.fileNum--;
						ReleaseMutex(hMutex);
						i--;// 배열 땡겼으니까
					}
				}
			}
			else{
				memset(&bufMsg, 0, sizeof(bufMsg));
				bufMsg.msgID = 10;
				bufMsg.msgType = 0;
				bufMsg.moreInfo = targetKey;
				bufMsg.bodySize = 0;

				retVal = sendto(rqSock, (char *)&bufMsg, sizeof(bufMsg), 0, (struct sockaddr *) &succNode.addrInfo, sizeof(succNode.addrInfo));
				if (retVal == SOCKET_ERROR){
					printf("File Reference Delete msg request Sendto error!\n");
					exit(1);
				}
				retVal = recvfrom(rqSock, (char *)&tempMsg, sizeof(tempMsg), 0, NULL, NULL);
				if (retVal == SOCKET_ERROR){
					if (WSAGetLastError() == WSAETIMEDOUT) {
						printf("\a[ERROR] File Reference Delete msg timed out. File Add Failed!\n");
						continue;
					}
					printf("CHORD> File Reference Delete msg request Recvfrom error!\n");
					continue;
				}

				if ((tempMsg.msgID != 10) || (tempMsg.msgType != 1)) { // wrong msg
					printf("\a[ERROR] Wrong Message (not FileRefAdd) Received. File Add Failed!\n");
					continue;
				}

				if (tempMsg.moreInfo == -1) { // failure
					printf("\a[ERROR] FileRefAdd Request Failed. File Add Failed!\n");
					continue;
				}
			}

			for (i = 0; i < myNode.fileInfo.fileNum; i++){
				if (myNode.fileInfo.fileRef[i].Key == targetKey){
					for (j = i; j < myNode.fileInfo.fileNum - 1; j++){
						WaitForSingleObject(hMutex, INFINITE);
						myNode.fileInfo.fileRef[i] = myNode.fileInfo.fileRef[i+1];
						ReleaseMutex(hMutex);
					}
					WaitForSingleObject(hMutex, INFINITE);
					myNode.fileInfo.fileNum--;
					ReleaseMutex(hMutex);
					i--;// 배열 땡겼으니까
				}
			}
			printf("CHORD> The file has been successfully deleted \n");
			break;
		case 'm':
			if (sMode == 1)
				sMode = 0;
			else
				sMode = 1;
			break;
		case 's':
			lookup();
			break;
		case 'a':
			resultFlag = 0;
			WaitForSingleObject(hMutex, INFINITE);
			if (myNode.fileInfo.fileNum == FileMax) { // file number is full 
				printf("\a[ERROR] Your Cannot Add more file. File Space is Full!\n\n");
				continue;
			}
			ReleaseMutex(hMutex);
			printf("CHORD> Files to be added must be in the same folder where this program is located.\n");
			printf("CHORD> Note that the maximum file name size is 32.\n");
			printf("CHORD> Enter the file name to add: ");
			//scanf("%s", fileName);
			fgets(fileName, sizeof(fileName), stdin);
			fgetsCleanup(fileName);

			// check if the file exits
			if ((fp = fopen(fileName, "rb")) == NULL) {
				printf("\a[ERROR] The file '%s' is not in the same folder where this program is!\n\n", fileName);
				continue;
			}
			fclose(fp);

			WaitForSingleObject(hMutex, INFINITE);
			strcpy(myNode.fileInfo.fileRef[myNode.fileInfo.fileNum].Name, fileName);
			myNode.fileInfo.fileRef[myNode.fileInfo.fileNum].Key = strHash(fileName);
			myNode.fileInfo.fileRef[myNode.fileInfo.fileNum].owner = myNode.nodeInfo;
			printf("CHORD> Input File Name: %s, Key: %d\n", fileName, myNode.fileInfo.fileRef[myNode.fileInfo.fileNum].Key);
			ReleaseMutex(hMutex);
			succNode = find_successor(rqSock, myNode.fileInfo.fileRef[myNode.fileInfo.fileNum].Key);
	
			WaitForSingleObject(hMutex, INFINITE);
			myNode.fileInfo.fileRef[myNode.fileInfo.fileNum].refOwner = succNode;
			ReleaseMutex(hMutex);
			printf("CHORD> File Successor IP Addr: %s, Port No: %d, ID: %d\n", inet_ntoa(myNode.fileInfo.fileRef[myNode.fileInfo.fileNum].refOwner.addrInfo.sin_addr), ntohs(myNode.fileInfo.fileRef[myNode.fileInfo.fileNum].refOwner.addrInfo.sin_port), myNode.fileInfo.fileRef[myNode.fileInfo.fileNum].refOwner.ID);
			WaitForSingleObject(hMutex, INFINITE);
			if (myNode.nodeInfo.ID == myNode.fileInfo.fileRef[myNode.fileInfo.fileNum].refOwner.ID)
			{
				myNode.chordInfo.FRefInfo.fileRef[myNode.chordInfo.FRefInfo.fileNum] = myNode.fileInfo.fileRef[myNode.fileInfo.fileNum];
				printf("CHORD> File Ref Info has been sent successfully to the Successor.\n");
				printf("CHORD> File Add has been successfully finished.\n");
				myNode.fileInfo.fileNum++;
				myNode.chordInfo.FRefInfo.fileNum++;
				break;
			}
			refInfo = myNode.fileInfo.fileRef[myNode.fileInfo.fileNum];
			ReleaseMutex(hMutex);
			//소켓통신은 자기가 자기에게 보내면 에러가 나므로 주의하자!

			memset(&bufMsg, 0, sizeof(bufMsg));
			bufMsg.msgID = 9;
			bufMsg.msgType = 0;
			bufMsg.nodeInfo = myNode.nodeInfo;
			bufMsg.moreInfo = 1;
			bufMsg.fileInfo = refInfo;
			bufMsg.bodySize = 0;
			retVal = sendto(rqSock, (char *)&bufMsg, sizeof(bufMsg), 0, (struct sockaddr *) &succNode.addrInfo, sizeof(succNode.addrInfo));
			if (retVal == SOCKET_ERROR){
				printf("File Reference Add msg request Sendto error!\n");
				exit(1);
			}
			retVal = recvfrom(rqSock, (char *)&tempMsg, sizeof(tempMsg), 0, NULL, NULL);
			if (retVal == SOCKET_ERROR){
				if (WSAGetLastError() == WSAETIMEDOUT) {
					printf("\a[ERROR] FileRefAdd Request timed out. File Add Failed!\n");
					continue;
				}
				printf("CHORD> File Reference Add msg request Recvfrom error!\n");
				continue;
			}

			if ((tempMsg.msgID != 9) || (tempMsg.msgType != 1)) { // wrong msg
				printf("\a[ERROR] Wrong Message (not FileRefAdd) Received. File Add Failed!\n");
				continue;
			}

			if (tempMsg.moreInfo == -1) { // failure
				printf("\a[ERROR] FileRefAdd Request Failed. File Add Failed!\n");
				continue;
			}

			WaitForSingleObject(hMutex, INFINITE);
			myNode.fileInfo.fileNum++;
			ReleaseMutex(hMutex);
			printf("CHORD> File Ref Info has been sent successfully to the Successor.\n");
			printf("CHORD> File Add has been successfully finished.\n");
			
			break;
		case 'c':
			if (joinFlag) {
				printf("\a[ERROR] You are currently in the network; You cannot create the network!\n\n");
				continue;
			}
			joinFlag = 1;
			myNode.chordInfo.fingerInfo.Pre = myNode.nodeInfo;
			for (i = 0; i < baseM; i++){
				myNode.chordInfo.fingerInfo.finger[i] = myNode.nodeInfo;
			}

			printf("CHORD> You have created a chord network!\n");
			printf("CHORD> Your finger table has been updated!\n");

			hThread[0] = (HANDLE)_beginthreadex(NULL, 0, (void*)procRecvMsg, (void*)&exitFlag, 0, NULL);//뒤에서 세번째가 인자주는 넘이다.
			hThread[1] = (HANDLE)_beginthreadex(NULL, 0, (void*)procPPandFF, (void*)&exitFlag, 0, NULL);
			break;
		case 'j':
			printf("CHORD> You need a helper node to join the existing network.\n");
			printf("CHORD> If you want to create a network, the helper node is yourself.\n");
			printf("CHORD> Enter IP address of the helper node: ");
			scanf("%s", tempIP);
			printf("CHORD> Enter port number of the helper node: ");
			scanf("%s", tempPort);
			memset(&bufMsg, 0, sizeof(bufMsg));
			bufMsg.msgID = 1;
			bufMsg.nodeInfo = myNode.nodeInfo;
			bufMsg.moreInfo = 0;
			bufMsg.bodySize = 0;
			bufMsg.msgType = 0;

			memset(&targetAddr, 0, sizeof(targetAddr));
			targetAddr.sin_family = AF_INET;
			targetAddr.sin_addr.s_addr = inet_addr(tempIP);
			targetAddr.sin_port = htons(atoi(tempPort));

			//printf("%s, %d\n", inet_ntoa(targetAddr.sin_addr), ntohs(targetAddr.sin_port));
			retVal = sendto(rqSock, (char *)&bufMsg, sizeof(bufMsg), 0, (struct sockaddr *) &targetAddr, sizeof(targetAddr));
			if (retVal == SOCKET_ERROR){
				printf("Join Info request Sendto error!\n");
				exit(1);
			}
			printf("CHORD> JoinInfo request Message has been sent.\n");

			retVal = recvfrom(rqSock, (char *)&tempMsg, sizeof(tempMsg), 0, NULL, NULL);
			if (retVal == SOCKET_ERROR){
				if (WSAGetLastError() == WSAETIMEDOUT) {
					printf("\a[ERROR] Join Info Request timed out. File Add Failed!\n");
					continue;
				}
				printf("Join Info request Recvfrom error!\n");
				continue;
			}

			if ((tempMsg.msgID != 1) || (tempMsg.msgType != 1)) { // wrong msg
				printf("\a[ERROR] Wrong Message (not FileRefAdd) Received. File Add Failed!\n");
				continue;
			}

			if (tempMsg.moreInfo == -1) { // failure
				printf("\a[ERROR] FileRefAdd Request Failed. File Add Failed!\n");
				continue;
			}
			printf("%s, %d\n", inet_ntoa(tempMsg.nodeInfo.addrInfo.sin_addr), ntohs(tempMsg.nodeInfo.addrInfo.sin_port));
			printf("CHORD> JoinInfo response Message has been received.\n");
			WaitForSingleObject(hMutex, INFINITE);
			myNode.chordInfo.fingerInfo.finger[0] = tempMsg.nodeInfo;
			ReleaseMutex(hMutex);
			printf("CHORD> You got your successor node from the helper node.\n");
			printf("CHORD> Successor IP Addr: %s, Port No: %d, ID: %d\n", inet_ntoa(tempMsg.nodeInfo.addrInfo.sin_addr), ntohs(tempMsg.nodeInfo.addrInfo.sin_port), tempMsg.nodeInfo.ID);
			move_keys();
			stabilizeJ(rqSock);
			fix_finger();
			
			hThread[0] = (HANDLE)_beginthreadex(NULL, 0, (void*)procRecvMsg, (void*)&exitFlag, 0, NULL);//뒤에서 세번째가 인자주는 넘이다.
			hThread[1] = (HANDLE)_beginthreadex(NULL, 0, (void*)procPPandFF, (void*)&exitFlag, 0, NULL);
			break;
		}

	} while (cmdChar != 'q');

	/* step 5: Program termination */

	WaitForMultipleObjects(2, hThread, TRUE, INFINITE);

	closesocket(rqSock);
	closesocket(rpSock);
	closesocket(frSock);
	closesocket(flSock);
	closesocket(fsSock);
	closesocket(pfSock);
	CloseHandle(hMutex);

	WSACleanup();

	printf("*************************  B  Y  E  *****************************\n");

	return 0;
}

int lookup()
{
	int targetKey;
	int i;
	int retVal;
	int Store = 0;
	int addrSize;
	char fileName[FNameMax + 1];
	nodeInfoType succNode = { 0 };
	nodeInfoType ownNode = { 0 };
	struct sockaddr_in peerAddr, targetAddr;
	chordHeaderType tempMsg, bufMsg;
	int temp;
	char fileBuf[fBufSize];
	int totalbytes;
	FILE *fp;
	int numRead;
	int numTotal;
	addrSize = sizeof(peerAddr);

	printf("CHORD> Input File name to search and download: ");
	scanf("%s", fileName);
	targetKey = strHash(fileName);
	printf("CHORD> Input File name: %s, Key: %d\n", fileName, targetKey);

	for (i = 0; i < myNode.fileInfo.fileNum; i++){
		if (targetKey == myNode.fileInfo.fileRef[i].Key){
			printf("CHORD> The file %s is at this node itself!\n", myNode.fileInfo.fileRef[i].Name);//done
			return 0;
		}
	}
	for (i = 0; i < myNode.chordInfo.FRefInfo.fileNum; i++){
		if (targetKey == myNode.chordInfo.FRefInfo.fileRef[i].Key) {
			ownNode = myNode.chordInfo.FRefInfo.fileRef[i].owner;
			printf("CHORD> File Owner IP Addr: %s, Port No: %d, ID: %d\n", inet_ntoa(myNode.chordInfo.FRefInfo.fileRef[i].owner.addrInfo.sin_addr), ntohs(myNode.chordInfo.FRefInfo.fileRef[i].owner.addrInfo.sin_port), myNode.chordInfo.FRefInfo.fileRef[i].owner.ID);
			memset(&bufMsg, 0, sizeof(bufMsg));
			bufMsg.msgID = 11;
			bufMsg.msgType = 0;
			bufMsg.moreInfo = myNode.chordInfo.FRefInfo.fileRef[i].Key;
			bufMsg.bodySize = 0;

			retVal = sendto(rqSock, (char *)&bufMsg, sizeof(bufMsg), 0, (struct sockaddr *) &ownNode.addrInfo, sizeof(ownNode.addrInfo));
			if (retVal == SOCKET_ERROR){
				printf("File Reference Info request Sendto error!\n");
				exit(1);
			}
			retVal = recvfrom(rqSock, (char *)&tempMsg, sizeof(tempMsg), 0, NULL, NULL);//이 tempMsg에 totalBytes값이 있다.
			if (retVal == SOCKET_ERROR){
				printf("File Reference Info request Recvfrom error!\n");
				exit(1);
			}
			printf("CHORD> own IP Addr: %s, Port No: %d, ID: %d\n", inet_ntoa(ownNode.addrInfo.sin_addr), ntohs(ownNode.addrInfo.sin_port), ownNode.ID);
			retVal = connect(frSock, (struct sockaddr *)&ownNode.addrInfo, sizeof(ownNode.addrInfo));
			if (retVal == SOCKET_ERROR) err_quit("connect()");

			fp = fopen(myNode.chordInfo.FRefInfo.fileRef[i].Name, "wb");
			if (fp == NULL){
				perror("파일 입출력 오류");
				exit(1);

			}

			totalbytes = tempMsg.bodySize;
			printf("CHORD> totalbytes: %d\n", totalbytes);

			numTotal = 0;
			temp = totalbytes;
			while (1){
				if (fBufSize > temp){
					retVal = recvn(frSock, fileBuf, temp, 0);
				}
				else
					retVal = recvn(frSock, fileBuf, fBufSize, 0);
				temp -= retVal;
				//printf("retVal : %d\n", retVal);
				if (retVal == SOCKET_ERROR){
					err_display("recv()");
					break;
				}
				else if (retVal == 0)
					break;
				else{
					fwrite(fileBuf, 1, retVal, fp);
					if (ferror(fp)){
						perror("파일 입출력 오류");
						break;
					}
					numTotal += retVal;
				}
				if (numTotal == totalbytes)
					break;
			}
			fclose(fp);
			// 전송 결과 출력
			if (numTotal == totalbytes)
				printf("-> 파일 전송 완료!\n");
			else
				printf("-> 파일 전송 실패!\n");

			printf("CHORD> File %s has been received successfully\n", myNode.chordInfo.FRefInfo.fileRef[i].Name);
			return 0;
		}
	}
	succNode = find_successor(rqSock, targetKey);
	printf("CHORD> File's Successor IP Addr: %s, Port No: %d, ID: %d\n", inet_ntoa(succNode.addrInfo.sin_addr), ntohs(succNode.addrInfo.sin_port), succNode.ID);
	memset(&bufMsg, 0, sizeof(bufMsg));
	bufMsg.msgID = 12;
	bufMsg.msgType = 0;
	bufMsg.moreInfo = targetKey;
	bufMsg.bodySize = 0;

	retVal = sendto(rqSock, (char *)&bufMsg, sizeof(bufMsg), 0, (struct sockaddr *) &succNode.addrInfo, sizeof(succNode.addrInfo));
	if (retVal == SOCKET_ERROR){
		printf("File Reference Info request Sendto error!\n");
		exit(1);
	}
	retVal = recvfrom(rqSock, (char *)&tempMsg, sizeof(tempMsg), 0, NULL, NULL);
	if (retVal == SOCKET_ERROR){
		printf("File Reference Info request Recvfrom error!\n");
		exit(1);
	}
	ownNode = tempMsg.nodeInfo;
	printf("CHORD> File Owner IP Addr: %s, Port No: %d, ID: %d\n", inet_ntoa(ownNode.addrInfo.sin_addr), ntohs(ownNode.addrInfo.sin_port), ownNode.ID);
	memset(&bufMsg, 0, sizeof(bufMsg));
	bufMsg.msgID = 11;
	bufMsg.msgType = 0;
	bufMsg.moreInfo = targetKey;
	bufMsg.bodySize = 0;

	retVal = sendto(rqSock, (char *)&bufMsg, sizeof(bufMsg), 0, (struct sockaddr *) &ownNode.addrInfo, sizeof(ownNode.addrInfo));
	if (retVal == SOCKET_ERROR){
		printf("File Reference Info request Sendto error!\n");
		exit(1);
	}
	retVal = recvfrom(rqSock, (char *)&tempMsg, sizeof(tempMsg), 0, NULL, NULL);//이 tempMsg에 totalBytes값이 있다.
	if (retVal == SOCKET_ERROR){
		printf("File Reference Info request Recvfrom error!\n");
		exit(1);
	}
	retVal = connect(frSock, (struct sockaddr *)&ownNode.addrInfo, sizeof(ownNode.addrInfo));
	if (retVal == SOCKET_ERROR) err_quit("connect()");

	fp = fopen(fileName, "wb");//여기서 에러구나
	if (fp == NULL){
		perror("파일 입출력 오류");
		//closesocket(frSock);
		exit(1);

	}

	totalbytes = tempMsg.bodySize;
	printf("CHORD> totalbytes: %d\n", totalbytes);
	
	numTotal = 0;
	temp = totalbytes;
	while (1){
		if (fBufSize > temp){
			retVal = recvn(frSock, fileBuf, temp, 0);
		}
		else
			retVal = recvn(frSock, fileBuf, fBufSize, 0);
		temp -= retVal;
		if (retVal == SOCKET_ERROR){
			err_display("recv()");
			break;
		}
		else if (retVal == 0)
			break;
		else{
			fwrite(fileBuf, 1, retVal, fp);
			if (ferror(fp)){
				perror("파일 입출력 오류");
				break;
			}
			numTotal += retVal;
		}
		if (numTotal == totalbytes)
			break;
	}
	fclose(fp);
	// 전송 결과 출력
	if (numTotal == totalbytes)
		printf("-> 파일 전송 완료!\n");
	else
		printf("-> 파일 전송 실패!\n");
	printf("CHORD> File %s has been received successfully!\n");


	return 0; /* Success */
}

void procRecvMsg(void *arg)//request를 받아서 response를 보내만 주고 response를 받지는 않는다.
{
	char tempIP[16];
	char tempPort[6];
	struct sockaddr_in peerAddr, targetAddr;
	chordHeaderType tempMsg, bufMsg;
	nodeInfoType succNode, predNode, reqNode;
	nodeType preNode = { 0 };
	int optVal = 5000;  // 5 seconds
	int retVal; // return value
	fileInfoType keysInfo;
	char fileBuf[fBufSize];
	char fileName[FNameMax + 1];
	FILE *fp;
	char* body = NULL;
	struct sockaddr_in serveraddr;
	int keyCount;
	int i, j, targetKey, resultCode, keyNum, addrSize, fileSize, numRead, numTotal;
	int *exitFlag = (int *)arg;
	int totalbytes;
	int targAddrSize;//타겟 주소 사이즈
	addrSize = sizeof(peerAddr);
	while (!(*exitFlag)) {
		//memset(&tempMsg, 0, sizeof(tempMsg));
		memset(&peerAddr, 0, sizeof(peerAddr));
		retVal = recvfrom(rpSock, (char *)&bufMsg, sizeof(bufMsg), 0, (struct sockaddr *) &peerAddr, &addrSize);
		if (retVal == SOCKET_ERROR){
			continue;
		}
		if (bufMsg.msgType != 0) {
			printf("\a[ERROR] Unexpected Response Message Received. Therefore Message Ignored!\n");
			continue;
		}
		switch (bufMsg.msgID) {
		case 0: // PingPong
			resultCode = 0;

			memset(&tempMsg, 0, sizeof(tempMsg));
			tempMsg.msgID = 0;
			tempMsg.msgType = 1;
			tempMsg.moreInfo = resultCode;
			tempMsg.bodySize = 0;
			retVal = sendto(rpSock, (char *)&tempMsg, sizeof(tempMsg), 0, (struct sockaddr *) &peerAddr, sizeof(peerAddr));
			if (retVal == SOCKET_ERROR){
				printf("Pingpong response Sendto error!\n");
				exit(1);
			}
			break;
		case 1:
			resultCode = 0;

			succNode = find_successor(rqSock, bufMsg.nodeInfo.ID);
			memset(&tempMsg, 0, sizeof(tempMsg));
			tempMsg.msgID = 1;
			tempMsg.msgType = 1;
			tempMsg.nodeInfo = succNode;
			tempMsg.moreInfo = resultCode;
			tempMsg.bodySize = 0;

			retVal = sendto(rpSock, (char *)&tempMsg, sizeof(tempMsg), 0, (struct sockaddr *) &peerAddr, sizeof(peerAddr));
			if (retVal == SOCKET_ERROR){
				printf("Join Info response Sendto error!\n");
				exit(1);
			}

			//printf("Succ : %s\n", inet_ntoa(succNode.addrInfo.sin_addr));
			break;
		case 2:
			keyCount = 0;
			memset(&tempMsg, 0, sizeof(tempMsg));
			for (i = 0; i < myNode.chordInfo.FRefInfo.fileNum; i++){
				if (modIn(ringSize, bufMsg.nodeInfo.ID, myNode.chordInfo.FRefInfo.fileRef[i].Key, myNode.nodeInfo.ID, 1, 0)){
					keysInfo.fileRef[keyCount] = myNode.chordInfo.FRefInfo.fileRef[i];
					tempMsg.bodySize += sizeof(keysInfo.fileRef[keyCount]);
					keyCount++;

					for (j = i; j < myNode.chordInfo.FRefInfo.fileNum - 1; j++){
						WaitForSingleObject(hMutex, INFINITE);
						myNode.chordInfo.FRefInfo.fileRef[j] = myNode.chordInfo.FRefInfo.fileRef[j + 1];
						ReleaseMutex(hMutex);
					}
					WaitForSingleObject(hMutex, INFINITE);
					myNode.chordInfo.FRefInfo.fileNum--;
					ReleaseMutex(hMutex);
					i--;// 배열 땡겼으니까
				}
			}
			body = &keysInfo.fileRef[0];
			tempMsg.msgID = 2;
			tempMsg.msgType = 1;
			tempMsg.moreInfo = keyCount;

			retVal = sendto(rpSock, (char *)&tempMsg, sizeof(tempMsg), 0, (struct sockaddr *) &peerAddr, sizeof(peerAddr));
			if (retVal == SOCKET_ERROR){
				printf("Movekeys tempMsg response Sendto error!\n");
				exit(1);
			}
			retVal = recvfrom(rpSock, (char *)&bufMsg, sizeof(bufMsg), 0, (struct sockaddr *) &peerAddr, &addrSize);
			if (retVal == SOCKET_ERROR){
				printf("Movekeys tempMsg response Recvfrom error!\n");
				exit(1);
			}

			retVal = sendto(rpSock, (char *)body, tempMsg.bodySize, 0, (struct sockaddr *) &peerAddr, sizeof(peerAddr));
			if (retVal == SOCKET_ERROR){
				printf("Movekeys body response Sendto error!\n");
				exit(1);
			}
			break;
		case 3:
			resultCode = 0;

			//WaitForSingleObject(hMutex, INFINITE);
			memset(&tempMsg, 0, sizeof(tempMsg));
			tempMsg.msgID = 3;//받는쪽도 msgID 4인걸 확인하고 받도록 해준다.
			tempMsg.msgType = 1;
			tempMsg.nodeInfo = myNode.chordInfo.fingerInfo.Pre;
			tempMsg.moreInfo = resultCode;
			tempMsg.bodySize = 0;

			retVal = sendto(rpSock, (char *)&tempMsg, sizeof(tempMsg), 0, (struct sockaddr *) &peerAddr, sizeof(peerAddr));
			if (retVal == SOCKET_ERROR){
				printf("Predecessor Info response Sendto error!\n");
				exit(1);
			}
			//ReleaseMutex(hMutex);
			break;
		case 4:
			resultCode = 0;

			//WaitForSingleObject(hMutex, INFINITE);
			notify(bufMsg.nodeInfo);
			memset(&tempMsg, 0, sizeof(tempMsg));
			tempMsg.msgID = 4;//받는쪽도 msgID 4인걸 확인하고 받도록 해준다.
			tempMsg.msgType = 1;
			tempMsg.moreInfo = resultCode;
			tempMsg.bodySize = 0;

			retVal = sendto(rpSock, (char *)&tempMsg, sizeof(tempMsg), 0, (struct sockaddr *) &peerAddr, sizeof(peerAddr));
			if (retVal == SOCKET_ERROR){
				printf("Predecessor Update response Sendto error!\n");
				exit(1);
			}
			//ReleaseMutex(hMutex);
			break;
		case 5:
			resultCode = 0;

			memset(&tempMsg, 0, sizeof(tempMsg));
			tempMsg.msgID = 5;
			tempMsg.msgType = 1;
			tempMsg.nodeInfo = myNode.chordInfo.fingerInfo.finger[0];
			tempMsg.moreInfo = resultCode;
			tempMsg.bodySize = 0;

			retVal = sendto(rpSock, (char *)&tempMsg, sizeof(tempMsg), 0, (struct sockaddr *) &peerAddr, sizeof(peerAddr));
			if (retVal == SOCKET_ERROR){
				printf("Successor Info response Sendto error!\n");
				exit(1);
			}
			break;
		case 6:
			resultCode = 0;

			WaitForSingleObject(hMutex, INFINITE);
			myNode.chordInfo.fingerInfo.finger[0] = bufMsg.nodeInfo;
			ReleaseMutex(hMutex);
			memset(&tempMsg, 0, sizeof(tempMsg));
			tempMsg.msgID = 6;
			tempMsg.msgType = 1;
			tempMsg.moreInfo = resultCode;
			tempMsg.bodySize = 0;

			retVal = sendto(rpSock, (char *)&tempMsg, sizeof(tempMsg), 0, (struct sockaddr *) &peerAddr, sizeof(peerAddr));
			if (retVal == SOCKET_ERROR){
				printf("Successor Update response Sendto error!\n");
				exit(1);
			}
			break;
		case 7:
			resultCode = 0;

			predNode = find_predecessor(rqSock, bufMsg.nodeInfo.ID);
			memset(&tempMsg, 0, sizeof(tempMsg));
			tempMsg.msgID = 7;
			tempMsg.msgType = 1;
			tempMsg.nodeInfo = predNode;
			tempMsg.moreInfo = resultCode;
			tempMsg.bodySize = 0;

			retVal = sendto(rpSock, (char *)&tempMsg, sizeof(tempMsg), 0, (struct sockaddr *) &peerAddr, sizeof(peerAddr));
			if (retVal == SOCKET_ERROR){
				printf("Find Predecessor response Sendto error!\n");
				exit(1);
			}
			break;
		case 8:
			resultCode = 0;

			/*for (i = 0; i < myNode.fileInfo.fileNum; i++)
			{
				printf("CHORD> %dth own file name: %s, key: %d\n", i + 1, myNode.fileInfo.fileRef[i].Name, myNode.fileInfo.fileRef[i].Key);
			}*/
			memset(&tempMsg, 0, sizeof(tempMsg));
			tempMsg.msgID = 8;
			tempMsg.msgType = 1;
			tempMsg.moreInfo = resultCode;
			tempMsg.bodySize = 0;

			retVal = sendto(rpSock, (char *)&tempMsg, sizeof(tempMsg), 0, (struct sockaddr *) &peerAddr, sizeof(peerAddr));
			if (retVal == SOCKET_ERROR){
				printf("LeaveKeys response Sendto error!\n");
				exit(1);
			}
			printf("bufsize : %d\n", bufMsg.bodySize);
			body = (char*)malloc(sizeof(char)*bufMsg.bodySize);//이 메모리 영역을 할당해주어야 한다 그렇지 않으면 바로 밑에서 body가 값을 받아올때 뒤에 어떤 배열이 있을 수 있는데 그 배열에 덮어쓰게 되어버린다. 그래서 받아오는 곳의 공간을 확보해주어야 한다!!!
			retVal = recvfrom(rpSock, (char*)body, bufMsg.bodySize, 0, (struct sockaddr *) &peerAddr, &addrSize);
			if (retVal == SOCKET_ERROR){
				printf("LeaveKeys response body Recvfrom error!\n");
				exit(1);
			}

			printf("CHORD> keyCount: %d, bodySize: %d\n", bufMsg.moreInfo, bufMsg.bodySize);
			printf("CHORD> RefFileNum: %d\n", myNode.chordInfo.FRefInfo.fileNum);

			/*for (i = 0; i < myNode.fileInfo.fileNum; i++)
			{
				printf("CHORD> %dth own file name: %s, key: %d\n", i + 1, myNode.fileInfo.fileRef[i].Name, myNode.fileInfo.fileRef[i].Key);
			}*/
			for (i = 0; i < bufMsg.moreInfo; i++){
				WaitForSingleObject(hMutex, INFINITE);
				memcpy(&myNode.chordInfo.FRefInfo.fileRef[myNode.chordInfo.FRefInfo.fileNum], body, 76);
				printf("CHORD> Name: %s, Key: %d\n", myNode.chordInfo.FRefInfo.fileRef[myNode.chordInfo.FRefInfo.fileNum].Name, myNode.chordInfo.FRefInfo.fileRef[myNode.chordInfo.FRefInfo.fileNum].Key);
				myNode.chordInfo.FRefInfo.fileNum++;
				ReleaseMutex(hMutex);
				body = body + 76;
			}

			retVal = sendto(rpSock, (char *)&tempMsg, sizeof(tempMsg), 0, (struct sockaddr *) &peerAddr, sizeof(peerAddr));
			if (retVal == SOCKET_ERROR){
				printf("LeaveKeys response body Sendto error!\n");
				exit(1);
			}
			break;
		case 9:
			resultCode = 0;
			if (myNode.chordInfo.FRefInfo.fileNum == FileMax) { // file ref number is full 
				printf("\a[ERROR] My Node Cannot Add more file ref info. File Ref Space is Full!\n");
				resultCode = -1;
			}
			WaitForSingleObject(hMutex, INFINITE);
			// file ref info update
			myNode.chordInfo.FRefInfo.fileRef[myNode.chordInfo.FRefInfo.fileNum] = bufMsg.fileInfo;
			myNode.chordInfo.FRefInfo.fileNum++;
			ReleaseMutex(hMutex);

			memset(&tempMsg, 0, sizeof(tempMsg));
			tempMsg.msgID = 9;
			tempMsg.msgType = 1;
			tempMsg.moreInfo = resultCode;
			tempMsg.bodySize = 0;
			retVal = sendto(rpSock, (char *)&tempMsg, sizeof(tempMsg), 0, (struct sockaddr *) &peerAddr, sizeof(peerAddr));
			if (retVal == SOCKET_ERROR){
				printf("File Reference Add tempMsg response Sendto error!\n");
				exit(1);
			}
			break;
		case 10:
			resultCode = 0;

			targetKey = bufMsg.moreInfo;

			for (i = 0; i < myNode.chordInfo.FRefInfo.fileNum; i++){
				if (myNode.chordInfo.FRefInfo.fileRef[i].Key == targetKey){
					for (j = i; j < myNode.chordInfo.FRefInfo.fileNum - 1; j++){
						WaitForSingleObject(hMutex, INFINITE);
						myNode.chordInfo.FRefInfo.fileRef[j] = myNode.chordInfo.FRefInfo.fileRef[j + 1];
						ReleaseMutex(hMutex);
					}
					WaitForSingleObject(hMutex, INFINITE);
					myNode.chordInfo.FRefInfo.fileNum--;
					ReleaseMutex(hMutex);
					i--;// 배열 땡겼으니까
				}
			}
			memset(&tempMsg, 0, sizeof(tempMsg));
			tempMsg.msgID = 10;
			tempMsg.msgType = 1;
			tempMsg.moreInfo = resultCode;
			tempMsg.bodySize = 0;
			retVal = sendto(rpSock, (char *)&tempMsg, sizeof(tempMsg), 0, (struct sockaddr *) &peerAddr, sizeof(peerAddr));
			if (retVal == SOCKET_ERROR){
				printf("File Reference Delete tempMsg response Sendto error!\n");
				exit(1);
			}

			break;
		case 11:
			resultCode = 0;

			targetKey = bufMsg.moreInfo;

			for (i = 0; i < myNode.fileInfo.fileNum; i++){
				if (targetKey == myNode.fileInfo.fileRef[i].Key){
					ZeroMemory(fileName, FNameMax);
					WaitForSingleObject(hMutex, INFINITE);
					strcpy(fileName, myNode.fileInfo.fileRef[i].Name);
					ReleaseMutex(hMutex);
					fp = fopen(fileName, "rb");
					if (fp == NULL){
						perror("파일 입출력 오류");
						return -1;
					}
					break;
				}
			}

			fseek(fp, 0, SEEK_END);
			totalbytes = ftell(fp);

			printf("totalbytes: %d\n", totalbytes);

			memset(&tempMsg, 0, sizeof(tempMsg));
			tempMsg.msgID = 11;
			tempMsg.msgType = 1;
			tempMsg.moreInfo = resultCode;
			tempMsg.bodySize = totalbytes;

			retVal = sendto(rpSock, (char *)&tempMsg, sizeof(tempMsg), 0, (struct sockaddr *) &peerAddr, sizeof(peerAddr));
			if (retVal == SOCKET_ERROR){
				printf("File  Down response Sendto error!\n");
				exit(1);
			}

			addrSize = sizeof(serveraddr);
			fsSock = accept(flSock, (struct sockaddr *)&serveraddr, &addrSize);//지금 여기서 블락킹 되있는것 같은데
			if (fsSock == INVALID_SOCKET){
				err_display("accept()");
				exit(1);
			}

			numTotal = 0;

			rewind(fp);
			while (1){
				numRead = fread(fileBuf, 1, fBufSize, fp);
				//printf("numRead : %d\n", numRead);
				if (numRead > 0){
					retVal = send(fsSock, fileBuf, numRead, 0);
					if (retVal == SOCKET_ERROR){
						err_display("send()");
						break;
					}
					numTotal += numRead;
				}
				else if (numRead == 0 && numTotal == totalbytes){
					printf("파일 전송 완료!: %d 바이트\n", numTotal);
					break;
				}
				else {
					perror("파일 입출력 오류");
					break;
				}
			}
			break;
		case 12:
			resultCode = 0;

			targetKey = bufMsg.moreInfo;
			for (i = 0; i < myNode.chordInfo.FRefInfo.fileNum; i++){
				if (targetKey == myNode.chordInfo.FRefInfo.fileRef[i].Key) {
					WaitForSingleObject(hMutex, INFINITE);
					tempMsg.nodeInfo = myNode.chordInfo.FRefInfo.fileRef[i].owner;
					ReleaseMutex(hMutex);
					break;
				}
			}
			memset(&tempMsg, 0, sizeof(tempMsg));
			tempMsg.msgID = 12;
			tempMsg.msgType = 1;
			tempMsg.moreInfo = resultCode;
			tempMsg.bodySize = 0;

			retVal = sendto(rpSock, (char *)&tempMsg, sizeof(tempMsg), 0, (struct sockaddr *) &peerAddr, sizeof(peerAddr));
			if (retVal == SOCKET_ERROR){
				printf("File Reference Info response Sendto error!\n");
				exit(1);
			}
			break;
		}
		if (sMode == 0) {
			printf("CHORD> \n");
			printf("CHORD> ");
		}
		memset(&bufMsg, 0, sizeof(bufMsg));
	}
}

int fix_finger()
{
	int i;

	if (!memcmp(&myNode.nodeInfo, &init, sizeof(init))){
		return -1;
	}

	for (i = 1; i < baseM; i++){
		myNode.chordInfo.fingerInfo.finger[i] = find_successor(rqSock, modPlus(ringSize, myNode.nodeInfo.ID, twoPow(i)));
		printf("CHORD> FixFinger: finger[%d] has been updated to %s<ID %d>\n", i + 1, inet_ntoa(myNode.chordInfo.fingerInfo.finger[i].addrInfo.sin_addr), myNode.chordInfo.fingerInfo.finger[i].ID);
	}
	printf("CHORD> Node join has been successfully finished.\n");
	return 0;
}

int move_keys()
{
	int retVal;
	struct sockaddr_in peerAddr, targetAddr;
	int fileSize = 0;
	chordHeaderType tempMsg, bufMsg;
	char* body = NULL;
	int i, j;
	memset(&bufMsg, 0, sizeof(bufMsg));
	bufMsg.msgID = 2;
	bufMsg.msgType = 0;
	bufMsg.nodeInfo = myNode.nodeInfo;
	bufMsg.moreInfo = 0;
	bufMsg.bodySize = 0;

	retVal = sendto(rqSock, (char *)&bufMsg, sizeof(bufMsg), 0, (struct sockaddr *) &myNode.chordInfo.fingerInfo.finger[0].addrInfo, sizeof(myNode.chordInfo.fingerInfo.finger[0].addrInfo));
	if (retVal == SOCKET_ERROR){
		printf("MoveKeys request Sendto error!\n");
		exit(1);
	}

	//printf("%s, %d\n", inet_ntoa(bufMsg.nodeInfo.addrInfo.sin_addr), ntohs(bufMsg.nodeInfo.addrInfo.sin_port));
	//printf("%s, %d\n", inet_ntoa(myNode.chordInfo.fingerInfo.finger[0].addrInfo.sin_addr), ntohs(myNode.chordInfo.fingerInfo.finger[0].addrInfo.sin_port));

	printf("CHORD> MoveKeys request Message has been sent.\n");

	retVal = recvfrom(rqSock, (char *)&tempMsg, sizeof(tempMsg), 0, NULL, NULL);
	if (retVal == SOCKET_ERROR){
		printf("MoveKeys tempMsg request Recvfrom error!\n");
		exit(1);
	}

	retVal = sendto(rqSock, (char *)&bufMsg, sizeof(bufMsg), 0, (struct sockaddr *) &myNode.chordInfo.fingerInfo.finger[0].addrInfo, sizeof(myNode.chordInfo.fingerInfo.finger[0].addrInfo));
	if (retVal == SOCKET_ERROR){
		printf("MoveKeys body request Sendto error!\n");
		exit(1);
	}

	//printf("bodysize : %d\n", tempMsg.bodySize);
	body = (char*)malloc(sizeof(char)*tempMsg.bodySize);
	retVal = recvfrom(rqSock, (char *)body, tempMsg.bodySize, 0, NULL, NULL);
	if (retVal == SOCKET_ERROR){
		printf("MoveKeys body request Recvfrom error!\n");
		exit(1);
	}
	printf("CHORD> MoveKeys response Message has been received.\n");
	WaitForSingleObject(hMutex, INFINITE);
	myNode.chordInfo.FRefInfo.fileNum = tempMsg.moreInfo;
	ReleaseMutex(hMutex);
	for (i = 0; i < myNode.chordInfo.FRefInfo.fileNum; i++){
		WaitForSingleObject(hMutex, INFINITE);
		memcpy(&myNode.chordInfo.FRefInfo.fileRef[i], body, 76);
		ReleaseMutex(hMutex);
		body = body + 76;
	}
	printf("CHORD> You got %d keys from your successor node.\n", tempMsg.moreInfo);

	return 0;
}

nodeInfoType find_successor(SOCKET socket, int IDKey)
{
	int retVal;
	struct sockaddr_in peerAddr, targetAddr;
	chordHeaderType tempMsg, bufMsg;
	nodeInfoType predNode;

	if ((modIn(ringSize, IDKey, myNode.nodeInfo.ID, myNode.chordInfo.fingerInfo.finger[0].ID, 0, 1)))
	{
		return myNode.chordInfo.fingerInfo.finger[0];
	}

	if (myNode.nodeInfo.ID == IDKey){
		return myNode.nodeInfo;
	
	}
	else {
		predNode = find_predecessor(socket, IDKey);
		//printf("predNode in findsucc: %d\n", predNode.ID);
		if (predNode.ID == myNode.nodeInfo.ID)
		{
			return myNode.chordInfo.fingerInfo.finger[0];
		}
		memset(&tempMsg, 0, sizeof(tempMsg));
		memset(&bufMsg, 0, sizeof(bufMsg));
		bufMsg.msgID = 5;
		bufMsg.msgType = 0;
		bufMsg.moreInfo = 0;
		bufMsg.bodySize = 0;
		//A에서 A노드에 보내주면 recv에서 블락되서 안빠져나온다.
		//printf("%s, %d\n", inet_ntoa(predNode.addrInfo.sin_addr), ntohs(predNode.addrInfo.sin_port));
		retVal = sendto(socket, (char *)&bufMsg, sizeof(bufMsg), 0, (struct sockaddr *) &predNode.addrInfo, sizeof(predNode.addrInfo));
		if (retVal == SOCKET_ERROR){
			printf("Successor Info request Sendto error!\n");
			exit(1);
		}
		retVal = recvfrom(socket, (char *)&tempMsg, sizeof(tempMsg), 0, NULL, NULL);
		//printf("%s, %d\n", inet_ntoa(tempMsg.nodeInfo.addrInfo.sin_addr), ntohs(tempMsg.nodeInfo.addrInfo.sin_port));
		if (retVal == SOCKET_ERROR){
			printf("Successor Info request Recvfrom error!\n");
			//exit(1);
			return tempMsg.nodeInfo;
		}
		return tempMsg.nodeInfo;
	}
}

nodeInfoType find_predecessor(SOCKET socket, int IDKey)
{
	int retVal;
	struct sockaddr_in peerAddr, targetAddr;
	chordHeaderType tempMsg, bufMsg;
	nodeType tempNode = myNode;

	if (tempNode.nodeInfo.ID == tempNode.chordInfo.fingerInfo.finger[0].ID){ // special case: the initial node
		return tempNode.nodeInfo;
	}
	//printf("%d    %d    %d\n", IDKey, tempNode.nodeInfo.ID, tempNode.chordInfo.fingerInfo.finger[0].ID);
	while (!modIn(ringSize, IDKey, tempNode.nodeInfo.ID, tempNode.chordInfo.fingerInfo.finger[0].ID, 0, 1))
	{
		WaitForSingleObject(hMutex, INFINITE);
		tempNode.nodeInfo = find_closest_predecessor(tempNode, IDKey);
		ReleaseMutex(hMutex);
		//printf("temp : %d\n", tempNode.nodeInfo.ID);//58을 리턴
		memset(&bufMsg, 0, sizeof(bufMsg));
		bufMsg.msgID = 5;
		bufMsg.msgType = 0;
		bufMsg.moreInfo = 0;
		bufMsg.bodySize = 0;

		retVal = sendto(socket, (char *)&bufMsg, sizeof(bufMsg), 0, (struct sockaddr *) &tempNode.nodeInfo.addrInfo, sizeof(tempNode.nodeInfo.addrInfo));
		if (retVal == SOCKET_ERROR){
			printf("Successor Info request Sendto error!\n");
			exit(1);
		}

		retVal = recvfrom(socket, (char *)&tempMsg, sizeof(tempMsg), 0, NULL, NULL);
		//printf("%s, %d\n", inet_ntoa(tempMsg.nodeInfo.addrInfo.sin_addr), ntohs(tempMsg.nodeInfo.addrInfo.sin_port));
		if (retVal == SOCKET_ERROR){
			printf("Successor Info request Recvfrom error!\n");
			break;
		}
		tempNode.chordInfo.fingerInfo.finger[0] = tempMsg.nodeInfo;
		//printf("%d %d\n", tempNode.nodeInfo.ID, tempNode.chordInfo.fingerInfo.finger[0].ID);//58 39 -> 58 2
		if (!modIn(ringSize, IDKey, tempNode.nodeInfo.ID, tempNode.chordInfo.fingerInfo.finger[0].ID, 0, 1)){
			memset(&bufMsg, 0, sizeof(bufMsg));
			bufMsg.msgID = 7;
			bufMsg.msgType = 0;
			bufMsg.nodeInfo.ID = IDKey;
			bufMsg.moreInfo = 0;
			bufMsg.bodySize = 0;

			retVal = sendto(socket, (char *)&bufMsg, sizeof(bufMsg), 0, (struct sockaddr *) &tempNode.nodeInfo.addrInfo, sizeof(tempNode.nodeInfo.addrInfo));
			if (retVal == SOCKET_ERROR){
				printf("Find Predecessor request Sendto error!\n");
				exit(1);
			}

			retVal = recvfrom(socket, (char *)&tempMsg, sizeof(tempMsg), 0, NULL, NULL);
			//printf("%s, %d\n", inet_ntoa(tempMsg.nodeInfo.addrInfo.sin_addr), ntohs(tempMsg.nodeInfo.addrInfo.sin_port));
			if (retVal == SOCKET_ERROR){
				printf("Find Predecessor request Recvfrom error!\n");
				exit(1);
			}
			//tempNode.nodeInfo = tempMsg.nodeInfo;
			return tempMsg.nodeInfo;
		}
	}
	return tempNode.nodeInfo;
}

nodeInfoType find_closest_predecessor(nodeType curNode, int IDKey)
{
	int i;

	for (i = baseM - 1; i >= 0; i--) {
		if (!memcmp(&curNode.chordInfo.fingerInfo.finger[i], &init, sizeof(init))) //비어있는지 확인하기 위해 init사용
			continue;//이거해도 i는 +1된다.
		if (modIn(ringSize, curNode.chordInfo.fingerInfo.finger[i].ID, curNode.nodeInfo.ID, IDKey, 0, 0))
		{
			return curNode.chordInfo.fingerInfo.finger[i];
		}
	}
	return curNode.nodeInfo;
}

void stabilizeL(SOCKET socket, int leaveID)
{
	nodeInfoType P, succNode;
	nodeInfoType ppred;
	int retVal;
	struct sockaddr_in peerAddr, targetAddr;
	chordHeaderType tempMsg, bufMsg;

	WaitForSingleObject(hMutex, INFINITE);
	succNode = myNode.chordInfo.fingerInfo.finger[0]; /*successor */
	ReleaseMutex(hMutex);
	//printf("%d, %d\n", myNode.nodeInfo.ID, succNode.ID);

	while (1){
		memset(&bufMsg, 0, sizeof(bufMsg));
		bufMsg.msgID = 3;
		bufMsg.msgType = 0;
		bufMsg.moreInfo = 0;
		bufMsg.bodySize = 0;

		//printf("%d\n", succNode.ID);
		retVal = sendto(socket, (char *)&bufMsg, sizeof(bufMsg), 0, (struct sockaddr *) &succNode.addrInfo, sizeof(succNode.addrInfo));
		if (retVal == SOCKET_ERROR){
			printf("Predecessor Info request Sendto error!\n");
			exit(1);
		}

		//printf("CHORD> PredInfo request Message has been sent.\n");

		retVal = recvfrom(socket, (char *)&tempMsg, sizeof(tempMsg), 0, NULL, NULL);
		if (retVal == SOCKET_ERROR){
			printf("Predecessor Info request Recvfrom error!\n");
			exit(1);
		}
		//printf("CHORD> PredInfo response Message has been received.\n");

		if (tempMsg.nodeInfo.ID != leaveID){
			P = tempMsg.nodeInfo;
			break;
		}
		else{
			continue;
		}
	}
	//printf("39id : %d\n", P.ID);
	
	printf("You got your successor's predecessor node from your seccessor node.\n");

	if (memcmp(&P, &init, sizeof(init))){
		//////////////////////////////////////////
		if ((myNode.nodeInfo.ID == succNode.ID))
		{
			WaitForSingleObject(hMutex, INFINITE);
			myNode.chordInfo.fingerInfo.finger[0] = P;
			ReleaseMutex(hMutex);
			succNode = P;
		}
		while (1){
			memset(&bufMsg, 0, sizeof(bufMsg));
			bufMsg.msgID = 3;
			bufMsg.msgType = 0;
			bufMsg.moreInfo = 0;
			bufMsg.bodySize = 0;

			retVal = sendto(socket, (char *)&bufMsg, sizeof(bufMsg), 0, (struct sockaddr *) &P.addrInfo, sizeof(P.addrInfo));
			if (retVal == SOCKET_ERROR){
				printf("Predecessor Info request Sendto error!\n");
				exit(1);
			}
			retVal = recvfrom(socket, (char *)&tempMsg, sizeof(tempMsg), 0, NULL, NULL);
			if (retVal == SOCKET_ERROR){
				printf("ffffPredecessor Info request Recvfrom error!\n");
				//exit(1);
				//break;
				continue;
			}
			if (tempMsg.nodeInfo.ID != leaveID){
				ppred = tempMsg.nodeInfo;
				break;
			}
			else{
				continue;
			}
		}
		while (memcmp(&ppred, &init, sizeof(init)))
		{
			if (modIn(ringSize, ppred.ID, myNode.nodeInfo.ID, succNode.ID, 0, 0))
			{
				P = ppred;
				memset(&bufMsg, 0, sizeof(bufMsg));
				bufMsg.msgID = 3;
				bufMsg.msgType = 0;
				bufMsg.moreInfo = 0;
				bufMsg.bodySize = 0;

				//printf("pp  : %d\n", P.ID);
				retVal = sendto(socket, (char *)&bufMsg, sizeof(bufMsg), 0, (struct sockaddr *) &ppred.addrInfo, sizeof(ppred.addrInfo));
				if (retVal == SOCKET_ERROR){
					printf("Predecessor Info request Sendto error!\n");
					exit(1);
				}
				retVal = recvfrom(socket, (char *)&tempMsg, sizeof(tempMsg), 0, NULL, NULL);
				if (retVal == SOCKET_ERROR){
					printf("Predecessor Info request Recvfrom error!\n");
					//exit(1);
					continue;
				}

				if (tempMsg.nodeInfo.ID != leaveID){
					ppred = tempMsg.nodeInfo;
					continue;
				}
				else{
					continue;
				}
			}
			else{
				break;
			}
		}
		//printf("pre %d\n", P.ID);
		////////////////////////////////////////
		if (modIn(ringSize, P.ID, myNode.nodeInfo.ID, succNode.ID, 0, 0)) {
			WaitForSingleObject(hMutex, INFINITE);
			myNode.chordInfo.fingerInfo.finger[0] = P;
			ReleaseMutex(hMutex);
			succNode = P;
		}
		else {  // Actually not necessary
			memset(&bufMsg, 0, sizeof(bufMsg));
			bufMsg.msgID = 6;
			bufMsg.msgType = 0;
			bufMsg.nodeInfo = myNode.nodeInfo;
			bufMsg.moreInfo = 0;
			bufMsg.bodySize = 0;
			retVal = sendto(socket, (char *)&bufMsg, sizeof(bufMsg), 0, (struct sockaddr *) &P.addrInfo, sizeof(P.addrInfo));
			if (retVal == SOCKET_ERROR){
				printf("Successor Update request Sendto error!\n");
				exit(1);
			}
			printf("CHORD> SuccUpdate request Message has been sent.\n");

			retVal = recvfrom(socket, (char *)&tempMsg, sizeof(tempMsg), 0, NULL, NULL);
			//printf("%s, %d\n", inet_ntoa(tempMsg.nodeInfo.addrInfo.sin_addr), ntohs(tempMsg.nodeInfo.addrInfo.sin_port));
			if (retVal == SOCKET_ERROR){
				printf("Successor Update request Recvfrom error!\n");
				exit(1);
			}

			printf("CHORD> SuccUpdate response Message has been received.\n");
			printf("CHORD> Your predecessor's successor has been updated as your node.\n");
			notify(P);
		}
		printf("CHORD> Your predecessor has been updated.\n");
	}
	memset(&bufMsg, 0, sizeof(bufMsg));
	bufMsg.msgID = 4;
	bufMsg.msgType = 0;
	bufMsg.nodeInfo = myNode.nodeInfo;
	bufMsg.moreInfo = 0;
	bufMsg.bodySize = 0;

	retVal = sendto(socket, (char *)&bufMsg, sizeof(bufMsg), 0, (struct sockaddr *) &myNode.chordInfo.fingerInfo.finger[0].addrInfo, sizeof(myNode.chordInfo.fingerInfo.finger[0].addrInfo));
	if (retVal == SOCKET_ERROR){
		printf("Sendto error!");
		exit(1);
	}
	printf("CHORD> PredUpdate request Message has been sent.\n");

	retVal = recvfrom(socket, (char *)&tempMsg, sizeof(tempMsg), 0, NULL, NULL);
	//printf("%s, %d\n", inet_ntoa(tempMsg.nodeInfo.addrInfo.sin_addr), ntohs(tempMsg.nodeInfo.addrInfo.sin_port));
	if (retVal == SOCKET_ERROR){
		printf("Predecessor Update request Recvfrom error!\n");
		exit(1);
	}
	printf("CHORD> PredUpdate response Message has been received.\n");
	printf("CHORD> Your successor's predecessor has been updated as your node.\n");
}
void stabilizeJ(SOCKET socket)
{
	nodeInfoType P, succNode;
	int retVal;
	struct sockaddr_in peerAddr, targetAddr;
	chordHeaderType tempMsg, bufMsg;

	WaitForSingleObject(hMutex, INFINITE);
	succNode = myNode.chordInfo.fingerInfo.finger[0]; /*successor */
	ReleaseMutex(hMutex);
	//printf("%d, %d\n", myNode.nodeInfo.ID, succNode.ID);

	memset(&bufMsg, 0, sizeof(bufMsg));
	bufMsg.msgID = 3;
	bufMsg.msgType = 0;
	bufMsg.moreInfo = 0;
	bufMsg.bodySize = 0;

	//printf("%d\n", succNode.ID);
	retVal = sendto(socket, (char *)&bufMsg, sizeof(bufMsg), 0, (struct sockaddr *) &succNode.addrInfo, sizeof(succNode.addrInfo));
	if (retVal == SOCKET_ERROR){
		printf("Predecessor Info request Sendto error!\n");
		exit(1);
	}

	printf("CHORD> PredInfo request Message has been sent.\n");

	retVal = recvfrom(socket, (char *)&tempMsg, sizeof(tempMsg), 0, NULL, NULL);
	if (retVal == SOCKET_ERROR){
		printf("Predecessor Info request Recvfrom error!\n");
		exit(1);
	}
	printf("CHORD> PredInfo response Message has been received.\n");

	P = tempMsg.nodeInfo;
	//printf("39id : %d\n", P.ID);

	printf("You got your successor's predecessor node from your seccessor node.\n");

	if (memcmp(&P, &init, sizeof(init))){
		if (modIn(ringSize, P.ID, myNode.nodeInfo.ID, succNode.ID, 0, 0)) {
			WaitForSingleObject(hMutex, INFINITE);
			myNode.chordInfo.fingerInfo.finger[0] = P;
			ReleaseMutex(hMutex);
			succNode = P;
		}
		else {  // Actually not necessary
			memset(&bufMsg, 0, sizeof(bufMsg));
			bufMsg.msgID = 6;
			bufMsg.msgType = 0;
			bufMsg.nodeInfo = myNode.nodeInfo;
			bufMsg.moreInfo = 0;
			bufMsg.bodySize = 0;
			retVal = sendto(socket, (char *)&bufMsg, sizeof(bufMsg), 0, (struct sockaddr *) &P.addrInfo, sizeof(P.addrInfo));
			if (retVal == SOCKET_ERROR){
				printf("Successor Update request Sendto error!\n");
				exit(1);
			}
			printf("CHORD> SuccUpdate request Message has been sent.\n");

			retVal = recvfrom(socket, (char *)&tempMsg, sizeof(tempMsg), 0, NULL, NULL);
			//printf("%s, %d\n", inet_ntoa(tempMsg.nodeInfo.addrInfo.sin_addr), ntohs(tempMsg.nodeInfo.addrInfo.sin_port));
			if (retVal == SOCKET_ERROR){
				printf("Successor Update request Recvfrom error!\n");
				exit(1);
			}

			printf("CHORD> SuccUpdate response Message has been received.\n");
			printf("CHORD> Your predecessor's successor has been updated as your node.\n");
			notify(P);
		}
		printf("CHORD> Your predecessor has been updated.\n");
	}
	memset(&bufMsg, 0, sizeof(bufMsg));
	bufMsg.msgID = 4;
	bufMsg.msgType = 0;
	bufMsg.nodeInfo = myNode.nodeInfo;
	bufMsg.moreInfo = 0;
	bufMsg.bodySize = 0;

	retVal = sendto(socket, (char *)&bufMsg, sizeof(bufMsg), 0, (struct sockaddr *) &myNode.chordInfo.fingerInfo.finger[0].addrInfo, sizeof(myNode.chordInfo.fingerInfo.finger[0].addrInfo));
	if (retVal == SOCKET_ERROR){
		printf("Sendto error!");
		exit(1);
	}
	printf("CHORD> PredUpdate request Message has been sent.\n");

	retVal = recvfrom(socket, (char *)&tempMsg, sizeof(tempMsg), 0, NULL, NULL);
	//printf("%s, %d\n", inet_ntoa(tempMsg.nodeInfo.addrInfo.sin_addr), ntohs(tempMsg.nodeInfo.addrInfo.sin_port));
	if (retVal == SOCKET_ERROR){
		printf("Predecessor Update request Recvfrom error!\n");
		exit(1);
	}
	printf("CHORD> PredUpdate response Message has been received.\n");
	printf("CHORD> Your successor's predecessor has been updated as your node.\n");
}

void notify(nodeInfoType targetNode)
{
	WaitForSingleObject(hMutex, INFINITE);
	nodeInfoType Pre = myNode.chordInfo.fingerInfo.Pre;
	ReleaseMutex(hMutex);
	if (!memcmp(&Pre, &init, sizeof(init)) || !memcmp(&Pre, &myNode.chordInfo.fingerInfo.Pre, sizeof(myNode.chordInfo.fingerInfo.Pre)) || modIn(ringSize, targetNode.ID, Pre.ID, myNode.nodeInfo.ID, 0, 0))
	{
		WaitForSingleObject(hMutex, INFINITE);
		myNode.chordInfo.fingerInfo.Pre = targetNode;
		ReleaseMutex(hMutex);
	}
}

void procPPandFF(void *arg)
{
	int *exitFlag = (int *)arg;
	unsigned int delayTime, varTime;
	int retVal, optVal = 5000;
	int i, j, targetKey;
	int isPred=0;
	int isUnPongPre = 0;
	int isUnPongSucc = 0;
	int preLeaveID = -1;
	int succLeaveID = -1;
	int isEmpty = 0;
	struct sockaddr_in peerAddr;
	chordHeaderType tempMsg, bufMsg;
	nodeInfoType succNode, predNode;

	bufMsg.msgID = 0;
	bufMsg.msgType = 0;
	srand(time(NULL));

	pfSock = socket(AF_INET, SOCK_DGRAM, 0); // for ping-pong and fix-finger
	retVal = setsockopt(pfSock, SOL_SOCKET, SO_RCVTIMEO, (char *)&optVal, sizeof(optVal));
	if (retVal == SOCKET_ERROR) {
		printf("\a[ERROR] setsockopt() Error!\n");
		exit(1);
	}

	while (!(*exitFlag)) {
		isUnPongPre = 0;
		isUnPongSucc = 0;
		preLeaveID = -1;
		if (myNode.nodeInfo.ID != myNode.chordInfo.fingerInfo.Pre.ID && (myNode.chordInfo.fingerInfo.Pre.ID != 0)){
			//printf("addr : %s, %d\n", inet_ntoa(myNode.chordInfo.fingerInfo.Pre.addrInfo.sin_addr), ntohs(myNode.chordInfo.fingerInfo.Pre.addrInfo.sin_port));
			retVal = sendto(pfSock, (char *)&bufMsg, sizeof(bufMsg), 0, (struct sockaddr *) &myNode.chordInfo.fingerInfo.Pre.addrInfo, sizeof(myNode.chordInfo.fingerInfo.Pre.addrInfo));
			if (retVal == SOCKET_ERROR){
				printf("Pingpong Pre request Sendto error!\n");
				exit(1);
			}
			retVal = recvfrom(pfSock, (char *)&tempMsg, sizeof(tempMsg), 0, NULL, NULL);
			if (retVal == SOCKET_ERROR){
				printf("Pingpong Pre request Recvfrom error!\n");
				//응답이 제시간에 안오면 여길 들어오니까 여기서 해결하면 된다.
				preLeaveID = myNode.chordInfo.fingerInfo.Pre.ID;
				memset(&myNode.chordInfo.fingerInfo.Pre, 0, sizeof(myNode.chordInfo.fingerInfo.Pre));
				isUnPongPre = 1;
			}
		}
		if (myNode.nodeInfo.ID != myNode.chordInfo.fingerInfo.finger[0].ID){
			retVal = sendto(pfSock, (char *)&bufMsg, sizeof(bufMsg), 0, (struct sockaddr *) &myNode.chordInfo.fingerInfo.finger[0].addrInfo, sizeof(myNode.chordInfo.fingerInfo.finger[0].addrInfo));
			if (retVal == SOCKET_ERROR){
				printf("Pingpong Finger[0] request Sendto error!\n", i);
				exit(1);
			}

			retVal = recvfrom(pfSock, (char *)&tempMsg, sizeof(tempMsg), 0, NULL, NULL);
			if (retVal == SOCKET_ERROR){
				printf("Pingpong Finger[0] request Recvfrom error!\n", i);
				succLeaveID = myNode.chordInfo.fingerInfo.finger[0].ID;
				isUnPongSucc = 1;
				if ((isUnPongPre == 1) && (isUnPongSucc == 1)){
					for (i = 5; i>0 ; i--){
						isEmpty = 1;
						if ((myNode.chordInfo.fingerInfo.finger[i].ID != preLeaveID) && (myNode.chordInfo.fingerInfo.finger[i].ID != succLeaveID))
						{
							myNode.chordInfo.fingerInfo.Pre = myNode.chordInfo.fingerInfo.finger[i];
							myNode.chordInfo.fingerInfo.finger[0] = myNode.chordInfo.fingerInfo.finger[i];
							stabilizeL(pfSock, succLeaveID);
							isEmpty = 0;
							break;
						}
					}
					if (isEmpty == 1){
						myNode.chordInfo.fingerInfo.Pre = myNode.nodeInfo;
						for (i = 0; i < baseM; i++){
							myNode.chordInfo.fingerInfo.finger[i] = myNode.nodeInfo;
						}
					}
				}
				else if (isUnPongSucc == 1){
					myNode.chordInfo.fingerInfo.finger[0] = myNode.chordInfo.fingerInfo.Pre;
					stabilizeL(pfSock, succLeaveID);
					succLeaveID = -1;
				}
			}
		}

		for (i = baseM-1; i > 0; i--){
			if (myNode.nodeInfo.ID != myNode.chordInfo.fingerInfo.finger[i].ID){
				retVal = sendto(pfSock, (char *)&bufMsg, sizeof(bufMsg), 0, (struct sockaddr *) &myNode.chordInfo.fingerInfo.finger[i].addrInfo, sizeof(myNode.chordInfo.fingerInfo.finger[i].addrInfo));
				if (retVal == SOCKET_ERROR){
					printf("Pingpong Finger[%d] request Sendto error!\n", i);
					//exit(1);
					continue;
				}

				retVal = recvfrom(pfSock, (char *)&tempMsg, sizeof(tempMsg), 0, NULL, NULL);
				if (retVal == SOCKET_ERROR){
					printf("Pingpong Finger[%d] request Recvfrom error!\n", i);
					if (i == baseM - 1){
						WaitForSingleObject(hMutex, INFINITE);
						myNode.chordInfo.fingerInfo.finger[i] = myNode.chordInfo.fingerInfo.Pre;
						ReleaseMutex(hMutex);
					}
					else{
						WaitForSingleObject(hMutex, INFINITE);
						myNode.chordInfo.fingerInfo.finger[i] = myNode.chordInfo.fingerInfo.finger[i + 1];
						ReleaseMutex(hMutex);
					}

					/*if (i == 0){
						isPred = 1;
					}*/
					
				}
			}
		}

		for (i = 1; i < baseM; i++){
			myNode.chordInfo.fingerInfo.finger[i] = find_successor(pfSock, modPlus(ringSize, myNode.nodeInfo.ID, twoPow(i)));
			if (sMode == 0){
				printf("CHORD> Periodic FixFinger: finger[%d] has been updated to %s<ID %d>\n", i + 1, inet_ntoa(myNode.chordInfo.fingerInfo.finger[i].addrInfo.sin_addr), myNode.chordInfo.fingerInfo.finger[i].ID);
			}
		}
		//ReleaseMutex(hMutex);
		//WaitForSingleObject(hMutex, INFINITE);
		varTime = rand() % 2000;
		delayTime = 2000 + varTime;  // delay: 8~10 sec
		Sleep(delayTime);
	}
}

int modIn(int modN, int targNum, int range1, int range2, int leftmode, int rightmode)
// leftmode, rightmode: 0 => range boundary not included, 1 => range boundary included   
{
	int result = 0;

	if (range1 == range2) {
		if ((leftmode == 0) || (rightmode == 0))
			return 0;
	}

	if (modPlus(ringSize, range1, 1) == range2) {
		if ((leftmode == 0) && (rightmode == 0))
			return 0;
	}

	if (leftmode == 0)
		range1 = modPlus(ringSize, range1, 1);
	if (rightmode == 0)
		range2 = modMinus(ringSize, range2, 1);

	if (range1  < range2) {
		if ((targNum >= range1) && (targNum <= range2))
			result = 1;
	}
	else if (range1 > range2) {
		if (((targNum >= range1) && (targNum < modN))
			|| ((targNum >= 0) && (targNum <= range2)))
			result = 1;
	}
	else if ((targNum == range1) && (targNum == range2))
		result = 1;

	return result;
}

int twoPow(int power)
{
	int i;
	int result = 1;

	if (power >= 0)
		for (i = 0; i<power; i++)
			result *= 2;
	else
		result = -1;

	return result;
}

int modMinus(int modN, int minuend, int subtrand)
{
	if (minuend - subtrand >= 0)
		return minuend - subtrand;
	else
		return (modN - subtrand) + minuend;
}

int modPlus(int modN, int addend1, int addend2)
{
	if (addend1 + addend2 < modN)
		return addend1 + addend2;
	else
		return (addend1 + addend2) - modN;
}

void showCommand(void)
{
	printf("CHORD> Enter a command - (c)reate: Create the chord network\n");
	printf("CHORD> Enter a command - (j)oin  : Join the chord network\n");
	printf("CHORD> Enter a command - (l)eave : Leave the chord network\n");
	printf("CHORD> Enter a command - (a)dd   : Add a file to the network\n");
	printf("CHORD> Enter a command - (d)elete: Delete a file to the network\n");
	printf("CHORD> Enter a command - (s)earch: File search and download\n");
	printf("CHORD> Enter a command - (f)inger: Show the finger table\n");
	printf("CHORD> Enter a command - (i)nfo  : Show the node information\n");
	printf("CHORD> Enter a command - (m)ute  : Toggle the silent mode\n");
	printf("CHORD> Enter a command - (h)elp  : Show the help message\n");
	printf("CHORD> Enter a command - (q)uit  : Quit the program\n");
}

char *fgetsCleanup(char *string)
{
	if (string[strlen(string) - 1] == '\n')
		string[strlen(string) - 1] = '\0';
	else
		flushStdin();

	return string;
}

void flushStdin(void)
{
	int ch;

	fseek(stdin, 0, SEEK_END);
	if (ftell(stdin)>0)
		do
			ch = getchar();
	while (ch != EOF && ch != '\n');
}

static const unsigned char sTable[256] =
{
	0xa3, 0xd7, 0x09, 0x83, 0xf8, 0x48, 0xf6, 0xf4, 0xb3, 0x21, 0x15, 0x78, 0x99, 0xb1, 0xaf, 0xf9,
	0xe7, 0x2d, 0x4d, 0x8a, 0xce, 0x4c, 0xca, 0x2e, 0x52, 0x95, 0xd9, 0x1e, 0x4e, 0x38, 0x44, 0x28,
	0x0a, 0xdf, 0x02, 0xa0, 0x17, 0xf1, 0x60, 0x68, 0x12, 0xb7, 0x7a, 0xc3, 0xe9, 0xfa, 0x3d, 0x53,
	0x96, 0x84, 0x6b, 0xba, 0xf2, 0x63, 0x9a, 0x19, 0x7c, 0xae, 0xe5, 0xf5, 0xf7, 0x16, 0x6a, 0xa2,
	0x39, 0xb6, 0x7b, 0x0f, 0xc1, 0x93, 0x81, 0x1b, 0xee, 0xb4, 0x1a, 0xea, 0xd0, 0x91, 0x2f, 0xb8,
	0x55, 0xb9, 0xda, 0x85, 0x3f, 0x41, 0xbf, 0xe0, 0x5a, 0x58, 0x80, 0x5f, 0x66, 0x0b, 0xd8, 0x90,
	0x35, 0xd5, 0xc0, 0xa7, 0x33, 0x06, 0x65, 0x69, 0x45, 0x00, 0x94, 0x56, 0x6d, 0x98, 0x9b, 0x76,
	0x97, 0xfc, 0xb2, 0xc2, 0xb0, 0xfe, 0xdb, 0x20, 0xe1, 0xeb, 0xd6, 0xe4, 0xdd, 0x47, 0x4a, 0x1d,
	0x42, 0xed, 0x9e, 0x6e, 0x49, 0x3c, 0xcd, 0x43, 0x27, 0xd2, 0x07, 0xd4, 0xde, 0xc7, 0x67, 0x18,
	0x89, 0xcb, 0x30, 0x1f, 0x8d, 0xc6, 0x8f, 0xaa, 0xc8, 0x74, 0xdc, 0xc9, 0x5d, 0x5c, 0x31, 0xa4,
	0x70, 0x88, 0x61, 0x2c, 0x9f, 0x0d, 0x2b, 0x87, 0x50, 0x82, 0x54, 0x64, 0x26, 0x7d, 0x03, 0x40,
	0x34, 0x4b, 0x1c, 0x73, 0xd1, 0xc4, 0xfd, 0x3b, 0xcc, 0xfb, 0x7f, 0xab, 0xe6, 0x3e, 0x5b, 0xa5,
	0xad, 0x04, 0x23, 0x9c, 0x14, 0x51, 0x22, 0xf0, 0x29, 0x79, 0x71, 0x7e, 0xff, 0x8c, 0x0e, 0xe2,
	0x0c, 0xef, 0xbc, 0x72, 0x75, 0x6f, 0x37, 0xa1, 0xec, 0xd3, 0x8e, 0x62, 0x8b, 0x86, 0x10, 0xe8,
	0x08, 0x77, 0x11, 0xbe, 0x92, 0x4f, 0x24, 0xc5, 0x32, 0x36, 0x9d, 0xcf, 0xf3, 0xa6, 0xbb, 0xac,
	0x5e, 0x6c, 0xa9, 0x13, 0x57, 0x25, 0xb5, 0xe3, 0xbd, 0xa8, 0x3a, 0x01, 0x05, 0x59, 0x2a, 0x46
};


#define PRIME_MULT 1717


unsigned int strHash(const char *str)  /* Hash: String to Key */
{
	unsigned int len = sizeof(str);
	unsigned int hash = len, i;
	unsigned int temp;


	for (i = 0; i != len; i++, str++)
	{

		hash ^= sTable[(*str + i) & 255];
		hash = hash * PRIME_MULT;
	}
	temp = hash % ringSize;
	if (temp == 0){
		temp = (hash+1) % ringSize;
	}
	return temp;
}

int recvn(SOCKET s, char *buf, int len, int flags)
{
	int received;
	char *ptr = buf;
	int left = len;

	while (left > 0) {
		received = recv(s, ptr, left, flags);
		if (received == SOCKET_ERROR){
			return SOCKET_ERROR;
		}
		else if (received == 0){
			break;
		}
		left -= received;
		ptr += received;
	}
	return (len - left);
}
void ErrorHandling(char * msg)
{
	fputs(msg, stderr);
	fputc('\n', stderr);
	exit(1);
}