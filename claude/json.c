/*
 * CLAOS — Claude Assisted Operating System
 * json.c — JSON builder/parser for the Anthropic API
 *
 * Upgraded from minimal text-only parser to handle:
 * - Tool definitions in requests
 * - System prompts
 * - Multi-turn conversation history
 * - stop_reason parsing (end_turn vs tool_use)
 * - tool_use content block extraction
 * - tool_result message building
 *
 * Still uses string scanning — not a general JSON library.
 */

#include "json.h"
#include "string.h"

/* ─── Helpers ─── */

/* Append a string to the buffer, respecting bounds */
static int append(char* buf, int pos, int max, const char* str) {
    while (*str && pos < max - 1) {
        buf[pos++] = *str++;
    }
    return pos;
}

/* Append an integer as decimal string */
static int append_int(char* buf, int pos, int max, int val) {
    char num[12];
    int ni = 0;
    if (val == 0) {
        num[ni++] = '0';
    } else {
        int tmp = val;
        if (tmp < 0) {
            if (pos < max - 1) buf[pos++] = '-';
            tmp = -tmp;
        }
        char rev[12]; int ri = 0;
        while (tmp > 0) { rev[ri++] = '0' + tmp % 10; tmp /= 10; }
        while (ri > 0) num[ni++] = rev[--ri];
    }
    num[ni] = '\0';
    return append(buf, pos, max, num);
}

/* ─── String Escaping ─── */

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

/* ─── Request Building (Simple — backward compat) ─── */

int json_build_request(char* buf, int buf_size,
                        const char* model, const char* prompt, int max_tokens) {
    int pos = 0;
    pos = append(buf, pos, buf_size, "{\"model\":\"");
    pos = append(buf, pos, buf_size, model);
    pos = append(buf, pos, buf_size, "\",\"max_tokens\":");
    pos = append_int(buf, pos, buf_size, max_tokens);
    pos = append(buf, pos, buf_size, ",\"messages\":[{\"role\":\"user\",\"content\":\"");

    char escaped[2048];
    json_escape_string(prompt, escaped, sizeof(escaped));
    pos = append(buf, pos, buf_size, escaped);

    pos = append(buf, pos, buf_size, "\"}]}");
    if (pos < buf_size) buf[pos] = '\0';
    return pos;
}

/* ─── Request Building (With Tools) ─── */

int json_build_request_with_tools(char* buf, int buf_size,
                                   const char* model,
                                   const char* system_prompt,
                                   const char* tools_json,
                                   struct claude_message* messages, int msg_count,
                                   int max_tokens) {
    int pos = 0;

    /* Opening + model */
    pos = append(buf, pos, buf_size, "{\"model\":\"");
    pos = append(buf, pos, buf_size, model);
    pos = append(buf, pos, buf_size, "\",\"max_tokens\":");
    pos = append_int(buf, pos, buf_size, max_tokens);

    /* System prompt */
    if (system_prompt && system_prompt[0]) {
        pos = append(buf, pos, buf_size, ",\"system\":\"");
        static char sys_escaped[3072];
        json_escape_string(system_prompt, sys_escaped, sizeof(sys_escaped));
        pos = append(buf, pos, buf_size, sys_escaped);
        pos = append(buf, pos, buf_size, "\"");
    }

    /* Tools array (pre-built JSON, inserted verbatim) */
    if (tools_json && tools_json[0]) {
        pos = append(buf, pos, buf_size, ",\"tools\":");
        pos = append(buf, pos, buf_size, tools_json);
    }

    /* Messages array */
    pos = append(buf, pos, buf_size, ",\"messages\":[");

    for (int i = 0; i < msg_count; i++) {
        if (i > 0) pos = append(buf, pos, buf_size, ",");

        struct claude_message* m = &messages[i];

        if (m->role == MSG_ROLE_USER) {
            pos = append(buf, pos, buf_size, "{\"role\":\"user\",\"content\":\"");
            static char msg_escaped[2048];
            json_escape_string(m->content, msg_escaped, sizeof(msg_escaped));
            pos = append(buf, pos, buf_size, msg_escaped);
            pos = append(buf, pos, buf_size, "\"}");
        }
        else if (m->role == MSG_ROLE_ASSISTANT) {
            /* Assistant messages have raw JSON content array */
            pos = append(buf, pos, buf_size, "{\"role\":\"assistant\",\"content\":");
            pos = append(buf, pos, buf_size, m->content);
            pos = append(buf, pos, buf_size, "}");
        }
        else if (m->role == MSG_ROLE_TOOL) {
            /* Tool result messages have raw JSON content array */
            pos = append(buf, pos, buf_size, "{\"role\":\"user\",\"content\":");
            pos = append(buf, pos, buf_size, m->content);
            pos = append(buf, pos, buf_size, "}");
        }
    }

    pos = append(buf, pos, buf_size, "]}");

    if (pos < buf_size) buf[pos] = '\0';
    return pos;
}

/* ─── Response Parsing Helpers ─── */

/*
 * Find a JSON string value by key.
 * Scans for "key":"value" and extracts the value.
 * Returns pointer to start of value (past opening quote), or NULL.
 */
static const char* find_json_string(const char* json, const char* key) {
    int key_len = strlen(key);

    while (*json) {
        if (*json == '"') {
            json++;
            if (strncmp(json, key, key_len) == 0 && json[key_len] == '"') {
                json += key_len + 1;
                while (*json == ' ' || *json == ':' || *json == '\t' ||
                       *json == '\n' || *json == '\r') json++;
                if (*json == '"') {
                    return json + 1;
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

/* Skip whitespace */
static const char* skip_ws(const char* p) {
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    return p;
}

/* Find matching closing brace/bracket, respecting nesting and strings */
static const char* find_matching_close(const char* p, char open, char close) {
    int depth = 1;
    p++; /* skip the opening char */
    while (*p && depth > 0) {
        if (*p == '"') {
            /* Skip string contents */
            p++;
            while (*p && *p != '"') {
                if (*p == '\\' && p[1]) p++;
                p++;
            }
            if (*p == '"') p++;
            continue;
        }
        if (*p == open) depth++;
        else if (*p == close) depth--;
        if (depth > 0) p++;
    }
    return p; /* points at the closing char */
}

/* ─── stop_reason Parsing ─── */

int json_parse_stop_reason(const char* json) {
    const char* val = find_json_string(json, "stop_reason");
    if (!val) return STOP_UNKNOWN;

    if (strncmp(val, "end_turn", 8) == 0) return STOP_END_TURN;
    if (strncmp(val, "tool_use", 8) == 0) return STOP_TOOL_USE;
    return STOP_UNKNOWN;
}

/* ─── Tool Call Parsing ─── */

int json_parse_tool_calls(const char* json, struct tool_call* calls, int max_calls) {
    int count = 0;

    /* Find "content" array in the response */
    const char* p = json;

    while (*p && count < max_calls) {
        /* Look for "type":"tool_use" pattern */
        const char* type_pos = strstr(p, "\"type\"");
        if (!type_pos) break;

        /* Check if it's followed by "tool_use" */
        const char* after_type = type_pos + 6;
        after_type = skip_ws(after_type);
        if (*after_type == ':') after_type++;
        after_type = skip_ws(after_type);

        if (*after_type == '"' && strncmp(after_type + 1, "tool_use", 8) == 0) {
            /* Found a tool_use block. Now find its enclosing object.
             * Search backward for the opening { */
            const char* obj_start = type_pos;
            while (obj_start > json && *obj_start != '{') obj_start--;

            if (*obj_start == '{') {
                const char* obj_end = find_matching_close(obj_start, '{', '}');

                /* Extract "id" field */
                const char* id_val = find_json_string(obj_start, "id");
                if (id_val) {
                    extract_json_string(id_val, calls[count].id, sizeof(calls[count].id));
                }

                /* Extract "name" field */
                const char* name_val = find_json_string(obj_start, "name");
                if (name_val) {
                    extract_json_string(name_val, calls[count].name, sizeof(calls[count].name));
                }

                /* Extract "input" field — this is a JSON object, extract by brace matching */
                const char* input_key = strstr(obj_start, "\"input\"");
                if (input_key && input_key < obj_end) {
                    const char* input_start = input_key + 7;
                    input_start = skip_ws(input_start);
                    if (*input_start == ':') input_start++;
                    input_start = skip_ws(input_start);

                    if (*input_start == '{') {
                        const char* input_end = find_matching_close(input_start, '{', '}');
                        int input_len = (int)(input_end - input_start + 1);
                        if (input_len >= (int)sizeof(calls[count].input))
                            input_len = sizeof(calls[count].input) - 1;
                        memcpy(calls[count].input, input_start, input_len);
                        calls[count].input[input_len] = '\0';
                    }
                }

                count++;
                p = obj_end + 1;
                continue;
            }
        }

        p = type_pos + 6;
    }

    return count;
}

/* ─── Content Array Extraction ─── */

/* Find the start of the "content" array in the response */
static const char* find_content_array(const char* json) {
    const char* p = json;
    while (*p) {
        if (*p == '"' && strncmp(p + 1, "content", 7) == 0 && p[8] == '"') {
            const char* after = p + 9;
            after = skip_ws(after);
            if (*after == ':') {
                after++;
                after = skip_ws(after);
                if (*after == '[') return after;
            }
        }
        p++;
    }
    return NULL;
}

int json_extract_content_array(const char* json, char* out, int out_size) {
    const char* arr_start = find_content_array(json);
    if (!arr_start) return -1;

    const char* arr_end = find_matching_close(arr_start, '[', ']');
    int len = (int)(arr_end - arr_start + 1);
    if (len >= out_size) len = out_size - 1;
    memcpy(out, arr_start, len);
    out[len] = '\0';
    return len;
}

int json_extract_all_text(const char* json, char* out, int out_size) {
    int total = 0;

    /* Find "content" and then look for all "type":"text" blocks */
    const char* content = find_content_array(json);
    if (!content) {
        /* Fallback: try the original method */
        return json_extract_response(json, out, out_size);
    }

    const char* p = content;
    const char* content_end = find_matching_close(content, '[', ']');

    while (p < content_end) {
        /* Find next "type":"text" */
        const char* type_pos = strstr(p, "\"type\"");
        if (!type_pos || type_pos >= content_end) break;

        const char* after = type_pos + 6;
        after = skip_ws(after);
        if (*after == ':') after++;
        after = skip_ws(after);

        if (*after == '"' && strncmp(after + 1, "text", 4) == 0 && after[5] == '"') {
            /* This is a text block — find the enclosing object */
            const char* obj_start = type_pos;
            while (obj_start > content && *obj_start != '{') obj_start--;

            if (*obj_start == '{') {
                const char* obj_end = find_matching_close(obj_start, '{', '}');
                /* Find "text":"<value>" within this object */
                const char* text_val = find_json_string(obj_start, "text");
                if (text_val && text_val < obj_end) {
                    int len = extract_json_string(text_val, out + total, out_size - total);
                    total += len;
                }
                p = obj_end + 1;
                continue;
            }
        }
        p = type_pos + 6;
    }

    if (total > 0) {
        out[total] = '\0';
    }
    return total > 0 ? total : -1;
}

/* ─── Original Response Extractor (backward compat) ─── */

int json_extract_response(const char* json, char* out, int out_size) {
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
        const char* msg = find_json_string(json, "message");
        if (msg) return extract_json_string(msg, out, out_size);
        return -1;
    }

    p = content_pos;
    while (*p) {
        if (*p == '"' && strncmp(p + 1, "text", 4) == 0 && p[5] == '"') {
            const char* after = p + 6;
            while (*after == ' ' || *after == '\t') after++;
            if (*after == ':') {
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

/* ─── JSON Field Helpers ─── */

int json_get_string(const char* json, const char* key, char* out, int out_size) {
    const char* val = find_json_string(json, key);
    if (!val) {
        out[0] = '\0';
        return -1;
    }
    return extract_json_string(val, out, out_size);
}

int json_get_int(const char* json, const char* key) {
    int key_len = strlen(key);
    const char* p = json;

    while (*p) {
        if (*p == '"') {
            p++;
            if (strncmp(p, key, key_len) == 0 && p[key_len] == '"') {
                p += key_len + 1;
                while (*p == ' ' || *p == ':' || *p == '\t' ||
                       *p == '\n' || *p == '\r') p++;
                /* Parse integer */
                int neg = 0;
                if (*p == '-') { neg = 1; p++; }
                int val = 0;
                while (*p >= '0' && *p <= '9') {
                    val = val * 10 + (*p - '0');
                    p++;
                }
                return neg ? -val : val;
            }
        }
        p++;
    }
    return 0;
}
