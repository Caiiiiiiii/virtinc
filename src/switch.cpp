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
#include <pthread.h>
#include <signal.h>

#include "util.h"
#include "rule.h"

#include "switch.h"

#include "action.cpp"

int rule_match(rule_t *rule, ip_pcb_t *pack) {
  if(rule->flag != ((incp_header_t *)(pack->data))->flag)
    return 0;
  return 1;
}

void * tle_resend(void *iiiii) {
  while(1) {
    int t = clock(), p;
    for(int i = 0; i < WINDOW_SIZE; ++i) {
      if(agg_flag[i] == 3 && t - agg_time[i] > MAX_DELAY) {
	sw_send(&agg_buffer[i], agg_port[i]);
	agg_time[i] = clock();
      }
    }
  }
  return NULL;
}

//dev_group初始化
void init_port(){
#if TEST
  printf("func: %s, port_num: %d\n", __FUNCTION__, port_num);
#endif
  for(int i = 0; i < port_num; ++i){
    sprintf(dev_group[i], init_sw_link_format, i+1);
    open_pcap(dev_group[i], &pcap_handle_group[i]);
    printf("dev: %s, pcap: %p\n", dev_group[i], &pcap_handle_group[i]);
  }
}

//缓冲区初始化
void init_buffer(){
  printf("func: %s\n", __FUNCTION__);
  
  empty_num = SLOT_NUM;
  used_slot_head = (ip_pcb_t *)malloc(sizeof(ip_pcb_t));
  unused_slot_head = (ip_pcb_t *)malloc(sizeof(ip_pcb_t));
  if(used_slot_head == NULL || unused_slot_head == NULL){
    perror("init_buffer: ");
    clean_exit();
  }
  last_used_slot = used_slot_head;
  memset(used_slot_head, 0, sizeof(ip_pcb_t));
  memset(unused_slot_head, 0, sizeof(ip_pcb_t));
		
  ip_pcb_t * prev, * current;
  prev = unused_slot_head;
  
  for(int i = 0; i < SLOT_NUM; ++i){
    current = (ip_pcb_t *)malloc(sizeof(ip_pcb_t));
    if(current == NULL){
      perror("init_buffer: ");
      clean_exit();
    }
    memset(current, 0, sizeof(ip_pcb_t));
    prev->next = current;
    prev = current;
  }
}

int init_switch(const char *iface_n, const char *rule_f, int _p){

  printf("func: %s, arg = [%s, %s, %d]\n", __FUNCTION__, iface_n, rule_f, _p);
  
  port_num = _p;
  
  // 初始化规则列表
  puts(rule_f);
  parse_rulefile(rule_f);

  init_sw_link_format = iface_n;
  
  // 初始化端口
  init_port();

  // 初始化缓冲区
  init_buffer();

  // 初始化线程
  writer_num = port_num;

  // 初始化锁
  pthread_mutex_init(&packet_num_mutex, NULL);
  pthread_mutex_init(&slot_mutex, NULL);
  pthread_cond_init(&slot_empty, NULL);
  pthread_cond_init(&slot_full, NULL);
  return 0;
}

//checksum检查，获得锁然后写入缓冲区（链表操作），释放锁
void write_buffer(unsigned char *argument, const struct pcap_pkthdr *packet_header,
		  const unsigned char *packet_content){
  printf("func: %s\n", __FUNCTION__);
  
  pthread_mutex_lock(&packet_num_mutex);
  receive_packet_num++;
	
  time_stamp = clock();
  
  pthread_mutex_unlock(&packet_num_mutex);

  //checksum检查
  if(!check(packet_content)){
    printf("Packet %d: checksum not match\n", receive_packet_num);
    return ;
  }

  //消费者操作
  pthread_mutex_lock(&slot_mutex);
  while(empty_num == 0){
    pthread_cond_wait(&slot_empty, &slot_mutex);
  }

  //写入缓冲区（链表操作）
  ip_pcb_t * current = unused_slot_head->next;
  unused_slot_head->next = current->next;
  memcpy(current, packet_content, packet_header->len);
  // 插入used_slot队列最后
  last_used_slot->next = current;
  last_used_slot = current;
  last_used_slot->next = NULL;
  empty_num--;
  
  printf("receive packet FLAG: %d\n", ((incp_header_t *)(current->data))->flag);

  pthread_cond_signal(&slot_full);
  pthread_mutex_unlock(&slot_mutex);

  return;
}

int match_and_send(ip_pcb_t * ip_pcb){
  printf("func: %s\n", __FUNCTION__);
  
  rule_t * prev_rule = rule_list;
  rule_t *curr_rule = prev_rule->next;
  while(curr_rule != NULL) {
    if(rule_match(curr_rule, ip_pcb)) {
      if(curr_rule->new_flag != -1)
	((incp_header_t *)(ip_pcb->data))->flag = curr_rule->new_flag;
      int rt = action_list[curr_rule->action](ip_pcb, curr_rule->port);
      printf("end with code: %d\n", rt);
      return 1;
    }
    prev_rule = curr_rule;
    curr_rule = prev_rule->next;
  }
  return -1;
}

//获得锁，遍历，读出，调整链表,释放锁
void * run_reader(void *arg) {
  printf("func: %s\n", __FUNCTION__);
  
  while(1){
    //生产者操作
    pthread_mutex_lock(&slot_mutex);
    while(empty_num == SLOT_NUM){
      pthread_cond_wait(&slot_full, &slot_mutex);
    }

    //遍历，读出，调整链表
    ip_pcb_t * current = used_slot_head->next;
    if(current != NULL) {
      used_slot_head->next = current->next;
      if(used_slot_head->next == NULL){
	last_used_slot = used_slot_head;
      }
      pthread_mutex_unlock(&slot_mutex);
      int res = match_and_send(current);
      if(res < 0) {
	printf("error packet: cannot found port to send: ");
	struct in_addr addr;
	memcpy(&addr, &(((ip_header_t*)current)->src_ip), sizeof(uint32_t));
	printf("src_ip = %s\n", inet_ntoa(addr));
      }
      memset(current, 0, sizeof(ip_pcb_t));
      pthread_mutex_lock(&slot_mutex);
      empty_num++;

      current->next = unused_slot_head->next;
      unused_slot_head->next = current;
    }
    else {
      printf("slot error: empty_num = %d current == NULL\n", empty_num);
    }

    pthread_cond_signal(&slot_empty);
    pthread_mutex_unlock(&slot_mutex);

  }
	
}

//switch设置包处理器，开始收包
void * run_receiver(void *arg){
  printf("func: %s\n", __FUNCTION__);
  int idx = *((int*)arg);
  printf("idx: %d\n", idx);
  if(pcap_loop(pcap_handle_group[idx], -1, write_buffer, NULL) < 0) {
    perror("pcap_loop: ");
  }
  return NULL;
}

//资源回收退出程序
void clean_exit(){
  for(int i = 0; i <= writer_num; ++i){
    if(pcap_handle_group[i] != NULL)
      pcap_close(pcap_handle_group[i]);
  }

  // actionlist
  rule_t * prev_rule = rule_list;
  rule_t * curr_rule = prev_rule->next;
  while(curr_rule != NULL){
    prev_rule->next = curr_rule->next;
    free(curr_rule);
    curr_rule = prev_rule->next;
  }
  free(rule_list);

  //缓冲区
  ip_pcb_t* prev = used_slot_head;
  ip_pcb_t* current = prev->next;
  while(current != NULL){
    prev->next = current->next;
    free(current);
    current = prev->next;
  }
  free(used_slot_head);

  prev = unused_slot_head;
  current = prev->next;
  while(current != NULL){
    prev->next = current->next;
    free(current);
    current = prev->next;
  }
  free(unused_slot_head);
	
  //锁
  pthread_mutex_destroy(&packet_num_mutex);
  pthread_mutex_destroy(&slot_mutex);
  pthread_cond_destroy(&slot_empty);
  pthread_cond_destroy(&slot_full);

  printf("Switch Exit...\n\n\n");
  exit(0);
}

void run_switch(){
  printf("func: %s\n", __FUNCTION__);

  pthread_t writer;
  pthread_t reader;
  
  int port_group[writer_num];
  for(int i = 0; i < writer_num; ++i){
    port_group[i] = i;
    int res = pthread_create(&(writer_list[i]), NULL, run_receiver, &(port_group[i]));
    if(res < 0){
      printf("pthread_create error: %d\n", res);
      clean_exit();
    }
  }
  
  int res = pthread_create(&reader, NULL, run_reader, NULL);
  if(res < 0){
    printf("pthread_create error: %d\n", res);
    clean_exit();
  }

  res = pthread_create(&p_tle_resend, NULL, tle_resend, NULL);
  if(res < 0) {
    printf("pthread_create error: %d\n", res);
    clean_exit();
  }
    
  pthread_join(p_tle_resend, NULL);
  
  for(int i = 0; i < writer_num; ++i){
    pthread_join(writer_list[i], NULL);
  }
  
  pthread_join(reader, NULL);
  
  clean_exit();
	
  return;
}

int main(int argc, char **argv) {
  for(int i = 0; i < argc; ++i)
    printf("arg %d: %s\n", i, argv[i]);
  if(argc < 3) {
    printf("expected 3 para, but fount %d\n", argc);
    return 0;
  }
  init_switch(argv[0], argv[1], strtol(argv[2], NULL, 0));
  run_switch();
}

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
