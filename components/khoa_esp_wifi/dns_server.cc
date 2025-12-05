#include "dns_server.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <lwip/sockets.h>
#include <lwip/netdb.h>

#define TAG "DnsServer"

DnsServer::DnsServer() : fd_(-1), task_handle_(nullptr), running_(false) {
}

DnsServer::~DnsServer() {
    Stop();
}

void DnsServer::Start(esp_ip4_addr_t gateway) {
    if (running_) {
        ESP_LOGW(TAG, "DNS server already running");
        return;
    }
    
    ESP_LOGI(TAG, "Starting DNS server");
    gateway_ = gateway;
    running_ = true;

    fd_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (fd_ < 0) {
        ESP_LOGE(TAG, "Failed to create socket");
        running_ = false;
        return;
    }

    // Set socket timeout for graceful shutdown
    struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
    setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    struct sockaddr_in server_addr = {};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(port_);

    if (bind(fd_, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        ESP_LOGE(TAG, "Failed to bind port %d", port_);
        close(fd_);
        fd_ = -1;
        running_ = false;
        return;
    }

    xTaskCreate([](void* arg) {
        static_cast<DnsServer*>(arg)->Run();
        vTaskDelete(nullptr);
    }, "dns_server", 3072, this, 4, &task_handle_);
}

void DnsServer::Stop() {
    if (!running_) return;
    
    ESP_LOGI(TAG, "Stopping DNS server");
    running_ = false;
    
    if (fd_ >= 0) {
        close(fd_);
        fd_ = -1;
    }
    
    // Wait for task to finish
    if (task_handle_) {
        vTaskDelay(pdMS_TO_TICKS(100));
        task_handle_ = nullptr;
    }
}

void DnsServer::Run() {
    uint8_t buffer[256];
    struct sockaddr_in client_addr;
    
    while (running_) {
        socklen_t client_addr_len = sizeof(client_addr);
        int len = recvfrom(fd_, buffer, sizeof(buffer), 0, 
                          (struct sockaddr *)&client_addr, &client_addr_len);
        
        if (len < 12) continue;  // Minimum DNS header size

        // Build DNS response
        buffer[2] |= 0x80;  // Set response flag
        buffer[3] |= 0x80;  // Set Recursion Available
        buffer[7] = 1;      // Set answer count to 1

        // Add answer section (pointer + type A + class IN + TTL + IP)
        uint8_t *p = buffer + len;
        *p++ = 0xc0; *p++ = 0x0c;  // Name pointer to query
        *p++ = 0x00; *p++ = 0x01;  // Type A
        *p++ = 0x00; *p++ = 0x01;  // Class IN
        *p++ = 0x00; *p++ = 0x00; *p++ = 0x00; *p++ = 0x3c;  // TTL 60s
        *p++ = 0x00; *p++ = 0x04;  // Data length
        memcpy(p, &gateway_.addr, 4);
        p += 4;

        sendto(fd_, buffer, p - buffer, 0, 
               (struct sockaddr *)&client_addr, client_addr_len);
    }
}
