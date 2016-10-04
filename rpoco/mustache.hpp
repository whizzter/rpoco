// This header file implements a rudimentary mustache template renderer on top
// of the RPOCO serialization functionality.

#ifndef __INCLUDED_RPOCO_MUSTACHE_HPP__
#define __INCLUDED_RPOCO_MUSTACHE_HPP__

#pragma once

#include <rpoco/rpoco.hpp>

namespace rpoco {
	namespace mustache {

		class multifragment;
		
		// parse parses mustache templates into a tree of fragments, that tree can
		// later be rendered out to an expanded structure from RPOCO structures.
		multifragment parse(std::string &src);
		
		static void dumpchars(std::function<void(char)> &f,const char *buf,int sz) {
			for (int i=0;i<sz;i++) {
				f(buf[i]);
			}
		}
		
		// fragment is the basic type of nodes from the mustache template
		class fragment {
			friend multifragment parse(std::string &src);
		protected:
			// all fragment nodes has a parent pointer (mostly used during parsing)
			fragment *parent;
		public:
			virtual ~fragment() {};

			// the renderFragment function is overriden by fragment nodes and called
			// recursively during template rendering, if custom output is wanted
			// instead of a string like in the render helper then just implement the
			// lambda function and push out characters somewhere else.
			virtual void renderFragment(std::function<void(char)> &out, std::vector<rpoco::query*> &q)=0;
			
			// a render helper function that dumps all characters to a string that
			// can then be used.
			template <typename T> std::string render(T &data) {
				std::string out;
				auto query=rpoco::make_query(data);
				std::vector<rpoco::query*> querystack;
				querystack.push_back(&query);
				std::function<void(char)> outfun=[&out](char c){ out.push_back(c); };
				this->renderFragment(outfun,querystack);
				return out;
			}
		};
		
		// Multifragment is a container that contains multiple subfragments
		class multifragment : public fragment {
			friend multifragment parse(std::string &src);
			std::vector<std::shared_ptr<fragment>> sub;
		public:
			//virtual ~multifragment(){}
			virtual void renderFragment(std::function<void(char)> &out, std::vector<rpoco::query*> &q){
				for (int i=0;i<sub.size();i++) {
					sub[i]->renderFragment(out,q);
				}
			}
		};
		
		// valuefragments refers to fragments that retrieves data from a structure
		class valuefragment : public fragment {
			friend multifragment parse(std::string &src);
			std::string valuename;
			bool escape;
		public:
			virtual void renderFragment(std::function<void(char)> &out, std::vector<rpoco::query*> &q){
				bool found=false;
				for (int i=q.size()-1;!found && i>=0;i--) {
					q[i]->find(valuename,[this,&found,&out](rpoco::query &vq){
						found=true;
						if (vq.kind()==rpoco::vt_string) {
							std::string strval=vq.get();
							if (!escape)
								dumpchars(out,strval.data(),strval.size());
							else
								for (int i=0;i<strval.size();i++) {
									switch(strval[i]) {
									case '<' :
										dumpchars(out,"&lt;",4);
										break;
									case '>' :
										dumpchars(out,"&gt;",4);
										break;
									case '\"' :
										dumpchars(out,"&quot;",6);
										break;
									case '\'' :
										dumpchars(out,"&#039;",6);
										break;
									case '&' :
										dumpchars(out,"&amp;",5);
										break;
									default:
										out(strval[i]);
										break;
									}
								}
						} else if (vq.kind()==rpoco::vt_number) {
							if (int *ip=vq) {
								auto is=std::to_string(*ip);
								dumpchars(out,is.c_str(),is.size());
							} else if (double *dp=vq) {
								auto ds=std::to_string(*dp);
								dumpchars(out,ds.c_str(),ds.size());
							}
						} else {
							printf("Vt kind:%d not handled\n",vq.kind());
						}
					});
				}
			}
		};

		// control fragments is used to control control flow, such as iteratin
		// fragments used to output data from vectors or inverted fragments that
		// hides data on truthyness.
		class ctlfragment : public fragment {
			friend multifragment parse(std::string &src);
			std::string ctlname;
			bool invert;
			multifragment sub;
		public:
			virtual void renderFragment(std::function<void(char)> &out,std::vector<rpoco::query*> &q) {
				bool found=false;
				for(int i=q.size()-1;!found && i>=0;i--) {
					q[i]->find(ctlname,[&](rpoco::query &vq){
						found=true;
						bool truthy=false;
						if (vq.kind()==rpoco::vt_array) {
							if (vq.size() && !invert) {
								q.push_back(nullptr);
								vq.all([&](int idx,rpoco::query &subq){
									(q.back())=&subq;
									sub.renderFragment(out,q);
								});
								q.pop_back();
								return;
							}
							truthy = vq.size()!=0;
						} else if (vq.kind()==rpoco::vt_number) {
							if (int* ip=vq) {
								truthy=*ip!=0;
							} else if (double *dp=vq) {
								truthy=*ip!=0;
							} else {
								return;
							}
						} else if (vq.kind()==rpoco::vt_bool) {
							truthy=*((bool*)vq);
						} else if (vq.kind()==rpoco::vt_string) {
							truthy=0!=vq.get().size();
						} else {
							return; // don't know how to handle this value type
						}
						if ( truthy ^ invert ) {
							sub.renderFragment(out,q);
						}
					});
				}
				if (invert && !found) {
					sub.renderFragment(out,q);
				}
			}
		};

		// textfragments are plain old boring fragments of plain text.
		class textfragment : public fragment {
			friend multifragment parse(std::string &src);
			std::string data;
		public:
			//virtual ~textfragment(){}
			virtual void renderFragment(std::function<void(char)> &out, std::vector<rpoco::query*> &q){
				dumpchars(out,data.data(),data.size());
			}
		};
		
		// the parsing function that turns templates into a fragment tree.
		multifragment parse(std::string &src) {
			multifragment parsed;
			std::string beginTag="{{";
			std::string endTag="}}";
			std::string ueEndTag="}}}";
			fragment *cur=&parsed;
			bool multistate=true;
			
			for (size_t i=0;i<src.size();) {
				if (multistate) {
					if (0==src.compare(i,beginTag.size(),beginTag)) {
						i+=beginTag.size();
						//std::cout <<"Tag at:" <<src.substr(i)<<"\n";
						if (i==src.size()) {
							printf("PRemature EOF\n");
							return parsed;
						}
						char kind=src[i];
						if (kind=='#' || kind=='!' || kind=='^' || kind=='/' || kind=='{') {
							i++;
						} else {
							kind=0;
						}
						size_t end=src.find(kind=='{'?ueEndTag:endTag,i);
						if (std::string::npos==end) {
							// Mark end somehow?!
							printf("End tag not found!!\n");
							return parsed;
						} else {
							std::string tag=src.substr(i,end-i);
							switch(kind) {
							case '{' :
							case 0 : {
									valuefragment * pvf=new valuefragment();
									pvf->escape=kind==0;
									pvf->valuename=tag;
									pvf->parent=cur;
									std::shared_ptr<fragment> valuefrag(pvf);
									((multifragment*)cur)->sub.push_back(valuefrag);
								} break;
							case '#' :
							case '^' : {
									ctlfragment * cf=new ctlfragment();
									cf->ctlname=tag;
									cf->invert=kind=='^';
									cf->sub.parent=cf;
									cf->parent=cur;
									std::shared_ptr<fragment> ctlfrag(cf);
									((multifragment*)cur)->sub.push_back(ctlfrag);
									cur=&(cf->sub);
								} break;
							case '/' : {
									// check for parse error on mismatching tags?
									cur=cur->parent->parent;
								} break;
							default:
								printf("Unknown kind:%d %c\n",kind,kind);
								abort();
							}
							i=end+2;
							continue;
						}
					} else {
						fragment *next=new textfragment();
						std::shared_ptr<fragment> tf(next);
						((multifragment*)cur)->sub.push_back(tf);
						next->parent=cur;
						multistate=false;
						cur=next;
						continue;
					}
				} else {
					// TODO: triple mustache!
					if (0==src.compare(i,beginTag.size(),beginTag)) {
						// if tag starts within text fragment, let's back out of it.
						//std::cout <<"When text.. Tag at:" <<src.substr(i)<<" curkind:"<<cur->kind()<<"\n";
						cur=cur->parent;
						multistate=true;
						continue;
					}
					((textfragment*)cur)->data.push_back(src[i]);
					i++;
					continue;
				}
			}
			return parsed;
		}
	};
};

#endif //  __INCLUDED_RPOCO_MUSTACHE_HPP__
