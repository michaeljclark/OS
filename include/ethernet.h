#define ETHERNET_HDR_SIZE 14
#define ETHERNET_TRL_SIZE 4
#define ETHERNET_FRAME_SIZE_MIN 64
#define ETHERNET_FRAME_SIZE_MAX 1518
#define ETHERNET_PAYLOAD_SIZE_MIN (ETHERNET_FRAME_SIZE_MIN - (ETHERNET_HDR_SIZE + ETHERNET_TRL_SIZE))
#define ETHERNET_PAYLOAD_SIZE_MAX (ETHERNET_FRAME_SIZE_MAX - (ETHERNET_HDR_SIZE + ETHERNET_TRL_SIZE))

#define ETHERNET_TYPE_IP   0x0800
#define ETHERNET_TYPE_ARP  0x0806
#define ETHERNET_TYPE_IPV6 0x86dd

#define ETHERNET_ADDR_LEN 6
#define ETHERNET_ADDR_STR_LEN 18 /* "xx:xx:xx:xx:xx:xx\0" */

extern const uint8 ETHERNET_ADDR_ANY[ETHERNET_ADDR_LEN];
extern const uint8 ETHERNET_ADDR_BROADCAST[ETHERNET_ADDR_LEN];

struct ethernet_hdr {
    uint8 dst[ETHERNET_ADDR_LEN];
    uint8 src[ETHERNET_ADDR_LEN];
    uint16 type;
};
