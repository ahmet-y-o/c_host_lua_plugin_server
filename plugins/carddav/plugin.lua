app = require("core")


app.get("/test2", function (req)
    return app.render("index", {method = 1})
        :status(200)
        :header("Content-Type", "text/html")
end)