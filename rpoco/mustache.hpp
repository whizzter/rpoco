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
		
		struct rendercontext {
			std::vector<rpoco::query*> rstack;
			std::function<void(char)> *out;
			std::function<multifragment*(std::string &name)> pfinder;
			
			bool resolve(std::string &name,std::function<void(rpoco::query&)> q) {
				bool found=false;
				if (name==".") {
					q(*rstack.back());
					return true;
				}
				for (int i=rstack.size()-1;!found && i>=0;i--) {
					found=rstack[i]->find(name,q);
				}
				return found;
			}
			void dumpchars(const char *buf,int sz) {
				for (int i=0;i<sz;i++) {
					(*out)(buf[i]);
				}
			}
		};
		
		// fragment is the basic type of nodes from the mustache template
		class fragment {
			friend multifragment parse(std::string &src);
		protected:
			// all fragment nodes has a parent pointer (mostly used during parsing)
			fragment *parent;
		public:
			//fragment(const fragment& src)=delete;
			//fragment& operator=(const fragment& src)=delete;
			virtual ~fragment() = default;

			// the renderFragment function is overriden by fragment nodes and called
			// recursively during template rendering, if custom output is wanted
			// instead of a string like in the render helper then just implement the
			// lambda function and push out characters somewhere else.
			virtual void renderFragment(rendercontext *)=0;
			
			// a render helper function that dumps all characters to a string that
			// can then be used.
			template <typename T> std::string render(T &data,std::function<multifragment*(std::string &name)> pres=std::function<multifragment*(std::string &name)>() ) {
				std::string out;
				auto query=rpoco::make_query(data);
				rendercontext ctx;
				ctx.rstack.push_back(&query);
				std::function<void(char)> outfun=[&out](char c){ out.push_back(c); };
				ctx.out=&outfun;
				ctx.pfinder=pres;
				this->renderFragment(&ctx);
				return out;
			}
		};
		
		// Multifragment is a container that contains multiple subfragments
		class multifragment : public fragment {
			friend multifragment parse(std::string &src);
			std::vector<std::shared_ptr<fragment>> sub;
		public:
			multifragment()=default;
			multifragment(const multifragment& src)=default;
			multifragment& operator=(const multifragment& src)=default;
			multifragment(multifragment &&src)=default;
			multifragment& operator=(multifragment &&src)=default;
			virtual ~multifragment()=default;

			virtual void renderFragment(rendercontext *ctx){
				for (int i=0;i<sub.size();i++) {
					sub[i]->renderFragment(ctx);
				}
			}
		};
		
		// valuefragments refers to fragments that retrieves data from a structure
		class valuefragment : public fragment {
			friend multifragment parse(std::string &src);
			std::string valuename;
			bool escape;
		public:
			virtual ~valuefragment()=default;
			virtual void renderFragment(rendercontext *ctx){
				//bool found=false;
				//for (int i=q.size()-1;!found && i>=0;i--) {
					//q[i]->find(valuename,
				ctx->resolve(valuename,
					[this,&ctx](rpoco::query &vq){
					//	found=true;
						if (vq.kind()==rpoco::vt_string) {
							std::string strval=vq.get();
							if (!escape)
								ctx->dumpchars(strval.data(),strval.size());
							else
								for (int i=0;i<strval.size();i++) {
									switch(strval[i]) {
									case '<' :
										ctx->dumpchars("&lt;",4);
										break;
									case '>' :
										ctx->dumpchars("&gt;",4);
										break;
									case '\"' :
										ctx->dumpchars("&quot;",6);
										break;
									case '\'' :
										ctx->dumpchars("&#039;",6);
										break;
									case '&' :
										ctx->dumpchars("&amp;",5);
										break;
									default:
										(*ctx->out)(strval[i]);
										break;
									}
								}
						} else if (vq.kind()==rpoco::vt_number) {
							if (int *ip=vq) {
								auto is=std::to_string(*ip);
								ctx->dumpchars(is.c_str(),is.size());
							} else if (double *dp=vq) {
								auto ds=std::to_string(*dp);
								ctx->dumpchars(ds.c_str(),ds.size());
							}
						} else {
							printf("Vt kind:%d not handled\n",vq.kind());
						}
					});
				//}
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
			virtual ~ctlfragment()=default;
			virtual void renderFragment(rendercontext *ctx) {
				//bool found=false;
				//for(int i=q.size()-1;!found && i>=0;i--) {
				
				bool found=ctx->resolve(ctlname,[&](rpoco::query &vq){
						//found=true;
						bool truthy=false;
						if (vq.kind()==rpoco::vt_array) {
							if (vq.size() && !invert) {
								ctx->rstack.push_back(nullptr);
								vq.all([&](int idx,rpoco::query &subq){
									(ctx->rstack.back())=&subq;
									sub.renderFragment(ctx);
								});
								ctx->rstack.pop_back();
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
							sub.renderFragment(ctx);
						}
					});
				//}
				if (invert && !found) {
					sub.renderFragment(ctx);
				}
			}
		};

		// partialfragments are fragments that refere to external file fragments
		class partialfragment : public fragment {
			friend multifragment parse(std::string &src);
			std::string name;
		public:
			virtual ~partialfragment()=default;
			virtual void renderFragment(rendercontext *ctx) {
				if (!ctx->pfinder) {
					printf("no finder!\n");
					return;
				}
				multifragment *sub=ctx->pfinder(name);
				if (!sub)
					return;
				sub->renderFragment(ctx);
			}
		};

		// textfragments are plain old boring fragments of plain text.
		class textfragment : public fragment {
			friend multifragment parse(std::string &src);
			std::string data;
		public:
			virtual ~textfragment()=default;
			virtual void renderFragment(rendercontext *ctx){
				ctx->dumpchars(data.data(),data.size());
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
							return std::move(parsed);
						}
						char kind=src[i];
						if (kind=='#' || kind=='!' || kind=='^' || kind=='/' || kind=='{' || kind=='>') {
							i++;
						} else {
							kind=0;
						}
						size_t end=src.find(kind=='{'?ueEndTag:endTag,i);
						if (std::string::npos==end) {
							// Mark end somehow?!
							printf("End tag not found!!\n");
							return std::move(parsed);
						} else {
							// once the tag kind is known we start trim away spaces from the tag data
							size_t tst=i,tend=end;
							while(tst<tend && isspace(src[tst])) tst++;
							while(tst<tend && isspace(src[tend-1])) tend--;
							// if there's anything left it's a valid tag.
							if (tst<tend) {
								std::string tag=src.substr(tst,tend-tst);
							
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
								case '>' : {
										partialfragment *pf=new partialfragment();
										pf->name=tag;
										pf->parent=cur;
										std::shared_ptr<fragment> valuefrag(pf);
										((multifragment*)cur)->sub.push_back(valuefrag);
									} break;
								case '/' : {
										// check for parse error on mismatching tags?
										cur=cur->parent->parent;
									} break;
								default:
									printf("Unknown kind:%d %c\n",kind,kind);
									abort();
								}
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
			return std::move(parsed);
		}
	};
};

#endif //  __INCLUDED_RPOCO_MUSTACHE_HPP__
