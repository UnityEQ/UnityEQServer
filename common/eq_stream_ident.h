#ifndef EQSTREAMIDENT_H_
#define EQSTREAMIDENT_H_

#include "eq_stream.h"
#include "timer.h"
#include <vector>
#include <queue>
#include <memory>

#define STREAM_IDENT_WAIT_MS 10000

class OpcodeManager;
class StructStrategy;
class EQStreamInterface;

class EQStreamIdentifier {
public:
	~EQStreamIdentifier();

	//registration interface.
	void RegisterPatch(const EQStream::Signature &sig, const char *name, OpcodeManager ** opcodes, const StructStrategy *structs);

	//registration interface.
	void RegisterWebPatch(const EQStream::Signature &sig, const char *name, OpcodeManager ** opcodes, const StructStrategy *structs);


	//main processing interface
	void Process();
	void AddStream(std::shared_ptr<EQStream> &eqs);
	void AddStream(std::shared_ptr<EQWebStream> &eqs);
	EQStreamInterface *PopIdentified();

protected:

	//registered patches..
	class Patch {
	public:
		std::string				name;
		EQStream::Signature		signature;
		OpcodeManager **		opcodes;
		const StructStrategy *structs;
	};
	std::vector<Patch *> m_patches;	//we own these objects.

	//registered patches..
	class WebPatch {
	public:
		std::string				name;
		EQStream::Signature		signature;
		OpcodeManager **		opcodes;
		const StructStrategy *structs;
	};
	std::vector<WebPatch *> m_webpatches;	//we own these objects.



	//pending streams..
	class Record {
	public:
		Record(std::shared_ptr<EQStream> s);
		std::shared_ptr<EQStream> stream;		//we own this
		Timer expire;
	};

		//pending streams..
	class WebRecord {
	public:
		WebRecord(std::shared_ptr<EQWebStream> s);
		std::shared_ptr<EQWebStream> stream;		//we own this
		Timer expire;
	};

	std::vector<Record> m_streams;	//we own these objects, and the streams contained in them.
	std::vector<WebRecord> m_webstreams;	//we own these objects, and the streams contained in them.

	std::queue<EQStreamInterface *> m_identified;	//we own these objects
};


#endif /*EQSTREAMIDENT_H_*/
