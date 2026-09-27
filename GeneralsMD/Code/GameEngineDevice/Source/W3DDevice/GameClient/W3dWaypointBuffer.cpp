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

// FILE: W3DWaypointBuffer.cpp ////////////////////////////////////////////////
//-----------------------------------------------------------------------------
//
//                       Electronic Arts Pacific.
//
//                       Confidential Information
//                Copyright (C) 2002 - All Rights Reserved
//
//-----------------------------------------------------------------------------
//
// Project:   Command & Conquers: Generals
//
// File name: W3DWaypointBuffer.cpp
//
// Created:   Kris Morness, October 2002
//
// Desc:      Draw buffer to handle all the waypoints in the scene. Waypoints
//            are rendered after terrain, after roads & bridges, and after
//            global fog, but before structures, objects, units, trees, etc.
//            This way if we have two waypoints at the bottom of a hill but
//            going through the hill, the line won't get cut off. However,
//            structures and units on top of paths will render above it. Waypoints
//            are only shown for selected units while in waypoint plotting mode.
//
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
//         Includes
//-----------------------------------------------------------------------------

#include "W3DDevice/GameClient/W3DWaypointBuffer.h"

#include <WW3D2/assetmgr.h>
#include <WW3D2/texture.h>

#include "Common/GameUtility.h"
#include "Common/GlobalData.h"
#include "Common/Player.h"			// TheSuperHackers @feature needed for Player::getRelationship when filtering whose paths to draw
#include "Common/RandomValue.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"

#include "GameClient/Drawable.h"
#include "GameClient/GameClient.h"
#include "GameClient/InGameUI.h"
#include "GameClient/View.h"			// TheSuperHackers @feature TheTacticalView, used to cull routes that are off screen

#include "GameLogic/GameLogic.h"		// TheSuperHackers @feature TheGameLogic, used to resolve the construction site of an in-progress waypoint build
#include "GameLogic/Object.h"

#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/DozerAIUpdate.h"	// TheSuperHackers @feature DozerAIInterface, used to read queued ghost build orders

#include "W3DDevice/GameClient/TerrainTex.h"
#include "W3DDevice/GameClient/HeightMap.h"

#include "WW3D2/camera.h"
#include "WW3D2/dx8wrapper.h"
#include "WW3D2/dx8renderer.h"
#include "WW3D2/mesh.h"
#include "WW3D2/meshmdl.h"
#include "WW3D2/segline.h"


#define MAX_DISPLAY_NODES 512

// TheSuperHackers @feature Safety cap on how many routes we will plot in a single frame.
// Routes that are entirely off screen are culled before they count towards this (see drawWaypoints),
// so in practice this only bites when you are zoomed all the way out and genuinely have dozens of
// units' paths overlapping. Each route costs one SegmentedLine render plus an robj render per node.
#define MAX_WAYPOINT_PATHS_SHOWN 64





//=============================================================================
// W3DWaypointBuffer::W3DWaypointBuffer
//=============================================================================
/** Constructor. Sets m_initialized to true if it finds the w3d models it needs
for the bibs. */
//=============================================================================
W3DWaypointBuffer::W3DWaypointBuffer()
{
	m_waypointNodeRobj = WW3DAssetManager::Get_Instance()->Create_Render_Obj( "SCMNode" );
	m_line = new SegmentedLineClass;

	m_texture = WW3DAssetManager::Get_Instance()->Get_Texture( "EXLaser.tga" );


  setDefaultLineStyle();
}

//=============================================================================
// W3DWaypointBuffer::~W3DWaypointBuffer
//=============================================================================
/** Destructor. Releases w3d assets. */
//=============================================================================
W3DWaypointBuffer::~W3DWaypointBuffer()
{
	REF_PTR_RELEASE( m_waypointNodeRobj );
	REF_PTR_RELEASE( m_texture );
	REF_PTR_RELEASE( m_line );
}

//=============================================================================
// W3DWaypointBuffer::freeBibBuffers
//=============================================================================
/** Frees the index and vertex buffers. */
//=============================================================================
void W3DWaypointBuffer::freeWaypointBuffers()
{
}


void W3DWaypointBuffer::setDefaultLineStyle()
{
	if( m_texture )
	{
		m_line->Set_Texture( m_texture );
	}
	ShaderClass lineShader=ShaderClass::_PresetAdditiveShader;
	lineShader.Set_Depth_Compare(ShaderClass::PASS_ALWAYS);
	m_line->Set_Shader( lineShader );	//pick the alpha blending mode you want - see shader.h for others.
	m_line->Set_Width( 1.5f );
	m_line->Set_Color( Vector3( 0.25f, 0.5f, 1.0f ) );
	m_line->Set_Texture_Mapping_Mode( SegLineRendererClass::TILED_TEXTURE_MAP );	//this tiles the texture across the line
}


//=============================================================================
// TheSuperHackers @feature doesSegmentCrossRect
//=============================================================================
/** Liang–Barsky clip: does the segment (x0,y0)-(x1,y1) touch the axis-aligned rectangle
	[xMin,yMin]-[xMax,yMax]? Used to catch a path leg that cuts across the screen even though
	neither of its endpoints is on screen — testing the endpoints alone cannot answer that. */
//=============================================================================
static Bool doesSegmentCrossRect( Real x0, Real y0, Real x1, Real y1,
																	Real xMin, Real yMin, Real xMax, Real yMax )
{
	const Real dx = x1 - x0;
	const Real dy = y1 - y0;

	// Each rectangle edge is a half-space p*t <= q. Entering edges push t0 up, leaving edges
	// pull t1 down; if the two ever cross, the segment never gets inside the rectangle.
	const Real p[ 4 ] = { -dx, dx, -dy, dy };
	const Real q[ 4 ] = { x0 - xMin, xMax - x0, y0 - yMin, yMax - y0 };

	Real t0 = 0.0f;
	Real t1 = 1.0f;

	for( int i = 0; i < 4; ++i )
	{
		if( p[ i ] == 0.0f )
		{
			// parallel to this edge and entirely outside it
			if( q[ i ] < 0.0f )
				return FALSE;
			continue;
		}

		const Real t = q[ i ] / p[ i ];
		if( p[ i ] < 0.0f )
		{
			if( t > t1 )
				return FALSE;
			if( t > t0 )
				t0 = t;
		}
		else
		{
			if( t < t0 )
				return FALSE;
			if( t < t1 )
				t1 = t;
		}
	}

	return TRUE;
}

//=============================================================================
//=============================================================================
// Ghost build previews — TheSuperHackers @feature
//=============================================================================
// A waypoint build order is only an *intent*: no foundation exists and no money has been
// spent until the builder reaches the waypoint the order is bound to. To make that visible
// we draw a translucent copy of the building at the recorded site and at the recorded angle,
// using exactly the same mechanism as the build-placement cursor preview (see
// InGameUI::updatePlacementIcons): a Drawable with no Object behind it.
//
// This is client-only presentation. The drawables are created and destroyed lazily and are
// never part of the simulation, so they cannot affect lockstep.
//=============================================================================

enum { MAX_GHOST_BUILD_PREVIEWS = 64 };

static Drawable *s_ghostPreviewDrawables[ MAX_GHOST_BUILD_PREVIEWS ] = { nullptr };
static const ThingTemplate *s_ghostPreviewTemplates[ MAX_GHOST_BUILD_PREVIEWS ] = { nullptr };

// TheSuperHackers @bugfix Ghost drawables must NEVER be destroyed from inside the drawable
// walk below. drawGhostBuildPreviews() iterates TheGameClient's drawable list, and
// destroyDrawable() releases list nodes — destroying one mid-walk leaves the iterator pointing
// at freed memory and crashes the game. That is exactly what happened when a queued order
// finally built (its slot was torn down) or when the player pulled the builder away (the whole
// queue was torn down). So destruction is deferred to the START of the next call, i.e. before
// any walking happens on that frame.
static Drawable *s_ghostsPendingDestroy[ MAX_GHOST_BUILD_PREVIEWS * 2 ] = { nullptr };
static Int s_ghostsPendingDestroyCount = 0;

static void destroyGhostDeferred( Drawable *ghost )
{
	if( ghost != nullptr && s_ghostsPendingDestroyCount < MAX_GHOST_BUILD_PREVIEWS * 2 )
		s_ghostsPendingDestroy[ s_ghostsPendingDestroyCount++ ] = ghost;
}

void updateGhostBuildPreviews( Player *localPlayer )
{

	Int used = 0;

	// Safe point to destroy: this runs from InGameUI::update(), NOT from the render walk, so
	// mutating the drawable list here cannot corrupt anything that is currently being drawn.
	for( Int i = 0; i < s_ghostsPendingDestroyCount; i++ )
	{
		if( s_ghostsPendingDestroy[ i ] != nullptr )
			TheGameClient->destroyDrawable( s_ghostsPendingDestroy[ i ] );
		s_ghostsPendingDestroy[ i ] = nullptr;
	}
	s_ghostsPendingDestroyCount = 0;

	if( localPlayer != nullptr )
	{
		for( Drawable *draw = TheGameClient->firstDrawable();
				 draw != nullptr && used < MAX_GHOST_BUILD_PREVIEWS;
				 draw = draw->getNextDrawable() )
		{
			Object *obj = draw->getObject();
			if( obj == nullptr )
				continue;

			// Only preview orders belonging to ourselves or to an ally. Enemies and neutral
			// parties reveal nothing about where they intend to build.
			Bool previewVisible = ( obj->getControllingPlayer() == localPlayer );
			if( !previewVisible && obj->getTeam() != nullptr &&
					localPlayer->getRelationship( obj->getTeam() ) == ALLIES )
			{
				previewVisible = TRUE;
			}
			if( !previewVisible )
				continue;

			// Same rule as the routes: if we cannot see the builder through shroud/fog we do
			// not get to see what it is planning to build either.
			if( draw->getFullyObscuredByShroud() )
				continue;

			AIUpdateInterface *ai = obj->getAIUpdateInterface();
			if( ai == nullptr )
				continue;

			DozerAIInterface *dozer = ai->getDozerAIInterface();
			if( dozer == nullptr )
				continue;

			const Int queued = dozer->getQueuedBuildCount();
			for( Int i = 0; i < queued && used < MAX_GHOST_BUILD_PREVIEWS; i++ )
			{
				const ThingTemplate *tmpl = dozer->getQueuedBuildTemplate( i );
				const Coord3D *pos = dozer->getQueuedBuildPosition( i );
				if( tmpl == nullptr || pos == nullptr )
					continue;

				Drawable *ghost = s_ghostPreviewDrawables[ used ];

				// Only rebuild when the template changed; position and angle are cheap to
				// refresh on the existing drawable every frame.
				if( ghost != nullptr && s_ghostPreviewTemplates[ used ] != tmpl )
				{
					destroyGhostDeferred( ghost );
					ghost = nullptr;
				}

				if( ghost == nullptr )
				{
					UnsignedInt drawableStatus = DRAWABLE_STATUS_NO_STATE_PARTICLES;
					drawableStatus |= TheGlobalData->m_objectPlacementShadows ? DRAWABLE_STATUS_SHADOWS : 0;
					ghost = TheThingFactory->newDrawable( tmpl, drawableStatus );
					s_ghostPreviewDrawables[ used ] = ghost;
					s_ghostPreviewTemplates[ used ] = tmpl;
				}

				if( ghost == nullptr )
					continue;

				ghost->setPosition( pos );
				ghost->setDrawableOpacity( TheGlobalData->m_objectPlacementOpacity );
				ghost->setOrientation( dozer->getQueuedBuildAngle( i ) );

				// Give the preview the owning side's house colour. ThingFactory::newDrawable()
				// has no Player parameter, so the draw modules default to red and cannot know
				// who the building will belong to.
				// NOTE: this must be setIndicatorColor(), not colorTint(). colorTint() is just a
				// colour FLASH (Drawable::colorTint -> colorFlash) which tints the whole model
				// and never touches the house-colour parts, so they stayed red.
				// setIndicatorColor() walks every draw module and calls replaceIndicatorColor(),
				// which is what actually recolours those parts (same call the disguise code uses).
				ghost->setIndicatorColor( obj->getControllingPlayer()->getPlayerColor() );

				used++;
			}
		}
	}

	// Release previews left over from orders that have since been built or cancelled.
	// Deferred — see the note on s_ghostsPendingDestroy above.
	for( Int i = used; i < MAX_GHOST_BUILD_PREVIEWS; i++ )
	{
		if( s_ghostPreviewDrawables[ i ] != nullptr )
		{
			destroyGhostDeferred( s_ghostPreviewDrawables[ i ] );
			s_ghostPreviewDrawables[ i ] = nullptr;
			s_ghostPreviewTemplates[ i ] = nullptr;
		}
	}

}

// W3DWaypointBuffer::drawWaypoints
//=============================================================================
/** Draws the waypoints. Uses camera to cull */
//=============================================================================
void W3DWaypointBuffer::drawWaypoints(RenderInfoClass &rinfo)
{

  if ( ! TheInGameUI )
    return;


  setDefaultLineStyle();

	if( TheInGameUI->isInWaypointMode() )
	{
		//Create a default light environment with no lights and only full ambient.
		//@todo: Fix later by copying default scene light environment from W3DScene.cpp.
		LightEnvironmentClass lightEnv;
		lightEnv.Reset(Vector3(0,0,0), Vector3(1.0f,1.0f,1.0f));
		lightEnv.Pre_Render_Update(rinfo.Camera.Get_Transform());
		RenderInfoClass localRinfo(rinfo.Camera);
		localRinfo.light_environment=&lightEnv;
		Vector3 points[ MAX_DISPLAY_NODES + 1 ]; //Lines have nodes + 1 points.

		// TheSuperHackers @feature While plotting waypoints, show the REMAINING route of every
		// unit we are entitled to see — not just the ones currently selected. Only our own units
		// and our allies' are shown, so you can see where your teammates are headed and
		// coordinate with them. Enemies and neutral parties reveal nothing.
		//
		// This is purely local presentation and cannot desync online: every machine already
		// simulates every unit and every MSG_ADD_WAYPOINT is replicated, so each client has every
		// path in memory regardless of whose it is. Only the drawing decision is client-side.
		//
		// Drawing starts at friend_getCurrentGoalPathIndex(), which advances as the unit consumes
		// nodes, so what you see is naturally only the part still left to walk.
		Player *localPlayer = rts::getObservedOrLocalPlayer();

		// (the queued build previews are drawn above, outside this if, so they stay visible
		//  whether or not we are currently plotting waypoints)

		if( localPlayer && TheTacticalView )
		{
			Int pathsDrawn = 0;

			for( Drawable *draw = TheGameClient->firstDrawable();
					 draw != nullptr && pathsDrawn < MAX_WAYPOINT_PATHS_SHOWN;
					 draw = draw->getNextDrawable() )
			{
				Object *obj = draw->getObject();
				if( obj == nullptr )
					continue;

				//so mobs and stuff dont make a gazillion lines
				if( obj->isKindOf( KINDOF_IGNORED_IN_GUI ) )
					continue;

				// Only ever show our own units and our allies'. Enemies reveal nothing, and
				// neither do neutral parties — they are not on our team, so there is no reason
				// we should know where they are headed.
				Bool waypointVisible = ( obj->getControllingPlayer() == localPlayer );
				if( !waypointVisible && obj->getTeam() != nullptr &&
						localPlayer->getRelationship( obj->getTeam() ) == ALLIES )
				{
					waypointVisible = TRUE;
				}
				if( !waypointVisible )
					continue;

				// Likewise, don't leak anything through shroud/fog: if we can't see the unit we
				// don't get to see where it's going, even when it belongs to an ally.
				if( draw->getFullyObscuredByShroud() )
					continue;

				AIUpdateInterface *ai = obj->getAI();
				Int goalSize = ai ? ai->friend_getWaypointGoalPathSize() : 0;
				Int gpIdx = ai ? ai->friend_getCurrentGoalPathIndex() : 0;
				if( ai && gpIdx >= 0 && gpIdx < goalSize )
				{
					// TheSuperHackers @feature view culling, done properly:
					// worldToScreenTriReturn() still fills in a valid screen coordinate when a
					// point is OUTSIDE the frustum, so for each leg we can tell whether the
					// straight line between two off-screen points still cuts across the screen.
					const Int screenW = MAX( TheTacticalView->getWidth(), 1 );
					const Int screenH = MAX( TheTacticalView->getHeight(), 1 );

					Bool anyPartOnScreen = FALSE;

					// The unit itself anchors the polyline, and is the "previous point" the first
					// leg gets tested against.
					const Coord3D *pos = obj->getPosition();
					ICoord2D prevScreen;
					View::WorldToScreenReturn prevResult =
						TheTacticalView->worldToScreenTriReturn( pos, &prevScreen );
					Bool prevOut = ( prevResult == View::WTS_OUTSIDE_FRUSTUM );

					if( !prevOut )
						anyPartOnScreen = TRUE;	// on screen, or unprojectable — stay conservative

					points[ 0 ].Set( Vector3( pos->x, pos->y, pos->z ) );
					Int numPoints = 1;

					for( int i = gpIdx; i < goalSize; i++ )
					{
						const Coord3D *waypoint = ai->friend_getGoalPathPosition( i );
						if( waypoint == nullptr )
							continue;

						ICoord2D curScreen;
						View::WorldToScreenReturn curResult =
							TheTacticalView->worldToScreenTriReturn( waypoint, &curScreen );
						const Bool curOut = ( curResult == View::WTS_OUTSIDE_FRUSTUM );

						Bool nodeOutOfSight = TRUE;
						if( !curOut )
						{
							anyPartOnScreen = TRUE;
							nodeOutOfSight = FALSE;
						}
						else if( !prevOut )
						{
							// one end is visible (or unprojectable), so this leg is partly visible
							anyPartOnScreen = TRUE;
						}
						else if( doesSegmentCrossRect( (Real)prevScreen.x, (Real)prevScreen.y,
																					 (Real)curScreen.x,  (Real)curScreen.y,
																					 0.0f, 0.0f,
																					 (Real)( screenW - 1 ), (Real)( screenH - 1 ) ) )
						{
							// both ends are off screen but the leg passes through what we can see
							anyPartOnScreen = TRUE;
						}

						// The little ice-hockey puck only needs drawing where it can be seen.
						if( !nodeOutOfSight )
						{
							m_waypointNodeRobj->Set_Position(Vector3(waypoint->x,waypoint->y,waypoint->z));
							WW3D::Render(*m_waypointNodeRobj,localRinfo);
						}

						//Render line from previous point to current node.
						if( numPoints < MAX_DISPLAY_NODES + 1 )
						{
							points[ numPoints ].Set( Vector3( waypoint->x, waypoint->y, waypoint->z ) );
							numPoints++;
						}

						prevScreen = curScreen;
						prevOut = curOut;
					}

					// Nothing of this route reaches the screen — draw nothing and don't even count
					// it against MAX_WAYPOINT_PATHS_SHOWN, since it costs us nothing either way.
					if( !anyPartOnScreen )
						continue;

					// Own units keep the classic blue; allies get teal so you can tell the two
					// apart at a glance when several routes cross.
					if( obj->getControllingPlayer() == localPlayer )
						m_line->Set_Color( Vector3( 0.25f, 0.5f, 1.0f ) );
					else
						m_line->Set_Color( Vector3( 0.2f, 0.85f, 0.6f ) );

					//Now render the lines in one pass!
					m_line->Set_Points( numPoints, points );
					m_line->Render( localRinfo );

					pathsDrawn++;
				}

				// TheSuperHackers @feature Show the build orders in the route line, so the player
				// can see the ORDER everything will happen in while plotting waypoints.
				// Build sites are deliberately NOT part of the unit's goal path (queueConstruct
				// never appends them — walking onto the middle of a foundation makes newTask()
				// fail), so without this the route would just stop at the last movement waypoint.
				// This chain continues exactly where the movement route left off:
				//   ... movement nodes -> [site under construction] -> queued ghost sites ...
				// Same plain node style and colour as the rest of the route.
				DozerAIInterface *dozer = ai ? ai->getDozerAIInterface() : nullptr;
				if( dozer != nullptr &&
						obj->getControllingPlayer() == localPlayer &&
						pathsDrawn < MAX_WAYPOINT_PATHS_SHOWN )
				{
					const Int queued = dozer->getQueuedBuildCount();
					Object *site = dozer->isTaskPending( DOZER_TASK_BUILD )
							? TheGameLogic->findObjectByID( dozer->getTaskTarget( DOZER_TASK_BUILD ) )
							: nullptr;

					// the chain starts where the movement route ended (or at the unit itself)
					const Coord3D *chainStart = nullptr;
					if( goalSize > 0 && gpIdx >= 0 )
						chainStart = ai->friend_getGoalPathPosition( goalSize - 1 );
					if( chainStart == nullptr )
						chainStart = obj->getPosition();

					Int numChain = 0;
					Vector3 chainPoints[ MAX_DISPLAY_NODES + 1 ];
					if( chainStart != nullptr )
						chainPoints[ numChain++ ].Set( Vector3( chainStart->x, chainStart->y, chainStart->z ) );

					// the site currently under construction comes first, if there is one
					if( site != nullptr && numChain < MAX_DISPLAY_NODES + 1 )
					{
						const Coord3D *sitePos = site->getPosition();
						chainPoints[ numChain++ ].Set( Vector3( sitePos->x, sitePos->y, sitePos->z ) );
						m_waypointNodeRobj->Set_Position( Vector3( sitePos->x, sitePos->y, sitePos->z ) );
						WW3D::Render( *m_waypointNodeRobj, localRinfo );
					}

					// then every still-queued ghost order, in the order they will be built
					for( Int q = 0; q < queued && numChain < MAX_DISPLAY_NODES + 1; q++ )
					{
						const Coord3D *ghostPos = dozer->getQueuedBuildPosition( q );
						if( ghostPos == nullptr )
							continue;
						chainPoints[ numChain++ ].Set( Vector3( ghostPos->x, ghostPos->y, ghostPos->z ) );
						m_waypointNodeRobj->Set_Position( Vector3( ghostPos->x, ghostPos->y, ghostPos->z ) );
						WW3D::Render( *m_waypointNodeRobj, localRinfo );
					}

					if( numChain >= 2 )
					{
						m_line->Set_Color( Vector3( 0.25f, 0.5f, 1.0f ) );
						m_line->Set_Points( numChain, chainPoints );
						m_line->Render( localRinfo );
						pathsDrawn++;
					}
				}
			}
		}
	}
	else // maybe we want to draw rally points, then?
	{
		//Create a default light environment with no lights and only full ambient.
		//@todo: Fix later by copying default scene light environment from W3DScene.cpp.
		LightEnvironmentClass lightEnv;
		lightEnv.Reset(Vector3(0,0,0), Vector3(1.0f,1.0f,1.0f));
		lightEnv.Pre_Render_Update(rinfo.Camera.Get_Transform());
		RenderInfoClass localRinfo(rinfo.Camera);
		localRinfo.light_environment=&lightEnv;
		Vector3 points[ MAX_DISPLAY_NODES + 1 ]; //Lines have nodes + 1 points.

		const DrawableList *selected = TheInGameUI->getAllSelectedDrawables();
		Drawable *draw;
		for( DrawableListCIt it = selected->begin(); it != selected->end(); ++it )
		{
			draw = *it;
			Object *obj = draw->getObject();

			Int numPoints = 0;
			if( obj )
			{
				if ( obj->getControllingPlayer() != rts::getObservedOrLocalPlayer())
					continue;



        // WAIT! before we go browsing the drawable list for buildings that want to draw their rally points
        // lets test for that very special case of having a listeningoutpost selected, and some enemy drawable moused-over
        if ( obj->isKindOf( KINDOF_REVEALS_ENEMY_PATHS ) )
        {

          DrawableID enemyID = TheInGameUI->getMousedOverDrawableID();
          Drawable *enemyDraw = TheGameClient->findDrawableByID( enemyID );
          if ( enemyDraw )
          {
            Object *enemy = enemyDraw->getObject();
            if ( enemy )
            {
              if ( enemy->getRelationship( obj ) == ENEMIES )
              {

                Coord3D delta = *obj->getPosition();
                delta.sub( *enemy->getPosition() );
                if ( delta.length() <= obj->getVisionRange() ) // is listening outpost close enough to do this?
                {


                  //////////////////////////////////////////////////////////////////////
                  AIUpdateInterface *ai = enemy->getAI();
				          Int goalSize = ai ? ai->friend_getWaypointGoalPathSize() : 0;
				          Int gpIdx = ai ? ai->friend_getCurrentGoalPathIndex() : 0;
                  if( ai )
                  {
                    Bool lineExists = FALSE;

					          const Coord3D *pos = enemy->getPosition();
					          points[ numPoints++ ].Set( Vector3( pos->x, pos->y, pos->z ) );

                    if ( gpIdx >= 0 && gpIdx < goalSize )// Ooh, the enemy is in waypoint mode
				            {

					            for( int i = gpIdx; i < goalSize; i++ )
					            {
						            const Coord3D *waypoint = ai->friend_getGoalPathPosition( i );
						            if( waypoint )
						            {
							            //Render line from previous point to current node.

							            if( numPoints < MAX_DISPLAY_NODES + 1 )
							            {
								            points[ numPoints++ ].Set( Vector3( waypoint->x, waypoint->y, waypoint->z ) );
							            }

							            m_waypointNodeRobj->Set_Position(Vector3(waypoint->x,waypoint->y,waypoint->z));
							            WW3D::Render(*m_waypointNodeRobj,localRinfo);
                          lineExists = TRUE;
						            }
					            }
                    }
                    else // then enemy may be moving to a goal position
                    {
                      const Coord3D *destinationPoint = ai->getGoalPosition();
                      if ( destinationPoint->length() > 1.0f )
                      {
								        points[ numPoints++ ].Set( Vector3( destinationPoint->x, destinationPoint->y, destinationPoint->z ) );
							          m_waypointNodeRobj->Set_Position(Vector3(destinationPoint->x,destinationPoint->y,destinationPoint->z));
							          WW3D::Render(*m_waypointNodeRobj,localRinfo);
                        lineExists = TRUE;
                      }
                    }

                    if ( lineExists )
                    {
					            //Now render the lines in one pass!

                      m_line->Set_Color( Vector3( 0.95f, 0.5f, 0.0f ) );
                      m_line->Set_Width( 3.0f );

					            m_line->Set_Points( numPoints, points );
					            m_line->Render( localRinfo );
                    }
                  }
                  //////////////////////////////////////////////////////////////////////




                }
              }
            }
          }

          break;// dont even bother with the rest, since this one listening outpost satisfies the single path-line limit
        }




				ExitInterface *exitInterface = obj->getObjectExitInterface();
				if( exitInterface )
				{

					Coord3D exitPoint;
					if ( ! exitInterface->getExitPosition(exitPoint))
						exitPoint = *obj->getPosition();

					points[ numPoints ].Set( Vector3( exitPoint.x, exitPoint.y, exitPoint.z ) );
					numPoints++;

					Bool boxWrap = TRUE;
					Coord3D naturalRallyPoint;
					if (exitInterface->getNaturalRallyPoint(naturalRallyPoint, FALSE))//FALSE means "without the extra offset"
					{
						if( !naturalRallyPoint.equals( exitPoint ) )
						{
							points[ numPoints ].Set( Vector3( naturalRallyPoint.x, naturalRallyPoint.y, naturalRallyPoint.z ) );
							numPoints++;
						}
						else
						{
							//Helipad rally point -- so don't use box wrapping.
							boxWrap = FALSE;
						}
					}
					else
						continue; //next drawable

					const Coord3D *rallyPoint = exitInterface->getRallyPoint();
					if( rallyPoint )
					{
						if( boxWrap )
						{

							//test to se whether the rally point flanks the side of the natural rally point
							//to do this we find the two corners of the geometry extents that are nearest the natural rally point
							// these define the natural rally point edge
							// an intermediate point should be inserted at the corner nearest the rally point
							const GeometryInfo& geom = obj->getGeometryInfo();
							const Coord3D *ctr = obj->getPosition();
							Coord3D NRPDelta;
							NRPDelta.x = naturalRallyPoint.x - exitPoint.x;
							NRPDelta.y = naturalRallyPoint.y - exitPoint.y;
							NRPDelta.z = 0.0f;


							//This is a quick idiot test to se whether the rally line needs to wrap the box at all
							Coord3D wayOutPoint = NRPDelta;
							wayOutPoint.normalize();
							wayOutPoint.scale( 99999.9f );
							Real wayOutLength = wayOutPoint.length();
							wayOutPoint.add(naturalRallyPoint);


							//if the rallypoint is closer to the wayoutpoint than it is to the natural rally point then we definitely do not wrap
							Coord3D rallyToWayOutDelta = wayOutPoint;
							rallyToWayOutDelta.sub(*rallyPoint);
							if ( (100.0f + rallyToWayOutDelta.length()) > wayOutLength)
							{

								//if we passed the above idiot test, now lets be sure by testing the dotproduct of the rp against the wayoutpoint
								wayOutPoint.normalize();// a normal shooting straight out the door
								//next comes the delta between the NRP and the RP
								Coord3D NRPToRPDelta = naturalRallyPoint;
								NRPToRPDelta.sub(*rallyPoint);
								NRPToRPDelta.normalize();
								Real dot = NRPToRPDelta.x * wayOutPoint.x + NRPToRPDelta.y * wayOutPoint.y;
								if (dot > 0)
								{

									Real angle = obj->getOrientation();
									Real c = (Real)cos(angle);
									Real s = (Real)sin(angle);

									Coord3D NRPToCtrDelta;
									NRPToCtrDelta.x = naturalRallyPoint.x - ctr->x;
									NRPToCtrDelta.y = naturalRallyPoint.y - ctr->y;
									NRPToCtrDelta.x = 0.0f;

									Real exc = geom.getMajorRadius() * c;
									Real eyc = geom.getMinorRadius() * c;
									Real exs = geom.getMajorRadius() * s;
									Real eys = geom.getMinorRadius() * s;

									Coord2D corners[ 4 ];

									corners[0].x = ctr->x - exc - eys;
									corners[0].y = ctr->y + eyc - exs;
									corners[1].x = ctr->x + exc - eys;
									corners[1].y = ctr->y + eyc + exs;
									corners[2].x = ctr->x + exc + eys;
									corners[2].y = ctr->y - eyc + exs;
									corners[3].x = ctr->x - exc + eys;
									corners[3].y = ctr->y - eyc - exs;

									Coord2D *pNearElbow = nullptr;//find the closest corner to the rallyPoint same end as door
									Coord2D *pFarElbow = nullptr; //find the closest corner to the rallypoint away from door
									Coord2D *nearCandidate = nullptr;
									Coord3D cornerToRPDelta, cornerToExitDelta;
									cornerToRPDelta.z = 0.0f;
									cornerToExitDelta.z = 0.0f;
									Real elbowDistanceNear = 99999.9f;
									Real elbowDistanceFar = 99999.9f;

									for (UnsignedInt cornerIndex = 0; cornerIndex < 4; ++ cornerIndex)
									{
										nearCandidate = &corners[cornerIndex];//for quicker array access
										cornerToExitDelta.x = exitPoint.x - nearCandidate->x;
										cornerToExitDelta.y = exitPoint.y - nearCandidate->y;
										cornerToExitDelta.normalize();

										dot = cornerToExitDelta.x * wayOutPoint.x + cornerToExitDelta.y * wayOutPoint.y;
										if ( dot < 0.0f )
										{
											cornerToRPDelta.x = rallyPoint->x - nearCandidate->x;
											cornerToRPDelta.y = rallyPoint->y - nearCandidate->y;
											if (cornerToRPDelta.length() < elbowDistanceNear)
											{
												elbowDistanceNear = cornerToRPDelta.length();
												pNearElbow = nearCandidate;
											}
										}
										else
										{
											cornerToRPDelta.x = rallyPoint->x - nearCandidate->x;
											cornerToRPDelta.y = rallyPoint->y - nearCandidate->y;
											if (cornerToRPDelta.length() < elbowDistanceFar)
											{
												elbowDistanceFar = cornerToRPDelta.length();
												pFarElbow = nearCandidate;
											}
										}



									}

									if (pNearElbow)//did we find a nearest corner?
									{
										m_waypointNodeRobj->Set_Position(Vector3(pNearElbow->x,pNearElbow->y,ctr->z));
										WW3D::Render(*m_waypointNodeRobj,localRinfo); //The little hockey puck
										points[ numPoints ].Set( Vector3( pNearElbow->x, pNearElbow->y, ctr->z ) );
										numPoints++;


										//and for that matter did we find a far side corner?
										if (pFarElbow)//did we find a nearest corner?
										{
											// but let's test the dot of the first elbow against the rally point to find out
											//whethet the rally point wraps around this one, too
											Coord3D firstElbowDelta;
											firstElbowDelta.x = naturalRallyPoint.x - pNearElbow->x;
											firstElbowDelta.y = naturalRallyPoint.y - pNearElbow->y;
											firstElbowDelta.z = 0.0f;
											firstElbowDelta.normalize();

											Coord3D firstToRPDelta;
											firstToRPDelta.x = pNearElbow->x - rallyPoint->x;
											firstToRPDelta.y = pNearElbow->y - rallyPoint->y;
											firstToRPDelta.z = 0.0f;
											firstToRPDelta.normalize();

											dot = firstToRPDelta.x * firstElbowDelta.x + firstToRPDelta.y * firstElbowDelta.y;
											if (dot < 0)// we have a second elbow
											{
												m_waypointNodeRobj->Set_Position(Vector3(pFarElbow->x,pFarElbow->y,ctr->z));
												WW3D::Render(*m_waypointNodeRobj,localRinfo); //The little hockey puck
												points[ numPoints ].Set( Vector3( pFarElbow->x, pFarElbow->y, ctr->z ) );
												numPoints++;
											}
										}

									}

								}

							}
						}

						// Finally draw the line out to the RallyPoint
						points[ numPoints ].Set( Vector3( rallyPoint->x, rallyPoint->y, rallyPoint->z ) );
						numPoints++;

					}
					else
						continue;

					m_waypointNodeRobj->Set_Position(Vector3(naturalRallyPoint.x,naturalRallyPoint.y,naturalRallyPoint.z));
					WW3D::Render(*m_waypointNodeRobj,localRinfo); //The little hockey puck


					m_line->Set_Points( numPoints, points );
					m_line->Render( localRinfo );

				}

			}
		}

	}
}


