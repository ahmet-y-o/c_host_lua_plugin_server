app = require("core")


app.get("/", function (req)
    return app.render("index", {})
        :status(200)        
end)

app.get("/test", function (req)
    
    local result = app.query("inv")
    return app.render("index", {hello = result})
        :status(200)        
end)

function log_event(data)
    print("[EVENT LOG] " .. data.message)
end

-- Register event as "audit_log"
app.on_event("audit_log", "log_event")