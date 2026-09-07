#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <curl/curl.h>

#define MAX_ATTR_LEN 2048
#define MIN_TEXT_LEN 25 // حداقل طول متن برای چاپ شدن

typedef enum {
    STATE_OUTSIDE,
    STATE_MATCH_TAG,
    STATE_WAIT_TAG_CLOSE,
    STATE_IN_TEXT,
    STATE_IN_INNER_TAG,
    STATE_IN_IMG,
    STATE_IN_VIDEO,
    STATE_SKIP_TAG
} ParserState;

static char current_domain[256] = "";
static char text_buf[16384];
static int text_idx = 0;
static char active_tag_type[8] = ""; // برای تشخیص p, h1, h2, h3, li

// فیلتر هوشمند تصاویر بی‌ارزش، آیکون‌ها و فایل‌های SVG
int is_valid_media_url(const char *url) {
    if (strstr(url, ".svg") != NULL) return 0;
    if (strstr(url, "/math/render/") != NULL) return 0;
    if (strstr(url, "OOjs_UI_icon") != NULL) return 0;
    if (strstr(url, "20px-") != NULL) return 0;
    if (strstr(url, "25px-") != NULL) return 0;
    if (strstr(url, "40px-") != NULL) return 0;
    if (strstr(url, "CentralAutoLogin") != NULL) return 0;
    if (strstr(url, "wikimedia-button") != NULL) return 0;
    return 1;
}

void extract_and_print_src(const char *tag_buf, const char *tag_type) {
    const char *src_ptr = strstr(tag_buf, "src=\"");
    if (!src_ptr) src_ptr = strstr(tag_buf, "src='");

    if (src_ptr) {
        src_ptr += 5;
        char url[MAX_ATTR_LEN];
        int idx = 0;

        while (*src_ptr && *src_ptr != '"' && *src_ptr != '\'' && idx < MAX_ATTR_LEN - 1) {
            url[idx++] = *src_ptr++;
        }
        url[idx] = '\0';

        if (strlen(url) > 0 && is_valid_media_url(url)) {
            if (strncmp(url, "//", 2) == 0) {
                printf("\n  ↳ [%s]: https:%s", tag_type, url);
            } else if (url[0] == '/') {
                printf("\n  ↳ [%s]: %s%s", tag_type, current_domain, url);
            } else {
                printf("\n  ↳ [%s]: %s", tag_type, url);
            }
        }
    }
}

void flush_text_buffer() {
    if (text_idx >= MIN_TEXT_LEN || (strcmp(active_tag_type, "h1") == 0 || strcmp(active_tag_type, "h2") == 0 || strcmp(active_tag_type, "h3") == 0)) {
        text_buf[text_idx] = '\0';
        
        printf("\n\n--------------------------------------------------\n");
        if (strcmp(active_tag_type, "h1") == 0) printf("# ");
        else if (strcmp(active_tag_type, "h2") == 0) printf("## ");
        else if (strcmp(active_tag_type, "h3") == 0) printf("### ");
        else if (strcmp(active_tag_type, "li") == 0) printf("* ");
        else printf("[TEXT]: ");

        printf("%s", text_buf);
    }
    text_idx = 0;
    active_tag_type[0] = '\0';
}

void parse_and_extract(const char *buffer, size_t length) {
    static ParserState state = STATE_OUTSIDE;
    static char tag_name[32];
    static int tag_name_len = 0;
    static char attr_buffer[MAX_ATTR_LEN];
    static int attr_len = 0;
    static int inside_block = 0;
    static int skip_depth = 0;

    for (size_t i = 0; i < length; i++) {
        char c = buffer[i];

        if (state == STATE_SKIP_TAG) {
            if (c == '>') {
                if (skip_depth > 0) skip_depth--;
                if (skip_depth == 0) state = inside_block ? STATE_IN_TEXT : STATE_OUTSIDE;
            }
            continue;
        }

        switch (state) {
            case STATE_OUTSIDE:
                if (c == '<') {
                    state = STATE_MATCH_TAG;
                    tag_name_len = 0;
                }
                break;

            case STATE_IN_TEXT:
                if (c == '<') {
                    state = STATE_MATCH_TAG;
                    tag_name_len = 0;
                } else {
                    if (c == '\n' || c == '\r' || c == '\t') c = ' ';
                    
                    // جلوگیری از فاصله‌های متوالی
                    if (c == ' ' && text_idx > 0 && text_buf[text_idx - 1] == ' ') {
                        break;
                    }

                    if (text_idx < (int)sizeof(text_buf) - 1) {
                        text_buf[text_idx++] = c;
                    }
                }
                break;

            case STATE_IN_INNER_TAG:
                if (c == '>') {
                    state = STATE_IN_TEXT;
                    // رفع چسبیدن کلمات: اضافه کردن فاصله هوشمند پس از خروج از تگ‌های درون متنی مثل <a>
                    if (text_idx > 0 && text_buf[text_idx - 1] != ' ') {
                        text_buf[text_idx++] = ' ';
                    }
                }
                break;

            case STATE_MATCH_TAG:
                if (isalpha((unsigned char)c) || c == '/' || isdigit((unsigned char)c)) {
                    if (tag_name_len < (int)sizeof(tag_name) - 1) {
                        tag_name[tag_name_len++] = tolower((unsigned char)c);
                    }
                } else {
                    tag_name[tag_name_len] = '\0';

                    if (strcmp(tag_name, "p") == 0 || strcmp(tag_name, "h1") == 0 || 
                        strcmp(tag_name, "h2") == 0 || strcmp(tag_name, "h3") == 0 || 
                        strcmp(tag_name, "li") == 0) {
                        
                        if (inside_block) flush_text_buffer();
                        inside_block = 1;
                        text_idx = 0;
                        strncpy(active_tag_type, tag_name, sizeof(active_tag_type) - 1);
                        state = STATE_WAIT_TAG_CLOSE;
                    } 
                    else if (strcmp(tag_name, "/p") == 0 || strcmp(tag_name, "/h1") == 0 || 
                             strcmp(tag_name, "/h2") == 0 || strcmp(tag_name, "/h3") == 0 || 
                             strcmp(tag_name, "/li") == 0) {
                        
                        inside_block = 0;
                        flush_text_buffer();
                        state = STATE_OUTSIDE;
                    } 
                    else if (strcmp(tag_name, "img") == 0) {
                        attr_len = 0;
                        state = STATE_IN_IMG;
                    } 
                    else if (strcmp(tag_name, "video") == 0) {
                        attr_len = 0;
                        state = STATE_IN_VIDEO;
                    } 
                    else if (strcmp(tag_name, "script") == 0 || strcmp(tag_name, "style") == 0 || 
                             strcmp(tag_name, "sup") == 0 || strcmp(tag_name, "table") == 0) {
                        skip_depth = 1;
                        state = STATE_SKIP_TAG;
                    } 
                    else {
                        state = inside_block ? STATE_IN_INNER_TAG : STATE_OUTSIDE;
                    }
                }
                break;

            case STATE_WAIT_TAG_CLOSE:
                if (c == '>') {
                    state = STATE_IN_TEXT;
                }
                break;

            case STATE_IN_IMG:
            case STATE_IN_VIDEO:
                if (c == '>') {
                    attr_buffer[attr_len] = '\0';
                    if (state == STATE_IN_IMG) {
                        extract_and_print_src(attr_buffer, "IMAGE");
                    } else {
                        extract_and_print_src(attr_buffer, "VIDEO");
                    }
                    state = inside_block ? STATE_IN_TEXT : STATE_OUTSIDE;
                } else {
                    if (attr_len < MAX_ATTR_LEN - 1) {
                        attr_buffer[attr_len++] = c;
                    }
                }
                break;

            default:
                break;
        }
    }
}

static size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    parse_and_extract((char *)contents, realsize);
    return realsize;
}

int main(int argc, char *argv[]) {
    char url[512];

    if (argc >= 2) {
        strncpy(url, argv[1], sizeof(url) - 1);
        url[sizeof(url) - 1] = '\0';
    } else {
        printf("=========================================\n");
        printf("         SEEKER - Web Extractor          \n");
        printf("=========================================\n");
        printf("Enter URL: ");
        if (scanf("%511s", url) != 1) return 1;
    }

    char full_url[600];
    if (strncmp(url, "http://", 7) != 0 && strncmp(url, "https://", 8) != 0) {
        snprintf(full_url, sizeof(full_url), "https://%s", url);
    } else {
        strncpy(full_url, url, sizeof(full_url) - 1);
    }

    sscanf(full_url, "%*[^:]://%255[^/]", current_domain);
    char temp_domain[300];
    snprintf(temp_domain, sizeof(temp_domain), "https://%s", current_domain);
    strcpy(current_domain, temp_domain);

    CURL *curl_handle;
    CURLcode res;

    curl_global_init(CURL_GLOBAL_ALL);
    curl_handle = curl_easy_init();

    if (!curl_handle) return 1;

    printf("\n[+] Connecting to: %s ...\n", full_url);

    curl_easy_setopt(curl_handle, CURLOPT_URL, full_url);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
    curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "Mozilla/5.0 (Windows NT 10.0; Win64; x64)");
    curl_easy_setopt(curl_handle, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl_handle, CURLOPT_SSL_VERIFYPEER, 0L);

    res = curl_easy_perform(curl_handle);

    if (res != CURLE_OK) {
        fprintf(stderr, "\n[-] Fetch failed: %s\n", curl_easy_strerror(res));
    } else {
        printf("\n\n=========================================\n");
        printf("[+] Extraction Complete!\n");
        printf("=========================================\n");
    }

    curl_cleanup:
    curl_easy_cleanup(curl_handle);
    curl_global_cleanup();

    return 0;
}
