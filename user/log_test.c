#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define PGSIZE 4096
#define MAX_CHILDREN 4
#define HEADER_SIZE 4
#define MAX_MSG_LEN 64

typedef unsigned int uint32;
typedef unsigned short uint16;

uint32 make_header(uint16 index, uint16 len) {
  return ((uint32)index << 16) | len;
}

void decode_header(uint32 header, uint16 *index, uint16 *len) {
  *index = header >> 16;
  *len = header & 0xFFFF;
}

void write_log(char *shm, int index) {
  char msg[MAX_MSG_LEN];
  int len = 0;

  // Manually build message: "Child X logging!"
  char *prefix = "Child ";
  for (int i = 0; prefix[i]; i++)
    msg[len++] = prefix[i];
  msg[len++] = '0' + index;
  char *suffix = " logging!";
  for (int i = 0; suffix[i]; i++)
    msg[len++] = suffix[i];
  msg[len] = '\0'; // null terminator

  for (char *ptr = shm; ptr + HEADER_SIZE + len < shm + PGSIZE;) {
    uint32 *hdr = (uint32 *)ptr;

    if (__sync_val_compare_and_swap(hdr, 0, make_header(index, len)) == 0) {
      memcpy(ptr + HEADER_SIZE, msg, len);

      //printf("Child %d wrote to shared buffer.\n", index);

      exit(0);
    } else {
      ptr += HEADER_SIZE + len;
      ptr = (char *)(((uint64)ptr + 3) & ~3); // align
    }
  }

  exit(0); // fail silently if no space
}

void read_log(char *shm) {
  char *ptr = shm;
  while (ptr + HEADER_SIZE < shm + PGSIZE) {
    uint32 header = *(uint32 *)ptr;
    if (header == 0)
      break;

    uint16 index, len;
    decode_header(header, &index, &len);

    char msg[MAX_MSG_LEN + 1] = {0};
    memcpy(msg, ptr + HEADER_SIZE, len);
    msg[len] = '\0';

    printf("Parent read message from child %d: %s\n", index, msg);

    ptr += HEADER_SIZE + len;
    ptr = (char *)(((uint64)ptr + 3) & ~3);
  }
}


int main() {
  char *buffer = malloc(PGSIZE);
  if (buffer == 0) {
    printf("Failed to allocate memory\n");
    exit(1);
  }

  memset(buffer, 0, PGSIZE);
  int mypid = getpid();

  // Fork children
  for (int i = 0; i < MAX_CHILDREN; i++) {
    int pid = fork();
    if (pid == 0) {
      char *mapped = (char *)map_shared_pages((void *)buffer, PGSIZE, mypid);
      if ((uint64)mapped == 0 || mapped == (void *)-1)
        exit(1);

      write_log(mapped, i);
    }
  }
  // Wait for all children
  for (int i = 0; i < MAX_CHILDREN; i++)
    wait(0);

  // Read log from shared memory
  read_log(buffer);
  exit(0);
}
