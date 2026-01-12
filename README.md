# C-Webserver with Lua Plugin Architecture

A high-performance web server built in C using **GNU libmicrohttpd**, featuring a robust plugin system powered by **Lua**. This server is designed for extensibility and security, providing each plugin with its own sandboxed environment and persistent SQLite storage.

## Core Features

* **Sandboxed Lua Plugins**: Each plugin runs in an isolated Lua state to ensure security and stability.
* **Hook System**: Facilitates communication between plugins using both synchronous and asynchronous hooks.
* **Persistent Storage**: Automatic schema-based SQLite database management for every plugin.
* **Server-Side Templating**: Integrated support for `etlua` for dynamic HTML rendering.
* **Asynchronous Processing**: Non-blocking hook execution for long-running tasks.

## Architecture Overview

The server acts as a host that manages the lifecycle of the plugins. When an HTTP request arrives, the server routes the data to the appropriate Lua environment.

### Plugin Isolation and Storage

Every plugin is assigned a private directory and a dedicated SQLite database. The schema is defined within the Lua script, and the C backend handles the initialization and migrations.

## Getting Started

### Prerequisites

* libmicrohttpd
* Lua 5.4 or Higher
* SQLite3
* C Compiler (GCC or Clang)
* libcjson

### Installation

1. Clone the repository:
```bash
git clone https://github.com/ahmet-y-o/c_host_lua_plugin_server
cd c_host_lua_plugin_server

```


2. Build the project:
```bash
mkdir build && cd build
cmake ..
make

```


3. Run the server:
```bash
./main.out

```



## Plugin Development

Plugins are written in Lua and placed in the designated plugins folder. Below is a basic example of a plugin utilizing the schema and hook system.

### Example Plugin (`plugin.lua`)

```lua
app = require("core")

schema = {
    products = {
        id = "INTEGER PRIMARY KEY AUTOINCREMENT",
        sku = "TEXT UNIQUE NOT NULL",
        name = "TEXT NOT NULL",
        quantity = "INTEGER DEFAULT 0",
    }
}

app.get("/", function(req)
    local items = db_query("SELECT * FROM products")
    return app.render("index", {items = items})
end)


app.get("/new-item", function (req)
    return app.render("new-item", {})
end)

app.get("/[item-id]", function (req)
    local query = string.format("SELECT * FROM products WHERE id = '%s'", req.params["item-id"])
    local items = db_query(query)
    local item = items[1]
    return app.render("view-item", {item = item})
end)

app.post("/new-item", function (req)
    local query = string.format(
        "INSERT INTO products (sku, name, quantity) VALUES ('%s', '%s', %s)",
        req.form.sku, req.form.name, req.form.quantity
    )
    db_exec(query)
    return app.redirect("/inventory")
end)


function slow_background_task(data)
    print("[WORKER] Starting heavy work for: " .. data.id)
    
    -- Simulate 3 seconds of work
    local start = os.clock()
    while os.clock() - start < 3 do end 
    
    print("[WORKER] Finished heavy work for: " .. data.id)
end

-- other plugins may call this function with app.emit("slow") for async
-- or app.query("slow") for sync invoking
app.emit_handle("slow", "slow_background_task")
```

## Hook System

The server implements a pub/sub model for inter-plugin communication:

| Hook Type | Description |
| --- | --- |
| **Sync Hook** | Executes immediately; the caller waits for a return value. |
| **Async Hook** | Pushed to a background thread pool; ideal for I/O or heavy computation. |