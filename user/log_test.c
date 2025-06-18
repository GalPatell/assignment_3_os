#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define PGSIZE 4096
#define MAX_CHILDREN 6  // Easy to change - can be 4, 6, 8, etc.
#define HEADER_SIZE 4
#define MAX_MSG_LEN 128  // Increased to accommodate longer messages

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
  int msg_count = 0;
  
  // Different suffixes for different children to create variable lengths
  char *suffixes[] = {
    " says hi!",           // Child 0: shorter message
    " is logging data!",   // Child 1: medium message  
    " has a longer message to demonstrate variable length support!",  // Child 2: long message
    " works!"              // Child 3: short message
  };
  
  // Keep writing messages until buffer is full
  while (1) {
    char msg[MAX_MSG_LEN];
    int len = 0;

    // Build message: "Child X message Y: <suffix>"
    char *prefix = "Child ";
    for (int i = 0; prefix[i]; i++)
      msg[len++] = prefix[i];
    msg[len++] = '0' + index;
    
    char *mid = " message ";
    for (int i = 0; mid[i]; i++)
      msg[len++] = mid[i];
    
    // Convert msg_count to string (support larger numbers)
    int temp = msg_count;
    int digits = 0;
    if (temp == 0) {
      msg[len++] = '0';
    } else {
      // Count digits
      int temp2 = temp;
      while (temp2 > 0) {
        digits++;
        temp2 /= 10;
      }
      // Write digits in reverse order, then reverse
      int start_pos = len;
      while (temp > 0) {
        msg[len++] = '0' + (temp % 10);
        temp /= 10;
      }
      // Reverse the digits
      for (int i = 0; i < digits / 2; i++) {
        char tmp = msg[start_pos + i];
        msg[start_pos + i] = msg[start_pos + digits - 1 - i];
        msg[start_pos + digits - 1 - i] = tmp;
      }
    }
    
    char *colon = ": ";
    for (int i = 0; colon[i]; i++)
      msg[len++] = colon[i];
    
    char *suffix = suffixes[index % 4]; // Cycle through suffixes for more children
    for (int i = 0; suffix[i]; i++)
      msg[len++] = suffix[i];
    msg[len] = '\0'; // null terminator

    // Try to find space in the buffer
    int found_space = 0;
    for (char *ptr = shm; ptr + HEADER_SIZE + len < shm + PGSIZE;) {
      uint32 *hdr = (uint32 *)ptr;
      uint32 current_header = *hdr;

      if (__sync_val_compare_and_swap(hdr, 0, make_header(index, len)) == 0) {
        // Successfully claimed this slot
        memcpy(ptr + HEADER_SIZE, msg, len);
        found_space = 1;
        msg_count++;
        break;
      } else {
        // Slot is occupied, need to skip over the existing message
        uint16 existing_index, existing_len;
        decode_header(current_header, &existing_index, &existing_len);
        
        // Skip over the existing header and message
        ptr += HEADER_SIZE + existing_len;
        // Align to 4-byte boundary
        ptr = (char *)(((uint64)ptr + 3) & ~3);
      }
    }

    // If no space found, buffer is full - exit
    if (!found_space) {
      // Remove the printf to avoid garbled output
      exit(0);
    }
    
    // Add a small delay to give other processes a chance
    // This helps with fairness but isn't required by the assignment
    for (volatile int i = 0; i < 500000; i++);
  }
}

void read_log(char *shm) {
  char *ptr = shm;
  int child_counts[32] = {0}; // Support up to 32 children
  int total_messages = 0;
  int max_child_seen = 0;
  
  while (ptr + HEADER_SIZE < shm + PGSIZE) {
    uint32 header = *(uint32 *)ptr;
    if (header == 0)
      break;

    uint16 index, len;
    decode_header(header, &index, &len);

    // Check if we have enough space for the message
    if (ptr + HEADER_SIZE + len > shm + PGSIZE)
      break;

    char msg[MAX_MSG_LEN + 1] = {0};
    memcpy(msg, ptr + HEADER_SIZE, len);
    msg[len] = '\0';

    printf("Parent read message from child %d: %s\n", index, msg);
    
    // Count messages per child
    if (index < 32) {
      child_counts[index]++;
      if (index > max_child_seen) {
        max_child_seen = index;
      }
    }
    total_messages++;

    ptr += HEADER_SIZE + len;
    ptr = (char *)(((uint64)ptr + 3) & ~3);
  }
  
  // Print summary
  printf("\n=== SUMMARY ===\n");
  printf("Total messages: %d\n", total_messages);
  for (int i = 0; i <= max_child_seen; i++) {
    printf("Child %d wrote: %d messages\n", i, child_counts[i]);
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

  printf("Parent creating %d children\n", MAX_CHILDREN);

  // Fork children
  for (int i = 0; i < MAX_CHILDREN; i++) {
    int pid = fork();
    if (pid == 0) {
      // Child process
      char *mapped = (char *)map_shared_pages((void *)buffer, PGSIZE, mypid);
      if ((uint64)mapped == 0 || mapped == (void *)-1) {
        exit(1);
      }
      write_log(mapped, i);
    } else if (pid < 0) {
      printf("Fork failed for child %d\n", i);
      exit(1);
    }
  }
  
  // Wait for all children
  printf("Parent waiting for children to finish\n");
  for (int i = 0; i < MAX_CHILDREN; i++) {
    wait(0);
  }

  printf("All children finished. Reading messages from buffer:\n");
  // Read log from shared memory
  read_log(buffer);
  exit(0);
}