#include "ipc.h"

// Utility function to get current time in microseconds
double get_time_us() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)(tv.tv_sec) * 1000000.0 + (double)(tv.tv_usec);
}

double get_memory_usage_mb() {
    struct rusage usage;
    if (getrusage(RUSAGE_SELF, &usage) != 0) {
        perror("getrusage");
        return 0.0;
    }
    // ru_maxrss is in kilobytes on Linux
    return usage.ru_maxrss / 1024.0;
}

double get_cpu_time_ms() {
    struct rusage usage;
    if (getrusage(RUSAGE_SELF, &usage) != 0) {
        perror("getrusage");
        return 0.0;
    }
    double user_time = usage.ru_utime.tv_sec * 1000.0 + usage.ru_utime.tv_usec / 1000.0;
    double sys_time = usage.ru_stime.tv_sec * 1000.0 + usage.ru_stime.tv_usec / 1000.0;
    return user_time + sys_time;
}

void get_context_switches(long* vol_cs, long* invol_cs) {
    struct rusage usage;
    if (getrusage(RUSAGE_SELF, &usage) != 0) {
        perror("getrusage");
        *vol_cs = 0;
        *invol_cs = 0;
        return;
    }
    *vol_cs = usage.ru_nvcsw;
    *invol_cs = usage.ru_nivcsw;
}

void run_mmap() {
    printf("Running mmap IPC...\n");

    double memory_before = get_memory_usage_mb();
    double cpu_before = get_cpu_time_ms();
    long vol_cs_before, invol_cs_before;
    get_context_switches(&vol_cs_before, &invol_cs_before);

    const pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return;
    }

    if (pid == 0) {
        // New process (reader)
        printf("[NEW] New process started\n");

        printf("[NEW] Opening shared memory for reading...\n");
        const int fd = shm_open("/ipc_mmap_demo", O_RDONLY, 0666);
        if (fd == -1) {
            perror("shm_open (child)");
            exit(1);
        }
        printf("[NEW] Shared memory opened for reading\n");

        char* ptr = mmap(NULL, BUFFER_SIZE, PROT_READ, MAP_SHARED, fd, 0);
        if (ptr == MAP_FAILED) {
            perror("mmap (child)");
            close(fd);
            exit(1);
        }
        printf("[NEW] Shared memory mapping successful\n");

        // Allocate local buffer to read data
        char* local_buffer = malloc(BUFFER_SIZE);
        if (!local_buffer) {
            perror("malloc (child)");
            munmap(ptr, BUFFER_SIZE);
            close(fd);
            exit(1);
        }

        printf("[NEW] Starting to read from memory...\n");
        const long start = get_time_us();
        memcpy(local_buffer, ptr, BUFFER_SIZE); // Read the entire memory
        const long end = get_time_us();
        const double latency_ms = (end - start) / 1000.0;
        const double throughput_mb_s = (BUFFER_SIZE / (1024.0 * 1024.0)) / ((end - start) / 1000000.0);
        printf("[NEW] Reading completed, read latency: %.3f ms, throughput: %.3f MB/s\n", latency_ms, throughput_mb_s);

        free(local_buffer);
        munmap(ptr, BUFFER_SIZE);
        close(fd);

        double memory_after = get_memory_usage_mb();
        double cpu_after = get_cpu_time_ms();
        long vol_cs_after, invol_cs_after;
        get_context_switches(&vol_cs_after, &invol_cs_after);

        double memory_overhead = memory_after - memory_before;
        double cpu_utilization = cpu_after - cpu_before;
        long vol_cs_overhead = vol_cs_after - vol_cs_before;
        long invol_cs_overhead = invol_cs_after - invol_cs_before;

        printf("[NEW] Memory overhead: %.3f MB\n", memory_overhead);
        printf("[NEW] CPU utilization: %.3f ms\n", cpu_utilization);
        printf("[NEW] Voluntary context switches: %ld, involuntary context switches: %ld\n",
               vol_cs_overhead, invol_cs_overhead);

        printf("[NEW] New process finished\n");
        exit(0);
    } else {
        // Original process (writer)
        printf("[ORIGINAL] Original process started\n");

        printf("[ORIGINAL] Creating and opening shared memory for writing...\n");
        const int fd = shm_open("/ipc_mmap_demo", O_CREAT | O_RDWR, 0666);
        if (fd == -1) {
            perror("shm_open (parent)");
            return;
        }
        printf("[ORIGINAL] Shared memory opened for writing\n");

        if (ftruncate(fd, BUFFER_SIZE) == -1) {
            perror("ftruncate");
            close(fd);
            shm_unlink("/ipc_mmap_demo");
            return;
        }
        printf("[ORIGINAL] Shared memory size set\n");

        char* ptr = mmap(NULL, BUFFER_SIZE, PROT_WRITE, MAP_SHARED, fd, 0);
        if (ptr == MAP_FAILED) {
            perror("mmap (parent)");
            close(fd);
            shm_unlink("/ipc_mmap_demo");
            return;
        }
        printf("[ORIGINAL] Shared memory mapping successful\n");

        printf("[ORIGINAL] Starting to write to memory...\n");
        const long start = get_time_us();
        memset(ptr, 'A', BUFFER_SIZE);
        const long end = get_time_us();
        const double latency_ms = (end - start) / 1000.0;
        const double throughput_mb_s =
            (BUFFER_SIZE / (1024.0 * 1024.0)) / ((end - start) / 1000000.0);
        printf("[ORIGINAL] Writing completed, write latency: %.3f ms, throughput: %.3f MB/s\n",
               latency_ms, throughput_mb_s);

        munmap(ptr, BUFFER_SIZE);
        close(fd);

        double memory_after = get_memory_usage_mb();
        double cpu_after = get_cpu_time_ms();
        long vol_cs_after, invol_cs_after;
        get_context_switches(&vol_cs_after, &invol_cs_after);

        double memory_overhead = memory_after - memory_before;
        double cpu_utilization = cpu_after - cpu_before;
        long vol_cs_overhead = vol_cs_after - vol_cs_before;
        long invol_cs_overhead = invol_cs_after - invol_cs_before;

        printf("[ORIGINAL] Memory overhead: %.3f MB\n", memory_overhead);
        printf("[ORIGINAL] CPU utilization: %.3f ms\n", cpu_utilization);
        printf("[ORIGINAL] Voluntary context switches: %ld, involuntary context switches: %ld\n",
               vol_cs_overhead, invol_cs_overhead);

        wait(NULL);
        shm_unlink("/ipc_mmap_demo");
        printf("[ORIGINAL] Original process finished\n");
    }
}

void run_shared_memory() {
    printf("Running shared memory IPC...\n");

    double memory_before = get_memory_usage_mb();
    double cpu_before = get_cpu_time_ms();
    long vol_cs_before, invol_cs_before;
    get_context_switches(&vol_cs_before, &invol_cs_before);

    const pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return;
    }

    if (pid == 0) {
        // New process (reader)
        printf("[NEW] New process started\n");

        printf("[NEW] Opening shared memory for reading...\n");
        const int fd = shm_open("/ipc_shared_memory_demo", O_RDONLY, 0666);
        if (fd == -1) {
            perror("shm_open (child)");
            exit(1);
        }
        printf("[NEW] Shared memory opened for reading\n");

        char* ptr = mmap(NULL, BUFFER_SIZE, PROT_READ, MAP_SHARED, fd, 0);
        if (ptr == MAP_FAILED) {
            perror("mmap (child)");
            close(fd);
            exit(1);
        }
        printf("[NEW] Shared memory mapping successful\n");

        // Synchronization using semaphore
        printf("[NEW] Opening semaphore for synchronization...\n");
        sem_t* sem = sem_open("/ipc_sem", 0);
        if (sem == SEM_FAILED) {
            perror("sem_open (child)");
            munmap(ptr, BUFFER_SIZE);
            close(fd);
            exit(1);
        }
        printf("[NEW] Semaphore opened\n");

        printf("[NEW] Waiting for signal from original process...\n");
        const long sync_start = get_time_us();
        sem_wait(sem); // Wait for signal from writer
        const long sync_end = get_time_us();
        const double sync_overhead_ms = (sync_end - sync_start) / 1000.0;
        printf("[NEW] Synchronization overhead: %.3f ms\n", sync_overhead_ms);

        printf("[NEW] Signal received, starting to read from memory...\n");

        // Allocate local buffer to read data
        char* local_buffer = malloc(BUFFER_SIZE);
        if (!local_buffer) {
            perror("malloc (child)");
            munmap(ptr, BUFFER_SIZE);
            close(fd);
            sem_close(sem);
            exit(1);
        }

        const long start = get_time_us();
        memcpy(local_buffer, ptr, BUFFER_SIZE); // Read the entire memory
        const long end = get_time_us();
        const double latency_ms = (end - start) / 1000.0;
        const double throughput_mb_s =
            (BUFFER_SIZE / (1024.0 * 1024.0)) / ((end - start) / 1000000.0);
        printf("[NEW] Reading completed, read latency: %.3f ms, throughput: %.3f MB/s\n",
               latency_ms, throughput_mb_s);

        free(local_buffer);
        munmap(ptr, BUFFER_SIZE);
        close(fd);
        sem_close(sem);

        double memory_after = get_memory_usage_mb();
        double cpu_after = get_cpu_time_ms();
        long vol_cs_after, invol_cs_after;
        get_context_switches(&vol_cs_after, &invol_cs_after);

        double memory_overhead = memory_after - memory_before;
        double cpu_utilization = cpu_after - cpu_before;
        long vol_cs_overhead = vol_cs_after - vol_cs_before;
        long invol_cs_overhead = invol_cs_after - invol_cs_before;

        printf("[NEW] Memory overhead: %.3f MB\n", memory_overhead);
        printf("[NEW] CPU utilization: %.3f ms\n", cpu_utilization);
        printf("[NEW] Voluntary context switches: %ld, involuntary context switches: %ld\n",
               vol_cs_overhead, invol_cs_overhead);

        printf("[NEW] New process finished\n");
        exit(0);
    } else {
        // Original process (writer)
        printf("[ORIGINAL] Original process started\n");

        printf("[ORIGINAL] Creating and opening shared memory for writing...\n");
        const int fd = shm_open("/ipc_shared_memory_demo", O_CREAT | O_RDWR, 0666);
        if (fd == -1) {
            perror("shm_open (parent)");
            return;
        }
        printf("[ORIGINAL] Shared memory opened for writing\n");

        if (ftruncate(fd, BUFFER_SIZE) == -1) {
            perror("ftruncate");
            close(fd);
            shm_unlink("/ipc_shared_memory_demo");
            return;
        }
        printf("[ORIGINAL] Shared memory size set\n");

        char* ptr = mmap(NULL, BUFFER_SIZE, PROT_WRITE, MAP_SHARED, fd, 0);
        if (ptr == MAP_FAILED) {
            perror("mmap (parent)");
            close(fd);
            shm_unlink("/ipc_shared_memory_demo");
            return;
        }
        printf("[ORIGINAL] Shared memory mapping successful\n");

        // Creating semaphore for synchronization
        printf("[ORIGINAL] Creating semaphore for synchronization...\n");
        sem_t* sem = sem_open("/ipc_sem", O_CREAT, 0666, 0);
        if (sem == SEM_FAILED) {
            perror("sem_open (parent)");
            munmap(ptr, BUFFER_SIZE);
            close(fd);
            shm_unlink("/ipc_shared_memory_demo");
            return;
        }
        printf("[ORIGINAL] Semaphore created\n");

        printf("[ORIGINAL] Starting to write to memory...\n");
        const long start = get_time_us();
        memset(ptr, 'B', BUFFER_SIZE);
        const long end = get_time_us();
        const double latency_ms = (end - start) / 1000.0;
        const double throughput_mb_s =
            (BUFFER_SIZE / (1024.0 * 1024.0)) / ((end - start) / 1000000.0);
        printf("[ORIGINAL] Writing completed, write latency: %.3f ms, throughput: %.3f MB/s\n",
               latency_ms, throughput_mb_s);

        printf("[ORIGINAL] Signaling new process about completion of writing...\n");
        const long sync_start = get_time_us();
        sem_post(sem);
        const long sync_end = get_time_us();
        const double sync_overhead_ms = (sync_end - sync_start) / 1000.0;
        printf("[ORIGINAL] Synchronization overhead: %.3f ms\n", sync_overhead_ms);

        munmap(ptr, BUFFER_SIZE);
        close(fd);
        sem_close(sem);
        sem_unlink("/ipc_sem");

        double memory_after = get_memory_usage_mb();
        double cpu_after = get_cpu_time_ms();
        long vol_cs_after, invol_cs_after;
        get_context_switches(&vol_cs_after, &invol_cs_after);

        double memory_overhead = memory_after - memory_before;
        double cpu_utilization = cpu_after - cpu_before;
        long vol_cs_overhead = vol_cs_after - vol_cs_before;
        long invol_cs_overhead = invol_cs_after - invol_cs_before;

        printf("[ORIGINAL] Memory overhead: %.3f MB\n", memory_overhead);
        printf("[ORIGINAL] CPU utilization: %.3f ms\n", cpu_utilization);
        printf("[ORIGINAL] Voluntary context switches: %ld, involuntary context switches: %ld\n",
               vol_cs_overhead, invol_cs_overhead);

        wait(NULL);
        shm_unlink("/ipc_shared_memory_demo");
        printf("[ORIGINAL] Original process finished\n");
    }
}

void run_file_io() {
    printf("Running file I/O IPC...\n");

    double memory_before = get_memory_usage_mb();
    double cpu_before = get_cpu_time_ms();
    long vol_cs_before, invol_cs_before;
    get_context_switches(&vol_cs_before, &invol_cs_before);

    const pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return;
    }

    const char* filename = "ipc_file_io_demo.dat";

    if (pid == 0) {
        // New process (reader)
        printf("[NEW] New process started\n");
        sleep(1); // Ensure writer has written data

        printf("[NEW] Opening file for reading...\n");
        const int fd = open(filename, O_RDONLY);
        if (fd == -1) {
            perror("open (child)");
            exit(1);
        }
        printf("[NEW] File opened for reading\n");

        char* buffer = malloc(BUFFER_SIZE);
        if (!buffer) {
            perror("malloc (child)");
            close(fd);
            exit(1);
        }

        printf("[NEW] Starting to read from file...\n");
        const long io_start = get_time_us();
        const ssize_t bytes = read(fd, buffer, BUFFER_SIZE);
        const long io_end = get_time_us();
        if (bytes == -1) {
            perror("read (child)");
            free(buffer);
            close(fd);
            exit(1);
        }
        const double io_time_ms = (io_end - io_start) / 1000.0;
        const double throughput_mb_s = (bytes / (1024.0 * 1024.0)) / ((io_end - io_start) / 1000000.0);
        printf("[NEW] Reading completed, read %ld bytes, I/O time: %.3f ms, throughput: %.3f MB/s\n",
               bytes, io_time_ms, throughput_mb_s);

        free(buffer);
        close(fd);
        unlink(filename);

        double memory_after = get_memory_usage_mb();
        double cpu_after = get_cpu_time_ms();
        long vol_cs_after, invol_cs_after;
        get_context_switches(&vol_cs_after, &invol_cs_after);

        double memory_overhead = memory_after - memory_before;
        double cpu_utilization = cpu_after - cpu_before;
        long vol_cs_overhead = vol_cs_after - vol_cs_before;
        long invol_cs_overhead = invol_cs_after - invol_cs_before;

        printf("[NEW] Memory overhead: %.3f MB\n", memory_overhead);
        printf("[NEW] CPU utilization: %.3f ms\n", cpu_utilization);
        printf("[NEW] Voluntary context switches: %ld, involuntary context switches: %ld\n",
               vol_cs_overhead, invol_cs_overhead);

        printf("[NEW] New process finished\n");
        exit(0);
    } else {
        // Original process (writer)
        printf("[ORIGINAL] Original process started\n");

        printf("[ORIGINAL] Opening file for writing...\n");
        const int fd = open(filename, O_CREAT | O_WRONLY | O_TRUNC, 0666);
        if (fd == -1) {
            perror("open (parent)");
            return;
        }
        printf("[ORIGINAL] File opened for writing\n");

        char* buffer = malloc(BUFFER_SIZE);
        if (!buffer) {
            perror("malloc (parent)");
            close(fd);
            exit(1);
        }
        memset(buffer, 'C', BUFFER_SIZE);

        printf("[ORIGINAL] Starting to write to file...\n");
        const long io_start = get_time_us();
        const ssize_t bytes = write(fd, buffer, BUFFER_SIZE);
        fsync(fd);
        const long io_end = get_time_us();
        if (bytes == -1) {
            perror("write (parent)");
            free(buffer);
            close(fd);
            exit(1);
        }
        const double io_time_ms = (io_end - io_start) / 1000.0;
        const double throughput_mb_s = (bytes / (1024.0 * 1024.0)) / ((io_end - io_start) / 1000000.0);
        printf("[ORIGINAL] Writing completed, wrote %ld bytes, I/O time: %.3f ms, throughput: %.3f MB/s\n",
               bytes, io_time_ms, throughput_mb_s);

        free(buffer);
        close(fd);
        wait(NULL);

        double memory_after = get_memory_usage_mb();
        double cpu_after = get_cpu_time_ms();
        long vol_cs_after, invol_cs_after;
        get_context_switches(&vol_cs_after, &invol_cs_before);

        double memory_overhead = memory_after - memory_before;
        double cpu_utilization = cpu_after - cpu_before;
        long vol_cs_overhead = vol_cs_after - vol_cs_before;
        long invol_cs_overhead = invol_cs_after - invol_cs_before;

        printf("[ORIGINAL] Memory overhead: %.3f MB\n", memory_overhead);
        printf("[ORIGINAL] CPU utilization: %.3f ms\n", cpu_utilization);
        printf("[ORIGINAL] Voluntary context switches: %ld, involuntary context switches: %ld\n",
               vol_cs_overhead, invol_cs_overhead);

        printf("[ORIGINAL] Original process finished\n");
    }
}

void run_fifo() {
    printf("Running FIFO IPC...\n");

    const int CHUNK_SIZE = 65536;
    const char* fifo_name = "ipc_fifo_demo";

    double memory_before = get_memory_usage_mb();
    double cpu_before = get_cpu_time_ms();
    long vol_cs_before, invol_cs_before;
    get_context_switches(&vol_cs_before, &invol_cs_before);

    // Creating FIFO
    if (mkfifo(fifo_name, 0666) == -1 && errno != EEXIST) {
        perror("mkfifo");
        return;
    }

    const pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        unlink(fifo_name);
        return;
    }

    if (pid == 0) {
        // New process (reader)
        printf("[NEW] New process started\n");
        sleep(1); // Give original process time to open FIFO for writing

        printf("[NEW] Opening FIFO for reading...\n");
        const int fd = open(fifo_name, O_RDONLY);
        if (fd == -1) {
            perror("open (child)");
            exit(1);
        }
        printf("[NEW] FIFO opened for reading\n");

        char* buffer = malloc(BUFFER_SIZE);
        if (!buffer) {
            perror("malloc (child)");
            close(fd);
            exit(1);
        }

        ssize_t total_read = 0;
        ssize_t bytes = 0;
        int read_errors = 0;
        printf("[NEW] Starting to read from FIFO...\n");
        const long io_start = get_time_us();

        // Loop to read until all data is read
        while (total_read < BUFFER_SIZE) {
            bytes = read(fd, buffer + total_read, CHUNK_SIZE);
            if (bytes == -1) {
                perror("read (child)");
                read_errors++;
                break;
            }
            total_read += bytes;
        }

        const long io_end = get_time_us();
        const double io_time_ms = (io_end - io_start) / 1000.0;
        const double throughput_mb_s =
            (total_read / (1024.0 * 1024.0)) / ((io_end - io_start) / 1000000.0);
        printf("[NEW] FIFO read completed, read %ld bytes, I/O time: %.3f ms, throughput: %.3f MB/s\n",
               total_read, io_time_ms, throughput_mb_s);
        printf("[NEW] Read errors: %d\n", read_errors);

        free(buffer);
        close(fd);
        unlink(fifo_name);

        double memory_after = get_memory_usage_mb();
        double cpu_after = get_cpu_time_ms();
        long vol_cs_after, invol_cs_after;
        get_context_switches(&vol_cs_after, &invol_cs_after);

        double memory_overhead = memory_after - memory_before;
        double cpu_utilization = cpu_after - cpu_before;
        long vol_cs_overhead = vol_cs_after - vol_cs_before;
        long invol_cs_overhead = invol_cs_after - invol_cs_before;

        printf("[NEW] Memory overhead: %.3f MB\n", memory_overhead);
        printf("[NEW] CPU utilization: %.3f ms\n", cpu_utilization);
        printf("[NEW] Voluntary context switches: %ld, involuntary context switches: %ld\n",
               vol_cs_overhead, invol_cs_overhead);

        printf("[NEW] New process finished\n");
        exit(0);
    } else {
        // Original process (writer)
        printf("[ORIGINAL] Original process started\n");

        printf("[ORIGINAL] Opening FIFO for writing...\n");
        const int fd = open(fifo_name, O_WRONLY);
        if (fd == -1) {
            perror("open (parent)");
            unlink(fifo_name);
            return;
        }
        printf("[ORIGINAL] FIFO opened for writing\n");

        char* buffer = malloc(BUFFER_SIZE);
        if (!buffer) {
            perror("malloc (parent)");
            close(fd);
            unlink(fifo_name);
            exit(1);
        }
        memset(buffer, 'D', BUFFER_SIZE);

        ssize_t total_written = 0;
        ssize_t bytes = 0;
        int write_errors = 0;
        printf("[ORIGINAL] Starting to write to FIFO...\n");
        const long io_start = get_time_us();

        // Loop to write until all data is written
        while (total_written < BUFFER_SIZE) {
            bytes = write(fd, buffer + total_written, CHUNK_SIZE);
            if (bytes == -1) {
                perror("write (parent)");
                write_errors++;
                break;
            }
            total_written += bytes;
        }

        const long io_end = get_time_us();
        const double io_time_ms = (io_end - io_start) / 1000.0;
        const double throughput_mb_s =
            (total_written / (1024.0 * 1024.0)) / ((io_end - io_start) / 1000000.0);
        printf("[ORIGINAL] FIFO write completed, wrote %ld bytes, I/O time: %.3f ms, throughput: %.3f MB/s\n",
               total_written, io_time_ms, throughput_mb_s);
        printf("[ORIGINAL] Write errors: %d\n", write_errors);

        free(buffer);
        close(fd);
        wait(NULL); // Wait for new process to finish

        double memory_after = get_memory_usage_mb();
        double cpu_after = get_cpu_time_ms();
        long vol_cs_after, invol_cs_after;
        get_context_switches(&vol_cs_after, &invol_cs_before);

        double memory_overhead = memory_after - memory_before;
        double cpu_utilization = cpu_after - cpu_before;
        long vol_cs_overhead = vol_cs_after - vol_cs_before;
        long invol_cs_overhead = invol_cs_after - invol_cs_before;

        printf("[ORIGINAL] Memory overhead: %.3f MB\n", memory_overhead);
        printf("[ORIGINAL] CPU utilization: %.3f ms\n", cpu_utilization);
        printf("[ORIGINAL] Voluntary context switches: %ld, involuntary context switches: %ld\n",
               vol_cs_overhead, invol_cs_overhead);

        printf("[ORIGINAL] Original process finished\n");
    }
}

void run_message_queue() {
    printf("Running message queue IPC...\n");

    const int MSG_SIZE = 8192;
    const char* mq_name = "/ipc_mq_demo";
    struct mq_attr attr;
    attr.mq_flags = 0;
    attr.mq_maxmsg = 10; // Maximum number of messages in queue
    attr.mq_msgsize = MSG_SIZE; // Maximum message size is 8 KB
    attr.mq_curmsgs = 0;

    double memory_before = get_memory_usage_mb();
    double cpu_before = get_cpu_time_ms();
    long vol_cs_before = 0, invol_cs_before = 0;
    get_context_switches(&vol_cs_before, &invol_cs_before);

    const pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        mq_unlink(mq_name);
        return;
    }

    if (pid == 0) {
        // New process (receiver)
        sleep(1); // Short delay to allow sender to create queue

        const mqd_t mq = mq_open(mq_name, O_RDONLY);
        if (mq == (mqd_t)-1) {
            perror("mq_open (child)");
            exit(1);
        }

        char* buffer = malloc(BUFFER_SIZE);
        if (!buffer) {
            perror("malloc (child)");
            mq_close(mq);
            exit(1);
        }

        ssize_t total_received = 0;
        ssize_t bytes = 0;
        int receive_errors = 0;
        printf("[NEW] Starting to read from message queue...\n");
        const long start = get_time_us();

        // Loop to receive messages until the entire buffer is received
        while (total_received < BUFFER_SIZE) {
            bytes = mq_receive(mq, buffer + total_received, MSG_SIZE, NULL);
            if (bytes == -1) {
                perror("mq_receive (child)");
                receive_errors++;
                break;
            }
            total_received += bytes;
        }

        const long end = get_time_us();
        const double latency_ms = (end - start) / 1000.0;
        const double throughput_mb_s =
            (total_received / (1024.0 * 1024.0)) / ((end - start) / 1000000.0);
        printf("[NEW] Received %ld bytes, read latency: %.3f ms, throughput: %.3f MB/s\n",
               total_received, latency_ms, throughput_mb_s);
        printf("[NEW] Message receive errors: %d\n", receive_errors);

        free(buffer);
        mq_close(mq);
        mq_unlink(mq_name);

        double memory_after = get_memory_usage_mb();
        double cpu_after = get_cpu_time_ms();
        long vol_cs_after = 0, invol_cs_after = 0;
        get_context_switches(&vol_cs_after, &invol_cs_after);

        double memory_overhead = memory_after - memory_before;
        double cpu_utilization = cpu_after - cpu_before;
        long vol_cs_overhead = vol_cs_after - vol_cs_before;
        long invol_cs_overhead = invol_cs_after - invol_cs_before;

        printf("[NEW] Memory overhead: %.3f MB\n", memory_overhead);
        printf("[NEW] CPU utilization: %.3f ms\n", cpu_utilization);
        printf("[NEW] Voluntary context switches: %ld, involuntary context switches: %ld\n",
               vol_cs_overhead, invol_cs_overhead);

        printf("[NEW] New process finished\n");
        exit(0);
    } else {
        // Original process (sender)
        mqd_t mq = mq_open(mq_name, O_CREAT | O_WRONLY, 0666, &attr);
        if (mq == (mqd_t)-1) {
            perror("mq_open (parent)");
            mq_unlink(mq_name);
            return;
        }

        char* buffer = malloc(BUFFER_SIZE);
        if (!buffer) {
            perror("malloc (parent)");
            mq_close(mq);
            mq_unlink(mq_name);
            exit(1);
        }
        memset(buffer, 'E', BUFFER_SIZE);

        ssize_t total_sent = 0;
        int send_errors = 0;
        printf("[ORIGINAL] Starting to write to message queue...\n");
        const long start = get_time_us();

        // Loop to send messages until the entire buffer is sent
        while (total_sent < BUFFER_SIZE) {
            if (mq_send(mq, buffer + total_sent, MSG_SIZE, 0) == -1) {
                perror("mq_send (parent)");
                send_errors++;
                break;
            }
            total_sent += MSG_SIZE;
        }

        const long end = get_time_us();
        const double latency_ms = (end - start) / 1000.0;
        const double throughput_mb_s =
            (total_sent / (1024.0 * 1024.0)) / ((end - start) / 1000000.0);
        printf("[ORIGINAL] Sent %ld bytes, write latency: %.3f ms, throughput: %.3f MB/s\n",
               total_sent, latency_ms, throughput_mb_s);
        printf("[ORIGINAL] Message send errors: %d\n", send_errors);

        free(buffer);
        mq_close(mq);
        wait(NULL);

        double memory_after = get_memory_usage_mb();
        double cpu_after = get_cpu_time_ms();
        long vol_cs_after = 0, invol_cs_after = 0;
        get_context_switches(&vol_cs_after, &invol_cs_after);

        double memory_overhead = memory_after - memory_before;
        double cpu_utilization = cpu_after - cpu_before;
        long vol_cs_overhead = vol_cs_after - vol_cs_before;
        long invol_cs_overhead = invol_cs_after - invol_cs_before;

        printf("[ORIGINAL] Memory overhead: %.3f MB\n", memory_overhead);
        printf("[ORIGINAL] CPU utilization: %.3f ms\n", cpu_utilization);
        printf("[ORIGINAL] Voluntary context switches: %ld, involuntary context switches: %ld\n",
               vol_cs_overhead, invol_cs_overhead);

        mq_unlink(mq_name);
        printf("[ORIGINAL] Original process finished\n");
    }
}

void run_unix_socket() {
    printf("Running UNIX domain socket IPC...\n");

    const char* socket_path = "/tmp/uds_demo.sock";

    double memory_before = get_memory_usage_mb();
    double cpu_before = get_cpu_time_ms();
    long vol_cs_before = 0, invol_cs_before = 0;
    get_context_switches(&vol_cs_before, &invol_cs_before);

    const pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        unlink(socket_path);
        return;
    }

    if (pid == 0) {
        // New process (server)
        printf("[NEW] New process (server) started\n");

        int server_fd, client_fd;
        struct sockaddr_un addr;
        char* buffer = malloc(BUFFER_SIZE);
        if (!buffer) {
            perror("malloc (server)");
            exit(1);
        }
        memset(buffer, 0, BUFFER_SIZE);

        printf("[NEW] Creating socket...\n");
        if ((server_fd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
            perror("socket (server)");
            free(buffer);
            exit(1);
        }
        printf("[NEW] Socket created\n");

        memset(&addr, 0, sizeof(struct sockaddr_un));
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);
        unlink(socket_path);

        printf("[NEW] Binding socket to address...\n");
        if (bind(server_fd, (struct sockaddr*)&addr, sizeof(struct sockaddr_un)) == -1) {
            perror("bind (server)");
            close(server_fd);
            free(buffer);
            exit(1);
        }
        printf("[NEW] Socket bound to address\n");

        printf("[NEW] Listening on socket...\n");
        if (listen(server_fd, 1) == -1) {
            perror("listen (server)");
            close(server_fd);
            free(buffer);
            exit(1);
        }
        printf("[NEW] Waiting for connection...\n");

        const long conn_start = get_time_us();
        if ((client_fd = accept(server_fd, NULL, NULL)) == -1) {
            perror("accept (server)");
            close(server_fd);
            free(buffer);
            exit(1);
        }
        const long conn_end = get_time_us();
        const double conn_overhead_ms = (conn_end - conn_start) / 1000.0;
        printf("[NEW] Connection established, connection overhead: %.3f ms\n", conn_overhead_ms);

        printf("[NEW] Starting to read data...\n");
        const long start = get_time_us();
        ssize_t total_read = 0;
        ssize_t bytes = 0;
        int read_errors = 0;

        // Loop to read data until the entire buffer is received
        while (total_read < BUFFER_SIZE) {
            bytes = read(client_fd, buffer + total_read, BUFFER_SIZE - total_read);
            if (bytes == -1) {
                perror("read (server)");
                read_errors++;
                break;
            }
            total_read += bytes;
        }

        const long end = get_time_us();
        const double latency_ms = (end - start) / 1000.0;
        const double throughput_mb_s =
            (total_read / (1024.0 * 1024.0)) / ((end - start) / 1000000.0);
        printf("[NEW] Reading completed, received %ld bytes, read latency: %.3f ms, throughput: %.3f MB/s\n",
               total_read, latency_ms, throughput_mb_s);
        printf("[NEW] Read errors: %d\n", read_errors);

        close(client_fd);
        close(server_fd);
        unlink(socket_path);
        free(buffer);

        double memory_after = get_memory_usage_mb();
        double cpu_after = get_cpu_time_ms();
        long vol_cs_after = 0, invol_cs_after = 0;
        get_context_switches(&vol_cs_after, &invol_cs_after);

        double memory_overhead = memory_after - memory_before;
        double cpu_utilization = cpu_after - cpu_before;
        long vol_cs_overhead = vol_cs_after - vol_cs_before;
        long invol_cs_overhead = invol_cs_after - invol_cs_before;

        printf("[NEW] Memory overhead: %.3f MB\n", memory_overhead);
        printf("[NEW] CPU utilization: %.3f ms\n", cpu_utilization);
        printf("[NEW] Voluntary context switches: %ld, involuntary context switches: %ld\n",
               vol_cs_overhead, invol_cs_overhead);

        printf("[NEW] New process finished\n");
        exit(0);
    } else {
        // Original process (client)
        printf("[ORIGINAL] Original process (client) started\n");
        sleep(1); // Delay for server startup

        int sock_fd;
        struct sockaddr_un addr;
        char* buffer = malloc(BUFFER_SIZE);
        if (!buffer) {
            perror("malloc (client)");
            exit(1);
        }
        memset(buffer, 'F', BUFFER_SIZE);

        printf("[ORIGINAL] Creating socket...\n");
        if ((sock_fd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
            perror("socket (client)");
            free(buffer);
            exit(1);
        }
        printf("[ORIGINAL] Socket created\n");

        memset(&addr, 0, sizeof(struct sockaddr_un));
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);

        printf("[ORIGINAL] Connecting to server...\n");
        const long conn_start = get_time_us();
        if (connect(sock_fd, (struct sockaddr*)&addr, sizeof(struct sockaddr_un)) == -1) {
            perror("connect (client)");
            close(sock_fd);
            free(buffer);
            exit(1);
        }
        const long conn_end = get_time_us();
        const double conn_overhead_ms = (conn_end - conn_start) / 1000.0;
        printf("[ORIGINAL] Connection established, connection overhead: %.3f ms\n", conn_overhead_ms);

        printf("[ORIGINAL] Starting to send data...\n");
        const long start = get_time_us();
        ssize_t total_sent = 0;
        ssize_t bytes = 0;
        int write_errors = 0;

        // Loop to send data until the entire buffer is sent
        while (total_sent < BUFFER_SIZE) {
            bytes = write(sock_fd, buffer + total_sent, BUFFER_SIZE - total_sent);
            if (bytes == -1) {
                perror("write (client)");
                write_errors++;
                break;
            }
            total_sent += bytes;
        }

        const long end = get_time_us();
        const double latency_ms = (end - start) / 1000.0;
        const double throughput_mb_s =
            (total_sent / (1024.0 * 1024.0)) / ((end - start) / 1000000.0);
        printf("[ORIGINAL] Data transfer completed, sent %ld bytes, write latency: %.3f ms, throughput: %.3f MB/s\n",
               total_sent, latency_ms, throughput_mb_s);
        printf("[ORIGINAL] Write errors: %d\n", write_errors);

        close(sock_fd);
        free(buffer);
        wait(NULL);

        double memory_after = get_memory_usage_mb();
        double cpu_after = get_cpu_time_ms();
        long vol_cs_after = 0, invol_cs_after = 0;
        get_context_switches(&vol_cs_after, &invol_cs_after);

        double memory_overhead = memory_after - memory_before;
        double cpu_utilization = cpu_after - cpu_before;
        long vol_cs_overhead = vol_cs_after - vol_cs_before;
        long invol_cs_overhead = invol_cs_after - invol_cs_before;

        printf("[ORIGINAL] Memory overhead: %.3f MB\n", memory_overhead);
        printf("[ORIGINAL] CPU utilization: %.3f ms\n", cpu_utilization);
        printf("[ORIGINAL] Voluntary context switches: %ld, involuntary context switches: %ld\n",
               vol_cs_overhead, invol_cs_overhead);

        printf("[ORIGINAL] Original process finished\n");
    }
}

int main(int argc, char* argv[]) {
    if (argc < 2 || argc > 3) {
        printf("Usage: %s <ipc_method> [buffer_size_in_bytes]\n", argv[0]);
        printf("IPC Methods:\n");
        printf("   mmap\n");
        printf("   shared_memory\n");
        printf("   file_io\n");
        printf("   fifo\n");
        printf("   message_queue\n");
        printf("   unix_socket\n");
        return 1;
    }

    if (argc == 3) {
        BUFFER_SIZE = strtoull(argv[2], NULL, 10);
        if (BUFFER_SIZE == 0) {
            printf("Invalid buffer size: %s\n", argv[2]);
            return 1;
        }
    }

    if (strcmp(argv[1], "mmap") == 0) {
        run_mmap();
    } else if (strcmp(argv[1], "shared_memory") == 0) {
        run_shared_memory();
    } else if (strcmp(argv[1], "file_io") == 0) {
        run_file_io();
    } else if (strcmp(argv[1], "fifo") == 0) {
        run_fifo();
    } else if (strcmp(argv[1], "message_queue") == 0) {
        run_message_queue();
    } else if (strcmp(argv[1], "unix_socket") == 0) {
        run_unix_socket();
    } else {
        printf("Unknown IPC method: %s\n", argv[1]);
        return 1;
    }

    return 0;
}