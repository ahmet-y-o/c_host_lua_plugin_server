/* Auto-generated from app.lua */
#ifndef APP_LUA_H
#define APP_LUA_H

const char* app_lua_source = R"lua(
local core = {}
core.routes = {}
local etlua = require("etlua")

-- QUERIES (Synchronous)
-- Used when you need an immediate answer from another plugin.
function core.query_handle(name, func_name) 
    c_register_hook(name, func_name) 
end

function core.query(name, data) 
    return c_call_hook(name, data or {}) 
end

-- EMITS (Asynchronous Events)
-- Used to broadcast that something happened. Handlers run in background.
function core.emit_handle(name, func_name) 
    c_register_hook(name, func_name) 
end

function core.emit(name, data) 
    return c_trigger_async_event(name, data or {}) 
end

-- DEFER (Direct Asynchronous Task)
-- Used to offload a specific, known function to the background.
function core.defer(func_name, data) 
    c_enqueue_job(func_name, data or {}) 
end

local function parse_route(path)
    local param_names = {}
    
    -- 1. Save the parameter names first
    for name in path:gmatch("%[([^%]]+)%]") do
        table.insert(param_names, name)
    end

    -- 2. Escape special Lua characters (. % + - * ? ^ $ etc.)
    -- but EXCLUDE the brackets [ ] for a moment so we can find them easily
    local pattern = path:gsub("([%(%)%.%%%+%-%*%?%^%$])", "%%%1")
    
    -- 3. Now replace [something] with the capture group ([^/]+)
    -- We use .- for a non-greedy match inside the brackets
    pattern = pattern:gsub("%[.-%]", "([^/]+)")
    
    return "^" .. pattern .. "$", param_names
end


-- Helper to create a response object with chainable methods
-- Internal helper to create the chainable response
local function create_response(body)
    local resp = {
        status_code = 200,
        body = body or "",
        headers = { ["Content-Type"] = "text/html" }
    }
    function resp:status(code)
        self.status_code = code; return self
    end

    function resp:header(k, v)
        self.headers[k] = v; return self
    end

    function resp:type(mime_type)
        self.headers["Content-Type"] = mime_type
        return self
    end

    return resp
end

-- Render function
function core.render(view_name, data)
    -- 1. Security Check: Block directory traversal attempts
    if view_name:find("%.%.") then
        return create_response("Security Error: Invalid view name"):status(403)
    end

    -- 2. Normalize PLUGIN_DIR: Remove trailing slash if it exists, then add one
    local base_path = PLUGIN_DIR:gsub("/$", "") .. "/"

    -- 3. Construct absolute path
    local path = base_path .. "views/" .. view_name .. ".etlua"

    -- 4. Safe File Loading
    local f = io.open(path, "r")
    if not f then
        core.error("Render Error: File not found at " .. path) -- Use logger!
        return create_response("Template not found"):status(500)
    end

    local content = f:read("*a")
    f:close()

    -- 5. Robust Compilation & Execution
    -- We use pcall to ensure a Lua error in the template doesn't crash the request
    local ok_compile, template = pcall(etlua.compile, content)
    if not ok_compile then
        return create_response("Template Syntax Error: " .. tostring(template)):status(500)
    end

    local ok_render, html = pcall(template, data)
    if not ok_render then
        return create_response("Template Runtime Error: " .. tostring(html)):status(500)
    end

    -- Explicitly set HTML type since we are rendering a template
    return create_response(html):type("text/html")
end

-- Redirect function
function core.redirect(url, status_code)
    -- Default to 302 Found (Temporary Redirect) if no status is provided
    local status = status_code or 302
    
    -- We create an empty response body because the browser follows the header
    return create_response("")
        :status(status)
        :header("Location", url)
end

local function url_decode(str)
    if not str then return "" end
    str = str:gsub("+", " ")
    str = str:gsub("%%(%x%x)", function(h) 
        return string.char(tonumber(h, 16)) 
    end)
    return str
end

function core.parse_form(body)
    local data = {}
    if not body or body == "" then 
        return data 
    end

    for key, value in body:gmatch("([^&=]+)=([^&]*)") do
        local d_key = url_decode(key)
        local d_val = url_decode(value)
        data[d_key] = d_val
    end

    return data
end
function core.memory_kb()
    return c_get_memory()
end

-- Routing logic
function core.match(method, path, handler)
    local pattern, keys = parse_route(path)
    table.insert(core.routes, {
        method = method:upper(),
        pattern = pattern,
        keys = keys,
        handler = handler
    })
end

function core.get(path, handler) core.match("GET", path, handler) end
function core.post(path, handler) core.match("POST", path, handler) end


-- Dispatcher
function core.handle_request(req)
    local method = req.method:upper()
    req.form = {}
    if method == "POST" or method == "PUT" then
        -- for now, assume form encoded body    
        req.form = core.parse_form(req.body or "")
        for k,v in ipairs(core.parse_form(req.body or "")) do
            print(k)
            print(v)
        end
    end
    
    for _, route in ipairs(core.routes) do
        if route.method == method then
            -- match() returns all captures as multiple return values
            local matches = { req.url:match(route.pattern) }
            
            if #matches > 0 then
                req.params = {}
                for i, name in ipairs(route.keys) do
                    req.params[name] = matches[i]
                end

                local result = route.handler(req)
                
                -- Wrap simple string responses
                if type(result) == "string" then
                    result = { status_code = 200, body = result, headers = {["Content-Type"]="text/html"} }
                end

                return {
                    status = result.status_code or 200,
                    body = result.body or "",
                    headers = result.headers or {}
                }
            end
        end
    end
    
    return { status = 404, body = "Not Found", headers = {} }
end



-- Wrapper for the C-logging function
function core.log(level, msg)
    if c_log then
        c_log(level:upper(), tostring(msg))
    else
        -- Fallback if not running inside the C host
        print("[" .. level:upper() .. "] " .. tostring(msg))
    end
end

-- Syntax sugar for different levels
function core.info(msg)  core.log("INFO", msg) end
function core.warn(msg)  core.log("WARN", msg) end
function core.error(msg) core.log("ERROR", msg) end

return core
)lua";


#endif /* APP_LUA_H */
