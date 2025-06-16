#include "kernel/types.h"
#include "user.h"

#define SHARED_SIZE 4096

void print_sz(const char *msg) {
  uint64 sz = (uint64)sbrk(0);
  printf("%s: %d\n", msg, (int)sz);
}

int main(int argc, char *argv[]) {
  int disable_unmap = 0;

  if (argc > 1 && strcmp(argv[1], "no_unmap") == 0)
    disable_unmap = 1;

  char *buf = malloc(SHARED_SIZE);
  strcpy(buf, "Original text in parent");
  int parent_pid = getpid();

  int pid = fork();

  if (pid < 0) {
    printf("Fork failed\n");
    exit(1);
  }

  if (pid == 0) { // child
    

    sleep(15); 
    print_sz("[child] before mapping");

    uint64 remote = map_shared_pages((void *)buf, SHARED_SIZE, parent_pid);
    if (remote == 0) {
      printf("[child] mapping failed\n");
      exit(1);
    }

    char *shared = (char *)remote;
    printf("[child] writing: Hello daddy\n");
    strcpy(shared, "Hello daddy");

    print_sz("[child] after writing");

    if (!disable_unmap) {
      if (unmap_shared_pages(shared, SHARED_SIZE) < 0) {
        printf("[child] unmap failed\n");
      } else {
        printf("[child] unmapped shared memory\n");
      }

      print_sz("[child] after unmap");

      char *p = malloc(100000);
      if (p)
        printf("[child] malloc after unmap successful\n");
      print_sz("[child] after malloc");
    } else {
      printf("[child] skipping unmap\n");
    }

    exit(0);
  } else { // parent
    

    print_sz("[parent] before mapping");

    sleep(5); 

    print_sz("[parent] after mapping");

    sleep(30);

    printf("[parent] read from shared: %s\n", buf);

    wait(0);

    print_sz("[parent] final");

    exit(0);
}
}