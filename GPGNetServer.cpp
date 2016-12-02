#include "GPGNetServer.h"

#include <type_traits>

#include <sigc++/sigc++.h>

#include "GPGNetMessage.h"
#include "logging.h"

namespace sigc {
  SIGC_FUNCTORS_DEDUCE_RESULT_TYPE_WITH_DECLTYPE
}

namespace faf
{

GPGNetServer::GPGNetServer(int port):
  TcpServer(port)
{
  FAF_LOG_TRACE << "GPGNetServer()";
}

GPGNetServer::~GPGNetServer()
{
  FAF_LOG_TRACE << "~GPGNetServer()";
}

void GPGNetServer::sendMessage(GPGNetMessage const& msg)
{
  if (mSessions.empty())
  {
    throw std::runtime_error(std::string("No GPGNetConnection. Wait for the game to connect first."));
  }
  for (auto it = mSessions.begin(), end = mSessions.end(); it != end; ++it)
  {
    (*it)->send(msg.toBinary());
  }
  FAF_LOG_TRACE << "sending " << msg.toDebug();
}

void GPGNetServer::sendCreateLobby(InitMode initMode,
                                   int port,
                                   std::string const& login,
                                   int playerId,
                                   int natTraversalProvider)
{
  GPGNetMessage msg;
  msg.header = "CreateLobby";
  msg.chunks = {
    static_cast<std::underlying_type<InitMode>::type>(initMode),
    port,
    login,
    playerId,
    natTraversalProvider
  };
  sendMessage(msg);
}

void GPGNetServer::sendConnectToPeer(std::string const& addressAndPort,
                                     std::string const& playerName,
                                     int playerId)
{
  GPGNetMessage msg;
  msg.header = "ConnectToPeer";
  msg.chunks = {
    addressAndPort,
    playerName,
    playerId
  };
  sendMessage(msg);
}

void GPGNetServer::sendJoinGame(std::string const& addressAndPort,
                                std::string const& remotePlayerName,
                                int remotePlayerId)
{
  GPGNetMessage msg;
  msg.header = "JoinGame";
  msg.chunks = {
    addressAndPort,
    remotePlayerName,
    remotePlayerId
  };
  sendMessage(msg);
}

void GPGNetServer::sendHostGame(std::string const& map)
{
  GPGNetMessage msg;
  msg.header = "HostGame";
  msg.chunks = {
    map
  };
  sendMessage(msg);
}

void GPGNetServer::sendSendNatPacket(std::string const& addressAndPort,
                                     std::string const& message)
{
  GPGNetMessage msg;
  msg.header = "SendNatPacket";
  msg.chunks = {
    addressAndPort,
    message
  };
  sendMessage(msg);
}

void GPGNetServer::sendDisconnectFromPeer(int remotePlayerId)
{
  GPGNetMessage msg;
  msg.header = "DisconnectFromPeer";
  msg.chunks = {
    remotePlayerId
  };
  sendMessage(msg);
}

void GPGNetServer::sendPing()
{
  GPGNetMessage msg;
  msg.header = "ping";
  sendMessage(msg);
}

void GPGNetServer::addGpgMessageCallback(GpgMessageCallback cb)
{
  mGPGNetMessageCallbacks.push_back(cb);
}

void GPGNetServer::addConnectionStateCallback(ConnectionStateCallback cb)
{
  mConnectionStateCallbacks.push_back(cb);
}

ConnectionState GPGNetServer::connectionState() const
{
  if (mSessions.size())
  {
    return ConnectionState::Connected;
  }
  else
  {
    return ConnectionState::Disconnected;
  }
}

bool GPGNetServer::parseMessage(std::vector<char>& msgBuffer)
{
  GPGNetMessage::parse(msgBuffer, [this](GPGNetMessage const& msg)
  {
    FAF_LOG_TRACE << "received " << msg.toDebug();
    for (auto cb : mGPGNetMessageCallbacks)
    {
      cb(msg);
    }
  });
  return false;
}

void GPGNetServer::onConnected()
{
  for (auto cb : mConnectionStateCallbacks)
  {
    cb(ConnectionState::Connected);
  }
}

void GPGNetServer::onDisconnected()
{
  for (auto cb : mConnectionStateCallbacks)
  {
    cb(ConnectionState::Disconnected);
  }
}

}
