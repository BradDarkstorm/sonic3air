/*
*	CoopNet -- generic peer-to-peer messaging for script mods
*	Addition to the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#include "oxygen/pch.h"
#include "oxygen/network/coopnet/CoopNet.h"
#include "oxygen/application/Configuration.h"
#include "oxygen/helper/JsonHelper.h"
#include "oxygen/simulation/EmulatorInterface.h"
#include "oxygen/simulation/RuntimeEnvironment.h"

#include "oxygen_netcore/network/HighLevelPacketBase.h"
#include "oxygen_netcore/serverclient/ProtocolVersion.h"

#include <lemon/program/ModuleBindingsBuilder.h>
#include <lemon/runtime/Runtime.h>


namespace
{
	static const constexpr uint16 COOPNET_PROTOCOL_VERSION = 1;

	// Client -> host, first packet after connecting
	struct CoopHelloPacket : public highlevel::PacketBase
	{
		HIGHLEVEL_PACKET_DEFINE_PACKET_TYPE("CoopNet.Hello");

		uint16 mProtocolVersion = COOPNET_PROTOCOL_VERSION;
		std::string mSignature;		// Set by the script mod, must match exactly (e.g. mod name + version)
		std::string mName;

		virtual void serializeContent(VectorBinarySerializer& serializer, uint8 protocolVersion) override
		{
			serializer.serialize(mProtocolVersion);
			serializer.serialize(mSignature, 255);
			serializer.serialize(mName, 32);
		}
	};

	// Host -> client, answer to hello
	struct CoopWelcomePacket : public highlevel::PacketBase
	{
		HIGHLEVEL_PACKET_DEFINE_PACKET_TYPE("CoopNet.Welcome");

		bool mAccepted = false;
		uint8 mPlayerID = 0;
		std::string mReason;

		virtual void serializeContent(VectorBinarySerializer& serializer, uint8 protocolVersion) override
		{
			serializer.serialize(mAccepted);
			serializer.serialize(mPlayerID);
			serializer.serialize(mReason, 255);
		}
	};

	// Host -> all clients, whenever someone joins or leaves
	struct CoopRosterPacket : public highlevel::PacketBase
	{
		HIGHLEVEL_PACKET_DEFINE_PACKET_TYPE("CoopNet.Roster");

		bool mActive[CoopNet::MAX_PLAYERS] = { false };
		std::string mNames[CoopNet::MAX_PLAYERS];

		virtual void serializeContent(VectorBinarySerializer& serializer, uint8 protocolVersion) override
		{
			for (int k = 0; k < CoopNet::MAX_PLAYERS; ++k)
			{
				serializer.serialize(mActive[k]);
				serializer.serialize(mNames[k], 32);
			}
		}
	};

	// Script data, in both directions
	struct CoopMessagePacket : public highlevel::PacketBase
	{
		HIGHLEVEL_PACKET_DEFINE_PACKET_TYPE("CoopNet.Message");

		uint8 mSender = 0;
		uint8 mTarget = CoopNet::TARGET_ALL;
		bool mReliable = true;		// Only relevant for the host relaying a client's message
		std::vector<uint8> mData;

		virtual void serializeContent(VectorBinarySerializer& serializer, uint8 protocolVersion) override
		{
			serializer.serialize(mSender);
			serializer.serialize(mTarget);
			serializer.serialize(mReliable);
			serializer.serializeData(mData, CoopNet::MAX_MESSAGE_SIZE);
		}
	};
}


CoopNet& CoopNet::instance()
{
	static CoopNet coopNet;
	return coopNet;
}

CoopNet::CoopNet() :
	mConnectionManager(&mUDPSocket, nullptr, *this, network::HIGHLEVEL_PROTOCOL_VERSION_RANGE)
{
}

CoopNet::~CoopNet()
{
	closeAll();
}

bool CoopNet::host(uint16 port)
{
	closeAll();
	if (port == 0)
		port = DEFAULT_PORT;

	if (!openSocket(true, port))
	{
		setFailed(State::FAILED, "Could not open port " + std::to_string(port) + " (already in use?)");
		return false;
	}

	mState = State::HOSTING;
	mLocalPlayerID = 0;
	mPlayers[0].mActive = true;
	mPlayers[0].mName = mLocalName;
	mStatusText = "Hosting on port " + std::to_string(port);
	RMX_LOG_INFO("CoopNet: " << mStatusText);
	return true;
}

bool CoopNet::join(std::string_view address, uint16 port)
{
	closeAll();
	if (port == 0)
		port = DEFAULT_PORT;

	if (!openSocket(false, 0))
	{
		setFailed(State::FAILED, "Could not open a network socket");
		return false;
	}

	const SocketAddress socketAddress(address, port);
	if (!mHostConnection.startConnectTo(mConnectionManager, socketAddress))
	{
		setFailed(State::FAILED, "Invalid address: " + std::string(address));
		return false;
	}

	mState = State::CONNECTING;
	mSentHello = false;
	mStatusText = "Connecting to " + std::string(address) + ":" + std::to_string(port);
	RMX_LOG_INFO("CoopNet: " << mStatusText);
	return true;
}

bool CoopNet::startFromConfig()
{
	loadConfig();
	switch (mConfigMode)
	{
		case 1:  return host(mConfigPort);
		case 2:  return join(mConfigAddress, mConfigPort);
		default:
			setFailed(State::FAILED, "No valid \"Mode\" set in coop.json");
			return false;
	}
}

void CoopNet::leave()
{
	closeAll();
	mState = State::IDLE;
	mStatusText.clear();
}

void CoopNet::update(float deltaSeconds)
{
	if (!mUDPSocket.isValid())
		return;

	mConnectionManager.updateConnectionManager();

	if (mState == State::CONNECTING || mState == State::CONNECTED)
	{
		const NetConnection::State connectionState = mHostConnection.getState();
		if (connectionState == NetConnection::State::CONNECTED && !mSentHello)
		{
			CoopHelloPacket packet;
			packet.mSignature = mSignature;
			packet.mName = mLocalName;
			mHostConnection.sendPacket(packet);
			mSentHello = true;
		}
		else if (connectionState == NetConnection::State::DISCONNECTED || connectionState == NetConnection::State::EMPTY)
		{
			const bool wasConnected = (mState == State::CONNECTED);
			const std::string text = wasConnected ? "Lost connection to host" : "Could not reach host";
			closeAll();
			setFailed(State::DISCONNECTED, text);
		}
	}
	else if (mState == State::HOSTING)
	{
		// Remove clients that disconnected
		bool rosterChanged = false;
		for (size_t k = 0; k < mPeers.size(); ++k)
		{
			Peer* peer = mPeers[k];
			if (peer->getState() == NetConnection::State::DISCONNECTED)
			{
				if (peer->mPlayerID != 0)
				{
					RMX_LOG_INFO("CoopNet: Player " << (peer->mPlayerID + 1) << " '" << mPlayers[peer->mPlayerID].mName << "' left");
					mPlayers[peer->mPlayerID] = PlayerInfo();
					rosterChanged = true;
				}
				mPeers.erase(mPeers.begin() + k);
				--k;
				peer->clear();
				delete peer;
			}
		}
		if (rosterChanged)
			sendRoster();
	}
}

bool CoopNet::send(uint8 target, const uint8* data, size_t size, bool reliable)
{
	if (!isOnline() || size > MAX_MESSAGE_SIZE || target == mLocalPlayerID)
		return false;

	std::vector<uint8> content(data, data + size);
	if (mState == State::HOSTING)
	{
		relayFromHost(mLocalPlayerID, target, content, reliable, nullptr);
	}
	else
	{
		CoopMessagePacket packet;
		packet.mSender = mLocalPlayerID;
		packet.mTarget = target;
		packet.mReliable = reliable;
		packet.mData.swap(content);
		mHostConnection.sendPacket(packet, reliable ? NetConnection::SendFlags::NONE : NetConnection::SendFlags::UNRELIABLE);
	}
	return true;
}

const CoopNet::Message* CoopNet::peekMessage() const
{
	return mIncoming.empty() ? nullptr : &mIncoming.front();
}

void CoopNet::popMessage()
{
	if (!mIncoming.empty())
		mIncoming.pop_front();
}

uint8 CoopNet::getNumPlayers() const
{
	uint8 count = 0;
	for (const PlayerInfo& info : mPlayers)
	{
		if (info.mActive)
			++count;
	}
	return count;
}

NetConnection* CoopNet::createNetConnection(ConnectionManager& connectionManager, const SocketAddress& senderAddress)
{
	// Only the host accepts incoming connections
	if (mState != State::HOSTING)
		return nullptr;
	if (mPeers.size() >= MAX_PLAYERS - 1)
		return nullptr;

	Peer* peer = new Peer();
	mPeers.push_back(peer);
	return peer;
}

void CoopNet::destroyNetConnection(NetConnection& connection)
{
	// Called by the connection manager when replacing a stale connection from the same address
	for (size_t k = 0; k < mPeers.size(); ++k)
	{
		if (mPeers[k] == &connection)
		{
			if (mPeers[k]->mPlayerID != 0)
				mPlayers[mPeers[k]->mPlayerID] = PlayerInfo();
			mPeers.erase(mPeers.begin() + k);
			delete &connection;
			sendRoster();
			return;
		}
	}
}

bool CoopNet::onReceivedPacket(ReceivedPacketEvaluation& evaluation)
{
	switch (evaluation.mPacketType)
	{
		case CoopHelloPacket::PACKET_TYPE:
		{
			if (mState != State::HOSTING)
				return true;

			CoopHelloPacket packet;
			if (!evaluation.readPacket(packet))
				return false;

			Peer& peer = static_cast<Peer&>(evaluation.mConnection);
			if (peer.mWelcomed)
				return true;

			CoopWelcomePacket answer;
			if (packet.mProtocolVersion != COOPNET_PROTOCOL_VERSION)
			{
				answer.mReason = "Different CoopNet engine version than the host";
			}
			else if (packet.mSignature != mSignature)
			{
				answer.mReason = "Different mod version than the host (host has \"" + mSignature + "\", you have \"" + packet.mSignature + "\")";
			}
			else
			{
				for (uint8 id = 1; id < MAX_PLAYERS; ++id)
				{
					if (!mPlayers[id].mActive)
					{
						answer.mAccepted = true;
						answer.mPlayerID = id;
						break;
					}
				}
				if (!answer.mAccepted)
					answer.mReason = "Session is full";
			}

			peer.sendPacket(answer);
			if (answer.mAccepted)
			{
				peer.mWelcomed = true;
				peer.mPlayerID = answer.mPlayerID;
				mPlayers[peer.mPlayerID].mActive = true;
				mPlayers[peer.mPlayerID].mName = packet.mName.empty() ? ("Player " + std::to_string(peer.mPlayerID + 1)) : packet.mName;
				RMX_LOG_INFO("CoopNet: Player " << (peer.mPlayerID + 1) << " '" << mPlayers[peer.mPlayerID].mName << "' joined");
				sendRoster();
			}
			else
			{
				RMX_LOG_INFO("CoopNet: Rejected a player: " << answer.mReason);
			}
			// Rejected clients disconnect themselves after reading the reason
			return true;
		}

		case CoopWelcomePacket::PACKET_TYPE:
		{
			if (mState != State::CONNECTING)
				return true;

			CoopWelcomePacket packet;
			if (!evaluation.readPacket(packet))
				return false;

			if (packet.mAccepted)
			{
				mState = State::CONNECTED;
				mLocalPlayerID = packet.mPlayerID;
				mStatusText = "Connected as player " + std::to_string(mLocalPlayerID + 1);
				RMX_LOG_INFO("CoopNet: " << mStatusText);
			}
			else
			{
				const std::string reason = "Host rejected: " + packet.mReason;
				closeAll();
				setFailed(State::DISCONNECTED, reason);
			}
			return true;
		}

		case CoopRosterPacket::PACKET_TYPE:
		{
			if (mState != State::CONNECTED && mState != State::CONNECTING)
				return true;

			CoopRosterPacket packet;
			if (!evaluation.readPacket(packet))
				return false;

			for (int k = 0; k < MAX_PLAYERS; ++k)
			{
				mPlayers[k].mActive = packet.mActive[k];
				mPlayers[k].mName = packet.mNames[k];
			}
			return true;
		}

		case CoopMessagePacket::PACKET_TYPE:
		{
			CoopMessagePacket packet;
			if (!evaluation.readPacket(packet))
				return false;

			if (mState == State::HOSTING)
			{
				// Never trust the sender field from a client, use the ID we assigned
				Peer& peer = static_cast<Peer&>(evaluation.mConnection);
				if (!peer.mWelcomed)
					return true;
				relayFromHost(peer.mPlayerID, packet.mTarget, packet.mData, packet.mReliable, &peer);
			}
			else if (mState == State::CONNECTED)
			{
				deliverLocally(packet.mSender, packet.mData);
			}
			return true;
		}
	}
	return false;
}

bool CoopNet::openSocket(bool asHost, uint16 port)
{
	Sockets::startupSockets();
	if (asHost)
		return mUDPSocket.bindToPort(port, Sockets::ProtocolFamily::IPv4);
	else
		return mUDPSocket.bindToAnyPort(Sockets::ProtocolFamily::IPv4);
}

void CoopNet::closeAll()
{
	for (Peer* peer : mPeers)
	{
		if (peer->getState() == NetConnection::State::CONNECTED)
			peer->disconnect(NetConnection::DisconnectReason::MANUAL_LOCAL);
		peer->clear();
		delete peer;
	}
	mPeers.clear();

	if (mHostConnection.getState() == NetConnection::State::CONNECTED)
		mHostConnection.disconnect(NetConnection::DisconnectReason::MANUAL_LOCAL);
	mHostConnection.clear();
	mSentHello = false;

	mUDPSocket.close();
	mIncoming.clear();
	for (PlayerInfo& info : mPlayers)
		info = PlayerInfo();
	mLocalPlayerID = 0;
}

void CoopNet::setFailed(State state, std::string_view text)
{
	mState = state;
	mStatusText = text;
	RMX_LOG_INFO("CoopNet: " << text);
}

void CoopNet::sendRoster()
{
	if (mState != State::HOSTING)
		return;

	CoopRosterPacket packet;
	for (int k = 0; k < MAX_PLAYERS; ++k)
	{
		packet.mActive[k] = mPlayers[k].mActive;
		packet.mNames[k] = mPlayers[k].mName;
	}
	for (Peer* peer : mPeers)
	{
		if (peer->mWelcomed)
			peer->sendPacket(packet);
	}
}

void CoopNet::deliverLocally(uint8 sender, const std::vector<uint8>& data)
{
	if (mIncoming.size() >= MAX_QUEUED_MESSAGES)
		mIncoming.pop_front();		// Script is not reading messages; drop the oldest

	Message& message = mIncoming.emplace_back();
	message.mSender = sender;
	message.mData = data;
}

void CoopNet::relayFromHost(uint8 sender, uint8 target, const std::vector<uint8>& data, bool reliable, Peer* exclude)
{
	// Deliver to the host itself
	if ((target == TARGET_ALL || target == mLocalPlayerID) && sender != mLocalPlayerID)
	{
		deliverLocally(sender, data);
	}
	if (target == mLocalPlayerID)
		return;

	CoopMessagePacket packet;
	packet.mSender = sender;
	packet.mTarget = target;
	packet.mReliable = reliable;
	packet.mData = data;

	for (Peer* peer : mPeers)
	{
		if (peer == exclude || !peer->mWelcomed)
			continue;
		if (target != TARGET_ALL && peer->mPlayerID != target)
			continue;
		peer->sendPacket(packet, reliable ? NetConnection::SendFlags::NONE : NetConnection::SendFlags::UNRELIABLE);
	}
}

CoopNet::Peer* CoopNet::findPeerByPlayerID(uint8 id)
{
	for (Peer* peer : mPeers)
	{
		if (peer->mWelcomed && peer->mPlayerID == id)
			return peer;
	}
	return nullptr;
}

void CoopNet::loadConfig()
{
	// Look for "coop.json" next to the game first, then in the app data folder (where settings.json is)
	Json::Value root = JsonHelper::loadFile(L"coop.json");
	if (!root.isObject())
		root = JsonHelper::loadFile(Configuration::instance().mAppDataPath + L"coop.json");
	if (!root.isObject())
	{
		mConfigMode = 0;
		return;
	}

	const std::string mode = root["Mode"].isString() ? root["Mode"].asString() : "";
	mConfigMode = (mode == "host" || mode == "Host") ? 1 : (mode == "join" || mode == "Join") ? 2 : 0;
	mConfigAddress = root["Address"].isString() ? root["Address"].asString() : "";
	if (root["Port"].isString())
		mConfigPort = (uint16)std::atoi(root["Port"].asString().c_str());
	else if (root["Port"].isIntegral())
		mConfigPort = (uint16)root["Port"].asInt();
	if (root["PlayerName"].isString() && !root["PlayerName"].asString().empty())
		mLocalName = root["PlayerName"].asString().substr(0, 32);
	mConfigLoaded = true;
}



//-------------------------------------------------------//
// Script bindings                                       //
//-------------------------------------------------------//

namespace
{
	inline EmulatorInterface& getEmulatorInterface()
	{
		return *lemon::Runtime::getActiveEnvironmentSafe<RuntimeEnvironment>().mEmulatorInterface;
	}

	uint8 s_lastSender = 0;

	bool Net_host(uint16 port)									{ return CoopNet::instance().host(port); }
	bool Net_join(lemon::StringRef address, uint16 port)		{ return address.isValid() && CoopNet::instance().join(address.getString(), port); }
	bool Net_startFromConfig()									{ return CoopNet::instance().startFromConfig(); }
	void Net_leave()											{ CoopNet::instance().leave(); }

	uint8 Net_getState()										{ return (uint8)CoopNet::instance().getState(); }
	bool Net_isHost()											{ return CoopNet::instance().isHost(); }
	bool Net_isOnline()											{ return CoopNet::instance().isOnline(); }
	uint8 Net_getLocalPlayerId()								{ return CoopNet::instance().getLocalPlayerID(); }
	uint8 Net_getNumPlayers()									{ return CoopNet::instance().getNumPlayers(); }
	uint8 Net_getMaxPlayers()									{ return CoopNet::MAX_PLAYERS; }
	bool Net_isPlayerConnected(uint8 id)						{ return CoopNet::instance().getPlayerInfo(id).mActive; }
	lemon::StringRef Net_getPlayerName(uint8 id)				{ return lemon::StringRef(lemon::FlyweightString(CoopNet::instance().getPlayerInfo(id).mName)); }
	lemon::StringRef Net_getStatusText()						{ return lemon::StringRef(lemon::FlyweightString(CoopNet::instance().getStatusText())); }
	uint8 Net_getConfigMode()									{ return CoopNet::instance().getConfigMode(); }

	void Net_setSignature(lemon::StringRef signature)			{ if (signature.isValid()) CoopNet::instance().setSignature(signature.getString()); }
	void Net_setPlayerName(lemon::StringRef name)				{ if (name.isValid()) CoopNet::instance().setPlayerName(name.getString()); }

	bool Net_send(uint8 target, uint32 sourceAddress, uint16 bytes, bool reliable)
	{
		if (bytes == 0 || bytes > CoopNet::MAX_MESSAGE_SIZE)
			return false;
		const uint8* src = getEmulatorInterface().getMemoryPointer(sourceAddress, false, bytes);
		if (nullptr == src)
			return false;
		return CoopNet::instance().send(target, src, bytes, reliable);
	}

	// Copies the next received message to "targetAddress" (max. "maxBytes", rest is cut off) and returns its full size, or 0 if there's none
	uint16 Net_receive(uint32 targetAddress, uint16 maxBytes)
	{
		CoopNet& coopNet = CoopNet::instance();
		const CoopNet::Message* message = coopNet.peekMessage();
		if (nullptr == message)
			return 0;

		const uint16 size = (uint16)message->mData.size();
		const uint16 copySize = std::min(size, maxBytes);
		if (copySize > 0)
		{
			uint8* dst = getEmulatorInterface().getMemoryPointer(targetAddress, true, copySize);
			if (nullptr != dst)
				memcpy(dst, &message->mData[0], copySize);
		}
		s_lastSender = message->mSender;
		coopNet.popMessage();
		return std::max<uint16>(size, 1);
	}

	uint8 Net_getLastSender()									{ return s_lastSender; }
}


void CoopNet::registerScriptBindings(lemon::Module& module)
{
	lemon::ModuleBindingsBuilder builder(module);
	const BitFlagSet<lemon::Function::Flag> defaultFlags(lemon::Function::Flag::ALLOW_INLINE_EXECUTION);
	const BitFlagSet<lemon::Function::Flag> compileTimeConstant(lemon::Function::Flag::ALLOW_INLINE_EXECUTION, lemon::Function::Flag::COMPILE_TIME_CONSTANT);

	// Session
	builder.addNativeFunction("Net.host", lemon::wrap(&Net_host), defaultFlags).setParameters("port");
	builder.addNativeFunction("Net.join", lemon::wrap(&Net_join), defaultFlags).setParameters("address", "port");
	builder.addNativeFunction("Net.startFromConfig", lemon::wrap(&Net_startFromConfig), defaultFlags);
	builder.addNativeFunction("Net.leave", lemon::wrap(&Net_leave), defaultFlags);
	builder.addNativeFunction("Net.setSignature", lemon::wrap(&Net_setSignature), defaultFlags).setParameters("signature");
	builder.addNativeFunction("Net.setPlayerName", lemon::wrap(&Net_setPlayerName), defaultFlags).setParameters("name");

	// State queries
	builder.addNativeFunction("Net.getState", lemon::wrap(&Net_getState), defaultFlags);
	builder.addNativeFunction("Net.isHost", lemon::wrap(&Net_isHost), defaultFlags);
	builder.addNativeFunction("Net.isOnline", lemon::wrap(&Net_isOnline), defaultFlags);
	builder.addNativeFunction("Net.getLocalPlayerId", lemon::wrap(&Net_getLocalPlayerId), defaultFlags);
	builder.addNativeFunction("Net.getNumPlayers", lemon::wrap(&Net_getNumPlayers), defaultFlags);
	builder.addNativeFunction("Net.getMaxPlayers", lemon::wrap(&Net_getMaxPlayers), compileTimeConstant);
	builder.addNativeFunction("Net.isPlayerConnected", lemon::wrap(&Net_isPlayerConnected), defaultFlags).setParameters("playerId");
	builder.addNativeFunction("Net.getPlayerName", lemon::wrap(&Net_getPlayerName), defaultFlags).setParameters("playerId");
	builder.addNativeFunction("Net.getStatusText", lemon::wrap(&Net_getStatusText), defaultFlags);
	builder.addNativeFunction("Net.getConfigMode", lemon::wrap(&Net_getConfigMode), defaultFlags);

	// Messaging
	builder.addNativeFunction("Net.send", lemon::wrap(&Net_send), defaultFlags).setParameters("targetPlayerId", "sourceAddress", "bytes", "reliable");
	builder.addNativeFunction("Net.receive", lemon::wrap(&Net_receive), defaultFlags).setParameters("targetAddress", "maxBytes");
	builder.addNativeFunction("Net.getLastSender", lemon::wrap(&Net_getLastSender), defaultFlags);
}
