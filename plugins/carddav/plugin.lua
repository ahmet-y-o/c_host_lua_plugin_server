app = require("core")



app.get("/", function(req)
    app.defer("slow_background_task", { 
        id = 123 
    })
    return app.query("inv")
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

-- This is the function the background worker will call
function slow_background_task(data)
    print("[WORKER] Starting heavy work for: ")
    
    -- Simulate 3 seconds of work
    -- If you don't have a sleep function, just a long loop:
    local start = os.clock()
    while os.clock() - start < 3 do end 
    
    print("[WORKER] Finished heavy work for: ")
end

app.emit_handle("slow", "slow_background_task")

