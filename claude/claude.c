/*
 * CLAOS — Claude Assisted Operating System
 * claude.c — Claude API Protocol Layer
 *
 * Sends prompts to Claude via the Anthropic Messages API over HTTPS.
 * Supports two modes:
 *   1. Simple: claude_ask() — single text prompt, single text response
 *   2. Agent:  claude_ask_with_tools() — multi-turn with tool execution
 *
 * The agent mode sends system prompt, sysinfo, and tool definitions
 * with every request. When Claude responds with tool_use, CLAOS
 * executes the tools locally and sends results back, looping until
 * Claude returns a final text response.
 */

#include "claude.h"
#include "json.h"
#include "https.h"
#include "config.h"
#include "claude_system.h"
#include "claude_tools.h"
#include "string.h"
#include "stdio.h"
#include "io.h"
#include "vga.h"
#include "keyboard.h"

/* ─── Runtime Configuration ─── */

static char api_key[256] = "";
static char model[128] = "";
static char masked_key[64] = "";

static const char* api_host = ANTHROPIC_HOST;
static const uint16_t api_port = ANTHROPIC_PORT;
static const char* api_path = ANTHROPIC_PATH;
static const char* api_version = ANTHROPIC_VERSION;

/* ─── Multi-Turn Limits ─── */
#define MAX_TOOL_ROUNDS 10

static void update_masked_key(void) {
    int len = strlen(api_key);
    if (len < 15) {
        strcpy(masked_key, "(not set)");
        return;
    }
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
    const char* compiled_key = ANTHROPIC_API_KEY;
    if (strlen(compiled_key) > 10 && strncmp(compiled_key, "sk-ant-your", 11) != 0) {
        claude_set_api_key(compiled_key);
        serial_print("[CLAUDE] API key loaded from config.h\n");
    }
    claude_set_model(CLAUDE_MODEL);

    if (claude_is_configured()) {
        serial_print("[CLAUDE] Ready (tools enabled)\n");
    } else {
        serial_print("[CLAUDE] No API key — use 'config' command to set one\n");
    }
}

void claude_interactive_setup(void) {
    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    vga_print("\n  -- Claude Configuration --\n\n");

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

/* ─── Helper: Build HTTP headers ─── */

static void build_headers(char* headers, int max_len) {
    headers[0] = '\0';
    strcat(headers, "Content-Type: application/json\r\n");
    strcat(headers, "x-api-key: ");
    strcat(headers, api_key);
    strcat(headers, "\r\n");
    strcat(headers, "anthropic-version: ");
    strcat(headers, api_version);
    strcat(headers, "\r\n");
}

/* ─── Simple Ask (no tools — backward compat) ─── */

int claude_ask(const char* prompt, char* response_buf, int buf_size) {
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

    serial_print("[CLAUDE] Sending prompt (simple mode)...\n");

    static char json_body[3072];
    int json_len = json_build_request(json_body, sizeof(json_body),
                                       model, prompt, CLAUDE_MAX_TOKENS);

    static char headers[512];
    build_headers(headers, sizeof(headers));

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
        return text_len;
    } else {
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

/* ─── Agent Mode: Ask with Tools ─── */

/* Static buffers for the multi-turn conversation */
static struct claude_message messages[MAX_MESSAGES];
static int message_count = 0;

/* Large static buffers */
static char system_prompt_buf[3072];
static char request_body[16384];
static char tool_result_buf[4096];

int claude_ask_with_tools(const char* user_message, char* response_buf, int buf_size) {
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

    serial_print("[CLAUDE] Sending prompt (agent mode with tools)...\n");

    /* Step 1: Build system prompt with fresh sysinfo */
    build_system_prompt(system_prompt_buf, sizeof(system_prompt_buf));

    /* Step 2: Get tool definitions */
    const char* tools_json = get_tools_json();

    /* Step 3: Initialize conversation with user message */
    message_count = 0;
    messages[0].role = MSG_ROLE_USER;
    strncpy(messages[0].content, user_message, sizeof(messages[0].content) - 1);
    messages[0].content[sizeof(messages[0].content) - 1] = '\0';
    message_count = 1;

    /* Step 4: Build HTTP headers */
    static char headers[512];
    build_headers(headers, sizeof(headers));

    /* Step 5: Multi-turn tool execution loop */
    for (int round = 0; round < MAX_TOOL_ROUNDS; round++) {
        serial_print("[CLAUDE] Round ");
        char round_str[4];
        round_str[0] = '0' + round;
        round_str[1] = '\0';
        serial_print(round_str);
        serial_print(" of tool loop...\n");

        /* Build the full request */
        int req_len = json_build_request_with_tools(
            request_body, sizeof(request_body),
            model, system_prompt_buf, tools_json,
            messages, message_count,
            CLAUDE_MAX_TOKENS
        );

        /* Send the request */
        struct http_response resp;
        bool ok = https_post(api_host, api_port, api_path,
                              headers, request_body, req_len, &resp);

        if (!ok) {
            const char* msg = "Failed to connect to Anthropic API.";
            int len = strlen(msg);
            if (len >= buf_size) len = buf_size - 1;
            memcpy(response_buf, msg, len);
            response_buf[len] = '\0';
            return len;
        }

        if (resp.status_code != 200) {
            serial_print("[CLAUDE] API error in tool loop, status=");
            char sbuf[8]; int si = 0;
            int tmp = resp.status_code;
            if (tmp == 0) sbuf[si++] = '0';
            else { char r[8]; int ri = 0; while(tmp>0){r[ri++]='0'+tmp%10;tmp/=10;} while(ri>0)sbuf[si++]=r[--ri]; }
            sbuf[si] = '\0';
            serial_print(sbuf);
            serial_putchar('\n');

            /* Try to extract error message */
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

        /* Parse the stop reason */
        int stop = json_parse_stop_reason(resp.body);

        if (stop == STOP_END_TURN || stop == STOP_UNKNOWN) {
            /* Claude is done — extract all text blocks and return */
            int text_len = json_extract_all_text(resp.body, response_buf, buf_size);
            if (text_len < 0) {
                text_len = json_extract_response(resp.body, response_buf, buf_size);
            }
            if (text_len < 0) {
                const char* msg = "Failed to parse Claude's response.";
                int len = strlen(msg);
                if (len >= buf_size) len = buf_size - 1;
                memcpy(response_buf, msg, len);
                response_buf[len] = '\0';
                return len;
            }
            serial_print("[CLAUDE] Got final response!\n");
            return text_len;
        }

        if (stop == STOP_TOOL_USE) {
            serial_print("[CLAUDE] Tool use requested — executing tools...\n");

            /* Save assistant's response (with tool_use blocks) to conversation history */
            if (message_count < MAX_MESSAGES) {
                messages[message_count].role = MSG_ROLE_ASSISTANT;
                json_extract_content_array(resp.body,
                    messages[message_count].content,
                    sizeof(messages[message_count].content));
                message_count++;
            }

            /* Parse tool calls */
            struct tool_call calls[MAX_TOOL_CALLS];
            int num_calls = json_parse_tool_calls(resp.body, calls, MAX_TOOL_CALLS);

            if (num_calls == 0) {
                /* No tool calls found despite stop_reason=tool_use — treat as end */
                int text_len = json_extract_all_text(resp.body, response_buf, buf_size);
                if (text_len > 0) return text_len;
                const char* msg = "Tool use indicated but no tools found in response.";
                int len = strlen(msg);
                if (len >= buf_size) len = buf_size - 1;
                memcpy(response_buf, msg, len);
                response_buf[len] = '\0';
                return len;
            }

            /* Execute each tool and build tool_result message */
            /* Build a JSON array of tool_result content blocks */
            if (message_count < MAX_MESSAGES) {
                int pos = 0;
                int max = sizeof(messages[message_count].content);
                char* buf_ptr = messages[message_count].content;

                pos = 0;
                buf_ptr[pos++] = '[';

                for (int i = 0; i < num_calls; i++) {
                    if (i > 0 && pos < max - 1) buf_ptr[pos++] = ',';

                    /* Execute the tool */
                    serial_print("[TOOL] Executing: ");
                    serial_print(calls[i].name);
                    serial_putchar('\n');

                    execute_tool(calls[i].name, calls[i].input,
                                tool_result_buf, sizeof(tool_result_buf));

                    /* Build tool_result content block */
                    /* {"type":"tool_result","tool_use_id":"...","content":"..."} */
                    int remain = max - pos;
                    if (remain < 128) break; /* safety: don't overflow */

                    static char escaped_result[2048];
                    json_escape_string(tool_result_buf, escaped_result, sizeof(escaped_result));

                    int written = snprintf(buf_ptr + pos, remain,
                        "{\"type\":\"tool_result\",\"tool_use_id\":\"%s\",\"content\":\"%s\"}",
                        calls[i].id, escaped_result);
                    if (written > 0 && written < remain) pos += written;
                }

                if (pos < max - 1) buf_ptr[pos++] = ']';
                buf_ptr[pos] = '\0';

                messages[message_count].role = MSG_ROLE_TOOL;
                message_count++;
            }

            /* Continue the loop to send results back to Claude */
            continue;
        }
    }

    /* Safety: max rounds exceeded */
    const char* msg = "[CLAOS] Tool execution limit reached (10 rounds).";
    int len = strlen(msg);
    if (len >= buf_size) len = buf_size - 1;
    memcpy(response_buf, msg, len);
    response_buf[len] = '\0';
    return len;
}
