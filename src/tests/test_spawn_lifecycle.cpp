#define BOOST_TEST_MODULE spawn_lifecycle

#include "../otpch.h"

#include "../events/events.h"
#include "../events/monster.h"
#include "../game.h"
#include "../items.h"
#include "../lua/env.h"
#include "../lua/script.h"
#include "../monster.h"
#include "../monsters.h"
#include "../movement.h"
#include "../spawn.h"

#include <boost/test/unit_test.hpp>
#include <filesystem>

extern Game g_game;
extern Monsters g_monsters;
extern MoveEvents* g_moveEvents;

struct SpawnLifecycleFixture
{
	MoveEvents moveEvents;

	SpawnLifecycleFixture()
	{
		g_moveEvents = &moveEvents;

		// Locate project root containing data/scripts/events/monster.lua
		if (!std::filesystem::exists("data/scripts/events/monster.lua")) {
			if (std::filesystem::exists("../../data/scripts/events/monster.lua")) {
				std::filesystem::current_path("../../");
			} else if (std::filesystem::exists("../data/scripts/events/monster.lua")) {
				std::filesystem::current_path("../");
			}
		}

		auto& scriptInterface = tfs::events::getScriptInterface();
		if (!scriptInterface.getLuaState()) {
			scriptInterface.initState();
		}

		lua_State* L = scriptInterface.getLuaState();
		luaL_dostring(L, "Event = Event or {}");

		tfs::events::monster::load();

		// Register dummy monster type in g_monsters
		auto& mType = g_monsters.monsters["testspawnmonster"];
		mType.name = "TestSpawnMonster";
		mType.nameDescription = "a test spawn monster";
		mType.info.health = 100;
		mType.info.healthMax = 100;
		mType.info.baseSpeed = 200;

		// Setup item 100 as walkable ground
		Item::items.getItemType(100).id = 100;
		Item::items.getItemType(100).group = ITEM_GROUP_GROUND;
		Item::items.getItemType(100).speed = 100;

		// Position (100, 100, 7) has a walkable ground tile
		auto walkableTile = std::make_shared<DynamicTile>(100, 100, 7);
		auto groundItem = Item::CreateItem(100);
		walkableTile->setGround(groundItem);
		g_game.map.setTile(Position(100, 100, 7), walkableTile);

		// Position (200, 200, 7) has a tile WITHOUT ground (requires force=true)
		auto nonWalkableTile = std::make_shared<DynamicTile>(200, 200, 7);
		g_game.map.setTile(Position(200, 200, 7), nonWalkableTile);

		// Position (300, 300, 7) has NO tile at all
	}

	~SpawnLifecycleFixture()
	{
		g_moveEvents = nullptr;

		lua_State* L = tfs::events::getScriptInterface().getLuaState();
		if (L) {
			luaL_dostring(L, "Event = Event or {}; Event.onMonsterSpawn = nil; g_spawnLog = nil; g_result = nil;");
		}
	}
};

BOOST_FIXTURE_TEST_SUITE(spawn_lifecycle, SpawnLifecycleFixture)

// A. successful placement:
// - returns Monster
// - monster is in world
// - callback sees valid position/tile, startup=false, artificial=true
BOOST_AUTO_TEST_CASE(test_successful_placement_runs_onspawn_and_returns_monster)
{
	lua_State* L = tfs::events::getScriptInterface().getLuaState();
	BOOST_REQUIRE(L != nullptr);

	const char* script = R"(
		Event = Event or {}
		g_spawnLog = {}
		Event.onMonsterSpawn = function(self, position, startup, artificial)
			table.insert(g_spawnLog, {
				hasTile = (self:getTile() ~= nil),
				pos = {x = self:getPosition().x, y = self:getPosition().y, z = self:getPosition().z},
				argPos = {x = position.x, y = position.y, z = position.z},
				id = self:getId(),
				startup = startup,
				artificial = artificial,
			})
		end
		g_result = Game.createMonster("testspawnmonster", Position(100, 100, 7), false, false)
	)";

	BOOST_REQUIRE_EQUAL(luaL_dostring(L, script), 0);

	lua_getglobal(L, "g_result");
	auto monster = tfs::lua::getSharedPtr<Monster>(L, -1);
	lua_pop(L, 1);
	BOOST_REQUIRE(monster != nullptr);
	BOOST_CHECK_EQUAL(monster->getName(), "TestSpawnMonster");
	BOOST_CHECK(monster->getTile() != nullptr);
	BOOST_CHECK(g_game.getMonsterByID(monster->getID()) == monster);

	// Check callback log
	lua_getglobal(L, "g_spawnLog");
	lua_rawgeti(L, -1, 1); // g_spawnLog[1]
	BOOST_REQUIRE(!lua_isnil(L, -1));

	lua_getfield(L, -1, "hasTile");
	BOOST_CHECK_EQUAL(lua_toboolean(L, -1), 1);
	lua_pop(L, 1);

	lua_getfield(L, -1, "startup");
	BOOST_CHECK_EQUAL(lua_toboolean(L, -1), 0);
	lua_pop(L, 1);

	lua_getfield(L, -1, "artificial");
	BOOST_CHECK_EQUAL(lua_toboolean(L, -1), 1);
	lua_pop(L, 1);

	lua_pop(L, 2); // pop entry and table

	// Cleanup
	g_game.removeCreature(monster);
}

// B. nil callback result:
// - creation succeeds
BOOST_AUTO_TEST_CASE(test_nil_callback_result_does_not_rollback_placement)
{
	lua_State* L = tfs::events::getScriptInterface().getLuaState();
	BOOST_REQUIRE(L != nullptr);

	const char* script = R"(
		Event = Event or {}
		Event.onMonsterSpawn = function(self, position, startup, artificial)
			-- return nil / no return
		end
		g_result = Game.createMonster("testspawnmonster", Position(100, 100, 7), false, false)
	)";

	BOOST_REQUIRE_EQUAL(luaL_dostring(L, script), 0);

	lua_getglobal(L, "g_result");
	auto monster = tfs::lua::getSharedPtr<Monster>(L, -1);
	lua_pop(L, 1);
	BOOST_REQUIRE(monster != nullptr);
	BOOST_CHECK(monster->getTile() != nullptr);
	BOOST_CHECK(g_game.getMonsterByID(monster->getID()) == monster);

	g_game.removeCreature(monster);
}

// C. explicit false callback result:
// - creation still succeeds (onSpawn is not a veto hook)
BOOST_AUTO_TEST_CASE(test_false_callback_result_does_not_rollback_placement)
{
	lua_State* L = tfs::events::getScriptInterface().getLuaState();
	BOOST_REQUIRE(L != nullptr);

	const char* script = R"(
		Event = Event or {}
		Event.onMonsterSpawn = function(self, position, startup, artificial)
			return false
		end
		g_result = Game.createMonster("testspawnmonster", Position(100, 100, 7), false, false)
	)";

	BOOST_REQUIRE_EQUAL(luaL_dostring(L, script), 0);

	lua_getglobal(L, "g_result");
	auto monster = tfs::lua::getSharedPtr<Monster>(L, -1);
	lua_pop(L, 1);
	BOOST_REQUIRE(monster != nullptr);
	BOOST_CHECK(monster->getTile() != nullptr);
	BOOST_CHECK(g_game.getMonsterByID(monster->getID()) == monster);

	g_game.removeCreature(monster);
}

// D. callback Lua error:
// - placement remains successful
// - error is caught by protected call, no crash, monster is returned
BOOST_AUTO_TEST_CASE(test_callback_lua_error_does_not_rollback_or_crash)
{
	lua_State* L = tfs::events::getScriptInterface().getLuaState();
	BOOST_REQUIRE(L != nullptr);

	const char* script = R"(
		Event = Event or {}
		Event.onMonsterSpawn = function(self, position, startup, artificial)
			error("simulated failure in post-spawn callback")
		end
		g_result = Game.createMonster("testspawnmonster", Position(100, 100, 7), false, false)
	)";

	BOOST_REQUIRE_EQUAL(luaL_dostring(L, script), 0);

	lua_getglobal(L, "g_result");
	auto monster = tfs::lua::getSharedPtr<Monster>(L, -1);
	lua_pop(L, 1);
	BOOST_REQUIRE(monster != nullptr);
	BOOST_CHECK(monster->getTile() != nullptr);
	BOOST_CHECK(g_game.getMonsterByID(monster->getID()) == monster);

	g_game.removeCreature(monster);
}

// E. force=true only affects placement, does not alter onSpawn behavior
BOOST_AUTO_TEST_CASE(test_force_flag_only_affects_placement)
{
	lua_State* L = tfs::events::getScriptInterface().getLuaState();
	BOOST_REQUIRE(L != nullptr);

	const char* script = R"(
		Event = Event or {}
		g_callCount = 0
		Event.onMonsterSpawn = function(self, position, startup, artificial)
			g_callCount = g_callCount + 1
		end

		-- Position(200, 200, 7) has no ground: force=false MUST fail placement
		g_unforcedResult = Game.createMonster("testspawnmonster", Position(200, 200, 7), false, false)

		-- force=true allows placement on the tile
		g_forcedResult = Game.createMonster("testspawnmonster", Position(200, 200, 7), false, true)
	)";

	BOOST_REQUIRE_EQUAL(luaL_dostring(L, script), 0);

	// unforced placement failed
	lua_getglobal(L, "g_unforcedResult");
	BOOST_CHECK(lua_isnil(L, -1));
	lua_pop(L, 1);

	// forced placement succeeded
	lua_getglobal(L, "g_forcedResult");
	auto monster = tfs::lua::getSharedPtr<Monster>(L, -1);
	lua_pop(L, 1);
	BOOST_REQUIRE(monster != nullptr);
	BOOST_CHECK(monster->getTile() != nullptr);

	// onSpawn was called exactly once (for the forced successful placement)
	lua_getglobal(L, "g_callCount");
	BOOST_CHECK_EQUAL(lua_tointeger(L, -1), 1);
	lua_pop(L, 1);

	g_game.removeCreature(monster);
}

// F. failed placement returns nil and onSpawn is NOT called
BOOST_AUTO_TEST_CASE(test_failed_placement_returns_nil_and_skips_onspawn)
{
	lua_State* L = tfs::events::getScriptInterface().getLuaState();
	BOOST_REQUIRE(L != nullptr);

	const char* script = R"(
		Event = Event or {}
		g_callCount = 0
		Event.onMonsterSpawn = function(self, position, startup, artificial)
			g_callCount = g_callCount + 1
		end

		-- Position(300, 300, 7) has NO tile on the map: placement must fail even with force=true
		g_result = Game.createMonster("testspawnmonster", Position(300, 300, 7), false, true)
	)";

	BOOST_REQUIRE_EQUAL(luaL_dostring(L, script), 0);

	lua_getglobal(L, "g_result");
	BOOST_CHECK(lua_isnil(L, -1));
	lua_pop(L, 1);

	lua_getglobal(L, "g_callCount");
	BOOST_CHECK_EQUAL(lua_tointeger(L, -1), 0);
	lua_pop(L, 1);
}

// G. normal spawn path:
// - callback occurs after placement
// - callback can query real position/tile/world state
// - startup=true, artificial=false
BOOST_AUTO_TEST_CASE(test_normal_spawn_path_runs_onspawn_after_placement)
{
	lua_State* L = tfs::events::getScriptInterface().getLuaState();
	BOOST_REQUIRE(L != nullptr);

	const char* script = R"(
		Event = Event or {}
		g_normalSpawnLog = {}
		Event.onMonsterSpawn = function(self, position, startup, artificial)
			table.insert(g_normalSpawnLog, {
				hasTile = (self:getTile() ~= nil),
				pos = {x = self:getPosition().x, y = self:getPosition().y, z = self:getPosition().z},
				argPos = {x = position.x, y = position.y, z = position.z},
				id = self:getId(),
				startup = startup,
				artificial = artificial,
			})
		end
	)";
	BOOST_REQUIRE_EQUAL(luaL_dostring(L, script), 0);

	Position spawnPos(100, 100, 7);
	Spawn spawn(spawnPos);
	BOOST_REQUIRE(spawn.addMonster("testspawnmonster", spawnPos, DIRECTION_SOUTH, 60000ms));

	spawn.startup();

	lua_getglobal(L, "g_normalSpawnLog");
	lua_rawgeti(L, -1, 1);
	BOOST_REQUIRE(!lua_isnil(L, -1));

	lua_getfield(L, -1, "hasTile");
	BOOST_CHECK_EQUAL(lua_toboolean(L, -1), 1);
	lua_pop(L, 1);

	lua_getfield(L, -1, "startup");
	BOOST_CHECK_EQUAL(lua_toboolean(L, -1), 1);
	lua_pop(L, 1);

	lua_getfield(L, -1, "artificial");
	BOOST_CHECK_EQUAL(lua_toboolean(L, -1), 0);
	lua_pop(L, 1);

	lua_getfield(L, -1, "id");
	uint32_t monsterId = static_cast<uint32_t>(lua_tointeger(L, -1));
	BOOST_CHECK(monsterId > 0);
	lua_pop(L, 1);

	lua_pop(L, 2);

	auto monster = g_game.getMonsterByID(monsterId);
	BOOST_REQUIRE(monster != nullptr);
	BOOST_CHECK_EQUAL(monster->getMasterPos(), spawnPos);
	BOOST_CHECK_EQUAL(monster->getDirection(), DIRECTION_SOUTH);
	BOOST_CHECK(monster->isInSpawnRange(spawnPos));

	g_game.removeCreature(monster);
}

BOOST_AUTO_TEST_SUITE_END()
