#include "brctl.h"

static FILE *fpopen(const char *dir, const char *name)
{
    char path[PATH_MAX];
    snprintf(path, PATH_MAX, "%s/%s", dir, name);
    return fopen(path, "r");
}

int show_interface(const char *name) {
    if (first) 
        first = 0;
    else
        printf("\n\t\t\t\t\t\t\t");
    printf("%s", name);
    return 0;
}

int isbridge(const struct dirent *entry) {
    char path[PATH_MAX];
    struct stat st;
    snprintf(path, PATH_MAX, SYSFS_CLASS_NET "%s/bridge", entry->d_name);
    int ret = (stat(path, &st) == 0 && S_ISDIR(st.st_mode));
    return ret;
}

int show_interfaces(const char *name) {
    first = 1;
    int i, count;
    struct dirent **namelist;
    char path[SYSFS_PATH_MAX];
    snprintf(path, SYSFS_PATH_MAX, SYSFS_CLASS_NET "%s/brif", name);
    count = scandir(path, &namelist, 0, alphasort);
    for (i = 0; i < count; i++) {
        if (namelist[i]->d_name[0] == '.'
            && (namelist[i]->d_name[1] == '\0'
            || (namelist[i]->d_name[1] == '.'
            && namelist[i]->d_name[2] == '\0')))
			continue;
        if (show_interface(namelist[i]->d_name))
        break;
    }
    for (i = 0; i < count; i++)
        free(namelist[i]);
    free(namelist);
    printf("\n");
    return count;
}

int show_bridge(const char *name) {
    static int first = 1;
    unsigned char bridge_id[8];
    int stp_enabled = -1;
    DIR *dir;
    char path[SYSFS_PATH_MAX];
    snprintf(path, SYSFS_PATH_MAX, SYSFS_CLASS_NET "%s/bridge", name);
    dir = opendir(path);
    FILE *f = fpopen(path, "bridge_id");
    if (!f)
        fprintf(stderr, "%s: %s\n", path, strerror(errno));
    else {
        fscanf(f, "%2hhx%2hhx.%2hhx%2hhx%2hhx%2hhx%2hhx%2hhx",
                &bridge_id[0], &bridge_id[1],
                &bridge_id[2], &bridge_id[3], &bridge_id[4],
                &bridge_id[5], &bridge_id[6], &bridge_id[7]);
        fclose(f);
    }
    f = fpopen(path, "stp_state");
    if (!f) 
        return 0;
    fscanf(f, "%i", &stp_enabled);
    fclose(f);
    if (first) {
        printf("bridge name\tbridge id\t\tSTP enabled\tinterfaces\n");
        first = 0;
    }
    printf("%s\t\t", name);
    printf("%.2x%.2x.%.2x%.2x%.2x%.2x%.2x%.2x", bridge_id[0], bridge_id[1], bridge_id[2], bridge_id[3],
            bridge_id[4], bridge_id[5], bridge_id[6], bridge_id[7]);
    printf("\t%s\t\t", stp_enabled ? "yes" : "no");
    show_interfaces(name);
    return 0;
}

int show_bridges() {
    struct dirent **namelist;
    int i, count = 0;
    count = scandir(SYSFS_CLASS_NET, &namelist, isbridge, alphasort);
    for (i = 0; i < count; i++) {
        if (show_bridge(namelist[i]->d_name))
            break;
    }
    for (i = 0; i < count; i++)
        free(namelist[i]);
    free(namelist);
    return 0;
}

int add_data(struct nlmsghdr *n, int maxlen, int type, const void *data, int datalen) {
    int len = RTA_LENGTH(datalen);
    struct rtattr *rta;
    rta = (struct rtattr *)(((char *)(n)) + NLMSG_ALIGN(n->nlmsg_len));
    rta->rta_type = type;
    rta->rta_len = len;
    memcpy(RTA_DATA(rta), data, datalen);
    n->nlmsg_len = NLMSG_ALIGN(n->nlmsg_len) + RTA_ALIGN(len);
    return 0;
}

int netlink_init(struct sockaddr_nl *socket_addr) {
    socklen_t addr_len;
    int err  = 0;
    fd = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_ROUTE);
    if (fd < 0) {
        fprintf(stderr, "Cannot open netlink socket.\n");
        return -1;
    }
    memset(socket_addr, 0, sizeof(*socket_addr));
    socket_addr->nl_family = AF_NETLINK;
    socket_addr->nl_groups = 0;
    if (err = bind(fd, (struct sockaddr *)socket_addr, sizeof(*socket_addr)) < 0) {
        fprintf(stderr, "Cannot bind socket: %s\n", strerror(err));
        return -1;
    }
    addr_len = sizeof(*socket_addr);
    if (err = getsockname(fd, (struct sockaddr *)socket_addr, &addr_len) < 0) {
        fprintf(stderr, "Cannot get socket name: %s\n", strerror(err));
        return -1;
    }
    return 0;
}

int send_netlink_req(struct sockaddr_nl *socket_addr, struct nlmsghdr *n) {
    int status;
    unsigned int seq;
    struct nlmsghdr *h;
    struct sockaddr_nl nladdr = { .nl_family = AF_NETLINK };
    struct iovec iov = {
        .iov_base = n,
        .iov_len = n->nlmsg_len
    };
    struct msghdr msg = {
        .msg_name = &nladdr,
        .msg_namelen = sizeof(nladdr),
        .msg_iov = &iov,
        .msg_iovlen = 1,
    };
    char buf[32768] = {};
    n->nlmsg_seq = ++seq;
    n->nlmsg_flags |= NLM_F_ACK;
    status = sendmsg(fd, &msg, 0);
    if (status < 0) {
        fprintf(stderr, "Cannot send netlink message: %s\n", strerror(status));
        return -1;
    }
    iov.iov_base = buf;
    iov.iov_len = sizeof(buf);
    int len = recvmsg(fd, &msg, 0);
    if (len < 0) {
        fprintf(stderr, "Netlink error: %s\n", strerror(errno));
        return -1;
    }
    if (len == 0) {
        fprintf(stderr, "Len of message 0.\n");
        return -1;
    }
    for (h = (struct nlmsghdr *)buf; NLMSG_OK(h, len); h = NLMSG_NEXT(h, len)) {
        if (h->nlmsg_type == NLMSG_DONE)
            return 0;
        if (h->nlmsg_type == NLMSG_ERROR) {
            struct nlmsgerr *err = (struct nlmsgerr *)NLMSG_DATA(h);
            if (!err->error) {
                return 0;
            }
            fprintf(stderr, "Netlink response: %s\n", strerror(-err->error));
            errno = -err->error;
			return -1;
        }
    }
}

int bridge_request(const char *br_name, int command, int flags) {
    struct netlink_request request = {
        .n.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg)),
        .n.nlmsg_flags = NLM_F_REQUEST | flags,
        .n.nlmsg_type = command,
        .i.ifi_family = AF_UNSPEC,
        .i.ifi_index = 0,
    };
    char *type = "bridge";
    int len = 0;
    if (br_name) {
        len = strlen(br_name);
        if (len == 0) {
            fprintf(stderr, "Name of bridge is empty.\n");
            return -1;
        }
        if (len > IF_NAMESIZE) {
            fprintf(stderr, "Name of bridge is too long.\n");
            return -1;
        }
        add_data(&request.n, sizeof(request), IFLA_IFNAME, br_name, len);
    }
    struct rtattr *info = (struct rtattr *)(((char *)(&request.n)) + NLMSG_ALIGN(request.n.nlmsg_len));
    add_data(&request.n, sizeof(request), IFLA_LINKINFO, NULL, 0);
    add_data(&request.n, sizeof(request), IFLA_INFO_KIND, type, strlen(type));
    info->rta_len = (void *)(struct rtattr *)(((char *)(&request.n)) + NLMSG_ALIGN(request.n.nlmsg_len)) - (void *)info;
    if (send_netlink_req(&addr, &request.n) < 0) {
        fprintf(stderr, "Netlink request failed: %s\n", strerror(errno));
        return -1;
    }
    return 0;
}

int interface_request(const char *br_name, const char *int_name, int flag) {
    struct netlink_request request = {
        .n.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg)),
        .n.nlmsg_flags = NLM_F_REQUEST,
        .n.nlmsg_type = RTM_NEWLINK,
        .i.ifi_family = AF_UNSPEC,
        .i.ifi_index = 0,
    };
    int ifindex = 0;
    if (flag) {
        ifindex = if_nametoindex(br_name);
        if (ifindex == 0) {
            fprintf(stderr, "Device does not exist %s\n", br_name);
            return -1;
        }
    }
    add_data(&request.n, sizeof(request), IFLA_MASTER, &ifindex, 4);
    request.i.ifi_index = if_nametoindex(int_name);
    if (request.i.ifi_index == 0) {
        fprintf(stderr, "Cannot find device %s\n", int_name);
        return -1;
    }
    int err = 0;
    if (err = send_netlink_req(&addr, &request.n) < 0) {
        fprintf(stderr, "Netlink request failed: %s\n", strerror(err));
        return -1;
    }
    return 0;
}

void netlink_close() {
    if (fd >= 0) {
        close(fd);
        fd = -1;
    }
}

int main(int argc, char* argv[]) {
    if (netlink_init(&addr) < 0) {
        fprintf(stderr, "Cannot open netlink connection.\n");
        exit(1);
    }
    if (argc < 2) {
        fprintf(stderr, "No arguments.\n");
    } else {
        if (!strcmp("show", argv[1])) {
            show_bridges();
        } else if (!strcmp("addbr", argv[1])) {
            if (!argv[2]) {
                fprintf(stderr, "No bridge name passed.\n");
            } else {
                bridge_request(argv[2], RTM_NEWLINK, NLM_F_CREATE|NLM_F_EXCL);
            }
        } else if (!strcmp("delbr", argv[1])) {
            if (!argv[2]) {
                fprintf(stderr, "No bridge name passed.\n");
            } else {
                bridge_request(argv[2], RTM_DELLINK, 0);
            }
        } else if (!strcmp("addif", argv[1])) {
            if (!argv[2]) {
                fprintf(stderr, "No bridge name passed.\n");
            } else if (!argv[3]) {
                fprintf(stderr, "No interface name passed.\n");
            } else {
                interface_request(argv[2], argv[3], 1);
            }
        } else if (!strcmp("delif", argv[1])) {
            if (!argv[2]) {
                fprintf(stderr, "No bridge name passed.\n");
            } else if (!argv[3]) {
                fprintf(stderr, "No interface name passed.\n");
            } else {
                interface_request(argv[2], argv[3], 0);
            }
        } else {
            fprintf(stderr, "Unknown command.\n");
        }
    }
    netlink_close();
    return 0;
}