// This header file provides the RPOCO macro and runtime system that creates 
// template specialized runtime type data usable for serialization and similar tasks.

// NOTICE: the design of RPOCO is still in flux so preferably depend on the
//          built in serialization functionality if possible.

// There are currently 2 ways to use the RPOCO reflection
// 1: the visitor system
// 2: the query system

// The visitor system is smaller and probably faster and is also designed to
// support both consumption of data and production. It is thus suitable for
// data parsing,etc. (see the json parser for an example).
//
// The query api however gives access to query objects along the way and
// is more suitable for situations where control flow can vary such as
// with a template renderer. (see the mustache renderer for an example)

#ifndef __INCLUDED_RPOCO_HPP__
#define __INCLUDED_RPOCO_HPP__

#pragma once

#include <string>
#include <vector>
#include <map>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <atomic>
#include <mutex>
#include <cctype>
#include <stdint.h>
#include <type_traits>
#include <functional>
#include <memory>
#include <string.h>
#include <cstddef>

// Use the RPOCO macro within a compound definition to create
// automatic serialization information upon the specified members.
// RPOCO has thread safe typeinfo init (double checked lock) so using
// functions dependant of the functionality from multiple threads should
// be safe.

// Note 1: The macro magic below is necessary to unpack the field data and provide a coherent interface
// Note 2: This lib uses ptrdiffed offsets to place fields at runtime

#define RPOCO(...) \
	rpoco::type_info* rpoco_type_info_get() { \
		static rpoco::type_info ti; \
		if(!ti.is_init()) { \
			ti.init([this](rpoco::type_info *ti) { \
				std::vector<std::string> names=rpoco::extract_macro_names(#__VA_ARGS__); \
				rpoco_type_info_expand(ti,(uintptr_t)this,names,0,__VA_ARGS__); \
			} ); \
		} \
		return &ti; \
	}

// Actual rpoco namespace containing member information and templates for iteration
namespace rpoco {
	class member;
	class member_provider;


	struct niltarget {};

	// visitation is done in a similar way both during creation (deserialization) and querying (serialization)
	// vt_none is the result any querying system should provide when calling peek on the visitor while
	// creation routines should provide the type of the next data item to be input/read/creation.
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

	// subclass this type to enumerate data structures.
	struct visitor {
		virtual visit_type peek()=0; // return vt_none if querying objects, otherwise return the next data type.
		virtual bool consume(visit_type vt,std::function<void(std::string&)> out)=0; // used by members to start consuming data from complex input objects during creation
		virtual void produce_start(visit_type vt)=0; // used to start producing complex objects
		virtual void produce_end(visit_type vt)=0; // used to stop a production
		// the primitive types below are just visited the same way during both reading and creation
		virtual void visit_null() = 0;
		virtual void visit(bool& b)=0;
		virtual void visit(int& x)=0;
		virtual void visit(double& x)=0;
		virtual void visit(std::string &k)=0; // 
		virtual void visit(char *,size_t sz)=0;
	};
	
	struct query {
		virtual visit_type kind()=0;

		bool find(const char* name,std::function<void(query&)> qt){
			std::string nm(name);
			return find(nm,qt);
		}

		virtual int size()=0;

		virtual void all(std::function<void(std::string&,query&)>)=0;
		virtual bool find(const std::string & name,std::function<void(query&)>)=0;
		virtual void add(std::string & name,std::function<void(query&)>)=0;

		virtual void all(std::function<void(int,query&)>)=0;
		virtual bool at(int idx,std::function<void(query&)>)=0;
		virtual void add(std::function<void(query&)>)=0;

		virtual operator bool*() = 0 ;
		virtual operator int*() = 0 ;
		virtual operator double*() = 0 ;
		virtual void set(const char *) = 0; // set a string value (or null!)
		virtual void set(std::string &k)=0; // set a string value
		virtual std::string get()=0;
	};

	struct emptyquery : query {
		virtual int size() { return 0; }
		virtual void all(std::function<void(std::string&,query&)>) {}
		virtual bool find(const std::string & name,std::function<void(query&)>) {
			return false;
		}
		virtual void add(std::string & name,std::function<void(query&)> q) {
			q(*this);
		}

		virtual void all(std::function<void(int,query&)>) {}
		virtual bool at(int idx,std::function<void(query&)>) {
			return false;
		}
		virtual void add(std::function<void(query&)> q) {
			q(*this);
		}

		virtual operator bool*() { return 0; }
		virtual operator int*() { return 0; }
		virtual operator double*() { return 0; }
		virtual void set(const char *) {}
		virtual void set(std::string &k) {}
		virtual std::string get() { return std::string("empty"); }; // no good!
	};
	struct nonequery : emptyquery {
		virtual visit_type kind() { return vt_none; }
	};

	// a generic member provider class
	class member_provider {
	public:
		virtual int size()=0; // number of members
		virtual bool has(std::string id)=0; // do we have the requested member?
		virtual member*& operator[](int idx)=0; // get an indexed member (0-size() are valid indexes)
		virtual member*& operator[](std::string id)=0; // get a named member
	};
	// base class for class members, gives a name and provides an abstract visitation function
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
		virtual void query(std::function<void(query&) >,void* )=0;
	};



	template<typename F>
	struct typedquery : query {
		F *p;
		typedquery(F *f) {
			p=f;
		}
		virtual visit_type kind() { return vt_object; }
		virtual int size() { return 0; }
		virtual void all(std::function<void(std::string&,query&)> qt) {
			member_provider *fp=p->rpoco_type_info_get();
			for (int i=0;i<fp->size();i++) {
				member* mp=(*fp)[i];
				mp->query([&qt,&mp](query& q){ qt(mp->name(),q);  },p);
			}
		}
		virtual bool find(const std::string & name,std::function<void(query&)> qt) {
			member_provider *fp=p->rpoco_type_info_get();
			if (!fp->has(name))
				return false;
			(*fp)[name]->query(qt,p);
			return true;
		}
		virtual void add(std::string & name,std::function<void(query&)> q) {
			nonequery nq;
			q(nq);
		}

		virtual void all(std::function<void(int,query&)>) {}
		virtual bool at(int idx,std::function<void(query&)>) {
			return false;
		}
		virtual void add(std::function<void(query&)> q) {
			nonequery nq;
			q(nq);
		}

		virtual operator bool*() { return 0; }
		virtual operator int*() { return 0; }
		virtual operator double*() { return 0; }
		virtual void set(const char *) {}
		virtual void set(std::string &k) {}
		virtual std::string get() { return std::string("Obj"); }; // no good!
	};

	template<typename F>
	struct typedquery<std::vector<F>> : emptyquery {
		std::vector<F> *p;
		typedquery(std::vector<F> *v) {
			p=v;
		}
		virtual visit_type kind() { return vt_array; }
		virtual int size() {
			return p->size();
		}
		virtual void all(std::function<void(int,query&)> qc) {
			for (size_t i=0;i<p->size();i++) {
				typedquery<F> tq( &(p->at(i)) );
//				auto tnf= typeid(F).name();
//				printf("Iterating over type:%s age:%d (at %p, name at %p) qptr:%p\n",tnf,(int)p->at(i).child,
//					&(p->at(i).age),
//					&(p->at(i).name),
//					&tq);
				qc(i,tq);
			}
		}
		virtual bool at(int idx,std::function<void(query&)> qc) {
			if (idx>=0 && idx<(int)p->size()) {
				typedquery<F> tq( p->data()+idx );
				qc(tq);
			}
			return false;
		}
	};

	template<typename ...TUP>
	struct typedquery<std::tuple<TUP...>> : emptyquery {
		std::tuple<TUP...> *p;
		typedquery(std::tuple<TUP...> *t) {
			p=t;
		}
		virtual visit_type kind() { return vt_array; }
		virtual int size() {
			return std::tuple_size<std::tuple<TUP...>>::value;
		}
		virtual void all(std::function<void(int,query&)> qc) {
			tupall<0, std::tuple<TUP...>, TUP...>(qc);
		}

		template<int N, typename T>
		void tupall(std::function<void(int, query&)> &qc) { }

		template<int N, typename T, typename H, typename ...R>
		void tupall(std::function<void(int, query&)> &qc) {
			typedquery<H> tq(&std::get<N>(*p));
			qc(N,tq);
			tupall<N+1, T, R...>(qc);
		}

		virtual bool at(int idx,std::function<void(query&)> qc) {
			tupat<0, std::tuple<TUP...>, TUP...>(idx,qc);
			return false;
		}

		template<int N,typename T>
		void tupat(int idx, std::function<void(query&)> &qc) { }

		template<int N,typename T,typename H,typename ...R>
		void tupat(int idx, std::function<void(query&)> &qc) {
			if (idx == N) {
				typedquery<H> tq(&std::get<N>(*p));
				qc(tq);
			} else {
				tupat<N+1, T, R...>(idx, qc);
			}
		}
	};

	template<>
	struct typedquery<std::string> : emptyquery {
		std::string *p;
		typedquery(std::string *str) {
			p=str;
		}

		virtual visit_type kind() { return vt_string; }
		virtual void set(const char *cp) {
			(*p)=std::string(cp);
		}
		virtual void set(std::string &k) {
			(*p)=k;
		}
		virtual std::string get() {
			return *p;
		}
	};
	
	template<>
	struct typedquery<int> : emptyquery {
		int *ip;
		typedquery(int *iv) {
			ip=iv;
		}
		virtual visit_type kind() { return vt_number; }
		virtual operator int*() {
			return ip;
		} 
	};

	template<>
	struct typedquery<double> : emptyquery {
		double *dp;
		typedquery(double *dv) {
			dp=dv;
		}
		virtual visit_type kind() { return vt_number; }
		virtual operator double*() {
			return dp;
		}
	};

	template<>
	struct typedquery<bool> : emptyquery {
		bool *bp;
		typedquery(bool *bv) {
			bp=bv;
		}
		virtual visit_type kind() { return vt_bool; }
		virtual operator bool*() {
			return bp;
		}
	};

	template<int N>
	struct typedquery<char[N]> : emptyquery {
		char (*p)[N];
		typedquery(char (*ip)[N]) {
			p=ip;
		}
		virtual visit_type kind() { return vt_string; }
		virtual void set(const char *cp) {
			if (!cp)
				return;
			size_t inLen=strlen(cp);
			if (inLen>=N)
				inLen=N-1;
			memcpy(*p,cp,inLen);
			(*p)[inLen]=0;
		}
		virtual void set(std::string &k) {
			set(k.data());
		}
		virtual std::string get() {
			int len=0;
			while(len<N && (*p)[len])
				len++;
			return std::string(*p,len);
		}
	};

	template<typename F>
	struct pointertypedquery : query {
		F* p;
		std::unique_ptr<typedquery<F>> sq;

		virtual visit_type kind() {
			if (p) {
				return (*sq).kind();
			} else {
				return vt_null;
			}
		}

		int size() {
			if (p)
				return (*sq).size();
			else
				return 0;
		}

		virtual void all(std::function<void(std::string&,query&)> q) {
			if (p)
				(*sq).all(q);
		}
		virtual bool find(const std::string & name,std::function<void(query&)> q) {
			if (p)
				return (*sq).find(name,q);
			return false;
		}
		virtual void add(std::string & name,std::function<void(query&)>) {
			// TODO
		}

		virtual void all(std::function<void(int,query&)> q) {
			if (p)
				(*sq).all(q);
		}
		virtual bool at(int idx,std::function<void(query&)> q) {
			if (p)
				return (*sq).at(idx,q);
			return false;
		}
		virtual void add(std::function<void(query&)>) {
			// TODO
		}

		virtual operator int*() {
			if (p)
				return *sq;
			return nullptr;
		}
		virtual operator double*() {
			if (p)
				return *sq;
			return nullptr;
		}
		virtual std::string get() {
			if (p)
				return (*sq).get();
			return "";
		}

		virtual void set(const char *s) {
			if (p)
				(*sq).set(s);
		}
		virtual void set(std::string &s) {
			if (p)
				(*sq).set(s);
		}
		virtual operator bool*() {
			if (p)
				return *sq;
			return nullptr;
		}

	};

	template<typename F>
	struct typedquery<std::shared_ptr<F>> : pointertypedquery<F> {
		typedquery(std::shared_ptr<F> *v) {
			this->p=v->get();
			if (this->p)
#if __cplusplus >= 201402L || _MSC_VER>=1900
				this->sq=std::make_unique<typedquery<F>>(this->p);
#else
				this->sq=std::unique_ptr<typedquery<F>>(new typedquery<F>(this->p));
#endif
		}
	};


	template<typename F>
	struct typedquery<F *> : public pointertypedquery<F> {

		// TODO
		typedquery(F **v) {
			this->p=*v;
			if (this->p)
#if __cplusplus >= 201402L || _MSC_VER>=1900
				this->sq=std::make_unique<typedquery<F>>(this->p);
#else
				this->sq=std::unique_ptr<typedquery<F>>(new typedquery<F>(this->p));
#endif
		}
	};


	template<typename F>
	typedquery<F> make_query(F &f) {
		return typedquery<F>(&f);
	}

	// predeclaration of dummy visitor when we consume unknown data.
	inline void visit_nil(visitor &v);

	// generic class type visitation template functionality.
	// if an object wants to override to handle multiple types a specialization
	// of this template can be done, see rpoco::niltarget or rpocojson::json_value
	template<typename F>
	struct visit { visit(visitor &v,F &f) {
		// get member info of a rpoco object
		member_provider *fp=f.rpoco_type_info_get();
		// if reading then start consuming data
		if (v.consume(vt_object,[&v,fp,&f](std::string& n){
				// check if the member to consume exists
				if (! fp->has(n)) {
					// if not start the nil consumer
					visit_nil(v);
				} else {
					// visit member
					(*fp)[n]->visit(v,(void*)&f);
				}
			}))
		{
			// nothing more to do post consumption
			return;
		} else {
			// we're in production mode so produce
			// data from our members
			v.produce_start(vt_object);
			for (int i=0;i<fp->size();i++) {
				v.visit((*fp)[i]->name());
				(*fp)[i]->visit(v,(void*)&f);
			}
			v.produce_end(vt_object);
		}
	}};

	// map visitation
	template<typename F>
	struct visit<std::map<std::string,F>> { visit(visitor &v,std::map<std::string,F> &mp) {
		if (v.consume(vt_object,[&v,&mp](std::string& x) {
				// just produce new entries during consumption
				rpoco::visit<F>(v, mp[x] );
			}))
		{
			return;
		} else {
			// production wanted, so produce all
			// members to a target object.
			v.produce_start(vt_object);
			for (std::pair<std::string,F> p:mp) {
				rpoco::visit<std::string>(v,p.first);
				rpoco::visit<F>(v,p.second);
			}
			v.produce_end(vt_object);
		}
	}};

	// nil visitor, this visitor
	// can consume any type thrown at it and is used
	// to ignore unknown incomming data
	template<>
	struct visit<niltarget> { visit(visitor &v,niltarget &nt) {
		visit_type vtn;
		switch(vtn=v.peek()) {
		case vt_null :
			v.visit_null();
			break;
		case vt_number : {
				double d;
				v.visit(d);
			} break;
		case vt_bool : {
				bool b;
				v.visit(b);
			} break;
		case vt_string : {
				std::string str;
				v.visit(str);
			} break;
		case vt_array :
		case vt_object : {
				v.consume(vtn,[&v,&nt](std::string& propname) {
					niltarget ntn;
					//std::cout<<"Ignoring prop:"<<propname<<"\n";
					rpoco::visit<niltarget>(v,ntn);
				});
			} break;
		case vt_none : case vt_error:
			break;
		}
	}};

	// we needed this visit function because clang couldn't specialize for niltarget visitors from the base template
	inline void visit_nil(visitor &v) {
		niltarget nt;
		rpoco::visit<niltarget>(v,nt);
	}

	// vector visitor, used for arrays
	template<typename F>
	struct visit<std::vector<F>> { visit(visitor &v,std::vector<F> &vp) {
		if (v.consume(vt_array,[&v,&vp](std::string& x) {
				// consumption of incoming data
				vp.emplace_back();
				rpoco::visit<F>(v,vp.back());
			}))
		{
			return ;
		} else {
			// production of outgoing data
			v.produce_start(vt_array);
			for (F &f:vp) {
				rpoco::visit<F>(v,f);
			}
			v.produce_end(vt_array);
		}
	}};

	template<typename ...TUP>
	struct visit<std::tuple<TUP...>> {
		visit(visitor &v, std::tuple<TUP...> &tp) {
			int i=0;
			if (v.consume(vt_array, [&v, &tp,&i](std::string& x) {
				consume<0, std::tuple<TUP...>,TUP...>(v,tp,i);
				i++;
			})) {
				return;
			} else {
				v.produce_start(vt_array);
				produce<0, std::tuple<TUP...>, TUP...>(v, tp);
				v.produce_end(vt_array);
			}
		}

		template<int N,typename T>
		static void consume(visitor &v,T &tuple,int idx) {
			niltarget nt;
			rpoco::visit<niltarget>(v, nt);
		}
		template<int N,typename T,typename H,typename ...R>
		static void consume(visitor &v, T &tuple,int idx) {
			if (idx == N) {
				rpoco::visit<H>(v,std::get<N>(tuple));
			} else {
				consume<N + 1, T,R...>(v,tuple,idx);
			}
		}

		template<int N,typename T>
		static void produce(visitor &v, T &tuple) {}
		template<int N, typename T, typename H, typename ...R>
		static void produce(visitor &v,T &tuple) {
			rpoco::visit<H>(v,std::get<N>(tuple));
			produce<N + 1, T, R...>(v, tuple);
		}
	};


	// the pointer visitor creates a new object of the specified type
	// during consumption so destructors should
	// always check for the presence and destroy if needed.
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

	// like the pointer consumer above the shared_ptr
	// consumer will also create new objects to hold if needed.
	template<typename F>
	struct visit<std::shared_ptr<F>> { visit(visitor &v,std::shared_ptr<F> & fp) {
		if (v.peek()!=vt_null && v.peek()!=vt_none && !fp) {
			fp.reset(new F());
		}
		if (fp)
			visit<F>(v,*fp);
		else
			v.visit_null();
	}};

	// a unique_ptr version of the above shared_ptr template
	template<typename F>
	struct visit<std::unique_ptr<F>> { visit(visitor &v,std::unique_ptr<F> & fp) {
		if (v.peek()!=vt_null && v.peek()!=vt_none && !fp) {
			fp.reset(new F());
		}
		if (fp)
			visit<F>(v,*fp);
		else
			v.visit_null();
	}};

	// integer visitation
	template<> struct visit<int> {
		visit(visitor &v,int &ip) {
			v.visit(ip);
		}
	};

	template<> struct visit<bool> {
		visit(visitor &v,bool &bp) {
			v.visit(bp);
		}
	};

	// double visitation
	template<> struct visit<double> {
		visit(visitor &v,double &ip) {
			v.visit(ip);
		}
	};

	// string visitation
	template<> struct visit<std::string> { visit(visitor &v,std::string &str) {
		v.visit(str);
	}};

	// sized C-string visitation
	template<int SZ> struct visit<char[SZ]> { visit(visitor &v,char (&str)[SZ]) {
		v.visit(str,SZ);
	}};

	// field class template for the actual members (see the RPOCO macro for usage)
	template<typename F>
	class field : public member {
		std::ptrdiff_t m_offset;
	public:
		field(std::string name,std::ptrdiff_t off) : member(name) {
			this->m_offset=off;
		}
		std::ptrdiff_t offset() {
			return m_offset;
		}
		virtual void visit(visitor &v,void *p) {
			rpoco::visit<F>(v,*(F*)( (uintptr_t)p+(std::ptrdiff_t)m_offset ));
		}
		virtual void query( std::function<void(rpoco::query&) > qt,void *p) {
			//auto q=make_query( *(F*)( (uintptr_t)p+(std::ptrdiff_t)m_offset ) );
			typedquery<F> q( (F*)( (uintptr_t)p+(std::ptrdiff_t)m_offset ) );
			qt(q);
		}
	};

	// type_info is a member_provider implementation for regular classes.
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

	template<typename T>
	struct taginfo {
		uintptr_t m_ref;
		std::vector<const char*> tags;
		taginfo(T &m,std::initializer_list<const char*> infoinit) : m_ref( (uintptr_t)&m ),tags(infoinit) {
		//	m_ref=(uintptr_t)&m;
		//	printf("Target:%p me:%p\n",&m,this);
			//std::initializer_list<const char*> l={info...};
		//	for(auto tg:tags) {
		//		printf("Given tag:%s\n",tg);
		//	}
		}
		uintptr_t ref() { return m_ref; }
	};

	template<typename T,typename ...I>
	taginfo<T> tag(T &m,I... info) {
		return std::move( taginfo<T>(m,{info...}) );
	}

	template<typename T>
	struct rpoco_type_info_expand_member {
		rpoco_type_info_expand_member(rpoco::type_info *ti,uintptr_t _ths,std::vector<std::string>&names,int idx,T &m) {
			std::ptrdiff_t off=(std::ptrdiff_t) (  ((uintptr_t)&m)-_ths );
			ti->add(new rpoco::field<typename std::remove_reference<T>::type >(names[idx],off) );
		}
	};
	
	template<typename T>
	struct rpoco_type_info_expand_member<taginfo<T>> {
		rpoco_type_info_expand_member(rpoco::type_info *ti,uintptr_t _ths,std::vector<std::string>&names,int idx,taginfo<T> &m) {
			std::string &name=names[idx];
			std::ptrdiff_t off=(std::ptrdiff_t) (  m.ref() -_ths );
			//printf("%s at %x\n",name.data(),off);
			//for(auto tag:m.tags) {
			//	printf("%s has tag %s\n",name.data(),tag);
			//}
			ti->add(new rpoco::field<typename std::remove_reference<T>::type >(name,off) );
		}
	};

	void rpoco_type_info_expand(rpoco::type_info *ti,uintptr_t _ths,std::vector<std::string>& names,int idx) {}
	template<typename H,typename... R>
	void rpoco_type_info_expand(rpoco::type_info *ti,uintptr_t _ths,std::vector<std::string>& names,int idx,H& head,R&... rest) {
		rpoco_type_info_expand_member<H>(ti,_ths,names,idx,head);
		rpoco_type_info_expand(ti,_ths,names,idx+1,rest...);
	}

	// helper function to the RPOCO macro to parse the data
	static std::vector<std::string> extract_macro_names(const char *t) {
		// skip spaces and commas
		while(*t&&(std::isspace(*t)||*t==',')) { t++; }
		// token start pos
		const char *s=t;
		// the vector of output names
		std::vector<std::string> out;
		// parsing loop to extract tokens
		while(*t) {
			if (*t=='(') {
				t++; // skip first paren
				// some kind of tag information, we don't particularly care for what is
				// used to create the tag info here so we will ignore that and instead
				// take the first name inside it.
				while(*t&& std::isspace(*t)) { t++; }
				s=t;
				// now size the name itself
				while(*t&&!(std::isspace(*t)||*t==',')) { t++; }
				// and record it
				out.push_back(std::string(s,t-s));
				// now we will skip all extra info
				bool inArg=false;
				while(*t) {
					if (!inArg && *t==')')
						break;
					if (*t=='\"')
						inArg=!inArg;
					t++;
				}
				t++;
				// skip trailing spaces and commas
				while(*t&&(std::isspace(*t)||*t==',')) { t++; }
				s=t;
			} else if (*t==','||std::isspace(*t)) {
				out.push_back(std::string(s,t-s));
				// skip spaces and commas
				while(*t&&(std::isspace(*t)||*t==',')) { t++; }
				s=t;
			} else {
				t++;
			}
		}
		// did we have an extra string? push it.
		if (s!=t)
			out.push_back(std::string(s,t-s));
		return out;
	}
};

#endif // __INCLUDED_RPOCO_HPP__
