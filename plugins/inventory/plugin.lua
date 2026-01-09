app = require("core")

local warehouse = {
    ["Central_WH"] = {
        ["Bin_A1"] = { ["SKU_IRON"] = "150" }
    }
}

function handle_get_stock(data)
    print("invoking stock data")
    return "warehouse"
end

app.get("/", function (req)
    app.query("audit_log")
    return app.render("index", {})
        :status(200)
    
end)

app.on_query("inv", "handle_get_stock")