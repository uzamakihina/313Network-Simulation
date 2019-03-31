#include <cnet.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

typedef enum    { DL_DATA, DL_ACK, DISCOVER, DISCOVER_ACK}   FRAMEKIND;

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
InfoTracker EveryNeighbor[32];

// static int discovery[32];
// static FRAME_D Ninfo[32];
static int neighborsRemaining;
static int i =0;

static int ACKEXP[32];
static int SEQSEND[32];

static CnetTime lastTime;



static FRAME_D global_frame;
static size_t global_size;
static int global_link;
static int sendLink;



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
    //char	buffer[MAX_MESSAGE_SIZE];
    //size_t	length;
    CHECK(CNET_disable_application(ALLNODES));
    FRAME_D f;

    if (neighborsRemaining > 0){
      //size_t length = sizeof(MSG_D);
      size_t length = MAX_MESSAGE_SIZE;
      CnetAddr dest;


      CHECK(CNET_read_application(&dest, f.data, &length));
      f.kind = DISCOVER;
      f.checksum = 0;
      f.seq = 0;
      f.nodeaddress = (int) nodeinfo.address;

      length = sizeof(FRAME_D);
      f.checksum  = CNET_ccitt((unsigned char *)&f, (int)length);

      printf("giving to link %d with checksum %d and address %d \n", neighborsRemaining , f.checksum, nodeinfo.address);

      global_frame = f;
      global_link = neighborsRemaining;
      global_size = length;
      CHECK(CNET_write_physical(neighborsRemaining, (char *) &f, &length));
      printf("SIZE OF FRAME IS %d\n", FRAME_SIZE(f));

      CnetTime timeout;
      timeout = FRAME_SIZE(f)*((CnetTime)8000000 / linkinfo[neighborsRemaining].bandwidth) + linkinfo[neighborsRemaining].propagationdelay;
      lastTime = CNET_start_timer(EV_TIMER1, 3 * timeout, 0);

    }
    else{

      size_t length = sizeof(MSG_D);
      CnetAddr destinationAddr;
      MSG_D garbage;
      CHECK(CNET_read_application(&destinationAddr, garbage.data, &length));

      int server = 0;
      for (i = 1; i <= nodeinfo.nlinks;i++){
        if (EveryNeighbor[i].address == (int)destinationAddr) server = 1;
      }
      if (server == 0) return;


      f.kind = DL_DATA;
      f.checksum = 0;
      f.seq = 0;
      f.len = sizeof(f.msg);
      f.nodeaddress =  (int) nodeinfo.address;

      for(int i = 1; i <= nodeinfo.nlinks ; i++){
        printf("destinaltion = %d message = %s size = %d \n", (int) destinationAddr, f.msg.data, (int) length );
        if (EveryNeighbor[i].address == (int)destinationAddr) {

          sendLink = i;
          printf("REACHED HERE sendint to link %d\n", sendLink);
          break;
          //CHECK(CNET_disable_application(ALLNODES));
          //return;
          }
      }
      length = FRAME_SIZE(f);
      f.checksum  = CNET_ccitt((unsigned char *)&f, (int)length);
      CHECK(CNET_write_physical(sendLink, (char *)&f,&length));

    }


}

void resend_packet(){
  printf("REsending\n");
  CNET_write_physical(global_link, (char *) &global_frame, &global_size);
  CNET_stop_timer(lastTime);
  CnetTime timeout;
  timeout = ((sizeof(FRAME_D) - sizeof(MSG_D)) + global_frame.len)*((CnetTime)8000000 / linkinfo[global_link].bandwidth) + linkinfo[global_link].propagationdelay;
  lastTime = CNET_start_timer(EV_TIMER1, 3 * timeout, 0);

}


 static EVENT_HANDLER(physical_ready){

    FRAME_D        f;
    size_t       len = sizeof(FRAME_D);
    int          link;
    CHECK(CNET_read_physical(&link, &f, &len));

    printf("TOOK FROM PHYSICAL WITH ADDRESS %d\n", f.nodeaddress);

    //printf("address received is %d\n ", f.nodeaddress);

    int checkActual = f.checksum; //CNET_ccitt((unsigned char *)&f, (int)len);
    f.checksum = 0;
    if (checkActual != CNET_ccitt((unsigned char *)&f, (int)len)  ) {
      printf("Checkfailed\n");
      return;
    }


    if (f.kind == DISCOVER){

      printf("   %d wants to discover me \n ", f.nodeaddress);
      printf("  Checksum succeed\n");
      strcpy(f.msg.data, nodeinfo.nodename);
      f.len = sizeof(f.msg);
      f.nodeaddress =  (int) nodeinfo.address;
      f.kind = DISCOVER_ACK;
      len = FRAME_SIZE(f);
      f.checksum = CNET_ccitt((unsigned char *)&f, (int)len);
      CHECK(CNET_write_physical(link, (char * )&f, &len));
      return;
    }

    if (f.kind == DISCOVER_ACK){
        CNET_stop_timer(lastTime);
        InfoTracker Information;
        strcpy(Information.name , f.msg.data);
        Information.address = f.nodeaddress;
        if (EveryNeighbor[link].exists == 0){
          Information.exists = 1;
          EveryNeighbor[link] = Information;
          neighborsRemaining -= 1;
          printf("ACKED %d with name %s\n", f.nodeaddress, f.msg.data);
          CNET_enable_application(ALLNODES);
          if(neighborsRemaining == 0) printf("All neighbors discovered\n");
        }



        return;
    }

    if(f.kind == DL_DATA){

      printf("SENDING DL DATA TO link %s\n", link);

      f.kind = DL_ACK;
      len = FRAME_SIZE(f);
      f.checksum = CNET_ccitt((unsigned char *)&f, (int)len);
      CHECK(CNET_write_physical(link,(char *) &f,&len));
      return;
    }

    if(f.kind == DL_ACK){
      printf("MY packet has been acked\n");
      CNET_enable_application(ALLNODES);
      return;
    }

}

EVENT_HANDLER(timeouts)
{
  printf("TIMEOUT \n");
  resend_packet();
}

EVENT_HANDLER(reboot_node)
{

    if (nodeinfo.nodenumber > 32){
      printf("There are more than 32 nodes\n");
      exit(1);
    }
    static char buf5[50000];
    setvbuf ( stdout , buf5 , _IOFBF , sizeof(buf5) );
    CHECK(CNET_set_handler(EV_APPLICATIONREADY, application_ready, 0));
    CHECK(CNET_set_handler( EV_PHYSICALREADY,    physical_ready, 0));
    CHECK(CNET_set_handler( EV_TIMER1,           timeouts, 0));

    neighborsRemaining = nodeinfo.nlinks;
    CHECK(CNET_set_handler(EV_DEBUG0, display, 0));
    CHECK(CNET_set_debug_string(EV_DEBUG0, "TIMERS info"));
    CHECK(CNET_enable_application(ALLNODES));



}
