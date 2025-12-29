local core = {}
core.routes = {}
local etlua = require("etlua")
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

-- Routing logic
function core.match(method, path, handler)
    method = method:upper()
    core.routes[method] = core.routes[method] or {}
    core.routes[method][path] = handler
end

function core.get(path, handler) core.match("GET", path, handler) end

-- Updated Dispatcher
function core.handle_request(req)
    core.info(req.method .. " " .. req.url)
    local method = req.method:upper()
    local handler = core.routes[method] and core.routes[method][req.url]
    
    if handler then
        local ok, result = pcall(handler, req)
        if not ok then
            core.error("Handler Error: " .. tostring(result))
            return { status = 500, body = "Internal Server Error", headers = {} }
        end

        -- If the user returned a simple string, wrap it in a default response
        if type(result) == "string" then
            result = create_response(result)
        end

        -- Ensure it's a table before returning to C
        return {
            status = result.status_code or 200,
            body = result.body or "",
            headers = result.headers or {}
        }
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
