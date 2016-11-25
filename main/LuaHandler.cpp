#include "stdafx.h"
#include "Helper.h"
#include "Logger.h"
#include "RFXtrx.h"
#include "../main/LuaHandler.h"

extern "C" {
#ifdef WITH_EXTERNAL_LUA
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
#else
#include "../lua/src/lua.h"
#include "../lua/src/lualib.h"
#include "../lua/src/lauxlib.h"
#endif
}

#include "../tinyxpath/xpath_processor.h"
#include "../json/json.h"
#include "SQLHelper.h"
#include "mainworker.h"
#include "../hardware/hardwaretypes.h"

extern std::string szUserDataFolder;

int CLuaHandler::l_domoticz_applyXPath(lua_State* lua_state)
{
	int nargs = lua_gettop(lua_state);
	if (nargs >= 2)
	{
		if (lua_isstring(lua_state, 1) && lua_isstring(lua_state, 2))
		{
			std::string buffer = lua_tostring(lua_state, 1);
			std::string xpath = lua_tostring(lua_state, 2);

			TiXmlDocument doc;
			doc.Parse(buffer.c_str(), 0, TIXML_ENCODING_UTF8);

			TiXmlElement* root = doc.RootElement();
			if (!root)
			{
				_log.Log(LOG_ERROR, "CLuaHandler (applyXPath from LUA) : Invalid data received!");
				return 0;
			}
			TinyXPath::xpath_processor processor(root, xpath.c_str());
			TiXmlString xresult = processor.S_compute_xpath();
			lua_pushstring(lua_state, xresult.c_str());
			return 1;
		}
		else
		{
			_log.Log(LOG_ERROR, "CLuaHandler (applyXPath from LUA) : Incorrect parameters type");
		}
	}
	else
	{
		_log.Log(LOG_ERROR, "CLuaHandler (applyXPath from LUA) : Not enough parameters");
	}
	return 0;
}

int CLuaHandler::l_domoticz_applyJsonPath(lua_State* lua_state)
{
	int nargs = lua_gettop(lua_state);
	if (nargs >= 2)
	{
		if (lua_isstring(lua_state, 1) && lua_isstring(lua_state, 2))
		{
			std::string buffer = lua_tostring(lua_state, 1);
			std::string jsonpath = lua_tostring(lua_state, 2);

			Json::Value root;
			Json::Reader jReader;
			if (!jReader.parse(buffer, root))
			{
				_log.Log(LOG_ERROR, "CLuaHandler (applyJsonPath from LUA) : Invalid Json data received");
				return 0;
			}

			// Grab optional arguments
			Json::PathArgument arg1;
			Json::PathArgument arg2;
			Json::PathArgument arg3;
			Json::PathArgument arg4;
			Json::PathArgument arg5;
			if (nargs >= 3)
			{
				if (lua_isstring(lua_state, 3))
				{
					arg1 = Json::PathArgument(lua_tostring(lua_state, 3));
				}
				else
				{
					_log.Log(LOG_ERROR, "CLuaHandler (applyJsonPath from LUA) : Invalid extra argument #1 for domoticz_applyJsonPath");
					return 0;
				}
				if (nargs >= 4)
				{
					if (lua_isstring(lua_state, 4))
					{
						arg2 = Json::PathArgument(lua_tostring(lua_state, 4));
					}
					else
					{
						_log.Log(LOG_ERROR, "CLuaHandler (applyJsonPath from LUA) : Invalid extra argument #2 for domoticz_applyJsonPath");
						return 0;
					}
					if (nargs >= 5)
					{
						if (lua_isstring(lua_state, 5))
						{
							arg3 = Json::PathArgument(lua_tostring(lua_state, 5));
						}
						else
						{
							_log.Log(LOG_ERROR, "CLuaHandler (applyJsonPath from LUA) : Invalid extra argument #3 for domoticz_applyJsonPath");
							return 0;
						}
						if (nargs >= 6)
						{
							if (lua_isstring(lua_state, 6))
							{
								arg2 = Json::PathArgument(lua_tostring(lua_state, 6));
							}
							else
							{
								_log.Log(LOG_ERROR, "CLuaHandler (applyJsonPath from LUA) : Invalid extra argument #4 for domoticz_applyJsonPath");
								return 0;
							}
							if (nargs >= 7)
							{
								if (lua_isstring(lua_state, 7))
								{
									arg5 = Json::PathArgument(lua_tostring(lua_state, 7));
								}
								else
								{
									_log.Log(LOG_ERROR, "WebServer (applyJsonPath from LUA) : Invalid extra argument #5 for domoticz_applyJsonPath");
									return 0;
								}
							}
						}
					}
				}
			}

			// Apply the JsonPath to the Json
			Json::Path path(jsonpath, arg1, arg2, arg3, arg4, arg5);
			Json::Value& node = path.make(root);

			// Check if some data has been found
			if (!node.isNull())
			{
				if (node.isDouble())
				{
					lua_pushnumber(lua_state, node.asDouble());
					return 1;
				}
				if (node.isInt())
				{
					lua_pushnumber(lua_state, (double)node.asInt());
					return 1;
				}
				if (node.isInt64())
				{
					lua_pushnumber(lua_state, (double)node.asInt64());
					return 1;
				}
				if (node.isString())
				{
					lua_pushstring(lua_state, node.asCString());
					return 1;
				}
				lua_pushnil(lua_state);
				return 1;
			}
		}
		else
		{
			_log.Log(LOG_ERROR, "CLuaHandler (applyJsonPath from LUA) : Incorrect parameters type");
		}
	}
	else
	{
		_log.Log(LOG_ERROR, "CLuaHandler (applyJsonPath from LUA) : Incorrect parameters count");
	}
	return 0;
}

int CLuaHandler::l_domoticz_updateDevice(lua_State* lua_state)
{
	int nargs = lua_gettop(lua_state);
	if (nargs >= 3 && nargs <= 5)
	{
		// Supported format ares :
		// - deviceId (integer), svalue (string), nvalue (string), [rssi(integer)], [battery(integer)]
		// - deviceId (integer), svalue (string), nvalue (integer), [rssi(integer)], [battery(integer)]
		if (lua_isnumber(lua_state, 1) && (lua_isstring(lua_state, 2) || lua_isnumber(lua_state, 2)) && lua_isstring(lua_state, 3))
		{
			// Extract the parameters from the lua 'updateDevice' function
			int ideviceId = (int)lua_tointeger(lua_state, 1);
			std::string nvalue = lua_tostring(lua_state, 2);
			std::string svalue = lua_tostring(lua_state, 3);
			if (((lua_isstring(lua_state, 3) && nvalue.empty()) && svalue.empty()))
			{
				_log.Log(LOG_ERROR, "CLuaHandler (updateDevice from LUA) : nvalue and svalue are empty ");
				return 0;
			}

			// Parse
			int invalue = (!nvalue.empty()) ? atoi(nvalue.c_str()) : 0;
			int signallevel = 12;
			if (nargs >= 4 && lua_isnumber(lua_state, 4))
			{
				signallevel = (int)lua_tointeger(lua_state, 4);
			}
			int batterylevel = 255;
			if (nargs == 5 && lua_isnumber(lua_state, 5))
			{
				batterylevel = (int)lua_tointeger(lua_state, 5);
			}
			_log.Log(LOG_NORM, "CLuaHandler (updateDevice from LUA) : idx=%d nvalue=%s svalue=%s invalue=%d signallevel=%d batterylevel=%d", ideviceId, nvalue.c_str(), svalue.c_str(), invalue, signallevel, batterylevel);

			// Get the raw device parameters
			std::vector<std::vector<std::string> > result;
			result = m_sql.safe_query("SELECT HardwareID, DeviceID, Unit, Type, SubType FROM DeviceStatus WHERE (ID==%d)", ideviceId);
			if (result.empty())
				return 0;
			std::string hid = result[0][0];
			std::string did = result[0][1];
			std::string dunit = result[0][2];
			std::string dtype = result[0][3];
			std::string dsubtype = result[0][4];

			int HardwareID = atoi(hid.c_str());
			std::string DeviceID = did;
			int unit = atoi(dunit.c_str());
			int devType = atoi(dtype.c_str());
			int subType = atoi(dsubtype.c_str());

			std::stringstream sstr;
			unsigned long long ulIdx;
			sstr << ideviceId;
			sstr >> ulIdx;
			m_mainworker.UpdateDevice(HardwareID, DeviceID, unit, devType, subType, invalue, svalue, signallevel, batterylevel);
		}
		else
		{
			_log.Log(LOG_ERROR, "CLuaHandler (updateDevice from LUA) : Incorrect parameters type");
		}
	}
	else
	{
		_log.Log(LOG_ERROR, "CLuaHandler (updateDevice from LUA) : Not enough parameters");
	}
	return 0;
}

int CLuaHandler::l_domoticz_print(lua_State* lua_state)
{
	int nargs = lua_gettop(lua_state);

	for (int i = 1; i <= nargs; i++)
	{
		if (lua_isstring(lua_state, i))
		{
			//std::string lstring=lua_tostring(lua_state, i);
			_log.Log(LOG_NORM, "CLuaHandler: udevices: %s", lua_tostring(lua_state, i));
		}
		else
		{
			/* non strings? */
		}
	}
	return 0;
}

CLuaHandler::CLuaHandler(int hwdID)
{
	m_HwdID = hwdID;
}

void CLuaHandler::luaThread(lua_State *lua_state, const std::string &filename)
{
	int status;

	status = lua_pcall(lua_state, 0, LUA_MULTRET, 0);
	report_errors(lua_state, status);
	lua_close(lua_state);
}

void CLuaHandler::luaStop(lua_State *L, lua_Debug *ar)
{
	if (ar->event == LUA_HOOKCOUNT)
	{
		(void)ar;  /* unused arg. */
		lua_sethook(L, NULL, 0, 0);
		luaL_error(L, "LuaHandler: Lua script execution exceeds maximum number of lines");
		lua_close(L);
	}
}

void CLuaHandler::report_errors(lua_State *L, int status)
{
	if (status != 0) {
		_log.Log(LOG_ERROR, "CLuaHandler: %s", lua_tostring(L, -1));
		lua_pop(L, 1); // remove error message
	}
}

bool CLuaHandler::executeLuaScript(const std::string &script, const std::string &content)
{
	std::vector<std::string> allParameters;
	return executeLuaScript(script, content, allParameters);
}

bool CLuaHandler::executeLuaScript(const std::string &script, const std::string &content, std::vector<std::string>& allParameters)
{
	std::stringstream lua_DirT;
#ifdef WIN32
	lua_DirT << szUserDataFolder << "scripts\\lua_parsers\\";
#else
	lua_DirT << szUserDataFolder << "scripts/lua_parsers/";
#endif
	std::string lua_Dir = lua_DirT.str();

	lua_State *lua_state;
	lua_state = luaL_newstate();

	luaL_openlibs(lua_state);
	lua_pushcfunction(lua_state, l_domoticz_print);
	lua_setglobal(lua_state, "print");

	lua_pushcfunction(lua_state, l_domoticz_updateDevice);
	lua_setglobal(lua_state, "domoticz_updateDevice");

	lua_pushcfunction(lua_state, l_domoticz_applyJsonPath);
	lua_setglobal(lua_state, "domoticz_applyJsonPath");

	lua_pushcfunction(lua_state, l_domoticz_applyXPath);
	lua_setglobal(lua_state, "domoticz_applyXPath");

	lua_pushinteger(lua_state, m_HwdID);
	lua_setglobal(lua_state, "hwdId");

	lua_createtable(lua_state, 1, 0);
	lua_pushstring(lua_state, "content");
	lua_pushstring(lua_state, content.c_str());
	lua_rawset(lua_state, -3);
	lua_setglobal(lua_state, "request");

	// Push all url parameters as a map indexed by the parameter name
	// Each entry will be uri[<param name>] = <param value>
	int totParameters = (int)allParameters.size();
	lua_createtable(lua_state, totParameters, 0);
	for (int i = 0; i < totParameters; i++)
	{
		std::vector<std::string> parameterCouple;
		StringSplit(allParameters[i], "=", parameterCouple);
		if (parameterCouple.size() == 2) {
			// Add an url parameter after 'url' decoding it
			lua_pushstring(lua_state, CURLEncode::URLDecode(parameterCouple[0]).c_str());
			lua_pushstring(lua_state, CURLEncode::URLDecode(parameterCouple[1]).c_str());
			lua_rawset(lua_state, -3);
		}
	}
	lua_setglobal(lua_state, "uri");

	std::string fullfilename = lua_Dir + script;
	int status = luaL_loadfile(lua_state, fullfilename.c_str());
	if (status == 0)
	{
		lua_sethook(lua_state, luaStop, LUA_MASKCOUNT, 10000000);
		boost::thread aluaThread(boost::bind(&CLuaHandler::luaThread, this, lua_state, fullfilename));
		aluaThread.timed_join(boost::posix_time::seconds(10));
		return true;
	}
	else
	{
		report_errors(lua_state, status);
		lua_close(lua_state);
	}
	return false;
}