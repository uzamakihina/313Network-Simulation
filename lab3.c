#include <cnet.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

typedef enum    { DL_DATA, DL_ACK, DISCOVER, DISCOVER_ACK}   FRAMEKIND;

#define FRAME_HEADER_SIZE  (sizeof(FRAME) - sizeof(MSG))
#define FRAME_SIZE(f)      (FRAME_HEADER_SIZE + f.len)
#define NCS 32

typedef struct {
    char data[MAX_MESSAGE_SIZE];
} MSG;

typedef struct {
    FRAMEKIND    kind;      	// only ever DL_DATA or DL_ACK
    size_t	 len;       	// the length of the msg field only
    int          checksum;  	// checksum of the whole frame
    int          seq;       	// only ever 0 or 1
    MSG          msg;         // name
    int          nodeaddress;
} FRAME;

typedef struct {
  char name[100];
  int address;
} InfoTracker ;
InfoTracker EveryNeighbor[32];

int discovery[32];
FRAME Ninfo[32];
int neighborsRemaining;
int i =0;

// static write_to_link(int link, FRAME * frame,size_t * length ){
//
// }

static EVENT_HANDLER(display){
  // for neighboring nodes - needs address, node name,
  // for current node - simulation time, count_too_busy, informtion of packet being timed out.
  CNET_clear();

  for (int i =1; i <= nodeinfo.nlinks; i ++){
    printf("Nodename = %s , nodeaddress = %d \n", EveryNeighbor[i].name , EveryNeighbor[i].address);
  }
  printf("Simulation time = %d \n", nodeinfo.time_in_usec);
}

static EVENT_HANDLER(application_ready)
{
    char	buffer[MAX_MESSAGE_SIZE];
    size_t	length;
    length = sizeof(buffer);

    if (neighborsRemaining > 0){
      size_t length = sizeof(MSG);
      FRAME f;
      f.kind = DISCOVER;
      f.checksum = 0;
      f.len = length;
      f.nodeaddress = (int) nodeinfo.address;
      length = FRAME_SIZE(f);

      f.checksum  = CNET_ccitt((unsigned char *)&f, (int)length);

      printf("giving to link %d with checksum %d \n", neighborsRemaining , f.checksum);


      CNET_write_physical(neighborsRemaining, &f, &length);
      CNET_disable_application(ALLNODES);
    }
}


static EVENT_HANDLER(physical_ready){

    FRAME        f;
    size_t	     len;
    int          link;

    len         = sizeof(FRAME);

    CHECK(CNET_read_physical(&link, &f, &len));


    if (f.kind == DISCOVER){
      printf("   %d wants to discover me \n ", f.nodeaddress);
      int checkActual = f.checksum; //CNET_ccitt((unsigned char *)&f, (int)len);
      f.checksum = 0;

      if (checkActual == CNET_ccitt((unsigned char *)&f, (int)len)){
        printf("  Checksum succeed\n");
        strcpy(f.msg.data, nodeinfo.nodename);
        f.nodeaddress =  (int) nodeinfo.address;
        f.kind = DISCOVER_ACK;
        //f.checksum = CNET_ccitt((unsigned char *)&f, (int)len);


        CNET_write_physical(link, &f, &len);
        return;
      }
      else{
        printf("FAILED CHECKSUM\n");
        printf(" linkRead = %s checkRead = %d checkActual = %d \n", f.msg.data ,f.checksum, checkActual);
        return;
      }
    }

    if (f.kind == DISCOVER_ACK){

        printf("ACKED %d with name %s\n", f.nodeaddress, f.msg.data);
        CNET_enable_application(ALLNODES);
        InfoTracker Information;
        strcpy(Information.name , f.msg.data);
        Information.address = f.nodeaddress;
        EveryNeighbor[neighborsRemaining] = Information;
        neighborsRemaining -= 1;

        return;
    }


}

EVENT_HANDLER(reboot_node)
{

    if (nodeinfo.nodenumber > 32){
      printf("There are more than 32 nodes\n");
      exit(1);
    }
    CNET_set_handler(EV_APPLICATIONREADY, application_ready, 0);
    CHECK(CNET_set_handler( EV_PHYSICALREADY,    physical_ready, 0));
    // CNET_set_handler(EV_DEBUG0, button_pressed, 0);
    // CNET_set_debug_string(EV_DEBUG0, "Node Info");
    neighborsRemaining = nodeinfo.nlinks;
    CNET_set_handler(EV_DEBUG0, display, 0);
    CNET_set_debug_string(EV_DEBUG0, "TIMERS info");
    CNET_enable_application(ALLNODES);


}
