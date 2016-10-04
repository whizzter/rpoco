// This header provides dynamic and templatized JSON serialization on
// top of the RPOCO type information.

#ifndef __INCLUDED_RPOCOJSON_HPP__
#define __INCLUDED_RPOCOJSON_HPP__

#pragma once

#include <rpoco/rpoco.hpp>
#include <iostream>
#include <sstream>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

namespace rpocojson {
	// Functions to parse the istream or string into the templatized target
	// these functions will return true if the parsing was successful.
	// Optional support exists for the parser to skip over C/C++ style comments.
	// By default UTF16 surrogate decoding is done so that the UTF8 strings
	// has full codepoints instead of surrogate pairs.
	template<typename X> bool parse(std::istream &in,X &x,bool allow_c_comments,bool utf16_to_utf8);
	template<typename X> bool parse(std::string &str,X &x,bool allow_c_comments,bool utf16_to_utf8);
	// A function to convert an RPOCO compatible structure to a JSON string.
	template<typename X> std::string to_json(X &x);
	// a catch-all class to read in arbitrary data from JSON fields.
	class json_value;

	// create a UTF8 sequence from a unicode codepoint
	// X is the type of our output
	template<typename X> void dump_utf8(X &x,uint32_t c) {
		if (c<0x80) {
			// plain ascii are below 128
			x.push_back((char)c);
		} else {
			// recursive call to count and produce a heading byte indicating the needed number of bytes
			struct dump {
				void dmp(X &x,int used,unsigned int rest) {
					// test if the rest of the bits fits inside the UTF8 count mask
					if ( rest&(used|~0xff) ) {
						// if not, we need more bits, try again with the 6 bottom bits chopped of and a new mask.
						dmp(x,used|(used>>1),rest>>6);
						// dump out our bits afterwardss
						x.push_back((char)(0x80|(rest&0x3f)));
					} else {
						// dump out the count mask and initial bits
						x.push_back((char)(((used<<1)|rest )&0xff));
					}
				}
			} d;
			d.dmp(x,0xc0,c);
		}
	}
	// decode a UTF8 sequence into a codepoint
	// X is our input stream type
	template<typename X> int read_utf8(X &x) {
		int out=x.get();
		if (out==EOF)
			return EOF;
		// simple ascii code
		if (!(out&0x80)) {
			return out;
		}
		// start with continuation mask
		int mask=0xc0;
		// size the bitmask
		while(mask!=0xff) {
			if ( ((mask<<1)&0xff) == (out&mask) )
				break; // is all bits but the lowest of the mask set?
			mask|=mask>>1; // no match, widen the mask.
		}
		// invalid masks to read, too long or continuation mask
		if (mask==0xff || mask==0xc0)
			return EOF; // got a continuation byte or an filled mask, return error as EOF
		out&=~mask;
		// dump the UTF8 bytes
		while(mask&0x20) {
			int next=x.get();
			if (next==EOF)
				return EOF; // mid-character EOF, return EOF
			if (0x80!=(next&0xc0))
				return EOF; // EOF is extra char data isn't continuation.
			out=(out<<6)|(next&0x3f);
			mask = (mask<<1)&0xff;
		}
		return out;
	}

	// the public JSON parsing function
	// X is the type of the RPOCO conforming target data type that will receive the root JSON data object.
	// utf16 to utf8 translates utf16 surrogate pairs to utf8 codepoints
	template<typename X> bool parse(std::istream &in,X &x,bool allow_c_comments = false,bool utf16_to_utf8 = true) {
		// an internal class with the actual logic acting as a rpoco visitor
		struct json_parser : public rpoco::visitor {
			// validity indicator, used for early exiting after errors
			bool ok = true;
			// the input
			std::istream *ins;
			// temporary string object used during various phases of the parsing
			std::string tmp;
			// allow C/C++ style comments within JSON literals
			bool allow_c_comments;
			// decode utf16 surrogates
			bool utf16_to_utf8;

			// constructor to take the options and input for the parser.
			json_parser(std::istream &ins,bool allow_c_comments = false,bool utf16_to_utf8 = true) {
				this->ins=&ins;
				this->allow_c_comments = allow_c_comments;
				this->utf16_to_utf8 = utf16_to_utf8;
			}
			// skip non-spaces (and comments if that is enabled)
			void skip() {
				while (ok) {
					if (std::isspace(ins->peek())) {
						ins->get();
						continue;
					}
					if (allow_c_comments && ins->peek() == '/') {
						ins->get(); // eat '/'
						switch (ins->peek()) { // what kind of comment do we have
						case '/' :
							// single line comment, eat until carriage return,linefeed or form feed
							while (true) {
								int c = ins->peek();
								if (c == 13 || c == 10 || c == 12 || c==EOF) {
									break;
								} else {
									ins->get();
									continue;
								}
							}
							continue;
						case '*' :
							// multiline comment
							{
								ins->get(); // eat '*'
								int last = 0;
								while (ok) {
									int c = ins->get();
									if (c == EOF) {
										// EOF inside comment leads to a syntax error.
										ok = false;
										break;
									} else if (last == '*'&&c == '/') {
										// end of multiline comment found.
										break;
									} else {
										last = c;
									}
								}
								if (ok)
									continue;
								break;
							}
						default:
							// syntax error in JSON
							ok = false;
							break;
						}
					}
					// not a comment
					break;
				}
			}
			// production functions are invalid to be called by the visitor during parsing.
			virtual void produce_start(rpoco::visit_type vt) {
				abort(); // should not be called
			}
			// production functions are invalid to be called by the visitor during parsing.
			virtual void produce_end(rpoco::visit_type vt) {
				abort(); // should not be called
			}
			// the peek function hints at what kind of objects can be consumed.
			virtual rpoco::visit_type peek() {
				skip(); // skip any spaces and comments so we can identify the token based on the first character
				// first check digits
				if (std::isdigit(ins->peek()))
					return rpoco::vt_number;
				switch(ins->peek()) {
				case '{' : // object start
					return rpoco::vt_object;
				case '[' : // array start
					return rpoco::vt_array;
				case '\"' : // string start
					return rpoco::vt_string;
				case 't' : // true start
				case 'f' : // false start
					return rpoco::vt_bool;
				case 'n' : // null start
					return rpoco::vt_null;
				case '-' : // negative number start
					return rpoco::vt_number;
				default: // invalid object start, stop parsing.
					ok=false;
					return rpoco::vt_error;
				}
			}
			// a match function used to skip the remainder of known constants (null/true/false)
			void match(const char *s) {
				for (int i=0;s[i];i++)
					ok&= (ins->get()==s[i]);
			}
			// null parsing
			virtual void visit_null() {
				skip();
				match("null");
			}
			// object and array parsing function ("consumption")
			// the visit type is checked and then the 
			virtual bool consume(rpoco::visit_type vt,std::function<void (std::string&)> g) {
				skip();
				if (vt==rpoco::vt_object) {
					// JSON object
					ok&=ins->get()=='{';
					if (!ok) return true;
					skip();
					if (ins->peek() != '}')
						while(ok) {
							// first validate and get the property name string
							skip();
							ok&=ins->peek()=='"';
							if (!ok) break; // stop if not a string property
							// now reset the tmp string
							tmp.clear();
							// read in the property name
							visit(tmp);
							// ensure that we have a correct separator :
							skip();
							ok&=ins->get()==':';
							if (!ok) break; // stop if syntax error
							skip();
							// invoke the consumer function with the key to parse the rest
							g(tmp);
							tmp.clear();
							skip();
							if (ins->peek()=='}')
								break; // end of object
							ok&=ins->get()==',';
							skip();
						}
					if (ok)
						ins->get(); // read '}'
				} else if (vt==rpoco::vt_array) {
					// JSON array
					ok&=ins->get()=='[';
					if (!ok) return true;
					tmp.clear();
					if (ins->peek()!=']')
						while(ok) {
							skip();
							// just let the consumer read in the members
							g(tmp);
							skip();
							// and detect the trailing ']' or check the separator comma
							if (ins->peek()==']')
								break;
							ok&=ins->get()==',';
							skip();
						}
					if (ok)
						ins->get(); // get end ']'
				} else abort(); // consume can only be called for objects and arrays
				return true;
			}
			// boolean parsing
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
			// a dual purpose function to parse JSON numbers
			// and convert them to a double of the current locale since the
			// standard built in double parsing functions are locale dependant
			void consume_frac_and_exp() {
				// if we have a decimal point consume it.
				if (ins->peek()=='.') {
					// eat the dot
					ins->get();
					// but append the locale decimal point
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
			}
			// double number visitor
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
				consume_frac_and_exp();
				if (ok)
					dv=std::stod(tmp);
				tmp.clear();
			}
			// integer visitor, has a fast path for obvious integers and also
			// a checking path that parses the number as a double and then
			// checks that the result is still an integer (or fails the parsing)
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
				// now a fallback in case we got something more complex than a simple integer.
				int c=ins->peek();
				if (c=='.' || c=='e' || c=='E') {
					// not encoded as a just a simple integer, do a complex fallback path.
					// first dump the integer prefix
					tmp=std::to_string(acc);
					// then consume the rest of the number info
					consume_frac_and_exp();
					if (ok) {
						double dv=std::stod(tmp);
						iv=(int)dv;
						// verify that the number was a valid integer.
						ok=((double)iv==dv);
					}
					tmp.clear();
				}
			}
			// reads a single UTF16 character inside a string, used by
			// the string parsing to convert the result to a
			// UTF8 representation without codepoints.
			int readSimpleCharacter() {
				int c=read_utf8(*ins);
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
							for (int i=0;i<4;i++) {
								int tmp=ins->get();
								c=c<<4;
								if ( '0'<=tmp && tmp<='9')
									c|=tmp-'0';
								else if ( 'A'<=tmp && tmp<='F')
									c|=tmp-'A'+10;
								else if ( 'a'<=tmp && tmp<='f')
									c|=tmp-'a'+10;
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
			// Parse strings to UTF8, converts UTF16 surrogate pairs
			// to full codepoints if the option is enabled.
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
					if(utf16_to_utf8 && c>=0xd800 && c<0xdc00) {
						// surrogate pair encountered and conversion enabled.
						int c2=readSimpleCharacter();
						if (!(c2>=0xdc00 && c2<0xe000)) {
							// invalid secondary surrogate pair character
							ok=false;
							return;
						}
						c=( ((c&0x3ff)<<10)|(c2&0x3ff) )+0x10000;
					}
					dump_utf8(str,c);
				}
				// eat "
				ins->get();
			}
			// fixed size string
			virtual void visit(char *str,size_t sz) {
				std::string tmp;
				visit(tmp);
				if (tmp.size()>=sz) {
					ok=false;
					str[0] = 0;
				} else {
					memcpy(str,tmp.data(),sz);
					str[sz] = 0;
				}
			}
		};
		// init parser object and then use it to visit the target
		json_parser parser(in,allow_c_comments,utf16_to_utf8);
		rpoco::visit<X>(parser,x);
		parser.skip();
		return parser.ok && EOF==in.peek();
	}
	// small utility to wrap strings into streams if needed for parsing.
	template<typename X> bool parse(std::string &str,X &x,bool allow_c_comments = false,bool utf16_to_utf8 = true) {
		return parse(std::istringstream(str),x,allow_c_comments,utf16_to_utf8);
	}

	// function to dump an arbitrary RPOCO oobject as a string containing a JSON object
	template<typename X> std::string to_json(X &x) {
		// the json_writer extends the rpoco::visitor struct to receive
		// data as the generic visitation code visits the structure.
		struct json_writer : public rpoco::visitor {
			// the output string
			std::string out;
			// state stack to keep track of terminators at each level.
			enum wrstate {
				def   =0x1, // default
				objid =0x2, // inside object expecting a propname
				objval=0x3, // inside object expecting a value
				objnxt=0x4, // inside object either expecting term or a new propname
				ary   =0x5, // inside array
				arynxt=0x6, // inside array either expecting term or a new value
				end   =0x1000 // termination
			};
			std::vector<wrstate> state;
			// initialize state with a dummy constructor
			json_writer() {
				state={def};
			}
			// pre-value function call to dump the appropriate separator
			// characters when the value is a member of a object literal or array
			void pre(bool str) {
				if (state.back() == end) {
					// cannot write objects if we're at an end-state
					abort();
				}
				if (state.back()==objnxt) {
					// with another property being added to an object,
					// add a ',' and advance state
					out.append(",");
					state.back()=objid;
				}
				if (state.back()==objid && !str) {
					// object property names must be strings
					abort();
				} else if (state.back()==arynxt) {
					// append commas when expecting another value in
					// an array
					out.append(",");
				}
			}
			// post-value, update state
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
			// called when entering a object or array
			// responsible for updating the state stack
			virtual void produce_start(rpoco::visit_type vt) {
				switch(vt) {
				case rpoco::vt_object :
					// update the previous level
					pre(false);
					// print and setup the object
					out.append("{");
					state.push_back(objid);
					break;
				case rpoco::vt_array :
					// update the previous level
					pre(false);
					// print and setup the array
					out.append("[");
					state.push_back(ary);
					break;
				default:
					abort();
				}
			}
			// visitor interface to query production or consumption mode
			virtual bool consume(rpoco::visit_type vt,std::function<void (std::string&)> g) {
				// the generator does not consume anything, return false
				return false;
			}
			// called to produce the object end
			virtual void produce_end(rpoco::visit_type vt) {
				switch(vt) {
				case rpoco::vt_object :
					// sanity check
					if (state.back()!=objid && state.back()!=objnxt)
						abort();
					// exit object
					state.pop_back();
					out.append("}");
					// call parent state to indicate end-of-value
					post();
					break;
				case rpoco::vt_array :
					// sanity check
					if (state.back()!=ary && state.back()!=arynxt)
						abort();
					// exit array
					state.pop_back();
					out.append("]");
					// call parent state to indicate end-of-value
					post();
					break;
				default:
					abort();
				}
			}
			// boolean visitor
			virtual void visit(bool& bv) {
				// sanity check
				if (state.back()==objid)
					abort();
				// inform parent of value start
				pre(false);
				// dump value
				out.append(bv?"true":"false");
				// inform parent of value end
				post();
			}
			// double visitor
			virtual void visit(double& dv) {
				// sanity check
				if (state.back()==objid)
					abort();
				// inform parent of value start
				pre(false);
				// dump double string
				char buf[500];
#ifdef _MSC_VER
				sprintf_s(buf,sizeof(buf),"%.17g",dv);
#else
				snprintf(buf,sizeof(buf),"%.17g",dv);
#endif
				// replace commas in locales where
				// they appear.
				for (int i=0;buf[i];i++)
					if (buf[i]==',')
						buf[i]='.';
				out.append(buf);
				// inform parent of value end
				post();
			}
			// integer visitor
			virtual void visit(int& iv) {
				// sanity check
				if (state.back()==objid)
					abort();
				// inform parent of value start
				pre(false);
				// dump integer string
				out.append(std::to_string(iv));
				// inform parent of value end
				post();
			}
			// get 1 hex character
			char toHex(int c) {
				c&=0xf;
				if (c<10)
					return c+'0';
				else
					return c-10+'A';
			}
			// dump JSON UTF16 codepoint
			void dumpUniEscape( int c) {
				out.append("\\u");
				out.push_back( toHex(c>>12) );
				out.push_back( toHex(c>>8) );
				out.push_back( toHex(c>>4) );
				out.push_back( toHex(c) );
			}
			// visit null terminated string
			virtual void visit(char *str,size_t sz) {
				// TODO: encapsulate to avoid copy and construction
				for (size_t i=0;i<sz;i++)
					if (!str[i])
						sz=i;
				std::string tmp(str,sz);
				visit(tmp);
			}
			virtual void visit(std::string &str) {
				struct strsrc {
					std::string *p;
					int idx;
					strsrc(std::string *s) {
						idx=0;
						p=s;
					}
					int peek() {
						if (idx==p->size())
							return EOF;
						return ((*p)[idx])&0xff;
					}
					int get() {
						int c=peek();
						if (c!=EOF) idx++;
						return c;
					}
				}src(&str);
				pre(true);
				out.append("\"");
				// TODO: proper string encoding
				//out.append(str);
				while(src.peek()!=EOF) {
					int c=read_utf8(src);
					//printf("Encoding:%d (%c)\n",c,c);
					switch(c) {
					case '\"' :
						out.append("\\\"");
						continue;
					case '\\' :
						out.append("\\\\");
						continue;
					//case '/' :
					//	out.append("\\/");
					//	continue;
					case '\b' :
						out.append("\\b");
						continue;
					case '\f' :
						out.append("\\f");
						continue;
					case '\n' :
						out.append("\\n");
						continue;
					case '\r' :
						out.append("\\r");
						continue;
					case '\t' :
						out.append("\\t");
						continue;
					}
					if (c>=32 && c<127) {
						out.push_back((char)c);
					} else if (c>0x10ffff) {
						abort(); // out of range character
					} else if (c>0xffff) {
						c-=0x10000;
						dumpUniEscape( 0xd800 | ((c>>10)&0x3ff) );
						dumpUniEscape( 0xdc00 | (c&0x3ff) );
					} else {
						dumpUniEscape( c );
					}
				}
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

	// a generic catch-all class that can have any kind of JSON data.
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
	// create a specialization visitor for rpocojson::json_value to enable
	// it to work coherently with the rest of the rpoco types.
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

