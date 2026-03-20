/*
 * CLAOS — Claude Assisted Operating System
 * claude.h — Claude API Protocol Layer
 *
 * High-level API for talking to Claude from the kernel.
 * Supports both simple text requests and multi-turn tool interactions.
 */

#ifndef CLAOS_CLAUDE_H
#define CLAOS_CLAUDE_H

#include "types.h"

/* Maximum response size from Claude */
#define CLAUDE_RESPONSE_MAX 4096

/* Initialize the Claude subsystem */
void claude_init(void);

/* Set the API key at runtime (e.g., from user input) */
void claude_set_api_key(const char* key);

/* Set the model at runtime */
void claude_set_model(const char* model);

/* Get the current API key (for display — shows masked version) */
const char* claude_get_api_key_masked(void);

/* Get the current model name */
const char* claude_get_model(void);

/* Simple prompt → response (no tools, no system prompt).
 * Used by panic handler and backward-compatible code.
 * Returns the length of the response, or -1 on error. */
int claude_ask(const char* prompt, char* response_buf, int buf_size);

/* Full tool-enabled prompt → response.
 * Sends with system prompt, sysinfo, tool definitions.
 * Executes up to 10 rounds of tool calls before returning
 * Claude's final text response.
 * Returns the length of the response, or -1 on error. */
int claude_ask_with_tools(const char* user_message, char* response_buf, int buf_size);

/* Check if Claude is configured (API key is set) */
bool claude_is_configured(void);

/* Interactive config prompt — asks the user for API key via keyboard */
void claude_interactive_setup(void);

#endif /* CLAOS_CLAUDE_H */
