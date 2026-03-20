/*
 * CLAOS — Claude Assisted Operating System
 * json.h — JSON builder/parser for the Anthropic API
 *
 * Handles:
 * - Building: Messages API requests with tools, system prompt, multi-turn history
 * - Parsing: stop_reason, text content, tool_use blocks, field extraction
 */

#ifndef CLAOS_JSON_H
#define CLAOS_JSON_H

#include "types.h"

/* ─── Stop Reasons ─── */
#define STOP_END_TURN   0
#define STOP_TOOL_USE   1
#define STOP_UNKNOWN    2

/* ─── Limits ─── */
#define MAX_TOOL_CALLS  5
#define MAX_MESSAGES    12   /* Up to ~5 rounds × 2 messages + initial */

/* ─── Data Structures ─── */

/* A single tool call extracted from Claude's response */
struct tool_call {
    char id[64];          /* tool_use_id from the API */
    char name[64];        /* tool name, e.g. "read_file" */
    char input[4096];     /* raw JSON of the input object */
};

/* Message roles */
#define MSG_ROLE_USER       0
#define MSG_ROLE_ASSISTANT  1
#define MSG_ROLE_TOOL       2

/* A message in the conversation history */
struct claude_message {
    int  role;             /* MSG_ROLE_* */
    char content[4096];    /* JSON content array (assistant) or text (user) or tool_results */
};

/* ─── Request Building ─── */

/* Build a simple Claude API request body (no tools, backward compat).
 * Writes JSON to `buf`, returns the length. */
int json_build_request(char* buf, int buf_size,
                        const char* model, const char* prompt, int max_tokens);

/* Build a full Claude API request with system prompt, tools, and conversation history.
 * Returns the length written to buf. */
int json_build_request_with_tools(char* buf, int buf_size,
                                   const char* model,
                                   const char* system_prompt,
                                   const char* tools_json,
                                   struct claude_message* messages, int msg_count,
                                   int max_tokens);

/* ─── Response Parsing ─── */

/* Extract the text content from a Claude API response.
 * Looks for content[0].text in the JSON.
 * Returns the length of extracted text, or -1 on parse failure. */
int json_extract_response(const char* json, char* out, int out_size);

/* Parse the stop_reason from an API response.
 * Returns STOP_END_TURN, STOP_TOOL_USE, or STOP_UNKNOWN. */
int json_parse_stop_reason(const char* json);

/* Parse tool_use blocks from the response content array.
 * Fills `calls` array, returns the number of tool calls found (0 if none). */
int json_parse_tool_calls(const char* json, struct tool_call* calls, int max_calls);

/* Extract all text blocks from the response content array into one string.
 * Returns length written, or -1 on failure. */
int json_extract_all_text(const char* json, char* out, int out_size);

/* Save the raw content array from assistant response (for conversation history).
 * Returns length written. */
int json_extract_content_array(const char* json, char* out, int out_size);

/* ─── JSON Field Helpers ─── */

/* Extract a string value by key from a JSON object.
 * Returns length of extracted string, or -1 if not found. */
int json_get_string(const char* json, const char* key, char* out, int out_size);

/* Extract an integer value by key from a JSON object.
 * Returns the integer value, or 0 if not found. */
int json_get_int(const char* json, const char* key);

/* ─── String Utilities ─── */

/* Escape a string for JSON (handles \n, \r, \t, \\, \", etc.)
 * Returns number of bytes written to `out`. */
int json_escape_string(const char* in, char* out, int out_size);

#endif /* CLAOS_JSON_H */
