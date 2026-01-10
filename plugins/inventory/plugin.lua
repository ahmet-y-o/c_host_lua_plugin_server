app = require("core")

schema = {
    products = {
        id = "INTEGER PRIMARY KEY AUTOINCREMENT",
        sku = "TEXT UNIQUE NOT NULL",
        name = "TEXT NOT NULL",
        quantity = "INTEGER DEFAULT 0",
    }
}

app.get("/", function(req)
    local items = db_query("SELECT * FROM products")
    return app.render("index", {items = items})
end)

app.get("/new-item", function (req)
    return app.render("new-item", {})
end)

app.post("/new-item", function (req)
    print("in post")
    local query = string.format(
        "INSERT INTO products (sku, name, quantity) VALUES ('%s', '%s', %s)",
        req.form.sku, req.form.name, req.form.quantity
    )
    print(query)
    db_exec(query)
    return app.redirect("/inventory")
end)

app.post("/posttest", function(req)
    app.info("Method: " .. req.method)
    app.info("Body Length: " .. #req.body)
    app.info("Body Content: " .. req.body)
    return "Post data is " .. req.body
end)
