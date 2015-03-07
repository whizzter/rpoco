// test.cpp
//
// this program runs a set of automatic tests on JSON data to
// validate the functionality of the library.

#include <cstdlib>
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>

#include <rpoco/rpocojson.hpp>


// Note: we probasbly need some #ifdefs to work with other compilers than MSVC2013
//       since <filesystem> has been standardized since the release of this compiler.
using namespace std::tr2::sys;

using namespace rpocojson;

bool node_verify=false;

int main(int argc,char **argv) {
	for (int i=1;i<argc;i++) {
		if (std::string("-node")==argv[i]) {
			node_verify=true;
		}
	}

	path p="json";
	p/="json_parser";
	printf("%s\n",p.string().c_str());
	for (directory_iterator it=directory_iterator(p);it!=directory_iterator();++it) {
		if (it->path().extension()!=".json")
			continue;
		//if (it->path().filename()!="valid-0002.json")
		//	continue;
		bool wanted=0==it->path().filename().find("valid-");

		json_value *jv=0;
		std::ifstream is(it->path().string().c_str());
		bool pr=parse(is,jv);
		if (wanted==pr) {
			printf("%s was %s as expected\n",it->path().string().c_str(),pr?"parsed":"not parsed");
			if (pr && node_verify) {
				std::string outname=it->path().string()+".out";
				{
					std::ofstream os(outname);
					os << to_json(jv);
				}
				std::string cmd="node verify.js "+it->path().string()+" "+outname;
				std::system(cmd.c_str());
			}
		} else {
			printf("Error, %s was unexpectedly %s\n",it->path().string().c_str(),pr?"parsed":"not parsed");
			printf("parsed ok?:%s wanted:%s to:%s\n",pr?"true":"false",wanted?"t":"f",to_json(jv).c_str());
		}
		if (jv)
			delete jv;
	}
	return 0;
}
