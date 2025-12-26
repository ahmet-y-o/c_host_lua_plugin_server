local mytes = require("mytes")
local etlua = require("etlua")

function handle(request)
    if request.url == "/carddav" then
        if request.method == "GET" then
            return index_page(request)
        elseif request.method == "PROPFIND" then
            return {
                status = 200,
                body = "carddav propfind",
                headers = {
                    ["Content-Type"] = "text/plain"
                }
            }
        end
    else
        return not_found_page(request)
    end
end

function index_page(request)
    -- 1. Load file
    local file = io.open(PLUGIN_DIR.."views/index.etlua", "r")
    local content = file:read("*a")
    file:close()
    -- 2. Compile and Render
    local template = etlua.compile(content)
    local html = template({ title = "My Plugin Page", method = request.method })
    
    return {
        status = 200,
        body = html,
        headers = {
            ["Content-Type"] = "text/html",
        }
    }
end

function not_found_page(request)
    return {
        status = 200,
        body = "that page is not found. current page is:"..request.url,
        headers = {
            ["Content-Type"] = "text/html",
        }
    }
end

