#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char** argv){
	
  if(strcmp(argv[1], "sw1") == 0) 
    execl("./bin/switch", "switch1-iface%d", "bin/switch1.config", "3", NULL);
  else if(strcmp(argv[1], "sw2") == 0) 
    execl("./bin/switch", "switch2-iface%d", "bin/switch2.config", "3", NULL);
  else if(strcmp(argv[1], "calc1") == 0) 
    execl("./bin/switch", "calc1-iface%d", "bin/calc1.config", "1", NULL);
  else if(strcmp(argv[1], "calc2") == 0)
    execl("./bin/switch", "calc2-iface%d", "bin/calc2.config", "1", NULL);
  else if(strcmp(argv[1], "client") == 0) 
    execv("./bin/client", argv);
  else if(strcmp(argv[1], "server") == 0)
    execv("./bin/server", argv);
  else 
    printf("NOT FOUND");

  return 0;
}
