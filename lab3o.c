#include <cnet.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

typedef enum    { DL_DATA, DL_ACK, DISCOVER, DISCOVER_ACK}   FRAMEKIND;

#define FRAME_HEADER_SIZE  (sizeof(FRAME) - sizeof(MSG))
#define FRAME_SIZE(f)      (FRAME_HEADER_SIZE + f.len)

void resend_packet();
#define NCS 32

typedef struct {
    char data[MAX_MESSAGE_SIZE];
} MSG;

typedef struct {
    FRAMEKIND    kind;      	// only ever DL_DATA or DL_ACK
    size_t	     len;       	// the length of the msg field only
    int          checksum;  	// checksum of the whole frame
    int          seq;       	// only ever 0 or 1
    MSG          msg;         // name
    int          nodeaddress;
} FRAME;

typedef struct {
  char name[100];
  int address;
  int exists;
} InfoTracker ;
InfoTracker EveryNeighbor[32];

int discovery[32];
FRAME Ninfo[32];
int neighborsRemaining;
int i =0;

int ACKEXP[32];
int SEQSEND[32];

CnetTime lastTime;


FRAME global_frame;
size_t global_size;
int global_link;
int sendLink;

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
    if (neighborsRemaining > 0){
      //size_t length = sizeof(MSG);
      size_t length = sizeof(FRAME);

      MSG msg;
      CnetAddr dest;
      FRAME f;

      CHECK(CNET_read_application(&dest, msg.data, &length));

      f.kind = DISCOVER;
      f.checksum = 0;
      f.len = sizeof(MSG);
      f.nodeaddress = (int) nodeinfo.address;

      length = FRAME_SIZE(f);
      f.checksum  = CNET_ccitt((unsigned char *)&f, (int)length);

      printf("giving to link %d with checksum %d and address %d \n", neighborsRemaining , f.checksum, nodeinfo.address);

      global_frame = f;
      global_link = neighborsRemaining;
      global_size = length;
      CHECK(CNET_write_physical(neighborsRemaining, &f, &length));

      CnetTime timeout;
      timeout = FRAME_SIZE(f)*((CnetTime)8000000 / linkinfo[neighborsRemaining].bandwidth) + linkinfo[neighborsRemaining].propagationdelay;
      lastTime = CNET_start_timer(EV_TIMER1, 3 * timeout, 0);

      //CNET_disable_application(ALLNODES);
      //return;
    }
    else{
      size_t length = sizeof(FRAME);
      MSG message;
      CnetAddr destinationAddr;
      FRAME f;
      CHECK(CNET_read_application(&destinationAddr, message.data, &length));
      int server = 0;
      for (i = 1; i <= nodeinfo.nlinks;i++){
        if (EveryNeighbor[i].address == (int)destinationAddr) server = 1;
      }
      if (server == 0) return;

      f.kind = DL_DATA;
      f.checksum = 0;
      f.len = sizeof(MSG);
      f.nodeaddress = (int) nodeinfo.address;

      length = FRAME_SIZE(f);
      f.checksum  = CNET_ccitt((unsigned char *)&f, (int)length);



      for(int i = 1; i <= nodeinfo.nlinks ; i++){
        printf("destinaltion = %d message = %s size = %d \n", (int) destinationAddr, message.data, (int) length );
        if (EveryNeighbor[i].address == (int)destinationAddr) {
          printf("REACHED HERE sendint to link %d\n", i);
          sendLink = i;
          //CHECK(CNET_disable_application(ALLNODES));
          //return;
          }
      }
    CHECK(CNET_write_physical(sendLink,&f,&length));
    // printf("REACHED ###############\n" );
    // sleep(1);
    // CnetTime timeout;
    // timeout = FRAME_SIZE(f)*((CnetTime)8000000 / linkinfo[neighborsRemaining].bandwidth) + linkinfo[neighborsRemaining].propagationdelay;
    // lastTime = CNET_start_timer(EV_TIMER1, 3 * timeout, 0);
    }


}

void resend_packet(){
  CNET_write_physical(global_link, &global_frame, &global_size);
  CNET_stop_timer(lastTime);
  CnetTime timeout;
  timeout = ((sizeof(FRAME) - sizeof(MSG)) + global_frame.len)*((CnetTime)8000000 / linkinfo[global_link].bandwidth) + linkinfo[global_link].propagationdelay;
  lastTime = CNET_start_timer(EV_TIMER1, 3 * timeout, 0);

}


 static EVENT_HANDLER(physical_ready){
   printf("REACHED PHYSICAL");

    FRAME        f;
    size_t	     len;
    int          link;
    len         = sizeof(FRAME);
    CHECK(CNET_read_physical(&link, &f, &len));

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


      len = FRAME_SIZE(f);
      f.nodeaddress =  (int) nodeinfo.address;
      f.kind = DISCOVER_ACK;
      f.checksum = CNET_ccitt((unsigned char *)&f, (int)len);
      CHECK(CNET_write_physical(link, &f, &len));
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
      f.checksum = CNET_ccitt((unsigned char *)&f, (int)len);
      printf("HERE\n");
      len = FRAME_SIZE(f);
      CHECK(CNET_write_physical(link,&f,&len));
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
    CHECK(CNET_set_handler(EV_APPLICATIONREADY, application_ready, 0));
    CHECK(CNET_set_handler( EV_PHYSICALREADY,    physical_ready, 0));
    CHECK(CNET_set_handler( EV_TIMER1,           timeouts, 0));

    neighborsRemaining = nodeinfo.nlinks;
    CHECK(CNET_set_handler(EV_DEBUG0, display, 0));
    CHECK(CNET_set_debug_string(EV_DEBUG0, "TIMERS info"));
    CHECK(CNET_enable_application(ALLNODES));
    //memset(EveryNeighbor, 0 , sizeof(EveryNeighbor));
    memset(ACKEXP, 0, sizeof(ACKEXP));
    memset(SEQSEND, 0, sizeof(SEQSEND));


}
