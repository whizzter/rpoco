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


// Use the RPOCO macro within a compound definition to create
// automatic serialization information upon the specified members.
// RPOCO has thread safe typeinfo init (double checked lock) so using
// functions dependant of the functionality from multiple threads should
// be safe.

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
				std::vector<std::string> names=rpoco::extract_macro_names(#__VA_ARGS__); \
				rpoco_type_info_expand(ti,names,0,__VA_ARGS__); \
			} ); \
		} \
		return &ti; \
	}

namespace rpoco {
	class member;
	class member_provider;

	enum visit_type {
		vt_none,
		vt_error,
		vt_object,
		vt_array,
		vt_null,
		vt_bool,
		vt_number,
		vt_string
	};

	struct visitor {
		virtual visit_type peek()=0;
		virtual bool consume(visit_type vt,std::function<void(std::string&)> out)=0;
		virtual void produce_start(visit_type vt)=0;
		virtual void produce_end(visit_type vt)=0;
		virtual void visit_null()=0;
		virtual void visit(bool& b)=0;
		virtual void visit(int& x)=0;
		virtual void visit(double& x)=0;
		virtual void visit(std::string &k)=0;
	};

	template<typename F>
	struct visit { visit(visitor &v,F &f) {
		member_provider *fp=f.rpoco_type_info_get();
		if (v.consume(vt_object,[&v,fp,&f](std::string& n){
				if (! fp->has(n))
					return;
				(*fp)[n]->visit(v,(void*)&f);
			}))
		{
			return;
		} else {
			v.produce_start(vt_object);
			for (int i=0;i<fp->size();i++) {
				v.visit((*fp)[i]->name());
				(*fp)[i]->visit(v,(void*)&f);
			}
			v.produce_end(vt_object);
		}
	}};

	template<typename F>
	struct visit<std::map<std::string,F>> { visit(visitor &v,std::map<std::string,F> &mp) {
		if (v.consume(vt_object,[&v,&mp](std::string& x) {
				// deserialization
				rpoco::visit<F>(v, mp[x] );
			}))
		{
			return;
		} else {
			v.produce_start(vt_object);
			// serialization
			for (std::pair<std::string,F> p:mp) {
				rpoco::visit<std::string>(v,p.first);
				rpoco::visit<F>(v,p.second);
			}
			v.produce_end(vt_object);
		}
	}};

	template<typename F>
	struct visit<std::vector<F>> { visit(visitor &v,std::vector<F> &vp) {
		if (v.consume(vt_array,[&v,&vp](std::string& x) {
				// deserialization
				vp.emplace_back();
				rpoco::visit<F>(v,vp.back());
			}))
		{
			return ;
		} else {
			v.produce_start(vt_array);
			// serialization
			for (F &f:vp) {
				rpoco::visit<F>(v,f);
			}
			v.produce_end(vt_array);
		}
	}};

	template<typename F>
	struct visit<F*> { visit(visitor &v,F *& fp) {
		if (v.peek()!=vt_null && v.peek()!=vt_none && !fp) {
			fp=new F();
		}
		if (fp)
			visit<F>(v,*fp);
		else
			v.visit_null();
	}};

	template<> struct visit<int> { visit (visitor &v,int &ip) {
		v.visit(ip);
	}};

	template<> struct visit<std::string> { visit(visitor &v,std::string &str) {
		v.visit(str);
	}};

	class member {
	public:
	protected:
		std::string m_name;
	public:
		member(std::string name) {
			this->m_name=name;
		}
		std::string& name() {
			return m_name;
		}
		virtual void visit(visitor &v,void *p)=0;
	};


	template<typename F>
	class field : public member {
		ptrdiff_t m_offset;
	public:
		field(std::string name,ptrdiff_t off) : member(name) {
			this->m_offset=off;
		}
		ptrdiff_t offset() {
			return m_offset;
		}
		virtual void visit(visitor &v,void *p) {
			rpoco::visit<F>(v,*(F*)( (uintptr_t)p+(ptrdiff_t)m_offset ));
		}
	};

	class member_provider {
	public:
		virtual int size()=0;
		virtual bool has(std::string id)=0;
		virtual member*& operator[](int idx)=0;
		virtual member*& operator[](std::string id)=0;
	};

	class type_info : public member_provider {
		std::vector<member*> fields;
		std::unordered_map<std::string,member*> m_named_fields;
		std::atomic<int> m_is_init;
		std::mutex init_mutex;
	public:
		virtual int size() {
			return fields.size();
		}
		virtual bool has(std::string id) {
			return m_named_fields.end()!=m_named_fields.find(id);
		}
		virtual member*& operator[](int idx) {
			return fields[idx];
		}
		virtual member*& operator[](std::string id) {
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
		void add(member *fb) {
			fields.push_back(fb);
			m_named_fields[fb->name()]=fb;
		}
	};

	std::vector<std::string> extract_macro_names(const char *t) {
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
