#include "Testserver.h"

#include <QtCore/QCoreApplication>
#include <QtNetwork/QHostAddress>

#include "JsonRpcQTcpSocket.h"
#include "logging.h"

namespace faf {

Testserver::Testserver():
  mServer(54321),
  mCurrentPlayerId(0)
{
  QObject::connect(&mServer,
                   &JsonRpcQTcpServer::disconnected,
                   [this](JsonRpcQTcpSocket* socket)
  {
    auto it = mSocketPlayers.find(socket);
    if (it != mSocketPlayers.end())
    {
      PlayerIdType leavingPlayerId = it->second;
      FAF_LOG_TRACE << "logout " << leavingPlayerId;
      disconnectPlayerFromGame(leavingPlayerId);
      mPlayerSockets.erase(leavingPlayerId);
      mSocketPlayers.erase(it);
      mPlayersLogins.erase(leavingPlayerId);
    }
  });

  mServer.setRpcCallback("login",
                         [this](Json::Value const& paramsArray,
                         Json::Value & result,
                         Json::Value & error,
                         Socket* socket)
  {
    if (mSocketPlayers.find(socket) != mSocketPlayers.end())
    {
      error = "Already logged in";
      return;
    }
    if (paramsArray.size() < 1)
    {
      error = "Need 1 parameters: login (string)";
      return;
    }

    auto loginExists = [this](std::string const& login)
    {
      for (auto it = mPlayersLogins.cbegin(), end = mPlayersLogins.cend(); it != end; ++it)
      {
        if (login == it->second)
        {
          return true;
        }
      }
      return false;
    };

    auto login = paramsArray[0].asString();
    if (loginExists(login))
    {
      int suffix = 1;
      while (true)
      {
        auto newLogin = login + std::to_string(suffix++);
        if (!loginExists(newLogin))
        {
          login = newLogin;
          break;
        }
      }
    }

    FAF_LOG_TRACE << "login " << mCurrentPlayerId;
    mPlayerSockets.insert(std::make_pair(mCurrentPlayerId, socket));
    mSocketPlayers.insert(std::make_pair(socket, mCurrentPlayerId));
    mPlayersLogins.insert(std::make_pair(mCurrentPlayerId, login));

    result["id"] = mCurrentPlayerId;
    result["login"] = login;
    result["games"] = gamelistJson();
    ++mCurrentPlayerId;
  });

  mServer.setRpcCallback("reconnect",
                         [this](Json::Value const& paramsArray,
                         Json::Value & result,
                         Json::Value & error,
                         Socket* socket)
  {
    if (mSocketPlayers.find(socket) != mSocketPlayers.end())
    {
      error = "Already logged in";
      return;
    }
    if (paramsArray.size() < 1)
    {
      error = "Need 1 parameters: id (int)";
      return;
    }

    auto id = paramsArray[0].asInt();
    auto socketIt = mPlayerSockets.find(id);
    if (socketIt == mPlayerSockets.end())
    {
      error = "id not found";
      return;
    }

    mSocketPlayers.erase(socketIt->second);
    mPlayerSockets.erase(id);
    mPlayerSockets.insert(std::make_pair(id, socket));
    mSocketPlayers.insert(std::make_pair(socket, id));
    result = "ok";
  });

  mServer.setRpcCallback("hostGame",
                         [this](Json::Value const& paramsArray,
                         Json::Value & result,
                         Json::Value & error,
                         Socket* socket)
  {
    auto it = mSocketPlayers.find(socket);
    if (it != mSocketPlayers.end())
    {
      PlayerIdType hostingPlayerId = it->second;
      disconnectPlayerFromGame(hostingPlayerId);
      mGames[hostingPlayerId].insert(hostingPlayerId);
      mPlayersGames[hostingPlayerId] = hostingPlayerId;
      {
        Json::Value params(Json::arrayValue);
        params.append("hostGame");
        Json::Value hostGameParams(Json::arrayValue);
        hostGameParams.append("testmap");
        mServer.sendRequest("sendToIceAdapter",
                            params,
                            socket);
      }
      sendGamelist();
    }
  });

  mServer.setRpcCallback("leaveGame",
                         [this](Json::Value const& paramsArray,
                         Json::Value & result,
                         Json::Value & error,
                         Socket* socket)
  {
    auto it = mSocketPlayers.find(socket);
    if (it != mSocketPlayers.end())
    {
      disconnectPlayerFromGame(it->second);
    }
  });

  mServer.setRpcCallback("joinGame",
                         [this](Json::Value const& paramsArray,
                         Json::Value & result,
                         Json::Value & error,
                         Socket* joiningPlayerSocket)
  {
    if (paramsArray.size() < 1)
    {
      error = "Need 1 parameters: remotePlayerId (int)";
      return;
    }
    auto joiningPlayerIt = mSocketPlayers.find(joiningPlayerSocket);
    if (joiningPlayerIt == mSocketPlayers.end())
    {
      error = "playerId not found";
      return;
    }

    PlayerIdType joiningPlayerId = joiningPlayerIt->second;
    disconnectPlayerFromGame(joiningPlayerId);

    PlayerIdType hostingPlayerId = paramsArray[0].asInt();
    auto gameIt = mGames.find(hostingPlayerId);
    if (gameIt == mGames.end())
    {
      error = "game not found";
      return;
    }
    gameIt->second.insert(joiningPlayerId);
    mPlayersGames[joiningPlayerId] = hostingPlayerId;

    /* send "connectToPeer" to host */
    {
      Json::Value params(Json::arrayValue);
      params.append("connectToPeer");
      Json::Value connectToPeerParams(Json::arrayValue);
      connectToPeerParams.append(mPlayersLogins.at(joiningPlayerId));
      connectToPeerParams.append(joiningPlayerId);
      connectToPeerParams.append(true);
      params.append(connectToPeerParams);
      mServer.sendRequest("sendToIceAdapter",
                          params,
                          mPlayerSockets[hostingPlayerId]);
    }

    /* Send "joinGame" to joining player */
    Json::Value joinGamesParams(Json::arrayValue);
    Json::Value joinGamesParamsParams(Json::arrayValue);
    joinGamesParamsParams.append(mPlayersLogins[hostingPlayerId]);
    joinGamesParamsParams.append(hostingPlayerId);
    joinGamesParams.append("joinGame");
    joinGamesParams.append(joinGamesParamsParams);
    mServer.sendRequest("sendToIceAdapter",
                        joinGamesParams,
                        joiningPlayerSocket);
    for(PlayerIdType const& existingPlayerId: gameIt->second)
    {
      if (existingPlayerId == joiningPlayerId)
      {
        continue;
      }
      if (existingPlayerId == hostingPlayerId)
      {
        continue;
      }
      /* Send "connectToPeer" to fellow players to connect to joining player */
      {
        Json::Value params(Json::arrayValue);
        params.append("connectToPeer");
        Json::Value connectToPeerParams(Json::arrayValue);
        connectToPeerParams.append(mPlayersLogins.at(joiningPlayerId));
        connectToPeerParams.append(joiningPlayerId);
        connectToPeerParams.append(true);
        params.append(connectToPeerParams);
        mServer.sendRequest("sendToIceAdapter",
                            params,
                            mPlayerSockets[existingPlayerId]);
      }
      /* Send "connectToPeer" to joining player to connect to fellow players   */
      {
        Json::Value params(Json::arrayValue);
        params.append("connectToPeer");
        Json::Value connectToPeerParams(Json::arrayValue);
        connectToPeerParams.append(mPlayersLogins.at(existingPlayerId));
        connectToPeerParams.append(existingPlayerId);
        connectToPeerParams.append(false);
        params.append(connectToPeerParams);
        mServer.sendRequest("sendToIceAdapter",
                            params,
                            joiningPlayerSocket);
      }
    }
  });
  mServer.setRpcCallback("sendIceMsg",
                         [this](Json::Value const& paramsArray,
                         Json::Value & result,
                         Json::Value & error,
                         Socket* socket)
  {
    if (paramsArray.size() < 3)
    {
      error = "Need 3 parameters: playerId (int), remotePlayerId (int), sdp (string)";
      return;
    }
    auto remoteId = paramsArray[1].asInt();
    auto remoteIt = mPlayerSockets.find(remoteId);
    if (remoteIt == mPlayerSockets.end())
    {
      error = "remotePlayerId not found";
      return;
    }
    Json::Value params(Json::arrayValue);
    params.append("iceMsg");
    Json::Value iceMsgParams(Json::arrayValue);
    iceMsgParams.append(mSocketPlayers[socket]);
    iceMsgParams.append(paramsArray[2]);
    params.append(iceMsgParams);
    mServer.sendRequest("sendToIceAdapter",
                        params,
                        remoteIt->second);
  });
}

Json::Value Testserver::gamelistJson() const
{
  Json::Value result(Json::objectValue);
  for (auto it = mGames.cbegin(), end = mGames.cend(); it != end; ++it)
  {
    result[mPlayersLogins.at(it->first)] = it->first;
  }
  return result;
}

void Testserver::sendGamelist(Socket* s)
{
  Json::Value params(Json::arrayValue);
  params.append(gamelistJson());
  mServer.sendRequest("onGamelist",
                      params,
                      s);
}

void Testserver::disconnectPlayerFromGame(PlayerIdType leavingPlayerId)
{
  /* first check if the player hosts a game */
  auto hostingGameIt = mGames.find(leavingPlayerId);
  if (hostingGameIt != mGames.end())
  {
    for(PlayerIdType const& fellowPlayerId: hostingGameIt->second)
    {
      Json::Value params(Json::arrayValue);
      params.append(leavingPlayerId);
      mServer.sendRequest("onHostLeft",
                          params,
                          mPlayerSockets[fellowPlayerId]);

    }
    mGames.erase(hostingGameIt);
    sendGamelist();
  }
  auto leavinPlayersGameIt = mPlayersGames.find(leavingPlayerId);
  if (leavinPlayersGameIt != mPlayersGames.end())
  {
    auto gameId = leavinPlayersGameIt->second;
    auto gameIt = mGames.find(gameId);
    if (gameIt != mGames.end())
    {
      /* disconnect all fellow players from the game */
      for(PlayerIdType const& fellowPlayerId: gameIt->second)
      {
        if (fellowPlayerId != leavingPlayerId)
        {
          {
            Json::Value disconnectParams(Json::arrayValue);
            disconnectParams.append(leavingPlayerId);
            Json::Value params(Json::arrayValue);
            params.append("disconnectFromPeer");
            params.append(disconnectParams);
            mServer.sendRequest("sendToIceAdapter",
                                params,
                                mPlayerSockets[fellowPlayerId]);
          }
          {
            Json::Value disconnectParams(Json::arrayValue);
            disconnectParams.append(fellowPlayerId);
            Json::Value params(Json::arrayValue);
            params.append("disconnectFromPeer");
            params.append(disconnectParams);
            mServer.sendRequest("sendToIceAdapter",
                                params,
                                mPlayerSockets[leavingPlayerId]);
          }
        }
      }
      gameIt->second.erase(leavingPlayerId);
    }
    mPlayersGames.erase(leavinPlayersGameIt);
  }
}

} // namespace faf

int main(int argc, char *argv[])
{
  faf::logging_init();

  QCoreApplication a(argc, argv);

  faf::Testserver server;

  return a.exec();
}
