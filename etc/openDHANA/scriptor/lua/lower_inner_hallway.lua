-- Lower inner hallway

active_scene = ""

function motion_in_lower_inner_hallway_detected (movement)
    
    -- Turn on the night light if someone is moving in the hallway
    if movement == "True" and active_scene == "night" then
        openDHANA_publish("change_lower_inner_hallway", "20")
        end
    
    if movement == "False" and active_scene == "night" then
        openDHANA_publish("change_lower_inner_hallway", "0")
        end
    
    end

function scene_changed (scene)
    active_scene = scene
    
    if scene == "morning" then
        openDHANA_publish("change_lower_inner_hallway", "99")
        
    elseif scene == "day" then
        openDHANA_publish("change_lower_inner_hallway", "0")
        
    elseif scene == "evening" then
        openDHANA_publish("change_lower_inner_hallway", "50")
        
    elseif scene == "night" then
        openDHANA_publish("change_lower_inner_hallway", "0")
        
        end
    end