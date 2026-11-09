--[[
  Zampler Import
  Reads a *.zampler.lua file exported by Zampler and writes it into the song:
  one new track per Zampler part, starting at the selected sequence slot.
  New patterns are added to the sequence when the export is longer than one
  pattern. Nothing existing is overwritten: notes go into freshly inserted
  tracks.
]]

local NOTE_OFF = 120

local function load_export(path)
  local f, err = io.open(path, "rb")
  if not f then return nil, err end
  local src = f:read("*a")
  f:close()
  local chunk, perr = (loadstring or load)(src, "=zampler")
  if not chunk then return nil, perr end
  if setfenv then setfenv(chunk, {}) end -- data only, no access to globals
  local ok, data = pcall(chunk)
  if not ok then return nil, data end
  if type(data) ~= "table" or type(data.patterns) ~= "table" or type(data.tracks) ~= "table" then
    return nil, "not a Zampler export"
  end
  return data
end

local function clamp(v, lo, hi)
  if v < lo then return lo elseif v > hi then return hi end
  return v
end

function zampler_import_data(song, data, opts)
  opts = opts or {}
  if opts.set_tempo ~= false then
    song.transport.bpm = clamp(math.floor((data.bpm or 120) + 0.5), 32, 999)
    song.transport.lpb = clamp(data.lpb or 4, 1, 256)
  end

  -- New tracks after the last sequencer track.
  local first_track = song.sequencer_track_count + 1
  for i, t in ipairs(data.tracks) do
    local track = song:insert_track_at(first_track + i - 1)
    track.name = t.name or ("Zampler " .. i)
    track.visible_note_columns = clamp(t.columns or 1, 1, 12)
  end

  local seq = song.sequencer
  local seq_pos = song.selected_sequence_index
  local max_ins = math.max(0, #song.instruments - 1)
  local count = 0

  for p, pat in ipairs(data.patterns) do
    local slot = seq_pos + p - 1
    local pattern_index
    if slot <= #seq.pattern_sequence then
      pattern_index = seq.pattern_sequence[slot]
    else
      pattern_index = seq:insert_new_pattern_at(slot)
    end
    local pattern = song:pattern(pattern_index)
    if pattern.number_of_lines < pat.lines then
      pattern.number_of_lines = clamp(pat.lines, 1, 512)
    end
    for _, n in ipairs(pat.notes) do
      -- n = { track, column, line (0-based), note [, instrument, volume] }
      local t, c, ln, note = n[1], n[2], n[3] + 1, n[4]
      if ln <= pattern.number_of_lines then
        local col = pattern:track(first_track + t - 1):line(ln):note_column(c)
        col.note_value = note
        if note ~= NOTE_OFF then
          col.instrument_value = clamp(n[5] or 0, 0, max_ins)
          col.volume_value = clamp(n[6] or 127, 0, 127)
          count = count + 1
        end
      end
    end
  end
  return count
end

local function import()
  local app = renoise.app()
  local path = app:prompt_for_filename_to_read({"*.lua"}, "Zampler: choose a .zampler.lua export")
  if path == nil or path == "" then return end
  local data, err = load_export(path)
  if not data then
    app:show_error("Zampler could not read that file:\n" .. tostring(err))
    return
  end
  local ok, result = pcall(zampler_import_data, renoise.song(), data)
  if not ok then
    app:show_error("Zampler import failed:\n" .. tostring(result))
    return
  end
  app:show_status(string.format("Zampler: imported %d notes into %d tracks, %d pattern(s)",
    result, #data.tracks, #data.patterns))
end

renoise.tool():add_menu_entry {
  name = "Main Menu:Tools:Zampler:Import Pattern File...",
  invoke = import,
}

renoise.tool():add_menu_entry {
  name = "Pattern Editor:Zampler:Import Pattern File...",
  invoke = import,
}
