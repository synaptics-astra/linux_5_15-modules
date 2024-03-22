#ifndef DSPG_SWITCH_COMMON_EXPORT_H
#define DSPG_SWITCH_COMMON_EXPORT_H

#include <linux/device.h>
#include <linux/fs.h>
#include <linux/ioport.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/spinlock.h>

struct dspg_metadata {
	unsigned int port_map_set:1;
	unsigned int reserved_1:31;  /*Must be zero*/
	unsigned int lan_port:1;
	unsigned int pc_port:1;
	unsigned int reserved_2:30; /*Must be zero*/
};

struct switch_common {
	spinlock_t lock;

	struct class *class;
	struct device *dev;
	struct resource *res;

	struct device *dspg_net_dev;

	//Switch dependent hooks
	int (*sw_dep_rx_hook)(struct net_device *ndev, struct sk_buff *skb, struct dspg_metadata * metadata);
	int (*sw_dep_tx_hook)(struct net_device *ndev, struct sk_buff *skb, struct dspg_metadata * metadata);
	long (*sw_dep_ioctl_hook)(struct file *file, unsigned int cmd, unsigned long arg);
	int (*sw_dep_is_tagged_hook)(struct net_device *ndev, struct sk_buff *skb);
};

extern struct switch_common *g_switch_common;
/* indicate layout has Tx/Rx crossed on PC Port compare to sonic (e.g. MercuryA)*/
extern unsigned int pcPortCrossed;
/* indicate layout has LAN port and PC Port swapped compare to sonic (e.g. Charlie/MercuryC)*/
extern int swap_port;

extern void agn_remove_eth_sw_dep_hooks(void);
extern void agn_set_eth_sw_dep_hooks(int (*rx)(struct net_device *ndev,
					struct sk_buff *skb,
					struct dspg_metadata *metadata),
			int (*tx)(struct net_device *ndev, struct sk_buff *skb,
					struct dspg_metadata *metadata),
			long (*ioctl)(struct file *file, unsigned int cmd,
							unsigned long arg),
            int (*is_tagged)(struct net_device *ndev, struct sk_buff *skb));

#endif

