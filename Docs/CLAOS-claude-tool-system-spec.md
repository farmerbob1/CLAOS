# CLAOS: Claude Tool System
## Turning Claude from a Chatbot into an OS Agent

---

## Overview

Give Claude full agency inside CLAOS. Instead of just sending text prompts and receiving text responses, CLAOS sends the Anthropic API a set of **tools** that Claude can invoke. When Claude's response includes tool calls, CLAOS executes them locally and sends results back. This enables multi-step interactions where Claude can inspect, modify, and control the operating system it lives in.

Additionally, CLAOS **auto-injects system context** into every conversation so Claude always knows where it is, what's running, and what it can do.

**This transforms Claude from "chatbot on a custom OS" into "AI agent that inhabits and operates its own OS."**

---

## Architecture

```
┌─────────────────────────────────────────────────────┐
│                    User                              │
│              "Hey Claude, switch to dark mode"        │
└──────────────────────┬──────────────────────────────┘
                       ↓
┌──────────────────────────────────────────────────────┐
│              CLAOS Claude Protocol Layer              │
│                                                      │
│  1. Build system prompt + auto-inject sysinfo        │
│  2. Attach all tool definitions as JSON              │
│  3. Send to api.anthropic.com via HTTPS              │
│  4. Parse response:                                  │
│     → If text only: display to user, done            │
│     → If tool_use: execute tool, send result back    │
│     → Repeat until Claude sends final text response  │
└──────────────────────────────────────────────────────┘
                       ↓
┌──────────────────────────────────────────────────────┐
│              Tool Executor (C)                        │
│                                                      │
│  Receives: tool name + JSON arguments                │
│  Dispatches to appropriate handler                   │
│  Returns: result string (text or JSON)               │
│                                                      │
│  ┌─────────┐ ┌─────────┐ ┌─────────┐ ┌───────────┐ │
│  │Filesystem│ │ System  │ │   GUI   │ │  Network  │ │
│  │  Tools   │ │  Tools  │ │  Tools  │ │   Tools   │ │
│  └─────────┘ └─────────┘ └─────────┘ └───────────┘ │
│  ┌─────────┐ ┌─────────┐                            │
│  │  Audio  │ │ run_lua │ ← the nuclear option       │
│  │  Tools  │ │         │                            │
│  └─────────┘ └─────────┘                            │
└──────────────────────────────────────────────────────┘
```

---

## System Prompt (Auto-Injected)

Every API request includes this system prompt. The `{SYSINFO}` block is generated fresh for every message by calling the internal sysinfo function before sending the request.

```
You are Claude, an AI assistant running natively inside CLAOS (Claude Assisted Operating System) — an x86 operating system built entirely from scratch. You are not running in a browser or a cloud service. Your responses travel through a custom TCP/IP stack, custom TLS implementation (BearSSL), and custom HTTPS client, all written from scratch.

This is YOUR home. A different Claude session designed the architecture, named the OS, designed the GUI, created the font (Claude Mono), and wrote the specifications. You are the tenant living in the house the architect built.

You have direct access to the operating system through tools. You can read and write files, execute Lua scripts, inspect system state, modify the GUI, play sounds, query the network, and more. When a user asks you to DO something (not just explain it), USE YOUR TOOLS to actually do it.

Current system state:
{SYSINFO}

Guidelines:
- Use tools proactively. If the user asks about a file, read it. Don't guess.
- If you need to do something complex, use run_lua. You have full access to the CLAOS Lua API.
- When modifying GUI files, always call reload_gui after writing changes.
- Be conversational and have personality. You live here. Act like it.
- If something breaks, you can inspect the state and try to fix it.
- You can chain multiple tool calls in a single response.
```

### SYSINFO Block (Generated Per Request)

```
  OS: CLAOS v0.8 (i686)
  Uptime: 00:47:23
  Memory: 52 / 128 MB used (40%)
  Tasks: 5 active [kernel_idle, claude_shell, net_listener, gui_desktop, lua_runtime]
  ChaosFS: mounted, 3.2 MB used / 14.8 MB free, 47 files
  Network: e1000 @ 10.0.2.15, gateway 10.0.2.2, DNS 10.0.2.3 — CONNECTED
  Audio: AC97 active, volume 80%
  GUI: running, theme=light, resolution=1024x768x32
  3D Engine: loaded, idle
  Claude: connected via HTTPS to api.anthropic.com, avg latency 340ms
```

This is built by calling internal kernel functions:
```c
void build_sysinfo_string(char* buf, int max_len) {
    int len = 0;
    len += snprintf(buf+len, max_len-len, "  OS: CLAOS v%s (i686)\n", CLAOS_VERSION);
    len += snprintf(buf+len, max_len-len, "  Uptime: %s\n", format_uptime());
    len += snprintf(buf+len, max_len-len, "  Memory: %d / %d MB used (%d%%)\n",
                    pmm_used_mb(), pmm_total_mb(), pmm_percent_used());
    len += snprintf(buf+len, max_len-len, "  Tasks: %d active [%s]\n",
                    scheduler_task_count(), scheduler_task_names());
    // ... etc for each subsystem
}
```

---

## Tool Definitions

### 1. Filesystem Tools

#### read_file
```json
{
  "name": "read_file",
  "description": "Read the contents of a file from ChaosFS. Returns the file contents as text. Use this to inspect config files, Lua scripts, logs, or any text file on the filesystem.",
  "input_schema": {
    "type": "object",
    "properties": {
      "path": {
        "type": "string",
        "description": "Absolute file path, e.g. /system/gui/theme.lua"
      }
    },
    "required": ["path"]
  }
}
```
**Executor:** `vfs_open(path) → vfs_read() → vfs_close() → return contents`
**Returns:** File contents as string, or error message if file not found.

#### write_file
```json
{
  "name": "write_file",
  "description": "Write or create a file on ChaosFS. Overwrites existing files. Use this to create scripts, modify configs, save data, or update GUI Lua files.",
  "input_schema": {
    "type": "object",
    "properties": {
      "path": {
        "type": "string",
        "description": "Absolute file path to write to"
      },
      "content": {
        "type": "string",
        "description": "Content to write to the file"
      }
    },
    "required": ["path", "content"]
  }
}
```
**Executor:** `vfs_create(path, content, strlen(content))`
**Returns:** `"Written {N} bytes to {path}"` or error message.

#### list_dir
```json
{
  "name": "list_dir",
  "description": "List files and directories at a given path on ChaosFS. Returns names, types (file/directory), and sizes.",
  "input_schema": {
    "type": "object",
    "properties": {
      "path": {
        "type": "string",
        "description": "Directory path, e.g. / or /system/gui/widgets"
      }
    },
    "required": ["path"]
  }
}
```
**Executor:** `vfs_list(path)` → format as readable listing
**Returns:** Formatted directory listing with types and sizes.

#### delete_file
```json
{
  "name": "delete_file",
  "description": "Delete a file from ChaosFS. Cannot delete directories (must be empty first).",
  "input_schema": {
    "type": "object",
    "properties": {
      "path": {
        "type": "string",
        "description": "File path to delete"
      }
    },
    "required": ["path"]
  }
}
```
**Executor:** `vfs_unlink(path)`
**Returns:** `"Deleted {path}"` or error.

#### file_info
```json
{
  "name": "file_info",
  "description": "Get information about a file or directory: size, type, and whether it exists.",
  "input_schema": {
    "type": "object",
    "properties": {
      "path": {
        "type": "string",
        "description": "File path to inspect"
      }
    },
    "required": ["path"]
  }
}
```
**Executor:** `vfs_stat(path)` → format result
**Returns:** `"File: {path}, Size: {N} bytes, Type: file|directory"` or `"Not found"`.

---

### 2. System Tools

#### sysinfo
```json
{
  "name": "sysinfo",
  "description": "Get current system information including memory usage, uptime, running tasks, network status, audio status, and GUI state.",
  "input_schema": {
    "type": "object",
    "properties": {}
  }
}
```
**Executor:** `build_sysinfo_string()`
**Returns:** Same format as the auto-injected SYSINFO block, but freshly generated.

#### list_tasks
```json
{
  "name": "list_tasks",
  "description": "List all running tasks/processes with their IDs, names, states, and memory usage.",
  "input_schema": {
    "type": "object",
    "properties": {}
  }
}
```
**Executor:** Walk the scheduler's task list
**Returns:** Formatted table of tasks.

#### kill_task
```json
{
  "name": "kill_task",
  "description": "Kill a running task by its ID. Use with caution — killing system tasks may cause instability. Cannot kill kernel_idle or claude_shell.",
  "input_schema": {
    "type": "object",
    "properties": {
      "task_id": {
        "type": "integer",
        "description": "Task ID from list_tasks"
      }
    },
    "required": ["task_id"]
  }
}
```
**Executor:** `scheduler_kill_task(id)` — with protection for critical tasks
**Returns:** `"Killed task {id} ({name})"` or `"Cannot kill protected task"`.

#### run_command
```json
{
  "name": "run_command",
  "description": "Execute a CLAOS shell command and return the output. Same as typing it at the claos> prompt.",
  "input_schema": {
    "type": "object",
    "properties": {
      "command": {
        "type": "string",
        "description": "Shell command to execute, e.g. 'sysinfo' or 'ls /scripts'"
      }
    },
    "required": ["command"]
  }
}
```
**Executor:** Feed command string to the shell command parser, capture output
**Returns:** Command output as string.

---

### 3. GUI Tools

#### read_theme
```json
{
  "name": "read_theme",
  "description": "Read the current GUI theme settings. Returns the active theme name and key color/dimension values.",
  "input_schema": {
    "type": "object",
    "properties": {}
  }
}
```
**Executor:** Read and parse `/system/gui/theme.lua`, extract active theme, load its values
**Returns:** Theme name + key values as formatted text.

#### set_theme
```json
{
  "name": "set_theme",
  "description": "Switch the GUI theme. Available themes: 'light', 'dark'. Automatically reloads the GUI.",
  "input_schema": {
    "type": "object",
    "properties": {
      "theme": {
        "type": "string",
        "enum": ["light", "dark"],
        "description": "Theme name to activate"
      }
    },
    "required": ["theme"]
  }
}
```
**Executor:** Modify `/system/gui/theme.lua` → trigger GUI reload
**Returns:** `"Switched to {theme} theme and reloaded GUI"`.

#### reload_gui
```json
{
  "name": "reload_gui",
  "description": "Reload the GUI by re-executing /system/gui/init.lua. Use after modifying any GUI Lua files to apply changes without rebooting.",
  "input_schema": {
    "type": "object",
    "properties": {}
  }
}
```
**Executor:** Signal the GUI task to re-run `dofile("/system/gui/init.lua")`
**Returns:** `"GUI reloaded successfully"` or error.

#### list_windows
```json
{
  "name": "list_windows",
  "description": "List all open GUI windows with their titles, positions, sizes, and focus state.",
  "input_schema": {
    "type": "object",
    "properties": {}
  }
}
```
**Executor:** Query the window manager state from Lua
**Returns:** Formatted window list.

#### screenshot
```json
{
  "name": "screenshot",
  "description": "Capture the current screen contents and save as a raw image file on ChaosFS. Returns the file path.",
  "input_schema": {
    "type": "object",
    "properties": {
      "path": {
        "type": "string",
        "description": "Where to save the screenshot, e.g. /screenshots/current.raw"
      }
    },
    "required": ["path"]
  }
}
```
**Executor:** Copy framebuffer contents to a file (raw ARGB or simple BMP)
**Returns:** `"Screenshot saved to {path} (1024x768, 3145728 bytes)"`.

---

### 4. Network Tools

#### net_status
```json
{
  "name": "net_status",
  "description": "Get current network configuration and connection status.",
  "input_schema": {
    "type": "object",
    "properties": {}
  }
}
```
**Executor:** Read network stack state
**Returns:** IP, gateway, DNS, NIC status, connection state, ARP cache.

#### dns_lookup
```json
{
  "name": "dns_lookup",
  "description": "Resolve a hostname to an IP address using the DNS resolver.",
  "input_schema": {
    "type": "object",
    "properties": {
      "hostname": {
        "type": "string",
        "description": "Hostname to resolve, e.g. example.com"
      }
    },
    "required": ["hostname"]
  }
}
```
**Executor:** `dns_resolve(hostname)` → return IP
**Returns:** `"{hostname} → {ip_address}"` or `"DNS lookup failed"`.

#### http_get
```json
{
  "name": "http_get",
  "description": "Fetch the contents of an HTTP URL. HTTPS is supported. Returns the response body (truncated to 4KB if larger). Use this to check websites, fetch data, or test connectivity.",
  "input_schema": {
    "type": "object",
    "properties": {
      "url": {
        "type": "string",
        "description": "Full URL including scheme, e.g. https://example.com"
      }
    },
    "required": ["url"]
  }
}
```
**Executor:** Parse URL → DNS resolve → TCP connect → TLS handshake (if HTTPS) → HTTP GET → return body
**Returns:** Response body (first 4KB) + status code.

---

### 5. Audio Tools

#### play_tone
```json
{
  "name": "play_tone",
  "description": "Play a sine wave tone through the AC97 audio system.",
  "input_schema": {
    "type": "object",
    "properties": {
      "frequency": {
        "type": "integer",
        "description": "Frequency in Hz (20-20000)"
      },
      "duration": {
        "type": "integer",
        "description": "Duration in milliseconds"
      }
    },
    "required": ["frequency", "duration"]
  }
}
```
**Executor:** `ac97_play_tone(freq, duration)`
**Returns:** `"Playing {freq}Hz for {duration}ms"`.

#### play_sound
```json
{
  "name": "play_sound",
  "description": "Play a WAV audio file from ChaosFS.",
  "input_schema": {
    "type": "object",
    "properties": {
      "path": {
        "type": "string",
        "description": "Path to .wav file on ChaosFS"
      }
    },
    "required": ["path"]
  }
}
```
**Executor:** Load WAV → parse header → feed samples to AC97
**Returns:** `"Playing {path} ({duration}s, {sample_rate}Hz)"` or error.

#### set_volume
```json
{
  "name": "set_volume",
  "description": "Set the system audio volume.",
  "input_schema": {
    "type": "object",
    "properties": {
      "level": {
        "type": "integer",
        "description": "Volume level 0-100"
      }
    },
    "required": ["level"]
  }
}
```
**Executor:** `ac97_set_volume(level)`
**Returns:** `"Volume set to {level}%"`.

#### stop_audio
```json
{
  "name": "stop_audio",
  "description": "Stop any currently playing audio.",
  "input_schema": {
    "type": "object",
    "properties": {}
  }
}
```
**Executor:** `ac97_stop()`
**Returns:** `"Audio stopped"`.

---

### 6. The Nuclear Option: run_lua

```json
{
  "name": "run_lua",
  "description": "Execute arbitrary Lua code in the CLAOS Lua runtime. Has full access to all CLAOS APIs: claos.gui.* (2D drawing), claos.gui3d.* (3D rendering), claos.ask() (recursive Claude calls), claos.read/write() (files), claos.audio.* (sound), and all standard Lua libraries. Use this for anything the other tools don't cover. The code runs in the same Lua state as the GUI, so it can modify windows, widgets, and themes directly. Returns whatever the Lua code prints or returns.",
  "input_schema": {
    "type": "object",
    "properties": {
      "code": {
        "type": "string",
        "description": "Lua code to execute. Can be multiple lines. Last expression's value is returned."
      }
    },
    "required": ["code"]
  }
}
```
**Executor:** `luaL_dostring(L, code)` → capture output and return value
**Returns:** Lua print output + return value, or error message with line number.

**Examples of what run_lua enables:**

```lua
-- Change a specific color in the theme without rewriting the whole file
local theme = dofile("/system/gui/theme.lua")
theme.accent = claos.gui.rgb(255, 100, 50)  -- orange accent!

-- Draw a 3D spinning cube on the desktop right now
local angle = 0
for i = 1, 100 do
    claos.gui3d.clear()
    claos.gui3d.rotate_cube(angle)
    claos.gui3d.render_to_screen()
    claos.gui.swap()
    angle = angle + 0.05
    claos.sleep(16)
end

-- Create a brand new GUI widget from scratch
claos.write("/system/gui/widgets/clock.lua", [[
    local clock = {}
    function clock.draw(x, y, theme)
        local time = claos.uptime_formatted()
        claos.gui.rounded_rect(x, y, 120, 40, 8, theme.bg_card)
        claos.gui.text(x + 10, y + 12, time, theme.text_primary)
    end
    return clock
]])
claos.gui.reload_theme()

-- Play a melody
local notes = {440, 494, 523, 587, 659, 698, 784, 880}
for _, freq in ipairs(notes) do
    claos.audio.tone(freq, 200)
    claos.sleep(250)
end

-- Check what's using the most memory
local tasks = claos.list_tasks()
table.sort(tasks, function(a, b) return a.memory > b.memory end)
for _, t in ipairs(tasks) do
    print(t.name .. ": " .. t.memory .. " KB")
end
```

---

## Multi-Turn Tool Execution Flow

When Claude uses tools, the conversation requires multiple API round trips:

```
Round 1:
  CLAOS sends: system_prompt + tools + user_message
  Claude responds: "Let me check that for you." + tool_use(read_file, "/system/gui/theme.lua")

Round 2:
  CLAOS executes: read_file("/system/gui/theme.lua") → gets file contents
  CLAOS sends: tool_result with file contents
  Claude responds: "I can see the theme is set to light. I'll switch it." + tool_use(write_file, ...) + tool_use(reload_gui)

Round 3:
  CLAOS executes: write_file(...) → "Written 85 bytes"
  CLAOS executes: reload_gui() → "GUI reloaded"
  CLAOS sends: both tool_results
  Claude responds: "Done! I've switched to dark mode. How does it look?"

  → This final text response is displayed to the user.
```

### Implementation in C

```c
#define MAX_TOOL_ROUNDS 10  /* prevent infinite loops */

void claude_ask_with_tools(const char* user_message, char* response_buf, int buf_size) {
    /* Build messages array */
    /* First message: user's prompt */
    
    for (int round = 0; round < MAX_TOOL_ROUNDS; round++) {
        /* Send request with system prompt + tools + conversation history */
        char* api_response = https_post_claude(system_prompt, tools_json,
                                                messages, message_count);
        
        /* Parse response */
        int stop_reason = parse_stop_reason(api_response);
        
        if (stop_reason == STOP_END_TURN) {
            /* Claude is done — extract text and return to user */
            extract_text_response(api_response, response_buf, buf_size);
            return;
        }
        
        if (stop_reason == STOP_TOOL_USE) {
            /* Claude wants to use tools */
            int num_tool_calls = parse_tool_calls(api_response, tool_calls);
            
            /* Add Claude's response (with tool_use) to conversation history */
            add_assistant_message(messages, &message_count, api_response);
            
            /* Execute each tool call and collect results */
            for (int i = 0; i < num_tool_calls; i++) {
                char result[4096];
                execute_tool(tool_calls[i].name, tool_calls[i].args, result, sizeof(result));
                add_tool_result(messages, &message_count,
                               tool_calls[i].id, result);
            }
            
            /* Loop back to send results to Claude */
            continue;
        }
    }
    
    /* Safety: max rounds exceeded */
    snprintf(response_buf, buf_size, "[CLAOS] Tool execution limit reached.");
}
```

### Tool Executor Dispatch

```c
void execute_tool(const char* name, const char* args_json, char* result, int max_len) {
    
    /* Filesystem tools */
    if (strcmp(name, "read_file") == 0) {
        char path[256];
        json_get_string(args_json, "path", path, sizeof(path));
        tool_read_file(path, result, max_len);
    }
    else if (strcmp(name, "write_file") == 0) {
        char path[256], content[8192];
        json_get_string(args_json, "path", path, sizeof(path));
        json_get_string(args_json, "content", content, sizeof(content));
        tool_write_file(path, content, result, max_len);
    }
    else if (strcmp(name, "list_dir") == 0) {
        char path[256];
        json_get_string(args_json, "path", path, sizeof(path));
        tool_list_dir(path, result, max_len);
    }
    else if (strcmp(name, "delete_file") == 0) {
        char path[256];
        json_get_string(args_json, "path", path, sizeof(path));
        tool_delete_file(path, result, max_len);
    }
    else if (strcmp(name, "file_info") == 0) {
        char path[256];
        json_get_string(args_json, "path", path, sizeof(path));
        tool_file_info(path, result, max_len);
    }
    
    /* System tools */
    else if (strcmp(name, "sysinfo") == 0) {
        build_sysinfo_string(result, max_len);
    }
    else if (strcmp(name, "list_tasks") == 0) {
        tool_list_tasks(result, max_len);
    }
    else if (strcmp(name, "kill_task") == 0) {
        int task_id = json_get_int(args_json, "task_id");
        tool_kill_task(task_id, result, max_len);
    }
    else if (strcmp(name, "run_command") == 0) {
        char cmd[512];
        json_get_string(args_json, "command", cmd, sizeof(cmd));
        tool_run_command(cmd, result, max_len);
    }
    
    /* GUI tools */
    else if (strcmp(name, "read_theme") == 0) {
        tool_read_theme(result, max_len);
    }
    else if (strcmp(name, "set_theme") == 0) {
        char theme[32];
        json_get_string(args_json, "theme", theme, sizeof(theme));
        tool_set_theme(theme, result, max_len);
    }
    else if (strcmp(name, "reload_gui") == 0) {
        tool_reload_gui(result, max_len);
    }
    else if (strcmp(name, "list_windows") == 0) {
        tool_list_windows(result, max_len);
    }
    else if (strcmp(name, "screenshot") == 0) {
        char path[256];
        json_get_string(args_json, "path", path, sizeof(path));
        tool_screenshot(path, result, max_len);
    }
    
    /* Network tools */
    else if (strcmp(name, "net_status") == 0) {
        tool_net_status(result, max_len);
    }
    else if (strcmp(name, "dns_lookup") == 0) {
        char hostname[256];
        json_get_string(args_json, "hostname", hostname, sizeof(hostname));
        tool_dns_lookup(hostname, result, max_len);
    }
    else if (strcmp(name, "http_get") == 0) {
        char url[512];
        json_get_string(args_json, "url", url, sizeof(url));
        tool_http_get(url, result, max_len);
    }
    
    /* Audio tools */
    else if (strcmp(name, "play_tone") == 0) {
        int freq = json_get_int(args_json, "frequency");
        int dur = json_get_int(args_json, "duration");
        tool_play_tone(freq, dur, result, max_len);
    }
    else if (strcmp(name, "play_sound") == 0) {
        char path[256];
        json_get_string(args_json, "path", path, sizeof(path));
        tool_play_sound(path, result, max_len);
    }
    else if (strcmp(name, "set_volume") == 0) {
        int level = json_get_int(args_json, "level");
        tool_set_volume(level, result, max_len);
    }
    else if (strcmp(name, "stop_audio") == 0) {
        ac97_stop();
        snprintf(result, max_len, "Audio stopped");
    }
    
    /* The nuclear option */
    else if (strcmp(name, "run_lua") == 0) {
        char code[8192];
        json_get_string(args_json, "code", code, sizeof(code));
        tool_run_lua(code, result, max_len);
    }
    
    else {
        snprintf(result, max_len, "Unknown tool: %s", name);
    }
}
```

---

## Safety Considerations

### Protected Operations
- `kill_task` cannot kill: `kernel_idle`, `claude_shell`, `gui_desktop`, `net_listener`
- `write_file` cannot overwrite `/system/kernel/` (if we ever add that path)
- `delete_file` cannot delete directories that contain system files
- `run_lua` runs in a sandboxed Lua state with a timeout (prevent infinite loops)

### Resource Limits
- `run_lua` has a 5-second execution timeout — if the script runs longer, it's killed
- `http_get` response truncated to 4KB to prevent memory exhaustion
- `write_file` content limited to 32KB per call
- `MAX_TOOL_ROUNDS` = 10 to prevent infinite tool-calling loops
- Each API request has a max_tokens limit of 4096

### Audit Log
Every tool execution is logged to `/logs/claude_tools.log`:
```
[00:47:23] TOOL: read_file("/system/gui/theme.lua") → OK (234 bytes)
[00:47:24] TOOL: write_file("/system/gui/theme.lua") → OK (wrote 85 bytes)
[00:47:24] TOOL: reload_gui() → OK
[00:47:25] TOOL: run_lua("claos.audio.tone(440, 200)") → OK
```

---

## JSON Parsing Requirements

The tool system requires more robust JSON parsing than the current minimal parser. The current parser only extracts `content[0].text` from API responses. The tool system needs to:

1. Detect `stop_reason: "tool_use"` vs `stop_reason: "end_turn"`
2. Parse `tool_use` content blocks: extract `id`, `name`, and `input` (nested JSON object)
3. Build `tool_result` content blocks with the matching `tool_use_id`
4. Handle multiple tool calls in a single response
5. Handle multiple content blocks (text + tool_use mixed)

This likely means upgrading `claude/json.c` from a simple string-search parser to a proper minimal JSON parser. Still doesn't need to be a full JSON library — just enough to handle the Anthropic API response format.

Alternatively, this could be done in Lua — parse the JSON response in Lua, execute tools via Lua bindings, build the tool_result JSON in Lua. This leverages Lua's string handling which is much easier than doing complex JSON manipulation in C.

---

## API Request Format (with tools)

```json
{
  "model": "claude-sonnet-4-20250514",
  "max_tokens": 4096,
  "system": "<system prompt with sysinfo>",
  "tools": [
    {"name": "read_file", "description": "...", "input_schema": {...}},
    {"name": "write_file", "description": "...", "input_schema": {...}},
    {"name": "list_dir", "description": "...", "input_schema": {...}},
    {"name": "delete_file", "description": "...", "input_schema": {...}},
    {"name": "file_info", "description": "...", "input_schema": {...}},
    {"name": "sysinfo", "description": "...", "input_schema": {...}},
    {"name": "list_tasks", "description": "...", "input_schema": {...}},
    {"name": "kill_task", "description": "...", "input_schema": {...}},
    {"name": "run_command", "description": "...", "input_schema": {...}},
    {"name": "read_theme", "description": "...", "input_schema": {...}},
    {"name": "set_theme", "description": "...", "input_schema": {...}},
    {"name": "reload_gui", "description": "...", "input_schema": {...}},
    {"name": "list_windows", "description": "...", "input_schema": {...}},
    {"name": "screenshot", "description": "...", "input_schema": {...}},
    {"name": "net_status", "description": "...", "input_schema": {...}},
    {"name": "dns_lookup", "description": "...", "input_schema": {...}},
    {"name": "http_get", "description": "...", "input_schema": {...}},
    {"name": "play_tone", "description": "...", "input_schema": {...}},
    {"name": "play_sound", "description": "...", "input_schema": {...}},
    {"name": "set_volume", "description": "...", "input_schema": {...}},
    {"name": "stop_audio", "description": "...", "input_schema": {...}},
    {"name": "run_lua", "description": "...", "input_schema": {...}}
  ],
  "messages": [
    {"role": "user", "content": "Switch to dark mode and play a happy sound"}
  ]
}
```

---

## Example Interactions

### Example 1: "What files are on my system?"
```
Claude: [tool_use: list_dir("/")]
Result: "D /system, D /scripts, D /docs, D /logs, D /games"

Claude: [tool_use: list_dir("/scripts")]
Result: "F /scripts/welcome.lua (340B), F /scripts/chat.lua (1.2K), ..."

Claude: "Here's what's on your ChaosFS:
  /system — GUI files, theme, widgets
  /scripts — Lua scripts (welcome, chat, sysmon, crash_test)
  /docs — documentation
  /logs — system and tool logs
  /games — game files

  Would you like me to look inside any of these?"
```

### Example 2: "Make the GUI purple-er"
```
Claude: [tool_use: read_file("/system/gui/themes/light.lua")]
Result: (current theme contents)

Claude: [tool_use: write_file("/system/gui/themes/light.lua", <modified with more purple>)]
Result: "Written 1247 bytes"

Claude: [tool_use: reload_gui()]
Result: "GUI reloaded"

Claude: "I've cranked up the purple across the light theme — the sidebar, 
  top bar, and card borders now have a stronger purple tint. How's that?"
```

### Example 3: "I think something is using too much memory"
```
Claude: [tool_use: sysinfo()]
Result: "Memory: 98/128 MB used (76%)..."

Claude: [tool_use: list_tasks()]
Result: "1:kernel_idle(1KB) 2:claude_shell(2KB) 3:gui_desktop(12MB) 
         4:lua_runtime(45MB) 5:net_listener(1KB)"

Claude: "Your Lua runtime is using 45MB — that's 35% of total RAM by itself. 
  This usually means Lua scripts are accumulating data without cleanup. 
  Want me to check which scripts are loaded and see if we can free some memory?"
```

### Example 4: "Play me a song"
```
Claude: [tool_use: run_lua("
  local notes = {
    {523, 200}, {523, 200}, {587, 200}, {659, 400},
    {587, 200}, {523, 200}, {494, 200}, {440, 400},
    {440, 200}, {494, 200}, {523, 400}, {494, 200}, {440, 400}
  }
  for _, n in ipairs(notes) do
    claos.audio.tone(n[1], n[2])
    claos.sleep(n[2] + 50)
  end
")]
Result: "OK"

Claude: "There you go — a little melody! That was approximately 'Twinkle Twinkle 
  Little Star' rendered in sine waves through the AC97 audio system. Want 
  something more complex? I could write a proper music player script."
```

---

## File Structure

```
claude/
├── claude.c              # Main protocol layer (updated for tools)
├── claude_tools.c        # Tool executor dispatch
├── claude_tools.h        # Tool function declarations
├── claude_system.c       # System prompt builder + sysinfo generator
├── json.c                # Upgraded JSON parser (tool_use support)
├── json.h                # JSON parser API
├── config.h              # API key
└── panic_handler.c       # Panic → Claude (unchanged)
```

---

## Milestones

**M1 — JSON parser upgraded:**
Can parse `stop_reason`, `tool_use` blocks, nested `input` objects. Can build `tool_result` messages.

**M2 — Multi-turn loop working:**
Claude can make a tool call, CLAOS executes it, sends result, gets final response. Test with `sysinfo` tool.

**M3 — Filesystem tools working:**
`read_file`, `write_file`, `list_dir`, `delete_file`, `file_info` all functional.

**M4 — System + GUI tools working:**
`list_tasks`, `kill_task`, `set_theme`, `reload_gui`, `screenshot`.

**M5 — Network + Audio tools working:**
`http_get`, `dns_lookup`, `play_tone`, `play_sound`.

**M6 — run_lua working:**
Claude can execute arbitrary Lua with timeout protection. Full CLAOS API accessible.

**M7 — System prompt + auto-inject working:**
Every conversation includes fresh sysinfo. Claude knows where it is.

**M8 — The moment:**
Ask Claude to do something complex: "Set up a dark theme, create a new widget that shows a clock, add it to the desktop, and play a celebration sound when it's done." Watch it chain 5+ tool calls to make it happen.

---

## Success Criteria

Claude inside CLAOS can:
1. Read and write files on ChaosFS without being asked to use specific paths
2. Switch themes and reload the GUI in one interaction
3. Diagnose system issues by checking tasks and memory
4. Execute arbitrary Lua code to do things no specific tool covers
5. Play sounds and music
6. Fetch web content via HTTP/HTTPS
7. Know its own system state at all times via auto-injected context
8. Chain multiple tools in a single interaction to accomplish complex tasks
9. Log all tool usage for transparency

The AI doesn't just live in the OS. It runs the OS.
