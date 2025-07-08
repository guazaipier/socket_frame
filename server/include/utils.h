#pragma once

#define MAX_BUFFER_SIZE 1024
#define MAX_EVENTS 1024
#define MAX_CONNECT_THREADS 4 // 4 threads for handling client events
#define MAX_SESSION_THREADS 2 // minimum 2 threads for handling client IO
#define IDLE_CHECK_INTERVAL 10 // 10 seconds to check idle clients
#define IDLE_SESSION_TIMEOUT 60000 // 60 seconds to close idle sessions