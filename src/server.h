#ifndef _SERVER_H_
#define _SERVER_H_

#include "defs.h"

#define WINDOW_SIZE 16
#define MAX_CONN_NUM 20

#define NAME_SIZE 30

#define IPC_MSG_SIZE 128

typedef struct {
  int id;
  int src;
  int dst;
  FILE *fp;
  int p;
  int packet_num;
  ip_pcb_t packet[WINDOW_SIZE];
  int recv_mask;
  int state; // {0: wait link; 1: linked; 2: closed}
}task_t;

char *dev_name;
int conn_num;
int all_recv, ended_recv;

int receive_packet_num;

pthread_t receive_daemon, receiver_process_daemon;;
pthread_t receiver_list[MAX_CONN_NUM];

// 接收缓冲区和发送缓冲区(host)，ip报文格式

task_t tasks[MAX_CONN_NUM];

ip_pcb_t *recv_head[MAX_CONN_NUM], *recv_tail[MAX_CONN_NUM];
pthread_mutex_t recv_mutex[MAX_CONN_NUM];

pcap_t *pcap_handle;

void send_packet(ip_pcb_t *);
void process_packet(task_t *, ip_pcb_t *);

void reply_ack(task_t *, int);

//sender、receiver线程相关函数
int init_conn(int conn_id); 
int listen_ipc(int conn_id);
int send_ipc(int conn_id, char * text);
int incp_recv(int conn_id, void *addr, unsigned int size); // 构造recv_state，监听

void * run_receiver(void *conn_id);

//daemon线程相关函数
uint32_t open_pcap(char * dev_name, pcap_t ** pcap_handle);//返回网卡地址，也就是src_ip
int check(const unsigned char *packet_content);

void write_receive_buffer(unsigned char *argument, const struct pcap_pkthdr *packet_header,
			  const unsigned char *packet_content);
void * run_receive_daemon();

void clean_exit();

#endif // _SERVER_H_
