/*
 * CLAOS — Claude Assisted Operating System
 * claude_system.h — System Prompt, Sysinfo, and Tool Definitions
 */

#ifndef CLAOS_CLAUDE_SYSTEM_H
#define CLAOS_CLAUDE_SYSTEM_H

/* Build a live system state string (memory, uptime, tasks, etc.)
 * Writes to buf, null-terminated. */
void build_sysinfo_string(char* buf, int max_len);

/* Build the full system prompt including sysinfo.
 * Writes to buf, null-terminated. */
void build_system_prompt(char* buf, int max_len);

/* Get the JSON array of all tool definitions (static string).
 * Returns a pointer to a compile-time constant string. */
const char* get_tools_json(void);

#endif /* CLAOS_CLAUDE_SYSTEM_H */
