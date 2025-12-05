/**
 * @file webserver.c
 * @brief Web server implementation with WebSocket support
 */

#include "webserver.h"
#include "../common/config.h"
#include "../finger/finger.h"
#include "../audio/audio.h"
#include "../app/app.h"

#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_http_server.h"
#include "cJSON.h"

/* Embedded assets */
extern const char index_html_start[] asm("_binary_index_html_start");
extern const char index_html_end[] asm("_binary_index_html_end");
extern const char style_css_start[] asm("_binary_style_css_start");
extern const char style_css_end[] asm("_binary_style_css_end");
extern const char app_js_start[] asm("_binary_app_js_start");
extern const char app_js_end[] asm("_binary_app_js_end");

static const char *TAG = "webserver";

/* ============================================================================
 * Constants
 * ============================================================================ */
#define MAX_WS_CLIENTS      4
#define WS_QUEUE_SIZE       8
#define JSON_BUFFER_SIZE    512

/* ============================================================================
 * Types
 * ============================================================================ */
typedef struct {
    httpd_handle_t hd;
    int fd;
} ws_client_t;

/* ============================================================================
 * Static Variables
 * ============================================================================ */
static httpd_handle_t s_server = NULL;
static ws_client_t s_ws_clients[MAX_WS_CLIENTS];
static int s_ws_client_count = 0;
static SemaphoreHandle_t s_ws_mutex = NULL;

/* ============================================================================
 * Forward Declarations
 * ============================================================================ */
static esp_err_t ws_handler(httpd_req_t *req);
static void handle_ws_message(httpd_req_t *req, const char *data, size_t len);
static void ws_send_json(int fd, cJSON *json);
static void ws_broadcast_json(cJSON *json);
static void send_status(int fd);
static void send_fingerprints(int fd);

/* ============================================================================
 * HTTP Handlers
 * ============================================================================ */
static esp_err_t index_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Cache-Control", "max-age=3600");
    size_t len = index_html_end - index_html_start;
    return httpd_resp_send(req, index_html_start, len);
}

static esp_err_t style_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/css");
    httpd_resp_set_hdr(req, "Cache-Control", "max-age=3600");
    size_t len = style_css_end - style_css_start;
    return httpd_resp_send(req, style_css_start, len);
}

static esp_err_t js_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/javascript");
    httpd_resp_set_hdr(req, "Cache-Control", "max-age=3600");
    size_t len = app_js_end - app_js_start;
    return httpd_resp_send(req, app_js_start, len);
}

/* ============================================================================
 * WebSocket Handler
 * ============================================================================ */
static esp_err_t ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        /* WebSocket handshake */
        ESP_LOGI(TAG, "WebSocket handshake");
        return ESP_OK;
    }
    
    /* Receive frame */
    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;
    
    /* Get frame length */
    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "httpd_ws_recv_frame failed: %d", ret);
        return ret;
    }
    
    if (ws_pkt.len == 0) {
        return ESP_OK;
    }
    
    /* Allocate buffer and receive */
    uint8_t *buf = malloc(ws_pkt.len + 1);
    if (!buf) {
        ESP_LOGE(TAG, "Failed to allocate buffer");
        return ESP_ERR_NO_MEM;
    }
    
    ws_pkt.payload = buf;
    ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "httpd_ws_recv_frame failed: %d", ret);
        free(buf);
        return ret;
    }
    
    buf[ws_pkt.len] = '\0';
    
    /* Handle message */
    if (ws_pkt.type == HTTPD_WS_TYPE_TEXT) {
        handle_ws_message(req, (char *)buf, ws_pkt.len);
    }
    
    free(buf);
    return ESP_OK;
}

/* ============================================================================
 * WebSocket Message Handling
 * ============================================================================ */
static void handle_ws_message(httpd_req_t *req, const char *data, size_t len)
{
    cJSON *json = cJSON_Parse(data);
    if (!json) {
        ESP_LOGE(TAG, "Failed to parse JSON");
        return;
    }
    
    cJSON *cmd = cJSON_GetObjectItem(json, "cmd");
    if (!cmd || !cJSON_IsString(cmd)) {
        cJSON_Delete(json);
        return;
    }
    
    int fd = httpd_req_to_sockfd(req);
    const char *cmd_str = cmd->valuestring;
    
    ESP_LOGI(TAG, "WS command: %s", cmd_str);
    
    if (strcmp(cmd_str, "get_status") == 0) {
        send_status(fd);
    }
    else if (strcmp(cmd_str, "get_fingerprints") == 0) {
        send_fingerprints(fd);
    }
    else if (strcmp(cmd_str, "enroll") == 0) {
        /* Post enroll event to app */
        app_request_enroll();
    }
    else if (strcmp(cmd_str, "search") == 0) {
        /* Post search event to app */
        app_request_search();
    }
    else if (strcmp(cmd_str, "cancel") == 0) {
        app_request_cancel();
    }
    else if (strcmp(cmd_str, "delete") == 0) {
        cJSON *id = cJSON_GetObjectItem(json, "id");
        if (id && cJSON_IsNumber(id)) {
            app_request_delete((int)id->valuedouble);
        }
    }
    else if (strcmp(cmd_str, "delete_all") == 0) {
        app_request_delete_all();
    }
    else if (strcmp(cmd_str, "set_volume") == 0) {
        cJSON *volume = cJSON_GetObjectItem(json, "volume");
        if (volume && cJSON_IsNumber(volume)) {
            audio_set_volume((int)volume->valuedouble);
        }
    }
    else if (strcmp(cmd_str, "set_auto_search") == 0) {
        cJSON *enabled = cJSON_GetObjectItem(json, "enabled");
        if (enabled && cJSON_IsBool(enabled)) {
            app_set_auto_search(cJSON_IsTrue(enabled));
        }
    }
    
    cJSON_Delete(json);
}

static void send_status(int fd)
{
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "type", "status");
    cJSON_AddNumberToObject(json, "finger_count", finger_get_template_count());
    cJSON_AddNumberToObject(json, "library_size", finger_get_library_size());
    cJSON_AddBoolToObject(json, "sensor_ok", finger_is_connected());
    cJSON_AddStringToObject(json, "state", app_get_state_string());
    cJSON_AddNumberToObject(json, "volume", audio_get_volume());
    cJSON_AddBoolToObject(json, "auto_search", app_get_auto_search());
    
    ws_send_json(fd, json);
    cJSON_Delete(json);
}

static void send_fingerprints(int fd)
{
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "type", "fingerprints");
    
    cJSON *list = cJSON_CreateArray();
    
    /* Get fingerprint list from sensor */
    int count = finger_get_template_count();
    if (count > 0) {
        /* For now, just list IDs 0 to count-1 */
        /* TODO: Get actual used IDs from sensor */
        for (int i = 0; i < count && i < 200; i++) {
            if (finger_is_id_used(i)) {
                cJSON *fp = cJSON_CreateObject();
                cJSON_AddNumberToObject(fp, "id", i);
                cJSON_AddItemToArray(list, fp);
            }
        }
    }
    
    cJSON_AddItemToObject(json, "list", list);
    ws_send_json(fd, json);
    cJSON_Delete(json);
}

/* ============================================================================
 * WebSocket Utilities
 * ============================================================================ */
static void ws_send_json(int fd, cJSON *json)
{
    char *str = cJSON_PrintUnformatted(json);
    if (!str) return;
    
    httpd_ws_frame_t ws_pkt = {
        .final = true,
        .fragmented = false,
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t *)str,
        .len = strlen(str)
    };
    
    if (s_server) {
        httpd_ws_send_frame_async(s_server, fd, &ws_pkt);
    }
    
    free(str);
}

static void ws_broadcast_json(cJSON *json)
{
    if (!s_server) return;
    
    char *str = cJSON_PrintUnformatted(json);
    if (!str) return;
    
    httpd_ws_frame_t ws_pkt = {
        .final = true,
        .fragmented = false,
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t *)str,
        .len = strlen(str)
    };
    
    /* Get all connected clients */
    size_t clients = MAX_WS_CLIENTS;
    int client_fds[MAX_WS_CLIENTS];
    
    if (httpd_get_client_list(s_server, &clients, client_fds) == ESP_OK) {
        for (size_t i = 0; i < clients; i++) {
            if (httpd_ws_get_fd_info(s_server, client_fds[i]) == HTTPD_WS_CLIENT_WEBSOCKET) {
                httpd_ws_send_frame_async(s_server, client_fds[i], &ws_pkt);
            }
        }
    }
    
    free(str);
}

/* ============================================================================
 * Public API - Broadcast Functions
 * ============================================================================ */
void webserver_broadcast_event(const char *event, const char *message)
{
    if (!s_server) return;
    
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "type", "event");
    cJSON_AddStringToObject(json, "event", event);
    if (message) {
        cJSON_AddStringToObject(json, "message", message);
    }
    
    ws_broadcast_json(json);
    cJSON_Delete(json);
}

void webserver_broadcast_state(const char *state)
{
    if (!s_server) return;
    
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "type", "state");
    cJSON_AddStringToObject(json, "state", state);
    
    ws_broadcast_json(json);
    cJSON_Delete(json);
}

void webserver_broadcast_match(int id, int score)
{
    if (!s_server) return;
    
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "type", "event");
    
    if (id >= 0) {
        cJSON_AddStringToObject(json, "event", "match_ok");
        cJSON_AddNumberToObject(json, "id", id);
        cJSON_AddNumberToObject(json, "score", score);
    } else {
        cJSON_AddStringToObject(json, "event", "match_fail");
    }
    
    ws_broadcast_json(json);
    cJSON_Delete(json);
}

void webserver_broadcast_enroll_step(int step)
{
    if (!s_server) return;
    
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "type", "event");
    cJSON_AddStringToObject(json, "event", "enroll_step");
    cJSON_AddNumberToObject(json, "step", step);
    
    ws_broadcast_json(json);
    cJSON_Delete(json);
}

void webserver_broadcast_enroll_ok(int id)
{
    if (!s_server) return;
    
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "type", "event");
    cJSON_AddStringToObject(json, "event", "enroll_ok");
    cJSON_AddNumberToObject(json, "id", id);
    
    ws_broadcast_json(json);
    cJSON_Delete(json);
}

void webserver_broadcast_delete(int id)
{
    if (!s_server) return;
    
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "type", "event");
    
    if (id < 0) {
        cJSON_AddStringToObject(json, "event", "delete_all");
    } else {
        cJSON_AddStringToObject(json, "event", "delete_ok");
        cJSON_AddNumberToObject(json, "id", id);
    }
    
    ws_broadcast_json(json);
    cJSON_Delete(json);
}

/* ============================================================================
 * Public API - Server Control
 * ============================================================================ */
bool webserver_init(void)
{
    if (s_ws_mutex == NULL) {
        s_ws_mutex = xSemaphoreCreateMutex();
        if (!s_ws_mutex) {
            ESP_LOGE(TAG, "Failed to create mutex");
            return false;
        }
    }
    
    memset(s_ws_clients, 0, sizeof(s_ws_clients));
    s_ws_client_count = 0;
    
    ESP_LOGI(TAG, "Initialized");
    return true;
}

bool webserver_start(void)
{
    if (s_server) {
        ESP_LOGW(TAG, "Already running");
        return true;
    }
    
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = WEBSERVER_TASK_STACK_SIZE;
    config.max_uri_handlers = 8;
    config.max_open_sockets = MAX_WS_CLIENTS + 2;
    config.lru_purge_enable = true;
    
    ESP_LOGI(TAG, "Starting server on port %d", config.server_port);
    
    esp_err_t ret = httpd_start(&s_server, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start server: %s", esp_err_to_name(ret));
        return false;
    }
    
    /* Register handlers */
    const httpd_uri_t index_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = index_handler
    };
    httpd_register_uri_handler(s_server, &index_uri);
    
    const httpd_uri_t style_uri = {
        .uri = "/style.css",
        .method = HTTP_GET,
        .handler = style_handler
    };
    httpd_register_uri_handler(s_server, &style_uri);
    
    const httpd_uri_t js_uri = {
        .uri = "/app.js",
        .method = HTTP_GET,
        .handler = js_handler
    };
    httpd_register_uri_handler(s_server, &js_uri);
    
    const httpd_uri_t ws_uri = {
        .uri = "/ws",
        .method = HTTP_GET,
        .handler = ws_handler,
        .is_websocket = true
    };
    httpd_register_uri_handler(s_server, &ws_uri);
    
    ESP_LOGI(TAG, "Server started");
    return true;
}

void webserver_stop(void)
{
    if (s_server) {
        httpd_stop(s_server);
        s_server = NULL;
        ESP_LOGI(TAG, "Server stopped");
    }
}

bool webserver_is_running(void)
{
    return s_server != NULL;
}
