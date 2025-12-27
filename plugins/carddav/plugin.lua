app = require("core")


app.get("/", function(req)
    return "CardDAV root reached!"
end)

app.get("/test", function(req)
    return "CardDAV test reached!"
end)