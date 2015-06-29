/*	EQEMu: Everquest Server Emulator
	Copyright (C) 2001-2014 EQEMu Development Team (http://eqemulator.net)

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; version 2 of the License.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY except by those people which sell it, which
	are required to give you total support for your newly bought product;
	without even the implied warranty of MERCHANTABILITY or FITNESS FOR
	A PARTICULAR PURPOSE. See the GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the Free Software
	Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/
#include "../common/global_define.h"
#include <iostream>
#include <string.h>
#include <stdio.h>
#include <iomanip>
#include <time.h>
#include <stdlib.h>
#include <stdarg.h>
#include <list>
#include <map>

#include "../common/servertalk.h"
#include "../common/packet_functions.h"
#include "../common/md5.h"
#include "../common/SocketLib/Base64.h"
#include "../common/packet_dump.h"
#include "worldserver.h"
#include "web_interface.h"

extern std::map<std::string, per_session_data_eqemu*> sessions;

WorldServer::WorldServer(std::string shared_key)
: WorldConnection(EmuTCPConnection::packetModeWebInterface, shared_key.c_str()){
	pTryReconnect = true;
}

WorldServer::~WorldServer(){
}

void WorldServer::OnConnected(){
	Log.Out(Logs::General, Logs::WebInterface_Server, "Connected to World.");
	WorldConnection::OnConnected();
}

void WorldServer::Process(){
	WorldConnection::Process();
	if (!Connected())
		return;

	ServerPacket *pack = nullptr;
	while((pack = tcpc.PopPacket())){
		Log.Out(Logs::General, Logs::Netcode, "Received Opcode: %4X", pack->opcode);
		switch(pack->opcode) {
			case 0: { break; }
			case ServerOP_KeepAlive: { break; }
			case ServerOP_WIRemoteCallResponse: {
				char *id = nullptr;
				char *session_id = nullptr;
				char *error = nullptr;

				id = new char[pack->ReadUInt32() + 1];
				pack->ReadString(id);

				session_id = new char[pack->ReadUInt32() + 1];
				pack->ReadString(session_id);

				error = new char[pack->ReadUInt32() + 1];
				pack->ReadString(error);

				uint32 param_count = pack->ReadUInt32();
				std::map<std::string, std::string> params;
				for(uint32 i = 0; i < param_count; ++i) {
					char *first = new char[pack->ReadUInt32() + 1];
					pack->ReadString(first);

					char *second = new char[pack->ReadUInt32() + 1];
					pack->ReadString(second);

					params[first] = second;

					safe_delete_array(first);
					safe_delete_array(second);
				}

				//send the response to client...
				rapidjson::StringBuffer s;
				rapidjson::Writer<rapidjson::StringBuffer> writer(s);

				writer.StartObject();
				writer.String("id");
				if(strlen(id) == 0) {
					writer.Null();
				} else {
					writer.String(id);
				}

				if(strlen(error) != 0) {
					writer.String("error");
					writer.Bool(true);

					writer.String("result");
					writer.String(error);
				} else {
					writer.String("error");
					writer.Null();
					writer.String("result");
					writer.StartObject();
					auto iter = params.begin();
					while(iter != params.end()) {
						writer.String(iter->first.c_str());
						writer.String(iter->second.c_str());
						++iter;
					}
					writer.EndObject();
				}
				writer.EndObject();

				if(sessions.count(session_id) != 0) {
					per_session_data_eqemu *session = sessions[session_id];
					session->send_queue->push_back(s.GetString());
				}

				safe_delete_array(id);
				safe_delete_array(session_id);
				safe_delete_array(error);
				break;
			}

			case ServerOP_WIRemoteCallToClient:
			{
				char *session_id = nullptr;
				char *method = nullptr;

				session_id = new char[pack->ReadUInt32() + 1];
				pack->ReadString(session_id);

				method = new char[pack->ReadUInt32() + 1];
				pack->ReadString(method);

				uint32 param_count = pack->ReadUInt32();
				std::vector<std::string> params(param_count);
				for(uint32 i = 0; i < param_count; ++i) {
					char *p = new char[pack->ReadUInt32() + 1];
					pack->ReadString(p);
					params[i] = p;

					safe_delete_array(p);
				}

				rapidjson::StringBuffer s;
				rapidjson::Writer<rapidjson::StringBuffer> writer(s);

				writer.StartObject();
				writer.String("id");
				writer.Null();

				writer.String("method");
				writer.String(method);

				writer.String("params");
				writer.StartArray();

				for(uint32 i = 0; i < param_count; ++i) {
					writer.String(params[i].c_str());
				}

				writer.EndArray();
				
				writer.EndObject();

				if(sessions.count(session_id) != 0) {
					per_session_data_eqemu *session = sessions[session_id];
					session->send_queue->push_back(s.GetString());
				}

				safe_delete_array(session_id);
				safe_delete_array(method);
				break;
			}

			case ServerOP_WIRemoteOpcodeToClient:
			{
				char *session_id = nullptr;
				session_id = new char[pack->ReadUInt32() + 1];
				pack->ReadString(session_id);

				char *method = nullptr;
				method = new char[pack->ReadUInt32() + 1];
				pack->ReadString(method);


				uint32 zone_id = pack->ReadUInt32();
				uint32 instance_id = pack->ReadUInt32();
				uint32 opcode = pack->ReadUInt32();
				uint32 datasize = pack->ReadUInt32();
				char *p = new char[datasize];
				pack->ReadData(p, datasize);
				rapidjson::StringBuffer s;
				rapidjson::Writer<rapidjson::StringBuffer> writer(s);

				writer.StartObject();
				writer.String("id");
				writer.Null();

				writer.String("method");
				writer.String(method);

				writer.String("zoneid");
				writer.Uint(zone_id);

				writer.String("instance_id");
				writer.Uint(instance_id);

				writer.String("opcode");
				writer.Uint(opcode);
				writer.String("datasize");
				writer.Uint(datasize);

				std::string enc;
				Base64::encode(p, datasize, enc, false);


				writer.String("data");
				writer.String(enc.c_str());

				writer.EndObject();

				if(sessions.count(session_id) != 0) {
					per_session_data_eqemu *session = sessions[session_id];
					session->send_queue->push_back(s.GetString());
				}

				safe_delete_array(session_id);
				safe_delete_array(method);
				safe_delete_array(p);

				break;
			}
		}
	}

	safe_delete(pack);
	return;
}
