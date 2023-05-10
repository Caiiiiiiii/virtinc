#ifndef _NET_H_
#define _NET_H_

#include <stdint.h>
#include <sys/types.h>
#include <stdio.h>
#include <pcap.h>
#include <pthread.h>
#include <time.h>

#define TEST 1

#define BUFFER_SIZE 1480
#define INCP_PAYLOAD 128

/*******************************************************************************
 * put all data structures and function prototypes here in this file
 ******************************************************************************/

/*******************************************************************************
 * data structures about packet headers, ip_header and layer-4 header
 ******************************************************************************/
typedef struct __attribute__((packed)) IP_Header
{
  uint8_t version:4, headlen:4; // 版本信息(前4位)，头长度(后4位)
  uint8_t type_of_service;      // 服务类型8位
  uint16_t length;              // 整个数据包长度
  uint16_t packet_id;           // 数据包标识
  uint16_t slice_info;          // 分片使用
  uint8_t ttl;                  // 存活时间
  uint8_t type_of_protocol;     // 协议类型
  uint16_t checksum;            // 校验和
  uint32_t src_ip;              // 源ip
  uint32_t dst_ip;              // 目的ip
}ip_header_t; // 总长度5*int32

/**** flag
 * 1: ACK
 * 2: 用于建立通信时
 * 3: 需要 calc 生成质数及原根 / 传输质数及原根
 * 4: 回复建立通信
 * 5: 传输质数及原根 (calc only
 * 6: 传输 key (calc only
 * 7: trans file
*** 举例：host1 向 host2 建立通信过程
*** A -x> B 表示 A 向 B 发包，flag 为 x
* host1 -2> switch1 -3> calc1(生成g,p,a，传输g,p,g^a) -5> switch1 -3> switch2 
  -3> calc2(记录g,p,g^a, 不传输) -5> switch2 -2> host2
* host2 -4> switch2 -4> calc2(生成b，传输g^{ab},g^b) -6> switch2(记录g^{ab}，传输g^b) 
  -4> switch1 -4> calc1(传输g^{ab}) -6> switch1 -4> host1
*****/

typedef struct __attribute__((packed)) INCP_Header
{
  uint8_t conn_id;
  uint32_t seq_num;
  uint8_t flag; // {1: ack, 2: hello, 4: hello_ack}
  uint16_t payload_length; // payload的长度（ack为0）
}incp_header_t;//总长度2*int32

/*******************************************************************************
 * data structures about packet control, ip_header and layer-4 header
 ******************************************************************************/
typedef struct __attribute__((packed)) incp_packet_control_t in_pcb_t;
struct incp_packet_control_t
{
  incp_header_t incp_head;
  char data[INCP_PAYLOAD * 3 + 1];
};

typedef struct __attribute__((packed)) ip_packet_control_t ip_pcb_t;
struct ip_packet_control_t
{
  ip_header_t ip_head;
  char data[BUFFER_SIZE];
  ip_pcb_t * next;
};

#endif
