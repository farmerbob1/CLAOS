/*
 * CLAOS — Claude Assisted Operating System
 * json.c — Minimal JSON builder/parser
 *
 * This is NOT a general-purpose JSON library. It only handles the
 * specific structures used by the Anthropic Messages API.
 *
 * Request format:
 *   {"model":"...","max_tokens":N,"messages":[{"role":"user","content":"..."}]}
 *
 * Response format (simplified):
 *   {"content":[{"type":"text","text":"..."}],...}
 *
 * We use string scanning rather than a proper parser. It's ugly but
 * it works and uses zero dynamic memory.
 */

#include "json.h"
#include "string.h"

/* Append a string to the buffer, respecting bounds */
static int append(char* buf, int pos, int max, const char* str) {
    while (*str && pos < max - 1) {
        buf[pos++] = *str++;
    }
    return pos;
}

int json_escape_string(const char* in, char* out, int out_size) {
    int pos = 0;
    while (*in && pos < out_size - 2) {
        switch (*in) {
            case '"':  out[pos++] = '\\'; out[pos++] = '"'; break;
            case '\\': out[pos++] = '\\'; out[pos++] = '\\'; break;
            case '\n': out[pos++] = '\\'; out[pos++] = 'n'; break;
            case '\r': out[pos++] = '\\'; out[pos++] = 'r'; break;
            case '\t': out[pos++] = '\\'; out[pos++] = 't'; break;
            default:
                if ((unsigned char)*in >= 0x20) {
                    out[pos++] = *in;
                }
                break;
        }
        in++;
    }
    out[pos] = '\0';
    return pos;
}

int json_build_request(char* buf, int buf_size,
                        const char* model, const char* prompt, int max_tokens) {
    int pos = 0;

    pos = append(buf, pos, buf_size, "{\"model\":\"");
    pos = append(buf, pos, buf_size, model);
    pos = append(buf, pos, buf_size, "\",\"max_tokens\":");

    /* Convert max_tokens to string */
    char num[12];
    int ni = 0;
    int tmp = max_tokens;
    if (tmp == 0) num[ni++] = '0';
    else {
        char rev[12]; int ri = 0;
        while (tmp > 0) { rev[ri++] = '0' + tmp % 10; tmp /= 10; }
        while (ri > 0) num[ni++] = rev[--ri];
    }
    num[ni] = '\0';
    pos = append(buf, pos, buf_size, num);

    pos = append(buf, pos, buf_size, ",\"messages\":[{\"role\":\"user\",\"content\":\"");

    /* Escape the prompt for JSON */
    char escaped[2048];
    json_escape_string(prompt, escaped, sizeof(escaped));
    pos = append(buf, pos, buf_size, escaped);

    pos = append(buf, pos, buf_size, "\"}]}");

    if (pos < buf_size) buf[pos] = '\0';
    return pos;
}

/*
 * Find a JSON string value by key.
 * Scans for "key":"value" and extracts the value.
 * Handles escaped characters in the value string.
 * Returns pointer to start of value in the JSON, or NULL.
 */
static const char* find_json_string(const char* json, const char* key) {
    int key_len = strlen(key);

    while (*json) {
        /* Look for the key pattern: "key" */
        if (*json == '"') {
            json++;
            if (strncmp(json, key, key_len) == 0 && json[key_len] == '"') {
                /* Found the key. Skip past "key" and look for : "value" */
                json += key_len + 1;
                /* Skip whitespace and colon */
                while (*json == ' ' || *json == ':' || *json == '\t' ||
                       *json == '\n' || *json == '\r') json++;
                /* Should now be at the opening quote of the value */
                if (*json == '"') {
                    return json + 1;    /* Return pointer past the opening quote */
                }
            }
        }
        json++;
    }
    return NULL;
}

/* Extract a JSON string value, handling escape sequences */
static int extract_json_string(const char* start, char* out, int out_size) {
    int pos = 0;
    const char* p = start;

    while (*p && *p != '"' && pos < out_size - 1) {
        if (*p == '\\' && p[1]) {
            p++;
            switch (*p) {
                case '"':  out[pos++] = '"'; break;
                case '\\': out[pos++] = '\\'; break;
                case 'n':  out[pos++] = '\n'; break;
                case 'r':  out[pos++] = '\r'; break;
                case 't':  out[pos++] = '\t'; break;
                case '/':  out[pos++] = '/'; break;
                default:   out[pos++] = *p; break;
            }
        } else {
            out[pos++] = *p;
        }
        p++;
    }
    out[pos] = '\0';
    return pos;
}

int json_extract_response(const char* json, char* out, int out_size) {
    /*
     * Anthropic API response:
     *   {"content":[{"type":"text","text":"THE RESPONSE"}],...}
     *
     * Strategy: scan for the pattern "text":" after "content" and
     * extract the string value. We skip the first "text" which is
     * the value of "type" (i.e. "type":"text"), and take the second
     * one which is the key "text":"<actual response>".
     */

    /* Find "content" key in the JSON */
    const char* p = json;
    const char* content_pos = NULL;
    while (*p) {
        if (*p == '"' && strncmp(p + 1, "content", 7) == 0 && p[8] == '"') {
            content_pos = p + 9;
            break;
        }
        p++;
    }

    if (!content_pos) {
        /* Try error response: find "message" key */
        const char* msg = find_json_string(json, "message");
        if (msg) return extract_json_string(msg, out, out_size);
        return -1;
    }

    /* From "content" onward, find "text":"<value>" pattern.
     * Skip occurrences where "text" appears as a VALUE (after "type":).
     * The one we want is where "text" is a KEY followed by :"..." */
    p = content_pos;
    while (*p) {
        /* Look for "text" as a key: "text":<whitespace>"<value>" */
        if (*p == '"' && strncmp(p + 1, "text", 4) == 0 && p[5] == '"') {
            /* Check what comes before — is this a key or a value?
             * If preceded by : it's a value ("type":"text").
             * If followed by : it's a key ("text":"response"). */
            const char* after = p + 6;
            while (*after == ' ' || *after == '\t') after++;
            if (*after == ':') {
                /* This is the key "text" — extract its value */
                after++;
                while (*after == ' ' || *after == '\t') after++;
                if (*after == '"') {
                    return extract_json_string(after + 1, out, out_size);
                }
            }
        }
        p++;
    }

    return -1;
}
