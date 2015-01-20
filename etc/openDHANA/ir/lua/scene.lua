-- Set the different home cinema scenes

function set_scene(scene)
    if scene == "off" then
        -- Turn off everything
        send_ir("standby", "Yamaha receiver")
        send_ir("power off", "Samsung TV")
        end
    
    if scene == "tv" then
        send_ir("power on", "Yamaha receiver")
        send_ir("hdmi1", "Yamaha receiver")
        send_ir("power on", "Samsung TV")
        send_ir("hdmi1", "Samsung TV")
        end
    
    if scene == "ps3" then
        send_ir("power on", "Yamaha receiver")
        send_ir("hdmi4", "Yamaha receiver")
        send_ir("power on", "Samsung TV")
        send_ir("hdmi1", "Samsung TV")
        end
    
    if scene == "ps2" then
        send_ir("power on", "Yamaha receiver")
        send_ir("av5", "Yamaha receiver")
        send_ir("power on", "Samsung TV")
        send_ir("hdmi1", "Samsung TV")
        end
    
    if scene == "ps1" then
        send_ir("power on", "Yamaha receiver")
        send_ir("av6", "Yamaha receiver")
        send_ir("power on", "Samsung TV")
        send_ir("hdmi1", "Samsung TV")
        end
    
    if scene == "atv" then
        send_ir("power on", "Yamaha receiver")
        send_ir("hdmi3", "Yamaha receiver")
        send_ir("power on", "Samsung TV")
        send_ir("hdmi1", "Samsung TV")
        end
    end
