app = require("core")

schema = {
  items = {
    id = "INTEGER PRIMARY KEY AUTOINCREMENT",
    name = "TEXT NOT NULL",
    stock = "INTEGER DEFAULT 0",
    price = "REAL"
  }
}

function handle_get_stock(data)
  print("invoking stock data")
  return "warehouse"
end

app.get("/write", function(req)
  print("[Lua] Writing to DB: " .. "kalem")

  local sql = string.format(
    "INSERT INTO items (name, stock) VALUES ('%s', %d)",
    "kalem", 3
  )

  local ok, err = db_exec(sql)
  if not ok then
    print("[Lua] DB Error: " .. err)
  else
    print("[Lua] DB Write Successful!")
  end
  return app.render("index", { id = 12 })
      :status(200)
end)

app.get("/read", function(req)
  print("[Lua] Fetching inventory...")
  local rows = db_query("SELECT * FROM items")

  for i, row in ipairs(rows) do
    print(string.format("  [%d] Item: %s | Stock: %d", row.id, row.name, row.stock))
  end

  return app.render("index", { id = 12 })
      :status(200)
end)

app.query_handle("inv", "handle_get_stock")
