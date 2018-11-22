
# include <stdio.h>
# include <stdlib.h>
# include <poll.h>
# include <unistd.h>
# include <stdarg.h>
# include <assert.h>
# include <sys/stat.h>
# include <fcntl.h>
# include <sys/types.h>
# include <signal.h>
# include <string.h>
# include <malloc.h>


# define MAX_IP 1000;
# define MIN_PRI 4;


typedef enum {ACK, OPEN, QUERY, ADD, RELAY} KIND; // kind of message from lab

// Messages structs
typedef struct {
    int swID;
    int port1;
    int port2;
    int IPlo;
    int Iphi;
    int switchfd;
} MSG_OPEN;

typedef struct {
    int swID;
    int dstIP;
    int srcIP;
    int port1;
    int port2;
} MSG_QUERY;

typedef struct {
    int swID;
    int dstIP;
    int dstIPlo;
    int dstIPhi;
    int srcIP;
    int action;
    int actionVal;
    int pri;
} MSG_ADD;

typedef struct{
    int srcIP;
    int dstIP;
} MDG_RELAY;

// FRAME struct for trans data
typedef union { MSG_OPEN mOpen; MSG_QUERY mQuery; MSG_ADD mAdd; MDG_RELAY mRelay;} MSG;
typedef struct { KIND kind; MSG msg; } FRAME;

// Base switch infomation work as parameter in the code
typedef struct {
    int swID;
    int port1;
    int port2;
    int IPlo;
    int Iphi;
} SwitchInfo;

//typedef struct {
//    int fd;        /* file descriptor */
//    short events;     /* requested events */
//    short revents;    /* returned events */
//}pollfd;

// Base flowtable infomation for switch
typedef struct {
    int srcIPlo;
    int srcIPhi;
    int dstIPlo;
    int dstIPhi;
    int actionType;
    int actionVal;
    int pri;
    int pktCount;
} FlowTable;

// Base counter infomation as parameter for switchPrint()
typedef struct {
    int admitCounter;
    int ackCounter;
    int addRuleCounter;
    int relayInCounter;
    int relayOutCounter;
    int openCounter;
    int queryCounter;
} SwitchCounter;

// base Controller information for USER1 signal handler
typedef struct {
    SwitchInfo switch_list[7];
    int numSwitch;
    int openCounter;
    int queryCounter;
    int ackCounter;
    int addCounter;
} CON;

// base Switch information for USER1 signal handler
typedef struct {
    FlowTable *flows;
    int numFlowTable;
    SwitchCounter swCounter;
} SW;

void user1Controller(int signum);
void user1Switch(int signum);
int executeswitch(SwitchInfo sw, char filename[]);
int switchAction(FlowTable flows[],int srcIP,int dstIP,int numFlowTable);
int printSwitch(FlowTable flows[],int numFlowTable,SwitchCounter swCounter);
MSG controllerRule(MSG_QUERY query, SwitchInfo switch_list[], int numSwitch);
int controller(int numSwitch);
int printController(SwitchInfo switch_list[],int numSwitch,int openCounter,int queryCounter,int ackCounter,int addCounter);
void FATAL (const char *fmt, ... );
int openFIFO(int sender, int revsiver);
FRAME rcvFrame (int fd);
void WARNING (const char *fmt, ... );
void sendFrame (int fd, KIND kind, MSG *msg);

// for USER1 handler
CON Con;
SW Sw;
// since marco do not work well in C, insteadlly using global variable
const int FORWARD = 1;
const int DROP = 0;

int main(int argc, char* argv[]) {

    char name[50];
    strcpy(name, argv[1]);

    if (strcmp(argv[1], "cont") == 0 && argc == 3) {
        /*controler mode*/
	char temp[20];
        strcpy(temp, argv[2]);
	if (atoi(&temp[0]) >= 7 ||atoi(&temp[0])<1) {
            printf("Too much Switchs"); // check number of switches
            return -2;
        }

        /* Bound singal USER1 with handler */
        signal(SIGUSR1, user1Controller);
 		
        /* New arugment argv[3] represent port number */
        controller(atoi(&temp[0])， atoi(&argv[3]));

    } else if (argc == 6 && strncmp(&name[0],"s",1)==0 && strncmp(&name[1],"w",1)==0) {
        /*switch mode*/
        SwitchInfo sw;
        sw.swID = atoi(&name[2]);
         
        char filename[50];
        strcpy(filename, argv[2]);

        // get the port of the switch
        if (strcmp(argv[3],"null")==0) {
            sw.port1 = -1;
        } else {
            sw.port1 = atoi(&argv[3][2]);
        }

        if (strcmp(argv[4],"null")==0) {
            sw.port2 = -1;
        } else {
            sw.port2 = atoi(&argv[4][2]);
        }

        // get the IP range
        char *temp;
        temp = strtok(argv[5], "-");
        sw.IPlo = atoi(temp);
        temp = strtok(NULL, "-");
        sw.Iphi = atoi(temp);

        /* Bound singal USER1 with handler */
        signal(SIGUSR1, user1Switch);

        /* New argument argv[6] represent serverAddress 7 represent portnumber*/
        executeswitch(sw, filename, argv[6], atoi(&argv[7]));

    } else {
        printf("Invalid Command\n");
        return -1;
    }
    return 0;
}

void user1Controller(int signum) {
    // handle the user1 singals when in controller mode
    printf("\n");
    printf("\nUSER1 singal received \n");
    printController(Con.switch_list, Con.numSwitch, Con.openCounter, Con.queryCounter, Con.ackCounter, Con.addCounter);
    return;
}

void user1Switch(int signum) {
    // handle the user1 singals when in switch mode
    printf("\n");
    printf("\nUSER1 singal received \n");
    printSwitch(Sw.flows, Sw.numFlowTable, Sw.swCounter);
    return;
}

int executeswitch(SwitchInfo sw, char filename[], const char* address,  int portnumber) {
    // Open the file
    FILE *filefp;
    filefp = fopen(filename, "r");
    if (filefp == NULL) {
        printf("Fail to read file");
        return -1;
    }


    // set up flows by struct FlowTable
    FlowTable flows[50];

    flows[0].srcIPlo = 0;
    flows[0].srcIPhi = MAX_IP;
    flows[0].dstIPhi = sw.Iphi;
    flows[0].dstIPlo = sw.IPlo;
    flows[0].actionType = FORWARD;
    flows[0].actionVal = 3;
    flows[0].pri = 4;
    flows[0].pktCount = 0;

    // set up counters
    int admitCounter = 0;
    int ackCounter = 0;
    int addRuleCounter = 0;
    int relayInCounter =0;
    int openCounter = 0;
    int queryCounter = 0;
    int relayOutCounter = 0;
    int numFlowTable = 1;
    int queryLocker = 0;

    // set TCP connection
    int fd;
    struct addrinfo hints, *res;
    bzeros(&hints, sizeof(addrinfo));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(NULL, portnumber, &hints, &res) != 0) {
        printf("Fail to tans portnumber.\n");
        return -1;
    }
    /*https://blog.csdn.net/u010889616/article/details/47159937*/
    if ((fd = socket(res->ai_family, res->ai_socktype, res->ar_protocol) == -1) {
        printf("Fail to set TCP socket\n");
        return -1;
    }
    if (connect(fd, res->ai_addr, res->ai_addrlen)) {
        printf("Switch cannot connect\n");
        return -1;
    }

    int fifo[2];
    int fdPort1Read;
    int fdPort1Write;
    int fdPort2Read;
    int fdPort2Write;
    if (sw.port1 != -1) {
        fdPort1Read = openFIFO(sw.port1, sw.swID);
        fdPort1Write = openFIFO(sw.swID, sw.port1);
        fifo[0] = fdPort1Read;
    }
    if (sw.port2 != -1) {
        fdPort2Read = openFIFO(sw.port2, sw.swID);
        fdPort2Write = openFIFO(sw.swID, sw.port2);
        fifo[1] = fdPort2Read;
    }

    // send open message to Controller
    MSG msg;
    msg.mOpen.swID = sw.swID;
    msg.mOpen.port1 = sw.port1;
    msg.mOpen.port2 = sw.port2;
    msg.mOpen.IPlo = sw.IPlo;
    msg.mOpen.Iphi = sw.Iphi;
    msg.mOpen.switchfd = fd;

    // send open message to controller
    sendFrame(fd, OPEN, &msg);
    openCounter += 1;
    printf("\nTransmitted (src= sw%d, dest= cont) [OPEN]:\n", sw.swID);
    printf("    port0= cont, port1= %d, port2= %d, port3= %d-%d\n", sw.port1, sw.port2, sw.IPlo, sw.Iphi);

    while (1) {
        int aimSwith = 0;
        int srcIP = 0;
        int dstIP = 0;

        // wait 500ms for respones from controller
        struct pollfd pollCon[1];
        pollCon[0].fd = fd;
        pollCon[0].events = POLLIN;
        poll(poll_list, 1, 500);

        // read from controller 
        // for update flows
        if (pollCon.revents & POLLIN) {
            FRAME frame;
            frame = rcvFrame(pollCon[0].fd);
	        pollCon[0].revents = -1;

            if (frame.kind == ACK) {
                // ACK message
                ackCounter += 1;
		        printf("\nReceived (src= cont, dest= sw%d) [ACK]\n", sw.swID);
            }

            if (frame.kind == ADD) {
                // add new flow as respones of query so unlock query
                queryLocker = 0;
                numFlowTable++;
                addRuleCounter++;

                // set up new flow
                flows[numFlowTable-1].srcIPlo = 0;
                flows[numFlowTable-1].srcIPhi = MAX_IP;
                flows[numFlowTable-1].dstIPlo = frame.msg.mAdd.dstIPlo;
                flows[numFlowTable-1].dstIPhi = frame.msg.mAdd.dstIPhi;
                flows[numFlowTable-1].actionType = frame.msg.mAdd.action;
                flows[numFlowTable-1].actionVal = frame.msg.mAdd.actionVal;
                flows[numFlowTable-1].pri = frame.msg.mAdd.pri;
                flows[numFlowTable-1].pktCount = 0;

                // print flows messages
                printf("\nReceived (src= cont, dest= sw%d) [ADD]:\n", sw.swID);
                printf("    (srcIP= 0-%d, destIP= %d-%d, ", flows[numFlowTable-1].srcIPhi, flows[numFlowTable-1].dstIPlo, flows[numFlowTable-1].dstIPhi);
                if (flows[numFlowTable].actionType == FORWARD) {
                    printf("action= FORWARD:%d, pri= %d, pktCount= %d\n", flows[numFlowTable-1].actionVal, flows[numFlowTable-1].pri, flows[numFlowTable-1].pktCount);
                } else {
                    printf("action= DROP:%d, pri= %d, pktCount= %d\n", flows[numFlowTable-1].actionVal, flows[numFlowTable-1].pri, flows[numFlowTable-1].pktCount);
                }

                // recheck the message caused query and due with it
                int n = switchAction(flows, frame.msg.mAdd.srcIP, frame.msg.mAdd.dstIP, numFlowTable);
                if (n > 0) {
                    // n > 0: need to relay and n is the port
                    relayOutCounter++;
                    MSG msg;
                    msg.mRelay.srcIP = frame.msg.mAdd.srcIP;
                    msg.mRelay.dstIP = frame.msg.mAdd.dstIP;

                    // send messages
                    if (n==1) {
                        sendFrame(fdPort1Write, RELAY, &msg);
                        printf("\nTransmitted (src= sw%d, dest= sw%d) [RELAY]:  header= (srcIP= %d, destIP= %d)\n", sw.swID, sw.port1, msg.mRelay.srcIP, msg.mRelay.dstIP);
                    } else if (n==2) {
                        sendFrame(fdPort2Write, RELAY, &msg);
                        printf("\nTransmitted (src= sw%d, dest= sw%d) [RELAY]:  header= (srcIP= %d, destIP= %d)\n", sw.swID, sw.port2, msg.mRelay.srcIP, msg.mRelay.dstIP );
                    }
                } else if (n == -1) {
                    // need to query not possible here
                    queryCounter++;
                    MSG msg;
                    msg.mQuery.swID = sw.swID;
                    msg.mQuery.dstIP = msg.mRelay.dstIP;
                    msg.mQuery.srcIP = msg.mRelay.srcIP;
                    msg.mQuery.port1 = sw.port1;
                    msg.mQuery.port2 = sw.port2;
                    sendFrame(fdConWrite, QUERY, &msg);
                    printf("\nTransmitted (src= sw%d, dest= cont)[QUERY]:  header= (srcIP= %d, destIP= %d)\n", sw.swID, srcIP, dstIP);
                } 
            }
        }

        // Loop the check controlle when did not get the responed of query
        // This is avoid send mult query that will make same result
        if (queryLocker ==  1) {
            continue;
        }
        
        // update global variable
        SwitchCounter swCounter;
        swCounter.admitCounter = admitCounter;
        swCounter.ackCounter = ackCounter;
        swCounter.addRuleCounter = addRuleCounter;
        swCounter.relayInCounter = relayInCounter;
        swCounter.relayOutCounter = relayOutCounter;
        swCounter.openCounter = openCounter;
        swCounter.queryCounter = queryCounter;

        Sw.swCounter = swCounter;
        Sw.flows = flows;
        Sw.numFlowTable = numFlowTable;

        // Poll from keyboard
        // http://blog.51cto.com/wait0804/1856818
        struct pollfd keyboard[1];
        keyboard[0].fd = STDIN_FILENO;
        keyboard[0].events = POLLIN;

        // read the input
        poll(keyboard, 1, 0);
        char userCmd[5];
        char userTemp[100];
        if ((keyboard[0].revents & POLLIN)) {
            read(STDIN_FILENO, userCmd, 4);
	        keyboard[0].revents = -1;
            userCmd[4] = '\0';
            if (poll(keyboard, 1, 0)) {
                read(STDIN_FILENO, userTemp, 100);
                keyboard[0].revents = -1;
            }
        }

        // run user cmd - base on input to make output
        // list for list info
        // exit for list and exit
        if (strcmp(userCmd,"list\0")==0) {
            strcpy(userCmd, "ad");
            printSwitch(flows, numFlowTable, swCounter);
        } else if (strcmp(userCmd,"exit\0")==0) {
            printSwitch(flows, numFlowTable, swCounter);
            return 0;
        }

        // read from file
        
        char line[100];
        if (fgets(line, 2, filefp)!=NULL) {
            // fgets get the 2ed arg-1 elements
            if (strcmp(&line[0], "#")==0 || line[0] == '\0'||line[0] == '\r') {
                fgets(line, 100, filefp);
            } else if ( line[0] == '\n') {
                
            }
            else{
                fgets(line, 100, filefp);
                // divide the spilter
                char *temp;
                temp = strtok(line, " ");
                aimSwith = atoi(&temp[1]);
                temp = strtok(NULL, " ");
                srcIP = atoi(temp);
                temp = strtok(NULL, " ");
                dstIP = atoi(temp);
            }
        }

        // if the swithc name is this switch
        // the text part
        if (aimSwith == sw.swID) {
            int n = switchAction(flows, srcIP, dstIP, numFlowTable);
            admitCounter+=1;
            if (n > 0) {
                // n > 0 send to other port
                relayOutCounter++;
                MSG msg;
                msg.mRelay.srcIP = srcIP;
                msg.mRelay.dstIP = dstIP;
                if (n==1) {
                    sendFrame(fdPort1Write, RELAY, &msg);
                    printf("\nTransmitted (src= sw%d, dest= sw%d) [RELAY]:  header= (srcIP= %d, destIP= %d)\n", sw.swID, sw.port1, msg.mRelay.srcIP, msg.mRelay.dstIP );
                } else if (n==2) {
                    sendFrame(fdPort2Write, RELAY, &msg);
                    printf("\nTransmitted (src= sw%d, dest= sw%d) [RELAY]:  header= (srcIP= %d, destIP= %d)\n", sw.swID, sw.port2, msg.mRelay.srcIP, msg.mRelay.dstIP );
                }
            } else if (n == -1) {
                // need to query
                queryCounter++;
                MSG msg;
                msg.mQuery.swID = sw.swID;
                msg.mQuery.dstIP = dstIP;
                msg.mQuery.srcIP = srcIP;
                msg.mQuery.port1 = sw.port1;
                msg.mQuery.port2 = sw.port2;
                sendFrame(fdConWrite, QUERY, &msg);
                // print message and lock on to wait the add
                printf("\nTransmitted (src= sw%d, dest= cont)[QUERY]:  header= (srcIP= %d, destIP= %d)\n", sw.swID, srcIP, dstIP);
                queryLocker = 1;
                 
                continue;
            } 
        }
        aimSwith = 0; // reset 

        poll(fifo, 2, 0);

        // read from controller
        if (0) {
            FRAME frame;
            frame = rcvFrame(fifo[0].fd);
	        fifo[0].revents = -1;

            if (frame.kind == ACK) {
                // no happened here
                ackCounter += 1;
		        printf("\nReceived (src= cont, dest= sw%d) [ACK]\n", sw.swID);
            }

            if (frame.kind == ADD) {
                // reserive add  reset querylocker
                queryLocker = 0;
                numFlowTable++;
                addRuleCounter++;

                // set up new flow
                flows[numFlowTable-1].srcIPlo = 0;
                flows[numFlowTable-1].srcIPhi = MAX_IP;
                flows[numFlowTable-1].dstIPlo = frame.msg.mAdd.dstIPlo;
                flows[numFlowTable-1].dstIPhi = frame.msg.mAdd.dstIPhi;
                flows[numFlowTable-1].actionType = frame.msg.mAdd.action;
                flows[numFlowTable-1].actionVal = frame.msg.mAdd.actionVal;
                flows[numFlowTable-1].pri = frame.msg.mAdd.pri;
                flows[numFlowTable-1].pktCount = 0;

                // print message
                printf("\nReceived (src= cont, dest= sw%d) [ADD]:\n", sw.swID);
                printf("    (srcIP= 0-%d, destIP= %d-%d, ", flows[numFlowTable-1].srcIPhi, flows[numFlowTable-1].dstIPlo, flows[numFlowTable-1].dstIPhi);
                if (flows[numFlowTable-1].actionType == FORWARD) {
                    printf("action= FORWARD:%d, pri= %d, pktCount= %d\n", flows[numFlowTable-1].actionVal, flows[numFlowTable-1].pri, flows[numFlowTable-1].pktCount);
                } else {
                    printf("action= DROP:%d, pri= %d, pktCount= %d\n", flows[numFlowTable-1].actionVal, flows[numFlowTable-1].pri, flows[numFlowTable-1].pktCount);
                }
                 

                // resend the message
                int n = switchAction(flows, frame.msg.mAdd.srcIP, frame.msg.mAdd.dstIP, numFlowTable);
                if (n > 0) {
                    // relay message
                    relayOutCounter++;
                    MSG msg;
                    msg.mRelay.srcIP = frame.msg.mAdd.srcIP;
                    msg.mRelay.dstIP = frame.msg.mAdd.dstIP;
                    // print the information
                    if (n==1) {
                        sendFrame(fdPort1Write, RELAY, &msg);
                        printf("\nTransmitted (src= sw%d, dest= sw%d) [RELAY]:  header= (srcIP= %d, destIP= %d)\n", sw.swID, sw.port1, msg.mRelay.srcIP, msg.mRelay.dstIP );
                    } else if (n==2) {
                        sendFrame(fdPort2Write, RELAY, &msg);
                        printf("\nTransmitted (src= sw%d, dest= sw%d) [RELAY]:  header= (srcIP= %d, destIP= %d)\n", sw.swID, sw.port2, msg.mRelay.srcIP, msg.mRelay.dstIP );
                    }
                     
                } else if (n == -1) {
                    // need to query
                    queryCounter++;
                    MSG msg;
                    msg.mQuery.swID = sw.swID;
                    msg.mQuery.dstIP = frame.msg.mAdd.dstIP;
                    msg.mQuery.srcIP = frame.msg.mAdd.srcIP;
                    msg.mQuery.port1 = sw.port1;
                    msg.mQuery.port2 = sw.port2;
                    sendFrame(fdConWrite, QUERY, &msg);
                    // print info and lock for wait
                    printf("\nTransmitted (src= sw%d, dest= cont)[QUERY]:  header= (srcIP= %d, destIP= %d)\n", sw.swID, srcIP, dstIP);
                    queryLocker = 1;
                     
                    continue;
                } 
            }
        }
        // read from port1
        if (sw.port1 != -1) {
            if (fifo[0].revents & POLLIN) {
                fifo[1].revents = -1;
                FRAME frame;
                frame = rcvFrame(fifo[1].fd);
                relayInCounter++;

                printf("\nReceived (src= sw%d, dest= sw%d) [RELAY]:  header= (srcIP= %d, destIP= %d)\n", sw.port1, sw.swID, frame.msg.mRelay.srcIP, frame.msg.mRelay.dstIP);
                 
                int n = switchAction(flows, frame.msg.mRelay.srcIP, frame.msg.mRelay.dstIP, numFlowTable);
                if (n > 0) {
                    // resend to other port
                    relayOutCounter++;
                    if (n==1) {
                        sendFrame(fdPort1Write, RELAY, &msg);
                        printf("\nTransmitted (src= sw%d, dest= sw%d) [RELAY]:  header= (srcIP= %d, destIP= %d)\n", sw.swID, sw.port1, frame.msg.mRelay.srcIP, frame.msg.mRelay.dstIP);
                         
                    } else if (n==2) {
                        sendFrame(fdPort2Write, RELAY, &msg);
                        printf("\nTransmitted (src= sw%d, dest= sw%d) [RELAY]:  header= (srcIP= %d, destIP= %d)\n", sw.swID, sw.port2, frame.msg.mRelay.srcIP, frame.msg.mRelay.dstIP);
                         
                    }
                } else if (n == -1) {
                    // need to query
                    queryCounter++;
                    MSG msg;
                    msg.mQuery.swID = sw.swID;
                    msg.mQuery.dstIP = frame.msg.mRelay.dstIP;
                    msg.mQuery.srcIP = frame.msg.mRelay.srcIP;
                    msg.mQuery.port1 = sw.port1;
                    msg.mQuery.port2 = sw.port2;
                    sendFrame(fdConWrite, QUERY, &msg);
                    // query sended wait for add
                    printf("\nTransmitted (src= sw%d, dest= cont)[QUERY]:  header= (srcIP= %d, destIP= %d)\n", sw.swID, srcIP, dstIP);
                    queryLocker = 1;
                    continue;
                } 
            }
        }
        if (sw.port2 != -1) {
            if (fifo[2].revents &POLLIN) {
                fifo[2].revents = -1;
                FRAME frame;
                frame = rcvFrame(fifo[2].fd);
                relayInCounter++;

                printf("\nReceived (src= sw%d, dest= sw%d) [RELAY]:  header= (srcIP= %d, destIP= %d)\n", sw.port2, sw.swID, frame.msg.mRelay.srcIP, frame.msg.mRelay.dstIP );
                 

                int n = switchAction(flows, frame.msg.mRelay.srcIP, frame.msg.mRelay.dstIP , numFlowTable);
                if (n > 0) {
                    relayOutCounter++;
                    MSG msg;
                    msg.mRelay.srcIP = frame.msg.mRelay.srcIP;
                    msg.mRelay.dstIP = frame.msg.mRelay.dstIP;
                    if (n==1) {
                        sendFrame(fdPort1Write, RELAY, &msg);
                        printf("\nTransmitted (src= sw%d, dest= sw%d) [RELAY]:  header= (srcIP= %d, destIP= %d)\n", sw.swID, sw.port1, msg.mRelay.srcIP, msg.mRelay.dstIP);
                         
                    } else if (n==2) {
                        sendFrame(fdPort2Write, RELAY, &msg);
                        printf("\nTransmitted (src= sw%d, dest= sw%d) [RELAY]:  header= (srcIP= %d, destIP= %d)\n", sw.swID, sw.port2, msg.mRelay.srcIP, msg.mRelay.dstIP);
                         
                    }
                } else if (n == -1) {
                    // need to query
                    queryCounter++;
                    MSG msg;
                    msg.mQuery.swID = sw.swID;
                    msg.mQuery.dstIP = frame.msg.mRelay.srcIP;
                    msg.mQuery.srcIP = frame.msg.mRelay.dstIP;
                    msg.mQuery.port1 = sw.port1;
                    msg.mQuery.port2 = sw.port2;
                    sendFrame(fdConWrite, QUERY, &msg);
                    printf("\nTransmitted (src= sw%d, dest= cont)[QUERY]:  header= (srcIP= %d, destIP= %d)\n", sw.swID, srcIP, dstIP);
                     
                    queryLocker = 1;
                    continue;
                } 
            }
        }
        fflush(stdout);
    }
}

int switchAction(FlowTable flows[],int srcIP,int dstIP,int numFlowTable) {
    // return 0 for drop; return 1 for reply; return -1 for query;
    for (int i=0; i<numFlowTable; i++) {
        if (flows[i].dstIPlo<=dstIP && dstIP<=flows[i].dstIPhi) {
            // destIP fit one flows IP range
            // action if forward and val is 3
            if (flows[i].actionType == (FORWARD) && flows[i].actionVal == 3) {
                flows[i].pktCount++;
                return 0;
            }
            // action if drop
            else if (flows[i].actionType == DROP) {
                flows[i].pktCount++;
                return 0;
            }
            // action is reply
            else if (flows[i].actionType == FORWARD) {
                flows[i].pktCount++;
                return flows[i].actionVal;
            }
        }
    }
    return -1;
}

int printSwitch(FlowTable flows[],int numFlowTable, SwitchCounter swCounter) {
    // function to show information 
    // call by executeswitch when user input list or exit
    // also could call be USER1 signal handler of switch

    printf("Flow table:\n");
    for (int i=0; i<numFlowTable; i++) {
        printf("sw[%d] (srcIP= %d-%d, destIP= %d-%d, )", i, flows[i].srcIPlo, flows[i].srcIPhi, flows[i].dstIPlo, flows[i].dstIPhi);

        // display different info base on the action type
        if (flows[i].actionType == FORWARD) {
            printf("action= FORWARD:%d, pri= %d, pktCount= %d\n", flows[i].actionVal, flows[i].pri, flows[i].pktCount);
        } else {
            printf("action= DROP:%d, pri= %d, pktCount= %d\n", flows[i].actionVal, flows[i].pri, flows[i].pktCount);
        }
    }
    // some times lines under shows very late and without change in when ssh to the lab machine
    printf("Packet Stats:\n    Received:    ADMIT:%d, ACK:%d, ADDRULE:%d, RELAYIN:%d\n", swCounter.admitCounter, swCounter.ackCounter, swCounter.addRuleCounter, swCounter.relayInCounter);
    printf("    Transmitted: OPEN:%d, QUERY:%d, RELAYOUT:%d\n", swCounter.openCounter, swCounter.queryCounter, swCounter.relayOutCounter);
    return 0;
}

int controller(int numSwitch, int portnumber) {
    // set up the counters
    int openCounter = 0;
    int queryCounter = 0;
    int ackCounter = 0;
    int addCounter = 0;
    int fd;
    SwitchInfo switch_list[numSwitch];
    printf("Controller start: have %d switch\n", numSwitch);

    /*Use TCP IP to reserive pack*/
    /*https://blog.csdn.net/liwentao1091/article/details/6661143*/
    struct addrinfo hints, *res;
    bzeros(&hints, sizeof(addrinfo));

    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_NUMERICHOST;

    if (getaddrinfo(NULL, portnumber, &hints, &res) != 0) {
        printf("Fail to tans portnumber.\n");
        return -1;
    }

    /*https://blog.csdn.net/u010889616/article/details/47159937*/
    if ((fd = socket(res->ai_family, res->ai_socktype, res->ar_protocol) == -1) {
        printf("Fail to set TCP socket\n");
        return -1;
    }

    if (bind(fd, res->ai_addr, res->ai_addrlen) == -1) {
        printf("Fail to bind\n");
        return -1;
    }

    if (listen(fd, numSwitch) == -1 ) {
        printf("Fail to listen port");
        return -1;
    }

    /***
    for (int i = 0; i<numSwitch; i++) {
        // set switch id = 0 means this switch have not been created
        switch_list[i].swID = 0;
        int fdread = openFIFO(i+1,0);
        int fdwrite = openFIFO(0,i+1);
        fdRead[i] = fdread;
        fdWrite[i] = fdwrite;
    }
    ***/

    while (1) {

        // update global variable
	    memcpy(&Con.switch_list, &switch_list, sizeof(switch_list));
        Con.numSwitch = numSwitch;
        Con.openCounter = openCounter;
        Con.queryCounter = queryCounter;
        Con.ackCounter = ackCounter;
        Con.addCounter = addCounter;

        // Poll from keyboard
        // http://blog.51cto.com/wait0804/1856818
        struct pollfd keyboard[1];
        keyboard[0].fd = STDIN_FILENO;
        keyboard[0].events = POLLIN;

        // read the input
        poll(keyboard, 1, 0);
        char userCmd[5];
        char userTemp[100];
        if ((keyboard[0].revents & POLLIN)) {
            read(STDIN_FILENO, userCmd, 4);
	        keyboard[0].revents = -1;
            userCmd[4] = '\0';
            if (poll(keyboard, 1, 0)) {
                read(STDIN_FILENO, userTemp, 100);
                keyboard[0].revents = -1;
            }
        }

        // run user cmd - base on input to make output
        // list for list info
        // exit for list and exit
        if (strcmp(userCmd,"list\0")==0) {
            strcpy(userCmd, "ad");
            printController(switch_list, numSwitch, openCounter, queryCounter, ackCounter, addCounter);
        } else if (strcmp(userCmd,"exit\0")==0) {
            printController(switch_list, numSwitch, openCounter, queryCounter, ackCounter, addCounter);
            return 0;
        }

        // poll from fifo
        // https://blog.csdn.net/nk_test/article/details/49283325
        struct pollfd pollSocket, pollSwitch;
        struct sockaddr_in peeraddr;
        socklen_t peerlen = sizeof(peeraddr);
        pollSocket.fd = fd;
        pollSocket.events = POLLIN;

        poll(pollSocket, 1, 0);

        if ((pollSocket.revents & POLLIN)) {
            FRAME frame;
            pollSocket.revents = -1;
            conn = accept(fd, (struct sockaddr*) &peeraddr, &peerlen);
            if (conn == -1) {
                printf("Accept Error\n");
                return -1;
            }
            pollSwitch.fd = conn;
            pollSwitch.events = POLLIN;

            poll(pollSwitch, 1, 5000);
            if ((pollSwitch.revents&POLLIN)) {

                frame = rcvFrame(conn);
                
                if (frame.kind == OPEN) {
                    // open msg
                    openCounter += 1;

                    switch_list[i].swID = frame.msg.mOpen.swID;
                    switch_list[i].port1 = frame.msg.mOpen.port1;
                    switch_list[i].port2 = frame.msg.mOpen.port2;
                    switch_list[i].Iphi = frame.msg.mOpen.Iphi;
                    switch_list[i].IPlo = frame.msg.mOpen.IPlo;

                    printf("\nReceived (src= sw%d, dest= cont) [OPEN]:\n", i+1);
                    printf("	 (port0= cont, port1= %d , port2= %d, port3= %d-%d)\n", switch_list[i].port1, switch_list[i].port2, switch_list[i].IPlo, switch_list[i].Iphi);

                    MSG msg;
                    sendFrame(conn, ACK, &msg);
                    ackCounter += 1;

                    printf("\nTransmitted (src= cont, dest= sw%d)[ACK]\n", i+1);
                }
                else if (frame.kind == QUERY) {
                    // query msg
                    queryCounter += 1;

                    printf("\nReceived (src= sw%d, dest= cont) [QUERY]:  header= (srcIP= %d, destIP= %d)\n", i+1, frame.msg.mQuery.srcIP, frame.msg.mQuery.dstIP);
                    MSG msg = controllerRule(frame.msg.mQuery, switch_list, numSwitch);
                    sendFrame(fdWrite[i], ADD, &msg);
                    
                    addCounter += 1;
                }
            }
        }
        fflush(stdout);
    }
}

MSG controllerRule(MSG_QUERY query, SwitchInfo switch_list[], int numSwitch) {
    // add msg creater, decide what action the switch will do
    MSG msg;
    int find=0;
    // set up the same part of add msg
    msg.mAdd.swID = query.swID;
    msg.mAdd.srcIP = query.srcIP;
    msg.mAdd.dstIP = query.dstIP;
    printf("here is conRule %d %d\n", msg.mAdd.srcIP, msg.mAdd.dstIP);
    msg.mAdd.pri = 4;
    printf("\nTransmitted (src= cont, dest= sw%d) [ADD]:\n	 (srcIP= 0-1000, ", query.swID);
    for (int i=0; i<numSwitch; i++) {
        if (switch_list[i].IPlo <= query.dstIP && query.dstIP <= switch_list[i].Iphi) {
            // find a IP range that fit with the destIP
	        find = 1;
            msg.mAdd.action = FORWARD;
            msg.mAdd.dstIPlo = switch_list[i].IPlo;
            msg.mAdd.dstIPhi = switch_list[i].Iphi;
            if (i > query.swID) {
                msg.mAdd.actionVal = 2;
            } else {
                msg.mAdd.actionVal = 1;
            }
            printf("destIP= %d-%d, action= FORWARD:%d", msg.mAdd.dstIPlo, msg.mAdd.dstIPhi, msg.mAdd.actionVal);
            
	    break;
        }
    }
    if (find == 0) {
        // not find good IP range create a new flow with action drop
        msg.mAdd.action = DROP;
        msg.mAdd.dstIPlo = query.dstIP;
        msg.mAdd.dstIPhi = query.dstIP;
        msg.mAdd.actionVal = 0;
        printf("destIP= %d-%d, action= DROP:%d",msg.mAdd.dstIPlo, msg.mAdd.dstIPhi, msg.mAdd.actionVal);
    }

    printf(", pri= %d, pktCount= 0)\n", msg.mAdd.pri);
    return msg;
}

int printController(SwitchInfo switch_list[],int numSwitch,int openCounter,int queryCounter,int ackCounter,int addCounter) {
    // function to show controller information 
    // call by executeswitch when user input list or exit
    // also could call be USER1 signal handler
    printf("Switch information:\n");
    // show all switches' info by loop
    for (int i=0; i< numSwitch; i++) {
        if (switch_list[i].swID != 0) {
            printf("[sw%d] port1= %d, port2= %d, ", switch_list[i].swID, switch_list[i].port1, switch_list[i].port2);
	    printf("port3=%d-%d\n", switch_list[i].IPlo, switch_list[i].Iphi);
        }
    }
    printf("Packet Stats:\n");
    printf("    Received:   OPEN: %d, QUERY: %d\n", openCounter, queryCounter);
    printf("    Transmitted: ACK: %d, ADD: %d\n", ackCounter, addCounter);

    return 0;
}

int openFIFO(int sender, int reciver) {
    // base function used in both controller and switch
    // to open the FIFO
    char fifoName[10];

    strcpy(fifoName, "fifo-x-y");
    fifoName[5] = sender + '0';
    fifoName[7] = reciver + '0';
    printf("%s\n", fifoName);

    return open(fifoName, O_RDWR);
}

// The code under this comment is all from the lab and class

FRAME rcvFrame (int fd)
{
    int    len; 
    FRAME  frame;

    assert (fd >= 0);
    memset( (char *) &frame, 0, sizeof(frame) );
    len= read (fd, (char *) &frame, sizeof(frame));
    if (len != sizeof(frame))
        WARNING ("Received frame has length= %d (expected= %d)\n",
		  len, sizeof(frame));
    return frame;		  
}

void FATAL (const char *fmt, ... )
{
    va_list  ap;
    fflush (stdout);
    va_start (ap, fmt);  vfprintf (stderr, fmt, ap);  va_end(ap);
    fflush (NULL);
    exit(1);
}

void WARNING (const char *fmt, ... )
{
    va_list  ap;
    fflush (stdout);
    va_start (ap, fmt);  vfprintf (stderr, fmt, ap);  va_end(ap);
}

void sendFrame (int fd, KIND kind, MSG *msg)
{
    FRAME  frame;

    assert (fd >= 0);
    memset( (char *) &frame, 0, sizeof(frame) );
    frame.kind= kind;
    frame.msg=  *msg;
    write (fd, (char *) &frame, sizeof(frame));
}
