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

test("health modifiers preserve incomplete and invalid results", function()
	local observerArgs

	local modifier = Event()
	modifier.onCreatureChangeHealth = function(_, _, primaryDamage, primaryType)
		return primaryDamage * 2, primaryType, nil, "invalid"
	end
	modifier:register(-20)

	local observer = Event()
	observer.onCreatureChangeHealth = function(_, _, primaryDamage, primaryType, secondaryDamage, secondaryType)
		observerArgs = {primaryDamage, primaryType, secondaryDamage, secondaryType}
	end
	observer:register(-10)

	local final = Event()
	final.onCreatureChangeHealth = function(_, _, primaryDamage, primaryType, secondaryDamage, secondaryType)
		return primaryDamage + 5, primaryType, secondaryDamage + 1, secondaryType
	end
	final:register(0)

	local primaryDamage, primaryType, secondaryDamage, secondaryType =
		Event.onCreatureChangeHealth({}, nil, -10, 1, -2, 2, 0)
	assertEqual(table.concat(observerArgs, ","), "-20,1,-2,2", "health observer arguments")
	assertEqual(primaryDamage, -15, "final primary health value")
	assertEqual(primaryType, 1, "final primary health type")
	assertEqual(secondaryDamage, -1, "final secondary health value")
	assertEqual(secondaryType, 2, "final secondary health type")
end)

test("mana modifiers survive a nil final result", function()
	local modifier = Event()
	modifier.onCreatureChangeMana = function(_, _, primaryDamage, primaryType, secondaryDamage, secondaryType)
		return primaryDamage - 4, primaryType, secondaryDamage - 2, secondaryType
	end
	modifier:register(-10)

	local observer = Event()
	observer.onCreatureChangeMana = function()
		return nil
	end
	observer:register(0)

	local primaryDamage, primaryType, secondaryDamage, secondaryType =
		Event.onCreatureChangeMana({}, nil, -10, 4, -3, 8, 0)
	assertEqual(primaryDamage, -14, "final primary mana value")
	assertEqual(primaryType, 4, "final primary mana type")
	assertEqual(secondaryDamage, -5, "final secondary mana value")
	assertEqual(secondaryType, 8, "final secondary mana type")
end)

test("false terminates a modifier chain without reverting values", function()
	local reachedFinal = false

	local modifier = Event()
	modifier.onCreatureChangeHealth = function(_, _, primaryDamage, primaryType, secondaryDamage, secondaryType)
		return primaryDamage * 2, primaryType, secondaryDamage, secondaryType
	end
	modifier:register(-20)

	local stop = Event()
	stop.onCreatureChangeHealth = function()
		return false
	end
	stop:register(-10)

	local final = Event()
	final.onCreatureChangeHealth = function()
		reachedFinal = true
	end
	final:register(0)

	local primaryDamage, primaryType, secondaryDamage, secondaryType =
		Event.onCreatureChangeHealth({}, nil, -10, 1, -2, 2, 0)
	assertEqual(reachedFinal, false, "terminated health callback")
	assertEqual(primaryDamage, -20, "preserved primary health value")
	assertEqual(primaryType, 1, "preserved primary health type")
	assertEqual(secondaryDamage, -2, "preserved secondary health value")
	assertEqual(secondaryType, 2, "preserved secondary health type")
end)

test("existing numeric modifier chains preserve a nil final result", function()
	local modifier = Event()
	modifier.onPlayerGainExperience = function(_, _, experience)
		return experience * 2
	end
	modifier:register(-10)

	local observer = Event()
	observer.onPlayerGainExperience = function()
		return nil
	end
	observer:register(0)

	assertEqual(Event.onPlayerGainExperience({}, nil, 100, 100, false), 200, "experience result")
end)

print(string.format("All %d Event callback modifier tests passed.", passed))
