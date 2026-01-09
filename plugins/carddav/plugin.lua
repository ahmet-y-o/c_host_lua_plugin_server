app = require("core")



app.get("/", function(req)
    app.emit("audit_log", {message="helo"})
    c_enqueue_job("slow_background_task", { 
        message = "Hello from the background!", 
        id = 123 
    })
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

-- This is the function the background worker will call
function slow_background_task(data)
    print("[WORKER] Starting heavy work for: " .. data.id)
    
    -- Simulate 3 seconds of work
    -- If you don't have a sleep function, just a long loop:
    local start = os.clock()
    while os.clock() - start < 3 do end 
    
    print("[WORKER] Finished heavy work for: " .. data.id)
end

-- Immediately enqueue a job to test the system
-- We tell it to call 'my_background_task' with a table of data
print("[MAIN] Enqueueing test job...")

