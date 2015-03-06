// First very simple example of parsing and writing JSON data

#include <rpoco/rpocojson.hpp>

struct Ser1 {
	std::string hello;
	int x=0;

	RPOCO(hello,x);
};

int main(int argc,char **argv) {

	// declare structure
	Ser1 d1;

	// sample data
	std::string sampleData="{ \"hello\":\"world\"  ,  \"x\":123  }";

	// parse in data
	rpocojson::parse(sampleData,d1);
	
	// now write the data from the object back to a new json string
	std::string d1str=rpocojson::to_json(d1);

	// and print it
	printf("%s\n",d1str.c_str());

	return 0;
}
