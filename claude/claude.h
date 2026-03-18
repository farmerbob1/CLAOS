/*
 * CLAOS — Claude Assisted Operating System
 * claude.h — Claude API Protocol Layer
 *
 * High-level API for talking to Claude from the kernel.
 * Handles JSON formatting, HTTPS requests, and response parsing.
 * API key and model are configured at runtime, not compile time.
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

/* Send a prompt to Claude and get a response.
 * Returns the length of the response, or -1 on error. */
int claude_ask(const char* prompt, char* response_buf, int buf_size);

/* Check if Claude is configured (API key is set) */
bool claude_is_configured(void);

/* Interactive config prompt — asks the user for API key via keyboard */
void claude_interactive_setup(void);

#endif /* CLAOS_CLAUDE_H */
