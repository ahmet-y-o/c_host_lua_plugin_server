app = require("core")



app.get("/", function(req)
    return "CardDAV root reached!"
end)

app.get("/test", function(req)
    return "CardDAV test reached!"
end)

app.get("/[test]", function(req)
    return "CardDAV route " .. (req.params.test or "unknown") .. " reached!"
end)

app.get("/[test]/[hello]", function(req)
    return "CardDAV hello " .. (req.params.test or "unknown") .. " reached!"
end)