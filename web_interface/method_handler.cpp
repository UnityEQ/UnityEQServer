#include "web_interface.h"
#include "method_handler.h"
#include "remote_call.h"

extern WorldServer *worldserver;
extern std::map<std::string, std::pair<int, MethodHandler>> authorized_methods;
extern std::map<std::string, MethodHandler> unauthorized_methods;

/* Web Interface:register_authorized_methods */
void register_authorized_methods()
{
	authorized_methods["WebInterface.Authorize"] = std::make_pair(0, handle_method_token_auth);
	authorized_methods["World.ListZones"] = std::make_pair(10, handle_method_world);
	authorized_methods["World.GetZoneDetails"] = std::make_pair(10, handle_method_world);
	authorized_methods["Zone.Subscribe"] = std::make_pair(10, handle_method_zone);
	authorized_methods["Zone.Unsubscribe"] = std::make_pair(10, handle_method_zone);
	authorized_methods["Zone.GetInitialEntityPositions"] = std::make_pair(10, handle_method_zone);
	authorized_methods["Zone.MoveEntity"] = std::make_pair(10, handle_method_zone); 
	authorized_methods["Zone.Action"] = std::make_pair(10, handle_method_zone);
	authorized_methods["Zone.GetEntityAttributes"] = std::make_pair(10, handle_method_zone);
	authorized_methods["Zone.SetEntityAttribute"] = std::make_pair(10, handle_method_zone);
	authorized_methods["World.GetFileContents"] = std::make_pair(10, handle_method_world);
	authorized_methods["World.SaveFileContents"] = std::make_pair(10, handle_method_world);
	authorized_methods["World.OpcodeToClient"] = std::make_pair(0, handle_method_world_to_client);
}

void register_unauthorized_methods()
{
	unauthorized_methods["WebInterface.Authorize"] = handle_method_token_auth;
}

void register_methods()
{
	register_authorized_methods();
	register_unauthorized_methods();
}

void handle_method_token_auth(per_session_data_eqemu *session, rapidjson::Document &document, std::string &method)
{
	CheckParams(1, "[token]");
	
	session->auth = document["params"][(rapidjson::SizeType)0].GetString();
	if (!CheckTokenAuthorization(session)) {
		WriteWebCallResponseBoolean(session, document, false, false);
	} else {
		WriteWebCallResponseBoolean(session, document, true, false);
	}
}

void handle_method_world(per_session_data_eqemu *session, rapidjson::Document &document, std::string &method) {
	CheckParams(0, "[]");
	VerifyID();
	CalculateSize();
	WriteWebProtocolPacket();
}

void handle_method_zone(per_session_data_eqemu *session, rapidjson::Document &document, std::string &method) {
	CheckParams(2, "[zone_id, instance_id]");
	VerifyID();
	CalculateSize();
	WriteWebProtocolPacket();
}

void handle_method_world_to_client(per_session_data_eqemu *session, rapidjson::Document &document, std::string &method) {
	CheckParams(5, "[zone_id, instance_id, opcode, size, data]");
	VerifyID();
	CalculateSize();
	ServerPacket *pack = new ServerPacket(ServerOP_WIRemoteOpcodeFromClient, sz);
	pack->WriteUInt32((uint32)id.size()); \
	pack->WriteString(id.c_str()); \
	pack->WriteUInt32((uint32)session->uuid.size()); \
	pack->WriteString(session->uuid.c_str()); \
	pack->WriteUInt32((uint32)method.size()); \
	pack->WriteString(method.c_str()); \
	auto &params = document["params"]; \
	auto params_sz = params.Size(); \
	pack->WriteUInt32(params_sz); \
	for(rapidjson::SizeType i = 0; i < params_sz; ++i) { \
		auto &param = params[(rapidjson::SizeType)i]; \
		pack->WriteUInt32((uint32)strlen(param.GetString())); \
		pack->WriteString(param.GetString()); \
	} \
	worldserver->SendPacket(pack); \
	safe_delete(pack); \
}