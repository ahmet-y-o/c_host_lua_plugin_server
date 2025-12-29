app = require("core")


app.get("/", function (req)
    return app.render("index", {})
        :status(200)        
end)

app.get("/test", function (req)
    return app.render("index", {hello = "hello mars"})
        :status(200)        
end)

function log_event(data)
    print("[EVENT LOG] " .. data.message)
end

-- Register to listen for "audit_log"
app.on("audit_log", "log_event")