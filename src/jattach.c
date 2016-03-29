/*
 * Copyright 2016 Andrei Pangin
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

// The utility to send commands to remote JVM via Dynamic Attach mechanism.
// 
// This is the lightweight native version of HotSpot Attach API
// https://docs.oracle.com/javase/8/docs/jdk/api/attach/spec/
//
// Supported commands:
//   - load            : load agent library
//   - properties      : print system properties
//   - agentProperties : print agent properties
//   - datadump        : heap histogram
//   - threaddump      : dump all stack traces (like jstack)
//   - dumpheap        : dump heap (like jmap)
//   - inspectheap     : heap histogram (like jmap -histo)
//   - setflag         : modify manageable VM flag
//   - printflag       : print VM flag
//   - jcmd            : execute jcmd command

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>

// Check if remote JVM has already opened socket for Dynamic Attach
static int check_socket(int pid) {
    char path[128];
    sprintf(path, "/tmp/.java_pid%d", pid);
    
    struct stat stats;
    return stat(path, &stats) == 0 && S_ISSOCK(stats.st_mode);
}

// Force remove JVM to start Attach listener.
// HotSpot will start Attach listener in response to SIGQUIT if it sees .attach_pid file
static int start_attach_mechanism(int pid) {
    char path[128];
    sprintf(path, "/proc/%d/cwd/.attach_pid%d", pid, pid);
    
    int fd = creat(path, 0660);
    if (fd == -1) {
        sprintf(path, "/tmp/.attach_pid%d", pid);
        fd = creat(path, 0660);
        if (fd == -1) {
            return 0;
        }
    }
    close(fd);
    
    kill(pid, SIGQUIT);
    
    int result;
    int retry = 0;
    do {
        sleep(1);
        result = check_socket(pid);
    } while (!result && ++retry < 10);
    
    unlink(path);
    return result;
}

// Connect to UNIX domain socket created by JVM for Dynamic Attach
static int connect_socket(int pid) {
    int fd = socket(PF_UNIX, SOCK_STREAM, 0);
    if (fd == -1) {
        return -1;
    }
    
    struct sockaddr_un addr;
    addr.sun_family = AF_UNIX;
    sprintf(addr.sun_path, "/tmp/.java_pid%d", pid);

    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        close(fd);
        return -1;
    }
    return fd;
}

// Send command with arguments to socket
static void write_command(int fd, int argc, char** argv) {
    // Protocol version
    write(fd, "1", 2);

    int i;
    for (i = 0; i < 4; i++) {
        const char* arg = i < argc ? argv[i] : "";
        write(fd, arg, strlen(arg) + 1);
    }
}

// Mirror response from remote JVM to stdout
static void read_response(int fd) {
    char buf[1024];
    ssize_t bytes;
    while ((bytes = read(fd, buf, sizeof(buf))) > 0) {
        write(1, buf, bytes);
    }
}

int main(int argc, char** argv) {
    if (argc < 3) {
        printf("Usage: jattach <pid> <cmd> <args> ...\n");
        return 1;
    }
    
    int pid = atoi(argv[1]);
    if (!check_socket(pid) && !start_attach_mechanism(pid)) {
        printf("Could not start attach mechanism\n");
        return 1;
    }

    int fd = connect_socket(pid);
    if (fd == -1) {
        printf("Could not connect to socket\n");
        return 1;
    }
    
    printf("Connected to remove JVM\n");
    write_command(fd, argc - 2, argv + 2);
    read_response(fd);
    close(fd);
    printf("\n");
    
    return 0;
}
