#ifndef __INCLUDED_RPOCOJSON_HPP__
#define __INCLUDED_RPOCOJSON_HPP__

#pragma once

#include <rpoco/rpoco.hpp>
#include <iostream>
#include <sstream>
#include <stdint.h>

namespace rpocojson {
	template<typename X> void dumpUTF8(X &x,uint32_t c) {
		if (c<0x80) {
			x.push_back((char)c);
		} else {
			struct dump {
				void dmp(X &x,int used,unsigned int rest) {
					if ( rest&(used|~0xff) ) {
						// need more bits, try again with the 6 bottom bits chopped of and a new mask.
						dmp(x,used|(used>>1),rest>>6);
						// dump out our continuation
						x.push_back((char)(0x80|(rest&0x3f)));
					} else {
						// dump out the signature bits.
						x.push_back((char)(((used<<1)|rest )&0xff));
					}
				}
			} d;
			d.dmp(x,0xc0,c);
		}
	}
	template<typename X> int readUTF8(X &x) {
		int out=x.get();
		if (out==EOF)
			return EOF;
		if (!(out&0x80)) {
			return out;
		}
		int mask=0xc0; // start with continuation mask
		while(mask!=0xff) {
			if ( (mask<<1)&0xff == (out&mask) )
				break; // is all bits but the lowest of the mask set?
			mask|=mask>>1; // no match, widen the mask.
		}
		if (mask==0xff || mask==0xc0)
			return EOF; // got a continuation byte or an filled mask, return error as EOF
		out&=~mask;
		while(mask&0x40) {
			int next=x.get();
			if (next==EOF)
				return EOF; // mid-character EOF, return EOF
			if (0x80!=(next&0xc0))
				return EOF; // EOF is extra char data isn't continuation.
			out=(out<<6)|(next&0x3f);
			mask<<=1;
		}
		return out;
	}
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
							skip();
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
							skip();
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
				tmp.clear();
				// consume negative sign
				if (ins->peek()=='-') {
					tmp.push_back(ins->get());
				}
				// consume either a solitary 0 or a sequence of digits
				if (ins->peek()=='0') {
					tmp.push_back(ins->get());
				} else if (std::isdigit(ins->peek())) {
					while(std::isdigit(ins->peek()))
						tmp.push_back(ins->get());
				} else {
					ok=false;
					return;
				}
				// if we have a decimal point consume it.
				if (ins->peek()=='.') {
					//tmp.push_back(ins->get());
					tmp.append( localeconv()->decimal_point );
					while(std::isdigit(ins->peek()))
						tmp.push_back(ins->get());
				}
				// do we have an exponent?
				if (ins->peek()=='e' || ins->peek()=='E') {
					tmp.push_back(ins->get());
					if (ins->peek()=='+' || ins->peek()=='-') {
						tmp.push_back(ins->get());
					}
					if(!std::isdigit(ins->peek()))
						ok=false;
					while(std::isdigit(ins->peek()))
						tmp.push_back(ins->get());
				}
				if (ok)
					dv=std::stod(tmp);
				tmp.clear();
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
			int readSimpleCharacter() {
				int c=readUTF8(*ins);
				if (c=='\\') {
					switch(c=ins->get()) {
					case '\"' : case '\\' : case '/' :
						break; // use the character found directly.
					case 'b' :
						c='\b';
						break;
					case 'f' :
						c='\f';
						break;
					case 'n' :
						c='\n';
						break;
					case 'r' :
						c='\r';
						break;
					case 't' :
						c='\t';
						break;
					case 'u' : {
							c=0;
							for (int i=0;i<4;i--) {
								int tmp=ins->get();
								c=c<<4;
								if ( '0'<=tmp && tmp<='9')
									c|=tmp-'0';
								if ( 'A'<=tmp && tmp<='F')
									c|=tmp-'A';
								else if ( 'a'<=tmp && tmp<='f')
									c|=tmp-'a';
								else {
									ok=false;
									return EOF;
								}
							}
						} break;
					default:
						ok=false;
						return EOF;
					}
				}
				return c;
			}
			virtual void visit(std::string &str) {
				skip();
				ok&=ins->get()=='"';
				if (!ok) return;
				while(ok) {
					int c=ins->peek();
					if (c==EOF || c<32) {
						// EOF or control code encountered
						ok=false;
						return;
					}
					if (c=='"')
						break;
					c=readSimpleCharacter();
					if (c==EOF) {
						ok=false;
						break;
					}
					if(c>=0xd800 && c<0xdc00) {
						// surrogate pair encountered
						int c2=readSimpleCharacter();
						if (!(c2>=0xdc00 && c2<0xe000)) {
							ok=false;
							return;
						}
						c=( ((c&0x3ff)<<10)|(c2&0x3ff) )+0x10000;
					}
					dumpUTF8(str,c);
				}
				// eat "
				ins->get();
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

	class json_value {
		rpoco::visit_type m_type;
		union {
			bool b;
			double n;
			std::string *s;
			std::vector<json_value> *a;
			std::map<std::string,json_value> *o;
		}data;
		void copy_from(const json_value &other) {
			set_type(other.m_type);
			switch(other.m_type) {
			case rpoco::vt_array :
				*data.a=*other.data.a;
				break;
			case rpoco::vt_object :
				*data.o=*other.data.o;
				break;
			case rpoco::vt_string :
				*data.s=*other.data.s;
				break;
			case rpoco::vt_bool :
				data.b=other.data.b;
				break;
			case rpoco::vt_number :
				data.n=other.data.n;
				break;
			}
		}
	public:
		json_value() {
			m_type=rpoco::vt_null;
		}
		json_value(double v) {
			m_type=rpoco::vt_number;
			data.n=v;
		}
		json_value(const json_value &other) {
			m_type=rpoco::vt_null;
			copy_from(other);
		}
		json_value& operator=(const json_value &other) {
			copy_from(other);
			return *this;
		}
		void set_null() {
			set_type(rpoco::vt_null);
		}
		json_value& operator=(double d) {
			set_type(rpoco::vt_number);
			data.n=d;
			return *this;
		}
		json_value& operator=(bool b) {
			set_type(rpoco::vt_bool);
			data.b=b;
			return *this;
		}
		json_value& operator=(std::string s) {
			set_type(rpoco::vt_string);
			*data.s=s;
			return *this;
		}
		rpoco::visit_type type() {
			return m_type;
		}
		bool to_bool() {
			if (m_type==rpoco::vt_bool) {
				return data.b;
			} else {
				return false;
			}
		}
		double to_number() {
			if (m_type==rpoco::vt_number) {
				return data.n;
			} else {
				return 0;
			}
		}
		std::string to_string() {
			if (m_type==rpoco::vt_string) {
				return *data.s;
			} else {
				return std::string("");
			}
		}
		std::map<std::string,json_value>* map() {
			if (m_type!=rpoco::vt_object)
				return 0;
			return data.o;
		}
		std::vector<json_value>* array() {
			if (m_type!=rpoco::vt_array)
				return 0;
			return data.a;
		}
		void set_type(rpoco::visit_type toType) {
			if (m_type!=toType) {
				switch(m_type) {
				case rpoco::vt_string :
					delete data.s;
					break;
				case rpoco::vt_array :
					delete data.a;
					break;
				case rpoco::vt_object :
					delete data.o;
					break;
				}
				switch(toType) {
				case rpoco::vt_array :
					data.a=new std::vector<json_value>();
					break;
				case rpoco::vt_object :
					data.o=new std::map<std::string,json_value>();
					break;
				case rpoco::vt_string :
					data.s=new std::string();
					break;
				case rpoco::vt_number :
					data.n=0;
					break;
				case rpoco::vt_bool :
					data.b=false;
					break;
				}
			}
			m_type=toType;
		}
		~json_value() {
			set_type(rpoco::vt_null);
		}
	};
}

namespace rpoco {
	template<> struct visit<rpocojson::json_value> { visit (visitor &v,rpocojson::json_value &jv) {
		if (v.peek()==vt_none) {
			switch(jv.type()) {
			case vt_null : {
					v.visit_null();
				} break;
			case vt_number : {
					double d=jv.to_number();
					v.visit(d);
				} break;
			case vt_bool : {
					bool b=jv.to_bool();
					v.visit(b);
				} break;
			case vt_string : {
					std::string str=jv.to_string();
					v.visit(str);
				} break;
			case vt_object :
				rpoco::visit<std::map<std::string,rpocojson::json_value>>(v,*jv.map());
				//v.produce_start(rpoco::vt_object);
				//jv.object_items([&v](std::string &n,rpocojson::json_value &sv){
				//	v.visit(n);
				//	visit(v,sv);
				//});
				//v.produce_end(rpoco::vt_array);
				break;
			case vt_array : {
					rpoco::visit<std::vector<rpocojson::json_value>>(v,*jv.array());
					//std::string tmp("");
					//v.produce_start(rpoco::vt_object);
					//jv.array_items([&v,&tmp](int idx,rpocojson::json_value &sv){
					//	v.visit(tmp);
					//	visit(v,sv);
					//});
					//v.produce_end(rpoco::vt_array);
				} break;
			default:
				abort();
			}
			return;
		} else {
			switch(v.peek()) {
			case vt_null : {
					v.visit_null();
					jv.set_null();
				} break;
			case vt_number : {
					double d;
					v.visit(d);
					jv=d;
				} break;
			case vt_bool : {
					bool b;
					v.visit(b);
					jv=b;
				} break;
			case vt_string : {
					std::string s;
					v.visit(s);
					jv=s;
				} break;
			case vt_object : {
					jv.set_type(rpoco::vt_object);
					rpoco::visit<std::map<std::string,rpocojson::json_value>>(v,*jv.map());
				} break;
			case vt_array : {
					jv.set_type(rpoco::vt_array);
					rpoco::visit<std::vector<rpocojson::json_value>>(v,*jv.array());
				} break;
			}
		}
	}};

}

#endif // __INCLUDED_RPOCOJSON_HPP__

