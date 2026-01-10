app = require("core")

app.get("/crash", function(req)
    app.error("SABOTAGE: Triggering a Nil error!")
    local x = nil
    return x.property -- This will cause a Lua runtime error
end)

app.get("/freeze", function(req)
    app.warn("SABOTAGE: Entering infinite loop!")
    while true do
        -- This thread is now stuck forever
    end
end)