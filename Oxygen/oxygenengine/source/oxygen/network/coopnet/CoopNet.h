/*
*	CoopNet -- generic peer-to-peer messaging for script mods
*	Addition to the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*
*	Design goals:
*	 - Contains NO game-specific logic: it only moves byte blobs between players.
*	   All gameplay syncing lives in a script mod, so engine updates need nothing here.
*	 - Star topology: one host, up to (MAX_PLAYERS - 1) clients, direct UDP connection.
*	   The host relays client messages to their targets.
*	 - Touches existing engine code in only two places (binding registration + update call).
*/

#pragma once

#include "oxygen_netcore/network/ConnectionListener.h"
#include "oxygen_netcore/network/ConnectionManager.h"
#include "oxygen_netcore/network/NetConnection.h"

namespace lemon
{
	class Module;
}


class CoopNet : public ConnectionListenerInterface
{
public:
	static const constexpr uint8  MAX_PLAYERS = 8;
	static const constexpr uint8  TARGET_ALL = 0xff;
	static const constexpr uint16 DEFAULT_PORT = 28850;
	static const constexpr uint16 MAX_MESSAGE_SIZE = 1024;
	static const constexpr size_t MAX_QUEUED_MESSAGES = 2048;

	enum class State : uint8
	{
		IDLE		 = 0,	// Nothing going on
		HOSTING		 = 1,	// Hosting a session (may or may not have clients yet)
		CONNECTING	 = 2,	// Client: connecting to the host, waiting for welcome
		CONNECTED	 = 3,	// Client: connected and accepted by the host
		FAILED		 = 4,	// Failed to host or join, see status text
		DISCONNECTED = 5	// Client: lost connection or got rejected, see status text
	};

	struct Message
	{
		uint8 mSender = 0;
		std::vector<uint8> mData;
	};

	struct PlayerInfo
	{
		bool mActive = false;
		std::string mName;
	};

public:
	static CoopNet& instance();
	static void registerScriptBindings(lemon::Module& module);

public:
	CoopNet();
	~CoopNet();

	bool host(uint16 port);
	bool join(std::string_view address, uint16 port);
	bool startFromConfig();
	void leave();

	void update(float deltaSeconds);

	bool send(uint8 target, const uint8* data, size_t size, bool reliable);
	const Message* peekMessage() const;
	void popMessage();

	inline State getState() const						{ return mState; }
	inline bool isHost() const							{ return mState == State::HOSTING; }
	inline bool isOnline() const						{ return mState == State::HOSTING || mState == State::CONNECTED; }
	inline uint8 getLocalPlayerID() const				{ return mLocalPlayerID; }
	inline const std::string& getStatusText() const		{ return mStatusText; }
	inline const PlayerInfo& getPlayerInfo(uint8 id) const  { static PlayerInfo EMPTY; return (id < MAX_PLAYERS) ? mPlayers[id] : EMPTY; }
	uint8 getNumPlayers() const;

	inline void setSignature(std::string_view signature)	{ mSignature = signature; }
	inline void setPlayerName(std::string_view name)		{ mLocalName = name; }

	inline uint8 getConfigMode() const					{ return mConfigMode; }

protected:
	// ConnectionListenerInterface
	virtual NetConnection* createNetConnection(ConnectionManager& connectionManager, const SocketAddress& senderAddress) override;
	virtual void destroyNetConnection(NetConnection& connection) override;
	virtual bool onReceivedPacket(ReceivedPacketEvaluation& evaluation) override;

private:
	struct Peer : public NetConnection
	{
		uint8 mPlayerID = 0;		// 0 until a hello was received and accepted
		bool mWelcomed = false;
	};

private:
	bool openSocket(bool asHost, uint16 port);
	void closeAll();
	void setFailed(State state, std::string_view text);

	void sendRoster();
	void deliverLocally(uint8 sender, const std::vector<uint8>& data);
	void relayFromHost(uint8 sender, uint8 target, const std::vector<uint8>& data, bool reliable, Peer* exclude);
	Peer* findPeerByPlayerID(uint8 id);
	void loadConfig();

private:
	UDPSocket mUDPSocket;
	ConnectionManager mConnectionManager;

	State mState = State::IDLE;
	std::string mStatusText;

	// Client side
	NetConnection mHostConnection;
	bool mSentHello = false;

	// Host side
	std::vector<Peer*> mPeers;

	uint8 mLocalPlayerID = 0;
	PlayerInfo mPlayers[MAX_PLAYERS];
	std::deque<Message> mIncoming;

	std::string mSignature;
	std::string mLocalName = "Player";

	// From "coop.json"
	bool mConfigLoaded = false;
	uint8 mConfigMode = 0;		// 0 = not set, 1 = host, 2 = join
	std::string mConfigAddress;
	uint16 mConfigPort = DEFAULT_PORT;
};
