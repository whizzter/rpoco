// Deliberate ifdef even if we have #pragma once if anyone wants to auto-detect RPOCO with the define
#ifndef __INCLUDED_RPOCO_HPP__
#define __INCLUDED_RPOCO_HPP__

#pragma once

#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <utility>
#include <atomic>
#include <mutex>
#include <cctype>
#include <stdint.h>
#include <type_traits>
#include <functional>


// Thread safe typeinfo init (double checked lock)

#define RPOCO(...) \
	void rpoco_type_info_expand(rpoco::type_info *ti,std::vector<std::string>& names,int idx) {} \
	template<typename H,typename... R> \
	void rpoco_type_info_expand(rpoco::type_info *ti,std::vector<std::string>& names,int idx,H&& head,R&&... rest) {\
		ptrdiff_t off=(ptrdiff_t) (  ((uintptr_t)&head)-((uintptr_t)this) ); \
		ti->add(new rpoco::field< std::remove_reference<H>::type >(names[idx],off) );\
		rpoco_type_info_expand(ti,names,idx+1,rest...); \
	} \
	rpoco::type_info* rpoco_type_info_get() { \
		static rpoco::type_info ti; \
		if(!ti.is_init()) { \
			ti.init([this](rpoco::type_info *ti) { \
				std::vector<std::string> names=rpoco::comma_split(#__VA_ARGS__); \
				rpoco_type_info_expand(ti,names,0,__VA_ARGS__); \
			} ); \
		} \
		return &ti; \
	}


// Write JSON:
// beginObject
// [] visitField
// endObject

// beginArray
// visitField (no fieldbase?)
// endArray

// Read JSON
// beginObject
// []locatefield
// endObject

// beginArray

namespace rpoco {
	class fieldbase;
	class field_provider;

	enum visit_type {
		object,
		array
	};

	struct visitor {
		virtual bool visit_start(visit_type vt)=0;
		virtual void visit_end(visit_type vt)=0;
		virtual void visit_end(visit_type vt,std::function<void(std::string&)> out)=0;
		virtual void visit(int& x)=0;
		virtual void visit(std::string &k)=0;
		virtual void visit_null()=0;
		virtual bool has_data()=0;
		//virtual void visit(field_provider *ti,void *p)=0;
	};

	class fieldbase {
	public:
	protected:
		std::string m_name;
		ptrdiff_t m_offset;
	public:
		fieldbase(std::string name,ptrdiff_t offset) {
			this->m_name=name;
			this->m_offset=offset;
		}
		std::string& name() {
			return m_name;
		}
		ptrdiff_t offset() {
			return m_offset;
		}
		virtual void visit(visitor &v,void *p)=0;
	};

	template<typename F>
	struct visit {
	visit(visitor &v,F* f) {
		if (f) {
			field_provider *fp=f->rpoco_type_info_get();
			if (v.visit_start(object)) {
				for (int i=0;i<fp->size();i++) {
					v.visit((*fp)[i]->name());
					(*fp)[i]->visit(v,f);
				}
				v.visit_end(object);
			} else {
				v.visit_end(object,[&v,fp,f](std::string& n){
					if (! fp->has(n))
						return;
					(*fp)[n]->visit(v,f);
				});
			}
		} else {
			v.visit_null();
		}
		//v.visit(f?f->rpoco_type_info_get():0,f);
	}
	};

	template<typename F>
	struct visit<std::vector<F>> { visit(visitor &v,std::vector<F> *vp) {
		if (!vp) {
			v.visit_null();
			return;
		}
		if (v.visit_start(array)) {
			// serialization
			for (F &f:*vp) {
				rpoco::visit<F>(v,&f);
			}
			v.visit_end(array);
		} else {
			v.visit_end(array,[&v,vp](std::string& x) {
				// deserialization
				vp->emplace_back();
				rpoco::visit<F>(v,&(vp->back()));
			});
		}
	}};

	template<typename F>
	struct visit<F*> { visit(visitor &v,F** fpp) {
		if (v.has_data() && !*fpp) {
			*fpp=new F();
		}
		visit<F>(v,*fpp);
	}};

	template<>
	struct visit<int> { visit (visitor &v,int* ip) {
		if (ip) { v.visit(*ip); } else { v.visit_null(); }
	}
	};

	template<>
	struct visit<std::string> { visit(visitor &v,std::string* str) {
		if (str) { v.visit(*str); } else { v.visit_null(); }
	}};

	template<typename F>
	class field : public fieldbase {
	public:
		field(std::string name,ptrdiff_t off) : fieldbase(name,off) {}
		virtual void visit(visitor &v,void *p) {
			rpoco::visit<F>(v,(F*)( (uintptr_t)p+(ptrdiff_t)m_offset ));
		}
	};

/*
	template<typename F>
	class field<std::map<std::string,F>> : public fieldbase {
	public:
		field(std::string name,ptrdiff_t off) : fieldbase(nameoff) {}
		virtual void visit(visitor &v,void *p) {
			std::map<std::string,F> *mp=(std::map<std::string,F>*)( (uintptr_t)p+(ptrdiff_t)m_offset );
			if (v.visit_start(object_start)) {
				// serialization
				for (std::pair<std::string,F> &p:*mp) {
					rpoco::visit(&p.first);
					rpoco::visit(&p.second);
				}
			}
			// deserialization
			//for (std::string k:v) {
			//	rpoco::visit( (*mp)[k] );
			//}
			v.visit_end(object_end,[](x){});
		}
	};
	*/

	class field_provider {
	public:
		virtual int size()=0;
		virtual bool has(std::string id)=0;
		virtual fieldbase*& operator[](int idx)=0;
		virtual fieldbase*& operator[](std::string id)=0;
	};

	class type_info : public field_provider {
		std::vector<fieldbase*> fields;
		std::unordered_map<std::string,fieldbase*> m_named_fields;
		std::atomic<int> m_is_init;
		std::mutex init_mutex;
	public:
		virtual int size() {
			return fields.size();
		}
		virtual bool has(std::string id) {
			return m_named_fields.end()!=m_named_fields.find(id);
		}
		virtual fieldbase*& operator[](int idx) {
			return fields[idx];
		}
		virtual fieldbase*& operator[](std::string id) {
			return m_named_fields[id];
		}
		int is_init() {
			return m_is_init.load();
		}
		void init(std::function<void (type_info *ti)> initfun) {
			std::lock_guard<std::mutex> lock(init_mutex);\
			if (!m_is_init.load()) {
				initfun(this);
				m_is_init.store(1);
			}
		}
		void add(fieldbase *fb) {
			//printf("Gotten field:%s at %p\n",fb->name().c_str(),fb->offset());
			fields.push_back(fb);
			m_named_fields[fb->name()]=fb;
		}
	};

	std::vector<std::string> comma_split(const char *t) {
		// skip spaces and commas
		while(*t&&(std::isspace(*t)||*t==',')) { t++; }
		// token start pos
		const char *s=t;
		std::vector<std::string> out;
		while(*t) {
			if (*t==','||std::isspace(*t)) {
				out.push_back(std::string(s,t-s));
				// skip spaces and commas
				while(*t&&(std::isspace(*t)||*t==',')) { t++; }
				s=t;
			} else {
				t++;
			}
		}
		if (s!=t)
			out.push_back(std::string(s,t-s));
		return out;
	}
};

#endif // __INCLUDED_RPOCO_HPP__
