/*
 * CLAOS — Claude Assisted Operating System
 * claude.c — Claude API Protocol Layer
 *
 * Sends prompts to Claude via the Anthropic Messages API over HTTPS
 * and returns the text response. This is the core "AI" component of CLAOS.
 *
 * Configuration is done at runtime — the user enters their API key
 * via the `config` command or gets prompted on first use. The key
 * is stored in memory (not persisted to disk).
 *
 * If claude/config.h has a real key compiled in, it's used as the
 * default so power users can skip the interactive setup.
 */

#include "claude.h"
#include "json.h"
#include "https.h"
#include "config.h"
#include "string.h"
#include "io.h"
#include "vga.h"
#include "keyboard.h"

/* ─── Runtime Configuration ─── */

/* API key — stored in memory, entered by user at runtime */
static char api_key[256] = "";

/* Model name — can be changed at runtime */
static char model[128] = "";

/* Masked key for display (shows first 10 + last 4 chars) */
static char masked_key[64] = "";

/* API endpoint settings (these rarely change) */
static const char* api_host = ANTHROPIC_HOST;
static const uint16_t api_port = ANTHROPIC_PORT;
static const char* api_path = ANTHROPIC_PATH;
static const char* api_version = ANTHROPIC_VERSION;

static void update_masked_key(void) {
    int len = strlen(api_key);
    if (len < 15) {
        strcpy(masked_key, "(not set)");
        return;
    }
    /* Show: "sk-ant-api0..." + last 4 chars */
    int show_start = 10;
    int show_end = 4;
    int pos = 0;
    for (int i = 0; i < show_start && i < len; i++)
        masked_key[pos++] = api_key[i];
    masked_key[pos++] = '.';
    masked_key[pos++] = '.';
    masked_key[pos++] = '.';
    for (int i = len - show_end; i < len; i++)
        masked_key[pos++] = api_key[i];
    masked_key[pos] = '\0';
}

void claude_set_api_key(const char* key) {
    strncpy(api_key, key, sizeof(api_key) - 1);
    api_key[sizeof(api_key) - 1] = '\0';
    update_masked_key();
}

void claude_set_model(const char* m) {
    strncpy(model, m, sizeof(model) - 1);
    model[sizeof(model) - 1] = '\0';
}

const char* claude_get_api_key_masked(void) {
    return masked_key;
}

const char* claude_get_model(void) {
    return model;
}

bool claude_is_configured(void) {
    return strlen(api_key) > 10 && strncmp(api_key, "sk-ant-your", 11) != 0;
}

void claude_init(void) {
    /* Load defaults from compile-time config (if set) */
    const char* compiled_key = ANTHROPIC_API_KEY;
    if (strlen(compiled_key) > 10 && strncmp(compiled_key, "sk-ant-your", 11) != 0) {
        claude_set_api_key(compiled_key);
        serial_print("[CLAUDE] API key loaded from config.h\n");
    }

    /* Set default model */
    claude_set_model(CLAUDE_MODEL);

    if (claude_is_configured()) {
        serial_print("[CLAUDE] Ready\n");
    } else {
        serial_print("[CLAUDE] No API key — use 'config' command to set one\n");
    }
}

/* Interactive first-run setup — prompts user for API key */
void claude_interactive_setup(void) {
    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    vga_print("\n  ── Claude Configuration ──\n\n");

    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_print("  Current API key: ");
    vga_set_color(VGA_YELLOW, VGA_BLACK);
    if (claude_is_configured()) {
        vga_print(claude_get_api_key_masked());
    } else {
        vga_print("(not set)");
    }
    vga_print("\n");

    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_print("  Current model:   ");
    vga_set_color(VGA_YELLOW, VGA_BLACK);
    vga_print(claude_get_model());
    vga_print("\n\n");

    /* Prompt for API key */
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    vga_print("  Enter API key (or press Enter to keep current):\n");
    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_print("  > ");

    char input[256];
    keyboard_readline(input, sizeof(input));

    if (strlen(input) > 0) {
        claude_set_api_key(input);
        vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
        vga_print("  API key set: ");
        vga_print(claude_get_api_key_masked());
        vga_print("\n");
    }

    /* Prompt for model */
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    vga_print("  Enter model (or press Enter for ");
    vga_print(claude_get_model());
    vga_print("):\n");
    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_print("  > ");

    keyboard_readline(input, sizeof(input));

    if (strlen(input) > 0) {
        claude_set_model(input);
        vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
        vga_print("  Model set: ");
        vga_print(claude_get_model());
        vga_print("\n");
    }

    vga_print("\n");

    if (claude_is_configured()) {
        vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
        vga_print("  Claude is ready! Try: claude Hello from CLAOS!\n");
    } else {
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        vga_print("  No API key set. Get one at console.anthropic.com\n");
    }
    vga_set_color(VGA_WHITE, VGA_BLACK);
}

int claude_ask(const char* prompt, char* response_buf, int buf_size) {
    /* If not configured, prompt the user interactively */
    if (!claude_is_configured()) {
        vga_set_color(VGA_YELLOW, VGA_BLACK);
        vga_print("  Claude is not configured yet.\n");
        vga_set_color(VGA_WHITE, VGA_BLACK);
        claude_interactive_setup();

        if (!claude_is_configured()) {
            const char* msg = "No API key configured. Use 'config' to set one.";
            int len = strlen(msg);
            if (len >= buf_size) len = buf_size - 1;
            memcpy(response_buf, msg, len);
            response_buf[len] = '\0';
            return len;
        }
    }

    serial_print("[CLAUDE] Sending prompt to Claude...\n");

    /* Step 1: Build the JSON request body */
    static char json_body[3072];
    int json_len = json_build_request(json_body, sizeof(json_body),
                                       model, prompt, CLAUDE_MAX_TOKENS);

    /* Step 2: Build the extra headers */
    static char headers[512];
    headers[0] = '\0';
    strcat(headers, "Content-Type: application/json\r\n");
    strcat(headers, "x-api-key: ");
    strcat(headers, api_key);
    strcat(headers, "\r\n");
    strcat(headers, "anthropic-version: ");
    strcat(headers, api_version);
    strcat(headers, "\r\n");

    /* Step 3: Send the HTTPS POST request */
    struct http_response resp;
    bool ok = https_post(api_host, api_port, api_path,
                          headers, json_body, json_len, &resp);

    if (!ok) {
        const char* msg = "Failed to connect to Anthropic API.";
        int len = strlen(msg);
        if (len >= buf_size) len = buf_size - 1;
        memcpy(response_buf, msg, len);
        response_buf[len] = '\0';
        return len;
    }

    /* Step 4: Parse the JSON response */
    if (resp.status_code == 200) {
        int text_len = json_extract_response(resp.body, response_buf, buf_size);
        if (text_len < 0) {
            const char* msg = "Failed to parse Claude's response.";
            int len = strlen(msg);
            if (len >= buf_size) len = buf_size - 1;
            memcpy(response_buf, msg, len);
            response_buf[len] = '\0';
            return len;
        }
        serial_print("[CLAUDE] Got response!\n");
        return text_len;
    } else {
        /* Error response */
        serial_print("[CLAUDE] Error body: ");
        serial_print(resp.body);
        serial_putchar('\n');
        serial_print("[CLAUDE] API error, status=");
        char sbuf[8]; int si = 0;
        int tmp = resp.status_code;
        if (tmp == 0) sbuf[si++] = '0';
        else { char r[8]; int ri = 0; while(tmp>0){r[ri++]='0'+tmp%10;tmp/=10;} while(ri>0)sbuf[si++]=r[--ri]; }
        sbuf[si] = '\0';
        serial_print(sbuf);
        serial_putchar('\n');

        int text_len = json_extract_response(resp.body, response_buf, buf_size);
        if (text_len < 0) {
            int blen = resp.body_len;
            if (blen >= buf_size) blen = buf_size - 1;
            memcpy(response_buf, resp.body, blen);
            response_buf[blen] = '\0';
            return blen;
        }
        return text_len;
    }
}
