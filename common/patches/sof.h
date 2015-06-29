#ifndef SOF_H_
#define SOF_H_

#include "../struct_strategy.h"

class EQStreamIdentifier;
class EQWebStreamIdentifier;

namespace SoF {

	//these are the only public member of this namespace.
	extern void Register(EQStreamIdentifier &into);
	extern void Reload();



	//you should not directly access anything below..
	//I just dont feel like making a seperate header for it.

	class Strategy : public StructStrategy {
	public:
		Strategy();

	protected:

		virtual std::string Describe() const;
		virtual const ClientVersion GetClientVersion() const;

		//magic macro to declare our opcode processors
		#include "ss_declare.h"
		#include "sof_ops.h"
	};

};

#endif /*SOF_H_*/
