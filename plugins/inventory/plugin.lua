app = require("core")

local warehouse = {
    ["Central_WH"] = {
        ["Bin_A1"] = { ["SKU_IRON"] = "150" }
    }
}

function handle_get_stock(data)
    print("invoking stock data")
    return warehouse
end
app.on_query("inventory_get", "handle_get_stock")

app.get("/", function (req)
    
    return app.render("index", {})
        :status(200)
    
end)