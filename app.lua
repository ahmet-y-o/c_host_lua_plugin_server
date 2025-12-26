local core = {}
core.routes = {} -- Table to store handlers

-- The generic registration function
function core.match(method, path, handler)
    method = method:upper()
    if not core.routes[method] then
        core.routes[method] = {}
    end
    core.routes[method][path] = handler
end

-- Shortcuts for common methods
function core.get(path, handler) core.match("GET", path, handler) end
function core.post(path, handler) core.match("POST", path, handler) end

-- Shortcut for WebDAV / Custom methods
function core.propfind(path, handler) core.match("PROPFIND", path, handler) end