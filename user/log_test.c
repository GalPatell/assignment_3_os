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
  
  // More suffixes to support more children
  char *suffixes[] = {
    " says hi!",           
    " is logging data!",   
    " has a longer message to demonstrate variable length support!",  
    " works!",
    " sends greetings!",
    " reports status!",
    " completes task!",
    " running smoothly!"
  };
  int num_suffixes = sizeof(suffixes) / sizeof(suffixes[0]);
  
  // FAIRNESS FIX 1: Add initial delay based on child index to stagger startup
  int initial_delay = (index * 200000) % 800000;  // Different startup delay for each child
  for (volatile int i = 0; i < initial_delay; i++);
  
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
    
    char *suffix = suffixes[index % num_suffixes]; // Use all available suffixes
    for (int i = 0; suffix[i]; i++)
      msg[len++] = suffix[i];
    msg[len] = '\0'; // null terminator

    // FAIRNESS FIX 2: Start from different positions in buffer to reduce contention
    int start_offset = (index * 64) % 512;  // Different starting points for each child
    char *start_ptr = shm + start_offset;
    if (start_ptr >= shm + PGSIZE - HEADER_SIZE - MAX_MSG_LEN) {
      start_ptr = shm;  // Wrap around if too close to end
    }
    
    int found_space = 0;
    int attempts = 0;
    char *ptr = start_ptr;
    
    // Try to find space, with wrapping capability
    while (attempts < 2) {  // At most 2 full scans of buffer
      while (ptr + HEADER_SIZE + len < shm + PGSIZE) {
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
      
      if (found_space) break;
      
      // If we didn't find space and haven't wrapped around yet, try from beginning
      if (attempts == 0 && start_ptr != shm) {
        ptr = shm;
        attempts++;
      } else {
        break;  // Already wrapped around or started from beginning
      }
    }

    // If no space found, buffer is full - exit
    if (!found_space) {
      exit(0);
    }
    
    // FAIRNESS FIX 3: Different delay patterns for each child
    int base_delay = 400000 + ((MAX_CHILDREN - index) * 100000);  // Base delay varies by child
    int variable_delay = (msg_count * 137 + index * 1000) % 300000;  // Some variation
    for (volatile int i = 0; i < base_delay + variable_delay; i++);
  }
}

// Modified read function to track reading position
int read_new_messages(char *shm, char **read_ptr, int child_counts[], int *total_messages, int *max_child_seen) {
  int new_messages = 0;
  char *ptr = *read_ptr;
  
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
      if (index > *max_child_seen) {
        *max_child_seen = index;
      }
    }
    (*total_messages)++;
    new_messages++;

    ptr += HEADER_SIZE + len;
    ptr = (char *)(((uint64)ptr + 3) & ~3);
  }
  
  *read_ptr = ptr;
  return new_messages;
}

// Simple check to see if we should keep reading
// Returns 1 if we should continue, 0 if we should stop
int should_continue_reading(int consecutive_empty_reads) {
  // If we've had several consecutive reads with no new messages,
  // assume children are done or buffer is full
  return consecutive_empty_reads < 50;  // Adjust this threshold as needed
}

int main() {
  char *buffer = malloc(PGSIZE);
  if (buffer == 0) {
    printf("Failed to allocate memory\n");
    exit(1);
  }

  memset(buffer, 0, PGSIZE);
  int mypid = getpid();

  printf("=== Testing Fairness with %d Children ===\n", MAX_CHILDREN);
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
  
  // Start reading messages immediately while children are writing
  printf("Parent starting to read messages while children write\n");
  printf("Using staggered delays and different start positions for fairness\n\n");
  
  char *read_ptr = buffer;  // Track where we've read up to
  int child_counts[32] = {0}; // Support up to 32 children
  int total_messages = 0;
  int max_child_seen = 0;
  int consecutive_empty_reads = 0;
  
  // Read messages concurrently while children are writing
  while (should_continue_reading(consecutive_empty_reads)) {
    // Read any new messages
    int new_messages = read_new_messages(buffer, &read_ptr, child_counts, &total_messages, &max_child_seen);
    
    if (new_messages == 0) {
      consecutive_empty_reads++;
      // Small delay to avoid busy waiting when no new messages
      for (volatile int i = 0; i < 100000; i++);
    } else {
      consecutive_empty_reads = 0;  // Reset counter when we find messages
    }
  }
  
  // Wait for all children to finish
  printf("\nNo more messages detected. Waiting for all children to finish...\n");
  for (int i = 0; i < MAX_CHILDREN; i++) {
    wait(0);
  }
  
  // Read any remaining messages after all children are done
  printf("All children finished. Reading any remaining messages...\n");
  read_new_messages(buffer, &read_ptr, child_counts, &total_messages, &max_child_seen);
  
  // Enhanced summary with fairness analysis
  printf("\n=== SUMMARY ===\n");
  printf("Total messages: %d\n", total_messages);
  
  // Check if all children wrote messages
  int children_with_messages = 0;
  int min_messages = total_messages;
  int max_messages = 0;
  
  for (int i = 0; i <= max_child_seen; i++) {
    printf("Child %d wrote: %d messages\n", i, child_counts[i]);
    if (child_counts[i] > 0) {
      children_with_messages++;
      if (child_counts[i] < min_messages) min_messages = child_counts[i];
      if (child_counts[i] > max_messages) max_messages = child_counts[i];
    }
  }
  
  // Check for missing children
  printf("\n=== FAIRNESS CHECK ===\n");
  printf("Expected children: %d\n", MAX_CHILDREN);
  printf("Children that wrote messages: %d\n", children_with_messages);
  
  if (children_with_messages < MAX_CHILDREN) {
    printf("WARNING: %d children wrote NO messages! (Child indices %d", 
           MAX_CHILDREN - children_with_messages, max_child_seen + 1);
    for (int i = max_child_seen + 2; i < MAX_CHILDREN; i++) {
      printf(", %d", i);
    }
    printf(")\n");
    printf("RESULT: Poor fairness - some children completely starved\n");
  } else {
    printf("SUCCESS: All children wrote at least one message!\n");
    printf("Message range: %d to %d\n", min_messages, max_messages);
    printf("Difference: %d messages\n", max_messages - min_messages);
    
    if (max_messages > 0) {
      int fairness_ratio = (min_messages * 100) / max_messages;
      printf("Fairness ratio: %d%%\n", fairness_ratio);
      
      if (fairness_ratio >= 70) {
        printf("RESULT: Good fairness achieved!\n");
      } else if (fairness_ratio >= 50) {
        printf("RESULT: Moderate fairness\n");
      } else {
        printf("RESULT: Room for improvement in fairness\n");
      }
    }
  }
  
  exit(0);
}