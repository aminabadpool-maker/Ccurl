#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <curl/curl.h>

#define MAX_ATTR_LEN 2048
#define MIN_TEXT_LEN 25

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

typedef struct {
    ParserState state;
    char current_domain[256];
    char text_buf[16384];
    int text_idx;
    char active_tag_type[8];
    char tag_name[32];
    int tag_name_len;
    char attr_buffer[MAX_ATTR_LEN];
    int attr_len;
    int inside_block;
    int skip_depth;
} ParserContext;

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

void extract_and_print_src(const char *tag_buf, const char *tag_type, const char *domain) {
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
                printf("\n  ↳ [%s]: %s%s", tag_type, domain, url);
            } else {
                printf("\n  ↳ [%s]: %s", tag_type, url);
            }
        }
    }
}

void flush_text_buffer(ParserContext *ctx) {
    if (ctx->text_idx >= MIN_TEXT_LEN || (strcmp(ctx->active_tag_type, "h1") == 0 || 
        strcmp(ctx->active_tag_type, "h2") == 0 || strcmp(ctx->active_tag_type, "h3") == 0)) {
        
        ctx->text_buf[ctx->text_idx] = '\0';

        printf("\n\n--------------------------------------------------\n");
        if (strcmp(ctx->active_tag_type, "h1") == 0) printf("# ");
        else if (strcmp(ctx->active_tag_type, "h2") == 0) printf("## ");
        else if (strcmp(ctx->active_tag_type, "h3") == 0) printf("### ");
        else if (strcmp(ctx->active_tag_type, "li") == 0) printf("* ");
        else printf("[TEXT]: ");

        printf("%s", ctx->text_buf);
    }
    ctx->text_idx = 0;
    ctx->active_tag_type[0] = '\0';
}

void parse_and_extract(const char *buffer, size_t length, ParserContext *ctx) {
    for (size_t i = 0; i < length; i++) {
        char c = buffer[i];

        if (ctx->state == STATE_SKIP_TAG) {
            if (c == '>') {
                if (ctx->skip_depth > 0) ctx->skip_depth--;
                if (ctx->skip_depth == 0) ctx->state = ctx->inside_block ? STATE_IN_TEXT : STATE_OUTSIDE;
            }
            continue;
        }

        switch (ctx->state) {
            case STATE_OUTSIDE:
                if (c == '<') {
                    ctx->state = STATE_MATCH_TAG;
                    ctx->tag_name_len = 0;
                }
                break;

            case STATE_IN_TEXT:
                if (c == '<') {
                    ctx->state = STATE_MATCH_TAG;
                    ctx->tag_name_len = 0;
                } else {
                    if (c == '\n' || c == '\r' || c == '\t') c = ' ';

                    if (c == ' ' && ctx->text_idx > 0 && ctx->text_buf[ctx->text_idx - 1] == ' ') {
                        break;
                    }

                    if (ctx->text_idx < (int)sizeof(ctx->text_buf) - 1) {
                        ctx->text_buf[ctx->text_idx++] = c;
                    }
                }
                break;

            case STATE_IN_INNER_TAG:
                if (c == '>') {
                    ctx->state = STATE_IN_TEXT;
                    if (ctx->text_idx > 0 && ctx->text_buf[ctx->text_idx - 1] != ' ') {
                        ctx->text_buf[ctx->text_idx++] = ' ';
                    }
                }
                break;

            case STATE_MATCH_TAG:
                if (isalpha((unsigned char)c) || c == '/' || isdigit((unsigned char)c)) {
                    if (ctx->tag_name_len < (int)sizeof(ctx->tag_name) - 1) {
                        ctx->tag_name[ctx->tag_name_len++] = tolower((unsigned char)c);
                    }
                } else {
                    ctx->tag_name[ctx->tag_name_len] = '\0';

                    if (strcmp(ctx->tag_name, "p") == 0 || strcmp(ctx->tag_name, "h1") == 0 ||
                        strcmp(ctx->tag_name, "h2") == 0 || strcmp(ctx->tag_name, "h3") == 0 ||
                        strcmp(ctx->tag_name, "li") == 0) {

                        if (ctx->inside_block) flush_text_buffer(ctx);
                        ctx->inside_block = 1;
                        ctx->text_idx = 0;
                        strncpy(ctx->active_tag_type, ctx->tag_name, sizeof(ctx->active_tag_type) - 1);
                        ctx->state = STATE_WAIT_TAG_CLOSE;
                    }
                    else if (strcmp(ctx->tag_name, "/p") == 0 || strcmp(ctx->tag_name, "/h1") == 0 ||
                             strcmp(ctx->tag_name, "/h2") == 0 || strcmp(ctx->tag_name, "/h3") == 0 ||
                             strcmp(ctx->tag_name, "/li") == 0) {

                        ctx->inside_block = 0;
                        flush_text_buffer(ctx);
                        ctx->state = STATE_OUTSIDE;
                    }
                    else if (strcmp(ctx->tag_name, "img") == 0) {
                        ctx->attr_len = 0;
                        ctx->state = STATE_IN_IMG;
                    }
                    else if (strcmp(ctx->tag_name, "video") == 0) {
                        ctx->attr_len = 0;
                        ctx->state = STATE_IN_VIDEO;
                    }
                    else if (strcmp(ctx->tag_name, "script") == 0 || strcmp(ctx->tag_name, "style") == 0 ||
                             strcmp(ctx->tag_name, "sup") == 0 || strcmp(ctx->tag_name, "table") == 0) {
                        ctx->skip_depth = 1;
                        ctx->state = STATE_SKIP_TAG;
                    }
                    else {
                        ctx->state = ctx->inside_block ? STATE_IN_INNER_TAG : STATE_OUTSIDE;
                    }
                }
                break;

            case STATE_WAIT_TAG_CLOSE:
                if (c == '>') {
                    ctx->state = STATE_IN_TEXT;
                }
                break;

            case STATE_IN_IMG:
            case STATE_IN_VIDEO:
                if (c == '>') {
                    ctx->attr_buffer[ctx->attr_len] = '\0';
                    if (ctx->state == STATE_IN_IMG) {
                        extract_and_print_src(ctx->attr_buffer, "IMAGE", ctx->current_domain);
                    } else {
                        extract_and_print_src(ctx->attr_buffer, "VIDEO", ctx->current_domain);
                    }
                    ctx->state = ctx->inside_block ? STATE_IN_TEXT : STATE_OUTSIDE;
                } else {
                    if (ctx->attr_len < MAX_ATTR_LEN - 1) {
                        ctx->attr_buffer[ctx->attr_len++] = c;
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
    ParserContext *ctx = (ParserContext *)userp;
    parse_and_extract((char *)contents, realsize, ctx);
    return realsize;
}

int main(int argc, char *argv[]) {
    int exit_code = 0;
    CURL *curl_handle = NULL;
    CURLcode res;
    char url[512];

    ParserContext ctx;
    memset(&ctx, 0, sizeof(ParserContext));
    ctx.state = STATE_OUTSIDE;

    if (argc >= 2) {
        strncpy(url, argv[1], sizeof(url) - 1);
        url[sizeof(url) - 1] = '\0';
    } else {
        printf("=========================================\n");
        printf("         SEEKER - Web Extractor          \n");
        printf("=========================================\n");
        printf("Enter URL: ");
        if (scanf("%511s", url) != 1) {
            exit_code = 1;
            goto cleanup;
        }
    }

    char full_url[600];
    if (strncmp(url, "http://", 7) != 0 && strncmp(url, "https://", 8) != 0) {
        snprintf(full_url, sizeof(full_url), "https://%s", url);
    } else {
        strncpy(full_url, url, sizeof(full_url) - 1);
    }

    char temp_domain[256] = "";
    if (sscanf(full_url, "%*[^:]://%255[^/]", temp_domain) == 1) {
        snprintf(ctx.current_domain, sizeof(ctx.current_domain), "https://%s", temp_domain);
    } else {
        snprintf(ctx.current_domain, sizeof(ctx.current_domain), "https://localhost");
    }

    curl_global_init(CURL_GLOBAL_ALL);
    curl_handle = curl_easy_init();

    if (!curl_handle) {
        exit_code = 1;
        goto cleanup;
    }

    printf("\n[+] Connecting to: %s ...\n", full_url);

    curl_easy_setopt(curl_handle, CURLOPT_URL, full_url);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)&ctx);
    curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "Mozilla/5.0 (Windows NT 10.0; Win64; x64)");
    curl_easy_setopt(curl_handle, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl_handle, CURLOPT_SSL_VERIFYPEER, 0L);

    res = curl_easy_perform(curl_handle);

    if (res != CURLE_OK) {
        fprintf(stderr, "\n[-] Fetch failed: %s\n", curl_easy_strerror(res));
        exit_code = 1;
    } else {
        printf("\n\n=========================================\n");
        printf("[+] Extraction Complete!\n");
        printf("=========================================\n");
    }

cleanup:
    if (curl_handle) curl_easy_cleanup(curl_handle);
    curl_global_cleanup();

    return exit_code;
}
