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

// FILE: HotKey.cpp /////////////////////////////////////////////////
//-----------------------------------------------------------------------------
//
//                       Electronic Arts Pacific.
//
//                       Confidential Information
//                Copyright (C) 2002 - All Rights Reserved
//
//-----------------------------------------------------------------------------
//
//	created:	Sep 2002
//
//	Filename: 	HotKey.cpp
//
//	author:		Chris Huybregts
//
//	purpose:
//
//-----------------------------------------------------------------------------
///////////////////////////////////////////////////////////////////////////////

//-----------------------------------------------------------------------------
// SYSTEM INCLUDES ////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------
#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine
//-----------------------------------------------------------------------------
// USER INCLUDES //////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------
#include "GameClient/HotKey.h"
#include "GameClient/KeyDefs.h"
#include "GameClient/MetaEvent.h"
#include "GameClient/GameWindow.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/Keyboard.h"
#include "GameClient/InGameUI.h"
#include "GameClient/ControlBar.h"
#include "GameClient/GameText.h"
#include "Common/AudioEventRTS.h"
//-----------------------------------------------------------------------------
// DEFINES ////////////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// PUBLIC FUNCTIONS ///////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
GameMessageDisposition HotKeyTranslator::translateGameMessage(const GameMessage *msg)
{
	GameMessageDisposition disp = KEEP_MESSAGE;
	GameMessage::Type t = msg->getType();

	if ( t == GameMessage::MSG_RAW_KEY_UP)
	{

		//char key = msg->getArgument(0)->integer;
		Int keyState = msg->getArgument(1)->integer;

		// for our purposes here, we don't care to distinguish between right and left keys,
		// so just fudge a little to simplify things.
		Int newModState = 0;

		if( keyState & KEY_STATE_CONTROL )
		{
			newModState |= CTRL;
		}

		// TheSuperHackers @feature SHIFT is deliberately NOT filtered here any more.
		// SHIFT+letter goes through the very same button click path as SHIFT+click on the
		// command bar, so it picks the build target of the selected producer and queues it as
		// a group. The queued amount is decided in ControlBarCommandProcessing exactly like a
		// shifted mouse click, which keeps the two input paths consistent.

		// TheSuperHackers @feature ALT is deliberately NOT filtered here any more. ALT is the
		// waypoint-mode key, and while it is held the player still has to be able to pick
		// build targets with hot keys (R, U, ...) for the waypoint build feature. Only
		// CTRL is treated as a command modifier that suppresses hot keys, as before.
		if(newModState != 0)
			return disp;
		WideChar key = TheKeyboard->getPrintableKey((KeyDefType)msg->getArgument(0)->integer, 0);
		UnicodeString uKey;
		uKey.concat(key);
		AsciiString aKey;
		aKey.translate(uKey);
		const Bool executed = ( TheHotKeyManager != nullptr ) && TheHotKeyManager->executeHotKey( aKey );
		if( executed )
			disp = DESTROY_MESSAGE;
	}
	return disp;
}

//-----------------------------------------------------------------------------
HotKey::HotKey()
{
	m_win = nullptr;
	m_key.clear();
}

//-----------------------------------------------------------------------------
HotKeyManager::HotKeyManager()
{

}

//-----------------------------------------------------------------------------
HotKeyManager::~HotKeyManager()
{
	m_hotKeyMap.clear();
}

//-----------------------------------------------------------------------------
void HotKeyManager::init()
{
	m_hotKeyMap.clear();
}

//-----------------------------------------------------------------------------
void HotKeyManager::reset()
{
	m_hotKeyMap.clear();
}

//-----------------------------------------------------------------------------
void HotKeyManager::addHotKey( GameWindow *win, const AsciiString& keyIn)
{
	AsciiString key = keyIn;
	key.toLower();
	std::vector<HotKey>& hotKeys = m_hotKeyMap[key];

	// The command bar hands the same window its command over and over again, and unrelated
	// buttons may legitimately share a letter. Register the pair once, then keep it.
	for( const HotKey& hotKey : hotKeys )
	{
		if( hotKey.m_win == win )
			return;
	}

	HotKey newHK;
	newHK.m_key.set(key);
	newHK.m_win = win;
	hotKeys.push_back(newHK);
}

//-----------------------------------------------------------------------------
/** Fire the clicked state of a hot key window, exactly like a mouse click on it would. */
//-----------------------------------------------------------------------------
static Bool executeHotKeyWindow( GameWindow *win )
{
	if( BitIsSet( win->winGetStatus(), WIN_STATUS_ENABLED ) )
	{
		TheWindowManager->winSendSystemMsg( win->winGetParent(), GBM_SELECTED, (WindowMsgData)win, win->winGetWindowId() );

		// here we make the same click sound that the GUI uses when you click a button
		AudioEventRTS buttonClick("GUIClick");

		if( TheAudio )
		{
			TheAudio->addAudioEvent( &buttonClick );
		}
		return TRUE;
	}

	AudioEventRTS disabledClick( "GUIClickDisabled" );
	if( TheAudio )
	{
		TheAudio->addAudioEvent( &disabledClick );
	}
	return FALSE;
}

//-----------------------------------------------------------------------------
Bool HotKeyManager::executeHotKey( const AsciiString& keyIn )
{
	AsciiString key = keyIn;
	key.toLower();
	HotKeyMap::iterator it = m_hotKeyMap.find(key);
	if( it == m_hotKeyMap.end() )
		return FALSE;

	// TheSuperHackers @bugfix Ask the command bar first. Dozens of command buttons share the
	// same letter (the Chinese localization alone has two dozen "&R" labels), so the only
	// sensible answer is the button that is on the command bar right now.
	if( TheControlBar )
	{
		GameWindow *commandWin = TheControlBar->findCommandWindowByHotKey( key );
		if( commandWin )
			return executeHotKeyWindow( commandWin );
	}

	// Not a command bar button (science purchase, special power shortcut, ...). Several windows
	// can share this letter there too, so prefer one that is on screen and usable. The same
	// ancestor-chain visibility rule applies here: hidden parents hide their children.
	GameWindow *enabledWin = nullptr;
	GameWindow *visibleWin = nullptr;
	for( const HotKey& hotKey : it->second )
	{
		GameWindow *win = hotKey.m_win;
		if( !win )
			continue;

		Bool onScreen = TRUE;
		for( GameWindow *ancestor = win; ancestor != nullptr; ancestor = ancestor->winGetParent() )
		{
			if( ancestor->winIsHidden() )
			{
				onScreen = FALSE;
				break;
			}
		}
		if( !onScreen )
			continue;

		if( BitIsSet( win->winGetStatus(), WIN_STATUS_ENABLED ) )
		{
			enabledWin = win;
			break;
		}
		if( !visibleWin )
			visibleWin = win;
	}

	if( enabledWin )
		return executeHotKeyWindow( enabledWin );

	if( visibleWin )
		executeHotKeyWindow( visibleWin );

	return FALSE;
}

//-----------------------------------------------------------------------------
AsciiString HotKeyManager::searchHotKey( const AsciiString& label)
{
	return searchHotKey(TheGameText->fetch(label));
}

//-----------------------------------------------------------------------------
AsciiString HotKeyManager::searchHotKey( const UnicodeString& uStr )
{
	if(uStr.isEmpty())
		return AsciiString::TheEmptyString;

	const WideChar *marker = (const WideChar *)uStr.str();
	while (marker && *marker)
	{
		if (*marker == L'&')
		{
			// found a '&' - now look for the next char
			UnicodeString tmp = UnicodeString::TheEmptyString;
			tmp.concat(*(marker+1));
			AsciiString retStr;
			retStr.translate(tmp);
			return retStr;
		}
		marker++;
	}
	return AsciiString::TheEmptyString;
}

//-----------------------------------------------------------------------------
HotKeyManager *TheHotKeyManager = nullptr;

//-----------------------------------------------------------------------------
// PRIVATE FUNCTIONS //////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------

