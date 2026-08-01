unpack = table.unpack
RETURNVALUE_NOERROR = 0

function isScriptsInterface()
	return true
end

function debugPrint(message)
	error(message)
end

dofile("data/scripts/lib/event_callbacks.lua")

local passed = 0

local function assertEqual(actual, expected, message)
	if actual ~= expected then
		error(string.format("%s: expected %s, got %s", message, tostring(expected), tostring(actual)))
	end
end

local function test(name, callback)
	Event:clear()
	callback()
	passed = passed + 1
	print(string.format("[PASS] %s", name))
end

test("equal priorities preserve registration order", function()
	local calls = {}
	for index = 1, 32 do
		local event = Event()
		event.onGameStartup = function()
			calls[#calls + 1] = index
			return true
		end
		event:register()
	end

	Event.onGameStartup()
	for index = 1, 32 do
		assertEqual(calls[index], index, "equal-priority callback order")
	end
end)

test("explicit priorities take precedence over registration order", function()
	local calls = {}
	for _, definition in ipairs({ { "late", 10 }, { "early", -10 }, { "middle", 0 } }) do
		local event = Event()
		event.onGameStartup = function()
			calls[#calls + 1] = definition[1]
			return true
		end
		event:register(definition[2])
	end

	Event.onGameStartup()
	assertEqual(table.concat(calls, ","), "early,middle,late", "explicit-priority callback order")
end)

test("clear resets callback order", function()
	local stale = Event()
	stale.onGameStartup = function()
		error("stale callback executed")
	end
	stale:register()

	Event:clear()
	assertEqual(Event.onGameStartup, nil, "cleared callback lookup")

	local calls = {}
	for index = 1, 2 do
		local event = Event()
		event.onGameStartup = function()
			calls[#calls + 1] = index
			return true
		end
		event:register()
	end

	Event.onGameStartup()
	assertEqual(table.concat(calls, ","), "1,2", "post-clear callback order")
end)

print(string.format("All %d Event callback order tests passed.", passed))
