#ifndef __INCLUDED_RPOCOJSON_HPP__
#define __INCLUDED_RPOCOJSON_HPP__

#pragma once

#include <rpoco/rpoco.hpp>

namespace rpocojson {
	template<typename X> std::string to_json(X *x) {
		struct json_writer : public rpoco::visitor {
			enum wrstate {
				def   =0x1,
				objid =0x2,
				objval=0x3,
				objnxt=0x4,
				ary   =0x5,
				arynxt=0x6,
				end   =0x1000
			};
			std::string out;
			std::vector<wrstate> state;
			json_writer() {
				state={def};
			}
			void pre(bool str) {
				if (state.back()==end)
					abort();
				if (state.back()==objnxt) {
					out.append(",");
					state.back()=objid;
				}
				if (state.back()==objid && !str) {
					abort();
				} else if (state.back()==arynxt) {
					out.append(",");
				}
			}
			void post() {
				switch(state.back()) {
				case ary :
					state.back()=arynxt;
					break;
				case objid :
					out.append(":");
					state.back()=objval;
					break;
				case objval :
					state.back()=objnxt;
					break;
				case def:
					state.back()=end;
					break;
				}
			}
			virtual void visit(rpoco::visit_type vt) {
				switch(vt) {
				case rpoco::object_start :
					pre(false);
					out.append("{");
					state.push_back(objid);
					break;
				case rpoco::object_end :
					if (state.back()!=objid && state.back()!=objnxt)
						abort();
					state.pop_back();
					out.append("}");
					post();
					break;
				case rpoco::array_start :
					pre(false);
					out.append("[");
					state.push_back(ary);
					break;
				case rpoco::array_end :
					if (state.back()!=ary && state.back()!=arynxt)
						abort();
					state.pop_back();
					out.append("]");
					post();
					break;
				}
			}
			
			virtual void visit(int& iv) {
				if (state.back()==objid)
					abort();
				pre(false);
				out.append(std::to_string(iv));
				post();
			}
			virtual void visit(std::string &str) {
				pre(true);
				out.append("\"");
				// TODO: proper string encoding
				out.append(str);
				out.append("\"");
				post();
			}
			virtual void visit(rpoco::type_info *ti,void *p) {
				if (!p) {
					pre(false);
					out.append("null");
					post();
					return;
				}
				visit(rpoco::object_start);
				for (int i=0;i<ti->size();i++) {
					visit((*ti)[i]->name());
					(*ti)[i]->visit(this,p);
				}
				visit(rpoco::object_end);
			}
		};
		json_writer writer;

		rpoco::visit(&writer,x);
		return writer.out;
	}
}

#endif // __INCLUDED_RPOCOJSON_HPP__

