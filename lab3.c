#include <cnet.h>
#include <stdlib.h>
#include <string.h>
#include <string.h>
#define NCS 32

typedef enum    { DL_DATA, DL_ACK, DL_DISCOVERY, DISCOVERY_ACK }   FRAMEKIND;

typedef struct {
    char        data[MAX_MESSAGE_SIZE];
    int         address;
} MSG;

typedef struct {
    FRAMEKIND    kind;
    size_t	 len;       	// the length of the msg field only
    int          checksum;  	// checksum of the whole frame
    int          seq;       	// only ever 0 or 1
    MSG          msg;
} FRAME;

#define FRAME_HEADER_SIZE  (sizeof(FRAME) - sizeof(MSG))
#define FRAME_SIZE(f)      (FRAME_HEADER_SIZE + f.len)


static  MSG       	*lastmsg;
static  size_t		lastlength		= 0;
static  CnetTimerID	lasttimer		= NULLTIMER;

static  int       	ackexpected		= 0;
static	int		nextframetosend		= 0;
static	int		frameexpected		= 0;
static  int   neightbor_undiscovered;

typedef struct {
  char data[100];
  int address;
} InfoTracker;


static InfoTracker Neightborinfo[32];


static void transmit_frame(MSG *msg, FRAMEKIND kind, size_t length, int seqno, int destinationlink)
{

    printf("destination is %d\n", destinationlink);

    FRAME       f;
    int		link = 1;

    f.kind      = kind;
    f.seq       = seqno;
    f.checksum  = 0;
    f.len       = length;

    switch (kind) {
    case DL_ACK :
        printf("ACK transmitted, seq=%d\n", seqno);
	      break;

    case DL_DATA: {
	      CnetTime	timeout;

        printf(" DATA transmitted, seq=%d\n", seqno);
        //memcpy(&f.msg, msg, (int)length);

	      timeout = FRAME_SIZE(f)*((CnetTime)8000000 / linkinfo[link].bandwidth) +
				linkinfo[link].propagationdelay;

        lasttimer = CNET_start_timer(EV_TIMER1, 3 * timeout, 0);
	      break;
        }

    case DL_DISCOVERY:
       printf("Transmit discovery, seq=%d\n", seqno);
       break;

   case DISCOVERY_ACK:
        printf("discoverey ACK sending\n");


    }

    length      = FRAME_SIZE(f);
    f.checksum  = CNET_ccitt((unsigned char *)&f, (int)length);
    CHECK(CNET_write_physical(link, &f, &length));
}

static EVENT_HANDLER(application_ready)
{
    CNET_disable_application(ALLNODES);

    if (neightbor_undiscovered > 0 ){
      size_t x = sizeof(MSG);
      transmit_frame(NULL, DL_DISCOVERY, x, nextframetosend,neightbor_undiscovered);
      nextframetosend = 1-nextframetosend;
      return;
    }


    CnetAddr destaddr;
    lastlength  = sizeof(MSG);
    CHECK(CNET_read_application(&destaddr, lastmsg, &lastlength));


    int destinationlink = 1;

    printf("down from application, seq=%d\n", nextframetosend);
    transmit_frame(lastmsg, DL_DATA, lastlength, nextframetosend,destinationlink);
    nextframetosend = 1-nextframetosend;






}

static EVENT_HANDLER(physical_ready)
{
    FRAME        f;
    size_t	 len;
    int          link, checksum;

    len         = sizeof(FRAME);
    CHECK(CNET_read_physical(&link, &f, &len));


    if (nodeinfo.nodetype == NT_ROUTER){
      int sendlink;
      if (link == 1) sendlink = 2;
      else{sendlink = 1;}
      CHECK(CNET_write_physical(sendlink, &f, &len));
      return;
    }


    checksum    = f.checksum;
    f.checksum  = 0;
    if(CNET_ccitt((unsigned char *)&f, (int)len) != checksum) {
        printf("\t\t\t\tBAD checksum - frame ignored\n");
        return;           // bad checksum, ignore frame
    }
    InfoTracker information;
    switch (f.kind) {

    case DISCOVERY_ACK:


      information.address = f.msg.address;
      strcpy(information.data, f.msg.data);
      Neightborinfo[link] = information;
      // strcpy(temperary.data, f.msg.data);
      // temperary.address = f.msg.address;
      // Neightborinfo[link] = temperary;
      printf("DISCOVERYACK\n");
      break;

    case DL_DISCOVERY:
      printf("\t\t\t\tDATA received, seq=%d, ", f.seq);
      if(f.seq == frameexpected) {
          printf("got discovery\n");
          //len = f.len;
          //CHECK(CNET_write_application(&f.msg, &len));
          f.msg.address = nodeinfo.address;
          strcpy(f.msg.data, nodeinfo.nodename);
          frameexpected = 1-frameexpected;
          size_t x = sizeof(MSG);
          transmit_frame(NULL, DISCOVERY_ACK, x, f.seq, 1);
      }
      else
          printf("ignored\n");
      transmit_frame(NULL, DL_ACK, 0, f.seq, 1);

      break;

    case DL_ACK :
        if(f.seq == ackexpected) {
            printf("\t\t\t\tACK received, seq=%d\n", f.seq);
            CNET_stop_timer(lasttimer);
            ackexpected = 1-ackexpected;
            //CNET_enable_application(ALLNODES);
        }
	      break;

    case DL_DATA :
        printf("\t\t\t\tDATA received, seq=%d, ", f.seq);
        if(f.seq == frameexpected) {
            printf("up to application\n");
            len = f.len;
            CHECK(CNET_write_application(&f.msg, &len));
            frameexpected = 1-frameexpected;
        }
        else
            printf("ignored\n");
        transmit_frame(NULL, DL_ACK, 0, f.seq, 1);
	      break;
    }
}

static EVENT_HANDLER(timeouts)
{
    printf("timeout, seq=%d\n", ackexpected);
    transmit_frame(lastmsg, DL_DATA, lastlength, ackexpected,1);
}

static EVENT_HANDLER(showstate)
{
    printf(
    "\n\tackexpected\t= %d\n\tnextframetosend\t= %d\n\tframeexpected\t= %d\n",
		    ackexpected, nextframetosend, frameexpected);

    for (int i =1; i <= nodeinfo.nlinks; i++){
      printf("Neightbore with address %d , and name %s", Neightborinfo[i].address, Neightborinfo[i].data);
    }
}

EVENT_HANDLER(reboot_node)
{
    if(nodeinfo.nodenumber > NCS) {
	fprintf(stderr,"This is a > 32-node network!\n");
	exit(1);
    }

    lastmsg	= calloc(1, sizeof(MSG));


    CHECK(CNET_set_handler( EV_PHYSICALREADY,    physical_ready, 0));
    CHECK(CNET_set_handler( EV_TIMER1,           timeouts, 0));
    CHECK(CNET_set_handler( EV_DEBUG0,           showstate, 0));

    CHECK(CNET_set_debug_string( EV_DEBUG0, "State"));

    if(nodeinfo.nodetype != NT_ROUTER){
	  CNET_enable_application(ALLNODES);
    CHECK(CNET_set_handler( EV_APPLICATIONREADY, application_ready, 0));
    neightbor_undiscovered = nodeinfo.nlinks;
  }
}
