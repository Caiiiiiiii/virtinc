#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <netinet/in.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <pcap.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <signal.h>

#include "client.h"
#include "util.h"

//打开网卡，返回ip地址
uint32_t open_pcap(char * dev_name, pcap_t ** pcap_handle){
  char errbuf[PCAP_ERRBUF_SIZE];
  uint32_t net_ip;    
  uint32_t net_mask; 
#if TEST
  printf("try to find %s\n", dev_name);
#endif
  int res = pcap_lookupnet(dev_name, &net_ip, &net_mask,  errbuf);
  if(res == -1) {
    printf("pcap_lookupnet(): %s\n", errbuf); 
    clean_exit();
  }

  *pcap_handle = pcap_open_live(dev_name, BUFSIZ, 1, -1, errbuf);
  if(*pcap_handle == NULL) { 
    printf("pcap_open_live(): %s\n", errbuf); 
    clean_exit();
  }
#if TEST
  printf("find %s successfully\n", dev_name);
#endif
  return net_ip;
}

//checksum验证，错误返回0，否则返回1
int check(const unsigned char *packet_content) {
  ip_header_t * ip_head = (ip_header_t *)packet_content;
  int tmpchecksum = ip_head->checksum;
  ip_head->checksum = 0;
  int checksum = compute_checksum(packet_content, ip_head->length);
  ip_head->checksum = tmpchecksum;
  if(checksum != tmpchecksum)
    return 0;
  return 1;
}

// 接收线程接收到数据之后写入缓冲区
void write_receive_buffer(unsigned char *argument, const struct pcap_pkthdr *packet_header, const unsigned char *packet_content){

  printf("func: %s\n", __FUNCTION__);

  // 接收包统计
  receive_packet_num++;

  // checksum计算
  if(!check(packet_content)){
    printf("Packet %d: checksum not match\n", receive_packet_num);
    return ;
  }

  // 写入缓冲区
  ip_pcb_t * current = malloc(sizeof(ip_pcb_t));
  memset(current, 0, sizeof(ip_pcb_t));
  memcpy(current, packet_content, packet_header->len);

  printf("receive packet id = %d, FLAG = %d, seq_num = %d\n",
	 ((incp_header_t *)(current->data))->conn_id,
	 ((incp_header_t *)(current->data))->flag,
	 ((incp_header_t *)(current->data))->seq_num);

  int id = ((incp_header_t *)(current->data))->conn_id;
  
  pthread_mutex_lock(&recv_mutex[id]);
  recv_tail[id]->next = current;
  recv_tail[id] = current;
  recv_tail[id]->next = NULL;
  pthread_mutex_unlock(&recv_mutex[id]);
}

// loop接收，调用grinder
void* run_receive_daemon(){
  printf("func: %s\n", __FUNCTION__);
  if(pcap_loop(pcap_handle, -1, write_receive_buffer, NULL) < 0) {
    perror("pcap_loop: ");
  }	
}

void send_packet(ip_pcb_t *ip) {
  int send_bytes = pcap_inject(pcap_handle, ip, ((ip_header_t*)ip)->length);
  if(send_bytes != ((ip_header_t*)ip)->length){
    printf("packet damage: sendbytes = %d length_of_ip = %d\n", 
	   send_bytes, ((ip_header_t*)&ip)->length);
    clean_exit();
  }
}

void process_window(task_t *task, int seq) {
  printf("func: %s, seq = %d\n", __FUNCTION__, seq);
  if(seq >= task->p + WINDOW_SIZE) // exceed
    return ;
  if(seq < task->p) // not in queue
    return ;
  int p = seq % WINDOW_SIZE;
  if(task->ack_mask >> p & 1) // repeat ack
    return ;
  task->ack_mask |= 1 << p;
  // first packet in (task->p % WS)
  for(int now = task->p % WINDOW_SIZE; task->ack_mask >> now & 1; now = (now + 1) % WINDOW_SIZE) {
    task->ack_mask ^= 1 << now; // clear bit
    task->time[now] = 0;
    if(task->p + WINDOW_SIZE < task->packet_num)
      make_and_send(task, task->p + WINDOW_SIZE);
    ++task->p;
  }
  if(task->p == task->packet_num) // all ACKed
    task->state = 0; // close
}

void send_tle_packet(task_t *task) {
  int now = clock();
  for(int i = 0; i < WINDOW_SIZE; ++i) 
    if(task->time[i] != 0 && task->time[i] + MAX_DELAY < now) {
      send_packet(&(task->packet[i]));
      task->time[i] = now;
    }
}

void pre_send(task_t *task) {
  for(int i = 0; i < WINDOW_SIZE && i < task->packet_num; ++i)
    make_and_send(task, i);
}

void make_and_send(task_t *task, int p) {
  int size = INCP_PAYLOAD;
  if(p == task->packet_num - 1) // last packet
    size = task->size - INCP_PAYLOAD * (task->packet_num - 1);
  ip_pcb_t *sd = &(task->packet[p % WINDOW_SIZE]);
  ((ip_header_t *)sd)->src_ip = task->src;
  ((ip_header_t *)sd)->dst_ip = task->dst;
  ((incp_header_t *)(sd->data))->conn_id = task->id;
  ((incp_header_t *)(sd->data))->seq_num = p;
  ((incp_header_t *)(sd->data))->flag = 0;
  ((incp_header_t *)(sd->data))->payload_length = size;
  fread(((in_pcb_t *)(sd->data))->data, size, 1, task->fp);
  ((ip_header_t *)sd)->length = sizeof(ip_header_t) + sizeof(incp_header_t) + size;
  ((ip_header_t *)sd)->checksum = 0;
  ((ip_header_t *)sd)->checksum = compute_checksum(sd, ((ip_header_t *)sd)->length);
  send_packet(sd);
  task->time[p % WINDOW_SIZE] = clock();
}

void init_link(task_t *task) {
  // hakusyu
  printf(">>>> %d, hakusyu!, file size = %d\n", task->id, task->size);
  ip_pcb_t sd;
  memset(&sd, 0, sizeof(ip_pcb_t));
  ((ip_header_t *)&sd)->src_ip = task->src;
  ((ip_header_t *)&sd)->dst_ip = task->dst;
  ((incp_header_t *)(sd.data))->conn_id = tasks->id;
  ((incp_header_t *)(sd.data))->seq_num = tasks->packet_num;
  ((incp_header_t *)(sd.data))->flag = 2;
  ((incp_header_t *)(sd.data))->payload_length = 0;
  ((in_pcb_t *)(sd.data))->data[0] = 0;
  sd.ip_head.length = sizeof(ip_header_t) + sizeof(incp_header_t);
  ((ip_header_t *)&sd)->checksum = 0;
  ((ip_header_t *)&sd)->checksum = compute_checksum(&sd, ((ip_header_t *)&sd)->length);
  send_packet(&sd);
}

// 从文件中读取字符串，调用send进入发送阻塞
void * run_sender(void * conn_id){
  int id = *(int *)conn_id;
  tasks[id].id = id;
  tasks[id].fp = fopen(file_name, "r");
  if(tasks[id].fp == NULL){
    perror("run_sender(): ");
    clean_exit();
  }
  fseek(tasks[id].fp, 0, SEEK_END);
  tasks[id].size = ftell(tasks[id].fp);
  rewind(tasks[id].fp);
  tasks[id].src = inet_addr(src_ip);
  tasks[id].dst = inet_addr(dst_ip);
  tasks[id].packet_num = tasks[id].size / INCP_PAYLOAD +
    (tasks[id].size % INCP_PAYLOAD != 0);
  tasks[id].state = 1;

  init_link(&tasks[id]);

  ip_pcb_t *cur;
  while(tasks[id].state) {
    cur = recv_head[id]->next;
    if(cur != NULL) {
      if(((incp_header_t *)(cur->data))->flag == 1) { // ACK
	if(tasks[id].state != 2) {
	  printf("%d: WRINING: HAKUSYU not competed!\n", id);
	}
	else 
	  process_window(&tasks[id], ((incp_header_t *)(cur->data))->seq_num);
      }
      else if(((incp_header_t *)(cur->data))->flag == 4) {
	printf(">>>>%d: hakusyu END!\n", id);
	tasks[id].state = 2;
	tasks[id].p = 0;
	tasks[id].ack_mask = 0;
	pre_send(&tasks[id]);
      }
      else 
	printf("%d: unknown FLAG: %d!\n", id, ((incp_header_t *)(cur->data))->flag);
      pthread_mutex_lock(&recv_mutex[id]);
      recv_head[id]->next = cur->next;
      if(recv_head[id]->next == NULL)
	recv_tail[id] = recv_head[id];
      free(cur);
      pthread_mutex_unlock(&recv_mutex[id]);
    }
    send_tle_packet(&tasks[id]);
  }

  ended_sender |= 1 << id;
  fclose(tasks[id].fp);
  printf("%d: end\n", id);
  // hakusyu end
}

void clean_exit() {
  if(pcap_handle != NULL)
    pcap_close(pcap_handle);

  ip_pcb_t *prev, *current;
  for(int i = 0; i < conn_num; ++i) {
    prev = recv_head[i];
    current = prev->next;
    while(current != NULL){
      prev = current;
      current = prev->next;
      free(prev);
    }
    free(recv_head[i]);
  }
  // 锁
  for(int i = 0; i < conn_num; ++i)
    pthread_mutex_destroy(&recv_mutex[i]);

  for(int i = 0; i < conn_num; ++i)
    if(tasks[i].state != 0)
      fclose(tasks[i].fp);
			
  printf("total sended packet: %d\n", send_packet_num);
  printf("Sender Exit...\n\n\n");

  exit(0);
}

void init_buffer() {
  for(int i = 0; i < conn_num; ++i) {
    recv_head[i] = malloc(sizeof(ip_pcb_t));
    memset(recv_head[i], 0, sizeof(ip_pcb_t));
    recv_tail[i] = recv_head[i];
  }
}

// host1运行：初始化send_state, 运行sender线程
int main(int argc, char** argv){
  // pcap初始化
  dev_name = "host1-iface1";
  open_pcap(dev_name, &pcap_handle);

  // 连接数
  conn_num = atoi(argv[2]);
  all_sender = (1 << conn_num) - 1;
  ended_sender = 0;
  
  //buffer
  init_buffer();
    
  // 锁和条件变量
  for(int i = 0; i < conn_num; ++i)
    pthread_mutex_init(&recv_mutex[i], NULL);

  // 运行处理线程和接收线程
  int res = pthread_create(&receive_daemon, NULL, run_receive_daemon, NULL);
  if(res < 0){
    printf("pthread_create error: %d\n", res);
    clean_exit();
  }

  // 运行sender_list
  int conn_group[MAX_CONN_NUM] = {};
  for(int i = 0; i < conn_num; ++i){ 
    conn_group[i] = i;
    int res = pthread_create(&(sender_list[i]), NULL, run_sender, &(conn_group[i]));
    if(res < 0){
      printf("pthread_create error: %d\n", res);
      clean_exit();
    }
  }

  while(all_sender != ended_sender) ;
  
  pthread_kill(receive_daemon, SIGKILL);
  //回收线程和内存
  for(int i = 0; i < conn_num; ++i){
    pthread_join(sender_list[i], NULL);
  }
#if TEST
  printf("pthread_join sender_list finish\n");
#endif
   
  pthread_join(receive_daemon, NULL);

#if TEST
  printf("pthread_join daemon finish\n");
#endif
  clean_exit();

  return 0;
}

