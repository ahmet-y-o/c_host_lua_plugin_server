/* Auto-generated from app.lua */
#ifndef APP_LUA_H
#define APP_LUA_H

const char* app_lua_source =
    "local core = {}\n"
    "core.routes = {} -- Table to store handlers\n"
    "\n"
    "-- The generic registration function\n"
    "function core.match(method, path, handler)\n"
    "    method = method:upper()\n"
    "    if not core.routes[method] then\n"
    "        core.routes[method] = {}\n"
    "    end\n"
    "    core.routes[method][path] = handler\n"
    "end\n"
    "\n"
    "-- Shortcuts for common methods\n"
    "function core.get(path, handler) core.match(\"GET\", path, handler) end\n"
    "function core.post(path, handler) core.match(\"POST\", path, handler) end\n"
    "\n"
    "-- Shortcut for WebDAV / Custom methods\n"
    "function core.propfind(path, handler) core.match(\"PROPFIND\", path, handler) end\n";

#endif /* APP_LUA_H */
