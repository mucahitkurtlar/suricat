#include <string.h>
#include <time.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"

#include "lwip/pbuf.h"
#include "lwip/tcp.h"

#define BUF_SIZE 2048

#define POLL_TIME_S 5

#define DUMP_BYTES(A,B)

struct HTTP_Request {
    char* method;
    char* path;
    char* body;
    char* complete_request;
};

struct HTTP_Client {
    struct tcp_pcb* pcb;
    ip_addr_t remote_addr;
    uint16_t port;
    uint8_t buffer[BUF_SIZE];
    uint32_t buffer_len;
    bool connected;
    struct HTTP_Request request;
};


static err_t http_client_close(void* arg) {
    struct HTTP_Client* client = (struct HTTP_Client*)arg;
    err_t err = ERR_OK;
    if (client->pcb != NULL) {
        tcp_arg(client->pcb, NULL);
        tcp_sent(client->pcb, NULL);
        tcp_recv(client->pcb, NULL);
        tcp_err(client->pcb, NULL);
        tcp_poll(client->pcb, NULL, 0);
        err = tcp_close(client->pcb);
        if (err != ERR_OK) {
            printf("Error closing connection: %d, aborting...\n", err);
            tcp_abort(client->pcb);
            printf("Connection aborted\n");
            err = ERR_ABRT;
        }
        client->pcb = NULL;
    }
    return err;
}

static err_t http_result(void* arg, int status) {
    struct HTTP_Client* client = (struct HTTP_Client*)arg;
    if (status == 0) {
        printf("HTTP request successful\n");
    }
    else {
        printf("HTTP request failed with status %d\n", status);
    }

    return http_client_close(arg);
}

static err_t http_client_sent(void* arg, struct tcp_pcb* tpcb, uint16_t len) {
    struct HTTP_Client* client = (struct HTTP_Client*)arg;
    printf("Sent %d bytes\n", len);

    return http_result(client, 0);
}

static err_t http_client_connected(void* arg, struct tcp_pcb* tpcb, err_t err) {
    struct HTTP_Client* client = (struct HTTP_Client*)arg;
    if (err != ERR_OK) {
        printf("Error connecting: %d\n", err);
        return http_result(client, err);
    }
    client->connected = true;
    printf("Connected to server\n");

    return ERR_OK;
}

static err_t http_client_poll(void* arg, struct tcp_pcb* tpcb) {
    printf("Polling...\n");

    return http_result(arg, -1);
}

static void http_client_error(void* arg, err_t err) {
    if (err != ERR_ABRT) {
        printf("Error: %d\n", err);
        http_result(arg, err);
    }
}

static bool http_client_open(void* arg) {
    struct HTTP_Client* client = (struct HTTP_Client*)arg;
    printf("Opening connection to %s port %u...\n", ipaddr_ntoa(&client->pcb->remote_ip), client->pcb->remote_port);
    client->pcb = tcp_new_ip_type(IP_GET_TYPE(&client->pcb->remote_ip));
    if (!client->pcb) {
        printf("Error creating PCB\n");
        
        return false;
    }
    tcp_arg(client->pcb, client);
    tcp_sent(client->pcb, http_client_sent);
    tcp_recv(client->pcb, NULL);
    tcp_err(client->pcb, http_client_error);
    tcp_poll(client->pcb, NULL, 0);

    client->buffer_len = 0;

    cyw43_arch_lwip_begin();
    err_t err = tcp_connect(client->pcb, &client->remote_addr, client->port, http_client_connected);
    tcp_write(client->pcb, client->request.complete_request, strlen(client->request.complete_request), TCP_WRITE_FLAG_COPY);
    cyw43_arch_lwip_end();
    
    if (err != ERR_OK) {
        printf("Error connecting: %d\n", err);
        return false;
    }
    return true;
}

static struct HTTP_Client* http_client_init(const char* host, uint16_t port) {
    struct HTTP_Client* client = (struct HTTP_Client*)malloc(sizeof(struct HTTP_Client));
    if (!client) {
        printf("Error allocating memory for client\n");
        return NULL;
    }

    return client;
}

err_t http_client_request(const char* ip_addr, uint16_t port, struct HTTP_Request* request) {
    struct HTTP_Client* client = http_client_init(ip_addr, port);
    if (!client) {
        return ERR_MEM;
    }
    client->request = *request;
    client->port = port;
    ipaddr_aton(ip_addr, &client->remote_addr);
    if (!http_client_open(client)) {
        http_result(client, ERR_CONN);

        return ERR_CONN;
    }

    free(client);

    return ERR_OK;
}

struct HTTP_Request* http_client_create_request(const char* method, const char* path, const char* body) {
    struct HTTP_Request* request = (struct HTTP_Request*)malloc(sizeof(struct HTTP_Request));
    if (!request) {
        printf("Error allocating memory for request\n");
        return NULL;
    }
    request->method = (char*)malloc(strlen(method) + 1);
    if (!request->method) {
        printf("Error allocating memory for request method\n");
        return NULL;
    }
    strcpy(request->method, method);

    request->path = (char*)malloc(strlen(path) + 1);
    if (!request->path) {
        printf("Error allocating memory for request path\n");
        return NULL;
    }
    strcpy(request->path, path);

    request->body = (char*)malloc(strlen(body) + 1);
    if (!request->body) {
        printf("Error allocating memory for request body\n");
        return NULL;
    }
    strcpy(request->body, body);


    request->complete_request = (char*)malloc(sprintf(NULL, "%s %s HTTP/1.1\r\nContent-Length: %d\r\n\r\n%s", request->method, request->path, strlen(request->body), request->body) + 1);
    if (!request->complete_request) {
        printf("Error allocating memory for complete request\n");
        return NULL;
    }
    sprintf(request->complete_request, "%s %s HTTP/1.1\r\nContent-Length: %d\r\n\r\n%s", request->method, request->path, strlen(request->body), request->body);

    return request;
}