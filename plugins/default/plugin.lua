app = require("core")


app.get("/", function (req)
    return app.render("index", {})
        :status(200)        
end)

app.get("/test", function (req)
    return app.render("index", {})
        :status(200)        
end)