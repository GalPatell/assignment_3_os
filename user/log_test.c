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
  // Append '!' characters — 1 per child index
  for (int j = 0; j <= index && len < MAX_MSG_LEN - 1; j++)
    msg[len++] = '!';
  msg[len] = '\0'; // null terminator

  for (char *ptr = shm; ptr + HEADER_SIZE + len < shm + PGSIZE;) {
    uint32 *hdr = (uint32 *)ptr;

    if (__sync_val_compare_and_swap(hdr, 0, make_header(index, len)) == 0) {
      memcpy(ptr + HEADER_SIZE, msg, len);
      ptr += HEADER_SIZE + len;
      ptr = (char *)(((uint64)ptr + 3) & ~3); // align
      sleep(1); // small pause to allow other children a chance

      } else {
      ptr += HEADER_SIZE + len;
      ptr = (char *)(((uint64)ptr + 3) & ~3); // align
    }
  }

  exit(0); 
}

int read_log_live(char *shm, int *last_offset) {
  char *ptr = shm + *last_offset;
  int msg_count = 0;

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
    msg_count++;

    ptr += HEADER_SIZE + len;
    ptr = (char *)(((uint64)ptr + 3) & ~3);
  }

  *last_offset = ptr - shm;
  return msg_count; // return number of messages read
}

int main() {
  char *buffer = malloc(PGSIZE);
  if (buffer == 0) {
    printf("Failed to allocate memory\n");
    exit(1);
  }

  memset(buffer, 0, PGSIZE);
  int mypid = getpid();

  for (int i = 0; i < MAX_CHILDREN; i++) {
    int pid = fork();
    if (pid == 0) {
      char *mapped = (char *)map_shared_pages((void *)buffer, PGSIZE, mypid);
      if ((uint64)mapped == 0 || mapped == (void *)-1)
        exit(1);
      sleep(5); // let parent start scanning
      write_log(mapped, i);
    }
  }

  int offset = 0;
  int total_messages = 0;

  int idle_rounds = 0;
  const int MAX_IDLE = 10;  // max rounds without new messages before stopping
  for (;;) {
    int msgs_read = read_log_live(buffer, &offset);
    total_messages += msgs_read;

    if (msgs_read == 0) {
        idle_rounds++;
    } else {
        idle_rounds = 0; // reset if we made progress
    }
    
    if (offset + HEADER_SIZE >= PGSIZE || idle_rounds >= MAX_IDLE) {
        printf("Parent: Buffer appears full, ending after %d total messages\n", total_messages);
        break;
    }
    
    sleep(1); // lightweight waiting
  }
  
  exit(0);
}
