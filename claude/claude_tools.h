/*
 * CLAOS — Claude Assisted Operating System
 * claude_tools.h — Tool Executor
 *
 * Dispatches tool calls from Claude and executes them against
 * the OS subsystems (filesystem, scheduler, GUI, network, audio, Lua).
 */

#ifndef CLAOS_CLAUDE_TOOLS_H
#define CLAOS_CLAUDE_TOOLS_H

/* Execute a tool by name with JSON arguments.
 * Writes result string to `result`, null-terminated.
 * `args_json` is the raw JSON object from the tool_use input field. */
void execute_tool(const char* name, const char* args_json,
                  char* result, int max_len);

/* Log a tool execution to /logs/claude_tools.log */
void tool_log(const char* name, const char* args_summary, const char* status);

#endif /* CLAOS_CLAUDE_TOOLS_H */
