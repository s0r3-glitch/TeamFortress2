//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================
#include "cbase.h"

#include "tf_autobalance.h"
#include "tf_gamerules.h"
#include "tf_matchmaking_shared.h"
#include "team.h"
#include "minigames/tf_duel.h"
#include "player_resource.h"
#include "tf_player_resource.h"

// memdbgon must be the last include file in a .cpp file!!!
#include <tier0/memdbgon.h>

extern ConVar mp_developer;
extern ConVar mp_teams_unbalance_limit;
extern ConVar tf_arena_use_queue;
extern ConVar mp_autoteambalance;
extern ConVar tf_autobalance_query_lifetime;
extern ConVar tf_autobalance_xp_bonus;

ConVar tf_autobalance_detected_delay( "tf_autobalance_detected_delay", "30", FCVAR_NONE );

//-----------------------------------------------------------------------------
// Purpose: resets the auto balance system when an auto balance occures
//-----------------------------------------------------------------------------
CTFAutobalance::CTFAutobalance()
{
	Reset(); //calls the reset function
}

//-----------------------------------------------------------------------------
// Purpose: does nothing
//-----------------------------------------------------------------------------
CTFAutobalance::~CTFAutobalance()
{
}

//-----------------------------------------------------------------------------
// Purpose: resets the auto balance system
//-----------------------------------------------------------------------------
void CTFAutobalance::Reset()
{
	m_iCurrentState = AB_STATE_INACTIVE; //changes the current state of the auto balance system to inactive
	m_iLightestTeam = m_iHeaviestTeam = TEAM_INVALID; //changes the lightest team and heaviest team to TEAM_INVALID
	m_nNeeded = 0; //changes the number of players needed to be auto balanced to 0
	m_flBalanceTeamsTime = -1.f; //changes how many times teams have have been auto balanced to -1.f?

	if ( m_vecPlayersAsked.Count() > 0 ) //if the amount of players greater then 0
	{
		// if we're resetting and we have people we haven't heard from yet, tell them to close their notification
		FOR_EACH_VEC( m_vecPlayersAsked, i ) //for each player who was asked
		{
			if ( m_vecPlayersAsked[i].hPlayer.Get() && ( m_vecPlayersAsked[i].eState == AB_VOLUNTEER_STATE_ASKED ) ) //if player_asked.playersname.get and player_asked.get_volunteer_state is asked 
			{
				CSingleUserRecipientFilter filter( m_vecPlayersAsked[i].hPlayer.Get() ); //filters the players name??????? no idea what CSingleUserRecipientFilter does
				filter.MakeReliable(); //makes the filter reliable
				UserMessageBegin( filter, "AutoBalanceVolunteer_Cancel" ); //sends the user a message saying the auto balance volunteer is canceled
				MessageEnd(); //ends the message
			}
		}

		m_vecPlayersAsked.Purge(); //purges the list of asked players
	}
}

//-----------------------------------------------------------------------------
// Purpose: resets the auto balance system when it gets shutdown
//-----------------------------------------------------------------------------
void CTFAutobalance::Shutdown()
{
	Reset(); //calls the reset function
}

//-----------------------------------------------------------------------------
// Purpose: resets the auto balance system when the level is shutdown and all player entities are removed.
// Assuming this is here becuase the game tries to auto balance the teams when the level is chaning
// and when players are disconnecting it might cause the auto balance system to do a team balance
// so it does a system reset so that its ready for use in the next level and carry over data is eliminated
//-----------------------------------------------------------------------------
void CTFAutobalance::LevelShutdownPostEntity()
{
	Reset();
}

//-----------------------------------------------------------------------------
// Purpose: Checks to see if the auto balance system should be activated.
//-----------------------------------------------------------------------------
bool CTFAutobalance::ShouldBeActive() const
{
	if ( !TFGameRules() ) //checks to see if the tf game rules is absent??? No clue why its checking for the not factor of it
		return false;

	if ( TFGameRules()->IsInTraining() || TFGameRules()->IsInItemTestingMode() ) //checks to see if the server is a training state or a item testing mode
		return false;

	if ( TFGameRules()->IsInArenaMode() && tf_arena_use_queue.GetBool() ) //checks to see if the server is in the arena mode and if the server is using the queue
		return false;

#if defined( _DEBUG ) || defined( STAGING_ONLY ) //if debug or defined as staging only assuming this means when loading the map
	if ( mp_developer.GetBool() ) //if map developer is true
		return false;
#endif // _DEBUG || STAGING_ONLY

	if ( mp_teams_unbalance_limit.GetInt() <= 0 ) //if the map teams unbalance limit is lessthen or equal to 0
		return false;

	const IMatchGroupDescription *pMatchDesc = GetMatchGroupDescription( TFGameRules()->GetCurrentMatchGroup() ); //NO FUCKING CLUE
	if ( pMatchDesc ) //NO FUCKING CLUE
	{
		return pMatchDesc->m_params.m_bUseAutoBalance;//NO FUCKING CLUE
	}

	// outside of managed matches, we don't normally do any balancing for tournament mode
	if ( TFGameRules()->IsInTournamentMode() )
		return false;

	return ( mp_autoteambalance.GetInt() == 2 ); //returns true if the map autoteambalance is set to 2?????
}

//-----------------------------------------------------------------------------
// Purpose: Checks to see if teams are unbalanced.
// The server may use auto balancing but there are conditions in place that will prevent auto balancing from running like map data dna server state
//-----------------------------------------------------------------------------
bool CTFAutobalance::AreTeamsUnbalanced()
{
	if ( !TFGameRules() ) //if gamerules isnt present the teams are balanced
		return false;

	// don't bother switching teams if the round isn't running
	if ( TFGameRules()->State_Get() != GR_STATE_RND_RUNNING )
		return false;

	if ( mp_teams_unbalance_limit.GetInt() <= 0 ) //if the maps unbalance limit is lessthen or equal to 0 auto balancing will not happen
		return false;

	if ( TFGameRules()->ArePlayersInHell() ) //if there are players in hell then auto balancing will not occure for this cycle
		return false;

	int nDiffBetweenTeams = 0; //if there is no difference in the teams numbers
	m_iLightestTeam = m_iHeaviestTeam = TEAM_INVALID; //sets the lightest and heaviest teams to TEAM_INVALID
	m_nNeeded = 0; //sets the number needed to 0
	//note: the above 3 lines of code appear to run when all the checks conditions fail. This is used to clear out the variables.
	//these normally would be cleared when the reset function is called but since the function doesnt seem to be called all the time
	//it makes sense to clear these variables before assigning them a proper value. The above if statements seem to be the reason this code exists
	//since they dont call the reset function and could possible have these variable containing data inside of them without clearing them.
	//I think the reason they didnt use the reset function to achieve this is because it messes with the asked player list and could cause problems if ran
	

	CMatchInfo *pMatch = GTFGCClientSystem()->GetLiveMatch(); //changes match info to whatever the GetLiveMatch function returns
	if ( pMatch ) //if match
	{
		int nNumTeamRed = pMatch->GetNumActiveMatchPlayersForTeam( TFGameRules()->GetGCTeamForGameTeam( TF_TEAM_RED ) ); //sets the nNumTeamRed variable to the active number of players on that team. Passes the red team color id from the game to the active player function 
		int nNumTeamBlue = pMatch->GetNumActiveMatchPlayersForTeam( TFGameRules()->GetGCTeamForGameTeam( TF_TEAM_BLUE ) ); //sets the nNumTeamBlue variable to the active number of players on that team. Passes the blue team color id from the game rules to the active player function

		m_iLightestTeam = ( nNumTeamRed > nNumTeamBlue ) ? TF_TEAM_BLUE : TF_TEAM_RED; //sets the lightest team to the team with the least number of players
		m_iHeaviestTeam = ( nNumTeamRed > nNumTeamBlue ) ? TF_TEAM_RED : TF_TEAM_BLUE; //sets the heaviest team to the team with the most number of players
		//the above codes seems to be able to switch the teams in the operation based
		//this can cause a problem if the team with the least number of players gains more players then the other team withing that cpu clock cycle,
		//but this is nearly impossible. If it did happen it would probably cause a server crash

		nDiffBetweenTeams = abs( nNumTeamRed - nNumTeamBlue ); //sets the difference between teams to the absolute value of the red team minus the blue team 
	}
	else //seems like this takes affect if there are no teams/more then 2 teams
	{
		int iMostPlayers = 0; //sets most players to 0
		int iLeastPlayers = MAX_PLAYERS + 1; //sets least players to 1 more then the server player cap
		int i = FIRST_GAME_TEAM; //i is the first game team

		for ( CTeam *pTeam = GetGlobalTeam( i ); pTeam != NULL; pTeam = GetGlobalTeam( ++i ) ) //runs the loop for every global team
		{
			int iNumPlayers = pTeam->GetNumPlayers();  //gets the number of players on the team

			if ( iNumPlayers < iLeastPlayers ) //checks to see if the number of players is less then the least number of players
			{
				iLeastPlayers = iNumPlayers; //sets the least player to the number of players
				m_iLightestTeam = i; //sets the lightest team to that team number
			}

			if ( iNumPlayers > iMostPlayers ) //checks to see if the number of players is more then those most players
			{
				iMostPlayers = iNumPlayers; //sets the most players the the number of players
				m_iHeaviestTeam = i; //sets the heaviest team to that team
			}
		}

		nDiffBetweenTeams = ( iMostPlayers - iLeastPlayers ); //saves the difference between the most players and least players to a variable
	}

	if ( nDiffBetweenTeams > mp_teams_unbalance_limit.GetInt() )  //if the difference is greater then the maps limit
	{
		m_nNeeded = ( nDiffBetweenTeams / 2 ); // devide the difference by 2
		return true;
	}

	return false;
}

//-----------------------------------------------------------------------------
// Purpose: It monitors the teams for auto blance and applys a waiting period for the gc to possible send someone and then starts the vvolunteer phase
//-----------------------------------------------------------------------------
void CTFAutobalance::MonitorTeams()
{
	if ( AreTeamsUnbalanced() ) //if the teams are unbalanced
	{
		if ( m_flBalanceTeamsTime < 0.f ) //if the balance time is less then 0
		{
			// trigger a small waiting period to see if the GC sends us someone before we need to balance the teams 
			m_flBalanceTeamsTime = gpGlobals->curtime + tf_autobalance_detected_delay.GetInt();
		}
		else if ( m_flBalanceTeamsTime < gpGlobals->curtime ) //if the wait time is less then the gc time
		{
			if ( IsOkayToBalancePlayers() ) //if its ok to balance players
			{
				UTIL_ClientPrintAll( HUD_PRINTTALK, "#TF_Autobalance_Start", ( m_iHeaviestTeam == TF_TEAM_RED ) ? "#TF_RedTeam_Name" : "#TF_BlueTeam_Name" ); //says that auto balance is starting
				m_iCurrentState = AB_STATE_FIND_VOLUNTEERS; // changes state to finding volunteers
			}
		}
	}
	else
	{
		m_flBalanceTeamsTime = -1.f; //change the wait time to -1
	}
}

//-----------------------------------------------------------------------------
// Purpose: it checks to see if the player has already been asked to auto balance
//-----------------------------------------------------------------------------
bool CTFAutobalance::HaveAlreadyAskedPlayer( CTFPlayer *pTFPlayer ) const
{
	FOR_EACH_VEC( m_vecPlayersAsked, i ) //for each player in players asked
	{
		if ( m_vecPlayersAsked[i].hPlayer == pTFPlayer ) //if the players name is equal to pTFPlayer (the argument needed when calling the function)
			return true;
	}

	return false;
}

//-----------------------------------------------------------------------------
// Purpose:  gets the total score of the team and returns it to the calling operation
//-----------------------------------------------------------------------------
int CTFAutobalance::GetTeamAutoBalanceScore( int nTeam ) const
{
	CMatchInfo *pMatch = GTFGCClientSystem()->GetLiveMatch(); //getting the live match data and saving it to variables
	if ( pMatch && TFGameRules() ) //if the pMatch and tfGameRules exist
	{
		return pMatch->GetTotalSkillRatingForTeam( TFGameRules()->GetGCTeamForGameTeam( nTeam ) ); //this takes the pMatch function(?) and sends it to get the total skill rating for the team and passes the tf game rules get gcteamforgameteam using the team specified when calling the function 
	}

	int nTotalScore = 0; //sets the total score to 0
	CTFPlayerResource *pTFPlayerResource = dynamic_cast<CTFPlayerResource *>( g_pPlayerResource ); //getting the player resources through a dynamic cast
	if ( pTFPlayerResource ) //if the players resource exists
	{
		CTeam *pTeam = GetGlobalTeam( nTeam ); //gets the global team by passing nTeam and saving it to 2 variables
		if ( pTeam ) //if pTeam exists?
		{
			for ( int i = 0; i < pTeam->GetNumPlayers(); i++ ) //for i in the team get the number of players
			{
				CTFPlayer *pTFPlayer = ToTFPlayer( pTeam->GetPlayer( i ) ); //gets the players number
				if ( pTFPlayer ) //if the pTFPlayer exists
				{
					nTotalScore += pTFPlayerResource->GetTotalScore( pTFPlayer->entindex() ); //total team score gets the players total score added to it
				}
			}
		}
	}

	return nTotalScore;
}

//-----------------------------------------------------------------------------
// Purpose: Gets the players auto balance score. If the plater has a match making rating then it will return that, other wise it will use the total score
//-----------------------------------------------------------------------------
int CTFAutobalance::GetPlayerAutoBalanceScore( CTFPlayer *pTFPlayer ) const
{
	if ( !pTFPlayer ) //if pTFPlayer doesnt exist return a 0
		return 0;

	CMatchInfo *pMatch = GTFGCClientSystem()->GetLiveMatch(); //getting the live match data and saving it to variables
	if ( pMatch ) //if pMatch exists
	{
		CSteamID steamID; //Something to do with the steam ID, probably defining the variable
		pTFPlayer->GetSteamID( &steamID ); //grabs the players steam id

		if ( steamID.IsValid() ) //if the steamID given is valid
		{
			const CMatchInfo::PlayerMatchData_t* pPlayerMatchData = pMatch->GetMatchDataForPlayer( steamID ); //gets the match data for the steamId and saves it to different variables
			if ( pPlayerMatchData ) //if the pPlayerMatchData exists
			{
				FixmeMMRatingBackendSwapping(); // Make sure this makes sense with arbitrary skill rating values --
												// e.g. maybe we want a smarter glicko-weighting thing.
				return (int)pPlayerMatchData->unMMSkillRating; //returns the unMMSkillRating of the player
			}
		}
	}

	int nTotalScore = 0; //sets the total score to 0
	CTFPlayerResource *pTFPlayerResource = dynamic_cast<CTFPlayerResource *>( g_pPlayerResource ); //gets the players resources through a dynamic castd
	if ( pTFPlayerResource ) //if pTFlayerResource exists
	{
		nTotalScore = pTFPlayerResource->GetTotalScore( pTFPlayer->entindex() ); //The total score is set to the players total score
	}

	return nTotalScore;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
CTFPlayer *CTFAutobalance::FindPlayerToAsk()
{
	CTFPlayer *pRetVal = NULL; //setting something to null

	CUtlVector< CTFPlayer* > vecCandiates;
	CTeam *pTeam = GetGlobalTeam( m_iHeaviestTeam );
	if ( pTeam )
	{
		// loop through and get a list of possible candidates
		for ( int i = 0; i < pTeam->GetNumPlayers(); i++ )
		{
			CTFPlayer *pTFPlayer = ToTFPlayer( pTeam->GetPlayer( i ) );
			if ( pTFPlayer && !HaveAlreadyAskedPlayer( pTFPlayer ) && pTFPlayer->CanBeAutobalanced() )
			{
				vecCandiates.AddToTail( pTFPlayer );
			}
		}
	}

	// no need to go any further if there's only one candidate
	if ( vecCandiates.Count() == 1 )
	{
		pRetVal = vecCandiates[0];
	}
	else if ( vecCandiates.Count() > 1 )
	{
		int nTotalDiff = abs( GetTeamAutoBalanceScore( m_iHeaviestTeam ) - GetTeamAutoBalanceScore( m_iLightestTeam ) );
		int nAverageNeeded = ( nTotalDiff / 2 ) / m_nNeeded;

		// now look a player on the heaviest team with skillrating closest to that average
		int nClosest = INT_MAX;
		FOR_EACH_VEC( vecCandiates, iIndex )
		{
			int nDiff = abs( nAverageNeeded - GetPlayerAutoBalanceScore( vecCandiates[iIndex] ) );
			if ( nDiff < nClosest )
			{
				nClosest = nDiff;
				pRetVal = vecCandiates[iIndex];
			}
		}
	}

	return pRetVal;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CTFAutobalance::FindVolunteers()
{
	// keep track of the state of things, this will also update our counts if more players drop from the server
	if ( !AreTeamsUnbalanced() || !IsOkayToBalancePlayers() )
	{
		Reset();
		return;
	}

	int nPendingReplies = 0;
	int nRepliedNo = 0;

	FOR_EACH_VEC( m_vecPlayersAsked, i )
	{
		// if the player is valid
		if ( m_vecPlayersAsked[i].hPlayer.Get() )
		{
			switch ( m_vecPlayersAsked[i].eState )
			{
			case AB_VOLUNTEER_STATE_ASKED:
				if ( m_vecPlayersAsked[i].flQueryExpireTime < gpGlobals->curtime )
				{
					// they've timed out the request period without replying
					m_vecPlayersAsked[i].eState = AB_VOLUNTEER_STATE_NO;
					nRepliedNo++;
				}
				else
				{
					nPendingReplies++;
				}
				break;
			case AB_VOLUNTEER_STATE_NO:
				nRepliedNo++;
				break;
			default:
				break;
			}
		}
	}

	int nNumToAsk = ( m_nNeeded * 2 );

	// do we need to ask for more volunteers?
	if ( nPendingReplies < nNumToAsk )
	{
		int nNumNeeded = nNumToAsk - nPendingReplies;
		int nNumAsked = 0;

		while ( nNumAsked < nNumNeeded )
		{
			CTFPlayer *pTFPlayer = FindPlayerToAsk();
			if ( pTFPlayer )
			{
				int iIndex = m_vecPlayersAsked.AddToTail();
				m_vecPlayersAsked[iIndex].hPlayer = pTFPlayer;
				m_vecPlayersAsked[iIndex].eState = AB_VOLUNTEER_STATE_ASKED;
				m_vecPlayersAsked[iIndex].flQueryExpireTime = gpGlobals->curtime + tf_autobalance_query_lifetime.GetInt() + 3; // add 3 seconds to allow for travel time to/from the client

				CSingleUserRecipientFilter filter( pTFPlayer );
				filter.MakeReliable();
				UserMessageBegin( filter, "AutoBalanceVolunteer" );
				MessageEnd();

				nNumAsked++;
				nPendingReplies++;
			}
			else
			{
				// we couldn't find anyone else to ask
				if ( nPendingReplies <= 0 )
				{
					// we're not waiting on anyone else to reply....so we should just reset
					Reset();
				}

				return;
			}
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CTFAutobalance::FrameUpdatePostEntityThink()
{
	bool bActive = ShouldBeActive();
	if ( !bActive )
	{
		Reset();
		return;
	}

	switch ( m_iCurrentState )
	{
	case AB_STATE_INACTIVE:
		// we should be active if we've made it this far
		m_iCurrentState = AB_STATE_MONITOR;
		break;
	case AB_STATE_MONITOR:
		MonitorTeams();
		break;
	case AB_STATE_FIND_VOLUNTEERS:
		FindVolunteers();
		break;
	default:
		break;
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
bool CTFAutobalance::IsOkayToBalancePlayers()
{
	if ( GTFGCClientSystem()->GetLiveMatch() && !GTFGCClientSystem()->CanChangeMatchPlayerTeams() ) 
		return false;

	return true;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CTFAutobalance::ReplyReceived( CTFPlayer *pTFPlayer, bool bResponse )
{
	if ( m_iCurrentState != AB_STATE_FIND_VOLUNTEERS )
		return;

	if ( !AreTeamsUnbalanced() || !IsOkayToBalancePlayers() )
	{
		Reset();
		return;
	}

	FOR_EACH_VEC( m_vecPlayersAsked, i )
	{
		// is this a player we asked?
		if ( m_vecPlayersAsked[i].hPlayer == pTFPlayer )
		{
			m_vecPlayersAsked[i].eState = bResponse ? AB_VOLUNTEER_STATE_YES : AB_VOLUNTEER_STATE_NO;
			if ( bResponse  && pTFPlayer->CanBeAutobalanced() )
			{
				pTFPlayer->ChangeTeam( m_iLightestTeam, false, false, true );
				pTFPlayer->ForceRespawn();
				pTFPlayer->SetLastAutobalanceTime( gpGlobals->curtime );

				CMatchInfo *pMatch = GTFGCClientSystem()->GetLiveMatch();
				if ( pMatch )
				{
					CSteamID steamID;
					pTFPlayer->GetSteamID( &steamID );

					// We're going to give the switching player a bonus pool of XP. This should encourage
					// them to keep playing to earn what's in the pool, rather than just quit after getting
					// a big payout
					if ( !pMatch->BSentResult() )
					{
						pMatch->GiveXPBonus( steamID, CMsgTFXPSource_XPSourceType_SOURCE_AUTOBALANCE_BONUS, 1, tf_autobalance_xp_bonus.GetInt() );
					}

					GTFGCClientSystem()->ChangeMatchPlayerTeam( steamID, TFGameRules()->GetGCTeamForGameTeam( m_iLightestTeam ) );
				}
			}
		}
	}
}

CTFAutobalance gTFAutobalance;
CTFAutobalance *TFAutoBalance(){ return &gTFAutobalance; }
