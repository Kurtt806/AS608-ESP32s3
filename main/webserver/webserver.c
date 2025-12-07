/**
 * @file webserver.c
 * @brief Web server implementation with WebSocket event support
 * 
 * Provides:
 * - HTTP endpoints for REST API
 * - WebSocket /ws/events for real-time event broadcasting
 * - Embedded web assets (HTML, CSS, JS)
 */

#include "webserver.h"
#include "../common/config.h"
#include "../finger/finger.h"
#include "../finger/finger_meta.h"
#include "audio.h"
#include "../app/app.h"
#include "../ota/ota.h"

#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_http_server.h"
#include "cJSON.h"

/* MIN macro if not defined */
#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

/* Embedded assets */
extern const char index_html_start[] asm("_binary_index_html_start");
extern const char index_html_end[] asm("_binary_index_html_end");
extern const char style_css_start[] asm("_binary_style_css_start");
extern const char style_css_end[] asm("_binary_style_css_end");
extern const char app_js_start[] asm("_binary_app_js_start");
extern const char app_js_end[] asm("_binary_app_js_end");
extern const char ws_js_start[] asm("_binary_ws_js_start");
extern const char ws_js_end[] asm("_binary_ws_js_end");
extern const char api_js_start[] asm("_binary_api_js_start");
extern const char api_js_end[] asm("_binary_api_js_end");

static const char *TAG = "webserver";

/* ============================================================================
 * Constants
 * ============================================================================ */
#define MAX_WS_CLIENTS      4
#define JSON_BUFFER_SIZE    256

/* ============================================================================
 * Static Variables
 * ============================================================================ */
static httpd_handle_t s_server = NULL;
static SemaphoreHandle_t s_ws_mutex = NULL;

/* ============================================================================
 * Forward Declarations
 * ============================================================================ */
static esp_err_t ws_events_handler(httpd_req_t *req);
static void ws_broadcast_json(cJSON *json);

/* ============================================================================
 * HTTP Static File Handlers
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

static esp_err_t app_js_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/javascript");
    httpd_resp_set_hdr(req, "Cache-Control", "max-age=3600");
    size_t len = app_js_end - app_js_start;
    return httpd_resp_send(req, app_js_start, len);
}

static esp_err_t ws_js_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/javascript");
    httpd_resp_set_hdr(req, "Cache-Control", "max-age=3600");
    size_t len = ws_js_end - ws_js_start;
    return httpd_resp_send(req, ws_js_start, len);
}

static esp_err_t api_js_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/javascript");
    httpd_resp_set_hdr(req, "Cache-Control", "max-age=3600");
    size_t len = api_js_end - api_js_start;
    return httpd_resp_send(req, api_js_start, len);
}

/* ============================================================================
 * HTTP REST API Handlers
 * ============================================================================ */

/* GET /finger/status - Get device status */
static esp_err_t finger_status_handler(httpd_req_t *req)
{
    cJSON *json = cJSON_CreateObject();
    cJSON_AddNumberToObject(json, "finger_count", finger_get_template_count());
    cJSON_AddNumberToObject(json, "library_size", finger_get_library_size());
    cJSON_AddBoolToObject(json, "sensor_ok", finger_is_connected());
    cJSON_AddStringToObject(json, "state", app_get_state_string());
    cJSON_AddNumberToObject(json, "volume", audio_get_volume());
    
    /* Calculate next available ID */
    int next_id = 0;
    for (int i = 0; i < finger_get_library_size(); i++) {
        if (!finger_is_id_used(i)) {
            next_id = i;
            break;
        }
    }
    cJSON_AddNumberToObject(json, "next_id", next_id);
    
    char *str = cJSON_PrintUnformatted(json);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, str, strlen(str));
    
    free(str);
    cJSON_Delete(json);
    return ESP_OK;
}

/* GET /finger/list - Get fingerprint list */
static esp_err_t finger_list_handler(httpd_req_t *req)
{
    cJSON *json = cJSON_CreateObject();
    cJSON *list = cJSON_CreateArray();
    
    int count = finger_get_template_count();
    if (count > 0) {
        for (int i = 0; i < finger_get_library_size() && i < 200; i++) {
            if (finger_is_id_used(i)) {
                cJSON *fp = cJSON_CreateObject();
                cJSON_AddNumberToObject(fp, "id", i);
                
                /* Add name from metadata */
                const char *name = finger_meta_get_name(i);
                if (name && name[0] != '\0') {
                    cJSON_AddStringToObject(fp, "name", name);
                } else {
                    /* Default name if no metadata */
                    char default_name[32];
                    snprintf(default_name, sizeof(default_name), "ID_%d", i);
                    cJSON_AddStringToObject(fp, "name", default_name);
                }
                
                /* Add match count if available */
                finger_meta_entry_t entry;
                if (finger_meta_get_entry(i, &entry) == ESP_OK) {
                    cJSON_AddNumberToObject(fp, "match_count", entry.match_count);
                }
                
                cJSON_AddItemToArray(list, fp);
            }
        }
    }
    
    cJSON_AddItemToObject(json, "list", list);
    
    char *str = cJSON_PrintUnformatted(json);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, str, strlen(str));
    
    free(str);
    cJSON_Delete(json);
    return ESP_OK;
}

/* POST /finger/enroll - Start enrollment */
static esp_err_t finger_enroll_handler(httpd_req_t *req)
{
    app_request_enroll();
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"ok\":true}", -1);
    return ESP_OK;
}

/* POST /finger/match - Start match test */
static esp_err_t finger_match_handler(httpd_req_t *req)
{
    app_request_search();
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"ok\":true}", -1);
    return ESP_OK;
}

/* POST /finger/cancel - Cancel current operation */
static esp_err_t finger_cancel_handler(httpd_req_t *req)
{
    app_request_cancel();
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"ok\":true}", -1);
    return ESP_OK;
}

/* POST /finger/delete - Delete fingerprint by ID */
static esp_err_t finger_delete_handler(httpd_req_t *req)
{
    char buf[64];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    
    if (ret > 0) {
        buf[ret] = '\0';
        cJSON *json = cJSON_Parse(buf);
        if (json) {
            cJSON *id = cJSON_GetObjectItem(json, "id");
            if (id && cJSON_IsNumber(id)) {
                app_request_delete((int)id->valuedouble);
            }
            cJSON_Delete(json);
        }
    }
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"ok\":true}", -1);
    return ESP_OK;
}

/* POST /finger/clear - Clear all fingerprints */
static esp_err_t finger_clear_handler(httpd_req_t *req)
{
    app_request_delete_all();
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"ok\":true}", -1);
    return ESP_OK;
}

/* PUT /audio/volume - Set volume */
static esp_err_t audio_volume_handler(httpd_req_t *req)
{
    char buf[64];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    
    if (ret > 0) {
        buf[ret] = '\0';
        cJSON *json = cJSON_Parse(buf);
        if (json) {
            cJSON *vol = cJSON_GetObjectItem(json, "vol");
            if (vol && cJSON_IsNumber(vol)) {
                audio_set_volume((int)vol->valuedouble);
            }
            cJSON_Delete(json);
        }
    }
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"ok\":true}", -1);
    return ESP_OK;
}

/* PUT /finger/name - Set fingerprint name */
static esp_err_t finger_name_handler(httpd_req_t *req)
{
    char buf[128];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    
    esp_err_t result = ESP_ERR_INVALID_ARG;
    
    if (ret > 0) {
        buf[ret] = '\0';
        cJSON *json = cJSON_Parse(buf);
        if (json) {
            cJSON *id_item = cJSON_GetObjectItem(json, "id");
            cJSON *name_item = cJSON_GetObjectItem(json, "name");
            
            if (id_item && cJSON_IsNumber(id_item) && name_item && cJSON_IsString(name_item)) {
                int id = (int)id_item->valuedouble;
                const char *name = name_item->valuestring;
                
                if (id >= 0 && id < FINGER_META_MAX_COUNT && name != NULL) {
                    result = finger_meta_set_name(id, name);
                }
            }
            cJSON_Delete(json);
        }
    }
    
    httpd_resp_set_type(req, "application/json");
    if (result == ESP_OK) {
        httpd_resp_send(req, "{\"ok\":true}", -1);
    } else {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_send(req, "{\"ok\":false,\"error\":\"Invalid request\"}", -1);
    }
    return ESP_OK;
}

/* GET /finger/meta/:id - Get metadata for specific ID */
static esp_err_t finger_meta_get_handler(httpd_req_t *req)
{
    /* Parse ID from query string */
    char query[32];
    int id = -1;
    
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        char param[16];
        if (httpd_query_key_value(query, "id", param, sizeof(param)) == ESP_OK) {
            id = atoi(param);
        }
    }
    
    if (id < 0 || id >= FINGER_META_MAX_COUNT) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_send(req, "{\"ok\":false,\"error\":\"Invalid ID\"}", -1);
        return ESP_OK;
    }
    
    cJSON *json = cJSON_CreateObject();
    finger_meta_entry_t entry;
    
    if (finger_meta_get_entry(id, &entry) == ESP_OK) {
        cJSON_AddBoolToObject(json, "ok", true);
        cJSON_AddNumberToObject(json, "id", entry.id);
        cJSON_AddStringToObject(json, "name", entry.name);
        cJSON_AddNumberToObject(json, "match_count", entry.match_count);
        cJSON_AddNumberToObject(json, "created_at", entry.created_at);
        cJSON_AddNumberToObject(json, "last_match", entry.last_match);
    } else {
        /* Return default data if no metadata exists */
        cJSON_AddBoolToObject(json, "ok", true);
        cJSON_AddNumberToObject(json, "id", id);
        
        char default_name[32];
        snprintf(default_name, sizeof(default_name), "ID_%d", id);
        cJSON_AddStringToObject(json, "name", default_name);
        cJSON_AddNumberToObject(json, "match_count", 0);
    }
    
    char *str = cJSON_PrintUnformatted(json);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, str, strlen(str));
    
    free(str);
    cJSON_Delete(json);
    return ESP_OK;
}

/* ============================================================================
 * OTA Handlers
 * ============================================================================ */

/* GET /ota/info - Get firmware info and OTA status */
static esp_err_t ota_info_handler(httpd_req_t *req)
{
    cJSON *json = cJSON_CreateObject();
    
    /* Get firmware info */
    ota_firmware_info_t info;
    if (ota_get_firmware_info(&info) == ESP_OK) {
        cJSON_AddStringToObject(json, "version", info.version);
        cJSON_AddStringToObject(json, "project", info.project_name);
        cJSON_AddStringToObject(json, "compile_date", info.compile_date);
        cJSON_AddStringToObject(json, "compile_time", info.compile_time);
        cJSON_AddStringToObject(json, "idf_version", info.idf_version);
        cJSON_AddBoolToObject(json, "can_rollback", info.can_rollback);
    }
    
    cJSON_AddStringToObject(json, "partition", ota_get_running_partition());
    
    /* Get OTA progress */
    ota_progress_t progress;
    if (ota_get_progress(&progress) == ESP_OK) {
        cJSON_AddNumberToObject(json, "state", progress.state);
        cJSON_AddNumberToObject(json, "progress", progress.progress);
        cJSON_AddStringToObject(json, "message", progress.message ? progress.message : "");
    }
    
    char *str = cJSON_PrintUnformatted(json);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, str, strlen(str));
    
    free(str);
    cJSON_Delete(json);
    return ESP_OK;
}

/* POST /ota/upload - Upload firmware file */
static esp_err_t ota_upload_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "OTA upload started, content length: %d", req->content_len);
    
    /* Begin OTA */
    esp_err_t ret = ota_begin(req->content_len);
    if (ret != ESP_OK) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_send(req, "{\"ok\":false,\"error\":\"OTA begin failed\"}", -1);
        return ESP_OK;
    }
    
    /* Allocate buffer */
    char *buf = malloc(OTA_BUFFER_SIZE);
    if (!buf) {
        ota_abort();
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_send(req, "{\"ok\":false,\"error\":\"Out of memory\"}", -1);
        return ESP_OK;
    }
    
    /* Receive and write firmware */
    int remaining = req->content_len;
    while (remaining > 0) {
        int recv_len = httpd_req_recv(req, buf, MIN(remaining, OTA_BUFFER_SIZE));
        
        if (recv_len <= 0) {
            if (recv_len == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;  /* Retry on timeout */
            }
            ESP_LOGE(TAG, "Receive failed");
            free(buf);
            ota_abort();
            httpd_resp_set_status(req, "500 Internal Server Error");
            httpd_resp_send(req, "{\"ok\":false,\"error\":\"Receive failed\"}", -1);
            return ESP_OK;
        }
        
        ret = ota_write(buf, recv_len);
        if (ret != ESP_OK) {
            free(buf);
            ota_abort();
            httpd_resp_set_status(req, "500 Internal Server Error");
            httpd_resp_send(req, "{\"ok\":false,\"error\":\"Write failed\"}", -1);
            return ESP_OK;
        }
        
        remaining -= recv_len;
    }
    
    free(buf);
    
    /* Finish OTA */
    ret = ota_end();
    if (ret != ESP_OK) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_send(req, "{\"ok\":false,\"error\":\"Verification failed\"}", -1);
        return ESP_OK;
    }
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"ok\":true,\"message\":\"Update complete. Rebooting...\"}", -1);
    
    /* Auto restart after 1 second to apply new firmware */
    ESP_LOGI(TAG, "OTA successful, restarting in 1 second...");
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
    
    return ESP_OK;
}

/* POST /ota/url - Start OTA from URL */
static esp_err_t ota_url_handler(httpd_req_t *req)
{
    char buf[OTA_URL_MAX_LEN + 32];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    
    if (ret <= 0) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_send(req, "{\"ok\":false,\"error\":\"Invalid request\"}", -1);
        return ESP_OK;
    }
    
    buf[ret] = '\0';
    cJSON *json = cJSON_Parse(buf);
    if (!json) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_send(req, "{\"ok\":false,\"error\":\"Invalid JSON\"}", -1);
        return ESP_OK;
    }
    
    cJSON *url_item = cJSON_GetObjectItem(json, "url");
    if (!url_item || !cJSON_IsString(url_item)) {
        cJSON_Delete(json);
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_send(req, "{\"ok\":false,\"error\":\"URL required\"}", -1);
        return ESP_OK;
    }
    
    esp_err_t result = ota_start_url(url_item->valuestring);
    cJSON_Delete(json);
    
    httpd_resp_set_type(req, "application/json");
    if (result == ESP_OK) {
        httpd_resp_send(req, "{\"ok\":true,\"message\":\"Download started\"}", -1);
    } else {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_send(req, "{\"ok\":false,\"error\":\"Failed to start download\"}", -1);
    }
    return ESP_OK;
}

/* POST /ota/reboot - Reboot device */
static esp_err_t ota_reboot_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"ok\":true,\"message\":\"Rebooting...\"}", -1);
    
    /* Delay to allow response to be sent */
    vTaskDelay(pdMS_TO_TICKS(500));
    ota_reboot();
    
    return ESP_OK;
}

/* POST /ota/rollback - Rollback to previous firmware */
static esp_err_t ota_rollback_handler(httpd_req_t *req)
{
    ota_firmware_info_t info;
    ota_get_firmware_info(&info);
    
    if (!info.can_rollback) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_send(req, "{\"ok\":false,\"error\":\"Rollback not available\"}", -1);
        return ESP_OK;
    }
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"ok\":true,\"message\":\"Rolling back...\"}", -1);
    
    vTaskDelay(pdMS_TO_TICKS(500));
    ota_rollback();
    
    return ESP_OK;
}

/* ============================================================================
 * WebSocket Handler - /ws/events
 * ============================================================================ */
static esp_err_t ws_events_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        /* WebSocket handshake */
        ESP_LOGI(TAG, "WebSocket client connected");
        return ESP_OK;
    }
    
    /* We don't expect messages from client on this endpoint */
    /* Just drain any incoming data */
    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;
    
    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK) {
        return ret;
    }
    
    if (ws_pkt.len > 0) {
        uint8_t *buf = malloc(ws_pkt.len + 1);
        if (buf) {
            ws_pkt.payload = buf;
            httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
            free(buf);
        }
    }
    
    return ESP_OK;
}

/* ============================================================================
 * WebSocket Broadcasting Utilities
 * ============================================================================ */
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
    
    /* Get all connected WebSocket clients */
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
 * Public API - Event Broadcasting Functions
 * ============================================================================ */

void finger_send_event(const char *event, int value)
{
    if (!s_server) return;
    
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "event", event);
    
    if (value >= 0) {
        cJSON_AddNumberToObject(json, "id", value);
    }
    
    ws_broadcast_json(json);
    cJSON_Delete(json);
    
    ESP_LOGI(TAG, "Event: %s, value: %d", event, value);
}

void webserver_broadcast_event(const char *event, const char *message)
{
    if (!s_server) return;
    
    cJSON *json = cJSON_CreateObject();
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
    cJSON_AddStringToObject(json, "event", state);
    cJSON_AddStringToObject(json, "type", "state");
    
    ws_broadcast_json(json);
    cJSON_Delete(json);
}

void webserver_broadcast_match(int id, int score)
{
    if (!s_server) return;
    
    cJSON *json = cJSON_CreateObject();
    
    if (id >= 0) {
        cJSON_AddStringToObject(json, "event", "match");
        cJSON_AddNumberToObject(json, "id", id);
        cJSON_AddNumberToObject(json, "score", score);
        
        /* Include fingerprint name if available */
        const char *name = finger_meta_get_name(id);
        if (name && name[0] != '\0') {
            cJSON_AddStringToObject(json, "name", name);
        }
    } else {
        cJSON_AddStringToObject(json, "event", "no_match");
    }
    
    ws_broadcast_json(json);
    cJSON_Delete(json);
}

void webserver_broadcast_enroll_step(int step)
{
    if (!s_server) return;
    
    cJSON *json = cJSON_CreateObject();
    
    if (step == 1) {
        cJSON_AddStringToObject(json, "event", "enroll_step1_ok");
    } else if (step == 2) {
        cJSON_AddStringToObject(json, "event", "enroll_step2_ok");
    }
    cJSON_AddNumberToObject(json, "step", step);
    
    ws_broadcast_json(json);
    cJSON_Delete(json);
}

void webserver_broadcast_enroll_ok(int id)
{
    if (!s_server) return;
    
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "event", "store_ok");
    cJSON_AddNumberToObject(json, "id", id);
    
    ws_broadcast_json(json);
    cJSON_Delete(json);
}

void webserver_broadcast_delete(int id)
{
    if (!s_server) return;
    
    cJSON *json = cJSON_CreateObject();
    
    if (id < 0) {
        cJSON_AddStringToObject(json, "event", "clear_ok");
    } else {
        cJSON_AddStringToObject(json, "event", "delete_ok");
        cJSON_AddNumberToObject(json, "id", id);
    }
    
    ws_broadcast_json(json);
    cJSON_Delete(json);
}

void webserver_broadcast_error(const char *message)
{
    if (!s_server) return;
    
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "event", "error");
    if (message) {
        cJSON_AddStringToObject(json, "message", message);
    }
    
    ws_broadcast_json(json);
    cJSON_Delete(json);
}

/* OTA progress callback for WebSocket broadcast */
static void ota_progress_callback(const ota_progress_t *progress, void *user_data)
{
    (void)user_data;
    if (!s_server || !progress) return;
    
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "event", "ota_progress");
    
    const char *state_str = "unknown";
    switch (progress->state) {
        case OTA_STATE_IDLE:        state_str = "idle"; break;
        case OTA_STATE_STARTING:    state_str = "starting"; break;
        case OTA_STATE_DOWNLOADING: state_str = "downloading"; break;
        case OTA_STATE_VERIFYING:   state_str = "verifying"; break;
        case OTA_STATE_APPLYING:    state_str = "applying"; break;
        case OTA_STATE_COMPLETE:    state_str = "completed"; break;
        case OTA_STATE_ERROR:       state_str = "failed"; break;
        case OTA_STATE_ROLLING_BACK: state_str = "rollback"; break;
    }
    
    cJSON_AddStringToObject(json, "state", state_str);
    cJSON_AddNumberToObject(json, "progress", progress->progress);
    if (progress->message) {
        cJSON_AddStringToObject(json, "message", progress->message);
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
    config.max_uri_handlers = 26;  /* Increased for OTA endpoints */
    config.max_open_sockets = 4;  /* Max 7 allowed (LWIP), 3 used by HTTP server */
    config.lru_purge_enable = true;
    
    ESP_LOGI(TAG, "Starting server on port %d", config.server_port);
    
    esp_err_t ret = httpd_start(&s_server, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start server: %s", esp_err_to_name(ret));
        return false;
    }
    
    /* Register OTA progress callback */
    ota_set_progress_callback(ota_progress_callback, NULL);
    
    /* Static file handlers */
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
    
    const httpd_uri_t app_js_uri = {
        .uri = "/app.js",
        .method = HTTP_GET,
        .handler = app_js_handler
    };
    httpd_register_uri_handler(s_server, &app_js_uri);
    
    const httpd_uri_t ws_js_uri = {
        .uri = "/ws.js",
        .method = HTTP_GET,
        .handler = ws_js_handler
    };
    httpd_register_uri_handler(s_server, &ws_js_uri);
    
    const httpd_uri_t api_js_uri = {
        .uri = "/api.js",
        .method = HTTP_GET,
        .handler = api_js_handler
    };
    httpd_register_uri_handler(s_server, &api_js_uri);
    
    /* REST API handlers */
    const httpd_uri_t finger_status_uri = {
        .uri = "/finger/status",
        .method = HTTP_GET,
        .handler = finger_status_handler
    };
    httpd_register_uri_handler(s_server, &finger_status_uri);
    
    const httpd_uri_t finger_list_uri = {
        .uri = "/finger/list",
        .method = HTTP_GET,
        .handler = finger_list_handler
    };
    httpd_register_uri_handler(s_server, &finger_list_uri);
    
    const httpd_uri_t finger_enroll_uri = {
        .uri = "/finger/enroll",
        .method = HTTP_POST,
        .handler = finger_enroll_handler
    };
    httpd_register_uri_handler(s_server, &finger_enroll_uri);
    
    const httpd_uri_t finger_match_uri = {
        .uri = "/finger/match",
        .method = HTTP_POST,
        .handler = finger_match_handler
    };
    httpd_register_uri_handler(s_server, &finger_match_uri);
    
    const httpd_uri_t finger_cancel_uri = {
        .uri = "/finger/cancel",
        .method = HTTP_POST,
        .handler = finger_cancel_handler
    };
    httpd_register_uri_handler(s_server, &finger_cancel_uri);
    
    const httpd_uri_t finger_delete_uri = {
        .uri = "/finger/delete",
        .method = HTTP_POST,
        .handler = finger_delete_handler
    };
    httpd_register_uri_handler(s_server, &finger_delete_uri);
    
    const httpd_uri_t finger_clear_uri = {
        .uri = "/finger/clear",
        .method = HTTP_POST,
        .handler = finger_clear_handler
    };
    httpd_register_uri_handler(s_server, &finger_clear_uri);
    
    const httpd_uri_t audio_volume_uri = {
        .uri = "/audio/volume",
        .method = HTTP_PUT,
        .handler = audio_volume_handler
    };
    httpd_register_uri_handler(s_server, &audio_volume_uri);
    
    /* Fingerprint metadata handlers */
    const httpd_uri_t finger_name_uri = {
        .uri = "/finger/name",
        .method = HTTP_PUT,
        .handler = finger_name_handler
    };
    httpd_register_uri_handler(s_server, &finger_name_uri);
    
    const httpd_uri_t finger_meta_uri = {
        .uri = "/finger/meta",
        .method = HTTP_GET,
        .handler = finger_meta_get_handler
    };
    httpd_register_uri_handler(s_server, &finger_meta_uri);
    
    /* OTA handlers */
    const httpd_uri_t ota_info_uri = {
        .uri = "/ota/status",
        .method = HTTP_GET,
        .handler = ota_info_handler
    };
    httpd_register_uri_handler(s_server, &ota_info_uri);
    
    const httpd_uri_t ota_upload_uri = {
        .uri = "/ota/upload",
        .method = HTTP_POST,
        .handler = ota_upload_handler
    };
    httpd_register_uri_handler(s_server, &ota_upload_uri);
    
    const httpd_uri_t ota_url_uri = {
        .uri = "/ota/update",
        .method = HTTP_POST,
        .handler = ota_url_handler
    };
    httpd_register_uri_handler(s_server, &ota_url_uri);
    
    const httpd_uri_t ota_reboot_uri = {
        .uri = "/ota/reboot",
        .method = HTTP_POST,
        .handler = ota_reboot_handler
    };
    httpd_register_uri_handler(s_server, &ota_reboot_uri);
    
    const httpd_uri_t ota_rollback_uri = {
        .uri = "/ota/rollback",
        .method = HTTP_POST,
        .handler = ota_rollback_handler
    };
    httpd_register_uri_handler(s_server, &ota_rollback_uri);
    
    /* WebSocket handler for real-time events */
    const httpd_uri_t ws_events_uri = {
        .uri = "/ws/events",
        .method = HTTP_GET,
        .handler = ws_events_handler,
        .is_websocket = true
    };
    httpd_register_uri_handler(s_server, &ws_events_uri);
    
    ESP_LOGI(TAG, "Server started successfully");
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
