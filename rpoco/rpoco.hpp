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
#include <typeinfo>
#include <typeindex>
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
			ti.init([this](rpoco::type_info *__rpoco__ti) { \
				std::vector<std::string> __rpoco_names=rpoco::extract_macro_names("" #__VA_ARGS__); \
				using rpoco::tag::_; \
				rpoco::rpoco_type_info_expand(__rpoco__ti,(uintptr_t)this,__rpoco_names,0,__VA_ARGS__); \
			} ); \
		} \
		return &ti; \
	}

// Actual rpoco namespace containing member information and templates for iteration
namespace rpoco {
	// predeclare a few classes that will be used frequently.

	// Queries are used by selective introspection systems, like the mustache renderer to pick out variables for different uses during rendering.
	struct query;
	// The visitor API is used for more or less full traversal of data during serialization/deserialization (JSON, database,etc)
	class visitor;
	// Members are parts of objects
	class member;
	// A member provider usually contains the data in an object. Really only used as an interface for type_info
	class member_provider;

	// dummy struc used for visiting data without retaining any of it.
	struct niltarget {};

	// (These names are subject to change, RPOCO started out with a visitation API but now it's also used for queries)
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

	// a generic member provider class
	class member_provider {
	protected:
		std::unordered_map<std::type_index, void*> m_attributes;
	public:
		virtual int size() = 0; // number of members
		virtual bool has(const std::string &id) = 0; // do we have the requested named member?
		virtual member*& operator[](int idx) = 0; // get an indexed member (0-size() are valid indexes)
		virtual member*& operator[](const std::string & id) = 0; // get a named member

		// access a named attribute of the type (the attributes here are more akin to C++ compiler attributes or Java annotations than the OO term)
		template<typename T>
		T* attribute() {
			auto it = m_attributes.find(std::type_index(typeid(T)));
			if (it == m_attributes.end())
				return nullptr;
			return (T*)it->second;
		}
	};

	// base class for class members, gives a name and provides an abstract visitation function
	class member {
	protected:
		// member name?
		std::string m_name;
		// member attributes
		std::unordered_map<std::type_index, void*> m_attributes;
		// internal member accessor function
		virtual void * access(std::type_index idx, void *obj) = 0;

		// constructor is protected since we shouldn't instansiate these objects directly.
		member(std::string name) {
			this->m_name = name;
		}
	public:
		// name accessor
		std::string& name() {
			return m_name;
		}
		// get the exact C++ type_index held by members described by this object.
		virtual std::type_index type_index() = 0;
		// visit the current member on the subject object
		virtual void visit(visitor &v, void *subject) = 0;
		// query the current member on the subject object
		virtual void query(std::function<void(query&) >, void*) = 0;

		// get a pointer to the member inside the subject IF and only IF the member is of the correct type!
		template<typename T>
		T* access(void *subject) {
			return (T*)access(std::type_index(typeid(T)), subject);
		}

		// get a pointer to an attribute of the kind given, ie not an instance member itself but rather the attributes given to the member.
		template <typename T>
		T* attribute() {
			auto it = m_attributes.find(std::type_index(typeid(T)));
			if (it == m_attributes.end())
				return nullptr;
			return (T*)it->second;
		}
	};


	// subclass this type to enumerate data structures.
	class visitor {
	protected:
		// internal construction method.
		virtual void * construct(std::type_index idx) { return nullptr; }
	public:
		virtual visit_type peek()=0; // return vt_none if querying objects, otherwise return the next data type.

		virtual bool consume_object(member_provider &mp,void *obj) = 0; // used by members to start consuming data from complex input objects during creation
		virtual bool consume_map(const std::function<void(const std::string&)> &out) = 0; // used by members to start consuming data from complex input objects during creation
		virtual bool consume_array(const std::function<void()> &out) = 0; // used by members to start consuming data from complex input objects during creation

																		  // a generic object production method, can be overridden by visitors that needs special semantics.
		virtual void produce_object(member_provider &mp, void *obj) {
			// we're in production mode so produce
			// data from our members
			produce_start(vt_object);
			for (int i = 0;i<mp.size();i++) {
				visit(mp[i]->name());
				mp[i]->visit(*this, obj);
			}
			produce_end(vt_object);

		}

		virtual void produce_start(visit_type vt)=0; // used to start producing complex objects
		virtual void produce_end(visit_type vt)=0; // used to stop a production

		// the primitive types below are just visited the same way during both reading and creation
		virtual void visit_null() = 0;
		virtual void visit(bool& b)=0;
		virtual void visit(int& x)=0;
		virtual void visit(double& x)=0;
		virtual void visit(std::string &k)=0;
		virtual void visit(char *,size_t sz)=0;
		virtual void error(const std::string &err)=0;

		// templated construction method
		template<typename T>
		T* construct() {
			return (T*)construct(std::type_index(typeid(T)));
		}
	};
	
	// The query API interface, implemented by subclasses.
	struct query {
		virtual ~query() = default;

		// what kind of basic object are we querying?
		virtual visit_type kind()=0;

		// a non-std::string proxy query function
		bool find(const char* name,std::function<void(query&)> qt){
			std::string nm(name);
			return find(nm,qt);
		}

		// returns the size of a collection of objects.
		virtual int size()=0;

		// Iterate a group of named objects (only applicable in case kind() returns vt_object)
		virtual void all(std::function<void(const std::string&,query&)>)=0;
		// Find a member and return a sub-query if it matches.
		virtual bool find(const std::string & name,std::function<void(query&)>)=0;
		virtual void add(std::string & name,std::function<void(query&)>)=0;

		// Iterate an array of unnamed objects (only applicable if kind() returns vt_array)
		virtual void all(std::function<void(int,query&)>)=0;
		// Get an indexed member
		virtual bool at(int idx,std::function<void(query&)>)=0;
		virtual void add(std::function<void(query&)>)=0;

		// Query or set values of various kinds
		virtual operator bool*() = 0 ;
		virtual operator int*() = 0 ;
		virtual operator double*() = 0 ;
		virtual void set(const char *) = 0; // set a string value (or null!)
		virtual void set(std::string &k)=0; // set a string value
		virtual std::string get()=0;
	};

	// Dummy query type used as a base for implementing simple queries.
	struct emptyquery : query {
		virtual int size() { return 0; }
		virtual void all(std::function<void(const std::string&,query&)>) {}
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


	// generic typed query for rpoco-ized objects.
	template<typename F>
	struct typedquery : query {
		F *p;
		typedquery(F *f) {
			p=f;
		}
		virtual visit_type kind() { return vt_object; }
		virtual int size() { return 0; }
		virtual void all(std::function<void(const std::string&,query&)> qt) {
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
	typedquery<F> make_query(F &f);

	// typed-query for std::vector
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

	// typed query for std::tuple tuple's. hides the complexity of the differently typed indices with expansion
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

	// basic type specializations
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

	template<>
	struct typedquery<char const *> : emptyquery {
		const char **ptr;
		typedquery(const char **inptr) {
			ptr = inptr;
		}
		virtual visit_type kind() {
			return vt_string;
		}
		virtual void set(const char *cp) {
			abort(); // const char consts's are write-only
		}
		virtual void set(std::string &k) {
			abort(); // const char consts's are write-only
		}
		virtual std::string get() {
			return std::string(*ptr);
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

	// specialization for std::map with std::string keys
	template<typename F>
	struct typedquery<std::map<std::string,F>> :query {
		std::map<std::string, F> *map;
		typedquery(std::map<std::string, F> *v) {
			map = v;
		}
		virtual visit_type kind() {
			return vt_object;
		}
		virtual int size() { return 0; }

		virtual void all(std::function<void(const std::string&, query&)> out) {
			for (auto pair : *map) {
				auto mq=make_query(pair.second);
				out(pair.first, mq);
			}
		}
		virtual bool find(const std::string & name, std::function<void(query&)> out) {
			auto it = map->find(name);
			if (it == map->end())
				return false;
			auto mq = make_query(it->second);
			out(mq);
			return true;
		}
		virtual void add(std::string & name, std::function<void(query&)>) {
			// really implement?!
		}

		virtual void all(std::function<void(int, query&)>) {}
		virtual bool at(int idx, std::function<void(query&)>) { return false; }
		virtual void add(std::function<void(query&)>) {}

		virtual operator bool*() { return nullptr; }
		virtual operator int*() { return nullptr; }
		virtual operator double*() { return nullptr; }
		virtual void set(const char *) {}
		virtual void set(std::string &k) {}
		virtual std::string get() { return ""; };
	};

	// pointertypedquery is a base class for querying objects sitting inside various kinds of pointers (std::shared_ptr,std::unique_ptr and regular ones)
	template<typename F>
	struct pointertypedquery : query {
		F* p;
		std::unique_ptr<query> sq;

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

		virtual void all(std::function<void(const std::string&,query&)> q) {
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
		if (v.consume_object(*fp,&f)) {
			// nothing more to do post consumption
			return;
		} else {
			// invoke the default producer
			v.produce_object(*fp, &f);
		}
	}};

	// map visitation
	template<typename F>
	struct visit<std::map<std::string,F>> { visit(visitor &v,std::map<std::string,F> &mp) {
		if (v.consume_map([&v,&mp](const std::string& x) {
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
		case vt_array: {
				v.consume_array([&v]() {
					niltarget ntn;
					rpoco::visit<niltarget>(v, ntn);
				});
			} break;
		case vt_object : {
				v.consume_map([&v](const std::string& propname) {
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
		if (v.consume_array([&v,&vp]() {
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
			if (v.consume_array([&v, &tp,&i]() {
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

	// special handling for allocating rpoco managed types (since we will allow for polymorphism if the visitor wants it!)
	template<typename F>
	void visit_ptrtarget(F* ptr,const std::function<void(F*)> &alloccb,visitor &v,decltype(&F::rpoco_type_info_get,(void*)nullptr) b) {
		if (v.peek() != vt_null && v.peek() != vt_none && !ptr) {
			ptr = v.construct<F>();
			if (!ptr)
				ptr = new F();
			alloccb(ptr);
		}
		if (ptr)
			visit<F>(v, *ptr);
		else
			v.visit_null();
	}

	// regular objects are just allocated and visited normally.
	template<typename F>
	void visit_ptrtarget(F* ptr,const std::function<void(F*)> &alloccb,visitor &v,...) {
		if (v.peek() != vt_null && v.peek() != vt_none && !ptr) {
			ptr = new F();
			alloccb(ptr);
		}
		if (ptr)
			visit<F>(v, *ptr);
		else
			v.visit_null();
	}

	// the pointer visitor creates a new object of the specified type
	// during consumption so destructors should
	// always check for the presence and destroy if needed.
	template<typename F>
	struct visit<F*> { visit(visitor &v,F *& fp) {
		std::function<void(F*)> cb([&fp](F* inv) {
			fp = inv;
		});
		visit_ptrtarget(fp, cb, v,nullptr);
	}};

	// like the pointer consumer above the shared_ptr
	// consumer will also create new objects to hold if needed.
	template<typename F>
	struct visit<std::shared_ptr<F>> { visit(visitor &v,std::shared_ptr<F> & fp) {
		std::function<void(F*)> cb([&fp](F* inv) {
			fp.reset(inv);
		});
		visit_ptrtarget(fp.get(), cb, v,nullptr);
	}};

	// a unique_ptr version of the above shared_ptr template
	template<typename F>
	struct visit<std::unique_ptr<F>> { visit(visitor &v,std::unique_ptr<F> & fp) {
		std::function<void(F*)> cb([&fp](F* inv) {
			fp.reset(inv);
		});
		visit_ptrtarget(fp.get(), cb, v,nullptr);
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

	template<> struct visit<char const * > {
		visit(visitor &v, char const * &p) {
			if (v.peek() == vt_none) {
				v.visit((char*)p, strlen(p)); // let's write out this value in normal fashion
			} else {
				std::string tmp;
				v.visit(tmp);
				if (tmp != p) {
					v.error("Read in " + tmp + " as a value when we expected " + p);
				}
			}
		}
	};

	// sized C-string visitation
	template<int SZ> struct visit<char[SZ]> { visit(visitor &v,char (&str)[SZ]) {
		v.visit(str,SZ);
	}};

	// type_info is a member_provider implementation for regular classes.
	class type_info : public member_provider {
		std::vector<member*> fields;
		std::unordered_map<std::string, member*> m_named_fields;
		std::atomic<int> m_is_init;
		std::mutex init_mutex;
		std::vector<std::function<void()>> post_init;


		// the register_post_init functions is a SFINAE template used to detect the
		// precence of rpoco_post_init member functions inside attribute types,
		// if the function exists then register a callback to finalize needed initialization
		template<typename T>
		void register_post_init(T* subj, decltype(&T::rpoco_post_init, (void*)nullptr) ) {
			post_init.push_back([this,subj]() {  subj->rpoco_post_init(*this); });
		}
		// empty-fun that is called for non specialized types
		template<typename T>
		void register_post_init(...) {}

		// friend so that attributes can be created from fields that needs to link to type attributes
		// rather than just field attributes
		template<typename T>
		friend class field;

		// get (and create if needed) attributes inside the type_info, used by the field template
		template<typename T>
		T* attribute_get_and_make() {
			auto it = m_attributes.find(std::type_index(typeid(T)));
			if (it != m_attributes.end())
				return (T*)it->second;
			T* out=(T*)(m_attributes[std::type_index(typeid(T))] = new T());
			register_post_init(out,nullptr);
			return out;
		}

		// friend so that members can be added.
		template<typename T>
		friend struct rpoco_type_info_expand_member;

		// the actual function for adding members.
		void add(member *fb) {
			fields.push_back(fb);
			m_named_fields[fb->name()] = fb;
		}
	public:
		// how many fields does this type have?
		virtual int size() {
			return fields.size();
		}
		// access the nth field of this type
		virtual member*& operator[](int idx) {
			return fields[idx];
		}
		// do we have a named field?
		virtual bool has(const std::string & id) {
			return m_named_fields.end() != m_named_fields.find(id);
		}
		// access the named field
		virtual member*& operator[](const std::string & id) {
			return m_named_fields[id];
		}
		// has this type been initialized?
		int is_init() {
			return m_is_init.load();
		}
		// initialize this type (only if it's not been done beforehand)
		void init(std::function<void(type_info *ti)> initfun) {
			std::lock_guard<std::mutex> lock(init_mutex);
			if (!m_is_init.load()) {
				initfun(this);
				for (auto pcb : post_init)
					pcb();
				m_is_init.store(1);
			}
		}
	};

	// field class template for the actual members (see the RPOCO macro for usage)
	template<typename F>
	class field : public member {
		std::ptrdiff_t m_offset;
	protected:
		// Actual access impl based on type index. do not return if an invalid type is requested.
		void* access(std::type_index idx, void *p) {
			if (!p || std::type_index(typeid(F)) != idx)
				return nullptr;
			return (void*)((uintptr_t)p+m_offset);
		}

		// conditional SFINAE expansion and linking of the field type to the attribute if the attribute receiver needs an exact function
		// to fulfill it's purpose. rpoco_link_field_type is expected to be a template whilst rpoco_want_link_field_type will be a concrete function.
		template<typename T>
		void link_field_type(decltype(&T::rpoco_want_link_field_type,(T*)nullptr) attrib) {
			attrib->rpoco_link_field_type(this);
		}
		// if the attribute lacks a rpoco_want_link_field_type member then just ignore trying to link up the type.
		template<typename T>
		void link_field_type(...) {}

		// automatic linkage of type info attributes to the field attribute via rpoco_link_type_info_attributes member function argument expansion
		// in short, if an attribute type is wanted then it's created and returned.
		template<typename AT, typename ...ARGS>
		void invoke_link(rpoco::type_info *mp, AT* ap, void (AT::* fp)(ARGS& ...)) {
			(ap->*fp)(*(mp->attribute_get_and_make<ARGS>())...);
		}

		// set attribute specialization overload for attribute types with a rpoco_link_type_info_attributes function
		// this creates a new attribute and links any type_info attributes if that is needed.
		template<typename T>
		void set_attribute_int(rpoco::type_info *mp, decltype(&T::rpoco_link_type_info_attributes, *(const T*)nullptr) &v,void *) {
			T* attrib = new T(v);
			link_field_type<T>(attrib);
			m_attributes[std::type_index(typeid(T))] = attrib;
			invoke_link(mp,attrib, &T::rpoco_link_type_info_attributes);
		}
		// general attribute types with no type attribute links.
		template<typename T>
		void set_attribute_int(rpoco::type_info *mp, const T &v,...) {
			T* attrib = new T(v);
			link_field_type<T>(attrib);
			m_attributes[std::type_index(typeid(T))] = attrib;
		}

		// the externally invoked method (GCC is not capable of externally specifying member template function invocations)
		template<typename T>
		void set_attribute(rpoco::type_info *mp, const T &v) {
			set_attribute_int<T>(mp, v, nullptr);
		}

		// friended class so that set_attribute can be invoked during initialization.
		template<typename T>
		friend struct rpoco_type_info_expand_member;
	public:
		field(std::string name,std::ptrdiff_t off) : member(name) {
			this->m_offset=off;
		}
		std::ptrdiff_t offset() {
			return m_offset;
		}
		virtual std::type_index type_index() {
			return std::type_index(typeid(F));
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

	// taginfo type, initialization time container for the field and associated attributes
	template<typename T,typename ...ATTRS>
	struct taginfo {
		uintptr_t m_ref;
		std::tuple<ATTRS...> attrs;
		taginfo(T &m,std::tuple<ATTRS...> inAttrs) : m_ref( (uintptr_t)&m ),attrs(inAttrs) {}
		uintptr_t ref() { return m_ref; }
	};

	// we have a special tag namespace with the _ function to make _ a simple symbol for the RPOCO macro to create taginfo's.
	namespace tag {
		template<typename T, typename ...I>
		taginfo<T,I...> _(T &m, I... info) {
			taginfo<T, I...> tmp(m, std::make_tuple<I...>(std::move(info)...));
			return tmp;
		}
	}

	template<typename T>
	struct rpoco_type_info_expand_member {
		rpoco_type_info_expand_member(rpoco::type_info *ti,uintptr_t _ths,std::vector<std::string>&names,int idx,const T &m) {
			std::ptrdiff_t off=(std::ptrdiff_t) (  ((uintptr_t)&m)-_ths );
			ti->add(new rpoco::field<typename std::remove_reference<typename std::remove_const<T>::type>::type >(names[idx],off) );
		}
	};
	
	template<typename T,typename ...ATTRS>
	struct rpoco_type_info_expand_member<taginfo<T,ATTRS...>> {
		template<int I, typename TAI>
		void set_attrib(rpoco::type_info *ti, rpoco::field<typename std::remove_reference<T>::type > *fld, TAI &tai) {
		}
		template<int I, typename TAI, typename H, typename ...REST>
		void set_attrib(rpoco::type_info *ti, rpoco::field<typename std::remove_reference<T>::type > *fld, TAI &tai) {
			//fld->set_attribute<H>(ti, std::get<I>(tai.attrs), nullptr);
			fld->set_attribute(ti, std::get<I>(tai.attrs));
			set_attrib<I + 1, TAI, REST...>(ti,fld, tai);
		}

		rpoco_type_info_expand_member(rpoco::type_info *ti,uintptr_t _ths,std::vector<std::string>&names,int idx,const taginfo<T,ATTRS...> &m) {
			std::string &name=names[idx];
			std::ptrdiff_t off=(std::ptrdiff_t) (  ((taginfo<T,ATTRS...>)m).ref() -_ths );
			rpoco::field<typename std::remove_reference<T>::type > *fld = new rpoco::field<typename std::remove_reference<T>::type >(name, off);
			ti->add(fld);
			set_attrib<0, const taginfo<T, ATTRS...>, ATTRS...>(ti,fld, m);
		}
	};

	static void rpoco_type_info_expand(rpoco::type_info *ti,uintptr_t _ths,std::vector<std::string>& names,int idx) {}

//	template<typename... R>
//	void rpoco_type_info_expand(rpoco::type_info *ti, uintptr_t _ths, std::vector<std::string>& names, int idx, const char * data, const R&... rest) {
//		printf("Text info? %s\n",data);
//		rpoco_type_info_expand(ti, _ths, names, idx + 1, rest...);
//	}

	template<typename H,typename... R>
	void rpoco_type_info_expand(rpoco::type_info *ti,uintptr_t _ths,std::vector<std::string>& names,int idx,const H& head,const R&... rest) {
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
				while(*t&&!(std::isspace(*t)||*t==','||*t==')')) { t++; }
				// and record it
				out.push_back(std::string(s,t-s));
				if (*t != ')') {
					// now we will skip all extra info associated with this var
					bool inStr = false;
					int prevChar = 0;
					int parenCount = 1; // loop while we have a paren count (we can have args in args)
					while (*t) {
						int c = *t++;
						if (inStr) {
							if (c == '\"' && prevChar != '\\') {
								inStr = false; // if prevchar differs from \ then we've ended the str.
							} else if (c == '\\' && prevChar == '\\') {
								prevChar = 0; // double slashes followed by a " must term so clear prevchar in that case.
								continue;
							}
							prevChar = c;
						} else {
							if (c == ')') {
								if (!(--parenCount))
									break;
							} else if (c == '(') {
								parenCount++;
							} else if (c == '\"') {
								inStr = true;
							}
							prevChar = c;
						}
					}
				} else {
					t++;
				}
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
