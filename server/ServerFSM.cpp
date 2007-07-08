#include "ServerFSM.h"

#include "SaveLoad.h"
#include "ServerApp.h"
#include "../Empire/Empire.h"
#include "../network/ServerNetworking.h"
#include "../network/Message.h"
#include "../util/Directories.h"
#include "../util/OrderSet.h"

#include <GG/Font.h>

#include <iostream>


namespace {
    const bool TRACE_EXECUTION = false;

    void RebuildSaveGameEmpireData(std::vector<SaveGameEmpireData>& save_game_empire_data, const std::string& save_game_filename)
    {
        save_game_empire_data.clear();
        std::vector<PlayerSaveGameData> player_save_game_data;
        int current_turn;
        Universe universe;
        LoadGame((GetLocalDir() / "save" / save_game_filename).native_file_string().c_str(),
                 current_turn, player_save_game_data, universe);
        for (unsigned int i = 0; i < player_save_game_data.size(); ++i) {
            SaveGameEmpireData data;
            Empire* empire = player_save_game_data[i].m_empire;
            data.m_id = empire->EmpireID();
            data.m_name = empire->Name();
            data.m_player_name = empire->PlayerName();
            data.m_color = empire->Color();
            save_game_empire_data.push_back(data);
            delete empire;
        }
    }
}

////////////////////////////////////////////////////////////
// MessageEventBase
////////////////////////////////////////////////////////////
Disconnection::Disconnection(PlayerConnectionPtr& player_connection) :
    m_player_connection(player_connection)
{}

////////////////////////////////////////////////////////////
// MessageEventBase
////////////////////////////////////////////////////////////
MessageEventBase::MessageEventBase(const Message& message, PlayerConnectionPtr& player_connection) :
    m_message(message),
    m_player_connection(player_connection)
{}


////////////////////////////////////////////////////////////
// ServerFSM
////////////////////////////////////////////////////////////
ServerFSM::ServerFSM(ServerApp &server) :
    m_server(server)
{}

void ServerFSM::unconsumed_event(const boost::statechart::event_base &event)
{ Logger().errorStream() << "ServerFSM : An illegal message or other event was passed to the ServerFSM.  It is being ignored."; }

ServerApp& ServerFSM::Server()
{ return m_server; }

void ServerFSM::HandleNonLobbyDisconnection(const Disconnection& d)
{
    PlayerConnectionPtr& player_connection = d.m_player_connection;

    int id = player_connection->ID();
    // this will not usually happen, since the host client process usually owns the server process, and will usually take it down if it fails
    if (player_connection->Host()) {
        // if the host dies, there's really nothing else we can do -- the game's over
        std::string message = player_connection->PlayerName();
        for (ServerNetworking::const_iterator it = m_server.m_networking.established_begin(); it != m_server.m_networking.established_end(); ++it) {
            if ((*it)->ID() != id) {
                (*it)->SendMessage(PlayerDisconnectedMessage((*it)->ID(), message));
                (*it)->SendMessage(EndGameMessage(-1, (*it)->ID()));
            }
        }
        Logger().debugStream() << "ServerFSM::HandleNonLobbyDisconnection : Host player disconnected; server terminating.";
        m_server.m_networking.DisconnectAll();
        m_server.Exit(1);
    } else if (m_server.m_losers.find(id) == m_server.m_losers.end()) { // player abnormally disconnected during a regular game
        Logger().debugStream() << "ServerFSM::HandleNonLobbyDisconnection : Lost connection to player #" << boost::lexical_cast<std::string>(id) 
                               << ", named \"" << player_connection->PlayerName() << "\"; server terminating.";
        std::string message = player_connection->PlayerName();
        for (ServerNetworking::const_iterator it = m_server.m_networking.established_begin(); it != m_server.m_networking.established_end(); ++it) {
            if ((*it)->ID() != id) {
                (*it)->SendMessage(PlayerDisconnectedMessage((*it)->ID(), message));
                // in the future we may find a way to recover from this, but for now we will immediately send a game ending message as well
                (*it)->SendMessage(EndGameMessage(-1, (*it)->ID()));
            }
        }
    }

    // independently of everything else, if there are no humans left, it's time to terminate
    if (m_server.m_networking.empty() || m_server.m_ai_clients.size() == m_server.m_networking.size()) {
        Logger().debugStream() << "ServerFSM::HandleNonLobbyDisconnection : All human players disconnected; server terminating.";
        m_server.m_networking.DisconnectAll();
        m_server.Exit(1);
    }
}


////////////////////////////////////////////////////////////
// Idle
////////////////////////////////////////////////////////////
Idle::Idle() :
    Base()
{ if (TRACE_EXECUTION) Logger().debugStream() << "(ServerFSM) Idle"; }

Idle::~Idle()
{ if (TRACE_EXECUTION) Logger().debugStream() << "(ServerFSM) ~Idle"; }

boost::statechart::result Idle::react(const HostMPGame& msg)
{
    if (TRACE_EXECUTION) Logger().debugStream() << "(ServerFSM) Idle.HostMPGame";
    ServerApp& server = context<ServerFSM>().Server();
    const Message& message = msg.m_message;
    PlayerConnectionPtr& player_connection = msg.m_player_connection;

    std::string host_player_name = message.Text();
    int player_id = Networking::HOST_PLAYER_ID;
    player_connection->EstablishPlayer(player_id, host_player_name, true);
    player_connection->SendMessage(HostMPAckMessage(player_id));
    player_connection->SendMessage(JoinAckMessage(player_id));
    server.m_single_player_game = false;

    return transit<MPLobby>();
}

boost::statechart::result Idle::react(const HostSPGame& msg)
{
    if (TRACE_EXECUTION) Logger().debugStream() << "(ServerFSM) Idle.HostSPGame";
    ServerApp& server = context<ServerFSM>().Server();
    const Message& message = msg.m_message;
    PlayerConnectionPtr& player_connection = msg.m_player_connection;

    boost::shared_ptr<SinglePlayerSetupData> setup_data(new SinglePlayerSetupData);
    ExtractMessageData(message, *setup_data);

    int player_id = Networking::HOST_PLAYER_ID;
    player_connection->EstablishPlayer(player_id, setup_data->m_host_player_name, true);
    player_connection->SendMessage(HostSPAckMessage(player_id));
    player_connection->SendMessage(JoinAckMessage(player_id));
    server.m_single_player_game = true;
    context<ServerFSM>().m_setup_data = setup_data;

    return transit<WaitingForSPGameJoiners>();
}


////////////////////////////////////////////////////////////
// MPLobby
////////////////////////////////////////////////////////////
MPLobby::MPLobby(my_context c) :
    Base(c),
    m_lobby_data (new MultiplayerLobbyData(true))
{
    if (TRACE_EXECUTION) Logger().debugStream() << "(ServerFSM) MPLobby";
    ServerApp& server = context<ServerFSM>().Server();
    int player_id = Networking::HOST_PLAYER_ID;
    const PlayerConnectionPtr& player_connection = *server.m_networking.GetPlayer(player_id);
    m_lobby_data->m_players.push_back(PlayerSetupData());
    m_lobby_data->m_players.back().m_player_id = player_id;
    m_lobby_data->m_players.back().m_player_name = player_connection->PlayerName();
    m_lobby_data->m_players.back().m_empire_color = EmpireColors().at(0);
    server.m_networking.SendMessage(ServerLobbyUpdateMessage(player_id, *m_lobby_data));
}

MPLobby::~MPLobby()
{ if (TRACE_EXECUTION) Logger().debugStream() << "(ServerFSM) ~MPLobby"; }

boost::statechart::result MPLobby::react(const Disconnection& d)
{
    if (TRACE_EXECUTION) Logger().debugStream() << "(ServerFSM) MPLobby.Disconnection";
    ServerApp& server = context<ServerFSM>().Server();
    PlayerConnectionPtr& player_connection = d.m_player_connection;

    int id = player_connection->ID();
    // this will not usually happen, since the host process usually owns the server process, and will usually take it down if it fails
    if (player_connection->Host()) {
        for (ServerNetworking::const_iterator it = server.m_networking.established_begin(); it != server.m_networking.established_end(); ++it) {
            if ((*it)->ID() != id) {
                (*it)->SendMessage(ServerLobbyHostAbortMessage((*it)->ID()));
                server.m_networking.Disconnect((*it)->ID());
            }
        }
        server.m_networking.Disconnect(id);
        Logger().debugStream() << "MPLobby.Disconnection : Host player disconnected; server terminating.";
        server.m_networking.DisconnectAll();
        server.Exit(1);
    } else {
        unsigned int i = 0;
        for (ServerNetworking::const_iterator it = server.m_networking.established_begin(); it != server.m_networking.established_end(); ++it, ++i) {
            if ((*it)->ID() == id) {
                if (i < m_lobby_data->m_players.size())
                    m_lobby_data->m_players.erase(m_lobby_data->m_players.begin() + i); // remove the exiting player's PlayerSetupData struct
            } else {
                (*it)->SendMessage(ServerLobbyExitMessage(id, (*it)->ID()));
            }
        }
        server.m_networking.Disconnect(id);
    }

    // independently of everything else, if there are no humans left, it's time to terminate
    if (server.m_networking.empty() || server.m_ai_clients.size() == server.m_networking.size()) {
        Logger().debugStream() << "MPLobby.Disconnection : All human players disconnected; server terminating.";
        server.m_networking.DisconnectAll();
        server.Exit(1);
    }

    return discard_event();
}

boost::statechart::result MPLobby::react(const JoinGame& msg)
{
    if (TRACE_EXECUTION) Logger().debugStream() << "(ServerFSM) MPLobby.JoinGame";
    ServerApp& server = context<ServerFSM>().Server();
    const Message& message = msg.m_message;
    PlayerConnectionPtr& player_connection = msg.m_player_connection;

    std::string player_name = message.Text();
    int player_id = server.m_networking.GreatestPlayerID() + 1;
    player_connection->EstablishPlayer(player_id, player_name, false);
    player_connection->SendMessage(JoinAckMessage(player_id));
    m_lobby_data->m_players.push_back(PlayerSetupData());
    m_lobby_data->m_players.back().m_player_id = player_id;
    m_lobby_data->m_players.back().m_player_name = player_name;
    m_lobby_data->m_players.back().m_empire_color = EmpireColors().at(0);
    for (ServerNetworking::const_iterator it = server.m_networking.established_begin(); it != server.m_networking.established_end(); ++it) {
        (*it)->SendMessage(ServerLobbyUpdateMessage((*it)->ID(), *m_lobby_data));
    }

    return discard_event();
}

boost::statechart::result MPLobby::react(const LobbyUpdate& msg)
{
    if (TRACE_EXECUTION) Logger().debugStream() << "(ServerFSM) MPLobby.LobbyUpdate";
    ServerApp& server = context<ServerFSM>().Server();
    const Message& message = msg.m_message;

    MultiplayerLobbyData incoming_lobby_data(new MultiplayerLobbyData);
    ExtractMessageData(message, incoming_lobby_data);

    // NOTE: The client is only allowed to update certain of these, so those are the only ones we'll copy into m_lobby_data.
    m_lobby_data->m_new_game = incoming_lobby_data.m_new_game;
    m_lobby_data->m_size = incoming_lobby_data.m_size;
    m_lobby_data->m_shape = incoming_lobby_data.m_shape;
    m_lobby_data->m_age = incoming_lobby_data.m_age;
    m_lobby_data->m_starlane_freq = incoming_lobby_data.m_starlane_freq;
    m_lobby_data->m_planet_density = incoming_lobby_data.m_planet_density;
    m_lobby_data->m_specials_freq = incoming_lobby_data.m_specials_freq;

    bool new_save_file_selected = false;
    if (incoming_lobby_data.m_save_file_index != m_lobby_data->m_save_file_index &&
        0 <= incoming_lobby_data.m_save_file_index && incoming_lobby_data.m_save_file_index < static_cast<int>(m_lobby_data->m_save_games.size())) {
        m_lobby_data->m_save_file_index = incoming_lobby_data.m_save_file_index;
        RebuildSaveGameEmpireData(m_lobby_data->m_save_game_empire_data, m_lobby_data->m_save_games[m_lobby_data->m_save_file_index]);
        // reset the current choice of empire for each player, since the new save game's empires may not have the same IDs
        for (unsigned int i = 0; i < m_lobby_data->m_players.size(); ++i) {
            m_lobby_data->m_players[i].m_save_game_empire_id = -1;
        }
        new_save_file_selected = true;
    }
    m_lobby_data->m_players = incoming_lobby_data.m_players;

    for (ServerNetworking::const_iterator it = server.m_networking.established_begin(); it != server.m_networking.established_end(); ++it) {
        if ((*it)->ID() != message.SendingPlayer() || new_save_file_selected)
            (*it)->SendMessage(ServerLobbyUpdateMessage((*it)->ID(), *m_lobby_data));
    }

    return discard_event();
}

boost::statechart::result MPLobby::react(const LobbyChat& msg)
{
    if (TRACE_EXECUTION) Logger().debugStream() << "(ServerFSM) MPLobby.LobbyChat";
    ServerApp& server = context<ServerFSM>().Server();
    const Message& message = msg.m_message;

    if (message.ReceivingPlayer() == -1) { // the receiver is everyone (except the sender)
        for (ServerNetworking::const_iterator it = server.m_networking.established_begin(); it != server.m_networking.established_end(); ++it) {
            if ((*it)->ID() != message.SendingPlayer())
                (*it)->SendMessage(ServerLobbyChatMessage(message.SendingPlayer(), (*it)->ID(), message.Text()));
        }
    } else {
        server.m_networking.SendMessage(ServerLobbyChatMessage(message.SendingPlayer(), message.ReceivingPlayer(), message.Text()));
    }

    return discard_event();
}

boost::statechart::result MPLobby::react(const LobbyHostAbort& msg)
{
    if (TRACE_EXECUTION) Logger().debugStream() << "(ServerFSM) MPLobby.LobbyHostAbort";
    ServerApp& server = context<ServerFSM>().Server();
    const Message& message = msg.m_message;

    for (ServerNetworking::const_iterator it = server.m_networking.established_begin(); it != server.m_networking.established_end(); ++it) {
        if ((*it)->ID() != message.SendingPlayer()) {
            (*it)->SendMessage(ServerLobbyHostAbortMessage((*it)->ID()));
            server.m_networking.Disconnect((*it)->ID());
        }
    }

    return discard_event();
}

boost::statechart::result MPLobby::react(const LobbyNonHostExit& msg)
{
    if (TRACE_EXECUTION) Logger().debugStream() << "(ServerFSM) MPLobby.LobbyNonHostExit";
    ServerApp& server = context<ServerFSM>().Server();
    const Message& message = msg.m_message;

    unsigned int i = 0;
    for (ServerNetworking::const_iterator it = server.m_networking.established_begin(); it != server.m_networking.established_end(); ++it, ++i) {
        if ((*it)->ID() == message.SendingPlayer()) {
            if (i < m_lobby_data->m_players.size())
                m_lobby_data->m_players.erase(m_lobby_data->m_players.begin() + i); // remove the exiting player's PlayerSetupData struct
        } else {
            (*it)->SendMessage(ServerLobbyExitMessage(message.SendingPlayer(), (*it)->ID()));
        }
    }
    server.m_networking.Disconnect(message.SendingPlayer());

    return discard_event();
}

boost::statechart::result MPLobby::react(const StartMPGame& msg)
{
    if (TRACE_EXECUTION) Logger().debugStream() << "(ServerFSM) MPLobby.StartMPGame";
    ServerApp& server = context<ServerFSM>().Server();
    const Message& message = msg.m_message;
    PlayerConnectionPtr& player_connection = msg.m_player_connection;

    if (player_connection->Host()) {
        if (m_lobby_data->m_new_game) {
            int expected_players = server.m_networking.size() + m_lobby_data->m_AIs.size();
            for (ServerNetworking::const_iterator it = server.m_networking.established_begin(); it != server.m_networking.established_end(); ++it) {
                if ((*it)->ID() != Networking::HOST_PLAYER_ID)
                    (*it)->SendMessage(Message(Message::GAME_START, -1, (*it)->ID(), Message::CLIENT_LOBBY_MODULE, ""));
            }
            if (expected_players == static_cast<int>(server.m_networking.size())) {
                server.NewGameInit(m_lobby_data);
                return discard_event();
            }
        } else {
            LoadGame((GetLocalDir() / "save" / m_lobby_data->m_save_games[m_lobby_data->m_save_file_index]).native_file_string(),
                     server.m_current_turn, m_player_save_game_data, GetUniverse());
            int expected_players = m_player_save_game_data.size();
            for (unsigned int i = 0; i < m_player_save_game_data.size(); ++i) {
                for (unsigned int j = 0; j < m_lobby_data->m_players.size(); ++j) {
                    assert(m_player_save_game_data[i].m_empire);
                    if (m_lobby_data->m_players[j].m_save_game_empire_id == m_player_save_game_data[i].m_empire->EmpireID()) {
                        ServerNetworking::const_iterator player_it = server.m_networking.GetPlayer(m_lobby_data->m_players[j].m_player_id);
                        assert(player_it != server.m_networking.established_end());
                        m_player_save_game_data[i].m_name = (*player_it)->PlayerName();
                        m_player_save_game_data[i].m_empire->SetPlayerName((*player_it)->PlayerName());
                    }
                }
                Empires().InsertEmpire(m_player_save_game_data[i].m_empire);
            }

            for (ServerNetworking::const_iterator it = server.m_networking.established_begin(); it != server.m_networking.established_end(); ++it) {
                if ((*it)->ID() != player_connection->ID())
                    (*it)->SendMessage(Message(Message::GAME_START, -1, (*it)->ID(), Message::CLIENT_LOBBY_MODULE, ""));
            }

            int AI_clients = expected_players - server.m_networking.size();
            if (!AI_clients) {
                server.LoadGameInit(m_lobby_data, m_player_save_game_data);
                return discard_event();
            }
        }
    } else {
        Logger().errorStream() << "(ServerFSM) MPLobby.StartMPGame : Player #" << message.SendingPlayer()
                               << " attempted to initiate a game load, but is not the host.  Terminating connection.";
        server.m_networking.Disconnect(player_connection);
        return discard_event();
    }

    context<ServerFSM>().m_lobby_data = m_lobby_data;
    std::copy(m_player_save_game_data.begin(), m_player_save_game_data.end(), std::back_inserter(context<ServerFSM>().m_player_save_game_data));

    return transit<WaitingForMPGameJoiners>();
}


////////////////////////////////////////////////////////////
// WaitingForSPGameJoiners
////////////////////////////////////////////////////////////
WaitingForSPGameJoiners::WaitingForSPGameJoiners(my_context c) :
    Base(c),
    m_setup_data(context<ServerFSM>().m_setup_data),
    m_num_expected_players(0)
{
    if (TRACE_EXECUTION) Logger().debugStream() << "(ServerFSM) WaitingForSPGameJoiners";

    context<ServerFSM>().m_setup_data.reset();
    ServerApp& server = context<ServerFSM>().Server();

    if (m_setup_data->m_new_game) {
        m_num_expected_players = m_setup_data->m_AIs + 1;
        server.CreateAIClients(std::vector<PlayerSetupData>(m_setup_data->m_AIs), m_expected_ai_player_names);
    } else {
        LoadGame(m_setup_data->m_filename, server.m_current_turn, m_player_save_game_data, GetUniverse());
        assert(!m_player_save_game_data.empty());
        m_num_expected_players = m_player_save_game_data.size();
        server.CreateAIClients(std::vector<PlayerSetupData>(m_num_expected_players - 1), m_expected_ai_player_names);
    }
}

WaitingForSPGameJoiners::~WaitingForSPGameJoiners()
{ if (TRACE_EXECUTION) Logger().debugStream() << "(ServerFSM) ~WaitingForSPGameJoiners"; }

boost::statechart::result WaitingForSPGameJoiners::react(const JoinGame& msg)
{
    if (TRACE_EXECUTION) Logger().debugStream() << "(ServerFSM) WaitingForSPGameJoiners.JoinGame";
    ServerApp& server = context<ServerFSM>().Server();
    const Message& message = msg.m_message;
    PlayerConnectionPtr& player_connection = msg.m_player_connection;

    std::string player_name = message.Text();
    int player_id = server.m_networking.GreatestPlayerID() + 1;
    std::set<std::string>::iterator it = m_expected_ai_player_names.find(player_name);
    if (it != m_expected_ai_player_names.end()) { // incoming AI player connection
        // let the networking system know what socket this player is on
        player_connection->EstablishPlayer(player_id, player_name, false);
        player_connection->SendMessage(JoinAckMessage(player_id));
        m_expected_ai_player_names.erase(player_name); // only allow one connection per AI
        server.m_ai_IDs.insert(player_id);
    } else { // non-AI player connection
        if (static_cast<int>(m_expected_ai_player_names.size() + server.m_networking.NumPlayers()) < m_num_expected_players) {
            player_connection->EstablishPlayer(player_id, player_name, false);
            player_connection->SendMessage(JoinAckMessage(player_id));
        } else {
            Logger().errorStream() << "WaitingForSPGameJoiners.JoinGame : A human player attempted to join "
                "the game but there was not enough room.  Terminating connection.";
            server.m_networking.Disconnect(player_connection);
        }
    }

    if (static_cast<int>(server.m_networking.NumPlayers()) == m_num_expected_players) {
        if (m_setup_data->m_new_game)
            server.NewGameInit(m_setup_data);
        else
            server.LoadGameInit(m_player_save_game_data);
        return transit<PlayingGame>();
    }

    return discard_event();
}


////////////////////////////////////////////////////////////
// WaitingForMPGameJoiners
////////////////////////////////////////////////////////////
WaitingForMPGameJoiners::WaitingForMPGameJoiners(my_context c) :
    Base(c),
    m_lobby_data(context<ServerFSM>().m_lobby_data),
    m_player_save_game_data(context<ServerFSM>().m_player_save_game_data.begin(), context<ServerFSM>().m_player_save_game_data.end()),
    m_num_expected_players(0)
{
    if (TRACE_EXECUTION) Logger().debugStream() << "(ServerFSM) WaitingForMPGameJoiners";
    context<ServerFSM>().m_lobby_data.reset();
    context<ServerFSM>().m_player_save_game_data.clear();
    ServerApp& server = context<ServerFSM>().Server();
    if (m_lobby_data->m_new_game) {
        m_num_expected_players = server.m_networking.size() + m_lobby_data->m_AIs.size();
        server.CreateAIClients(m_lobby_data->m_AIs, m_expected_ai_player_names);
    } else {
        m_num_expected_players = m_player_save_game_data.size();
        int AI_clients = m_num_expected_players - server.m_networking.size();
        server.CreateAIClients(std::vector<PlayerSetupData>(AI_clients), m_expected_ai_player_names);
    }
}

WaitingForMPGameJoiners::~WaitingForMPGameJoiners()
{ if (TRACE_EXECUTION) Logger().debugStream() << "(ServerFSM) ~WaitingForMPGameJoiners"; }

boost::statechart::result WaitingForMPGameJoiners::react(const JoinGame& msg)
{
    if (TRACE_EXECUTION) Logger().debugStream() << "(ServerFSM) WaitingForMPGameJoiners.JoinGame";
    ServerApp& server = context<ServerFSM>().Server();
    const Message& message = msg.m_message;
    PlayerConnectionPtr& player_connection = msg.m_player_connection;

    std::string player_name = message.Text();
    int player_id = server.m_networking.GreatestPlayerID() + 1;
    std::set<std::string>::iterator it = m_expected_ai_player_names.find(player_name);
    if (it != m_expected_ai_player_names.end()) { // incoming AI player connection
        // let the networking system know what socket this player is on
        player_connection->EstablishPlayer(player_id, player_name, false);
        player_connection->SendMessage(JoinAckMessage(player_id));
        m_expected_ai_player_names.erase(player_name); // only allow one connection per AI
        server.m_ai_IDs.insert(player_id);
    } else { // non-AI player connection
        if (static_cast<int>(m_expected_ai_player_names.size() + server.m_networking.NumPlayers()) < m_num_expected_players) {
            player_connection->EstablishPlayer(player_id, player_name, false);
            player_connection->SendMessage(JoinAckMessage(player_id));
        } else {
            Logger().errorStream() << "WaitingForMPGameJoiners.JoinGame : A human player attempted to join "
                "the game but there was not enough room.  Terminating connection.";
            server.m_networking.Disconnect(player_connection);
        }
    }

    if (static_cast<int>(server.m_networking.NumPlayers()) == m_num_expected_players) {
        if (m_player_save_game_data.empty())
            server.NewGameInit(m_lobby_data);
        else
            server.LoadGameInit(m_lobby_data, m_player_save_game_data);
        return transit<PlayingGame>();
    }

    return discard_event();
}


////////////////////////////////////////////////////////////
// PlayingGame
////////////////////////////////////////////////////////////
PlayingGame::PlayingGame() :
    Base()
{ if (TRACE_EXECUTION) Logger().debugStream() << "(ServerFSM) PlayingGame"; }

PlayingGame::~PlayingGame()
{ if (TRACE_EXECUTION) Logger().debugStream() << "(ServerFSM) ~PlayingGame"; }


////////////////////////////////////////////////////////////
// WaitingForTurnEnd
////////////////////////////////////////////////////////////
WaitingForTurnEnd::WaitingForTurnEnd() :
    Base()
{ if (TRACE_EXECUTION) Logger().debugStream() << "(ServerFSM) WaitingForTurnEnd"; }

WaitingForTurnEnd::~WaitingForTurnEnd()
{ if (TRACE_EXECUTION) Logger().debugStream() << "(ServerFSM) ~WaitingForTurnEnd"; }

boost::statechart::result WaitingForTurnEnd::react(const LoadSPGame& msg)
{
    if (TRACE_EXECUTION) Logger().debugStream() << "(ServerFSM) WaitingForTurnEnd.LoadSPGame";
    ServerApp& server = context<ServerFSM>().Server();
    const Message& message = msg.m_message;
    PlayerConnectionPtr& player_connection = msg.m_player_connection;

    if (player_connection->Host()) {
        Empires().RemoveAllEmpires();
        server.m_single_player_game = true;
        boost::shared_ptr<SinglePlayerSetupData> setup_data(new SinglePlayerSetupData);
        setup_data->m_new_game = false;
        setup_data->m_filename = message.Text();
        context<ServerFSM>().m_setup_data = setup_data;
        return transit<WaitingForSPGameJoiners>();
    } else {
        Logger().errorStream() << "WaitingForTurnEnd.LoadSPGame : Player #" << message.SendingPlayer()
                               << " attempted to initiate a game save, but is not the host. Terminating connection.";
        server.m_networking.Disconnect(player_connection);
    }

    return discard_event();
}

boost::statechart::result WaitingForTurnEnd::react(const TurnOrders& msg)
{
    if (TRACE_EXECUTION) Logger().debugStream() << "(ServerFSM) WaitingForTurnEnd.TurnOrders";
    ServerApp& server = context<ServerFSM>().Server();
    const Message& message = msg.m_message;

    OrderSet* order_set = new OrderSet;
    ExtractMessageData(message, *order_set);

    // check order validity -- all orders must originate from this empire in order to be considered valid
    Empire* empire = server.GetPlayerEmpire(message.SendingPlayer());
    assert(empire);
    for (OrderSet::const_iterator it = order_set->begin(); it != order_set->end(); ++it) {
        Order* order = it->second;
        assert(order);
        if (empire->EmpireID() != order->EmpireID()) {
            throw std::runtime_error(
                "WaitingForTurnEnd.TurnOrders : Player \"" + empire->PlayerName() +
                "\" attempted to issue an order for player \"" +
                Empires().Lookup(order->EmpireID())->PlayerName() + "\"!  Terminating...");
        }
    }

    server.m_networking.SendMessage(TurnProgressMessage(message.SendingPlayer(), Message::WAITING_FOR_PLAYERS, -1));

    Logger().debugStream() << "WaitingForTurnEnd.TurnOrders : Received orders from player " << message.SendingPlayer();

    server.SetEmpireTurnOrders(server.GetPlayerEmpire(message.SendingPlayer())->EmpireID(), order_set);

    if (server.AllOrdersReceived()) {
        Logger().debugStream() << "WaitingForTurnEnd.TurnOrders : All orders received.  Processing turn....";
        server.ProcessTurns();
        return transit<WaitingForTurnEnd>();
    }

    return discard_event();
}

boost::statechart::result WaitingForTurnEnd::react(const RequestObjectID& msg)
{
    if (TRACE_EXECUTION) Logger().debugStream() << "(ServerFSM) WaitingForTurnEnd.RequestObjectID";
    context<ServerFSM>().Server().m_networking.SendMessage(DispatchObjectIDMessage(msg.m_message.SendingPlayer(), GetUniverse().GenerateObjectID()));
    return discard_event();
}

boost::statechart::result WaitingForTurnEnd::react(const PlayerChat& msg)
{
    if (TRACE_EXECUTION) Logger().debugStream() << "(ServerFSM) WaitingForTurnEnd.PlayerChat";
    ServerApp& server = context<ServerFSM>().Server();
    const Message& message = msg.m_message;

    std::string text = message.Text();

    // If there's a colon in the message, treat all tokens before the colon as player names.  if there are tokens before
    // the colon, but at least one of them *is not* a valid player names, assume there has been a typo, and don't send
    // the message at all, since we can't decipher which parts are message and which parts are names
    std::string::size_type colon_position = text.find(':');
    // Note that target_player_names.empty() implies that all players should be sent the message; otherwise, only the
    // indicated players will receive the message.
    std::set<std::string> target_player_names;
    if (colon_position != std::string::npos) {
        std::vector<std::string> tokens = Tokenize(text.substr(0, colon_position));
        for (unsigned int i = 0; i < tokens.size(); ++i) {
            bool token_is_name = false;
            for (ServerNetworking::const_iterator it = server.m_networking.established_begin(); it != server.m_networking.established_end(); ++it) {
                if (tokens[i] == (*it)->PlayerName()) {
                    token_is_name = true;
                    break;
                }
            }
            if (token_is_name)
                target_player_names.insert(tokens[i]);
            else
                return discard_event();
        }
    }
    if (!target_player_names.empty()) {
        text = text.substr(colon_position + 1);
        if (text == "")
            return discard_event();
    }
    Empire* sender_empire = server.GetPlayerEmpire(message.SendingPlayer());
    std::string final_text = RgbaTag(Empires().Lookup(sender_empire->EmpireID())->Color()) + (*server.m_networking.GetPlayer(message.SendingPlayer()))->PlayerName() +
        (target_player_names.empty() ? ": " : " (whisper):") + text + "</rgba>\n"; // TODO: "whisper" should be a translated string
    for (ServerNetworking::const_iterator it = server.m_networking.established_begin(); it != server.m_networking.established_end(); ++it) {
        if (target_player_names.empty() || target_player_names.find((*it)->PlayerName()) != target_player_names.end())
            (*it)->SendMessage(ChatMessage(message.SendingPlayer(), (*it)->ID(), final_text));
    }

    return discard_event();
}

boost::statechart::result WaitingForTurnEnd::react(const EndGame& msg)
{
    if (TRACE_EXECUTION) Logger().debugStream() << "(ServerFSM) WaitingForTurnEnd.EndGame";
    ServerApp& server = context<ServerFSM>().Server();
    const Message& message = msg.m_message;
    PlayerConnectionPtr& player_connection = msg.m_player_connection;

    if (player_connection->Host()) {
        for (ServerNetworking::const_iterator it = server.m_networking.established_begin(); it != server.m_networking.established_end(); ++it) {
            if ((*it)->ID() != player_connection->ID())
                (*it)->SendMessage(EndGameMessage(-1, (*it)->ID()));
        }
        Logger().debugStream() << "WaitingForTurnEnd.EndGame : Server terminating.";
        server.m_networking.SendMessage(Message(Message::SERVER_DYING, -1, message.SendingPlayer(), Message::CORE, ""));
        server.m_networking.DisconnectAll();
        server.Exit(0);
    }

    return discard_event();
}


////////////////////////////////////////////////////////////
// WaitingForTurnEndIdle
////////////////////////////////////////////////////////////
WaitingForTurnEndIdle::WaitingForTurnEndIdle() :
    Base()
{ if (TRACE_EXECUTION) Logger().debugStream() << "(ServerFSM) WaitingForTurnEndIdle"; }

WaitingForTurnEndIdle::~WaitingForTurnEndIdle()
{ if (TRACE_EXECUTION) Logger().debugStream() << "(ServerFSM) ~WaitingForTurnEndIdle"; }

boost::statechart::result WaitingForTurnEndIdle::react(const SaveGameRequest& msg)
{
    if (TRACE_EXECUTION) Logger().debugStream() << "(ServerFSM) WaitingForTurnEnd.SaveGameRequest";
    ServerApp& server = context<ServerFSM>().Server();
    const Message& message = msg.m_message;
    PlayerConnectionPtr& player_connection = msg.m_player_connection;

    if (!player_connection->Host()) {
        Logger().errorStream() << "WaitingForTurnEndIdle.SaveGameRequest : Player #" << message.SendingPlayer()
                               << " attempted to initiate a game save, but is not the host.  Terminating connection.";
        server.m_networking.Disconnect(player_connection);
    }

    context<WaitingForTurnEnd>().m_save_filename = message.Text();

    return transit<WaitingForSaveData>();
}


////////////////////////////////////////////////////////////
// WaitingForSaveData
////////////////////////////////////////////////////////////
WaitingForSaveData::WaitingForSaveData(my_context c) :
    Base(c)
{
    if (TRACE_EXECUTION) Logger().debugStream() << "(ServerFSM) WaitingForSaveData";

    ServerApp& server = context<ServerFSM>().Server();
    for (ServerNetworking::const_iterator it = server.m_networking.established_begin(); it != server.m_networking.established_end(); ++it) {
        (*it)->SendMessage(ServerSaveGameMessage((*it)->ID()));
        m_needed_reponses.insert((*it)->ID());
    }
}

WaitingForSaveData::~WaitingForSaveData()
{ if (TRACE_EXECUTION) Logger().debugStream() << "(ServerFSM) ~WaitingForSaveData"; }

boost::statechart::result WaitingForSaveData::react(const ClientSaveData& msg)
{
    if (TRACE_EXECUTION) Logger().debugStream() << "(ServerFSM) WaitingForSaveData.ClientSaveData";
    ServerApp& server = context<ServerFSM>().Server();
    const Message& message = msg.m_message;
    PlayerConnectionPtr& player_connection = msg.m_player_connection;

    boost::shared_ptr<OrderSet> order_set(new OrderSet);
    boost::shared_ptr<SaveGameUIData> ui_data(new SaveGameUIData);
    if (!ExtractMessageData(message, *order_set, *ui_data))
        ui_data.reset();
    m_player_save_game_data.push_back(PlayerSaveGameData(player_connection->PlayerName(), server.GetPlayerEmpire(message.SendingPlayer()), order_set, ui_data));
    m_players_responded.insert(message.SendingPlayer());

    if (m_players_responded == m_needed_reponses) {
        SaveGame(context<WaitingForTurnEnd>().m_save_filename, server.m_current_turn, m_player_save_game_data, GetUniverse());
        context<WaitingForTurnEnd>().m_save_filename = "";
        return transit<WaitingForTurnEndIdle>();
    }

    return discard_event();
}
