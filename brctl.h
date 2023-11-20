#ifndef BRCTL_H_ 
#define BRCTL_H_

#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>
#include <dirent.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <net/if.h>

#define SYSFS_CLASS_NET "/sys/class/net/"
#define SYSFS_PATH_MAX	256

struct sockaddr_nl addr;
int fd = -1;
__u32 seq = 0;
static int first;

struct netlink_request {
    struct nlmsghdr		n;
    struct ifinfomsg	i;
    char			buf[1024];
};

static FILE *fpopen(const char *dir, const char *name);
int show_interface(const char *name);
int isbridge(const struct dirent *entry);
int show_interfaces(const char *name);
int show_bridge(const char *name);
int show_bridges();
int add_data(struct nlmsghdr *n, int maxlen, int type, const void *data, int alen);
int netlink_init(struct sockaddr_nl *socket_addr);
int send_netlink_req(struct sockaddr_nl *socket_addr, struct nlmsghdr *n);
int bridge_request(const char *br_name, int command, int flags);
int interface_request(const char *br_name, const char *int_name, int flag);
void netlink_close();

#endif // BRCTL_H_