#ifndef DSPG_L2SWITCH_DEFS_H
#define DSPG_L2SWITCH_DEFS_H

#define DISABLE 0
#define ENABLE  1

typedef unsigned long  uint32;
typedef unsigned short uint16;

// STATUS: 0 if switch configured as expected. 
//        -1 for error.
typedef int            STATUS; 

typedef unsigned char  L2SW_MAC[6];

#define L2SW_PORTMASK_NONE			0x0000
#define L2SW_PORTMASK_PC			0x0001
#define L2SW_PORTMASK_LAN			0x0002
#define L2SW_PORTMASK_PC_AND_LAN	0x0003
#define L2SW_PORTMASK_CPU			0x0400
#define L2SW_PORTMASK_ALL_PORTS		0x0403

//---------------------------------- CTTC ------------------------------------//
// Provides a method to enforce compile-time type checking (CTTC) for unique
// PHY's, unique ports, multiple PHY's, and multiple ports.

// PRIVATE DEFINITIONS

// Assigning distinct values for these enums is not necessary, but it provides
// for defensive programming, so we can also add run-time validations if needed
// (but this won't be needed if the application uses the macros PHY_LAN, PHY_PC,
// PORT_LAN, PORT_PC, PORT_CPU, etc. which is the whole intent of this CTTC.

#define E_PHY_START   0x100
#define E_PORT_START  E_PHY_START  << 1

typedef enum { E_PHY_LAN  = E_PHY_START , E_PHY_PC              } e_Phy;
typedef enum { E_PORT_LAN = E_PORT_START, E_PORT_PC, E_PORT_CPU } e_Port;

// Only one choice allowed, so embed enum in struct to force compile-time type 
// checking. Having distinct structs for Phy and Port also enforces compile-time
// checking for  which functions accept CPU port and which ones do not.
typedef struct s_Phy  {e_Phy  p;} Phy ; 
typedef struct s_Port {e_Port p;} Port; 

// More than one choice allowed, so no need for enum. In fact, no need for masks 
// either. Having distinct structs for Phys and Ports also enforces compile-time
// checking for which functions accept CPU port and which ones do not.
typedef struct s_Phys  {int lan; int pc;         } Phys ; 
typedef struct s_Ports {int lan; int pc; int cpu;} Ports;

// PUBLIC DEFINITIONS
// macros to be used by the application, instead of enums
#define PHY_LAN  ( (Phy ){E_PHY_LAN } )
#define PHY_PC   ( (Phy ){E_PHY_PC  } )

#define PORT_LAN ( (Port){E_PORT_LAN} )
#define PORT_PC  ( (Port){E_PORT_PC } )
#define PORT_CPU ( (Port){E_PORT_CPU} )

#define PHYS_NONE         ( (Phys  ){.lan=0, .pc=0        } )   // 0
#define PHYS_LAN          ( (Phys  ){.lan=1, .pc=0        } )   // 1
#define PHYS_PC           ( (Phys  ){.lan=0, .pc=1        } )   // 2
#define PHYS_LAN_AND_PC   ( (Phys  ){.lan=1, .pc=1        } )   // 3

#define PORTS_NONE        ( (Ports ){.lan=0, .pc=0, .cpu=0} )   // 0
#define PORTS_CPU         ( (Ports ){.lan=0, .pc=0, .cpu=1} )   // 1
#define PORTS_PC          ( (Ports ){.lan=0, .pc=1, .cpu=0} )   // 2
#define PORTS_CPU_AND_PC  ( (Ports ){.lan=0, .pc=1, .cpu=1} )   // 3
#define PORTS_LAN         ( (Ports ){.lan=1, .pc=0, .cpu=0} )   // 4
#define PORTS_LAN_AND_CPU ( (Ports ){.lan=1, .pc=0, .cpu=1} )   // 5
#define PORTS_LAN_AND_PC  ( (Ports ){.lan=1, .pc=1, .cpu=0} )   // 6
#define PORTS_ALL         ( (Ports ){.lan=1, .pc=1, .cpu=1} )   // 7

#define IS_PHY_LAN(phy)     (phy.p == E_PHY_LAN)
#define IS_PHY_PC(phy)      (phy.p == E_PHY_PC)

#define IS_PORT_LAN(port)   (port.p == E_PORT_LAN)
#define IS_PORT_PC(port)    (port.p == E_PORT_PC)
#define IS_PORT_CPU(port)   (port.p == E_PORT_CPU)

#define IS_PHYS_LAN(phys)   (phys.lan  == 1)
#define IS_PHYS_PC(phys)    (phys.pc   == 1)

#define IS_PORTS_LAN(ports) (ports.lan == 1)
#define IS_PORTS_PC(ports)  (ports.pc  == 1)
#define IS_PORTS_CPU(ports) (ports.cpu == 1)

//++++++++++++++++++++++++++++++++++ CTTC ++++++++++++++++++++++++++++++++++++//

#define L2SW_PACKETSMASK_ALL_BPDU		0x00FF
#define L2SW_PACKETSMASK_LLDP			0x0001
#define L2SW_PACKETSMASK_STP			0x0002
#define L2SW_PACKETSMASK_DOT1X			0x0004
#define L2SW_PACKETSMASK_DOT1X_UNICAST		0x0008
#define L2SW_PACKETSMASK_DOT1X_MULTICAST	0x0010
#define L2SW_PACKETSMASK_ALL_PACKETS		0xffff

#define L2SW_PACKETSMASK_TAGGED         0x0100
#define L2SW_PACKETSMASK_UNTAGGED       0x0200

#define L2SW_PACKETSMASK_BROADCAST      0x1000
#define L2SW_PACKETSMASK_MULTICAST      0x2000
#define L2SW_PACKETSMASK_UNICAST        0x4000
#define L2SW_PACKETSMASK_UNKNOWN        0x8000

typedef enum {
    L2SW_AUTO_MDIX_DISABLED = 0,
    L2SW_AUTO_MDIX_ENABLED
} L2SW_AUTO_MDIX_MODE;

typedef enum {
    L2SW_VLAN_DOT1Q_ENABLED = 0,
    L2SW_VLAN_TABLE_ENABLED
} L2SW_VLAN_MODE;

typedef enum{
    L2SW_VLAN_STATUS_DISABLED=0,
    L2SW_VLAN_STATUS_ENABLED
} L2SW_VLAN_STATUS;


typedef enum{  
     L2SW_FWD_TO_CPU_PC=0,
     L2SW_FWD_CPU_ONLY
} L2SW_UNKNOWN_MCAST_FWD_DEST;
// // L2SW_FWD_CPU_PC: all Unknown packets ingress switch PORT_LAN will be forward to PORT_CPU and PORT_PC (default state).
// // L2SW_FWD_CPU_ONLY: all Unknown packets ingress switch PORT_LAN will be forward to PORT_CPU.

typedef enum {
    L2SW_STATIC_ENTRY = 0,
    L2SW_DYNAMIC_ENTRY,
    L2SW_ALL_ENTRIES
} L2SW_LOOKUPTABLE_ENTRY;

typedef enum {
    L2SW_FLOWCONTROL_DISABLE = 0, 
    L2SW_FLOWCONTROL_ENABLE
} L2SW_FLOWCONTROL_MODE;

typedef enum{  
    L2SW_RULE_OFF =0, 
    L2SW_RULE_ON
} L2SW_RULE_MODE;

typedef enum{  
    L2SW_PACKET_MODIFY = 0, //should follow switch configuration
    L2SW_PACKET_NO_MODIFY
} L2SW_PACKET_MODIFY_MODE;

typedef enum {
    L2SW_PORTMIRROR_DISABLE = 0,
    L2SW_PORTMIRROR_ENABLE
} L2SW_PORTMIRROR_STATUS;

typedef enum {
    L2SW_LINK_INVALID = 0,  
    L2SW_LINK_UP,
    L2SW_LINK_DOWN,
    L2SW_LINK_ENABLE,
    L2SW_LINK_DISABLED    /* Link is disabled */
} L2SW_LINK_STATUS;

typedef enum {
    L2SW_LINK_MODE_DISABLED = 0,
    L2SW_LINK_MODE_AUTO,      /* Auto Negotiate speed & duplex */
    L2SW_LINK_MODE_10_HALF,   /*   10 Mbps half duplex */
    L2SW_LINK_MODE_10_FULL,   /*   10 Mbps full duplex */
    L2SW_LINK_MODE_100_HALF,  /*  100 Mbps half duplex */
    L2SW_LINK_MODE_100_FULL,  /*  100 Mbps full duplex */
    L2SW_LINK_MODE_1000_FULL, /* 1000 Mbps full duplex */
    L2SW_LINK_MODE_INVALID
} L2SW_LINK_MODE;

typedef enum {
    L2SW_IVL_MODE = 0,
    L2SW_SVL_MODE,
    IVL_AND_SVL_MODE, // used only for GET not SET!
    L2SW_LEARNING_MODE_NONE
} L2SW_VLAN_LEARNING_MODE;

typedef enum {
	L2SW_NOT_SUPPORTED = 0,
	L2SW_SUPPORTED
}L2SW_CAPABILITIES;

typedef enum {
	L2SW_LED_MODE_DISABLE = 0,
	L2SW_LED_MODE_OFF,
	L2SW_LED_MODE_ON,
	L2SW_LED_MODE_ENABLE
}L2SW_LED_MODE;

typedef enum {
	L2SW_TEST_MODE_NORMAL = 0,
	L2SW_TEST_MODE_1,
	L2SW_TEST_MODE_2,
	L2SW_TEST_MODE_3,
	L2SW_TEST_MODE_4
}L2SW_TEST_MODE;

typedef enum{
	L2SW_STP_DISABLED = 0,
	L2SW_STP_BLOCKING,
	L2SW_STP_LEARNING,
	L2SW_STP_FORWARDING
} L2SW_STP_STATE;

typedef enum
{
    DOS_MODE_SIP_EQ_DIP,
    DOS_MODE_TCP_PORTS_EQ,
    DOS_MODE_UDP_PORTS_EQ,
    DOS_MODE_TCP_FLAGS,
    DOS_MODE_TCP_FLAGS_FUP,
    DOS_MODE_TCP_FLAGS_SF
} L2SW_DOS_MODE_TYPE;

typedef enum
{
    DOS_MODE_DISABLED,
    DOS_MODE_ENABLED
} L2SW_DOS_MODE_STATUS;

typedef enum 
{
    L2SW_EEE_DISABLED = 0,
    L2SW_EEE_ENABLED
} L2SW_EEE_MODE;


typedef enum 
{
     L2SW_GREEN_ETHERNET_DISABLED = 0,
     L2SW_GREEN_ETHERNET_ENABLED
} L2SW_GREEN_ETHERNET_MODE;

typedef uint32            L2SW_VLANSEP_MODE; 
#define L2SW_NO_VLANSEP_MODE        0x0001
#define L2SW_PARTIAL_VLANSEP_MODE   0x0002
#define L2SW_FULL_VLANSEP_MODE      0x0004

typedef uint32            L2SW_ACL_FIELD; 
#define L2SW_ACL_RULE_MAC_ADDRESS   0x0001
#define L2SW_ACL_RULE_ETHERTYPE     0x0002

#endif
