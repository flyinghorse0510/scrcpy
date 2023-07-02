#include "input_manager.h"

#include <assert.h>
#include <SDL2/SDL_keycode.h>

#include "input_events.h"
#include "screen.h"
#include "util/log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#define INJECT_SOCKET_PATH "/tmp/remote_inject.sock"
#define MAXI_SOCKET_CONN 5
#define BUFFER_SIZE 255
#define MAX_RETRY_TIMES_ON_FAILURE 3

static int serverSocketFd;
static int cmdSocketFd;
static struct sockaddr_un socketAddr;
static char readBuffer[BUFFER_SIZE];

struct RemoteInputCmd
{
    Uint32 eventType;
    Uint32 buttonState;
    Sint32 x;
    Sint32 y;
};

typedef struct RemoteInputCmd RemoteInputCmd;

static int start_server()
{
    serverSocketFd = socket(AF_UNIX, SOCK_STREAM, 0);
    // Create serever socket failed
    if (serverSocketFd == -1)
    {
        printf("Create serever socket failed\n");
        return -1;
    }

    /*
     * In case the program exited inadvertently on the last run,
     * remove the socket.
     */
    remove(INJECT_SOCKET_PATH);

    // socket path to long
    if (strlen(INJECT_SOCKET_PATH) > sizeof(socketAddr.sun_path) - 1)
    {
        printf("Server socket path too long: %s\n", INJECT_SOCKET_PATH);
    }

    memset(&socketAddr, 0, sizeof(struct sockaddr_un));
    socketAddr.sun_family = AF_UNIX;
    strncpy(socketAddr.sun_path, INJECT_SOCKET_PATH, sizeof(socketAddr.sun_path) - 1);

    // bind socket
    int ret = bind(serverSocketFd, (const struct sockaddr *)&socketAddr,
                   sizeof(struct sockaddr_un));
    if (ret == -1)
    {
        printf("Bind server socket failed\n");
        return -1;
    }

    ret = listen(serverSocketFd, MAXI_SOCKET_CONN);
    if (ret == -1)
    {
        printf("listen on server socket failed\n");
        return -1;
    }

    return 0;
}

static int accept_connection()
{
    // Blocking, accept new connection
    printf("Waiting for new connection...\n");
    cmdSocketFd = accept(serverSocketFd, NULL, NULL);
    if (cmdSocketFd == -1)
    {
        printf("Accept new connection failed\n");
        return -1;
    }

    return 0;
}

extern void *remote_inject_thread(void *data)
{
    // start server socket and listening
    int ret = start_server();
    if (ret != 0)
    {
        printf("Start remote inject failed!\n");
        return NULL;
    }
    
    for (;;)
    {
        // wait for connection and accept it(blocking)
        ret = accept_connection();
        if (ret != 0)
        {
            printf("Accept new connection failed!\n");
            return NULL;
        }

        printf("Connection accepted, start receiving remote input...\n");

        for (;;)
        {
            int bytesCount = sizeof(RemoteInputCmd);
            while (bytesCount)
            {
                ssize_t readBytes = read(cmdSocketFd, (void *)(&readBuffer[sizeof(RemoteInputCmd) - bytesCount]), bytesCount);
                if (readBytes < 0)
                {
                    perror("Read command error!\n");
                    close(cmdSocketFd);
                    printf("Session terminated!\n");
                    printf("Thread terminated!\n");
                    return NULL;
                }
                bytesCount -= readBytes;
            }
            // printf("New command received!\n");

            // Extract remote inject command
            RemoteInputCmd *injectedCmd = (RemoteInputCmd *)(readBuffer);
            if (injectedCmd->eventType == UINT32_MAX)
            {
                close(cmdSocketFd);
                printf("Session terminated!\n");
                break;
            }

            SDL_Event event = {};
            event.type = injectedCmd->eventType;
            switch (injectedCmd->eventType)
            {
            case SDL_MOUSEBUTTONDOWN:
            case SDL_MOUSEBUTTONUP:
                event.button.button = SDL_BUTTON_LEFT;
                event.button.type = injectedCmd->eventType;
                event.button.clicks = 1;
                event.button.x = injectedCmd->x;
                event.button.y = injectedCmd->y;
                event.button.which = SDL_POSITION_DIRECT;
                if (SDL_PushEvent(&event) != 1)
                {
                    printf("Remote injection failed!\n");
                }
                break;
            case SDL_MOUSEMOTION:
                event.motion.type = injectedCmd->eventType;
                event.motion.state = injectedCmd->buttonState;
                event.motion.which = SDL_POSITION_DIRECT;
                event.motion.x = injectedCmd->x;
                event.motion.y = injectedCmd->y;
                if (SDL_PushEvent(&event) != 1)
                {
                    printf("Remote injection failed!\n");
                }
                break;
            default:
                printf("Invalid injection command!\n");
                break;
            }
        }
    }
}