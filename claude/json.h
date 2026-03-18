/*
 * CLAOS — Claude Assisted Operating System
 * json.h — Minimal JSON builder/parser for the Anthropic API
 *
 * Only handles the specific JSON structures we need:
 * - Building: Messages API request bodies
 * - Parsing: extract content[0].text from API responses
 */

#ifndef CLAOS_JSON_H
#define CLAOS_JSON_H

#include "types.h"

/* Build a Claude API request body.
 * Writes JSON to `buf`, returns the length.
 * `model`, `prompt` are the inputs.
 * `max_tokens` is the response length limit. */
int json_build_request(char* buf, int buf_size,
                        const char* model, const char* prompt, int max_tokens);

/* Extract the text content from a Claude API response.
 * Looks for content[0].text in the JSON.
 * Copies the extracted text to `out`, null-terminated.
 * Returns the length of extracted text, or -1 on parse failure. */
int json_extract_response(const char* json, char* out, int out_size);

/* Escape a string for JSON (handles \n, \r, \t, \\, \", etc.)
 * Returns number of bytes written to `out`. */
int json_escape_string(const char* in, char* out, int out_size);

#endif /* CLAOS_JSON_H */
