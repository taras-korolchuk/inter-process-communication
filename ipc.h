#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <mqueue.h>
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <unistd.h>

#define DEFAULT_BUFFER_SIZE (1024 * 1024) // 1MB

size_t BUFFER_SIZE = DEFAULT_BUFFER_SIZE;

double get_time_us();
void run_mmap();
void run_shared_memory();
void run_file_io();
void run_fifo();
void run_message_queue();
void run_unix_socket();