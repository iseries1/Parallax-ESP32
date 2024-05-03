/**
* @brief HTTP Web Server
* @author Michael Burmeister
* @date January 27, 2020
* @version 1.0
*/

#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <sys/param.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include "esp_err.h"
#include "esp_system.h"
#include "esp_mac.h"
#include "esp_log.h"
#include "esp_vfs.h"
#include "esp_spiffs.h"
#include "esp_http_server.h"
#include "esp_http_client.h"
#include "esp_wifi_types.h"
#include "esp_wifi.h"
#include "driver/uart.h"


#include "config.h"
#include "parser.h"
#include "settings.h"
#include "httpd.h"
#include "json.h"
#include "cmds.h"
#include "status.h"

#define MAXHANDLERS 26

typedef struct
{
    const char* url;
    httpd_method_t meth;
    esp_err_t(*handler)(httpd_req_t* r);
} HttpdBuiltInUrl;

typedef struct
{
    const char* url;
    const char* page;
} HttpRedirect;

typedef struct {
    char* name;
    int (*getHandler)(void* data, char* value);
    int (*setHandler)(void* data, char* value);
    void* data;
} cmd_def;

/* Max length a file path can have on storage */
#define FILE_PATH_MAX (ESP_VFS_PATH_MAX + CONFIG_SPIFFS_OBJ_NAME_LEN)

/* Max Access Points That Can Be Found */
#define DEFAULT_SCAN_LIST_SIZE 5

/* Max size of an individual file. Make sure this
 * value is same as that set in upload_script.html */
#define MAX_FILE_SIZE   (200*1024) // 200 KB
#define MAX_FILE_SIZE_STR "200KB"

 /* Scratch buffer size */
#define SCRATCH_BUFSIZE  8192

struct file_server_data {
    /* Base path of file storage */
    char base_path[ESP_VFS_PATH_MAX + 1];

    /* Scratch buffer for temporary storage during file transfer */
    char scratch[SCRATCH_BUFSIZE];
};

static struct file_server_data* server_data = NULL;

HttpRedirect Red[] = {
    {"/websocket", "/websocket/index.html"},
    {"/wifi", "/wifi/wifi.html"},
    {"/wifi/connect.cgi", "/wifi/connecting.html"},
    {"/delete/*", "/directory"},
    {"/upload/*", "/directory"},
    {"", "/index.html"},
    {NULL, NULL}
};

#define MAX_LOGS 1024
static char log_buf[MAX_LOGS];
volatile int log_head, log_tail;
httpd_handle_t server = NULL;
httpd_config_t config = HTTPD_DEFAULT_CONFIG();


static cmd_def vars[] = {
    {   "version",          getVersion,         NULL,               NULL                            },
    {   "module-name",      getModuleName,      setModuleName,      NULL                            },
    {   "wifi-mode",        getWiFiMode,        setWiFiMode,        NULL                            },
    {   "wifi-ssid",        getWiFiSSID,        NULL,               NULL                            },
    {   "station-ipaddr",   getIPAddress,       setIPAddress,       (void*)WIFI_MODE_STA            },
    {   "station-macaddr",  getMACAddress,      setMACAddress,      (void*)WIFI_MODE_STA            },
    {   "softap-ipaddr",    getIPAddress,       setIPAddress,       (void*)WIFI_MODE_AP             },
    {   "softap-macaddr",   getMACAddress,      setMACAddress,      (void*)WIFI_MODE_AP             },
    {   "cmd-start-char",   uint8GetHandler,    uint8SetHandler,    &flashConfig.start              },
    {   "cmd-events",       int8GetHandler,     int8SetHandler,     &flashConfig.events             },
    {   "cmd-enable",       int8GetHandler,     int8SetHandler,     &flashConfig.enable             },
    {   "cmd-loader",       int8GetHandler,     int8SetHandler,     &flashConfig.loader             },
    {   "loader-baud-rate", intGetHandler,      setLoaderBaudrate,  &flashConfig.loader_baud_rate   },
    {   "baud-rate",        intGetHandler,      setBaudrate,        &flashConfig.baud_rate          },
    {   "dbg-baud-rate",    intGetHandler,      setDbgBaudrate,     &flashConfig.dbg_baud_rate      },
    {   "dbg-enable",       int8GetHandler,     int8SetHandler,     &flashConfig.dbg_enable         },
    {   "reset-pin",        int8GetHandler,     setResetPin,        &flashConfig.reset_pin          },
    {   "connect-led-pin",  int8GetHandler,     int8SetHandler,     &flashConfig.conn_led_pin       },
    {   NULL,               NULL,               NULL,               NULL                            }
};


static const char* TAG = "httpd";

static struct {
    httpd_handle_t hd;
    int fd;
    char method;
    char uri[32];
    char vars[128];
} UsrReq[10];

static char HexDecode(char x)
{
    if (x >= 'A')
        return x - 'A' + 10;
    if (x >= 'a')
        return x - 'a' + 10;
    return x - '0';
}

static int findArg(char* uri, char* arg, char* val)
{
    char buffer[128];
    char* e, *s;
    int i = 0;
    int t = 0;
    char v;

    buffer[0] = 0;
    // expand hex encoding
    while (uri[i] != 0)
    {
        v = uri[i++];
        if (v == '%')
        {
            v = HexDecode(uri[i++]) << 4;
            v = v + HexDecode(uri[i++]);
        }
        buffer[t++] = v;
    }
    buffer[t] = 0;

    s = buffer;
    e = strchr(s, '=');
    while (e != NULL)
    {
        e = strchr(s, '=');
        *e = 0;
        if (strcmp(arg, s) == 0)
        {
            s = e + 1;
            e = strchr(s, '&');
            if (e != NULL)
                *e = 0;
            strcpy(val, s);
            return 0;
        }
        e++;
        e = strchr(e, '&'); //find next parameter
        s = e + 1;
    }
    return -1;
}

/**
 * @brief proccess redirects 
 */
static esp_err_t redirect(httpd_req_t* req)
{
    char *buffer;
    int i, j;

    buffer = ((struct file_server_data*)req->user_ctx)->scratch;
    strcpy(buffer, req->uri);

    i = strlen(buffer);
    if (buffer[i - 1] == '/')
        buffer[i - 1] = 0;

    i = 0;
    while (Red[i].url != NULL)
    {
        j = strlen(Red[i].url);
        if (Red[i].url[j - 1] == '*')
            j -= 1;

        if (memcmp(Red[i].url, buffer, j) == 0)
        {
            httpd_resp_set_status(req, "307 Temporary Redirect");
            httpd_resp_set_hdr(req, "Location", Red[i].page);
            httpd_resp_send(req, NULL, 0);  // Response body can be empty
            return ESP_OK;
        }
        i++;
    }

    ESP_LOGI(TAG, "URI: %s", req->uri);
    httpd_resp_set_status(req, "307 Temporary Redirect");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, NULL, 0);  // Response body can be empty
    return ESP_OK;
}

/**
 * @brief process setting requests
 */
static esp_err_t PropSettings(httpd_req_t* req)
{
    char *buffer;
    char name[128], value[128], save[128];
    cmd_def* def = NULL;
    int i;

    memset(name, 0, sizeof(name));
    buffer = ((struct file_server_data*)req->user_ctx)->scratch;
    httpd_req_get_url_query_str(req, buffer, SCRATCH_BUFSIZE);

    if (def == NULL)
        i = 0;

    if (findArg(buffer, "name", name) != 0)
    {
        ESP_LOGW(TAG, "Argument 'name' not found");
        httpd_resp_send_err(req, 400, "Missing name argument\r\n");
        return ESP_OK;
    }

    for (i = 0; vars[i].name != NULL; ++i)
    {
        if (strcmp(name, vars[i].name) == 0)
        {
            def = &vars[i];
            break;
        }
    }

    if (!def)
    {
        ESP_LOGW(TAG, "Unknown Setting: %s", name);
        httpd_resp_send_err(req, 400, "Unknown setting\r\n");
        return ESP_OK;
    }

    if ((*def->getHandler)(def->data, value) != 0)
    {
        ESP_LOGE(TAG, "GET '%s' ERROR", def->name);
        httpd_resp_send_err(req, 400, "Get setting failed\r\n");
        return ESP_OK;
    }

    if (findArg(buffer, "value", save) == 0)
    {
        if (strcmp(value, save) != 0)
        {
            strcpy(value, save);
            if ((*def->setHandler)(def->data, value) != 0)
            {
                ESP_LOGE(TAG, "SET '%s' ERROR", def->name);
                httpd_resp_send_err(req, 400, "Get setting failed\r\n");
                return ESP_OK;
            }
            ESP_LOGI(TAG, "SET '%s' --> '%s'", def->name, value);
        }
    }
    else
    {
        ESP_LOGI(TAG, "GET '%s' --> '%s'", def->name, value);
    }

    i = strlen(value);

    sprintf(buffer, "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: %d\r\n\r\n%s", i, value);
    httpd_send(req, buffer, strlen(buffer));
    //httpd_resp_set_type(req, "text/plain");
    //httpd_resp_set_hdr(req, "Server", "ESP32");
    //httpd_resp_set_hdr(req, "Connection", "close");
    //httpd_resp_send(req, value, i);

    return ESP_OK;
}

/**
 * @brief Generate directory listing
 */
esp_err_t doDirectory(httpd_req_t* req, const char *Dir)
{
    char *Buffer;
    char dirpath[32];
    char entrypath[FILE_PATH_MAX];
    char entrysize[16];
    const char* entrytype;
    int len;

    struct dirent* entry;
    struct stat entry_stat;

    Buffer = ((struct file_server_data*)req->user_ctx)->scratch;
    strcpy(dirpath, ((struct file_server_data*)req->user_ctx)->base_path);
    strcat(dirpath, Dir);

    DIR* dir = opendir(dirpath);
    const size_t dirpath_len = strlen(dirpath);

    /* Retrieve the base path of file storage to construct the full path */
    strlcpy(entrypath, dirpath, sizeof(entrypath));

    if (!dir)
    {
        ESP_LOGE(TAG, "Failed to stat dir : %s", dirpath);
        return ESP_FAIL;
    }

    /* Iterate over all files / folders and fetch their names and sizes */
    while ((entry = readdir(dir)) != NULL)
    {
        entrytype = (entry->d_type == DT_DIR ? "directory" : "file");

        strlcpy(entrypath + dirpath_len, entry->d_name, sizeof(entrypath) - dirpath_len);
        if (stat(entrypath, &entry_stat) == -1)
        {
            ESP_LOGE(TAG, "Failed to stat %s : %s", entrytype, entry->d_name);
            continue;
        }
        sprintf(entrysize, "%ld", entry_stat.st_size);

        strcpy(Buffer, "<tr><td><a href=\"/");
        strcat(Buffer, entry->d_name);
        if (entry->d_type == DT_DIR)
            strcat(Buffer, "/");
        strcat(Buffer, "\">");
        strcat(Buffer, entry->d_name);
        strcat(Buffer, "</a></td><td>");
        strcat(Buffer, entrysize);
        strcat(Buffer, "</td><td>");
        strcat(Buffer, "<form method=\"post\" action=\"/delete/");
        strcat(Buffer, entry->d_name);
        strcat(Buffer, "\"><button type=\"submit\">Delete</button></form>");
        strcat(Buffer, "</td></tr>\n");
        len = strlen(Buffer);

        if (httpd_resp_send_chunk(req, Buffer, len) != ESP_OK)
        {
            return ESP_FAIL;
        }
    }

    closedir(dir);

    return ESP_OK;
}

// Proccess Dynamic Upload code with directory
esp_err_t http_dynamic_upload(httpd_req_t* req)
{
    char dirpath[32];
    char filepath[FILE_PATH_MAX];
    FILE* fd = NULL;
    int len;
    char* dynamic;

    strcpy(dirpath, ((struct file_server_data*)req->user_ctx)->base_path);
    strcat(dirpath, "/");

    strcpy(filepath, dirpath);
    strcat(filepath, "Directory.html");
    fd = fopen(filepath, "r");
    if (!fd)
    {
        ESP_LOGE(TAG, "Dynamic file not found : %s", filepath);
        /* Respond with 500 Internal Server Error */
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to read dynamic file");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "text/html");

    /* Retrieve the pointer to scratch buffer for temporary storage */
    char* chunk = ((struct file_server_data*)req->user_ctx)->scratch;
    size_t chunksize = 0;
    do
    {
        /* Read file in chunks into the scratch buffer */
        chunksize = fread(chunk, 1, SCRATCH_BUFSIZE, fd);

        if (chunksize > 0)
        {
            chunk[chunksize] = 0;
            dynamic = strstr(chunk, "<DYNAMIC>");
            if (dynamic != NULL)
            {
                len = dynamic - chunk;
                if (httpd_resp_send_chunk(req, chunk, len) != ESP_OK)
                {
                    fclose(fd);
                    ESP_LOGE(TAG, "File sending failed for start!");
                    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to send file");
                    return ESP_FAIL;
                }
                if (doDirectory(req, "/") != ESP_OK)
                {
                    fclose(fd);
                    ESP_LOGE(TAG, "Directory Failed!");
                    return ESP_FAIL;
                }
                dynamic = strstr(dynamic, "</DYNAMIC>");
                if (dynamic == NULL)
                {
                    fclose(fd);
                    ESP_LOGE(TAG, "Dynamic end tag not found!");
                    return ESP_FAIL;
                }
                dynamic += 10;
                len = strlen(dynamic);
                if (httpd_resp_send_chunk(req, dynamic, len) != ESP_OK)
                {
                    fclose(fd);
                    ESP_LOGE(TAG, "File sending failed for end!");
                    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to send file");
                    return ESP_FAIL;
                }
            }
            else
            {
                /* Send the buffer contents as HTTP response chunk */
                if (httpd_resp_send_chunk(req, chunk, chunksize) != ESP_OK)
                {
                    fclose(fd);
                    ESP_LOGE(TAG, "File sending failed!");
                    /* Abort sending file */
                    httpd_resp_sendstr_chunk(req, NULL);
                    /* Respond with 500 Internal Server Error */
                    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to send file");
                    return ESP_FAIL;
                }
            }
        }

        /* Keep looping till the whole file is sent */
    } while (chunksize != 0);

    /* Close file after sending complete */
    fclose(fd);
    //ESP_LOGI(TAG, "File sending complete");

    /* Send empty chunk to signal HTTP response completion */
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

/* Send HTTP response with a run-time generated html consisting of
 * a list of all files and folders under the requested path.
 * In case of SPIFFS this returns empty list when path is any
 * string other than '/', since SPIFFS doesn't support directories */
static esp_err_t http_resp_dir_html(httpd_req_t* req, const char* dirpath)
{

    /* Get handle to embedded file upload script */
    extern const unsigned char upload_script_start[] asm("_binary_upload_script_html_start");
    extern const unsigned char upload_script_end[]   asm("_binary_upload_script_html_end");
    const size_t upload_script_size = (upload_script_end - upload_script_start);

    unsigned char* dynamic = (unsigned char*)strstr((const char*)upload_script_start, "<DYNAMIC>");
    if (dynamic != NULL)
    {
        int size = dynamic - upload_script_start;
        httpd_resp_send_chunk(req, (const char*)upload_script_start, size);
    }
    else
    {
        /* Add file upload form and script which on execution sends a POST request to /upload */
        httpd_resp_send_chunk(req, (const char*)upload_script_start, upload_script_size);
    }

    if (doDirectory(req, dirpath) != ESP_OK)
    {
        ESP_LOGW(TAG, "Directory Failed!");
    }

    /* Send remaining chunk of HTML file to complete it */
    httpd_resp_sendstr_chunk(req, "</table></div></div></body></html>");

    /* Send empty chunk to signal HTTP response completion */
    httpd_resp_sendstr_chunk(req, NULL);
    return ESP_OK;
}

#define IS_FILE_EXT(filename, ext) (strcasecmp(&filename[strlen(filename) - sizeof(ext) + 1], ext) == 0)

/* Set HTTP response content type according to file extension */
static esp_err_t set_content_type_from_file(httpd_req_t* req, const char* filename)
{
    if (IS_FILE_EXT(filename, ".pdf")) {
        return httpd_resp_set_type(req, "application/pdf");
    }
    else if (IS_FILE_EXT(filename, ".htm")) {
        return httpd_resp_set_type(req, "text/html");
    }
    else if (IS_FILE_EXT(filename, ".html")) {
        return httpd_resp_set_type(req, "text/html");
    }
    else if (IS_FILE_EXT(filename, ".jpeg")) {
        return httpd_resp_set_type(req, "image/jpeg");
    }
    else if (IS_FILE_EXT(filename, ".ico")) {
        return httpd_resp_set_type(req, "image/x-icon");
    }
    else if (IS_FILE_EXT(filename, ".css")) {
        return httpd_resp_set_type(req, "text/css");
    }
    else if (IS_FILE_EXT(filename, ".js")) {
        return httpd_resp_set_type(req, "text/javascript");
    }
    else if (IS_FILE_EXT(filename, ".txt")) {
        return httpd_resp_set_type(req, "text/plain");
    }
    else if (IS_FILE_EXT(filename, ".jpg")) {
        return httpd_resp_set_type(req, "image/jpeg");
    }
    else if (IS_FILE_EXT(filename, ".png")) {
        return httpd_resp_set_type(req, "image/png");
    }
    /* This is a limited set only */
    /* For any other type always set as plain text */
    return httpd_resp_set_type(req, "text/plain");
}

/* Copies the full path into destination buffer and returns
 * pointer to path (skipping the preceding base path) */
static const char* get_path_from_uri(char* dest, const char* base_path, const char* uri, size_t destsize)
{
    const size_t base_pathlen = strlen(base_path);
    size_t pathlen = strlen(uri);

    const char* quest = strchr(uri, '?');
    if (quest)
    {
        pathlen = MIN(pathlen, quest - uri);
    }
    const char* hash = strchr(uri, '#');
    if (hash)
    {
        pathlen = MIN(pathlen, hash - uri);
    }

    if (base_pathlen + pathlen + 1 > destsize)
    {
        /* Full path string won't fit into destination buffer */
        return NULL;
    }

    /* Construct full path (base + path) */
    strcpy(dest, base_path);
    strlcpy(dest + base_pathlen, uri, pathlen + 1);

    /* Return pointer to path, skipping the base */
    return dest + base_pathlen;
}

/* Handler file request */
static esp_err_t handleRequests(httpd_req_t* req)
{
    char *m;
    int p;
    char filepath[FILE_PATH_MAX];
    FILE* fd = NULL;
    struct stat file_stat;

    const char* filename = get_path_from_uri(filepath, ((struct file_server_data*)req->user_ctx)->base_path,
        req->uri, sizeof(filepath));

    if (!filename)
    {
        ESP_LOGE(TAG, "Filename is too long");
        /* Respond with 500 Internal Server Error */
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Filename too long");
        return ESP_FAIL;
    }

    if (strcmp(req->uri, "/") == 0)
    {
        return redirect(req);
    }

    /* If name is /directory then do directory*/
    if (strcmp(req->uri, "/directory") == 0)
    {
        return http_resp_dir_html(req, "/");
    }

    /* process user added uri */
    for (int i=0;i<10;i++)
    {
        if (*UsrReq[i].uri == '\0')
            continue;

        m = strchr(UsrReq[i].uri, '*');
        if (m != NULL)
            p = m - UsrReq[i].uri;
        else
            p = strlen(UsrReq[i].uri);
        
        if (memcmp(req->uri, UsrReq[i].uri, p) == 0)
        {
            ESP_LOGI(TAG, "Found URI: %s", UsrReq[i].uri);
            UsrReq[i].hd = req->handle;
            UsrReq[i].fd = httpd_req_to_sockfd(req);
            UsrReq[i].method = req->method;
            if (req->method == HTTP_GET)
            {
                httpd_req_get_url_query_str(req, UsrReq[i].vars, sizeof(UsrReq[i].vars));
                if (flashConfig.events != 0)
                    sendResponseP('G', i, 0);
            }
            if (req->method == HTTP_POST)
            {
                httpd_req_recv(req, UsrReq[i].vars, sizeof(UsrReq[i].vars));
                if (flashConfig.events != 0)
                    sendResponseP('P', i, 0);
            }
            ESP_LOGI(TAG, "Vars:%s", UsrReq[i].vars);
            return ESP_OK;
        }
    }

    if (stat(filepath, &file_stat) == -1) 
    {
        /* If file not present on SPIFFS check if URI
         * corresponds to one of the hardcoded paths */

        ESP_LOGE(TAG, "Failed to stat file : %s", filepath);
        /* Respond with 404 Not Found */
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File does not exist");
        return ESP_FAIL;
    }

    fd = fopen(filepath, "r");
    if (!fd)
    {
        ESP_LOGE(TAG, "Failed to read existing file : %s", filepath);
        /* Respond with 500 Internal Server Error */
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to read existing file");
        return ESP_FAIL;
    }

    //ESP_LOGI(TAG, "Sending file : %s (%ld bytes)...", filename, file_stat.st_size);
    set_content_type_from_file(req, filename);

    /* Retrieve the pointer to scratch buffer for temporary storage */
    char* chunk = ((struct file_server_data*)req->user_ctx)->scratch;
    size_t chunksize = 0;
    do 
    {
        /* Read file in chunks into the scratch buffer */
        chunksize = fread(chunk, 1, SCRATCH_BUFSIZE, fd);

        if (chunksize > 0)
        {
            /* Send the buffer contents as HTTP response chunk */
            if (httpd_resp_send_chunk(req, chunk, chunksize) != ESP_OK)
            {
                fclose(fd);
                ESP_LOGE(TAG, "File sending failed!");
                /* Abort sending file */
                httpd_resp_sendstr_chunk(req, NULL);
                /* Respond with 500 Internal Server Error */
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to send file");
                return ESP_FAIL;
            }
        }

        /* Keep looping till the whole file is sent */
    } while (chunksize != 0);

    /* Close file after sending complete */
    fclose(fd);
    //ESP_LOGI(TAG, "File sending complete");

    /* Respond with an empty chunk to signal HTTP response completion */
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

/* Handler to upload a file onto the server */
static esp_err_t upload_post_handler(httpd_req_t* req)
{
    char filepath[FILE_PATH_MAX];
    FILE* fd = NULL;
    struct stat file_stat;

    /* Skip leading "/upload" from URI to get filename */
    /* Note sizeof() counts NULL termination hence the -1 */
    const char* filename = get_path_from_uri(filepath, ((struct file_server_data*)req->user_ctx)->base_path,
        req->uri + sizeof("/upload") - 1, sizeof(filepath));
    if (!filename)
    {
        /* Respond with 500 Internal Server Error */
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Filename too long");
        return ESP_FAIL;
    }

    /* Filename cannot have a trailing '/' */
    if (filename[strlen(filename) - 1] == '/')
    {
        ESP_LOGE(TAG, "Invalid filename : %s", filename);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Invalid filename");
        return ESP_FAIL;
    }

    if (stat(filepath, &file_stat) == 0)
    {
        ESP_LOGE(TAG, "File already exists : %s", filepath);
        /* Respond with 400 Bad Request */
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "File already exists");
        return ESP_FAIL;
    }

    /* File cannot be larger than a limit */
    if (req->content_len > MAX_FILE_SIZE)
    {
        ESP_LOGE(TAG, "File too large : %d bytes", req->content_len);
        /* Respond with 400 Bad Request */
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
            "File size must be less than "
            MAX_FILE_SIZE_STR "!");
        /* Return failure to close underlying connection else the
         * incoming file content will keep the socket busy */
        return ESP_FAIL;
    }

    fd = fopen(filepath, "w");
    if (!fd)
    {
        ESP_LOGE(TAG, "Failed to create file : %s", filepath);
        /* Respond with 500 Internal Server Error */
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to create file");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Receiving file : %s...", filename);

    /* Retrieve the pointer to scratch buffer for temporary storage */
    char* buf = ((struct file_server_data*)req->user_ctx)->scratch;
    int received;

    /* Content length of the request gives
     * the size of the file being uploaded */
    int remaining = req->content_len;

    while (remaining > 0)
    {
        ESP_LOGI(TAG, "Remaining size : %d", remaining);
        /* Receive the file part by part into a buffer */
        if ((received = httpd_req_recv(req, buf, MIN(remaining, SCRATCH_BUFSIZE))) <= 0)
        {
            if (received == HTTPD_SOCK_ERR_TIMEOUT)
            {
                /* Retry if timeout occurred */
                continue;
            }

            /* In case of unrecoverable error,
             * close and delete the unfinished file*/
            fclose(fd);
            unlink(filepath);

            ESP_LOGE(TAG, "File reception failed!");
            /* Respond with 500 Internal Server Error */
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to receive file");
            return ESP_FAIL;
        }

        /* Write buffer content to file on storage */
        if (received && (received != fwrite(buf, 1, received, fd)))
        {
            /* Couldn't write everything to file!
             * Storage may be full? */
            fclose(fd);
            unlink(filepath);

            ESP_LOGE(TAG, "File write failed!");
            /* Respond with 500 Internal Server Error */
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to write file to storage");
            return ESP_FAIL;
        }

        /* Keep track of remaining size of
         * the file left to be uploaded */
        remaining -= received;
    }

    /* Close file upon upload completion */
    fclose(fd);
    ESP_LOGI(TAG, "File reception complete");

    return redirect(req);
}

/* Handler to delete a file from the server */
static esp_err_t delete_post_handler(httpd_req_t* req)
{
    char filepath[FILE_PATH_MAX];
    struct stat file_stat;

    /* Skip leading "/delete" from URI to get filename */
    /* Note sizeof() counts NULL termination hence the -1 */
    const char* filename = get_path_from_uri(filepath, ((struct file_server_data*)req->user_ctx)->base_path,
        req->uri + sizeof("/delete") - 1, sizeof(filepath));
    if (!filename)
    {
        /* Respond with 500 Internal Server Error */
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Filename too long");
        return ESP_FAIL;
    }

    /* Filename cannot have a trailing '/' */
    if (filename[strlen(filename) - 1] == '/')
    {
        ESP_LOGE(TAG, "Invalid filename : %s", filename);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Invalid filename");
        return ESP_FAIL;
    }

    if (stat(filepath, &file_stat) == -1)
    {
        ESP_LOGE(TAG, "File does not exist : %s", filename);
        /* Respond with 400 Bad Request */
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "File does not exist");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Deleting file : %s", filename);
    /* Delete file */
    unlink(filepath);

    return redirect(req);
}

static esp_err_t propModuleInfo(httpd_req_t* req)
{
    char *buffer;
    char value[32];
    uint8_t Mac[6];
    esp_netif_t* nf = NULL;
    esp_netif_ip_info_t info;

    buffer = ((struct file_server_data*)req->user_ctx)->scratch;
    memset(buffer, 0, SCRATCH_BUFSIZE);
    json_init(buffer);
    json_putStr("name", flashConfig.module_name);
    nf = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_get_ip_info(nf, &info);
    esp_ip4addr_ntoa(&info.ip, value, 16);
    json_putStr("sta-ipaddr", value);
    esp_wifi_get_mac(0, Mac);
    sprintf(value, "%02x:%02x:%02x:%02x:%02x:%02x", Mac[0], Mac[1], Mac[2], Mac[3], Mac[4], Mac[5]);
    json_putStr("sta-macaddr", value);
    nf = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    esp_netif_get_ip_info(nf, &info);
    esp_ip4addr_ntoa(&info.ip, value, 16);
    json_putStr("softap-ipaddr", value);
    esp_wifi_get_mac(1, Mac);
    sprintf(value, "%02x:%02x:%02x:%02x:%02x:%02x", Mac[0], Mac[1], Mac[2], Mac[3], Mac[4], Mac[5]);
    json_putStr("softap-macaddr", value);
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_sendstr(req, buffer);
    return ESP_OK;
}

static esp_err_t propSaveSettings(httpd_req_t* req)
{
    if (configSave() != 0)
    {
        ESP_LOGE(TAG, "Save Settings Failed");
        httpd_resp_send_err(req, 400, "Save Settings Failed\r\n");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Settings Saved");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t propRestoreSettings(httpd_req_t* req)
{
    if (configRestore() != 0)
    {
        ESP_LOGE(TAG, "Restore Settings Failed");
        httpd_resp_send_err(req, 400, "Restore Settings Failed\r\n");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Settings Restored");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t propRestoreDefaultSettings(httpd_req_t* req)
{
    if (configRestoreDefaults() != 0)
    {
        ESP_LOGE(TAG, "Restore Default Settings Failed");
        httpd_resp_send_err(req, 400, "Restore Default Settings Failed\r\n");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Default Settings Restored");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t ajaxLog(httpd_req_t* req)
{
    char buff[MAX_LOGS + 128];
    char *output;
    char c;
    int i;

    int log_len = log_head - log_tail;
    if (log_len < 0)
        log_len += MAX_LOGS;

    output = ((struct file_server_data*)req->user_ctx)->scratch;
    // start outputting
    i = 0;
    while (log_head != log_tail)
    {
        c = log_buf[log_tail++];
        log_tail = log_tail & (MAX_LOGS-1);
        switch (c)
        {
        case '\\':
            buff[i++] = '\\';
            buff[i++] = '\\';
            break;
        case '\r':
            buff[i++] = '\\';
            buff[i++] = 'r';
            break;
        case '\n':
            buff[i++] = '\\';
            buff[i++] = 'n';
            break;
        case '\t':
            buff[i++] = '\\';
            buff[i++] = 't';
            break;
        default:
            buff[i++] = c;
        }
    }

    buff[i] = 0;
    sprintf(output, "{\"len\":%d, \"start\":0, \"text\": \"", i);
    strcat(output, buff);
    strcat(output, "\"}");
    i = strlen(output);
    sprintf(buff, "%d", i);

    ESP_LOGI(TAG, "Outputing Data, %d", i);

    httpd_resp_set_type(req, "text/plain");
    httpd_resp_send(req, output, i);
    return ESP_OK;
}

esp_err_t logData(char d)
{
    log_buf[log_head++] = d;
    log_head = log_head & (MAX_LOGS-1);

    if (log_head == log_tail)
        log_tail++;
    
    log_tail = log_tail & (MAX_LOGS-1);

    return ESP_OK;
}

// Log capture function
int vlogData(const char* format, va_list valist)
{
    char LogBuffer[132];
    int i;
    int skip;
    int len;

    len = vsprintf(LogBuffer, format, valist);

    skip = 0;
    for (i = 0; i < len; i++)
    {
        if (LogBuffer[i] == 0x1b)
            skip = 1;
        if (skip == 0)
            logData(LogBuffer[i]);
        if (skip == 1)
            if (LogBuffer[i] == 'm')
                skip = 0;
    }

    return vprintf(format, valist);
}

//Setup log file and install log capture
esp_err_t logInit()
{

    log_head = 0;
    log_tail = 0;

    esp_log_set_vprintf(vlogData);

    return ESP_OK;
}

//Find access points
esp_err_t WiFiScan(httpd_req_t* req)
{
    char *Buffer;
    char size[16];
    int i;
    uint16_t number = DEFAULT_SCAN_LIST_SIZE;
    wifi_ap_record_t ap_info[DEFAULT_SCAN_LIST_SIZE];

    Buffer = ((struct file_server_data*)req->user_ctx)->scratch;
    httpd_req_get_url_query_str(req, Buffer, SCRATCH_BUFSIZE);

    uint16_t ap_count = 0;
    memset(ap_info, 0, sizeof(ap_info));

    if (!statusIsConnecting())
    {
        ESP_ERROR_CHECK(esp_wifi_scan_start(NULL, true));
        ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&number, ap_info));
        ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(&ap_count));
        ESP_LOGI(TAG, "Total APs found = %u", ap_count);
    }
    else
        number = 0;

    memset(Buffer, 0, SCRATCH_BUFSIZE);

    json_init(Buffer);
    json_putObject("result");
    if (number == 0)
        json_putStr("inProgress", "1");
    else
        json_putStr("inProgress", "0");

    json_putArray("APs");
    for (i = 0; i < number; i++)
    {
        if (i != 0)
            json_putMore();
        json_putStr("essid", (char*)ap_info[i].ssid);
        json_putStr("bssid", "00:00:00:00:00:00");
        json_putDec("rssi", itoa(ap_info[i].rssi, size, 10));
        json_putDec("enc", itoa(ap_info[i].authmode, size, 10));
        json_putDec("channel", itoa(ap_info[1].primary, size, 10));
    }
    if (number == 0)
        json_putStr("none", "");
    json_putArray(NULL);
    json_putObject(NULL);
    i = strlen(Buffer);
    sprintf(size, "%d", i);
    httpd_resp_set_type(req, "text/plain");
    //httpd_resp_set_hdr(req, "Content-Length", size);
    httpd_resp_send(req, Buffer, i);
    return ESP_OK;
}

esp_err_t WiFiConnStatus(httpd_req_t* req)
{
    char *Buffer;
    char value[128];
    int status;
    esp_netif_t* nf = NULL;
    esp_netif_ip_info_t info;

    status = statusGet();

    Buffer = ((struct file_server_data*)req->user_ctx)->scratch;
    memset(Buffer, 0, SCRATCH_BUFSIZE);
    json_init(Buffer);
    switch (status)
    {
    case 0:
        json_putStr("status", "idle");
        break;
    case 1:
        json_putStr("status", "connecting");
        break;
    case 2:
        nf = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        esp_netif_get_ip_info(nf, &info);
        esp_ip4addr_ntoa(&info.ip, value, 16);
        json_putStr("status", "success");
        json_putStr("ip", value);
        break;
    }
    status = strlen(Buffer);
    sprintf(value, "%d", status);

    httpd_resp_set_type(req, "text/plain");
    //httpd_resp_set_hdr(req, "Content-Length", value);
    httpd_resp_send(req, Buffer, status);
    return ESP_OK;
}


esp_err_t WiFiConnect(httpd_req_t* req)
{
    int i;
    char *Buffer;
    char essid[32];
    char passwd[128];
    wifi_config_t wifi_config;

    Buffer = ((struct file_server_data*)req->user_ctx)->scratch;
    memset(Buffer, 0, SCRATCH_BUFSIZE);

    i = httpd_req_recv(req, Buffer, SCRATCH_BUFSIZE);
    ESP_LOGI(TAG, "Post size: %d", i);

    if (findArg(Buffer, "essid", essid) != 0)
    {
        ESP_LOGW(TAG, "Argument 'essid' not found");
        httpd_resp_send_err(req, 400, "Missing name argument\r\n");
        return ESP_OK;
    }
    ESP_LOGI(TAG, "essid: %s", essid);

    if (findArg(Buffer, "passwd", passwd) != 0)
    {
        ESP_LOGW(TAG, "Argument 'passwd' not found");
        httpd_resp_send_err(req, 400, "Missing name argument\r\n");
        return ESP_OK;
    }
    ESP_LOGI(TAG, "passwd: %s", passwd);

    statusDisconnect();
    esp_wifi_get_config(ESP_IF_WIFI_STA, &wifi_config);
    strcpy((char*)wifi_config.sta.ssid, essid);
    strcpy((char*)wifi_config.sta.password, passwd);
    esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config);
    statusConnect();
    
    return redirect(req);
}

esp_err_t WiFiSetMode(httpd_req_t* req)
{
    char *Buffer;
    char value[128];
    wifi_mode_t mode;
    wifi_mode_t x;

    Buffer = ((struct file_server_data*)req->user_ctx)->scratch;
    httpd_req_get_url_query_str(req, Buffer, SCRATCH_BUFSIZE);
    findArg(Buffer, "mode", value);

    if (strcmp(value, "STA") == 0)
        x = WIFI_MODE_STA;
    else if (strcmp(value, "AP") == 0)
        x = WIFI_MODE_AP;
    else if (strcmp(value, "APSTA") == 0)
        x = WIFI_MODE_APSTA;
    else if (isdigit((int)value[0]))
        x = atoi(value);
    else
        return -1;

    switch (x)
    {
    case WIFI_MODE_STA:
        ESP_LOGW(TAG, "Entering STA mode");
        break;
    case WIFI_MODE_AP:
        ESP_LOGW(TAG, "Entering AP mode");
        break;
    case WIFI_MODE_APSTA:
        ESP_LOGW(TAG, "Entering APSTA mode");
        break;
    default:
        ESP_LOGW(TAG, "Unknown wi-fi mode: %d", mode);
        return -1;
    }

    esp_wifi_get_mode(&mode);

    if (x != mode)
        esp_wifi_set_mode(x);

    httpd_resp_set_type(req, "text/plain");
    //httpd_resp_set_hdr(req, "Content-Length", "0");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

esp_err_t PropReset(httpd_req_t* req)
{
    statusReset();

    ESP_LOGI(TAG, "Prop Reset");
    httpd_resp_set_type(req, "text/plain");
    //httpd_resp_set_hdr(req, "Content-Length", "0");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

esp_err_t PropLoad(httpd_req_t* req)
{
    char *Buffer;

    Buffer = ((struct file_server_data*)req->user_ctx)->scratch;
    httpd_req_get_url_query_str(req, Buffer, SCRATCH_BUFSIZE);
    printf(Buffer);
    printf("\r\n");
    httpd_req_recv(req, Buffer, SCRATCH_BUFSIZE);
    printf(Buffer);
    printf("\r\n");

    httpd_resp_set_type(req, "text/plain");
    //httpd_resp_set_hdr(req, "Content-Length", "0");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

/*
This is the main url->function dispatching data struct.
In short, it's a struct with various URLs plus their handlers. The handlers can
be 'standard' CGI functions you wrote, or 'special' CGIs requiring an argument.
They can also be auth - functions.An asterisk will match any url starting with
everything before the asterisks; "*" matches everything.The list will be
handled top - down, so make sure to put more specific rules above the more
general ones.Authorization things(like authBasic) act as a 'barrier' and
should be placed above the URLs they protect.
*/
HttpdBuiltInUrl builtInUrls[] = {
    {"/upload/*", HTTP_POST, upload_post_handler},
    {"/delete/*", HTTP_POST, delete_post_handler},
    {"/wx/module-info", HTTP_GET, propModuleInfo},
    {"/wx/setting", HTTP_GET, PropSettings},
    {"/wx/setting", HTTP_POST, PropSettings},
    {"/wx/save-settings", HTTP_POST, propSaveSettings},
    {"/wx/restore-settings", HTTP_POST, propRestoreSettings},
    {"/wx/restore-default-settings", HTTP_POST, propRestoreDefaultSettings},
    {"/wifi/", HTTP_GET, redirect},
    {"/wifi", HTTP_GET, redirect},
    {"/websocket", HTTP_GET, redirect},
    {"/websocket/", HTTP_GET, redirect},
    {"/log/text", HTTP_GET, ajaxLog},
    {"/wifi/wifiscan", HTTP_GET, WiFiScan},
    {"/wifi/connect", HTTP_POST, WiFiConnect},
    {"/wifi/connstatus", HTTP_GET, WiFiConnStatus},
    {"/wifi/setmode", HTTP_POST, WiFiSetMode},
    {"/propeller/reset", HTTP_POST, PropReset},
    {"/propeller/reset", HTTP_GET, PropReset},
    {"/propeller/load", HTTP_POST, PropLoad},
    {"/dynamic", HTTP_GET, http_dynamic_upload},
/*
{"/flash/reboot", cgiRebootFirmware, NULL},

        {"/websocket/ws.cgi", cgiWebsocket, myWebsocketConnect},
        {"/websocket/echo.cgi", cgiWebsocket, myEchoWebsocketConnect},

        //Routines to make the /wifi URL and everything beneath it work.

    //Enable the line below to protect the WiFi configuration with an username/password combo.
    //	{"/wifi/\*", authBasic, myPassFn},

        { "/userfs/format", cgiRoffsFormat, NULL },
        { "/userfs/write", cgiRoffsWriteFile, NULL },
        { "/propeller/load", cgiPropLoad, NULL },
        { "/propeller/load-file", cgiPropLoadFile, NULL },
        { "/propeller/reset", cgiPropReset, NULL },
        { "/files/\*", cgiRoffsHook, NULL }, //Catch-all cgi function for the flash filesystem
        { "/ws/\*", cgiWebsocket, sscp_websocketConnect},
 */
    {"/*", HTTP_GET, handleRequests},
    {"/*", HTTP_POST, handleRequests},
    {NULL, 0, NULL}
};

void doGet(char* parms)
{
    char* s;
    cmd_def* def = NULL;
    char value[128];

    s = &parms[1];

    for (int i = 0; vars[i].name != NULL; i++)
    {
        if (strcmp(s, vars[i].name) == 0)
        {
            def = &vars[i];
            break;
        }
    }

    if (def == NULL)
    {
        sendResponse('E', ERROR_UNIMPLEMENTED);
        return;
    }

    if ((*def->getHandler)(def->data, value) != 0)
    {
        sendResponse('E', ERROR_INVALID_ARGUMENT);
        return;
    }

    sendResponseT(value);
}

void doSet(char* parms)
{
    char* p, *s;
    cmd_def* def = NULL;

    s = &parms[1];
    p = strchr(s, ',');
    *p = 0;
    p++;

    for (int i = 0; vars[i].name != NULL; i++)
    {
        if (strcmp(s, vars[i].name) == 0)
        {
            def = &vars[i];
            break;
        }
    }

    if (def == NULL)
    {
        sendResponse('E', ERROR_UNIMPLEMENTED);
        return;
    }

    if ((*def->setHandler)(def->data, p) != 0)
    {
        sendResponse('E', ERROR_INVALID_ARGUMENT);
        return;
    }

    sendResponse('S', ERROR_NONE);
}

esp_err_t register_uri(char *uri)
{
    for (int i=0;i<10;i++)
    {
        if (UsrReq[i].uri[0] == '\0')
        {
            strcpy(UsrReq[i].uri, uri);
            UsrReq[i].fd = -1;
            UsrReq[i].method = ' ';
            return ESP_OK;
        }
    }
    return ESP_ERR_HTTPD_INVALID_REQ;
}

esp_err_t handleReply(int handle, char *code, int tcount, int count)
{
    httpd_handle_t hd;
    int fd;
    char Buffer[1024];
    int i, t;

    hd = UsrReq[handle].hd;
    fd = UsrReq[handle].fd;

    t = 0;

    i = sprintf(Buffer, "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: %d\r\n\r\n", count);
    i = httpd_socket_send(hd, fd, Buffer, i, 0);

    while (count > t)
    {
        i = uart_read_bytes(UART_NUM_1, Buffer, count, 500 / portTICK_PERIOD_MS);
        if (i > 0)
            t = t + i;
    }
    
    i = httpd_socket_send(hd, fd, Buffer, count, 0);

    UsrReq[handle].fd = -1;
    UsrReq[handle].method = ' ';

    return ESP_OK;
}

esp_err_t getVar(int handle, char *name, char *value)
{

    if (findArg(UsrReq[handle].vars, name, value) != 0)
    {
        *value = 0;
    }

    return ESP_OK;
}

esp_err_t polling(int filter)
{
    int results;

    results = 0;

    for (int i=0;i<10;i++)
    {
        if (UsrReq[i].fd >= 0)
            if ((filter & (1 << i)) != 0)
                sendResponseP(UsrReq[i].method, UsrReq[i].fd, 0);
    }

    if (results == 0)
        sendResponseP('N', 0, 0);

    return ESP_OK;
}

/* Function to start the HTTP server */
esp_err_t httpdInit(int port)
{
  httpd_uri_t hd;
  int i;

  if (server_data)
  {
    ESP_LOGE(TAG, "HTTP server already started");
    return ESP_ERR_INVALID_STATE;
  }

  /* Allocate memory for server data */
  server_data = calloc(1, sizeof(struct file_server_data));
  if (!server_data)
  {
    ESP_LOGE(TAG, "Failed to allocate memory for server data");
    return ESP_ERR_NO_MEM;
  }

  strlcpy(server_data->base_path, "/spiffs", sizeof(server_data->base_path));

  config.server_port = port;
  config.max_uri_handlers = MAXHANDLERS;
  config.uri_match_fn = httpd_uri_match_wildcard;

  ESP_LOGI(TAG, "Starting HTTP Server");
  if (httpd_start(&server, &config) != ESP_OK)
  {
    ESP_LOGE(TAG, "Failed to start file server!");
    return ESP_FAIL;
  }

  i = 0;
  while (builtInUrls[i].url != NULL)
  {
      hd.uri = builtInUrls[i].url;
      hd.method = builtInUrls[i].meth;
      hd.handler = builtInUrls[i].handler;
      hd.user_ctx = server_data;

      httpd_register_uri_handler(server, &hd);
      i++;
  }

  memset(UsrReq, 0, sizeof(UsrReq));

  return ESP_OK;
}

esp_err_t httpRestart()
{
  httpd_uri_t hd;
  int i;

  httpd_stop(server);

  ESP_LOGI(TAG, "Restarting HTTP Server");
  if (httpd_start(&server, &config) != ESP_OK)
  {
    ESP_LOGE(TAG, "Failed to start file server!");
    return ESP_FAIL;
  }

  i = 0;
  while (builtInUrls[i].url != NULL)
  {
      hd.uri = builtInUrls[i].url;
      hd.method = builtInUrls[i].meth;
      hd.handler = builtInUrls[i].handler;
      hd.user_ctx = server_data;

      httpd_register_uri_handler(server, &hd);
      i++;
  }

  memset(UsrReq, 0, sizeof(UsrReq));

  return ESP_OK;
}