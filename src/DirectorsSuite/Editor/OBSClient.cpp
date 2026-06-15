#include "OBSClient.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <bcrypt.h>
#include <thread>
#include <vector>
#include <random>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "bcrypt.lib")

// ---------------------------------------------------------------------------
// Tiny helpers: base64, SHA256, minimal JSON scraping
// ---------------------------------------------------------------------------

namespace {

const char* kB64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string Base64Encode(const unsigned char* data, size_t len)
{
	std::string out;
	out.reserve(((len + 2) / 3) * 4);
	for (size_t i = 0; i < len; i += 3) {
		unsigned v = data[i] << 16;
		if (i + 1 < len) v |= data[i + 1] << 8;
		if (i + 2 < len) v |= data[i + 2];
		out.push_back(kB64[(v >> 18) & 0x3F]);
		out.push_back(kB64[(v >> 12) & 0x3F]);
		out.push_back((i + 1 < len) ? kB64[(v >> 6) & 0x3F] : '=');
		out.push_back((i + 2 < len) ? kB64[v & 0x3F] : '=');
	}
	return out;
}

// SHA256 -> base64, via Windows CNG (bcrypt)
std::string Sha256Base64(const std::string& input)
{
	BCRYPT_ALG_HANDLE hAlg = nullptr;
	BCRYPT_HASH_HANDLE hHash = nullptr;
	std::string result;

	if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) {
		return result;
	}
	if (BCryptCreateHash(hAlg, &hHash, nullptr, 0, nullptr, 0, 0) == 0) {
		if (BCryptHashData(hHash, (PUCHAR)input.data(), (ULONG)input.size(), 0) == 0) {
			unsigned char digest[32];
			if (BCryptFinishHash(hHash, digest, sizeof(digest), 0) == 0) {
				result = Base64Encode(digest, sizeof(digest));
			}
		}
		BCryptDestroyHash(hHash);
	}
	BCryptCloseAlgorithmProvider(hAlg, 0);
	return result;
}

// obs-websocket auth: base64(sha256(base64(sha256(password+salt)) + challenge))
std::string BuildAuthString(const std::string& password, const std::string& salt, const std::string& challenge)
{
	std::string secret = Sha256Base64(password + salt);
	return Sha256Base64(secret + challenge);
}

// Extract the string value of "key" from a flat-ish JSON blob. Good enough for
// the small, well-formed messages obs-websocket sends.
bool JsonFindString(const std::string& json, const std::string& key, std::string& out)
{
	std::string needle = "\"" + key + "\"";
	size_t p = json.find(needle);
	if (p == std::string::npos) return false;
	p = json.find(':', p + needle.size());
	if (p == std::string::npos) return false;
	p++;
	while (p < json.size() && (json[p] == ' ' || json[p] == '\t')) p++;
	if (p >= json.size() || json[p] != '"') return false;
	p++;
	std::string value;
	while (p < json.size() && json[p] != '"') {
		if (json[p] == '\\' && p + 1 < json.size()) { value.push_back(json[p + 1]); p += 2; continue; }
		value.push_back(json[p]);
		p++;
	}
	out = value;
	return true;
}

bool JsonFindInt(const std::string& json, const std::string& key, int& out)
{
	std::string needle = "\"" + key + "\"";
	size_t p = json.find(needle);
	if (p == std::string::npos) return false;
	p = json.find(':', p + needle.size());
	if (p == std::string::npos) return false;
	p++;
	while (p < json.size() && (json[p] == ' ' || json[p] == '\t')) p++;
	bool neg = (p < json.size() && json[p] == '-');
	if (neg) p++;
	int v = 0;
	bool any = false;
	while (p < json.size() && json[p] >= '0' && json[p] <= '9') {
		v = v * 10 + (json[p] - '0');
		p++;
		any = true;
	}
	if (!any) return false;
	out = neg ? -v : v;
	return true;
}

// ---------------------------------------------------------------------------
// Minimal blocking WebSocket client (client role = frames are masked)
// ---------------------------------------------------------------------------

class WsConn
{
public:
	~WsConn() { Close(); }

	bool Connect(const std::string& host, int port, std::string& err)
	{
		m_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (m_sock == INVALID_SOCKET) { err = "socket() failed"; return false; }

		// 5 second send/recv timeouts so a missing OBS never hangs the worker
		DWORD to = 5000;
		setsockopt(m_sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&to, sizeof(to));
		setsockopt(m_sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&to, sizeof(to));

		addrinfo hints{};
		hints.ai_family = AF_INET;
		hints.ai_socktype = SOCK_STREAM;
		addrinfo* res = nullptr;
		std::string portStr = std::to_string(port);
		if (getaddrinfo(host.c_str(), portStr.c_str(), &hints, &res) != 0 || !res) {
			err = "cannot resolve " + host;
			return false;
		}
		int rc = connect(m_sock, res->ai_addr, (int)res->ai_addrlen);
		freeaddrinfo(res);
		if (rc != 0) { err = "connection refused (is OBS + obs-websocket running?)"; return false; }

		return Handshake(host, port, err);
	}

	bool SendText(const std::string& payload, std::string& err)
	{
		std::vector<unsigned char> frame;
		frame.push_back(0x81); // FIN + text opcode

		size_t len = payload.size();
		if (len <= 125) {
			frame.push_back((unsigned char)(0x80 | len)); // MASK bit set
		}
		else if (len <= 0xFFFF) {
			frame.push_back(0x80 | 126);
			frame.push_back((unsigned char)((len >> 8) & 0xFF));
			frame.push_back((unsigned char)(len & 0xFF));
		}
		else {
			frame.push_back(0x80 | 127);
			for (int i = 7; i >= 0; i--) frame.push_back((unsigned char)((len >> (i * 8)) & 0xFF));
		}

		unsigned char mask[4];
		for (int i = 0; i < 4; i++) mask[i] = (unsigned char)(m_rng() & 0xFF);
		frame.insert(frame.end(), mask, mask + 4);

		for (size_t i = 0; i < len; i++) {
			frame.push_back((unsigned char)payload[i] ^ mask[i & 3]);
		}

		return SendAll((const char*)frame.data(), frame.size(), err);
	}

	// Reads one server text frame (handles fragmentation/control frames minimally)
	bool RecvText(std::string& out, std::string& err)
	{
		out.clear();
		for (;;) {
			unsigned char hdr[2];
			if (!RecvAll((char*)hdr, 2, err)) return false;

			bool fin = (hdr[0] & 0x80) != 0;
			int opcode = hdr[0] & 0x0F;
			bool masked = (hdr[1] & 0x80) != 0; // servers never mask, but tolerate it
			uint64_t len = hdr[1] & 0x7F;

			if (len == 126) {
				unsigned char ext[2];
				if (!RecvAll((char*)ext, 2, err)) return false;
				len = ((uint64_t)ext[0] << 8) | ext[1];
			}
			else if (len == 127) {
				unsigned char ext[8];
				if (!RecvAll((char*)ext, 8, err)) return false;
				len = 0;
				for (int i = 0; i < 8; i++) len = (len << 8) | ext[i];
			}

			unsigned char mask[4] = { 0,0,0,0 };
			if (masked && !RecvAll((char*)mask, 4, err)) return false;

			// Sanity cap: obs-websocket messages are tiny, so never honour an
			// absurd frame length from the wire (avoids a huge allocation if the
			// server misbehaves or the stream desyncs).
			if (len > 16ull * 1024 * 1024) { err = "frame too large"; return false; }

			std::string payload;
			payload.resize((size_t)len);
			if (len > 0 && !RecvAll(&payload[0], (size_t)len, err)) return false;
			if (masked) {
				for (size_t i = 0; i < payload.size(); i++) payload[i] ^= mask[i & 3];
			}

			if (opcode == 0x8) { err = "server closed connection"; return false; } // close
			if (opcode == 0x9) { continue; } // ping - ignore (OBS doesn't need a pong for our short sessions)
			if (opcode == 0xA) { continue; } // pong

			out += payload;
			if (fin) return true; // text/continuation complete
		}
	}

	void Close()
	{
		if (m_sock != INVALID_SOCKET) {
			closesocket(m_sock);
			m_sock = INVALID_SOCKET;
		}
	}

private:
	SOCKET m_sock = INVALID_SOCKET;
	std::mt19937 m_rng{ std::random_device{}() };

	bool SendAll(const char* data, size_t len, std::string& err)
	{
		size_t sent = 0;
		while (sent < len) {
			int n = send(m_sock, data + sent, (int)(len - sent), 0);
			if (n <= 0) { err = "send failed / timed out"; return false; }
			sent += n;
		}
		return true;
	}

	bool RecvAll(char* data, size_t len, std::string& err)
	{
		size_t got = 0;
		while (got < len) {
			int n = recv(m_sock, data + got, (int)(len - got), 0);
			if (n <= 0) { err = "recv failed / timed out"; return false; }
			got += n;
		}
		return true;
	}

	bool Handshake(const std::string& host, int port, std::string& err)
	{
		unsigned char keyBytes[16];
		for (auto& b : keyBytes) b = (unsigned char)(m_rng() & 0xFF);
		std::string key = Base64Encode(keyBytes, sizeof(keyBytes));

		std::string req =
			"GET / HTTP/1.1\r\n"
			"Host: " + host + ":" + std::to_string(port) + "\r\n"
			"Upgrade: websocket\r\n"
			"Connection: Upgrade\r\n"
			"Sec-WebSocket-Key: " + key + "\r\n"
			"Sec-WebSocket-Protocol: obswebsocket.json\r\n"
			"Sec-WebSocket-Version: 13\r\n\r\n";

		if (!SendAll(req.c_str(), req.size(), err)) return false;

		// Read the HTTP response headers up to the blank line
		std::string resp;
		char c;
		while (resp.find("\r\n\r\n") == std::string::npos) {
			int n = recv(m_sock, &c, 1, 0);
			if (n <= 0) { err = "handshake recv failed"; return false; }
			resp.push_back(c);
			if (resp.size() > 4096) break;
		}
		if (resp.find(" 101") == std::string::npos) {
			err = "websocket upgrade rejected";
			return false;
		}
		return true;
	}
};

std::once_flag g_wsaOnce;
void EnsureWinsock()
{
	std::call_once(g_wsaOnce, [] {
		WSADATA wsaData;
		WSAStartup(MAKEWORD(2, 2), &wsaData);
	});
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// COBSClient
// ---------------------------------------------------------------------------

void COBSClient::SetMessage(const std::string& msg)
{
	std::lock_guard<std::mutex> lock(m_msgMutex);
	m_lastMessage = msg;
}

std::string COBSClient::LastMessage()
{
	std::lock_guard<std::mutex> lock(m_msgMutex);
	return m_lastMessage;
}

void COBSClient::RunCommand(const std::string& requestType, eOBSStatus successStatus)
{
	std::lock_guard<std::mutex> lock(m_cmdMutex); // one command at a time
	m_status.store(eOBSStatus::Working);

	EnsureWinsock();

	WsConn conn;
	std::string err;
	if (!conn.Connect(Host, Port, err)) {
		m_status.store(eOBSStatus::Error);
		SetMessage("OBS: " + err);
		return;
	}

	// 1. Hello
	std::string hello;
	if (!conn.RecvText(hello, err)) {
		m_status.store(eOBSStatus::Error);
		SetMessage("OBS: no Hello (" + err + ")");
		return;
	}

	// 2. Build Identify, answering the auth challenge if present
	std::string identify;
	std::string challenge, salt;
	bool needsAuth = JsonFindString(hello, "challenge", challenge) && JsonFindString(hello, "salt", salt);
	if (needsAuth) {
		if (Password.empty()) {
			m_status.store(eOBSStatus::Error);
			SetMessage("OBS: a password is required but none is set in the INI");
			return;
		}
		std::string auth = BuildAuthString(Password, salt, challenge);
		identify = "{\"op\":1,\"d\":{\"rpcVersion\":1,\"authentication\":\"" + auth + "\",\"eventSubscriptions\":0}}";
	}
	else {
		identify = "{\"op\":1,\"d\":{\"rpcVersion\":1,\"eventSubscriptions\":0}}";
	}

	if (!conn.SendText(identify, err)) {
		m_status.store(eOBSStatus::Error);
		SetMessage("OBS: identify failed (" + err + ")");
		return;
	}

	// 3. Identified (op 2) - or a close on bad auth
	std::string identified;
	if (!conn.RecvText(identified, err)) {
		m_status.store(eOBSStatus::Error);
		SetMessage("OBS: authentication failed (wrong password?)");
		return;
	}
	int op = -1;
	JsonFindInt(identified, "op", op);
	if (op != 2) {
		m_status.store(eOBSStatus::Error);
		SetMessage("OBS: not identified (op " + std::to_string(op) + ")");
		return;
	}

	// Connect-only probe
	if (requestType.empty()) {
		m_status.store(successStatus);
		SetMessage("OBS: connected successfully");
		return;
	}

	// 4. Request (op 6)
	std::string reqId = "rdreditor-" + requestType;
	std::string request = "{\"op\":6,\"d\":{\"requestType\":\"" + requestType +
		"\",\"requestId\":\"" + reqId + "\"}}";
	if (!conn.SendText(request, err)) {
		m_status.store(eOBSStatus::Error);
		SetMessage("OBS: request send failed (" + err + ")");
		return;
	}

	// 5. RequestResponse (op 7) - read frames until we see our response
	for (int attempts = 0; attempts < 10; attempts++) {
		std::string resp;
		if (!conn.RecvText(resp, err)) {
			m_status.store(eOBSStatus::Error);
			SetMessage("OBS: no response to " + requestType + " (" + err + ")");
			return;
		}
		int rop = -1;
		JsonFindInt(resp, "op", rop);
		if (rop != 7) continue; // skip events etc.

		int code = 0;
		bool ok = JsonFindInt(resp, "code", code) ? (code == 100) : false;
		// `result` boolean is also present; treat code 100 as success.
		if (ok) {
			m_status.store(successStatus);
			SetMessage(requestType == "StartRecord" ? "OBS: recording started"
				: requestType == "StopRecord" ? "OBS: recording stopped"
				: "OBS: " + requestType + " ok");
		}
		else {
			// Common benign case: starting when already recording, or stopping
			// when not recording. Report but don't treat as fatal.
			std::string comment;
			JsonFindString(resp, "comment", comment);
			m_status.store(eOBSStatus::Error);
			SetMessage("OBS: " + requestType + " failed" + (comment.empty() ? "" : (" - " + comment)));
		}
		return;
	}

	m_status.store(eOBSStatus::Error);
	SetMessage("OBS: no matching response to " + requestType);
}

void COBSClient::StartRecording()
{
	std::thread([this] { RunCommand("StartRecord", eOBSStatus::Recording); }).detach();
}

void COBSClient::StopRecording()
{
	std::thread([this] { RunCommand("StopRecord", eOBSStatus::Stopped); }).detach();
}

void COBSClient::TestConnection()
{
	std::thread([this] { RunCommand("", eOBSStatus::Stopped); }).detach();
}
