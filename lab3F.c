#include <cnet.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

typedef enum    {  DISCOVER, DISCOVER_ACK, DL_DATA,DL_ACK}   FRAMEKIND;

// #define FRAME_HEADER_SIZE  (sizeof(FRAME_D) - sizeof(MSG_D))
// #define FRAME_SIZE(f)      (FRAME_HEADER_SIZE + f.len)

void resend_packet();
#define NCS 32

// typedef struct {
//     char data[MAX_MESSAGE_SIZE];
// } MSG_D;

typedef struct {
    FRAMEKIND    kind;      	// only ever DL_DATA or DL_ACK
    int          checksum;  	// checksum of the whole FRAME_D
    int          seq;       	// only ever 0 or 1        // name
    int          nodeaddress;
    char          msg[MAX_MESSAGE_SIZE];
} FRAME_D;

typedef struct {
  char name[100];
  int address;
  int exists;
} InfoTracker ;
static InfoTracker EveryNeighbor[32];

static int neighborsRemaining;

static int ACKEXP[33];
static int SEQSEND[33];

//static CnetTime lastTime;



// static FRAME_D global_frame;
// static size_t global_size;
// static int global_link;
// static int sendLink;



static EVENT_HANDLER(display){
  // for neighboring nodes - needs address, node name,
  // for current node - simulation time, count_too_busy, informtion of packet being timed out.
  CNET_clear();

  for (int i =1; i <= nodeinfo.nlinks; i ++){
    printf("Nodename = %s , nodeaddress = %d \n", EveryNeighbor[i].name , EveryNeighbor[i].address);
    printf("ACKEXP = %d, SEQSEND = %d \n", ACKEXP[i], SEQSEND[i]);
  }
  printf("Simulation time = %d \n", nodeinfo.time_in_usec);
}

static EVENT_HANDLER(application_ready)
{

    CHECK(CNET_disable_application(ALLNODES));
    FRAME_D f;

    if (neighborsRemaining > 0){
      size_t length = MAX_MESSAGE_SIZE;
      CnetAddr dest;


      CHECK(CNET_read_application(&dest, f.msg, &length));
      f.kind = DISCOVER;
      f.checksum = 0;
      f.seq = SEQSEND[neighborsRemaining];
      f.nodeaddress = (int) nodeinfo.address;

      length = sizeof(FRAME_D);
      f.checksum  = CNET_ccitt((unsigned char *)&f, (int)length);
      CHECK(CNET_write_physical(neighborsRemaining,&f,&length));
     }
     else{
       size_t length = MAX_MESSAGE_SIZE;
       CnetAddr dest;
       int linker = 0;
       CHECK(CNET_read_application(&dest, f.msg, &length));
       //printf("Entering if statement nlinks = %d\n", nodeinfo.nlinks);
       for (int i = 1; i <= nodeinfo.nlinks ; i++){
         if (EveryNeighbor[i].address == dest){
           //printf("FOUND LINK link = %d, dest = %d\n", i, dest);
           linker = i;
           break;
         }
       }


       f.kind = DL_DATA;
       f.checksum = 0;
       f.seq = 0;
       f.nodeaddress = (int) nodeinfo.address;

       length = sizeof(FRAME_D);
       f.checksum  = CNET_ccitt((unsigned char *)&f, (int)length);
       CHECK(CNET_write_physical(linker,&f,&length));



     }


}




 static EVENT_HANDLER(physical_ready){

    FRAME_D f;
    size_t length;
    int link;
    CNET_read_physical(&link, &f, &length);
    int compare  = f.checksum;
    f.checksum = 0;
    if (compare != CNET_ccitt((unsigned char *)&f, (int)length)) return;

    if (f.seq == ACKEXP[link]){
      ACKEXP[link] = 1 - ACKEXP[link];
    }else{
      printf("Wrong sequence number ignoring packet\n");
    }

    InfoTracker temp;

    switch(f.kind){
      case DISCOVER :
        printf("seq = %d in discover\n", f.seq);
        f.nodeaddress = nodeinfo.address;
        strcpy(f.msg, nodeinfo.nodename);
        f.kind = DISCOVER_ACK;
        length = sizeof(FRAME_D);
        f.checksum  = CNET_ccitt((unsigned char *)&f, (int)length);
        CHECK(CNET_write_physical(link,&f,&length));
        break;

      case DISCOVER_ACK:
        strcpy(temp.name, f.msg);
        temp.address = f.nodeaddress;
        EveryNeighbor[link] = temp;
        printf("GOT AN ACK FROM NODEINFO REQUEST neightbor %d has name %s\n", EveryNeighbor[link].address, EveryNeighbor[link].name);
        neighborsRemaining -= 1;
        CNET_enable_application(ALLNODES);
        break;

      case DL_DATA:
        printf(" Got data request \n");
        f.kind = DL_ACK;
        length = sizeof(FRAME_D);
        f.checksum = CNET_ccitt((unsigned char *)&f, (int)length);
        CHECK(CNET_write_physical(link,&f,&length));


        break;
      case DL_ACK:
        printf("GOT AN ACK FROM DATA REQUEST\n");
        CNET_enable_application(ALLNODES);
        break;
    }

}

static EVENT_HANDLER(timeouts){}


EVENT_HANDLER(reboot_node)
{

    if (nodeinfo.nodenumber > 32){
      printf("There are more than 32 nodes\n");
      exit(1);
    }

    CHECK(CNET_set_handler(EV_APPLICATIONREADY, application_ready, 0));
    CHECK(CNET_set_handler( EV_PHYSICALREADY,    physical_ready, 0));
    CHECK(CNET_set_handler( EV_TIMER1,           timeouts, 0));


    neighborsRemaining = nodeinfo.nlinks;
  //  memset(EveryNeighbor,0,sizeof(EveryNeighbor));
    CHECK(CNET_set_handler(EV_DEBUG0, display, 0));
    CHECK(CNET_set_debug_string(EV_DEBUG0, "TIMERS info"));
    CHECK(CNET_enable_application(ALLNODES));
    for (int i = 1; i <= 32; i++){
      SEQSEND[i] = 0;
    }


}
