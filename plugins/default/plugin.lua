app = require("core")


app.get("/", function (req)
    return app.render("index", {})
        :status(200)        
end)

app.get("/test", function (req)
    -- This now works because C transfers the table and returns the result!
    local result = app.emit("inventory_get", {})
    return app.render("index", {hello = result})
        :status(200)        
end)

function log_event(data)
    print("[EVENT LOG] " .. data.message)
end

-- Register to listen for "audit_log"
app.on_event("audit_log", "log_event")