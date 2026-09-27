/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

// Registry.cpp
// Simple interface for storing/retrieving registry values
// Author: Matthew D. Campbell, December 2001

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/Registry.h"

#include "../NGMP_include.h"
#include <format>
#include <filesystem>

// TheSuperHackers @bugfix installers may write the game keys to either the 32-bit or the
// 64-bit view of the registry; a 32-bit process only sees the Wow6432Node view by default,
// so both views must be probed explicitly for the registry fallback to be reliable.
#ifndef KEY_WOW64_64KEY
#define KEY_WOW64_64KEY 0x0100
#endif
#ifndef KEY_WOW64_32KEY
#define KEY_WOW64_32KEY 0x0200
#endif

Bool  getStringFromRegistry(HKEY root, AsciiString path, AsciiString key, AsciiString& val, REGSAM extraAccess = 0)
{
	HKEY handle;
	unsigned char buffer[256];
	unsigned long size = 256;
	unsigned long type;
	int returnValue;

	if ((returnValue = RegOpenKeyEx( root, path.str(), 0, KEY_READ | extraAccess, &handle )) == ERROR_SUCCESS)
	{
		returnValue = RegQueryValueEx(handle, key.str(), nullptr, &type, (unsigned char *) &buffer, &size);
		RegCloseKey( handle );
	}

	if (returnValue == ERROR_SUCCESS)
	{
		val = (char *)buffer;
		return TRUE;
	}

	return FALSE;
}

// TheSuperHackers @bugfix probe a value in both hives and both registry views
static Bool getStringFromRegistryEveryView(const char *subKey, const char *valueName, AsciiString& val)
{
	static const HKEY roots[] = { HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER };
	static const REGSAM views[] = { 0, KEY_WOW64_64KEY, KEY_WOW64_32KEY };

	for (HKEY root : roots)
	{
		for (REGSAM view : views)
		{
			if (getStringFromRegistry(root, subKey, valueName, val, view))
				return TRUE;
		}
	}

	return FALSE;
}

Bool getUnsignedIntFromRegistry(HKEY root, AsciiString path, AsciiString key, UnsignedInt& val)
{
	HKEY handle;
	unsigned char buffer[4];
	unsigned long size = 4;
	unsigned long type;
	int returnValue;

	if ((returnValue = RegOpenKeyEx( root, path.str(), 0, KEY_READ, &handle )) == ERROR_SUCCESS)
	{
		returnValue = RegQueryValueEx(handle, key.str(), nullptr, &type, (unsigned char *) &buffer, &size);
		RegCloseKey( handle );
	}

	if (returnValue == ERROR_SUCCESS)
	{
		val = *(UnsignedInt *)buffer;
		return TRUE;
	}

	return FALSE;
}

Bool setStringInRegistry( HKEY root, AsciiString path, AsciiString key, AsciiString val)
{
	HKEY handle;
	unsigned long type;
	unsigned long returnValue;
	int size;
	char lpClass[] = "REG_NONE";

	if ((returnValue = RegCreateKeyEx( root, path.str(), 0, lpClass, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &handle, nullptr )) == ERROR_SUCCESS)
	{
		type = REG_SZ;
		size = val.getLength()+1;
		returnValue = RegSetValueEx(handle, key.str(), 0, type, (unsigned char *)val.str(), size);
		RegCloseKey( handle );
	}

	return (returnValue == ERROR_SUCCESS);
}

Bool setUnsignedIntInRegistry( HKEY root, AsciiString path, AsciiString key, UnsignedInt val)
{
	HKEY handle;
	unsigned long type;
	unsigned long returnValue;
	int size;
	char lpClass[] = "REG_NONE";

	if ((returnValue = RegCreateKeyEx( root, path.str(), 0, lpClass, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &handle, nullptr )) == ERROR_SUCCESS)
	{
		type = REG_DWORD;
		size = 4;
		returnValue = RegSetValueEx(handle, key.str(), 0, type, (unsigned char *)&val, size);
		RegCloseKey( handle );
	}

	return (returnValue == ERROR_SUCCESS);
}

Bool GetStringFromGeneralsRegistry(AsciiString path, AsciiString key, AsciiString& val)
{
	// TheSuperHackers @bugfix probe every known install location of the original Generals
	// (retail/EA App and The First Decade) across both hives and both registry views, so the
	// fallback in the BIG file system works even when no local ZH_Generals folder exists.
	// Stale entries pointing at a directory that no longer exists are skipped so the next
	// probe (and finally the caller's failure handling) gets its chance.
	if (path.isEmpty())
	{
		struct RegistryProbe { const char *subKey; const char *valueName; };
		static const RegistryProbe probes[] = {
			{ "SOFTWARE\\Electronic Arts\\EA Games\\Generals", "InstallPath" },
			{ "SOFTWARE\\Electronic Arts\\EA Games\\Command and Conquer The First Decade", "gr_folder" },
		};

		for (const RegistryProbe &probe : probes)
		{
			if (getStringFromRegistryEveryView(probe.subKey, probe.valueName, val))
			{
				if (std::filesystem::is_directory(val.str()))
					return TRUE;

				DEBUG_LOG(("GetStringFromGeneralsRegistry - skipping stale entry %s=%s", probe.subKey, val.str()));
			}
		}

		val = AsciiString::TheEmptyString;
		return FALSE;
	}

	AsciiString fullPath = "SOFTWARE\\Electronic Arts\\EA Games\\Generals";

	fullPath.concat(path);
	DEBUG_LOG(("GetStringFromRegistry - looking in %s for key %s", fullPath.str(), key.str()));
	return getStringFromRegistryEveryView(fullPath.str(), key.str(), val);
}

Bool GetStringFromRegistry(AsciiString path, AsciiString key, AsciiString& val)
{
#if RTS_GENERALS
	AsciiString fullPath = "SOFTWARE\\Electronic Arts\\EA Games\\Generals";
#elif RTS_ZEROHOUR
	AsciiString fullPath = "SOFTWARE\\Electronic Arts\\EA Games\\Command and Conquer Generals Zero Hour";
#endif

	fullPath.concat(path);
	DEBUG_LOG(("GetStringFromRegistry - looking in %s for key %s", fullPath.str(), key.str()));
	if (getStringFromRegistry(HKEY_CURRENT_USER, fullPath.str(), key.str(), val))
	{
		return TRUE;
	}

	return getStringFromRegistry(HKEY_LOCAL_MACHINE, fullPath.str(), key.str(), val);
}

Bool GetUnsignedIntFromRegistry(AsciiString path, AsciiString key, UnsignedInt& val)
{
#if RTS_GENERALS
	AsciiString fullPath = "SOFTWARE\\Electronic Arts\\EA Games\\Generals";
#elif RTS_ZEROHOUR
	AsciiString fullPath = "SOFTWARE\\Electronic Arts\\EA Games\\Command and Conquer Generals Zero Hour";
#endif

	fullPath.concat(path);
	DEBUG_LOG(("GetUnsignedIntFromRegistry - looking in %s for key %s", fullPath.str(), key.str()));
	if (getUnsignedIntFromRegistry(HKEY_CURRENT_USER, fullPath.str(), key.str(), val))
	{
		return TRUE;
	}

	return getUnsignedIntFromRegistry(HKEY_LOCAL_MACHINE, fullPath.str(), key.str(), val);
}

AsciiString GetRegistryLanguage()
{
	static Bool cached = FALSE;
	// NOTE: static causes a memory leak, but we have to keep it because the value is cached.
	static AsciiString val = "english";
	if (cached) {
		return val;
	} else {
		cached = TRUE;
	}

#if defined(GENERALS_ONLINE)
	bool bExistsInRegistry = GetStringFromRegistry("", "Language", val);
	
	if (!bExistsInRegistry)
	{
		// This is a crash fix, Steam client lets people change language post-install/on-demand, but doesnt update registry until run.
		// But its more reliable to just fall back and determine language from disk files instead of continuing and crashing because english (default) .big files don't exist

		// get current process dir
		char szProcessDir[MAX_PATH] = { 0 };
		DWORD length = GetModuleFileNameA(NULL, szProcessDir, MAX_PATH);
		if (length > 0 && length != MAX_PATH)
		{
			// Remove the executable name to get the directory
			for (int i = length - 1; i >= 0; --i) {
				if (szProcessDir[i] == '\\' || szProcessDir[i] == '/')
				{
					szProcessDir[i] = '\0';
					break;
				}
			}

			// now check which language exists
			std::map<std::string, AsciiString> languageFiles = {
				{"GermanZH", AsciiString("german")},
				{"FrenchZH", AsciiString("french")},
				{"KoreanZH", AsciiString("korean")},
				{"ItalianZH", AsciiString("italian")},
				{"SpanishZH", AsciiString("spanish")},
				{"ChineseZH", AsciiString("chinese")},
				{"PolishZH", AsciiString("polish")},
				{"BrazilianZH", AsciiString("brazilian")},
				{"EnglishZH", AsciiString("english")},
			};

			for (auto& kvPair : languageFiles)
			{
				std::string filePath = std::format("{}/{}.big", szProcessDir, kvPair.first);
				if (std::filesystem::exists(filePath))
				{
					val = kvPair.second;
					break;
				}

			}
		}
	}
#else
	GetStringFromRegistry("", "Language", val);
#endif
	return val;
}

AsciiString GetRegistryGameName()
{
	AsciiString val = "GeneralsMPTest";
	GetStringFromRegistry("", "SKU", val);
	return val;
}

UnsignedInt GetRegistryVersion()
{
	UnsignedInt val = 65536;
	GetUnsignedIntFromRegistry("", "Version", val);
	return val;
}

UnsignedInt GetRegistryMapPackVersion()
{
	UnsignedInt val = 65536;
	GetUnsignedIntFromRegistry("", "MapPackVersion", val);
	return val;
}
