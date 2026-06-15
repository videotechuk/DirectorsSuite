// Director's Suite - OBS Studio recording control over the obs-websocket 5.x protocol.
//
// Used to automatically start/stop an OBS recording around full-project
// playback so finished sequences can be exported in one click.
//
// The protocol (see obs-websocket docs/generated/protocol.md):
//   1. TCP connect + HTTP WebSocket upgrade
//   2. receive Hello (op 0); if it carries an `authentication` object, answer
//      the SHA256 challenge
//   3. send Identify (op 1) with rpcVersion 1 (+ auth string)
//   4. receive Identified (op 2)
//   5. send Request (op 6) with requestType "StartRecord" / "StopRecord"
//
// OBS recording is an output that keeps running independently of the websocket
// session, so each command uses its own short-lived connection on a detached
// worker thread - the game thread never blocks on network I/O.

#pragma once
#include <string>
#include <atomic>
#include <mutex>

enum class eOBSStatus : int
{
	Idle,        // never used / disconnected cleanly
	Working,     // a connect/command is in flight
	Recording,   // StartRecord acknowledged
	Stopped,     // StopRecord acknowledged
	Error,       // last command failed (see LastMessage)
};

class COBSClient
{
public:
	// Connection settings (filled from the INI)
	std::string Host = "127.0.0.1";
	int Port = 4455;
	std::string Password = "";

	// Fire-and-forget recording control. Safe to call from the game thread;
	// the actual network work runs on a detached worker.
	void StartRecording();
	void StopRecording();

	// Synchronous connectivity probe (used by the "Test Connection" menu item).
	// Runs on its own worker too; result lands in Status()/LastMessage().
	void TestConnection();

	eOBSStatus Status() const { return m_status.load(); }
	bool IsRecording() const { return m_status.load() == eOBSStatus::Recording; }
	std::string LastMessage();

private:
	std::atomic<eOBSStatus> m_status{ eOBSStatus::Idle };
	std::mutex m_msgMutex;
	std::string m_lastMessage;
	std::mutex m_cmdMutex; // serialises overlapping commands

	void SetMessage(const std::string& msg);

	// Runs the whole connect -> auth -> single request cycle. requestType is
	// "StartRecord" / "StopRecord" / "" (connect-only probe).
	void RunCommand(const std::string& requestType, eOBSStatus successStatus);
};

inline COBSClient g_OBS;
