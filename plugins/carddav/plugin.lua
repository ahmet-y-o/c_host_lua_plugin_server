app = require("core")



app.get("/", function(req)
    app.emit("audit_log", {message="helo"})
    return app.emit("inv")
end)

app.get("/test", function(req)
    return "CardDAV test reached!"
end)

app.post("/posttest", function(req)
    app.info("Method: " .. req.method)
    app.info("Body Length: " .. #req.body)
    app.info("Body Content: " .. req.body)
    return "Post data is " .. req.body
end)
