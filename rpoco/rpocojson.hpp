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
			
			virtual void produce_start(rpoco::visit_type vt) {
				abort(); // should not be called
			}
			virtual void produce_end(rpoco::visit_type vt) {
				abort(); // should not be called
			}
			virtual rpoco::visit_type peek() {
				skip();
				if (std::isdigit(ins->peek()))
					return rpoco::vt_number;
				switch(ins->peek()) {
				case '{' :
					return rpoco::vt_object;
				case '[' :
					return rpoco::vt_array;
				case '\"' :
					return rpoco::vt_string;
				case 't' :
				case 'f' :
					return rpoco::vt_bool;
				case 'n' :
					return rpoco::vt_null;
				case '-' :
					return rpoco::vt_number;
				default:
					ok=false;
					return rpoco::vt_error;
				}
			}
			void match(const char *s) {
				for (int i=0;s[i];i++)
					ok&= (ins->get()==s[i]);
			}
			virtual void visit_null() {
				skip();
				match("null");
			}
			virtual bool consume(rpoco::visit_type vt,std::function<void (std::string&)> g) {
				skip();
				if (vt==rpoco::vt_object) {
					ok&=ins->get()=='{';
					if (!ok) return true;
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
							skip();
						}
					ins->get();
				} else if (vt==rpoco::vt_array) {
					ok&=ins->get()=='[';
					if (!ok) return true;
					tmp.clear();
					if (ins->peek()!=']')
						while(ok) {
							g(tmp);
							skip();
							if (ins->peek()==']')
								break;
							ok&=ins->get()==',';
							skip();
						}
					ins->get();
					//abort();
				} else abort();
				return true;
			}
			virtual void visit(bool &bv) {
				skip();
				if (ins->peek()=='t') {
					bv=true;
					match("true");
				} else {
					bv=false;
					match("false");
				}
			}
			virtual void visit(double &dv) {
				skip();
				abort(); // TODO: implement double parsing
			}
			virtual void visit(int &iv) {
				skip();
				int sign=1;
				int acc=0;
				if (ins->peek()=='-') {
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
			}
		};
		
		json_parser parser(in);
		rpoco::visit<X>(parser,x);
		return parser.ok;
	}
	template<typename X> bool parse(std::string &str,X &x) {
		return parse(std::istringstream(str),x);
	}

	template<typename X> std::string to_json(X &x) {
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
			virtual void produce_start(rpoco::visit_type vt) {
				switch(vt) {
				case rpoco::vt_object :
					pre(false);
					out.append("{");
					state.push_back(objid);
					break;
				case rpoco::vt_array :
					pre(false);
					out.append("[");
					state.push_back(ary);
					break;
				default:
					abort();
				}
			}
			virtual bool consume(rpoco::visit_type vt,std::function<void (std::string&)> g) {
				// does not consume
				return false;
			}
			virtual void produce_end(rpoco::visit_type vt) {
				switch(vt) {
				case rpoco::vt_object :
					if (state.back()!=objid && state.back()!=objnxt)
						abort();
					state.pop_back();
					out.append("}");
					post();
					break;
				case rpoco::vt_array :
					if (state.back()!=ary && state.back()!=arynxt)
						abort();
					state.pop_back();
					out.append("]");
					post();
					break;
				default:
					abort();
				}
			}
			
			virtual void visit(bool& bv) {
				if (state.back()==objid)
					abort();
				pre(false);
				out.append(bv?"true":"false");
				post();
			}
			virtual void visit(double& dv) {
				if (state.back()==objid)
					abort();
				pre(false);
				out.append(std::to_string(dv));
				post();
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
			virtual rpoco::visit_type peek() {
				return rpoco::vt_none;
			}
			virtual void visit_null() {
				pre(false);
				out.append("null");
				post();
			}
		};
		json_writer writer;

		rpoco::visit<X>(writer,x);
		return writer.out;
	}
}

#endif // __INCLUDED_RPOCOJSON_HPP__

