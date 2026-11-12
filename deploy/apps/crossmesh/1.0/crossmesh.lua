-- CrossMesh 1.0: Dual-Mesh Cross-Play (MeshCore + Meshtastic) for wadamesh
-- Plots MeshCore nodes in Blue (0x2D8CFF), Meshtastic nodes in Green (0x53C06B),
-- and Self in Amber (0xE8A33D). Supports Dual-Watch TDM, Meshtastic Ping +
-- Traceroute (portnum 70), NodeInfo/Position Beacon, and LongFast/DM Chat.

local app = {}
local C = wada.ui.colors

local COL_MC = 0x2D8CFF   -- Blue: MeshCore contacts
local COL_MT = 0x53C06B   -- Green: Meshtastic nodes
local COL_ME = 0xE8A33D   -- Amber: Self position

local PRESET_NAMES = { [0] = "LF", [1] = "LM", [2] = "MF", [3] = "SF" }

local W, H = 320, 240
local tab = 1             -- 1 = Map, 2 = Nodes/Ping, 3 = Chat
local dual_watch = true
local preset = 0
local freq_mhz = 869.525
local map_zoom = 12

-- State tables
local mt_nodes = {}       -- hex -> { hex, id, long_name, short_name, lat, lon, has_pos, snr, rssi, hops, last_ms, rtt_ms, route_str }
local mt_order = {}       -- array of hex keys
local mc_list = {}        -- cached wada.mesh.contacts()
local combined = {}       -- unified list for Tab 2: { kind="MT"|"MC", hex=..., name=..., sub=..., lat=..., lon=... }
local sel_idx = 1
local chat_msgs = {}      -- array of { dir="rx"|"tx", from=..., to_hex=..., text=..., pkt_id=..., status=..., rtt_ms=... }
local last_ping_str = "Press P or tap Ping to probe a Meshtastic node"
local last_widget_ms = -5000
local last_nodes_save_ms = 0
local tick_count = 0
local nodes_dirty = false
local chat_dirty = false

-- UI handles
local lbl_hdr = nil
local lbl_info = nil
local lbl_ping = nil
local lbl_rows = {}
local map_v = nil
local radar_cv = nil

local function clean_tsv(s)
  return tostring(s or ""):gsub("[\t\r\n]", " ")
end

local function self_pos()
  if wada.sys.gps then
    local g = wada.sys.gps()
    if g and g.lat and g.lon and (g.lat ~= 0 or g.lon ~= 0) then
      return g.lat, g.lon
    end
  end
  local me = wada.mesh.self and wada.mesh.self() or nil
  if me and me.lat and me.lon and (me.lat ~= 0 or me.lon ~= 0) then
    return me.lat, me.lon
  end
  return 0, 0
end

local function fmt_dist(lat, lon)
  if not lat or not lon or (lat == 0 and lon == 0) then return "" end
  local mlat, mlon = self_pos()
  if mlat == 0 and mlon == 0 then return "" end
  local d = wada.geo.distance(mlat, mlon, lat, lon)
  local brg = wada.geo.bearing(mlat, mlon, lat, lon)
  local card = wada.geo.cardinal(brg)
  if d >= 1000 then
    return string.format("%.1fkm %s", d / 1000.0, card)
  end
  return string.format("%.0fm %s", d, card)
end

local function save_nodes()
  if not wada.fs or not wada.fs.write then return false end
  local lines = {}
  for _, hex in ipairs(mt_order) do
    local n = mt_nodes[hex]
    if n then
      lines[#lines + 1] = string.format("%s\t%s\t%s\t%.6f\t%.6f\t%.1f\t%d\t%d",
        n.hex, clean_tsv(n.long_name), clean_tsv(n.short_name),
        n.lat or 0, n.lon or 0, n.snr or 0, n.rssi or 0, n.hops or 0)
    end
  end
  if wada.fs.write("nodes.tsv", table.concat(lines, "\n")) then
    nodes_dirty = false
    last_nodes_save_ms = wada.sys.millis()
    return true
  end
  return false
end

local function load_nodes()
  if not wada.fs or not wada.fs.read then return end
  local raw = wada.fs.read("nodes.tsv")
  if not raw or raw == "" then return end
  for line in raw:gmatch("[^\r\n]+") do
    local hex, ln, sn, lat, lon, snr, rssi, hops =
      line:match("^([^\t]+)\t([^\t]*)\t([^\t]*)\t([^\t]+)\t([^\t]+)\t([^\t]+)\t([^\t]+)\t([^\t]+)")
    if hex and #hex == 8 then
      local la, lo = tonumber(lat) or 0, tonumber(lon) or 0
      mt_nodes[hex] = {
        hex = hex,
        long_name = ln ~= "" and ln or ("!" .. hex),
        short_name = sn ~= "" and sn or hex:sub(5, 8):upper(),
        lat = la, lon = lo,
        has_pos = (la ~= 0 or lo ~= 0),
        snr = tonumber(snr) or 0,
        rssi = tonumber(rssi) or -100,
        hops = tonumber(hops) or 0,
        last_ms = 0,
      }
      mt_order[#mt_order + 1] = hex
    end
  end
end

local function save_chat()
  if not wada.fs or not wada.fs.write then return false end
  local lines = {}
  local start = #chat_msgs > 30 and (#chat_msgs - 29) or 1
  for i = start, #chat_msgs do
    local m = chat_msgs[i]
    lines[#lines + 1] = string.format("%s\t%s\t%s\t%s",
      clean_tsv(m.dir), clean_tsv(m.from), clean_tsv(m.status), clean_tsv(m.text))
  end
  if wada.fs.write("chat.tsv", table.concat(lines, "\n")) then
    chat_dirty = false
    return true
  end
  return false
end

local function flush_dirty_fs(force_nodes)
  local now = wada.sys.millis()
  if nodes_dirty and (force_nodes or last_nodes_save_ms == 0 or (now - last_nodes_save_ms >= 10000)) then
    save_nodes()
  end
  if chat_dirty then
    save_chat()
  end
end

local function load_chat()
  if not wada.fs or not wada.fs.read then return end
  local raw = wada.fs.read("chat.tsv")
  if not raw or raw == "" then return end
  for line in raw:gmatch("[^\r\n]+") do
    local dir, from, st, txt = line:match("^([^\t]+)\t([^\t]*)\t([^\t]*)\t(.*)$")
    if dir and txt then
      chat_msgs[#chat_msgs + 1] = { dir = dir, from = from, status = st, text = txt }
    end
  end
end

local function ensure_mt_node(hex, id)
  if not hex or hex == "" then hex = "00000000" end
  local n = mt_nodes[hex]
  if not n then
    n = {
      hex = hex,
      id = id or 0,
      long_name = "!" .. hex,
      short_name = hex:sub(5, 8):upper(),
      lat = 0, lon = 0, has_pos = false,
      snr = 0, rssi = -100, hops = 0,
      last_ms = wada.sys.millis(),
    }
    mt_nodes[hex] = n
    mt_order[#mt_order + 1] = hex
    while #mt_order > 128 do
      local old = table.remove(mt_order, 1)
      if old then mt_nodes[old] = nil end
    end
  end
  return n
end

local function rebuild_combined()
  mc_list = (wada.mesh and wada.mesh.contacts and wada.mesh.contacts(0, 50)) or {}
  combined = {}
  for _, hex in ipairs(mt_order) do
    local n = mt_nodes[hex]
    if n then
      local dist = fmt_dist(n.lat, n.lon)
      local rtt = n.rtt_ms and string.format(" RTT:%dms", n.rtt_ms) or ""
      local rt = n.route_str and (" via " .. n.route_str) or ""
      local sub = string.format("SNR:%.1fdB %ddBm %dhop%s%s%s",
        n.snr or 0, n.rssi or -100, n.hops or 0,
        dist ~= "" and (" " .. dist) or "", rtt, rt)
      combined[#combined + 1] = {
        kind = "MT",
        hex = n.hex,
        name = string.format("[MT] %s (%s)", n.long_name, n.short_name),
        sub = sub,
        lat = n.lat, lon = n.lon, has_pos = n.has_pos,
      }
    end
  end
  for _, c in ipairs(mc_list) do
    local has_pos = c.lat and c.lon and (c.lat ~= 0 or c.lon ~= 0)
    local dist = has_pos and fmt_dist(c.lat, c.lon) or ""
    combined[#combined + 1] = {
      kind = "MC",
      name = string.format("[MC] %s", c.name or "?"),
      mc_name = c.name,
      sub = string.format("%s (%ds ago)", dist ~= "" and dist or "no fix", c.ago_s or 0),
      lat = c.lat or 0, lon = c.lon or 0, has_pos = has_pos,
    }
  end
  if sel_idx < 1 then sel_idx = 1 end
  if #combined > 0 and sel_idx > #combined then sel_idx = #combined end
end

local function update_widget()
  if not wada.widget or not wada.widget.set then return end
  local now = wada.sys.millis()
  if now - last_widget_ms < 1000 then return end
  last_widget_ms = now
  wada.widget.set({
    title = "CrossMesh",
    value = string.format("MT:%d MC:%d", #mt_order, #mc_list),
    line = last_ping_str,
  })
end

local function header_text()
  local st = wada.meshtastic and wada.meshtastic.status and wada.meshtastic.status() or {}
  local mode = (st.dual_watch ~= false) and "Dual" or "MT"
  local pname = PRESET_NAMES[st.preset or preset] or "LF"
  return string.format("MC:869.618<->MT:%.3f %s [%s] MT:%d MC:%d",
    st.freq or freq_mhz, pname, mode, #mt_order, #mc_list)
end

local function draw_map_view()
  local mlat, mlon = self_pos()
  if mlat == 0 and mlon == 0 then
    mlat, mlon = 48.1351, 11.5820
  end
  local sel = combined[sel_idx]

  if map_v then
    map_v:clear()
    map_v:center(mlat, mlon, map_zoom)
    map_v:marker(mlat, mlon, COL_ME, 10)
    for _, c in ipairs(mc_list) do
      if c.lat and c.lon and (c.lat ~= 0 or c.lon ~= 0) then
        map_v:marker(c.lat, c.lon, COL_MC, 9)
      end
    end
    for _, hex in ipairs(mt_order) do
      local n = mt_nodes[hex]
      if n and n.has_pos then
        map_v:marker(n.lat, n.lon, COL_MT, 9)
      end
    end
    if sel and sel.has_pos then
      map_v:line(mlat, mlon, sel.lat, sel.lon, sel.kind == "MT" and COL_MT or COL_MC, 2)
    end
    map_v:redraw()
  end

  if radar_cv then
    radar_cv:fill(C.bg)
    local cx, cy = radar_cv.w // 2, radar_cv.h // 2
    local rmax = math.min(cx, cy) - 8
    radar_cv:circle(cx, cy, rmax, C.sub, false, 1)
    radar_cv:circle(cx, cy, math.max(2, rmax // 2), C.sub, false, 1)
    radar_cv:circle(cx, cy, 4, COL_ME, true)

    local function plot_pt(lat, lon, col)
      local d = wada.geo.distance(mlat, mlon, lat, lon)
      local brg = math.rad(wada.geo.bearing(mlat, mlon, lat, lon))
      local rr = math.min(rmax - 4, math.max(6, math.floor((d / 5000.0) * rmax)))
      local px = cx + math.floor(rr * math.sin(brg))
      local py = cy - math.floor(rr * math.cos(brg))
      radar_cv:circle(px, py, 4, col, true)
      return px, py
    end

    for _, c in ipairs(mc_list) do
      if c.lat and c.lon and (c.lat ~= 0 or c.lon ~= 0) then
        plot_pt(c.lat, c.lon, COL_MC)
      end
    end
    for _, hex in ipairs(mt_order) do
      local n = mt_nodes[hex]
      if n and n.has_pos then
        plot_pt(n.lat, n.lon, COL_MT)
      end
    end
    if sel and sel.has_pos then
      local tx, ty = plot_pt(sel.lat, sel.lon, sel.kind == "MT" and COL_MT or COL_MC)
      radar_cv:line(cx, cy, tx, ty, sel.kind == "MT" and COL_MT or COL_MC, 1)
    end
  end

  if lbl_info then
    if sel then
      lbl_info:set(string.format("Sel: %s | %s", sel.name, sel.sub))
    else
      lbl_info:set("Blue=MeshCore  Green=Meshtastic  Amber=Self")
    end
  end
end

local function refresh_ui()
  rebuild_combined()
  if lbl_hdr then lbl_hdr:set(header_text()) end

  if tab == 1 then
    draw_map_view()
  elseif tab == 2 then
    if lbl_ping then lbl_ping:set(last_ping_str) end
    for i = 1, #lbl_rows do
      local item = combined[i]
      if item then
        local prefix = (i == sel_idx) and "> " or "  "
        lbl_rows[i]:set(string.format("%s%s  %s", prefix, item.name, item.sub))
        lbl_rows[i]:color(item.kind == "MT" and COL_MT or COL_MC)
      else
        lbl_rows[i]:set(i == 1 and "  No nodes heard yet (tap Beacon)" or "")
        lbl_rows[i]:color(C.sub)
      end
    end
  elseif tab == 3 then
    local total = #chat_msgs
    local rows_n = #lbl_rows
    local start = total > rows_n and (total - rows_n + 1) or 1
    for i = 1, rows_n do
      local m = chat_msgs[start + i - 1]
      if m then
        local st = (m.status and m.status ~= "") and (" [" .. m.status .. "]") or ""
        lbl_rows[i]:set(string.format("%s: %s%s", m.from or "?", m.text or "", st))
        lbl_rows[i]:color(m.dir == "tx" and COL_ME or COL_MT)
      else
        lbl_rows[i]:set(i == 1 and "No Meshtastic messages yet (press C to compose)" or "")
        lbl_rows[i]:color(C.sub)
      end
    end
  end
end

local function do_ping()
  rebuild_combined()
  local target = combined[sel_idx]
  if not target or target.kind ~= "MT" then
    for i, item in ipairs(combined) do
      if item.kind == "MT" then
        sel_idx = i
        target = item
        break
      end
    end
  end
  if target and target.kind == "MT" then
    local pkt_id, err = wada.meshtastic.ping(target.hex, true)
    if pkt_id then
      last_ping_str = string.format("Pinging !%s (id %d)...", target.hex, pkt_id)
      wada.sys.toast("MT Ping !" .. target.hex)
    else
      last_ping_str = "Ping blocked: " .. tostring(err or "duty limit")
    end
  elseif target and target.kind == "MC" and target.mc_name then
    wada.mesh.send_dm(target.mc_name, "PING")
    last_ping_str = "MC Ping sent to " .. target.mc_name
  else
    last_ping_str = "No Meshtastic node to ping yet"
  end
  refresh_ui()
end

local function do_beacon()
  local pkt_id, err = wada.meshtastic.beacon()
  if pkt_id then
    last_ping_str = string.format("Beacon TX ok (id %d)", pkt_id)
    wada.sys.toast("MT Beacon sent")
  else
    last_ping_str = "Beacon blocked: " .. tostring(err or "duty limit")
  end
  refresh_ui()
end

local function do_toggle_mode()
  dual_watch = not dual_watch
  wada.store.set("dual_watch", dual_watch and 1 or 0)
  wada.meshtastic.start({ preset = preset, freq = freq_mhz, dual_watch = dual_watch })
  refresh_ui()
end

local function do_compose(dm_selected)
  if not wada.ui.input then return end
  rebuild_combined()
  local dst_hex = "ffffffff"
  local title = "Meshtastic #LongFast"
  if dm_selected then
    local sel = combined[sel_idx]
    if not sel or sel.kind ~= "MT" then
      for _, item in ipairs(combined) do
        if item.kind == "MT" then sel = item; break end
      end
    end
    if sel and sel.kind == "MT" and sel.hex then
      dst_hex = sel.hex
      title = "MT DM !" .. dst_hex
    end
  end
  wada.ui.input(title, "", function(text)
    if not text or text == "" then return end
    local pkt_id, err = wada.meshtastic.send_text(text, dst_hex, true)
    local st = pkt_id and "wait" or ("err:" .. tostring(err))
    chat_msgs[#chat_msgs + 1] = {
      dir = "tx",
      from = dst_hex == "ffffffff" and "Me" or ("Me->!" .. dst_hex:sub(5, 8)),
      to_hex = dst_hex,
      text = text,
      pkt_id = pkt_id or 0,
      status = st,
    }
    chat_dirty = true
    save_chat()
    refresh_ui()
  end, 200)
end

local switch_tab

switch_tab = function(new_tab)
  if map_v then
    map_v:close()
    map_v = nil
  end
  radar_cv = nil
  lbl_info = nil
  lbl_ping = nil
  lbl_rows = {}
  wada.ui.clear()

  tab = new_tab
  lbl_hdr = wada.ui.label(header_text(), 4, 2, 12, C.accent)

  local bw = math.floor((W - 16) / 3)
  wada.ui.button(tab == 1 and "[1:Map]" or "1:Map", 4, 18, bw, 20, function() switch_tab(1) end)
  wada.ui.button(tab == 2 and "[2:Nodes]" or "2:Nodes", 8 + bw, 18, bw, 20, function() switch_tab(2) end)
  wada.ui.button(tab == 3 and "[3:Chat]" or "3:Chat", 12 + bw * 2, 18, bw, 20, function() switch_tab(3) end)

  local body_y = 42
  local body_h = H - body_y - 26

  if tab == 1 then
    if wada.map and wada.map.view then
      local mw = math.floor(W * 0.62)
      map_v = wada.map.view(4, body_y, mw, body_h)
      radar_cv = wada.ui.canvas(W - mw - 12, body_h)
      radar_cv:pos(mw + 8, body_y)
    else
      radar_cv = wada.ui.canvas(W - 8, body_h)
      radar_cv:pos(4, body_y)
    end
    lbl_info = wada.ui.label("", 4, H - 22, 12, C.text)
  elseif tab == 2 then
    lbl_ping = wada.ui.label(last_ping_str, 4, body_y, 12, COL_ME)
    local row_y = body_y + 18
    local max_rows = math.max(3, math.floor((body_h - 22) / 18))
    for i = 1, max_rows do
      lbl_rows[i] = wada.ui.label("", 4, row_y + (i - 1) * 18, 12, C.text)
    end
    local abw = math.floor((W - 16) / 3)
    wada.ui.button("Ping (P)", 4, H - 24, abw, 22, do_ping)
    wada.ui.button("Beacon (B)", 8 + abw, H - 24, abw, 22, do_beacon)
    wada.ui.button("Mode (M)", 12 + abw * 2, H - 24, abw, 22, do_toggle_mode)
  elseif tab == 3 then
    local max_rows = math.max(3, math.floor((body_h - 4) / 18))
    for i = 1, max_rows do
      lbl_rows[i] = wada.ui.label("", 4, body_y + (i - 1) * 18, 12, C.text)
    end
    local cbw = math.floor((W - 16) / 3)
    wada.ui.button("Compose (C)", 4, H - 24, cbw, 22, function() do_compose(false) end)
    wada.ui.button("DM (D)", 8 + cbw, H - 24, cbw, 22, function() do_compose(true) end)
    wada.ui.button("Beacon (B)", 12 + cbw * 2, H - 24, cbw, 22, do_beacon)
  end

  refresh_ui()
end

local function process_mt_events()
  if not wada.meshtastic or not wada.meshtastic.poll then return false end
  local evs = wada.meshtastic.poll()
  if not evs or #evs == 0 then return false end

  for _, ev in ipairs(evs) do
    local hex = ev.from_hex or "00000000"
    local n = ensure_mt_node(hex, ev.from_id)
    n.snr = ev.snr or n.snr
    n.rssi = ev.rssi or n.rssi
    n.hops = ev.hops or n.hops
    n.last_ms = ev.rx_ms or wada.sys.millis()
    nodes_dirty = true

    if ev.kind == "nodeinfo" then
      if ev.long_name and ev.long_name ~= "" then n.long_name = ev.long_name end
      if ev.short_name and ev.short_name ~= "" then n.short_name = ev.short_name end
    elseif ev.kind == "position" then
      if ev.has_pos and ev.lat and ev.lon then
        n.lat = ev.lat
        n.lon = ev.lon
        n.has_pos = true
      end
    elseif ev.kind == "text" then
      local sender = (n.short_name and n.short_name ~= "") and n.short_name or ("!" .. hex:sub(5, 8))
      chat_msgs[#chat_msgs + 1] = {
        dir = "rx",
        from = sender,
        to_hex = ev.to_hex or "ffffffff",
        text = ev.text or "",
        pkt_id = ev.packet_id or 0,
        status = string.format("%.1fdB", ev.snr or 0),
      }
      chat_dirty = true
    elseif ev.kind == "traceroute" then
      n.rtt_ms = ev.rtt_ms or n.rtt_ms
      local hops_str = (ev.route and #ev.route > 0) and table.concat(ev.route, "->") or "direct"
      n.route_str = hops_str
      last_ping_str = string.format("RTT %dms | %d hop(s) via %s (SNR %.1fdB)",
        ev.rtt_ms or 0, ev.hops or 0, hops_str, ev.snr or 0)
    elseif ev.kind == "routing" or ev.kind == "ack" then
      if ev.rtt_ms and ev.rtt_ms > 0 then
        n.rtt_ms = ev.rtt_ms
        last_ping_str = string.format("ACK from !%s: RTT %dms (err %d)",
          hex, ev.rtt_ms, ev.error_reason or 0)
      end
      if ev.request_id and ev.request_id ~= 0 then
        for i = #chat_msgs, 1, -1 do
          local m = chat_msgs[i]
          if m.pkt_id == ev.request_id then
            if (ev.error_reason or 0) == 0 then
              m.status = string.format("ACK %dms", ev.rtt_ms or 0)
            else
              m.status = string.format("ERR %d", ev.error_reason or 0)
            end
            chat_dirty = true
            break
          end
        end
      end
    end
  end

  flush_dirty_fs()
  update_widget()
  return true
end

function app.on_open(w, h)
  W, H = w or 320, h or 240
  preset = wada.store.get("preset", 0)
  dual_watch = (wada.store.get("dual_watch", 1) ~= 0)
  load_nodes()
  load_chat()

  if wada.meshtastic and wada.meshtastic.start then
    wada.meshtastic.start({ preset = preset, freq = freq_mhz, dual_watch = dual_watch })
  end

  wada.timer.every(250)
  switch_tab(1)
  update_widget()
end

function app.on_tick(dt)
  tick_count = tick_count + 1
  local changed = process_mt_events()
  if nodes_dirty or chat_dirty then
    flush_dirty_fs(false)
  end
  if changed or (tab == 1 and (tick_count % 8 == 0)) then
    refresh_ui()
  end
end

function app.on_input(ev)
  if not ev then return end
  if ev.type == "key" then
    local k = ev.key
    if k == "1" then switch_tab(1)
    elseif k == "2" then switch_tab(2)
    elseif k == "3" then switch_tab(3)
    elseif k == "left" then switch_tab(tab > 1 and (tab - 1) or 3)
    elseif k == "right" then switch_tab(tab < 3 and (tab + 1) or 1)
    elseif k == "up" then
      if #combined > 0 then
        sel_idx = sel_idx > 1 and (sel_idx - 1) or #combined
        refresh_ui()
      end
    elseif k == "down" then
      if #combined > 0 then
        sel_idx = sel_idx < #combined and (sel_idx + 1) or 1
        refresh_ui()
      end
    elseif k == "p" or k == "P" then do_ping()
    elseif k == "b" or k == "B" then do_beacon()
    elseif k == "m" or k == "M" then do_toggle_mode()
    elseif k == "c" or k == "C" then do_compose(false)
    elseif k == "d" or k == "D" then do_compose(true)
    elseif k == "+" or k == "=" then
      map_zoom = math.min(16, map_zoom + 1)
      if tab == 1 then draw_map_view() end
    elseif k == "-" then
      map_zoom = math.max(5, map_zoom - 1)
      if tab == 1 then draw_map_view() end
    end
  elseif ev.type == "swipe" then
    if ev.dir == "left" then switch_tab(tab < 3 and (tab + 1) or 1)
    elseif ev.dir == "right" then switch_tab(tab > 1 and (tab - 1) or 3)
    end
  end
end

function app.on_close()
  if map_v then
    map_v:close()
    map_v = nil
  end
  flush_dirty_fs(true)
  if wada.meshtastic and wada.meshtastic.stop then
    wada.meshtastic.stop()
  end
end

return app
