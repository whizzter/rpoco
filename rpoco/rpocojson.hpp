#ifndef __INCLUDED_RPOCOJSON_HPP__
#define __INCLUDED_RPOCOJSON_HPP__

#pragma once

#include <rpoco/rpoco.hpp>
#include <iostream>
#include <sstream>

namespace rpocojson {
	template<typename X> bool parse(std::istream &in,X &x) {
		struct json_parser : public rpoco::visitor {
			std::istream *ins;
			bool ok=true;
			std::string tmp;

			json_parser(std::istream &ins) {
				this->ins=&ins;
			}
			void skip() {
				if (ok)
					while(std::isspace(ins->peek())) ins->get();
			}
			
			virtual bool visit_start(rpoco::visit_type vt) {
				skip();
				printf("VISITSOME start:%d\n",vt);
				switch(vt) {
				case rpoco::object:
					ok&=ins->get()=='{';
					break;
				case rpoco::array :
					ok&=ins->get()=='[';
					break;
				}
				return false;
			}
			virtual void visit_end(rpoco::visit_type vt) {
				abort(); // should not be called
			}
			virtual void visit_null() {
				printf("visitnull\n");
			}
			virtual void visit_end(rpoco::visit_type vt,std::function<void (std::string&)> g) {
				//printf("VISITSOME end:%d %c\n",vt,ins->peek());
				skip();
				if (vt==rpoco::object) {
					if (ins->peek()!='}')
						while(ok) {
							ok&=ins->peek()=='"';
							if (!ok) break;
							tmp.clear();
							visit(tmp);
							skip();
							ok&=ins->get()==':';
							if (!ok) break;
							skip();
							g(tmp);
							tmp.clear();
							skip();
							if (ins->peek()=='}')
								break;
							ok&=ins->get()==',';
						}
					ins->get();
				} else if (vt==rpoco::array) {
					abort();
				} else abort();
			}
			virtual void visit(int &iv) {
				skip();
				int sign=1;
				int acc=0;
				if (ins->peek()=='+') {
					ins->get();
				} else if (ins->peek()=='-') {
					ins->get();
					sign=-1;
				}
				while(std::isdigit(ins->peek())) {
					acc=acc*10 + ( ins->get()-'0' );
				}
				iv=sign*acc;
			}
			virtual void visit(std::string &str) {
				skip();
				printf("VISITSTR %c\n",ins->peek());
				ok&=ins->get()=='"';
				if (!ok) return;
				while(ok) {
					int c=ins->get();
					if (c==EOF) {
						ok=false;
						break;
					}
					if (c=='"')
						break;
					str.push_back((char)c);
				}
				printf("ComplStr:%s\n",str.c_str());
			}
		};
		
		json_parser parser(in);
		rpoco::visit(parser,&x);
		return parser.ok;
	}
	template<typename X> bool parse(std::string &str,X &x) {
		return parse(std::istringstream(str),x);
	}

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
			virtual bool visit_start(rpoco::visit_type vt) {
				switch(vt) {
				case rpoco::object :
					pre(false);
					out.append("{");
					state.push_back(objid);
					break;
				case rpoco::array :
					pre(false);
					out.append("[");
					state.push_back(ary);
					break;
				}
				return true;
			}
			virtual void visit_end(rpoco::visit_type vt,std::function<void (std::string&)> g) {
				// should not be called!
				abort();
			}
			virtual void visit_end(rpoco::visit_type vt) {
				switch(vt) {
				case rpoco::object :
					if (state.back()!=objid && state.back()!=objnxt)
						abort();
					state.pop_back();
					out.append("}");
					post();
					break;
				case rpoco::array :
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
			virtual void visit_null() {
				pre(false);
				out.append("null");
				post();
			}
//			virtual void visit(rpoco::field_provider *ti,void *p) {
//				if (!p) {
//					pre(false);
//					out.append("null");
//					post();
//					return;
//				}
//				if (visit_start(rpoco::object)) {
//					for (int i=0;i<ti->size();i++) {
//						visit((*ti)[i]->name());
//						(*ti)[i]->visit(*this,p);
//					}
//				}
//				visit_end(rpoco::object,[](std::string &x){});
//			}
		};
		json_writer writer;

		rpoco::visit(writer,x);
		return writer.out;
	}
}

#endif // __INCLUDED_RPOCOJSON_HPP__

