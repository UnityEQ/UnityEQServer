#include "../common/global_define.h"
#include "web_interface.h"
#include "world_config.h"
#include "clientlist.h"
#include "zonelist.h"
#include "zoneserver.h"
#include "remote_call.h"
#include "../common/eqemu_logsys.h"
#include "../common/md5.h"
#include "../common/emu_tcp_connection.h"
#include "../common/packet_dump.h"
#include "../common/SocketLib/Base64.h"

extern ClientList client_list;
extern ZSList zoneserver_list;
extern EQWebStreamFactory eqwsf;
extern std::map<std::string, RemoteCallHandler> remote_call_methods;

WebInterfaceConnection::WebInterfaceConnection()
{
	stream = 0;
	authenticated = false;
}

void WebInterfaceConnection::SetConnection(EmuTCPConnection *inStream)
{
	if(stream)
	{
		Log.Out(Logs::General, Logs::WebInterface_Server, "Incoming WebInterface Connection while we were already connected to a WebInterface.");
		stream->Disconnect();
	}

	stream = inStream;

	authenticated = false;
}

bool WebInterfaceConnection::Process()
{
	if (!stream || !stream->Connected())
		return false;

	ServerPacket *pack = 0;

	while((pack = stream->PopPacket()))
	{
		if (!authenticated)
		{
			if (WorldConfig::get()->SharedKey.length() > 0)
			{
				if (pack->opcode == ServerOP_ZAAuth && pack->size == 16)
				{
					uint8 tmppass[16];

					MD5::Generate((const uchar*) WorldConfig::get()->SharedKey.c_str(), WorldConfig::get()->SharedKey.length(), tmppass);

					if (memcmp(pack->pBuffer, tmppass, 16) == 0)
						authenticated = true;
					else
					{
						struct in_addr in;
						in.s_addr = GetIP();
						Log.Out(Logs::General, Logs::Error, "WebInterface authorization failed.");
						ServerPacket* pack = new ServerPacket(ServerOP_ZAAuthFailed);
						SendPacket(pack);
						delete pack;
						Disconnect();
						return false;
					}
				}
				else
				{
					struct in_addr in;
					in.s_addr = GetIP();
					Log.Out(Logs::General, Logs::Error, "WebInterface authorization failed.");
					ServerPacket* pack = new ServerPacket(ServerOP_ZAAuthFailed);
					SendPacket(pack);
					delete pack;
					Disconnect();
					return false;
				}
			}
			else
			{
				Log.Out(Logs::General, Logs::Error, "**WARNING** You have not configured a world shared key in your config file. You should add a <key>STRING</key> element to your <world> element to prevent unauthorized zone access.");
				authenticated = true;
			}
			delete pack;
			continue;
		}
		switch(pack->opcode)
		{
			case 0:
				break;

			case ServerOP_KeepAlive:
			{
				// ignore this
				break;
			}
			case ServerOP_ZAAuth:
			{
				Log.Out(Logs::General, Logs::Error, "Got authentication from WebInterface when they are already authenticated.");
				break;
			}
			case ServerOP_WIRemoteCall:
			{
				char *id = nullptr;
				char *session_id = nullptr;
				char *method = nullptr;

				id = new char[pack->ReadUInt32() + 1];
				pack->ReadString(id);

				session_id = new char[pack->ReadUInt32() + 1];
				pack->ReadString(session_id);

				method = new char[pack->ReadUInt32() + 1];
				pack->ReadString(method);

				uint32 param_count = pack->ReadUInt32();
				std::vector<std::string> params;
				for(uint32 i = 0; i < param_count; ++i) {
					char *p = new char[pack->ReadUInt32() + 1];
					pack->ReadString(p);
					params.push_back(p);
					safe_delete_array(p);
				}

				if (remote_call_methods.count(method) != 0) {
					auto f = remote_call_methods[method];
					f(method, session_id, id, params);
				}

				safe_delete_array(id);
				safe_delete_array(session_id);
				safe_delete_array(method);
				break;
			}
			case ServerOP_WIClientSessionResponse: {
				uint32 zone_id = pack->ReadUInt32();
				uint32 instance_id = pack->ReadUInt32();
				
				ZoneServer *zs = nullptr;
				if(instance_id != 0) {
					zs = zoneserver_list.FindByInstanceID(instance_id);
				} else {
					zs = zoneserver_list.FindByZoneID(zone_id);
				}

				if(zs) {
					ServerPacket *npack = new ServerPacket(ServerOP_WIClientSessionResponse, pack->size - 8);
					memcpy(npack->pBuffer, pack->pBuffer + 8, pack->size - 8);

					zs->SendPacket(npack);
					safe_delete(npack);
				}

				break;
			}

			case ServerOP_WIRemoteOpcodeFromClient: {
				char *id = nullptr;
				char *session_id = nullptr;
				char *method = nullptr;

				id = new char[pack->ReadUInt32() + 1];
				pack->ReadString(id);

				session_id = new char[pack->ReadUInt32() + 1];
				pack->ReadString(session_id);

				method = new char[pack->ReadUInt32() + 1];
				pack->ReadString(method);

				uint32 param_count = pack->ReadUInt32();
				std::vector<std::string> params;
				for(uint32 i = 0; i < param_count; ++i) {
					char *p = new char[pack->ReadUInt32() + 1];
					pack->ReadString(p);
					params.push_back(p);
					safe_delete_array(p);
				}
				uint32 zone_id = std::stoi(params[0]);
				uint32 instance_id = std::stoi(params[1]);
				

				ZoneServer *zs = nullptr;
				if(instance_id != 0) {
					zs = zoneserver_list.FindByInstanceID(instance_id);
				} else {
					zs = zoneserver_list.FindByZoneID(zone_id);
				}

				if(zs) {
					ServerPacket *npack = new ServerPacket(ServerOP_WIRemoteOpcodeFromClient, pack->size);
					memcpy(npack->pBuffer, pack->pBuffer, pack->size);

					zs->SendPacket(npack);
					safe_delete(npack);
				}
				else
				{
					if(eqwsf.FindSessionByID(session_id))
					{
						eqwsf.ProcessIncomingPacket(session_id, params);
					}
					else
					{
						eqwsf.Push(session_id, params);
					}
				}

				safe_delete_array(id);
				safe_delete_array(session_id);
				safe_delete_array(method);

				break;
			}

			default:
			{
				Log.Out(Logs::General, Logs::Error, "Unknown ServerOPcode from WebInterface 0x%04x, size %d", pack->opcode, pack->size);
				DumpPacket(pack->pBuffer, pack->size);
				break;
			}
		}

		delete pack;
	}
	return(true);
}

bool WebInterfaceConnection::SendPacket(ServerPacket* pack)
{
	if(!stream)
		return false;

	return stream->SendPacket(pack);
}