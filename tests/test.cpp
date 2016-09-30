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


// Note: we probably need some #ifdefs to work with other compilers than MSVC2013
//       since <filesystem> has been standardized since the release of this compiler.

#ifdef _MSC_VER
 #if _MSC_VER <= 1800
  #define STD_FS_TR2
 #else
  #if _MSC_VER <= 1900
   #define STD_FS_EXP
  #endif
 #endif
#endif

#ifdef STD_FS_TR2
 using namespace std::tr2::sys;
#else
 #ifdef STD_FS_EXP
  using namespace std::experimental::filesystem;
 #else
  using namespace std::filesystem;
 #endif
#endif

using namespace rpocojson;

bool node_diff=false;

int main(int argc,char **argv) {
	for (int i=1;i<argc;i++) {
		if (std::string("-node-diff")==argv[i]) {
			node_diff=true;
		}
	}

	path p="json";
	p/="json_parser";
	printf("%s\n",p.string().c_str());
	for (directory_iterator it=directory_iterator(p);it!=directory_iterator();++it) {
		if (it->path().extension()!=".json")
			continue;

		bool wanted=0==it->path().filename().string().find("valid-");
		bool extWanted = 0 == it->path().filename().string().find("ext-valid-");
		bool doExt = extWanted || (0==it->path().filename().string().find("ext-invalid-"));

		for (int i = 0; i< (doExt ? 2 : 1); i++) {
			json_value *jv = 0;
			bool pr = parse(std::ifstream(it->path().string().c_str()),jv,i==1);	
			bool curWanted = (i == 1 ? extWanted : wanted);
			if (curWanted == pr) {
				printf("%s was %s as expected%s\n",it->path().string().c_str(),pr ? "parsed" : "not parsed",i==1?" with extensions":"");
				if (pr && node_diff) {
					std::string outname = it->path().string() + ".out";
					{
						std::ofstream os(outname);
						os << to_json(jv);
					}
					std::string cmd = "node json_diff.js " + it->path().string() + " " + outname;
					std::system(cmd.c_str());
				}
			} else {
				printf("Error, %s was unexpectedly %s\n",it->path().string().c_str(),pr ? "parsed" : "not parsed");
				printf("parsed ok?:%s wanted:%s to:%s\n",pr ? "true" : "false",curWanted ? "t" : "f",to_json(jv).c_str());
				return -1;
			}
			if (jv)
				delete jv;
		}
	}
	return 0;
}
