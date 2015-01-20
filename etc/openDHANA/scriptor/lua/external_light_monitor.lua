-- External lights monitor

active_scene = ""

function scene_changed (scene)
    active_scene = scene
    end

function external_light_changed (lux)
    -- If it's "day" and it gets dark, then switch to scene "evening"
    if active_scene == "day" and tonumber(lux) <= 400 then
        openDHANA_publish ("change_scene", "evening")
        end
    
    -- If it's "morning" and it gets light outside, then switch to scene "day"
    if active_scene == "morning" and tonumber(lux) >= 300 then
        openDHANA_publish ("change_scene", "day")
        end
    end