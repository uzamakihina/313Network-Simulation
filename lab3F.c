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

static int EXPECT[33];
static int SEQSEND[33];

static EVENT_HANDLER(display){

  CNET_clear();

  for (int i =1; i <= nodeinfo.nlinks; i ++){
    printf("Nodename = %s , nodeaddress = %d \n", EveryNeighbor[i].name , EveryNeighbor[i].address);
    printf("EXPECT = %d, SEQSEND = %d \n", EXPECT[i], SEQSEND[i]);
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

       // CHECK IF the destination is for a server its connected to
       //if not  exit out
       int linker = 0;
       CHECK(CNET_read_application(&dest, f.msg, &length));
       for (int i = 1; i <= nodeinfo.nlinks ; i++){
         if (EveryNeighbor[i].address == dest){
           linker = i;
           break;
         }
       }
       if (linker == 0) return ;

       f.kind = DL_DATA;
       f.checksum = 0;
       f.seq = SEQSEND[linker];
       f.nodeaddress = (int) nodeinfo.address;
       strcpy(f.msg, nodeinfo.nodename);
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
    if (nodeinfo.nodetype == NT_ROUTER){
      int sender;
      if (link == 1) sender = 2;
      else {sender = 1;}

      length = sizeof(FRAME_D);
      CHECK(CNET_write_physical(sender,&f,&length));
      return;
    }


    int compare  = f.checksum;
    f.checksum = 0;
    if (compare != CNET_ccitt((unsigned char *)&f, (int)length)){
      printf("Corrupt packet ignored\n");
       return;}

    if (f.seq == EXPECT[link] && (f.kind == DISCOVER || f.kind == DL_DATA)){
      EXPECT[link] = 1 - EXPECT[link];
    }else if (f.seq == SEQSEND[link] && (f.kind == DISCOVER_ACK || f.kind == DL_ACK)){
      SEQSEND[link] = 1 - SEQSEND[link];
    }else{printf("WRONG PACKET SEQ IGNORE\n");}

    InfoTracker temp;

    switch(f.kind){

      case DISCOVER :
        printf("Received discover request from link %d\n", link);
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
        printf("  GOT AN ACK_DISCOVER FROM LINK %d with name %s\n", link, EveryNeighbor[link].name);
        neighborsRemaining -= 1;
        CNET_enable_application(ALLNODES);
        break;

      case DL_DATA:
        printf("Got DL_DATA request from link %d with name %s\n", link, f.msg);
        memset(f.msg,0,sizeof(f.msg));
        strcpy(f.msg, nodeinfo.nodename);
        f.kind = DL_ACK;
        length = sizeof(FRAME_D);
        f.checksum = CNET_ccitt((unsigned char *)&f, (int)length);
        CHECK(CNET_write_physical(link,&f,&length));
        break;

      case DL_ACK:
        printf("  GOT AN DL_ACK FROM LINK %d named %s\n", link, f.msg);
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


    CHECK(CNET_set_handler( EV_PHYSICALREADY,    physical_ready, 0));

    if (nodeinfo.nodetype != NT_ROUTER){
    CHECK(CNET_set_handler( EV_TIMER1,           timeouts, 0));
    neighborsRemaining = nodeinfo.nlinks;
    CHECK(CNET_set_handler(EV_APPLICATIONREADY, application_ready, 0));
    CHECK(CNET_set_handler(EV_DEBUG0, display, 0));
    CHECK(CNET_set_debug_string(EV_DEBUG0, "TIMERS info"));
    CHECK(CNET_enable_application(ALLNODES));
    for (int i = 1; i <= 32; i++){
      SEQSEND[i] = 0;
      EXPECT[i] = 0;
    }

    }


}
