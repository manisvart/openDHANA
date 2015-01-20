-- The master bedroom

function scene_changed (scene)
    if scene == "morning" then
        openDHANA_publish("change_master_bedroom", "99")
        
    elseif scene == "day" then
        openDHANA_publish("change_master_bedroom", "0")
        
    elseif scene == "evening" then
        openDHANA_publish("change_master_bedroom", "50")
        
    elseif scene == "night" then
        openDHANA_publish("change_master_bedroom", "0")
        
        end
    end