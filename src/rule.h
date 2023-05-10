#ifndef _RULE_H_
#define _RULE_H_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <arpa/inet.h>

#define _RULE_LENGTH 25

typedef struct _rule rule_t;
struct _rule {
  uint32_t src_ip;
  uint32_t flag;
  uint32_t new_flag;
  uint32_t action;
  uint32_t port;
  rule_t *next;
};

rule_t * rule_list;

char *i_ntoa(int ip) {
  struct in_addr ipaddr;
  memcpy(&ipaddr, &ip, 4);
  return inet_ntoa(ipaddr);
}

void output_rule(struct _rule r) {
  printf("{\n");
  printf("src: %s\n", i_ntoa(r.src_ip));
  printf("flag: %d\n", r.flag);
  printf("new_flag: %d\n", r.new_flag);
  printf("action: %d\n", r.action);
  printf("port: %d\n", r.port);
  printf("}\n");
}

void print_rulelist() {
  rule_t *prev;
  while(prev->next != NULL) {
    prev = prev->next;
    output_rule(*prev);
  }
}

void prase_error(int errnum, char *s) {
  if(errnum == -1) 
    fprintf(stderr, "ERROR: %s.. is too long, parse stopped!\n", s);
  else if(errnum == -2) 
    fprintf(stderr, "ERROR: unexpected %s}, parse stopped!\n", s);
  else if(errnum == -3) 
    fprintf(stderr, "ERROR: unexpected EOF, parse stopped!\n");
}

// read a string from f ending with @end or '}'
int fread_string(FILE *f, char *buf, char end) {
  char c;
  int times = 0;
  for(c = fgetc(f); c != EOF && c != end && c != '}'; c = fgetc(f)) {
    if(c == '#') {
      while(c != '\n' && c != EOF)
	c = fgetc(f);
      continue;
    }
    if(c == ' ' || c == '\n')
      continue;
    *buf++ = c;
    if(++times > _RULE_LENGTH) 
      return -1; // too long
  }
  *buf = 0;
  if(c == end)
    return 0;
  else if(c == '}') {
    if(times == 0)
      return 1;
    else
      return -2; //
  }
  else
    return -3;
}

int fread_type(FILE *f, char *s) {
  int ret = fread_string(f, s, ':');
  if(ret < 0) {
    prase_error(ret, s);
    return -1;
  }
  return ret;
}

int fread_value(FILE *f, char *s) {
  int ret = fread_string(f, s, ';');
  if(ret < 0) {
    prase_error(ret, s);
    return -1;
  }
  if(ret == 1) {
    prase_error(-2, s);
    return -1;
  }
  return 0;
}

int parse_rulefile(const char *filename) {
#if TEST
  printf("parse_rulefile\n");
#endif
  int ret;
  char c, s[_RULE_LENGTH + 5], t[_RULE_LENGTH + 5], *p;

  FILE *f = fopen(filename, "r");
  if(f == NULL) {
    fprintf(stderr, "%s cannot open\n", filename);
    return -1;
  }

  rule_list = (rule_t *)malloc(sizeof(rule_t));
  if(rule_list == NULL){
    perror("parse_rulefile: ");
    return -1;
  }
  memset(rule_list, 0, sizeof(rule_t));
  rule_t r, *prev, *current;
  prev = rule_list;
  
  for(c = fgetc(f); c != EOF; c = fgetc(f)) {
    if(c == '#') {
      while(c != '\n' && c != EOF)
	c = fgetc(f);
      continue;
    }
    if(c == ' ' || c == '\n')
      continue;
    if(c == '{') {
      r.action = 0;
      r.flag = r.new_flag = -1;
      while(1) {
	ret = fread_type(f, s);
	if(ret < 0)
	  return -1;
	if(ret == 1) 
	  break;
	if((ret = fread_value(f, t)) < 0)
	  return -1;
	if(!strcmp(s, "src")) 
	  r.src_ip = inet_addr(t);
	else if(!strcmp(s, "flag"))
	  sscanf(t, "%d", &r.flag);
	else if(!strcmp(s, "new_flag")) 
	  sscanf(t, "%d", &r.new_flag);
	else if(!strcmp(s, "action"))
	  sscanf(t, "%d", &r.action);
	else if(!strcmp(s, "port"))
	  sscanf(t, "%d", &r.port);
	else {
	  printf("unknown type %s, parse stopped!\n", s);
	  return -1;
	}
      }
      current = (rule_t *)malloc(sizeof(rule_t));
      if(current == NULL){
	perror("parse_rulefile: ");
	return -1;
      }
      memcpy(current, &r, sizeof(rule_t));
      prev->next = current;
      prev = current;
    }
  }
  fclose(f);
  return 0;
}

#endif // _RULE_H_
